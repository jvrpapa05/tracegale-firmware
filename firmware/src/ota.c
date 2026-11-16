/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Over-the-air updates, using "Server to device: firmware images" in docs/protocol/README.md.
 *
 * Tracegale only moves bytes. It downloads an image into MCUboot's second slot and asks MCUboot to try it. Whether the
 * image runs is decided by MCUboot's signature check alone, with a key the server never has, so nothing here is a
 * security check of the image (threat T1). What this file does guard is the device: against a download that's damaged,
 * against an older image offered again, and against a server that makes it erase its flash again and again.
 *
 * The position of a download is found from flash, so a download continues after a dropped connection, a restart or a
 * power loss. A small record in settings storage says which image the slot is for, and what became of it.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include <tracegale/tracegale.h>

#include "build_id.h"
#include "compat.h"
#include "core.h"
#include "ota.h"
#include "sha256.h"
#include "wire.h"

LOG_MODULE_DECLARE(tracegale, CONFIG_TRACEGALE_LOG_LEVEL);

/* The slot the running image is in, and the slot an update is downloaded into. */
#define SLOT_RUNNING TG_PARTITION_ID(slot0_partition)
#define SLOT_UPDATE TG_PARTITION_ID(slot1_partition)

#define STATE_NAME "tracegale/ota"
#define STATE_VERSION 1
/* The most an IMG_DATA adds around its data, from "Cost per message" in docs/protocol/README.md. */
#define DATA_OVERHEAD 25
/* MCUboot's image header: a magic number first, and the version at byte 20. */
#define HEADER_SIZE 32
#define HEADER_MAGIC 0x96f3b83dU
#define HEADER_VERSION 20
/* Give up on a server that keeps sending no usable bytes. */
#define MAX_STALLS 3
/* The longest wait after failed updates, however many there were. */
#define HOLD_MAX_S 86400U

enum phase {
	PHASE_NONE = TRACEGALE_UPDATE_NONE,
	/* The second slot was erased for the image in this record, and holds some or all of it. */
	PHASE_DOWNLOADING = TRACEGALE_UPDATE_DOWNLOADING,
	/* MCUboot was asked to try the image at the next restart. */
	PHASE_INSTALLING = TRACEGALE_UPDATE_RESTART_PENDING,
	/* The image is running, and MCUboot brings the previous one back at the next restart unless it's confirmed. */
	PHASE_TESTING = TRACEGALE_UPDATE_TESTING,
	/* The outcome is known, and the server hasn't been told yet. */
	PHASE_RESULT = TRACEGALE_UPDATE_REPORTING,
};

/* What's kept in settings storage. After the result was sent, the rest stays, so the same bad image isn't tried forever. */
struct ota_state {
	uint8_t version;
	uint8_t phase;
	/* How the image in this record ended, as sent in IMG_RESULT. */
	uint8_t outcome;
	/* Failed attempts at the image in this record. */
	uint8_t failures;
	/* Failed updates in a row, of any image. */
	uint8_t streak;
	uint8_t reserved[3];
	uint32_t image_ref;
	uint32_t total_len;
	uint8_t sha256[TG_SHA256_SIZE];
};

BUILD_ASSERT(sizeof(struct ota_state) == 48, "the stored record must not change size");

struct offer {
	uint32_t image_ref;
	uint32_t total_len;
	uint32_t kind;
	uint8_t sha256[TG_SHA256_SIZE];
};

static struct ota_state st;
/* How much of the image is in the slot. Known after this boot erased the slot or looked at it. */
static uint32_t offset;
static bool offset_known;
static atomic_t paused;
/* When the wait after a failed update began: at that failure, or at boot if the failure was before it. */
static int64_t hold_from;

__weak void tracegale_update_restart(enum tracegale_restart_reason reason)
{
	ARG_UNUSED(reason);
	LOG_PANIC();
	sys_reboot(SYS_REBOOT_WARM);
}

void tracegale_update_pause(bool pause)
{
	atomic_set(&paused, pause ? 1 : 0);
	if (!pause && st.phase == PHASE_DOWNLOADING) {
		/* Continues at once, on a link that is up. */
		tg_core_wake();
	}
}

static int save(void)
{
	int rc = settings_save_one(STATE_NAME, &st, sizeof(st));

	if (rc != 0) {
		LOG_ERR("Can't store the update's state: %d", rc);
	}
	return rc;
}

/* Records how an update ended. It's sent to the server by send_result(). */
static void conclude(uint8_t outcome)
{
	st.phase = PHASE_RESULT;
	st.outcome = outcome;
	if (outcome == TRACEGALE_UPDATE_INSTALLED) {
		st.failures = 0;
		st.streak = 0;
	} else {
		if (outcome == TRACEGALE_UPDATE_NOT_AN_IMAGE || outcome == TRACEGALE_UPDATE_DOWNGRADE) {
			/* Trying this image again can't end differently. */
			st.failures = UINT8_MAX;
		} else if (st.failures < UINT8_MAX) {
			st.failures++;
		}
		if (st.streak < UINT8_MAX) {
			st.streak++;
		}
		hold_from = k_uptime_get();
	}
	offset_known = false;
	(void)save();
}

/*
 * After failed updates, a new one waits: by default an hour after the first failure, and twice as long after each
 * further one. The time is counted from the failure. A failure that ended in a restart is counted from that boot, since
 * a device without a clock can't tell how long it was off. This bounds how often a server, even a breached one, can make
 * a device erase its flash and restart.
 */
static bool held(void)
{
#if CONFIG_TRACEGALE_OTA_HOLD_S > 0
	uint32_t hold = CONFIG_TRACEGALE_OTA_HOLD_S;

	if (st.streak == 0) {
		return false;
	}
	for (uint8_t i = 1; i < st.streak && hold < HOLD_MAX_S; i++) {
		hold *= 2;
	}
	return (k_uptime_get() - hold_from) / MSEC_PER_SEC < MIN(hold, HOLD_MAX_S);
#else
	return false;
#endif
}

static int hash_area(const struct flash_area *fa, size_t start, size_t len, uint8_t *scratch, size_t cap,
		     uint8_t digest[TG_SHA256_SIZE])
{
	struct tg_sha256 ctx;

	tg_sha256_init(&ctx);
	for (size_t done = 0; done < len;) {
		size_t n = MIN(cap, len - done);

		if (flash_area_read(fa, start + done, scratch, n) != 0) {
			return -TRACEGALE_ERR_STORAGE;
		}
		tg_sha256_update(&ctx, scratch, n);
		done += n;
	}
	tg_sha256_final(&ctx, digest);
	return 0;
}

/* @return true if the running image is the one with this size and SHA-256. */
static bool running_is(const uint8_t sha256[TG_SHA256_SIZE], size_t len, uint8_t *scratch, size_t cap)
{
	uint8_t digest[TG_SHA256_SIZE];
	const struct flash_area *fa;
	bool same = false;

	if (flash_area_open(SLOT_RUNNING, &fa) != 0) {
		return false;
	}
	if (len >= HEADER_SIZE && len <= fa->fa_size && hash_area(fa, 0, len, scratch, cap, digest) == 0) {
		same = memcmp(digest, sha256, sizeof(digest)) == 0;
	}
	flash_area_close(fa);
	return same;
}

/* Where an image starts in the second slot. One of MCUboot's modes keeps the slot's first sector free. */
static size_t slot_start(void)
{
#if defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_USING_OFFSET)
	return boot_get_image_start_offset(SLOT_UPDATE);
#else
	return 0;
#endif
}

/* Erases every flash page that has a part in this range of the slot. */
static int erase_pages(const struct flash_area *fa, size_t from, size_t to)
{
	const struct device *dev = flash_area_get_device(fa);

	while (from < to) {
		struct flash_pages_info page;
		int rc = flash_get_page_info_by_offs(dev, fa->fa_off + from, &page);

		if (rc != 0 || page.start_offset < fa->fa_off || page.size == 0) {
			return -TRACEGALE_ERR_STORAGE;
		}
		size_t begin = page.start_offset - fa->fa_off;
		size_t len = MIN(page.size, fa->fa_size - begin);

		if (flash_area_flatten(fa, begin, len) != 0) {
			return -TRACEGALE_ERR_STORAGE;
		}
		from = begin + len;
	}
	return 0;
}

static int is_blank(const struct flash_area *fa, size_t from, size_t to, uint8_t *scratch, size_t cap, bool *blank)
{
	uint8_t erased = flash_area_erased_val(fa);

	*blank = true;
	while (from < to) {
		size_t n = MIN(cap, to - from);

		if (flash_area_read(fa, from, scratch, n) != 0) {
			return -TRACEGALE_ERR_STORAGE;
		}
		for (size_t i = 0; i < n; i++) {
			if (scratch[i] != erased) {
				*blank = false;
				return 0;
			}
		}
		from += n;
	}
	return 0;
}

/*
 * Finds how much of the image is in the slot, after a restart. The slot was erased before the download began, so
 * everything after the last page with data in it is still blank. That last page may hold a write that a power loss cut
 * short, so it's erased and downloaded again, along with what a single write after it could have touched.
 */
static int find_offset(const struct flash_area *fa, size_t start, uint8_t *scratch, size_t cap)
{
	const struct device *dev = flash_area_get_device(fa);
	size_t end = start + st.total_len;
	size_t pos = end;

	offset = 0;
	while (pos > 0) {
		struct flash_pages_info page;
		bool blank;
		int rc = flash_get_page_info_by_offs(dev, fa->fa_off + pos - 1, &page);

		if (rc != 0 || page.start_offset < fa->fa_off || page.size == 0) {
			return -TRACEGALE_ERR_STORAGE;
		}
		size_t begin = page.start_offset - fa->fa_off;

		rc = is_blank(fa, begin, MIN(begin + page.size, fa->fa_size), scratch, cap, &blank);
		if (rc != 0) {
			return rc;
		}
		if (!blank) {
			rc = erase_pages(fa, begin, MIN(begin + page.size + cap, fa->fa_size));
			if (rc != 0) {
				return rc;
			}
			offset = begin > start ? begin - start : 0;
			break;
		}
		pos = begin;
	}
	offset_known = true;
	return 0;
}

/* Sends one message and reads the reply. Bad data can only fail this attempt. */
static int exchange(const struct tg_transport *transport, uint8_t *buf, size_t cap, size_t len, uint32_t id,
		    struct tg_message *reply, int *detail)
{
	size_t reply_len = 0;
	uint32_t code = 0;
	int rc = transport->exchange(buf, len, buf, cap, &reply_len, detail);

	if (rc != 0) {
		return rc;
	}
	if (tg_read_message(buf, reply_len, reply) != 0 || reply->id != id) {
		return -TRACEGALE_ERR_BAD_REPLY;
	}
	if (reply->type == TG_ERROR) {
		(void)tg_read_uint(reply, 0, &code);
		*detail = code;
		return -TRACEGALE_ERR_SERVER;
	}
	return 0;
}

/* @return true for a refusal that isn't worth failing the check-in for, because asking again soon can't help. */
static bool refused_for_good(int rc, int detail)
{
	return rc == -TRACEGALE_ERR_SERVER && detail != TG_WIRE_ERR_RATE_LIMITED && detail != TG_WIRE_ERR_SERVER_BUSY;
}

static int send_outcome(const struct tg_transport *transport, uint8_t *buf, size_t cap, uint32_t id, uint32_t image_ref,
			uint8_t outcome, int *detail)
{
	struct tg_message reply;
	struct tg_writer w;
	int rc;

	tg_write_begin(&w, buf, cap, TG_IMG_RESULT, id, 2);
	tg_write_uint(&w, 0, image_ref);
	tg_write_uint(&w, 1, outcome);
	if (w.overflow) {
		return -TRACEGALE_ERR_TOO_LARGE;
	}
	LOG_INF("Update %u: reporting outcome %u, message %u", image_ref, outcome, id);
	rc = exchange(transport, buf, cap, w.len, id, &reply, detail);
	if (rc == 0 && reply.type != TG_ACK) {
		rc = -TRACEGALE_ERR_BAD_REPLY;
	}
	if (refused_for_good(rc, *detail)) {
		/* For example, the rollout was deleted, so nobody is waiting for this. */
		LOG_WRN("Server didn't take the update's outcome: error %d", *detail);
		rc = 0;
	}
	return rc;
}

/* Sends the recorded outcome, and marks it sent. */
static int send_result(const struct tg_transport *transport, uint8_t *buf, size_t cap, uint32_t id, int *detail)
{
	int rc = send_outcome(transport, buf, cap, id, st.image_ref, st.outcome, detail);

	if (rc == 0) {
		st.phase = PHASE_NONE;
		(void)save();
	}
	return rc;
}

static int check(const struct tg_transport *transport, uint8_t *buf, size_t cap, uint32_t id, struct offer *offer,
		 bool *offered, int *detail)
{
	const uint8_t *build_id;
	size_t build_id_len = tg_build_id(&build_id);
	const uint8_t *sha256;
	size_t sha256_len;
	struct tg_message reply;
	struct tg_writer w;
	int rc;

	*offered = false;
	tg_write_begin(&w, buf, cap, TG_IMG_CHECK, id, 3);
	tg_write_text(&w, 0, tg_core_fw_version());
	tg_write_bytes(&w, 1, build_id, build_id_len);
	tg_write_text(&w, 2, CONFIG_TRACEGALE_HARDWARE);
	if (w.overflow) {
		return -TRACEGALE_ERR_TOO_LARGE;
	}
	LOG_DBG("Asking for an update, message %u", id);
	rc = exchange(transport, buf, cap, w.len, id, &reply, detail);
	if (rc != 0 || reply.type == TG_ACK) {
		return rc;
	}
	if (reply.type != TG_IMG_OFFER || !tg_read_uint(&reply, 0, &offer->image_ref) ||
	    !tg_read_uint(&reply, 2, &offer->kind)) {
		return -TRACEGALE_ERR_BAD_REPLY;
	}
	*offered = true;
	if (offer->kind != 0) {
		/* A kind this firmware doesn't know may have other fields. */
		return 0;
	}
	if (!tg_read_uint(&reply, 1, &offer->total_len) || !tg_read_bytes(&reply, 5, &sha256, &sha256_len) ||
	    sha256_len != sizeof(offer->sha256)) {
		return -TRACEGALE_ERR_BAD_REPLY;
	}
	memcpy(offer->sha256, sha256, sizeof(offer->sha256));
	return 0;
}

#if !defined(CONFIG_TRACEGALE_OTA_DOWNGRADE)
/* Compares two MCUboot image versions: major, minor, revision, build. */
static int version_compare(const uint8_t *a, const uint8_t *b)
{
	uint32_t parts_a[] = {a[0], a[1], sys_get_le16(&a[2]), sys_get_le32(&a[4])};
	uint32_t parts_b[] = {b[0], b[1], sys_get_le16(&b[2]), sys_get_le32(&b[4])};

	for (size_t i = 0; i < ARRAY_SIZE(parts_a); i++) {
		if (parts_a[i] != parts_b[i]) {
			return parts_a[i] < parts_b[i] ? -1 : 1;
		}
	}
	return 0;
}
#endif

/* Looks at the start of a new image before any of it is written. @return 0, or the outcome that ends this update. */
static uint8_t header_check(const uint8_t *header, const struct flash_area *running)
{
	if (sys_get_le32(header) != HEADER_MAGIC) {
		return TRACEGALE_UPDATE_NOT_AN_IMAGE;
	}
#if !defined(CONFIG_TRACEGALE_OTA_DOWNGRADE)
	uint8_t mine[HEADER_SIZE];

	/* An image this device can't compare with its own is refused, since it might be an older one. */
	if (flash_area_read(running, 0, mine, sizeof(mine)) != 0 || sys_get_le32(mine) != HEADER_MAGIC) {
		return TRACEGALE_UPDATE_STORAGE;
	}
	if (version_compare(&header[HEADER_VERSION], &mine[HEADER_VERSION]) < 0) {
		return TRACEGALE_UPDATE_DOWNGRADE;
	}
#else
	ARG_UNUSED(running);
#endif
	return 0;
}

/*
 * Downloads from where the slot ends, until the image is complete, the application pauses updates, or something fails.
 *
 * @return 0 with *outcome 0 when it stopped or finished normally, 0 with an outcome that ends this update, or a negative
 *         tracegale_error for a failure that's worth another try.
 */
static int download(const struct tg_transport *transport, uint8_t *buf, size_t cap, uint32_t (*next_id)(void),
		    const struct flash_area *slot, const struct flash_area *running, size_t start, uint8_t *outcome,
		    int *detail)
{
	size_t align = MAX(flash_area_align(slot), 1U);
	size_t want = ROUND_DOWN(cap - DATA_OVERHEAD, align);
	uint32_t stalls = 0;

	if (want < HEADER_SIZE) {
		return -TRACEGALE_ERR_TOO_LARGE;
	}
	while (offset < st.total_len && atomic_get(&paused) == 0) {
		size_t len = MIN(want, st.total_len - offset);
		struct tg_message reply;
		uint32_t id = next_id();
		uint32_t image_ref;
		uint32_t at;
		const uint8_t *data;
		size_t n;
		struct tg_writer w;
		int rc;

		tg_write_begin(&w, buf, cap, TG_IMG_GET, id, 3);
		tg_write_uint(&w, 0, st.image_ref);
		tg_write_uint(&w, 1, offset);
		tg_write_uint(&w, 2, len);
		if (w.overflow) {
			return -TRACEGALE_ERR_TOO_LARGE;
		}
		rc = exchange(transport, buf, cap, w.len, id, &reply, detail);
		if (rc != 0) {
			return rc;
		}
		if (reply.type != TG_IMG_DATA || !tg_read_uint(&reply, 0, &image_ref) || image_ref != st.image_ref ||
		    !tg_read_uint(&reply, 1, &at) || at != offset || !tg_read_bytes(&reply, 2, &data, &n) || n > len) {
			return -TRACEGALE_ERR_BAD_REPLY;
		}
		/* Flash is written in whole blocks, so a short reply's odd bytes are asked for again. */
		size_t usable = offset + n == st.total_len ? n : ROUND_DOWN(n, align);

		if (offset == 0 && usable < HEADER_SIZE) {
			usable = 0;
		}
		if (usable == 0) {
			if (++stalls >= MAX_STALLS) {
				return -TRACEGALE_ERR_BAD_REPLY;
			}
			continue;
		}
		stalls = 0;
		/* The data sits inside the reply. At the start of the buffer there's room to fill the last block. */
		memmove(buf, data, usable);
		if (offset == 0) {
			*outcome = header_check(buf, running);
			if (*outcome != 0) {
				return 0;
			}
		}
		size_t padded = ROUND_UP(usable, align);

		memset(&buf[usable], flash_area_erased_val(slot), padded - usable);
		if (flash_area_write(slot, start + offset, buf, padded) != 0) {
			LOG_ERR("Update %u: can't write flash at %u", st.image_ref, (unsigned int)(start + offset));
			*outcome = TRACEGALE_UPDATE_STORAGE;
			return 0;
		}
		offset += usable;
		LOG_DBG("Update %u: %u of %u bytes", st.image_ref, offset, st.total_len);
	}
	return 0;
}

/* The whole image is in the slot. @return 0 if MCUboot will try it at the next restart, or the outcome that ends it. */
static uint8_t install(const struct flash_area *slot, size_t start, uint8_t *scratch, size_t cap)
{
	uint8_t digest[TG_SHA256_SIZE];

	if (hash_area(slot, start, st.total_len, scratch, cap, digest) != 0) {
		return TRACEGALE_UPDATE_STORAGE;
	}
	if (memcmp(digest, st.sha256, sizeof(digest)) != 0) {
		LOG_ERR("Update %u: what was downloaded isn't the image that was offered", st.image_ref);
		return TRACEGALE_UPDATE_DAMAGED;
	}
	/* Asked for before it's recorded. If power is lost between the two, tg_ota_boot() still finds the new image
	 * running. The other order would report a failure for an image that was never tried.
	 */
	if (boot_request_upgrade(BOOT_UPGRADE_TEST) != 0) {
		return TRACEGALE_UPDATE_STORAGE;
	}
	st.phase = PHASE_INSTALLING;
	(void)save();
	LOG_INF("Update %u: downloaded and intact. It's installed at the next restart", st.image_ref);
	return 0;
}

/* Answers an offer without touching flash. */
static int decline(const struct tg_transport *transport, uint8_t *buf, size_t cap, uint32_t id,
		   const struct offer *offer, uint8_t outcome, int *detail)
{
	return send_outcome(transport, buf, cap, id, offer->image_ref, outcome, detail);
}

/* Erases the slot for a new image, and records which image it's for. @return 0, or the outcome that ends it. */
static uint8_t begin(const struct flash_area *slot, const struct offer *offer, bool same)
{
	uint8_t failures = same ? st.failures : 0;

	if (st.phase == PHASE_DOWNLOADING) {
		/* The record must stop describing the slot before the slot changes. */
		st.phase = PHASE_NONE;
		if (save() != 0) {
			return TRACEGALE_UPDATE_STORAGE;
		}
	}
	LOG_INF("Update %u: %u bytes. Erasing the slot for it", offer->image_ref, offer->total_len);
	/* All of it, including pages that read as blank: an erase that was cut short can leave such a page unreliable. */
	if (erase_pages(slot, 0, slot->fa_size) != 0) {
		LOG_ERR("Update %u: can't erase the slot", offer->image_ref);
		/* Not stored, since this flash is failing, but it still makes the next try wait. */
		st.streak = MIN(st.streak + 1, UINT8_MAX);
		hold_from = k_uptime_get();
		return TRACEGALE_UPDATE_STORAGE;
	}
	st.version = STATE_VERSION;
	st.phase = PHASE_DOWNLOADING;
	st.outcome = 0;
	st.failures = failures;
	st.image_ref = offer->image_ref;
	st.total_len = offer->total_len;
	memcpy(st.sha256, offer->sha256, sizeof(st.sha256));
	if (save() != 0) {
		st.phase = PHASE_NONE;
		return TRACEGALE_UPDATE_STORAGE;
	}
	offset = 0;
	offset_known = true;
	return 0;
}

static int take(const struct tg_transport *transport, uint8_t *buf, size_t cap, uint32_t (*next_id)(void),
		const struct offer *offer, int *detail)
{
	bool same = st.version == STATE_VERSION && memcmp(offer->sha256, st.sha256, sizeof(st.sha256)) == 0;
	bool resume = same && st.phase == PHASE_DOWNLOADING && st.total_len == offer->total_len;
	const struct flash_area *running = NULL;
	const struct flash_area *slot = NULL;
	uint8_t outcome = 0;
	int rc = 0;

	if (offer->kind != 0) {
		return decline(transport, buf, cap, next_id(), offer, TRACEGALE_UPDATE_UNSUPPORTED_KIND, detail);
	}
	if (same && !resume && st.failures >= CONFIG_TRACEGALE_OTA_MAX_ATTEMPTS) {
		LOG_WRN("Update %u: this image already failed here, so it's declined", offer->image_ref);
		return decline(transport, buf, cap, next_id(), offer, st.outcome, detail);
	}
	if (flash_area_open(SLOT_RUNNING, &running) != 0 || flash_area_open(SLOT_UPDATE, &slot) != 0) {
		return decline(transport, buf, cap, next_id(), offer, TRACEGALE_UPDATE_STORAGE, detail);
	}
	size_t start = slot_start();
	size_t room = slot->fa_size > start ? slot->fa_size - start : 0;

	if (offer->total_len < HEADER_SIZE) {
		rc = decline(transport, buf, cap, next_id(), offer, TRACEGALE_UPDATE_NOT_AN_IMAGE, detail);
		goto out;
	}
	if (offer->total_len > room || ROUND_UP(offer->total_len, MAX(flash_area_align(slot), 1U)) > room) {
		rc = decline(transport, buf, cap, next_id(), offer, TRACEGALE_UPDATE_TOO_LARGE, detail);
		goto out;
	}
	if (resume) {
		if (st.image_ref != offer->image_ref) {
			/* The same image under a new rollout. */
			st.image_ref = offer->image_ref;
			(void)save();
		}
		if (!offset_known && find_offset(slot, start, buf, cap) != 0) {
			outcome = TRACEGALE_UPDATE_STORAGE;
		}
		if (outcome == 0) {
			LOG_INF("Update %u: continuing at %u of %u bytes", st.image_ref, offset, st.total_len);
		}
	} else {
		if (held()) {
			LOG_INF("Update %u: waiting a while after the last failed update", offer->image_ref);
			goto out;
		}
		if (running_is(offer->sha256, offer->total_len, buf, cap)) {
			/* The device decides this, not the server: it's the one that knows what it runs. */
			rc = decline(transport, buf, cap, next_id(), offer, TRACEGALE_UPDATE_INSTALLED, detail);
			goto out;
		}
		outcome = begin(slot, offer, same);
		if (outcome != 0) {
			/* Nothing was recorded, so there's no update to end. */
			rc = decline(transport, buf, cap, next_id(), offer, outcome, detail);
			goto out;
		}
	}
	if (outcome == 0) {
		rc = download(transport, buf, cap, next_id, slot, running, start, &outcome, detail);
	}
	if (rc == 0 && outcome == 0 && offset == st.total_len) {
		outcome = install(slot, start, buf, cap);
	}
	if (rc == 0 && outcome != 0) {
		conclude(outcome);
		rc = send_result(transport, buf, cap, next_id(), detail);
	}
out:
	if (slot != NULL) {
		flash_area_close(slot);
	}
	if (running != NULL) {
		flash_area_close(running);
	}
	return rc;
}

int tg_ota_run(const struct tg_transport *transport, uint8_t *buf, size_t cap, uint32_t (*next_id)(void),
	       uint32_t pending, int *detail)
{
	if (cap <= DATA_OVERHEAD + HEADER_SIZE) {
		return 0;
	}
	if (st.phase == PHASE_RESULT) {
		/* Only this, in this check-in. A failed image is then not downloaded again straight away. */
		return send_result(transport, buf, cap, next_id(), detail);
	}
	if (st.phase == PHASE_INSTALLING || st.phase == PHASE_TESTING || atomic_get(&paused) != 0 ||
	    (pending & TG_PENDING_UPDATE) == 0U) {
		/* An unfinished download stays as it is. It continues when the server offers the same image again. */
		return 0;
	}
	if (st.phase != PHASE_DOWNLOADING && held()) {
		return 0;
	}
	for (int round = 0;; round++) {
		struct offer offer = {0};
		bool offered;
		int rc = check(transport, buf, cap, next_id(), &offer, &offered, detail);

		if (refused_for_good(rc, *detail)) {
			LOG_WRN("Server refused to say whether there's an update: error %d", *detail);
			return 0;
		}
		if (rc != 0 || !offered) {
			return rc;
		}
		rc = take(transport, buf, cap, next_id, &offer, detail);
		if (rc == -TRACEGALE_ERR_SERVER && round == 0 &&
		    (*detail == TG_WIRE_ERR_UNKNOWN_IMAGE || *detail == TG_WIRE_ERR_BAD_OFFSET)) {
			/* The rollout changed during the download: ask what to do now, once. */
			continue;
		}
		return rc;
	}
}

void tg_ota_connected(void)
{
	if (st.phase != PHASE_TESTING) {
		return;
	}
#if defined(CONFIG_TRACEGALE_OTA_CONFIRM)
	/* This image started, joined the network and reached the server, so it can be replaced again if it's bad. */
	if (!boot_is_img_confirmed() && boot_write_img_confirmed() != 0) {
		LOG_ERR("Can't confirm the update");
	}
#endif
	if (boot_is_img_confirmed()) {
		LOG_INF("Update %u: confirmed, so this image stays", st.image_ref);
		conclude(TRACEGALE_UPDATE_INSTALLED);
	}
}

void tg_ota_closed(void)
{
	if (st.phase == PHASE_INSTALLING) {
		LOG_INF("Update %u: restarting to install it", st.image_ref);
		tracegale_update_restart(TRACEGALE_RESTART_INSTALL);
	}
}

#if CONFIG_TRACEGALE_OTA_CONFIRM_TIMEOUT_S > 0
static void revert(struct k_work *work)
{
	ARG_UNUSED(work);
	if (st.phase == PHASE_TESTING && !boot_is_img_confirmed()) {
		LOG_ERR("Update %u: not confirmed after %d s. Restarting, so the bootloader brings the previous image back",
			st.image_ref, CONFIG_TRACEGALE_OTA_CONFIRM_TIMEOUT_S);
		tracegale_update_restart(TRACEGALE_RESTART_REVERT);
	}
}

static K_WORK_DELAYABLE_DEFINE(revert_work, revert);
#endif

static int state_read(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param)
{
	struct ota_state stored;

	ARG_UNUSED(key);
	ARG_UNUSED(param);
	if (len == sizeof(stored) && read_cb(cb_arg, &stored, sizeof(stored)) == (ssize_t)sizeof(stored) &&
	    stored.version == STATE_VERSION && stored.phase <= PHASE_RESULT) {
		st = stored;
	}
	return 0;
}

void tg_ota_boot(void)
{
	uint8_t scratch[128];

	memset(&st, 0, sizeof(st));
	offset_known = false;
	hold_from = k_uptime_get();
	(void)settings_load_subtree_direct(STATE_NAME, state_read, NULL);
	if (st.phase == PHASE_DOWNLOADING || st.phase == PHASE_INSTALLING || st.phase == PHASE_TESTING) {
		/* What's running tells what MCUboot did with the image, in every mode it has. */
		if (running_is(st.sha256, st.total_len, scratch, sizeof(scratch))) {
			if (st.phase != PHASE_TESTING) {
				LOG_INF("Update %u: the new image is running", st.image_ref);
				st.phase = PHASE_TESTING;
				(void)save();
			}
		} else if (st.phase == PHASE_TESTING) {
			LOG_ERR("Update %u: the bootloader brought the previous image back", st.image_ref);
			conclude(TRACEGALE_UPDATE_REVERTED);
		} else if (st.phase == PHASE_INSTALLING) {
			LOG_ERR("Update %u: the bootloader didn't start the new image", st.image_ref);
			conclude(TRACEGALE_UPDATE_NOT_STARTED);
		}
	}
#if CONFIG_TRACEGALE_OTA_CONFIRM_TIMEOUT_S > 0
	if (st.phase == PHASE_TESTING && !boot_is_img_confirmed()) {
		(void)k_work_reschedule(&revert_work, K_SECONDS(CONFIG_TRACEGALE_OTA_CONFIRM_TIMEOUT_S));
	}
#endif
}

void tg_ota_status(struct tracegale_status *out)
{
	out->update_state = (enum tracegale_update_state)st.phase;
	out->update_result = (enum tracegale_update_result)st.outcome;
	out->update_total = st.phase == PHASE_DOWNLOADING || st.phase == PHASE_INSTALLING ? st.total_len : 0;
	out->update_offset = 0;
	if (st.phase == PHASE_INSTALLING) {
		out->update_offset = st.total_len;
	} else if (st.phase == PHASE_DOWNLOADING && offset_known) {
		out->update_offset = offset;
	}
}
