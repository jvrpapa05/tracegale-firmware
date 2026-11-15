/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * The update options an application can change: it confirms images itself (CONFIG_TRACEGALE_OTA_CONFIRM=n), no restart
 * is asked for an unconfirmed image (CONFIG_TRACEGALE_OTA_CONFIRM_TIMEOUT_S=0), older images are taken
 * (CONFIG_TRACEGALE_OTA_DOWNGRADE=y), and there's no wait after a failed update (CONFIG_TRACEGALE_OTA_HOLD_S=0).
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "fake.h"
#include "store.h"

#define LEN_BASE 9001
#define LEN_NEXT 10003
#define GETS_NEXT DIV_ROUND_UP(LEN_NEXT, BLOCK)

static uint8_t base[LEN_BASE];
static uint8_t next[LEN_NEXT];
static uint8_t older[LEN_NEXT];

static void *setup(void)
{
	make_image(base, sizeof(base), 2, 1);
	make_image(next, sizeof(next), 3, 2);
	make_image(older, sizeof(older), 1, 4);
	tg_store_load();
	return NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	fake_server_reset();
	device_reset(base, sizeof(base));
}

ZTEST(ota_options, test_application_confirms)
{
	offer(next, sizeof(next), 5);
	zassert_ok(checkin());
	device_restart();
	zassert_equal(update_state(), TRACEGALE_UPDATE_TESTING);

	/* Reaching the server isn't enough for this application. */
	zassert_ok(checkin());
	zassert_ok(checkin());
	zassert_equal(srv.results, 0);
	zassert_equal(srv.checks, 1, "no other update is taken during a trial");
	zassert_equal(update_state(), TRACEGALE_UPDATE_TESTING);
	k_sleep(K_HOURS(48));
	zassert_equal(restarts.revert, 0, "without a time limit, Tracegale never asks for a restart");

	confirm_running();
	zassert_ok(checkin());
	zassert_equal(srv.results, 1);
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_INSTALLED);
	zassert_equal(update_state(), TRACEGALE_UPDATE_NONE);
}

ZTEST(ota_options, test_application_confirms_and_restarts)
{
	offer(next, sizeof(next), 5);
	zassert_ok(checkin());
	device_restart();
	confirm_running();
	device_restart();
	zassert_true(in_slot(SLOT0, 0, next, sizeof(next)));
	zassert_ok(checkin());
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_INSTALLED);
}

ZTEST(ota_options, test_application_does_not_confirm)
{
	offer(next, sizeof(next), 5);
	zassert_ok(checkin());
	device_restart();
	zassert_ok(checkin());
	/* Restarted by the application, or by a crash. */
	device_restart();
	zassert_true(in_slot(SLOT0, 0, base, sizeof(base)));
	zassert_ok(checkin());
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_REVERTED);
}

ZTEST(ota_options, test_older_image_taken)
{
	offer(older, sizeof(older), 5);
	zassert_ok(checkin());
	zassert_equal(update_state(), TRACEGALE_UPDATE_RESTART_PENDING);
	device_restart();
	zassert_true(in_slot(SLOT0, 0, older, sizeof(older)));
}

ZTEST(ota_options, test_no_wait_after_a_failed_update)
{
	offer(next, sizeof(next), 5);
	srv.corrupt_at = 100;
	zassert_ok(checkin());
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_DAMAGED);
	srv.corrupt_at = -1;
	zassert_ok(checkin());
	zassert_equal(srv.checks, 2);
	zassert_equal(srv.gets, 2 * GETS_NEXT);
	zassert_equal(update_state(), TRACEGALE_UPDATE_RESTART_PENDING);
}

ZTEST_SUITE(ota_options, NULL, setup, before, NULL, NULL);
