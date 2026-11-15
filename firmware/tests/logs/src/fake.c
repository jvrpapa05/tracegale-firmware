/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "core.h"
#include "fake.h"
#include "transport.h"
#include "wire.h"

struct fake_server srv;

struct cursor {
	const uint8_t *at;
};

/* Reads one CBOR head. The message has passed tg_read_message(), so it's well formed. */
static uint64_t head(struct cursor *c, uint8_t *major)
{
	uint8_t first = *c->at++;
	uint8_t info = first & 0x1f;
	uint64_t value = 0;

	*major = first >> 5;
	if (info < 24) {
		return info;
	}
	for (size_t n = 1U << (info - 24); n > 0; n--) {
		value = (value << 8) | *c->at++;
	}
	return value;
}

static uint64_t uint(struct cursor *c)
{
	uint8_t major;
	uint64_t value = head(c, &major);

	zassert_equal(major, 0, "an unsigned integer");
	return value;
}

static size_t text(struct cursor *c, char *out, size_t keep)
{
	uint8_t major;
	uint64_t len = head(c, &major);

	zassert_equal(major, 3, "text");
	for (uint64_t i = 0; i < len; i++) {
		zassert_true(c->at[i] >= 0x20 && c->at[i] < 0x7f, "only printable ASCII leaves the device");
	}
	memcpy(out, c->at, MIN(len, keep));
	out[MIN(len, keep)] = '\0';
	c->at += len;
	return len;
}

static void keep(const struct tg_message *msg, size_t len)
{
	struct logs_msg *m;
	struct cursor c = {.at = msg->body};
	uint8_t major;
	bool has_boot = false, has_records = false;

	zassert_true(srv.messages <= MAX_MESSAGES, "more messages than this test keeps");
	m = &srv.msg[srv.messages - 1];
	memset(m, 0, sizeof(*m));
	m->id = msg->id;
	m->len = len;
	for (uint32_t field = 0; field < msg->fields; field++) {
		uint64_t key = head(&c, &major);

		switch (key) {
		case 0:
			m->boot = uint(&c);
			has_boot = true;
			break;
		case 1:
			m->uptime_ms = uint(&c);
			m->has_uptime = true;
			break;
		case 2: {
			uint64_t n = head(&c, &major);

			zassert_equal(major, 4, "records are an array");
			zassert_true(n <= MAX_RECORDS, "more records in one message than this test keeps");
			m->count = n;
			for (uint64_t i = 0; i < n; i++) {
				struct rec *r = &m->rec[i];

				zassert_equal(head(&c, &major), 5, "a record has five items");
				zassert_equal(major, 4);
				r->seq = uint(&c);
				r->uptime_ms = uint(&c);
				r->level = uint(&c);
				zassert_true(r->level >= 1 && r->level <= 4, "a level from 1 to 4");
				zassert_true(text(&c, r->module, 32) <= 32, "a module of 32 bytes at most");
				r->text_len = text(&c, r->text, TEXT_KEPT);
				zassert_true(r->text_len <= 255);
				zassert_true(i == 0 || r->seq == m->rec[i - 1].seq + 1, "records in order, none missing");
			}
			has_records = true;
			break;
		}
		case 3:
			m->dropped = uint(&c);
			m->has_dropped = true;
			zassert_true(m->dropped > 0, "nothing dropped is left out");
			break;
		default:
			zassert_unreachable("field %u isn't in the specification", (unsigned int)key);
		}
	}
	zassert_true(has_boot && has_records && m->has_uptime, "boot, uptime and records are always there");
	zassert_true(m->boot != 0);
	for (int i = 0; i < m->count; i++) {
		zassert_true(m->rec[i].uptime_ms <= m->uptime_ms, "a record is older than its message");
	}
}

static int fake_open(int *detail)
{
	ARG_UNUSED(detail);
	return 0;
}

static int fake_exchange(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_cap, size_t *rx_len, int *detail)
{
	struct tg_message msg;
	struct tg_writer w;

	zassert_true(tx_len <= CONFIG_TRACEGALE_MAX_MESSAGE, "a message of %zu bytes", tx_len);
	zassert_ok(tg_read_message(tx, tx_len, &msg), "the device sent a message that breaks the specification");
	tg_write_begin(&w, rx, rx_cap, TG_ACK, msg.id, 0);
	if (msg.type == TG_HELLO) {
		srv.hellos++;
	} else {
		zassert_equal(msg.type, TG_LOGS);
		srv.messages++;
		keep(&msg, tx_len);
		if (srv.during != NULL) {
			srv.during();
		}
		if (srv.lose_at == srv.messages) {
			*detail = 222;
			return -TRACEGALE_ERR_TIMEOUT;
		}
		if (srv.refuse_at == srv.messages) {
			tg_write_begin(&w, rx, rx_cap, TG_ERROR, msg.id, 1);
			tg_write_uint(&w, 0, srv.refuse_code);
		}
	}
	*rx_len = w.len;
	return 0;
}

static void fake_close(void)
{
}

const struct tg_transport *tg_transport_get(void)
{
	static const struct tg_transport transport = {.open = fake_open, .exchange = fake_exchange, .close = fake_close};

	return &transport;
}

void fake_reset(void)
{
	memset(&srv, 0, sizeof(srv));
}

void fake_flush(void)
{
#if defined(CONFIG_LOG_MODE_DEFERRED)
	while (log_process()) {
	}
#endif
}

void fake_before(void *fixture)
{
	ARG_UNUSED(fixture);
	for (int i = 0; i < 4; i++) {
		fake_flush();
		fake_reset();
		zassert_ok(tg_core_attempt());
	}
	fake_flush();
	fake_reset();
}
