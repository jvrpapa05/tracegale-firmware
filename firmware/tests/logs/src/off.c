/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * With CONFIG_TRACEGALE_LOGS=n, logging works as before, and nothing is sent.
 */

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "core.h"
#include "fake.h"

LOG_MODULE_REGISTER(test, LOG_LEVEL_INF);

ZTEST(logs_off, test_nothing_is_sent)
{
	struct tracegale_status status;

	LOG_ERR("Logged with Tracegale's logs off");
	fake_reset();
	zassert_ok(tg_core_attempt());
	zassert_equal(srv.hellos, 1);
	zassert_equal(srv.messages, 0);
	tracegale_get_status(&status);
	zassert_equal(status.logs_sent + status.logs_waiting + status.logs_dropped, 0);
}

ZTEST_SUITE(logs_off, NULL, NULL, fake_before, NULL, NULL);
