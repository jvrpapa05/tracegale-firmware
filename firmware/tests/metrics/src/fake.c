/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "core.h"
#include "fake.h"
#include "transport.h"
#include "wire.h"

struct fake_server srv;

/* Used from main.c, as an application's metric can be defined in one file and counted in another. */
TRACEGALE_METRIC_COUNTER_DEFINE(defined_elsewhere);

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

static void read_pairs(struct cursor *c, struct pair *out, int *count)
{
	uint8_t major;
	uint64_t items = head(c, &major);

	zassert_equal(major, 4, "metrics are an array");
	zassert_equal(items % 2, 0, "of pairs");
	zassert_true(items > 0, "an empty array is left out");
	zassert_true(items / 2 <= MAX_PAIRS, "more metrics in one message than this test keeps");
	*count = items / 2;
	for (int i = 0; i < *count; i++) {
		uint64_t len = head(c, &major);
		uint64_t value;

		zassert_equal(major, 3, "a name is text");
		zassert_true(len >= 1 && len <= 32, "a name has 1 to 32 bytes");
		memcpy(out[i].name, c->at, len);
		out[i].name[len] = '\0';
		c->at += len;
		value = head(c, &major);
		zassert_true(major <= 1 && value <= INT32_MAX, "a value is an integer");
		out[i].value = major == 0 ? (int32_t)value : -1 - (int32_t)value;
	}
}

static void keep(const struct tg_message *msg, size_t len)
{
	struct report *report;
	struct cursor c = {.at = msg->body};
	uint8_t major;

	zassert_true(srv.metrics <= MAX_REPORTS, "more messages than this test keeps");
	report = &srv.report[srv.metrics - 1];
	memset(report, 0, sizeof(*report));
	report->id = msg->id;
	report->len = len;
	for (uint32_t field = 0; field < msg->fields; field++) {
		uint64_t key = head(&c, &major);

		switch (key) {
		case 0:
			report->period = head(&c, &major);
			zassert_equal(major, 0);
			break;
		case 1: {
			uint64_t pairs = head(&c, &major);

			zassert_equal(major, 5, "system is a map");
			report->has_system = true;
			for (uint64_t i = 0; i < pairs; i++) {
				uint64_t name = head(&c, &major);
				uint64_t value = head(&c, &major);

				zassert_true(name < ARRAY_SIZE(report->has) && major == 0 && value <= UINT32_MAX);
				report->has[name] = true;
				report->system[name] = value;
			}
			break;
		}
		case 2:
			read_pairs(&c, report->gauge, &report->gauges);
			break;
		case 3:
			read_pairs(&c, report->counter, &report->counters);
			break;
		default:
			zassert_unreachable("field %u isn't in the specification", (unsigned int)key);
		}
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
		srv.hello_id = msg.id;
	} else {
		zassert_equal(msg.type, TG_METRICS);
		srv.metrics++;
		keep(&msg, tx_len);
		if (srv.during != NULL) {
			srv.during();
		}
		if (srv.lose_at == srv.metrics) {
			*detail = 222;
			return -TRACEGALE_ERR_TIMEOUT;
		}
		if (srv.refuse_at == srv.metrics) {
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

void fake_before(void *fixture)
{
	ARG_UNUSED(fixture);
	fake_reset();
	zassert_ok(tg_core_attempt());
	fake_reset();
}

static const struct pair *find(const struct pair *pairs, int count, const char *name)
{
	for (int i = 0; i < count; i++) {
		if (strcmp(pairs[i].name, name) == 0) {
			return &pairs[i];
		}
	}
	return NULL;
}

const struct pair *find_gauge(const struct report *report, const char *name)
{
	return find(report->gauge, report->gauges, name);
}

const struct pair *find_counter(const struct report *report, const char *name)
{
	return find(report->counter, report->counters, name);
}
