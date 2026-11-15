/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * A fake server for the update tests, following "Server to device: firmware images" in docs/protocol/README.md, and a
 * stand-in for MCUboot that does to the two slots what MCUboot does at a restart.
 */

#include <string.h>

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "core.h"
#include "fake.h"
#include "ota.h"
#include "sha256.h"
#include "transport.h"
#include "wire.h"

#define RECORD_NAME "tracegale/ota"
/* MCUboot's trailer, counted from the end of a slot. */
#define TRAILER_MAGIC 16
#define TRAILER_IMAGE_OK 24
#define TRAILER_COPY_DONE 32

static const uint8_t magic[16] = {0x77, 0xc2, 0x95, 0xf3, 0x60, 0xd2, 0xef, 0x7f,
				  0x35, 0x52, 0x50, 0x0f, 0x2c, 0xb6, 0x79, 0x80};

struct fake_server srv;
struct restarts restarts;
bool bootloader_refuses;

/* The image that ran before the one on trial, which MCUboot keeps in the second slot. */
static uint8_t previous[IMAGE_MAX];
static uint8_t moving[IMAGE_MAX];

/* ---- The server ---- */

static void reply_ack(uint8_t *rx, size_t cap, size_t *rx_len, uint32_t id, bool pending)
{
	struct tg_writer w;

	tg_write_begin(&w, rx, cap, TG_ACK, id, pending ? 1 : 0);
	if (pending) {
		tg_write_uint(&w, 0, TG_PENDING_UPDATE);
	}
	*rx_len = w.len;
}

static void reply_error(uint8_t *rx, size_t cap, size_t *rx_len, uint32_t id, uint32_t code)
{
	struct tg_writer w;

	tg_write_begin(&w, rx, cap, TG_ERROR, id, 1);
	tg_write_uint(&w, 0, code);
	*rx_len = w.len;
}

static int dropped(int *detail)
{
	*detail = 113;
	return -TRACEGALE_ERR_TIMEOUT;
}

static int fake_exchange(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t cap, size_t *rx_len, int *detail)
{
	struct tg_message m;
	struct tg_writer w;
	uint32_t image_ref;
	uint32_t at;
	uint32_t len;

	zassert_ok(tg_read_message(tx, tx_len, &m), "the device sent a message that breaks the specification");
	switch (m.type) {
	case TG_HELLO:
		srv.hellos++;
		reply_ack(rx, cap, rx_len, m.id, srv.pending);
		return 0;
	case TG_IMG_CHECK:
		srv.checks++;
		srv.first_seen = false;
		if (srv.error_on_check != 0) {
			reply_error(rx, cap, rx_len, m.id, srv.error_on_check);
			return 0;
		}
		if (!srv.offer) {
			reply_ack(rx, cap, rx_len, m.id, false);
			return 0;
		}
		tg_write_begin(&w, rx, cap, TG_IMG_OFFER, m.id, 5);
		tg_write_uint(&w, 0, srv.image_ref);
		tg_write_uint(&w, 1, srv.total_len);
		tg_write_uint(&w, 2, srv.kind);
		tg_write_text(&w, 4, "1.3.0");
		tg_write_bytes(&w, 5, srv.sha, sizeof(srv.sha));
		zassert_false(w.overflow);
		*rx_len = w.len;
		return 0;
	case TG_IMG_GET:
		srv.gets++;
		if (srv.drop_get_at == srv.gets) {
			return dropped(detail);
		}
		if (srv.on_get != NULL) {
			srv.on_get();
		}
		zassert_true(tg_read_uint(&m, 0, &image_ref) && tg_read_uint(&m, 1, &at) && tg_read_uint(&m, 2, &len));
		if (srv.error_on_get != 0) {
			reply_error(rx, cap, rx_len, m.id, srv.error_on_get);
			return 0;
		}
		if (!srv.offer || image_ref != srv.image_ref) {
			reply_error(rx, cap, rx_len, m.id, TG_WIRE_ERR_UNKNOWN_IMAGE);
			return 0;
		}
		if (at > srv.total_len) {
			reply_error(rx, cap, rx_len, m.id, TG_WIRE_ERR_BAD_OFFSET);
			return 0;
		}
		zassert_true(len > 0 && len <= BLOCK, "the device asked for %u bytes", len);
		zassert_equal(at % 8, 0, "a download continues at a whole write block, not at %u", at);
		if (!srv.first_seen) {
			srv.first_seen = true;
			srv.first_offset = at;
		}
		len = MIN(MIN(len, srv.chunk_max), srv.total_len - at);
		tg_write_begin(&w, rx, cap, TG_IMG_DATA, m.id, 3);
		tg_write_uint(&w, 0, srv.wrong_ref ? image_ref + 1 : image_ref);
		tg_write_uint(&w, 1, srv.wrong_offset ? at + 8 : at);

		uint8_t *data = tg_write_bytes_reserve(&w, 2, len);

		zassert_true(data != NULL && !w.overflow, "the reply fits the size the device announced");
		memcpy(data, &srv.image[at], len);
		if (srv.corrupt_at >= (int)at && srv.corrupt_at < (int)(at + len)) {
			data[srv.corrupt_at - at] ^= 0x01;
		}
		srv.bytes_sent += len;
		*rx_len = w.len;
		return 0;
	case TG_IMG_RESULT:
		srv.results++;
		if (srv.drop_result_at == srv.results) {
			return dropped(detail);
		}
		zassert_true(srv.results <= RESULTS);
		zassert_true(tg_read_uint(&m, 0, &srv.result_ref[srv.results - 1]));
		zassert_true(tg_read_uint(&m, 1, &srv.result_outcome[srv.results - 1]));
		if (srv.error_on_result != 0) {
			reply_error(rx, cap, rx_len, m.id, srv.error_on_result);
			return 0;
		}
		reply_ack(rx, cap, rx_len, m.id, false);
		return 0;
	default:
		zassert_unreachable("the device sent message type %u", m.type);
		return -TRACEGALE_ERR_BAD_REPLY;
	}
}

static int fake_open(int *detail)
{
	if (srv.offline) {
		*detail = 101;
		return -TRACEGALE_ERR_CONNECT;
	}
	return 0;
}

static void fake_close(void)
{
}

static const struct tg_transport fake = {.open = fake_open, .exchange = fake_exchange, .close = fake_close};

const struct tg_transport *tg_transport_get(void)
{
	return &fake;
}

void fake_server_reset(void)
{
	memset(&srv, 0, sizeof(srv));
	srv.chunk_max = UINT32_MAX;
	srv.corrupt_at = -1;
}

void offer(const uint8_t *image, size_t len, uint32_t image_ref)
{
	struct tg_sha256 ctx;

	srv.pending = true;
	srv.offer = true;
	srv.image = image;
	srv.total_len = len;
	srv.image_ref = image_ref;
	tg_sha256_init(&ctx);
	tg_sha256_update(&ctx, image, len);
	tg_sha256_final(&ctx, srv.sha);
}

uint32_t last_outcome(void)
{
	return srv.results == 0 ? 0 : srv.result_outcome[srv.results - 1];
}

/* ---- The device ---- */

void tracegale_update_restart(enum tracegale_restart_reason reason)
{
	if (reason == TRACEGALE_RESTART_INSTALL) {
		restarts.install++;
	} else {
		restarts.revert++;
	}
}

int checkin(void)
{
	return tg_core_attempt();
}

enum tracegale_update_state update_state(void)
{
	struct tracegale_status status;

	tracegale_get_status(&status);
	return status.update_state;
}

enum tracegale_update_result update_result(void)
{
	struct tracegale_status status;

	tracegale_get_status(&status);
	return status.update_result;
}

uint32_t update_offset(void)
{
	struct tracegale_status status;

	tracegale_get_status(&status);
	return status.update_offset;
}

size_t image_start(void)
{
	return IS_ENABLED(CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_USING_OFFSET) ? PAGE : 0;
}

size_t slot_room(void)
{
	const struct flash_area *fa;
	size_t size;

	zassert_ok(flash_area_open(SLOT1, &fa));
	size = fa->fa_size;
	flash_area_close(fa);
	return size - image_start();
}

void make_image(uint8_t *out, size_t len, uint8_t minor, uint32_t seed)
{
	uint32_t x = seed * 2654435761U + 1;

	for (size_t i = 0; i < len; i++) {
		x = x * 1664525U + 1013904223U;
		out[i] = x >> 24;
	}
	memset(out, 0, 32);
	sys_put_le32(0x96f3b83dU, out);
	/* Major, minor, revision and build. */
	out[20] = 1;
	out[21] = minor;
}

static const struct flash_area *open_area(uint8_t area)
{
	const struct flash_area *fa;

	zassert_ok(flash_area_open(area, &fa));
	return fa;
}

static void erase_slot(uint8_t area)
{
	const struct flash_area *fa = open_area(area);

	zassert_ok(flash_area_erase(fa, 0, fa->fa_size));
	flash_area_close(fa);
}

void scribble(uint8_t area, size_t at, const uint8_t *data, size_t len)
{
	const struct flash_area *fa = open_area(area);
	uint8_t block[8];

	zassert_equal(at % sizeof(block), 0);
	zassert_ok(flash_area_write(fa, at, data, ROUND_DOWN(len, sizeof(block))));
	if (len % sizeof(block) != 0) {
		memset(block, 0xff, sizeof(block));
		memcpy(block, &data[ROUND_DOWN(len, sizeof(block))], len % sizeof(block));
		zassert_ok(flash_area_write(fa, at + ROUND_DOWN(len, sizeof(block)), block, sizeof(block)));
	}
	flash_area_close(fa);
}

bool in_slot(uint8_t area, size_t at, const uint8_t *data, size_t len)
{
	const struct flash_area *fa = open_area(area);
	uint8_t block[64];
	bool same = true;

	for (size_t done = 0; same && done < len; done += sizeof(block)) {
		size_t n = MIN(sizeof(block), len - done);

		zassert_ok(flash_area_read(fa, at + done, block, n));
		same = memcmp(block, &data[done], n) == 0;
	}
	flash_area_close(fa);
	return same;
}

bool blank_from(uint8_t area, size_t at)
{
	const struct flash_area *fa = open_area(area);
	uint8_t block[64];
	bool blank = true;

	for (; blank && at < fa->fa_size; at += sizeof(block)) {
		size_t n = MIN(sizeof(block), fa->fa_size - at);

		zassert_ok(flash_area_read(fa, at, block, n));
		for (size_t i = 0; i < n; i++) {
			blank = blank && block[i] == 0xff;
		}
	}
	flash_area_close(fa);
	return blank;
}

static void trailer_read(uint8_t area, size_t from_end, uint8_t *out, size_t len)
{
	const struct flash_area *fa = open_area(area);

	zassert_ok(flash_area_read(fa, fa->fa_size - from_end, out, len));
	flash_area_close(fa);
}

static void trailer_write(uint8_t area, size_t from_end, const uint8_t *data, size_t len)
{
	const struct flash_area *fa = open_area(area);
	size_t at = fa->fa_size - from_end;

	flash_area_close(fa);
	scribble(area, at, data, len);
}

static bool has_magic(uint8_t area)
{
	uint8_t found[sizeof(magic)];

	trailer_read(area, TRAILER_MAGIC, found, sizeof(found));
	return memcmp(found, magic, sizeof(magic)) == 0;
}

static void read_image(uint8_t area, size_t at, uint8_t *out)
{
	const struct flash_area *fa = open_area(area);

	zassert_ok(flash_area_read(fa, at, out, IMAGE_MAX));
	flash_area_close(fa);
}

/* What MCUboot does before the application starts. */
static void bootloader(void)
{
	static const uint8_t set = 0x01;
	uint8_t flag;

	if (has_magic(SLOT1)) {
		/* An image was requested. */
		if (bootloader_refuses) {
			bootloader_refuses = false;
			erase_slot(SLOT1);
			return;
		}
		read_image(SLOT1, image_start(), moving);
		read_image(SLOT0, 0, previous);
		erase_slot(SLOT0);
		erase_slot(SLOT1);
		scribble(SLOT0, 0, moving, IMAGE_MAX);
		if (IS_ENABLED(CONFIG_MCUBOOT_BOOTLOADER_MODE_OVERWRITE_ONLY)) {
			/* Nothing is kept of the previous image, and the new one isn't on trial. */
			return;
		}
		scribble(SLOT1, image_start(), previous, IMAGE_MAX);
		trailer_write(SLOT0, TRAILER_MAGIC, magic, sizeof(magic));
		trailer_write(SLOT0, TRAILER_COPY_DONE, &set, 1);
		return;
	}
	if (!has_magic(SLOT0)) {
		return;
	}
	trailer_read(SLOT0, TRAILER_IMAGE_OK, &flag, 1);
	if (flag != set) {
		/* The image on trial wasn't confirmed, so the previous one comes back, for good. */
		erase_slot(SLOT0);
		erase_slot(SLOT1);
		scribble(SLOT0, 0, previous, IMAGE_MAX);
		trailer_write(SLOT0, TRAILER_MAGIC, magic, sizeof(magic));
		trailer_write(SLOT0, TRAILER_COPY_DONE, &set, 1);
		trailer_write(SLOT0, TRAILER_IMAGE_OK, &set, 1);
	}
}

void device_restart(void)
{
	bootloader();
	/* RAM starts again. */
	tracegale_update_pause(false);
	tg_ota_boot();
}

void device_reset(const uint8_t *running, size_t len)
{
	erase_slot(SLOT0);
	erase_slot(SLOT1);
	scribble(SLOT0, 0, running, len);
	(void)settings_delete(RECORD_NAME);
	bootloader_refuses = false;
	device_restart();
	/* A trial that an earlier test left behind ends before this test starts. */
	k_sleep(K_SECONDS(CONFIG_TRACEGALE_OTA_CONFIRM_TIMEOUT_S + 1));
	memset(&restarts, 0, sizeof(restarts));
	device_restart();
}

void confirm_running(void)
{
	zassert_ok(boot_write_img_confirmed());
}

static struct record found;
static bool found_any;

static int record_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param)
{
	ARG_UNUSED(key);
	ARG_UNUSED(param);
	zassert_equal(len, sizeof(found), "the stored record changed size");
	zassert_equal(read_cb(cb_arg, &found, len), (ssize_t)len);
	found_any = true;
	return 0;
}

bool record_read(struct record *out)
{
	found_any = false;
	zassert_ok(settings_load_subtree_direct(RECORD_NAME, record_cb, NULL));
	if (found_any) {
		*out = found;
	}
	return found_any;
}

void record_write(const void *in, size_t len)
{
	zassert_ok(settings_save_one(RECORD_NAME, in, len));
}
