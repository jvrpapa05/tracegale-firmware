/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * A fake server for the crash tests, following "Device to server" in docs/protocol/README.md. It's also the connection
 * Tracegale's core uses in these tests.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "crash.h"
#include "fake.h"
#include "sha256.h"
#include "transport.h"
#include "wire.h"

struct fake_server srv;

static void reply_status(uint8_t *rx, size_t cap, size_t *rx_len, uint32_t id)
{
	struct tg_writer w;

	tg_write_begin(&w, rx, cap, TG_XFER_STATUS, id, 4);
	tg_write_uint(&w, 0, 7);
	tg_write_uint(&w, 1, srv.have);
	tg_write_uint(&w, 2, srv.chunk_max);
	tg_write_bool(&w, 3, srv.complete);
	*rx_len = w.len;
}

static void reply_error(uint8_t *rx, size_t cap, size_t *rx_len, uint32_t id, uint32_t code)
{
	struct tg_writer w;

	tg_write_begin(&w, rx, cap, TG_ERROR, id, 1);
	tg_write_uint(&w, 0, code);
	*rx_len = w.len;
}

static int fake_exchange(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t cap, size_t *rx_len, int *detail)
{
	struct tg_message m;
	const uint8_t *bytes;
	size_t len;
	uint32_t value;

	srv.exchanges++;
	if (srv.fail_exchange_at == srv.exchanges) {
		*detail = 113;
		return -TRACEGALE_ERR_TIMEOUT;
	}
	zassert_ok(tg_read_message(tx, tx_len, &m), "the device sent a message that breaks the specification");
	if (srv.garbage) {
		rx[0] = 0xff;
		*rx_len = 1;
		return 0;
	}
	if (m.type == TG_HELLO) {
		struct tg_writer w;

		tg_write_begin(&w, rx, cap, TG_ACK, m.id, 0);
		*rx_len = w.len;
		return 0;
	}
	if (m.type == TG_XFER_START) {
		srv.starts++;
		zassert_true(tg_read_uint(&m, 0, &value) && value == TG_XFER_KIND_COREDUMP);
		zassert_true(tg_read_uint(&m, 1, &value));
		zassert_true(tg_read_bytes(&m, 2, &bytes, &len) && len == TG_SHA256_SIZE);
		if (srv.error_on_start != 0) {
			reply_error(rx, cap, rx_len, m.id, srv.error_on_start);
			return 0;
		}
		if (srv.started && memcmp(bytes, srv.sha, len) != 0) {
			srv.started = false;
		}
		if (!srv.started) {
			srv.started = true;
			srv.complete = false;
			srv.have = 0;
			srv.total = value;
			memcpy(srv.sha, bytes, TG_SHA256_SIZE);
		}
		srv.build_id_len = 0;
		if (tg_read_bytes(&m, 3, &bytes, &len)) {
			srv.build_id_len = MIN(len, sizeof(srv.build_id));
			memcpy(srv.build_id, bytes, srv.build_id_len);
		}
		reply_status(rx, cap, rx_len, m.id);
		return 0;
	}
	zassert_equal(m.type, TG_XFER_CHUNK);
	srv.chunks++;
	zassert_true(tg_read_uint(&m, 0, &value) && value == 7);
	if (srv.forget_once) {
		srv.forget_once = false;
		srv.started = false;
		reply_error(rx, cap, rx_len, m.id, TG_WIRE_ERR_UNKNOWN_TRANSFER);
		return 0;
	}
	zassert_true(tg_read_uint(&m, 1, &value));
	zassert_true(tg_read_bytes(&m, 2, &bytes, &len));
	zassert_true(len <= srv.chunk_max, "a chunk larger than the server allows");
	srv.bytes_received += len;
	if (!srv.stall && value == srv.have && value + len <= srv.total) {
		memcpy(&srv.data[srv.have], bytes, len);
		srv.have += len;
		if (srv.have == srv.total) {
			uint8_t digest[TG_SHA256_SIZE];
			struct tg_sha256 ctx;

			tg_sha256_init(&ctx);
			tg_sha256_update(&ctx, srv.data, srv.have);
			tg_sha256_final(&ctx, digest);
			zassert_mem_equal(digest, srv.sha, sizeof(digest), "the device's SHA-256 matches its data");
			srv.complete = true;
			srv.completed++;
		}
	}
	reply_status(rx, cap, rx_len, m.id);
	if (srv.lose_reply_at == srv.exchanges) {
		*detail = 113;
		return -TRACEGALE_ERR_TIMEOUT;
	}
	return 0;
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

static uint32_t ids;

static uint32_t next_id(void)
{
	return ++ids;
}

static uint8_t buffer[CONFIG_TRACEGALE_MAX_MESSAGE];

int send_crash(void)
{
	int detail = 0;
	int rc = tg_crash_send(&fake, buffer, sizeof(buffer), next_id, &detail);

	return rc == -TRACEGALE_ERR_SERVER ? -1000 - detail : rc;
}

void fake_server_reset(void)
{
	memset(&srv, 0, sizeof(srv));
	srv.chunk_max = 100;
}
