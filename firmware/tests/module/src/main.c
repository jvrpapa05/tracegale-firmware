/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/ztest.h>

#include <tracegale/version.h>

ZTEST(tracegale_module, test_version_matches_header)
{
	zassert_equal(strcmp(tracegale_version(), TRACEGALE_VERSION_STRING), 0,
		      "unexpected version: %s", tracegale_version());
}

ZTEST_SUITE(tracegale_module, NULL, NULL, NULL, NULL, NULL);
