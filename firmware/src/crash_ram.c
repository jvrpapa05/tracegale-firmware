/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tracegale's coredump store: Zephyr's coredump backend interface, kept in RAM that a reboot doesn't clear, so no flash
 * partition is needed. Power loss does clear it.
 *
 * Zephyr writes a dump as a file header followed by blocks: registers ('A'), thread metadata ('T'), CPU snapshots ('F')
 * and memory ('M'). This follows the blocks as they arrive, so that a dump too large for the store is cut at a block
 * boundary and still decodes: a memory block is shortened from its end, keeping its start, which for a stack is the
 * newest frames, and any other block that doesn't fit is left out. Format: include/zephyr/debug/coredump.h.
 *
 * Every dump ends with one more memory block, holding a crash counter and the cycle count. The server finds an upload by its
 * SHA-256, so two identical dumps would look like one being resumed, and the second crash would never be counted. A device
 * that crashes the same way at every boot does produce identical dumps.
 *
 * With CONFIG_TRACEGALE_CRASH_KEEP, a crash that couldn't be sent is also saved with the application's settings, at the
 * end of this file.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/debug/coredump.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

#if defined(CONFIG_TRACEGALE_CRASH_KEEP)
#include <zephyr/settings/settings.h>
#endif

#include "build_id.h"
#include "crash.h"

LOG_MODULE_DECLARE(tracegale, CONFIG_TRACEGALE_LOG_LEVEL);

#define STORE_MAGIC 0x54474352u /* "TGCR" */
#define FILE_HEADER_SIZE 12
#define MIN_MEMORY_KEPT 16

struct store {
	uint32_t magic;
	uint32_t crc;
	uint32_t length;
	uint8_t flags;
	uint8_t build_id_len;
	uint8_t reserved[2];
	uint8_t build_id[TG_BUILD_ID_MAX];
	uint8_t data[CONFIG_TRACEGALE_CRASH_RAM_SIZE];
};

static __noinit struct store store;

/* Garbage after power-on, which is fine: it only has to differ between crashes. */
static __noinit struct {
	uint32_t sequence;
	uint32_t cycles;
} mark;

/* Only used while a dump is being written, which happens with interrupts locked, once. */
static struct {
	enum { WANT_FILE, WANT_ID, WANT_HEADER, IN_PAYLOAD, STOPPED } state;
	uint8_t header[3 + 2 * 8];
	size_t have;
	size_t need;
	size_t ptr_size;
	uint64_t left;
	uint64_t keep;
} w;

static uint32_t store_crc(void)
{
	uint32_t crc = crc32_ieee_update(0, (const uint8_t *)&store.length,
					 offsetof(struct store, data) - offsetof(struct store, length));

	return crc32_ieee_update(crc, store.data, MIN(store.length, sizeof(store.data)));
}

static bool store_valid(void)
{
	return store.magic == STORE_MAGIC && store.length <= sizeof(store.data) && store.length >= FILE_HEADER_SIZE &&
	       store.build_id_len <= sizeof(store.build_id) && store.crc == store_crc();
}

/* The last block's size: its header, then the mark. */
static size_t mark_block_size(void)
{
	return 3 + 2 * w.ptr_size + sizeof(mark);
}

/* Room left for Zephyr's blocks, keeping space for the mark's block. */
static size_t room(void)
{
	size_t reserve = mark_block_size();

	return store.length + reserve >= sizeof(store.data) ? 0 : sizeof(store.data) - store.length - reserve;
}

static void append(const uint8_t *data, size_t len)
{
	memcpy(&store.data[store.length], data, len);
	store.length += len;
}

static uint64_t get_ptr(const uint8_t *p)
{
	return w.ptr_size == 8 ? sys_get_le64(p) : sys_get_le32(p);
}

static void put_ptr(uint8_t *p, uint64_t value)
{
	if (w.ptr_size == 8) {
		sys_put_le64(value, p);
	} else {
		sys_put_le32((uint32_t)value, p);
	}
}

static void stop(uint8_t flag)
{
	store.flags |= flag;
	w.state = STOPPED;
}

/* A block's header is complete: decide how much of it to keep. */
static void header_done(void)
{
	uint64_t payload;

	if (w.header[0] == 'M') {
		uint64_t start = get_ptr(&w.header[3]);
		uint64_t end = get_ptr(&w.header[3 + w.ptr_size]);

		if (end < start) {
			stop(TG_CRASH_FLAG_UNREADABLE);
			return;
		}
		payload = end - start;
		w.keep = payload;
		if (w.need + payload > room()) {
			store.flags |= TG_CRASH_FLAG_CUT;
			w.keep = room() > w.need ? ROUND_DOWN(room() - w.need, 4) : 0;
			if (w.keep < MIN_MEMORY_KEPT) {
				w.keep = 0;
			} else {
				put_ptr(&w.header[3 + w.ptr_size], start + w.keep);
			}
		}
	} else {
		payload = sys_get_le16(&w.header[3]);
		w.keep = w.need + payload <= room() ? payload : 0;
		if (w.keep < payload) {
			store.flags |= TG_CRASH_FLAG_CUT;
		}
	}
	if (w.keep > 0 || payload == 0) {
		if (w.need > room()) {
			store.flags |= TG_CRASH_FLAG_CUT;
			w.keep = 0;
		} else {
			append(w.header, w.need);
		}
	}
	w.left = payload;
	w.state = payload > 0 ? IN_PAYLOAD : WANT_ID;
}

static void backend_start(void)
{
	const uint8_t *id;
	size_t len = tg_build_id(&id);

	store.magic = 0;
	store.length = 0;
	store.flags = 0;
	mark.sequence++;
	mark.cycles = k_cycle_get_32();
	store.build_id_len = MIN(len, sizeof(store.build_id));
	memcpy(store.build_id, id, store.build_id_len);
	w.state = WANT_FILE;
	w.have = 0;
	w.ptr_size = 0;
}

static void backend_output(uint8_t *buf, size_t len)
{
	while (len > 0 && w.state != STOPPED) {
		switch (w.state) {
		case WANT_FILE: {
			size_t take = MIN(len, FILE_HEADER_SIZE - w.have);

			memcpy(&w.header[w.have], buf, take);
			w.have += take;
			buf += take;
			len -= take;
			if (w.have == FILE_HEADER_SIZE) {
				/* Byte 6 is the pointer size in bits, as a power of two. */
				w.ptr_size = (w.header[6] == 5) ? 4 : (w.header[6] == 6) ? 8 : 0;
				if (w.header[0] != 'Z' || w.header[1] != 'E' || w.ptr_size == 0) {
					stop(TG_CRASH_FLAG_UNREADABLE);
					break;
				}
				append(w.header, FILE_HEADER_SIZE);
				w.state = WANT_ID;
			}
			break;
		}
		case WANT_ID:
			w.header[0] = *buf;
			w.have = 1;
			switch (*buf) {
			case 'A':
			case 'T':
				w.need = 5;
				break;
			case 'F':
				w.need = 9;
				break;
			case 'M':
				w.need = 3 + 2 * w.ptr_size;
				break;
			default:
				/* A block this version doesn't know: what came before it is still a valid dump. */
				stop(TG_CRASH_FLAG_CUT);
				continue;
			}
			buf++;
			len--;
			w.state = WANT_HEADER;
			break;
		case WANT_HEADER: {
			size_t take = MIN(len, w.need - w.have);

			memcpy(&w.header[w.have], buf, take);
			w.have += take;
			buf += take;
			len -= take;
			if (w.have == w.need) {
				header_done();
			}
			break;
		}
		case IN_PAYLOAD: {
			size_t take = (size_t)MIN((uint64_t)len, w.left);
			size_t copy = (size_t)MIN((uint64_t)take, w.keep);

			append(buf, copy);
			w.keep -= copy;
			w.left -= take;
			buf += take;
			len -= take;
			if (w.left == 0) {
				w.state = WANT_ID;
			}
			break;
		}
		default:
			return;
		}
	}
}

static void backend_end(void)
{
	uint8_t header[3 + 2 * 8];
	uintptr_t start = (uintptr_t)&mark;

	if (store.length < FILE_HEADER_SIZE || w.ptr_size == 0) {
		return;
	}
	/* room() kept space for this. */
	header[0] = 'M';
	sys_put_le16(1, &header[1]);
	w.state = STOPPED;
	put_ptr(&header[3], start);
	put_ptr(&header[3 + w.ptr_size], start + sizeof(mark));
	append(header, 3 + 2 * w.ptr_size);
	append((const uint8_t *)&mark, sizeof(mark));
	store.crc = store_crc();
	store.magic = STORE_MAGIC;
}

static int backend_query(enum coredump_query_id id, void *arg)
{
	ARG_UNUSED(arg);
	switch (id) {
	case COREDUMP_QUERY_GET_ERROR:
		return 0;
	case COREDUMP_QUERY_HAS_STORED_DUMP:
		return store_valid() ? 1 : 0;
	case COREDUMP_QUERY_GET_STORED_DUMP_SIZE:
		return store_valid() ? (int)store.length : 0;
	default:
		return -ENOTSUP;
	}
}

static int backend_cmd(enum coredump_cmd_id id, void *arg)
{
	struct coredump_cmd_copy_arg *copy = arg;

	switch (id) {
	case COREDUMP_CMD_CLEAR_ERROR:
		return 0;
	case COREDUMP_CMD_VERIFY_STORED_DUMP:
		return store_valid() ? 1 : 0;
	case COREDUMP_CMD_ERASE_STORED_DUMP:
	case COREDUMP_CMD_INVALIDATE_STORED_DUMP:
		/* The checksum stays, so tg_crash_kept_next() can tell which crash this was. */
		store.magic = 0;
		return 0;
	case COREDUMP_CMD_COPY_STORED_DUMP:
		if (copy == NULL || copy->buffer == NULL || copy->offset < 0) {
			return -EINVAL;
		}
		if (!store_valid()) {
			return -ENOENT;
		}
		if ((size_t)copy->offset >= store.length) {
			return 0;
		}
		size_t len = MIN(copy->length, store.length - (size_t)copy->offset);

		memcpy(copy->buffer, &store.data[copy->offset], len);
		return (int)len;
	default:
		return -ENOTSUP;
	}
}

struct coredump_backend_api coredump_backend_other = {
	.start = backend_start,
	.end = backend_end,
	.buffer_output = backend_output,
	.query = backend_query,
	.cmd = backend_cmd,
};

size_t tg_crash_stored_build_id(uint8_t *out, size_t cap)
{
	if (!store_valid()) {
		return 0;
	}
	size_t len = MIN((size_t)store.build_id_len, cap);

	memcpy(out, store.build_id, len);
	return len;
}

uint8_t tg_crash_stored_flags(void)
{
	return store_valid() ? store.flags : 0;
}

#if defined(CONFIG_TRACEGALE_CRASH_KEEP)
/*
 * The kept copy: the store, up to the end of its dump, as one settings value.
 *
 * Zephyr's flash coredump backend isn't used for this, because it writes flash while the device is crashing, which
 * fails with flash drivers that need interrupts or locks, and it needs a partition in the devicetree. This runs on
 * Tracegale's thread, so it works with any flash that settings work with, and keeps the build ID, flags and mark that
 * were recorded at the crash.
 *
 * RAM holds the newest crash and flash the oldest unsent one: a crash is only saved when nothing is kept yet. So a device
 * that crashes again and again while offline writes its flash once, and the first crash, usually the cause, is the
 * one that survives.
 */
#define KEPT_NAME "tracegale/crash"
#define KEPT_HEADER offsetof(struct store, data)

static struct {
	/* A copy is in flash, and this is its checksum. */
	bool present;
	uint32_t crc;
	/* The copy in flash can't be used. */
	bool bad;
	/* The crash whose save was already tried, so a failing flash is written once for each crash. */
	bool tried;
	uint32_t tried_crc;
} kept;

static int kept_read(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param)
{
	struct {
		uint32_t magic;
		uint32_t crc;
	} head;

	ARG_UNUSED(param);
	if (key != NULL && key[0] != '\0') {
		return 0;
	}
	if (len < KEPT_HEADER + FILE_HEADER_SIZE || len > sizeof(store)) {
		/* Empty, or written by firmware with a larger store. */
		kept.bad = len > 0;
		return 0;
	}
	if (store_valid()) {
		/* RAM has a newer crash, which is sent first. Only note which crash flash holds. */
		if (read_cb(cb_arg, &head, sizeof(head)) == (ssize_t)sizeof(head) && head.magic == STORE_MAGIC) {
			kept.present = true;
			kept.crc = head.crc;
		} else {
			kept.bad = true;
		}
		return 0;
	}
	/* Whatever flash holds is checked like the RAM store after a reboot, so a damaged or changed copy is never sent. */
	if (read_cb(cb_arg, &store, len) == (ssize_t)len && store_valid() && len == KEPT_HEADER + store.length) {
		kept.present = true;
		kept.crc = store.crc;
		LOG_INF("Loaded a kept crash of %u bytes", (unsigned int)store.length);
	} else {
		store.magic = 0;
		kept.bad = true;
	}
	return 0;
}

void tg_crash_kept_load(void)
{
	kept.present = false;
	kept.bad = false;
	(void)settings_load_subtree_direct(KEPT_NAME, kept_read, NULL);
	if (kept.bad) {
		LOG_WRN("The kept crash can't be used, so it's deleted");
		(void)settings_delete(KEPT_NAME);
	}
}

void tg_crash_keep(void)
{
	int rc;

	if (kept.present || !store_valid() || (kept.tried && kept.tried_crc == store.crc)) {
		return;
	}
	kept.tried = true;
	kept.tried_crc = store.crc;
	rc = settings_save_one(KEPT_NAME, &store, KEPT_HEADER + store.length);
	if (rc != 0) {
		LOG_WRN("Can't keep the crash through a power loss: %d", rc);
		return;
	}
	kept.present = true;
	kept.crc = store.crc;
	LOG_INF("Saved the crash, so a power loss can't lose it");
}

void tg_crash_kept_next(void)
{
	if (!kept.present) {
		return;
	}
	if (kept.crc == store.crc) {
		/* The crash that was just erased from RAM. If this fails, the copy is sent again after the next
		 * boot, and the server, which knows it by its SHA-256, counts it once.
		 */
		if (settings_delete(KEPT_NAME) == 0) {
			kept.present = false;
		}
		return;
	}
	/* An older crash, kept before the one that was just sent. RAM is free now, so this loads it. */
	tg_crash_kept_load();
}

int tg_crash_kept_forget(void)
{
	int rc = settings_delete(KEPT_NAME);

	/* Some settings backends report a value that wasn't there. That's still the wanted result. */
	if (rc == -ENOENT) {
		rc = 0;
	}
	if (rc == 0) {
		kept.present = false;
	}
	return rc;
}
#endif /* CONFIG_TRACEGALE_CRASH_KEEP */

#if defined(CONFIG_ZTEST)
/* Lets tests damage the store, as a fault in RAM would. */
void tg_crash_ram_damage(void)
{
	store.data[FILE_HEADER_SIZE] ^= 0x01;
}
#endif
