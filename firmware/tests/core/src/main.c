/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "core.h"
#include "reset.h"
#include "store.h"
#include "transport.h"
#include "wire.h"

/* What the fake connection will do next. */
static struct {
	int open_rc;
	int exchange_rc;
	int opened;
	int closed;
	uint8_t last_tx[CONFIG_TRACEGALE_MAX_MESSAGE];
	size_t last_tx_len;
	enum { REPLY_ACK, REPLY_ERROR, REPLY_WRONG_ID, REPLY_GARBAGE, REPLY_WRONG_TYPE } reply;
} fake;

static int fake_open(int *detail)
{
	fake.opened++;
	*detail = 111;
	return fake.open_rc;
}

static int fake_exchange(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_cap, size_t *rx_len, int *detail)
{
	struct tg_message msg;
	struct tg_writer w;

	memcpy(fake.last_tx, tx, tx_len);
	fake.last_tx_len = tx_len;
	if (fake.exchange_rc != 0) {
		*detail = 222;
		return fake.exchange_rc;
	}
	zassert_ok(tg_read_message(tx, tx_len, &msg), "the device sent a message that breaks the specification");
	switch (fake.reply) {
	case REPLY_ACK:
		tg_write_begin(&w, rx, rx_cap, TG_ACK, msg.id, 0);
		break;
	case REPLY_ERROR:
		tg_write_begin(&w, rx, rx_cap, TG_ERROR, msg.id, 1);
		tg_write_uint(&w, 0, 10);
		break;
	case REPLY_WRONG_ID:
		tg_write_begin(&w, rx, rx_cap, TG_ACK, msg.id + 1, 0);
		break;
	case REPLY_WRONG_TYPE:
		tg_write_begin(&w, rx, rx_cap, 99, msg.id, 0);
		break;
	default:
		rx[0] = 0xff;
		*rx_len = 1;
		return 0;
	}
	*rx_len = w.len;
	return 0;
}

static void fake_close(void)
{
	fake.closed++;
}

const struct tg_transport *tg_transport_get(void)
{
	static const struct tg_transport transport = {.open = fake_open, .exchange = fake_exchange, .close = fake_close};

	return &transport;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(&fake, 0, sizeof(fake));
}

ZTEST(core, test_hello_follows_the_specification)
{
	struct tg_message msg;
	const uint8_t *data;
	size_t len;
	uint32_t value;
	struct tracegale_status status;

	zassert_ok(tg_core_attempt());
	zassert_equal(fake.opened, 1);
	zassert_equal(fake.closed, 1);
	zassert_ok(tg_read_message(fake.last_tx, fake.last_tx_len, &msg));
	zassert_equal(msg.type, TG_HELLO);
	zassert_true(tg_read_bytes(&msg, 1, &data, &len));
	zassert_true(tg_read_uint(&msg, 3, &value));
	zassert_true(tg_read_uint(&msg, 4, &value));
	zassert_equal(value, CONFIG_TRACEGALE_MAX_MESSAGE);
	tracegale_get_status(&status);
	zassert_equal(status.last_message_id, msg.id);
}

ZTEST(core, test_each_failure_has_its_own_error_and_always_closes)
{
	struct tracegale_status status;

	fake.open_rc = -TRACEGALE_ERR_CONNECT;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_CONNECT);
	tracegale_get_status(&status);
	zassert_equal(status.last_error, TRACEGALE_ERR_CONNECT);
	zassert_equal(status.last_error_detail, 111);
	zassert_equal(fake.closed, 0, "nothing was open");

	fake.open_rc = 0;
	fake.exchange_rc = -TRACEGALE_ERR_TIMEOUT;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_TIMEOUT);
	tracegale_get_status(&status);
	zassert_equal(status.last_error, TRACEGALE_ERR_TIMEOUT);
	zassert_equal(status.last_error_detail, 222);
	zassert_equal(fake.closed, 1);
}

ZTEST(core, test_bad_replies_fail_the_attempt_only)
{
	static const int bad[] = {REPLY_WRONG_ID, REPLY_GARBAGE, REPLY_WRONG_TYPE};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		fake.reply = bad[i];
		zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_BAD_REPLY, "reply %d", bad[i]);
	}
	fake.reply = REPLY_ACK;
	zassert_ok(tg_core_attempt(), "the next attempt must still work");
}

ZTEST(core, test_server_error_is_recorded)
{
	struct tracegale_status status;

	fake.reply = REPLY_ERROR;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_SERVER);
	tracegale_get_status(&status);
	zassert_equal(status.last_server_error, 10);
}

ZTEST(core, test_message_ids_change)
{
	struct tracegale_status status;
	uint32_t first;

	zassert_ok(tg_core_attempt());
	tracegale_get_status(&status);
	first = status.last_message_id;
	zassert_ok(tg_core_attempt());
	tracegale_get_status(&status);
	zassert_not_equal(status.last_message_id, first);
}

ZTEST(core, test_backoff_grows_stops_and_has_jitter)
{
	uint32_t min = CONFIG_TRACEGALE_BACKOFF_MIN_S;
	uint32_t max = CONFIG_TRACEGALE_BACKOFF_MAX_S;

	zassert_within(tg_core_backoff_s(1, 0), min, min, "first wait");
	zassert_equal(tg_core_backoff_s(2, 0), (min * 2 * 3) / 4);
	zassert_equal(tg_core_backoff_s(1000, 0), (max * 3) / 4, "must stop growing, without overflowing");
	zassert_true(tg_core_backoff_s(1000, UINT32_MAX) <= (max * 5) / 4 + 1);
	zassert_not_equal(tg_core_backoff_s(5, 0), tg_core_backoff_s(5, 3), "must have jitter");
}

ZTEST(core, test_reset_reasons_map_to_the_specification)
{
	zassert_equal(tg_reset_reason_from_cause(0), 0);
	zassert_equal(tg_reset_reason_from_cause(RESET_POR), 1);
	zassert_equal(tg_reset_reason_from_cause(RESET_PIN), 2);
	zassert_equal(tg_reset_reason_from_cause(RESET_SOFTWARE), 3);
	zassert_equal(tg_reset_reason_from_cause(RESET_WATCHDOG), 4);
	zassert_equal(tg_reset_reason_from_cause(RESET_BROWNOUT), 5);
	zassert_equal(tg_reset_reason_from_cause(RESET_DEBUG), 6);
	zassert_equal(tg_reset_reason_from_cause(RESET_CPU_LOCKUP), 7);
	zassert_equal(tg_reset_reason_from_cause(RESET_LOW_POWER_WAKE), 8);
	/* The most specific cause wins when hardware sets several. */
	zassert_equal(tg_reset_reason_from_cause(RESET_POR | RESET_WATCHDOG), 4);
}

ZTEST(core, test_provisioning_needs_everything)
{
	static const uint8_t der[] = {1, 2, 3};
	uint8_t too_big[CONFIG_TRACEGALE_CRED_MAX_SIZE + 1] = {0};

	zassert_equal(tracegale_provision_commit(), -TRACEGALE_ERR_NOT_PROVISIONED);
	zassert_false(tg_store_ready());
	zassert_equal(tracegale_provision_server("", 8443), -TRACEGALE_ERR_BAD_ARGUMENT);
	zassert_equal(tracegale_provision_server("server.example", 0), -TRACEGALE_ERR_BAD_ARGUMENT);
	zassert_ok(tracegale_provision_server("server.example", 8443));
	zassert_equal(tracegale_provision_credential(TRACEGALE_CREDENTIAL_CA, too_big, sizeof(too_big)), -TRACEGALE_ERR_TOO_LARGE);
	zassert_ok(tracegale_provision_credential(TRACEGALE_CREDENTIAL_CA, der, sizeof(der)));
	zassert_ok(tracegale_provision_credential(TRACEGALE_CREDENTIAL_DEVICE_CERT, der, sizeof(der)));
	zassert_equal(tracegale_provision_commit(), -TRACEGALE_ERR_NOT_PROVISIONED, "the key is still missing");
	zassert_ok(tracegale_provision_credential(TRACEGALE_CREDENTIAL_DEVICE_KEY, der, sizeof(der)));
	zassert_ok(tracegale_provision_commit());
	zassert_true(tg_store_ready());
	zassert_mem_equal(tg_store_host(), "server.example", 15);
	zassert_ok(tracegale_provision_erase());
	zassert_false(tg_store_ready());
}

ZTEST_SUITE(core, NULL, NULL, before, NULL, NULL);
