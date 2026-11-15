/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * A crash kept through a power loss (CONFIG_TRACEGALE_CRASH_KEEP), on real settings storage: NVS on a simulated flash.
 * Whole attempts run through the core, against the fake server in main.c.
 *
 * A power loss is simulated the way the device sees one: the RAM store no longer holds a valid dump, and the boot-time
 * load runs again.
 */

#include <string.h>

#include <zephyr/debug/coredump.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "core.h"
#include "crash.h"
#include "fake.h"
#include "store.h"
#include "wire.h"

#define KEPT_NAME "tracegale/crash"
/* What the store keeps before the dump: magic, checksum, length, flags and the build ID. */
#define KEPT_HEADER 48

static uint8_t kept[KEPT_HEADER + STORE_SIZE + 8];
static size_t kept_len;

static int read_kept(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param)
{
	ARG_UNUSED(key);
	ARG_UNUSED(param);
	zassert_true(len <= sizeof(kept));
	zassert_equal(read_cb(cb_arg, kept, len), (ssize_t)len);
	kept_len = len;
	return 0;
}

/* Reads the copy in flash into kept[]. @return its size, or 0 when there's none. */
static size_t load_kept(void)
{
	kept_len = 0;
	zassert_ok(settings_load_subtree_direct(KEPT_NAME, read_kept, NULL));
	return kept_len;
}

static void power_loss(void)
{
	tg_crash_ram_damage();
	zassert_equal(tg_crash_pending(), 0, "RAM no longer holds a crash");
	tg_crash_kept_load();
}

static uint32_t crashes_sent(void)
{
	struct tracegale_status status;

	tracegale_get_status(&status);
	return status.crashes_sent;
}

static void *setup(void)
{
	/* Starts settings, as Tracegale's thread does at boot. */
	tg_store_load();
	return NULL;
}

ZTEST(crash_keep, test_crash_sent_at_once_never_touches_flash)
{
	store_dump();
	zassert_ok(tg_core_attempt());
	zassert_true(srv.complete);
	zassert_equal(tg_crash_pending(), 0);
	zassert_equal(load_kept(), 0, "a device that can report right away doesn't wear its flash");
}

ZTEST(crash_keep, test_unsent_crash_survives_power_loss)
{
	static uint8_t before[STORE_SIZE], after[STORE_SIZE];
	uint8_t id[TG_BUILD_ID_MAX], id_after[TG_BUILD_ID_MAX];
	size_t len, id_len;
	uint32_t sent = crashes_sent();

	store_dump();
	zassert_equal(load_kept(), 0, "nothing is kept before an attempt has failed");
	len = stored(before, sizeof(before));
	id_len = tg_crash_stored_build_id(id, sizeof(id));
	zassert_true(id_len > 0);

	srv.offline = true;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_CONNECT);
	zassert_equal(load_kept(), KEPT_HEADER + len, "kept once an attempt has failed");

	power_loss();
	zassert_equal(tg_crash_pending(), len, "the crash is back");
	zassert_equal(stored(after, sizeof(after)), len);
	zassert_mem_equal(after, before, len, "byte for byte, with its mark, so the server sees the same upload");
	zassert_equal(tg_crash_stored_build_id(id_after, sizeof(id_after)), id_len);
	zassert_mem_equal(id_after, id, id_len, "with the build ID recorded at the crash");

	srv.offline = false;
	zassert_ok(tg_core_attempt());
	zassert_true(srv.complete);
	zassert_mem_equal(srv.data, before, len);
	zassert_equal(crashes_sent(), sent + 1);
	zassert_equal(tg_crash_pending(), 0);
	zassert_equal(load_kept(), 0, "deleted from flash once the server has it");

	power_loss();
	zassert_equal(tg_crash_pending(), 0, "and it doesn't come back");
}

ZTEST(crash_keep, test_cut_flag_survives)
{
	/* A dump larger than the store. */
	dump_begin();
	dump_block('A', 40);
	dump_memory(0x20001000, 1000);
	feed(dump_len);
	zassert_equal(tg_crash_stored_flags(), TG_CRASH_FLAG_CUT);
	srv.offline = true;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_CONNECT);
	power_loss();
	zassert_true(tg_crash_pending() > 0);
	zassert_equal(tg_crash_stored_flags(), TG_CRASH_FLAG_CUT);
}

ZTEST(crash_keep, test_warm_restart_keeps_one_copy)
{
	size_t len;

	store_dump();
	len = tg_crash_pending();
	srv.offline = true;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_CONNECT);
	zassert_equal(load_kept(), KEPT_HEADER + len);

	/* A restart that kept RAM: the same crash is in RAM and in flash. */
	tg_crash_kept_load();
	zassert_equal(tg_crash_pending(), len);
	srv.offline = false;
	zassert_ok(tg_core_attempt());
	zassert_equal(srv.completed, 1, "sent once");
	zassert_equal(tg_crash_pending(), 0);
	zassert_equal(load_kept(), 0);
}

ZTEST(crash_keep, test_flash_keeps_the_first_crash_and_ram_the_newest)
{
	static uint8_t first[STORE_SIZE], second[STORE_SIZE];
	static uint8_t first_kept[sizeof(kept)];
	size_t len, kept_size;
	uint32_t sent = crashes_sent();

	store_dump();
	len = stored(first, sizeof(first));
	srv.offline = true;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_CONNECT);
	kept_size = load_kept();
	zassert_equal(kept_size, KEPT_HEADER + len);
	memcpy(first_kept, kept, kept_size);

	/* The device crashes again while offline: RAM now has the second crash. */
	feed(dump_len);
	zassert_equal(stored(second, sizeof(second)), len);
	zassert_true(memcmp(first, second, len) != 0);
	tg_crash_kept_load();
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_CONNECT);
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_CONNECT);
	zassert_equal(load_kept(), kept_size);
	zassert_mem_equal(kept, first_kept, kept_size, "the first crash stays: an offline crash loop writes flash once");

	/* Back online: the newest goes first, then the one from flash, in the same attempt. */
	srv.offline = false;
	zassert_ok(tg_core_attempt());
	zassert_equal(srv.completed, 2, "both crashes reached the server");
	zassert_mem_equal(srv.data, first, len, "the kept one went last");
	zassert_equal(crashes_sent(), sent + 2);
	zassert_equal(tg_crash_pending(), 0);
	zassert_equal(load_kept(), 0);
}

ZTEST(crash_keep, test_a_later_crash_is_kept_after_the_first_was_sent)
{
	size_t len;

	store_dump();
	srv.offline = true;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_CONNECT);
	srv.offline = false;
	zassert_ok(tg_core_attempt());
	zassert_equal(load_kept(), 0);

	feed(dump_len);
	len = tg_crash_pending();
	srv.offline = true;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_CONNECT);
	zassert_equal(load_kept(), KEPT_HEADER + len);
	power_loss();
	zassert_equal(tg_crash_pending(), len);
}

ZTEST(crash_keep, test_crash_given_up_is_deleted_from_flash)
{
	store_dump();
	srv.offline = true;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_CONNECT);
	zassert_true(load_kept() > 0);

	srv.offline = false;
	srv.error_on_start = TG_WIRE_ERR_UNSUPPORTED_KIND;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_SERVER);
	zassert_equal(tg_crash_pending(), 0, "this server will never take it");
	zassert_equal(load_kept(), 0, "so it must not come back after a power loss");
}

ZTEST(crash_keep, test_damaged_copy_is_never_loaded)
{
	size_t len;

	store_dump();
	srv.offline = true;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_CONNECT);
	len = load_kept();
	zassert_true(len > 0);

	/* One bit of the dump changed in flash. */
	kept[KEPT_HEADER + 20] ^= 0x10;
	zassert_ok(settings_save_one(KEPT_NAME, kept, len));
	power_loss();
	zassert_equal(tg_crash_pending(), 0, "the checksum found it");
	zassert_equal(load_kept(), 0, "and the bad copy is deleted");
}

ZTEST(crash_keep, test_copy_of_the_wrong_size_is_never_loaded)
{
	size_t len;

	store_dump();
	srv.offline = true;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_CONNECT);
	len = load_kept();
	(void)coredump_cmd(COREDUMP_CMD_ERASE_STORED_DUMP, NULL);

	/* Longer than this firmware's store, as after an update to a smaller CONFIG_TRACEGALE_CRASH_RAM_SIZE. */
	zassert_ok(settings_save_one(KEPT_NAME, kept, sizeof(kept)));
	tg_crash_kept_load();
	zassert_equal(tg_crash_pending(), 0);
	zassert_equal(load_kept(), 0);

	/* Its own length field says more than was saved. */
	zassert_ok(settings_save_one(KEPT_NAME, kept, len - 4));
	tg_crash_kept_load();
	zassert_equal(tg_crash_pending(), 0);
	zassert_equal(load_kept(), 0);

	/* Too short to be a dump. */
	zassert_ok(settings_save_one(KEPT_NAME, kept, 10));
	tg_crash_kept_load();
	zassert_equal(tg_crash_pending(), 0);
	zassert_equal(load_kept(), 0);
}

ZTEST(crash_keep, test_application_loading_settings_changes_nothing)
{
	size_t len;

	store_dump();
	len = tg_crash_pending();
	srv.offline = true;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_CONNECT);
	zassert_ok(settings_load(), "the kept crash isn't an error to the application's own load");
	zassert_equal(tg_crash_pending(), len);
	zassert_equal(load_kept(), KEPT_HEADER + len);
}

ZTEST(crash_keep, test_erasing_the_device_deletes_the_copy)
{
	store_dump();
	srv.offline = true;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_CONNECT);
	zassert_true(load_kept() > 0);
	zassert_ok(tracegale_provision_erase());
	zassert_equal(load_kept(), 0, "it holds the device's memory, so it goes with the credentials");
}

ZTEST_SUITE(crash_keep, NULL, setup, before_upload, NULL, NULL);
