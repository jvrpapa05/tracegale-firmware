/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Crash reports with one of Zephyr's own coredump backends, here the flash partition, instead of Tracegale's store. Tracegale
 * then only sends what that backend stored.
 */

#include <zephyr/debug/coredump.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "build_id.h"
#include "core.h"
#include "crash.h"
#include "fake.h"

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	(void)coredump_cmd(COREDUMP_CMD_ERASE_STORED_DUMP, NULL);
	fake_server_reset();
	/* Zephyr's dump of a thread holds its whole stack, so it's larger than those from Tracegale's store. */
	srv.chunk_max = 4096;
}

ZTEST(crash_zephyr_backend, test_nothing_stored_sends_nothing)
{
	zassert_equal(tg_crash_pending(), 0);
	zassert_ok(tg_core_attempt());
	zassert_equal(srv.starts, 0);
}

ZTEST(crash_zephyr_backend, test_dump_from_zephyrs_backend_is_sent_then_erased)
{
	const uint8_t *own;
	size_t own_len = tg_build_id(&own);
	size_t size;

	/* Zephyr's own dump of this thread, written to the flash partition. */
	coredump(K_ERR_KERNEL_OOPS, NULL, k_current_get());
	size = tg_crash_pending();
	zassert_true(size > 12 && size <= sizeof(srv.data), "stored %u bytes", (unsigned int)size);

	srv.fail_exchange_at = 3;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_TIMEOUT);
	zassert_equal(tg_crash_pending(), size, "kept until the server has all of it");

	zassert_ok(tg_core_attempt());
	zassert_true(srv.complete);
	zassert_equal(srv.have, size);
	zassert_equal(srv.data[0], 'Z');
	zassert_equal(srv.data[1], 'E');
	zassert_equal(sys_get_le32(&srv.data[8]), K_ERR_KERNEL_OOPS);
	zassert_true(own_len > 0);
	zassert_equal(srv.build_id_len, own_len, "the running image's build ID, since this backend records none");
	zassert_mem_equal(srv.build_id, own, own_len);
	zassert_equal(tg_crash_pending(), 0, "erased once the server has it");
}

ZTEST_SUITE(crash_zephyr_backend, NULL, NULL, before, NULL, NULL);
