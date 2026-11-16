/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * A link the application turns on only for a moment, such as a modem it powers for a check-in: tracegale_link_set() holds
 * attempts back without counting failures, and tracegale_check_in() makes one attempt and says how it went.
 */

#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "fake_creds.h"
#include "transport.h"
#include "wire.h"

static atomic_t opened;
static atomic_t open_rc;
static atomic_t open_delay_ms;

static int fake_open(int *detail)
{
	atomic_inc(&opened);
	*detail = 0;
	k_msleep(atomic_get(&open_delay_ms));
	return atomic_get(&open_rc);
}

static int fake_exchange(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_cap, size_t *rx_len, int *detail)
{
	struct tg_message msg;
	struct tg_writer w;

	ARG_UNUSED(detail);
	zassert_ok(tg_read_message(tx, tx_len, &msg));
	tg_write_begin(&w, rx, rx_cap, TG_ACK, msg.id, 0);
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

static struct tracegale_status now(void)
{
	struct tracegale_status s;

	tracegale_get_status(&s);
	return s;
}

static bool wait_for(enum tracegale_state wanted, int seconds)
{
	for (int i = 0; i < seconds * 10; i++) {
		if (now().state == wanted) {
			return true;
		}
		k_msleep(100);
	}
	return false;
}

ZTEST(link, test_a_link_that_is_usually_off)
{
	FAKE_PROVISION();

	/* Provisioned, with the link down since boot: nothing is attempted, so nothing fails. */
	zassert_true(wait_for(TRACEGALE_STATE_WAITING_FOR_LINK, 2), "got state %d", now().state);
	k_sleep(K_SECONDS(2));
	zassert_equal(atomic_get(&opened), 0, "an attempt was made with the link down");
	zassert_equal(now().last_error, 0);

	/* The application powers its modem for a moment, checks in, and is told how it went. */
	zassert_ok(tracegale_check_in(K_SECONDS(5)));
	zassert_equal(atomic_get(&opened), 1);
	zassert_equal(now().successes, 1);
	zassert_true(wait_for(TRACEGALE_STATE_WAITING_FOR_LINK, 2), "the link is still down after the check-in");

	/* A failure is reported as the attempt's error, and counted, because it was a real attempt. */
	atomic_set(&open_rc, -TRACEGALE_ERR_CONNECT);
	zassert_equal(tracegale_check_in(K_SECONDS(5)), -TRACEGALE_ERR_CONNECT);
	zassert_equal(now().last_error, TRACEGALE_ERR_CONNECT);
	atomic_set(&open_rc, 0);

	/* An attempt longer than the caller will wait. */
	atomic_set(&open_delay_ms, 1500);
	zassert_equal(tracegale_check_in(K_MSEC(300)), -TRACEGALE_ERR_TIMEOUT);
	k_sleep(K_SECONDS(2));
	atomic_set(&open_delay_ms, 0);

	/* With the link down long enough for a check-in to come due, it's made as soon as the link is up again. */
	int before = atomic_get(&opened);

	k_sleep(K_SECONDS(CONFIG_TRACEGALE_CHECKIN_INTERVAL_S + 1));
	zassert_equal(atomic_get(&opened), before, "the schedule must wait for the link");
	tracegale_link_set(true);
	zassert_true(wait_for(TRACEGALE_STATE_IDLE, 2), "got state %d", now().state);
	zassert_equal(atomic_get(&opened), before + 1, "the check-in that came due should be made at once");

	/* Up, and not due: no extra attempt. */
	tracegale_link_set(false);
	tracegale_link_set(true);
	k_sleep(K_SECONDS(1));
	zassert_equal(atomic_get(&opened), before + 1);
	zassert_equal(now().state, TRACEGALE_STATE_IDLE);
}

ZTEST_SUITE(link, NULL, NULL, NULL, NULL, NULL);
