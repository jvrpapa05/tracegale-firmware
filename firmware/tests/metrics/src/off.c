/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * With CONFIG_TRACEGALE_METRICS=n, an application that defines and updates metrics builds unchanged.
 */

#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "core.h"
#include "fake.h"

TRACEGALE_METRIC_GAUGE_DEFINE(battery_mv);
TRACEGALE_METRIC_COUNTER_DEFINE(door_opened);
TRACEGALE_METRIC_DECLARE(defined_elsewhere);

ZTEST(metrics_off, test_the_api_builds_and_sends_nothing)
{
	struct tracegale_status status;
	int reading = 3712;

	TRACEGALE_METRIC_SET(battery_mv, reading);
	TRACEGALE_METRIC_ADD(door_opened, 1);
	TRACEGALE_METRIC_ADD(defined_elsewhere, 1);
	fake_reset();
	zassert_ok(tg_core_attempt());
	zassert_equal(srv.hellos, 1);
	zassert_equal(srv.metrics, 0);
	tracegale_get_status(&status);
	zassert_equal(status.metrics_sent, 0);
}

ZTEST_SUITE(metrics_off, NULL, NULL, fake_before, NULL, NULL);
