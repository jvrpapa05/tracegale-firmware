/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sends a stored coredump using "Device to server" in docs/protocol/README.md. It uses only Zephyr's backend-neutral
 * coredump calls, so it works with any backend that stores a dump.
 *
 * The device keeps no transfer state: the server finds an upload by its SHA-256, so after a dropped connection or a reboot
 * sending XFER_START again resumes it.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/debug/coredump.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <tracegale/tracegale.h>

#include "build_id.h"
#include "crash.h"
#include "sha256.h"
#include "wire.h"

LOG_MODULE_DECLARE(tracegale, CONFIG_TRACEGALE_LOG_LEVEL);

/* The most an XFER_CHUNK adds around its data, from "Cost per message" in docs/protocol/README.md. */
#define CHUNK_OVERHEAD 25
/* Give up on a dump the server keeps refusing as damaged, rather than send it forever. */
#define MAX_HASH_MISMATCHES 3

static uint8_t hash_mismatches;

struct xfer_status {
	uint32_t xfer_id;
	uint32_t next_offset;
	uint32_t chunk_max;
	bool complete;
};

size_t tg_crash_pending(void)
{
	int size = coredump_query(COREDUMP_QUERY_GET_STORED_DUMP_SIZE, NULL);

	return size > 0 ? (size_t)size : 0;
}

static void erase(void)
{
	(void)coredump_cmd(COREDUMP_CMD_ERASE_STORED_DUMP, NULL);
	hash_mismatches = 0;
#if defined(CONFIG_TRACEGALE_CRASH_KEEP)
	tg_crash_kept_next();
#endif
}

#if !defined(CONFIG_TRACEGALE_CRASH_RAM)
/*
 * With one of Zephyr's own backends, nothing recorded which image crashed. The running image is the best answer, and is
 * wrong only when the image was replaced between the crash and its upload. The server then can't find the right symbols.
 */
size_t tg_crash_stored_build_id(uint8_t *out, size_t cap)
{
	const uint8_t *id;
	size_t len = MIN(tg_build_id(&id), cap);

	memcpy(out, id, len);
	return len;
}

uint8_t tg_crash_stored_flags(void)
{
	return 0;
}
#endif

static int copy(size_t offset, uint8_t *out, size_t len)
{
	struct coredump_cmd_copy_arg arg = {.offset = offset, .buffer = out, .length = len};
	int rc = coredump_cmd(COREDUMP_CMD_COPY_STORED_DUMP, &arg);

	return rc == (int)len ? 0 : -TRACEGALE_ERR_STORAGE;
}

static int hash(size_t size, uint8_t *scratch, size_t cap, uint8_t digest[TG_SHA256_SIZE])
{
	struct tg_sha256 ctx;

	tg_sha256_init(&ctx);
	for (size_t offset = 0; offset < size;) {
		size_t len = MIN(cap, size - offset);
		int rc = copy(offset, scratch, len);

		if (rc != 0) {
			return rc;
		}
		tg_sha256_update(&ctx, scratch, len);
		offset += len;
	}
	tg_sha256_final(&ctx, digest);
	return 0;
}

/* Sends one message and reads the XFER_STATUS it should get back. Bad data can only fail this attempt. */
static int exchange(const struct tg_transport *transport, uint8_t *buf, size_t cap, size_t len, uint32_t id,
		    struct xfer_status *status, int *detail)
{
	struct tg_message reply;
	size_t reply_len = 0;
	uint32_t code = 0;
	int rc = transport->exchange(buf, len, buf, cap, &reply_len, detail);

	if (rc != 0) {
		return rc;
	}
	if (tg_read_message(buf, reply_len, &reply) != 0 || reply.id != id) {
		return -TRACEGALE_ERR_BAD_REPLY;
	}
	if (reply.type == TG_ERROR) {
		(void)tg_read_uint(&reply, 0, &code);
		*detail = code;
		return -TRACEGALE_ERR_SERVER;
	}
	if (reply.type != TG_XFER_STATUS || !tg_read_uint(&reply, 0, &status->xfer_id) ||
	    !tg_read_uint(&reply, 1, &status->next_offset) || !tg_read_uint(&reply, 2, &status->chunk_max) ||
	    !tg_read_bool(&reply, 3, &status->complete)) {
		return -TRACEGALE_ERR_BAD_REPLY;
	}
	return 0;
}

static int start(const struct tg_transport *transport, uint8_t *buf, size_t cap, uint32_t id, size_t size,
		 const uint8_t digest[TG_SHA256_SIZE], struct xfer_status *status, int *detail)
{
	uint8_t build_id[TG_BUILD_ID_MAX];
	size_t build_id_len = tg_crash_stored_build_id(build_id, sizeof(build_id));
	uint8_t flags = tg_crash_stored_flags();
	struct tg_writer w;

	tg_write_begin(&w, buf, cap, TG_XFER_START, id, 3 + (build_id_len > 0) + (flags != 0));
	tg_write_uint(&w, 0, TG_XFER_KIND_COREDUMP);
	tg_write_uint(&w, 1, size);
	tg_write_bytes(&w, 2, digest, TG_SHA256_SIZE);
	if (build_id_len > 0) {
		tg_write_bytes(&w, 3, build_id, build_id_len);
	}
	if (flags != 0) {
		tg_write_uint(&w, 4, flags);
	}
	if (w.overflow) {
		return -TRACEGALE_ERR_TOO_LARGE;
	}
	return exchange(transport, buf, cap, w.len, id, status, detail);
}

/* What to do with the stored dump after the server refused it. */
static void refused(uint32_t code)
{
	switch (code) {
	case TG_WIRE_ERR_HASH_MISMATCH:
		if (++hash_mismatches >= MAX_HASH_MISMATCHES) {
			LOG_ERR("Server keeps finding the crash damaged, so it's erased");
			erase();
		}
		break;
	case TG_WIRE_ERR_TOO_LARGE:
	case TG_WIRE_ERR_UNSUPPORTED_KIND:
		/* This server will never take it. */
		LOG_WRN("Server doesn't accept this crash (error %u), so it's erased", code);
		erase();
		break;
	default:
		break;
	}
}

int tg_crash_send(const struct tg_transport *transport, uint8_t *buf, size_t cap, uint32_t (*next_id)(void),
		  int *detail)
{
	uint8_t digest[TG_SHA256_SIZE];
	struct xfer_status status;
	size_t size = tg_crash_pending();
	uint32_t stalled = 0;
	bool restarted = false;
	int rc;

	if (size == 0) {
		return 0;
	}
	if (cap <= CHUNK_OVERHEAD + 16) {
		return -TRACEGALE_ERR_TOO_LARGE;
	}
	rc = hash(size, buf, cap, digest);
	if (rc != 0) {
		return rc;
	}
	uint32_t id = next_id();

	LOG_INF("Sending a crash of %u bytes, message %u", (unsigned int)size, id);
	rc = start(transport, buf, cap, id, size, digest, &status, detail);
	while (rc == 0 && !status.complete) {
		uint32_t offset = status.next_offset;

		if (offset >= size || status.chunk_max == 0 || stalled > 2) {
			/* The server has all of it but hasn't accepted it, or isn't making progress. */
			return -TRACEGALE_ERR_BAD_REPLY;
		}
		size_t len = MIN(MIN((size_t)status.chunk_max, cap - CHUNK_OVERHEAD), size - offset);
		uint32_t xfer_id = status.xfer_id;
		struct tg_writer w;

		id = next_id();
		tg_write_begin(&w, buf, cap, TG_XFER_CHUNK, id, 3);
		tg_write_uint(&w, 0, xfer_id);
		tg_write_uint(&w, 1, offset);
		uint8_t *data = tg_write_bytes_reserve(&w, 2, len);

		if (data == NULL) {
			return -TRACEGALE_ERR_TOO_LARGE;
		}
		rc = copy(offset, data, len);
		if (rc != 0) {
			return rc;
		}
		LOG_DBG("Sending bytes %u to %u, message %u", offset, (unsigned int)(offset + len), id);
		rc = exchange(transport, buf, cap, w.len, id, &status, detail);
		if (rc == -TRACEGALE_ERR_SERVER && *detail == TG_WIRE_ERR_UNKNOWN_TRANSFER && !restarted) {
			/* The server dropped it, for example after it expired: start again, once. */
			restarted = true;
			id = next_id();
			rc = start(transport, buf, cap, id, size, digest, &status, detail);
			continue;
		}
		stalled = (rc == 0 && status.next_offset <= offset) ? stalled + 1 : 0;
	}
	if (rc == -TRACEGALE_ERR_SERVER) {
		LOG_WRN("Server refused the crash: error %d", *detail);
		refused(*detail);
		return rc;
	}
	if (rc != 0) {
		return rc;
	}
	LOG_INF("Crash sent, message %u", id);
	erase();
	return 0;
}
