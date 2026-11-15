/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Over-the-air updates (CONFIG_TRACEGALE_OTA), with whole check-ins run through the core against the fake server in
 * fake.c. Flash is the simulator's, settings storage is real NVS on it, and Zephyr's own MCUboot code marks images. Only
 * the bootloader itself is a stand-in.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "fake.h"
#include "store.h"
#include "wire.h"

/* Sizes that are no multiple of a write block, a message or a flash page. */
#define LEN_BASE 9001
#define LEN_NEXT 10003
#define LEN_OTHER 12345
/* Messages that the download of the next image takes. */
#define GETS_NEXT DIV_ROUND_UP(LEN_NEXT, BLOCK)
#define HOLD CONFIG_TRACEGALE_OTA_HOLD_S
#define TRIAL CONFIG_TRACEGALE_OTA_CONFIRM_TIMEOUT_S

/* Version 1.2.0 runs. 1.3.0 and 1.4.0 are updates, 1.1.0 is older, and one more is 1.2.0 with other contents. */
static uint8_t base[LEN_BASE];
static uint8_t next[LEN_NEXT];
static uint8_t other[LEN_OTHER];
static uint8_t older[LEN_NEXT];
static uint8_t equal[LEN_NEXT];
static const uint8_t garbage[16] = {0xde, 0xad, 0xbe, 0xef, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

static void *setup(void)
{
	make_image(base, sizeof(base), 2, 1);
	make_image(next, sizeof(next), 3, 2);
	make_image(other, sizeof(other), 4, 3);
	make_image(older, sizeof(older), 1, 4);
	make_image(equal, sizeof(equal), 2, 5);
	/* Starts settings, as Tracegale's thread does at boot. */
	tg_store_load();
	return NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	fake_server_reset();
	device_reset(base, sizeof(base));
	zassert_equal(update_state(), TRACEGALE_UPDATE_NONE);
	zassert_equal(update_result(), TRACEGALE_UPDATE_NO_RESULT);
}

static bool swaps(void)
{
	return !IS_ENABLED(CONFIG_MCUBOOT_BOOTLOADER_MODE_OVERWRITE_ONLY);
}

/* Downloads the next image whole, up to the restart that would install it. */
static void download_next(void)
{
	offer(next, sizeof(next), 5);
	zassert_ok(checkin());
	zassert_equal(update_state(), TRACEGALE_UPDATE_RESTART_PENDING);
	zassert_true(in_slot(SLOT1, image_start(), next, sizeof(next)));
}

/* After a restart with a downloaded image: it runs, and the next check-in reports it installed. */
static void expect_installed(const uint8_t *image, size_t len, uint32_t image_ref)
{
	int results = srv.results;
	int checks = srv.checks;

	zassert_true(in_slot(SLOT0, 0, image, len), "the new image runs");
	zassert_equal(update_state(), TRACEGALE_UPDATE_TESTING);
	zassert_ok(checkin());
	zassert_equal(srv.results, results + 1);
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_INSTALLED);
	zassert_equal(srv.result_ref[srv.results - 1], image_ref);
	zassert_equal(srv.checks, checks, "the check-in that reports an update doesn't ask for another");
	zassert_equal(update_state(), TRACEGALE_UPDATE_NONE);
	zassert_equal(update_result(), TRACEGALE_UPDATE_INSTALLED);
}

ZTEST(ota, test_nothing_announced_nothing_asked)
{
	offer(next, sizeof(next), 5);
	srv.pending = false;
	zassert_ok(checkin());
	zassert_equal(srv.hellos, 1);
	zassert_equal(srv.checks, 0, "a device asks only after the server said there may be an update");
}

ZTEST(ota, test_announced_but_nothing_offered)
{
	srv.pending = true;
	zassert_ok(checkin());
	zassert_equal(srv.checks, 1);
	zassert_equal(srv.gets, 0);
	zassert_equal(srv.results, 0);
	zassert_equal(update_state(), TRACEGALE_UPDATE_NONE);
}

ZTEST(ota, test_update_installed_and_confirmed)
{
	struct record record;

	download_next();
	zassert_equal(srv.checks, 1);
	zassert_equal(srv.gets, GETS_NEXT);
	zassert_equal(srv.bytes_sent, sizeof(next), "no byte is downloaded twice");
	zassert_equal(srv.results, 0, "nothing is reported before the image has run");
	zassert_equal(update_offset(), sizeof(next));
	zassert_equal(restarts.install, 1);
	zassert_true(blank_from(SLOT1, image_start() + ROUND_UP(sizeof(next), 8)) == false, "MCUboot was asked to try it");

	/* What's in settings storage. Devices in the field have this record, so its layout must stay. */
	zassert_true(record_read(&record));
	zassert_equal(record.version, 1);
	zassert_equal(record.phase, TRACEGALE_UPDATE_RESTART_PENDING);
	zassert_equal(record.image_ref, 5);
	zassert_equal(record.total_len, sizeof(next));
	zassert_mem_equal(record.sha256, srv.sha, sizeof(record.sha256));

	/* An application that puts the restart off is asked again after each check-in, and nothing else happens. */
	zassert_ok(checkin());
	zassert_equal(restarts.install, 2);
	zassert_equal(srv.checks, 1);
	zassert_equal(srv.gets, GETS_NEXT);

	device_restart();
	expect_installed(next, sizeof(next), 5);
	zassert_equal(restarts.install, 2);

	/* The server offers it once more. The device knows what it runs, and downloads nothing. */
	zassert_ok(checkin());
	zassert_equal(srv.checks, 2);
	zassert_equal(srv.gets, GETS_NEXT);
	zassert_equal(srv.results, 2);
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_INSTALLED);

	k_sleep(K_SECONDS(TRIAL + 1));
	zassert_equal(restarts.revert, 0, "a confirmed image is never reverted");
	device_restart();
	zassert_true(in_slot(SLOT0, 0, next, sizeof(next)), "a confirmed image stays through a restart");
	zassert_equal(update_state(), TRACEGALE_UPDATE_NONE);
}

ZTEST(ota, test_continues_after_a_dropped_connection)
{
	offer(next, sizeof(next), 5);
	srv.drop_get_at = 20;
	zassert_equal(checkin(), -TRACEGALE_ERR_TIMEOUT);
	zassert_equal(update_state(), TRACEGALE_UPDATE_DOWNLOADING);
	zassert_equal(update_offset(), 19 * BLOCK);
	zassert_equal(restarts.install, 0);

	zassert_ok(checkin());
	zassert_equal(srv.first_offset, 19 * BLOCK, "it continues where it stopped");
	zassert_equal(srv.bytes_sent, sizeof(next));
	zassert_equal(update_state(), TRACEGALE_UPDATE_RESTART_PENDING);
	device_restart();
	expect_installed(next, sizeof(next), 5);
}

ZTEST(ota, test_continues_after_a_power_loss)
{
	offer(next, sizeof(next), 5);
	/* 29 blocks are in flash: the first page, and part of the second. */
	srv.drop_get_at = 30;
	zassert_equal(checkin(), -TRACEGALE_ERR_TIMEOUT);

	device_restart();
	zassert_equal(update_state(), TRACEGALE_UPDATE_DOWNLOADING);
	zassert_ok(checkin());
	zassert_equal(srv.first_offset, PAGE, "the page that was being written is downloaded again, and nothing before it");
	zassert_equal(srv.bytes_sent, 29 * BLOCK + sizeof(next) - PAGE);
	device_restart();
	expect_installed(next, sizeof(next), 5);
}

ZTEST(ota, test_a_write_cut_short_is_erased)
{
	offer(next, sizeof(next), 5);
	srv.drop_get_at = 30;
	zassert_equal(checkin(), -TRACEGALE_ERR_TIMEOUT);
	/* Power was lost during the next write, which left bytes that aren't the image's. */
	scribble(SLOT1, image_start() + 29 * BLOCK, garbage, sizeof(garbage));

	device_restart();
	zassert_ok(checkin());
	zassert_equal(srv.first_offset, PAGE);
	zassert_true(in_slot(SLOT1, image_start(), next, sizeof(next)));
	device_restart();
	expect_installed(next, sizeof(next), 5);
}

ZTEST(ota, test_a_write_cut_short_across_two_pages_is_erased)
{
	offer(next, sizeof(next), 5);
	/* 36 blocks are in flash, and the next block ends in the third page. */
	srv.drop_get_at = 37;
	zassert_equal(checkin(), -TRACEGALE_ERR_TIMEOUT);
	zassert_true(36 * BLOCK < 2 * PAGE && 37 * BLOCK > 2 * PAGE);
	scribble(SLOT1, image_start() + 36 * BLOCK, &next[36 * BLOCK], 2 * PAGE - 36 * BLOCK);
	scribble(SLOT1, image_start() + 2 * PAGE, garbage, sizeof(garbage));

	device_restart();
	zassert_ok(checkin());
	zassert_equal(srv.first_offset, 2 * PAGE);
	zassert_true(in_slot(SLOT1, image_start(), next, sizeof(next)));
	device_restart();
	expect_installed(next, sizeof(next), 5);
}

ZTEST(ota, test_power_loss_after_the_last_byte)
{
	offer(next, sizeof(next), 5);
	srv.drop_get_at = GETS_NEXT;
	zassert_equal(checkin(), -TRACEGALE_ERR_TIMEOUT);
	/* The last block reached flash, and power was lost before MCUboot was asked to try the image. */
	scribble(SLOT1, image_start() + (GETS_NEXT - 1) * BLOCK, &next[(GETS_NEXT - 1) * BLOCK],
		 sizeof(next) - (GETS_NEXT - 1) * BLOCK);

	device_restart();
	zassert_true(in_slot(SLOT0, 0, base, sizeof(base)));
	zassert_equal(update_state(), TRACEGALE_UPDATE_DOWNLOADING);
	zassert_ok(checkin());
	zassert_equal(srv.first_offset, 2 * PAGE);
	zassert_equal(update_state(), TRACEGALE_UPDATE_RESTART_PENDING);
	device_restart();
	expect_installed(next, sizeof(next), 5);
}

ZTEST(ota, test_power_loss_before_the_request_is_recorded)
{
	struct record record;

	download_next();
	/* MCUboot was asked, and power was lost before that was recorded. */
	zassert_true(record_read(&record));
	record.phase = TRACEGALE_UPDATE_DOWNLOADING;
	record_write(&record, sizeof(record));

	device_restart();
	expect_installed(next, sizeof(next), 5);
}

ZTEST(ota, test_blank_pages_inside_an_image)
{
	/* The second page of this image is what erased flash looks like. */
	static uint8_t holed[LEN_NEXT];

	memcpy(holed, next, sizeof(holed));
	memset(&holed[PAGE], 0xff, PAGE);
	offer(holed, sizeof(holed), 5);
	/* Stopped in the third page. */
	srv.drop_get_at = 40;
	zassert_equal(checkin(), -TRACEGALE_ERR_TIMEOUT);
	device_restart();
	zassert_ok(checkin());
	zassert_equal(srv.first_offset, 2 * PAGE);
	zassert_true(in_slot(SLOT1, image_start(), holed, sizeof(holed)));
	device_restart();
	expect_installed(holed, sizeof(holed), 5);
}

ZTEST(ota, test_stopped_inside_a_blank_page)
{
	static uint8_t holed[LEN_NEXT];

	memcpy(holed, next, sizeof(holed));
	memset(&holed[PAGE], 0xff, PAGE);
	offer(holed, sizeof(holed), 5);
	srv.drop_get_at = 30;
	zassert_equal(checkin(), -TRACEGALE_ERR_TIMEOUT);
	device_restart();
	zassert_ok(checkin());
	zassert_equal(srv.first_offset, 0, "flash can't tell how far it got, so it goes back to the last page with data");
	zassert_true(in_slot(SLOT1, image_start(), holed, sizeof(holed)));
	device_restart();
	expect_installed(holed, sizeof(holed), 5);
}

ZTEST(ota, test_short_replies)
{
	offer(next, sizeof(next), 5);
	/* The device takes the 48 bytes that fill whole write blocks, and asks again for the other 2. */
	srv.chunk_max = 50;
	zassert_ok(checkin());
	zassert_equal(srv.gets, DIV_ROUND_UP(sizeof(next), 48));
	zassert_true(srv.bytes_sent > sizeof(next));
	zassert_true(in_slot(SLOT1, image_start(), next, sizeof(next)));
	device_restart();
	expect_installed(next, sizeof(next), 5);
}

ZTEST(ota, test_a_server_that_sends_nothing)
{
	offer(next, sizeof(next), 5);
	srv.chunk_max = 0;
	zassert_equal(checkin(), -TRACEGALE_ERR_BAD_REPLY);
	zassert_equal(srv.gets, 3, "the device gives up after three empty replies");
	zassert_equal(update_state(), TRACEGALE_UPDATE_DOWNLOADING);

	/* Less than an image header at the start is as good as nothing. */
	srv.chunk_max = 24;
	zassert_equal(checkin(), -TRACEGALE_ERR_BAD_REPLY);
	zassert_equal(srv.gets, 6);
	zassert_true(blank_from(SLOT1, 0));

	srv.chunk_max = UINT32_MAX;
	zassert_ok(checkin());
	device_restart();
	expect_installed(next, sizeof(next), 5);
}

ZTEST(ota, test_data_for_another_image_or_place)
{
	offer(next, sizeof(next), 5);
	srv.wrong_ref = true;
	zassert_equal(checkin(), -TRACEGALE_ERR_BAD_REPLY);
	srv.wrong_ref = false;
	srv.wrong_offset = true;
	zassert_equal(checkin(), -TRACEGALE_ERR_BAD_REPLY);
	zassert_equal(srv.gets, 2);
	zassert_true(blank_from(SLOT1, 0), "nothing of it is written");
	zassert_equal(srv.results, 0);
}

ZTEST(ota, test_damaged_image_attempt_limit_and_wait)
{
	struct record record;

	offer(next, sizeof(next), 5);
	srv.corrupt_at = 5000;
	zassert_ok(checkin());
	zassert_equal(srv.gets, GETS_NEXT);
	zassert_equal(srv.results, 1, "it's reported in the same check-in");
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_DAMAGED);
	zassert_equal(restarts.install, 0, "MCUboot is never asked to try it");
	zassert_equal(update_state(), TRACEGALE_UPDATE_NONE);
	zassert_equal(update_result(), TRACEGALE_UPDATE_DAMAGED);

	/* The device waits before it takes another update: first this long, then twice as long each time. */
	zassert_ok(checkin());
	k_sleep(K_SECONDS(HOLD - 2));
	zassert_ok(checkin());
	zassert_equal(srv.checks, 1);
	k_sleep(K_SECONDS(3));
	zassert_ok(checkin());
	zassert_equal(srv.checks, 2);
	zassert_equal(srv.gets, 2 * GETS_NEXT);
	zassert_equal(srv.results, 2);

	k_sleep(K_SECONDS(2 * HOLD - 2));
	zassert_ok(checkin());
	zassert_equal(srv.checks, 2);
	k_sleep(K_SECONDS(3));
	zassert_ok(checkin());
	zassert_equal(srv.gets, 3 * GETS_NEXT);
	zassert_equal(srv.results, 3);
	zassert_true(record_read(&record));
	zassert_equal(record.failures, 3);
	zassert_equal(record.streak, 3);

	/* Three attempts are the limit. From now on this image is declined, even if the server's copy is repaired. */
	srv.corrupt_at = -1;
	k_sleep(K_SECONDS(4 * HOLD + 1));
	zassert_ok(checkin());
	zassert_equal(srv.checks, 4);
	zassert_equal(srv.gets, 3 * GETS_NEXT, "nothing is downloaded");
	zassert_equal(srv.results, 4);
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_DAMAGED);

	/* Another image is welcome. */
	offer(other, sizeof(other), 6);
	zassert_ok(checkin());
	zassert_equal(update_state(), TRACEGALE_UPDATE_RESTART_PENDING);
	device_restart();
	expect_installed(other, sizeof(other), 6);
	zassert_true(record_read(&record));
	zassert_equal(record.failures, 0);
	zassert_equal(record.streak, 0, "after an update that worked, the next one doesn't wait");
}

ZTEST(ota, test_the_wait_does_not_stop_an_unfinished_download)
{
	offer(next, sizeof(next), 5);
	srv.corrupt_at = 5000;
	zassert_ok(checkin());
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_DAMAGED);
	k_sleep(K_SECONDS(HOLD + 1));

	/* The wait is over and another image is on its way, when power is lost. */
	offer(other, sizeof(other), 6);
	srv.corrupt_at = -1;
	srv.drop_get_at = srv.gets + 30;
	zassert_equal(checkin(), -TRACEGALE_ERR_TIMEOUT);
	device_restart();

	/* The wait counts again from this start, but only for an update that would erase the slot. This one continues. */
	zassert_ok(checkin());
	zassert_equal(srv.first_offset, PAGE);
	zassert_equal(update_state(), TRACEGALE_UPDATE_RESTART_PENDING);
	device_restart();
	expect_installed(other, sizeof(other), 6);
}

ZTEST(ota, test_not_an_image)
{
	static uint8_t junk[LEN_NEXT];

	memcpy(junk, next, sizeof(junk));
	junk[0] ^= 0xff;
	offer(junk, sizeof(junk), 5);
	zassert_ok(checkin());
	zassert_equal(srv.gets, 1, "the first block shows it");
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_NOT_AN_IMAGE);
	zassert_true(blank_from(SLOT1, 0), "nothing of it is written");

	/* It can't end differently, so it isn't downloaded again. */
	k_sleep(K_SECONDS(HOLD + 1));
	zassert_ok(checkin());
	zassert_equal(srv.checks, 2);
	zassert_equal(srv.gets, 1);
	zassert_equal(srv.results, 2);
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_NOT_AN_IMAGE);
}

ZTEST(ota, test_older_image_refused)
{
	offer(older, sizeof(older), 5);
	zassert_ok(checkin());
	zassert_equal(srv.gets, 1);
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_DOWNGRADE);
	zassert_true(blank_from(SLOT1, 0));
	zassert_equal(restarts.install, 0);

	/* The slot was erased for it, so the device waits, as after any update that didn't work. */
	offer(next, sizeof(next), 6);
	zassert_ok(checkin());
	zassert_equal(srv.checks, 1);
	k_sleep(K_SECONDS(HOLD + 1));
	zassert_ok(checkin());
	zassert_equal(update_state(), TRACEGALE_UPDATE_RESTART_PENDING);
}

ZTEST(ota, test_same_version_with_other_contents_is_taken)
{
	offer(equal, sizeof(equal), 5);
	zassert_ok(checkin());
	zassert_equal(update_state(), TRACEGALE_UPDATE_RESTART_PENDING);
	device_restart();
	expect_installed(equal, sizeof(equal), 5);
}

ZTEST(ota, test_too_large)
{
	scribble(SLOT1, 0, garbage, sizeof(garbage));
	offer(next, sizeof(next), 5);
	srv.total_len = slot_room() + 1;
	zassert_ok(checkin());
	zassert_equal(srv.gets, 0);
	zassert_equal(srv.results, 1);
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_TOO_LARGE);
	zassert_true(in_slot(SLOT1, 0, garbage, sizeof(garbage)), "the slot isn't touched");
	zassert_equal(update_state(), TRACEGALE_UPDATE_NONE);
}

ZTEST(ota, test_too_small)
{
	scribble(SLOT1, 0, garbage, sizeof(garbage));
	offer(next, 31, 5);
	zassert_ok(checkin());
	zassert_equal(srv.gets, 0);
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_NOT_AN_IMAGE);
	zassert_true(in_slot(SLOT1, 0, garbage, sizeof(garbage)));
}

ZTEST(ota, test_a_kind_this_firmware_does_not_know)
{
	scribble(SLOT1, 0, garbage, sizeof(garbage));
	offer(next, sizeof(next), 5);
	srv.kind = 1;
	zassert_ok(checkin());
	zassert_equal(srv.gets, 0);
	zassert_equal(srv.results, 1);
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_UNSUPPORTED_KIND);
	zassert_equal(srv.result_ref[0], 5);
	zassert_true(in_slot(SLOT1, 0, garbage, sizeof(garbage)));

	/* Nothing was erased for it, so a full image right after it is taken. */
	srv.kind = 0;
	zassert_ok(checkin());
	zassert_equal(update_state(), TRACEGALE_UPDATE_RESTART_PENDING);
}

ZTEST(ota, test_the_running_image_is_not_downloaded)
{
	scribble(SLOT1, 0, garbage, sizeof(garbage));
	offer(base, sizeof(base), 5);
	zassert_ok(checkin());
	zassert_equal(srv.gets, 0);
	zassert_equal(srv.results, 1);
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_INSTALLED);
	zassert_true(in_slot(SLOT1, 0, garbage, sizeof(garbage)));
}

ZTEST(ota, test_the_whole_slot_is_erased_first)
{
	static const uint8_t blank[16] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
					  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	/* Left by something else: at the start, and far behind where this image ends. */
	scribble(SLOT1, 0, garbage, sizeof(garbage));
	scribble(SLOT1, slot_room() - 2 * PAGE, garbage, sizeof(garbage));
	offer(next, sizeof(next), 5);
	srv.drop_get_at = 2;
	zassert_equal(checkin(), -TRACEGALE_ERR_TIMEOUT);
	zassert_true(in_slot(SLOT1, image_start(), next, BLOCK));
	zassert_true(blank_from(SLOT1, image_start() + BLOCK));
	if (image_start() > 0) {
		zassert_true(in_slot(SLOT1, 0, blank, sizeof(blank)));
	}
}

ZTEST(ota, test_image_not_confirmed_is_reverted)
{
	if (!swaps()) {
		/* In this mode MCUboot keeps no previous image. */
		ztest_test_skip();
	}
	download_next();
	device_restart();
	zassert_equal(update_state(), TRACEGALE_UPDATE_TESTING);
	zassert_true(in_slot(SLOT0, 0, next, sizeof(next)));

	/* The new image can't reach the server. */
	srv.offline = true;
	zassert_equal(checkin(), -TRACEGALE_ERR_CONNECT);
	k_sleep(K_SECONDS(TRIAL - 1));
	zassert_equal(restarts.revert, 0);
	k_sleep(K_SECONDS(2));
	zassert_equal(restarts.revert, 1, "Tracegale asks for a restart, so MCUboot brings the previous image back");
	k_sleep(K_SECONDS(3 * TRIAL));
	zassert_equal(restarts.revert, 1, "an application that puts it off isn't asked again");
	zassert_equal(restarts.install, 1);

	device_restart();
	zassert_true(in_slot(SLOT0, 0, base, sizeof(base)));
	zassert_equal(update_state(), TRACEGALE_UPDATE_REPORTING);
	srv.offline = false;
	zassert_ok(checkin());
	zassert_equal(srv.results, 1);
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_REVERTED);
	zassert_equal(srv.result_ref[0], 5);
	zassert_equal(srv.checks, 1, "the check-in that reports a failure doesn't ask for the image again");
	zassert_equal(update_result(), TRACEGALE_UPDATE_REVERTED);

	/* It waits, counted from the restart, and then gives the image another try. */
	k_sleep(K_SECONDS(HOLD - 2));
	zassert_ok(checkin());
	zassert_equal(srv.checks, 1);
	k_sleep(K_SECONDS(3));
	zassert_ok(checkin());
	zassert_equal(srv.checks, 2);
	zassert_equal(update_state(), TRACEGALE_UPDATE_RESTART_PENDING);
	device_restart();
	expect_installed(next, sizeof(next), 5);
}

ZTEST(ota, test_restart_during_the_trial_reverts)
{
	if (!swaps()) {
		ztest_test_skip();
	}
	download_next();
	device_restart();
	/* Any restart before the image is confirmed, such as a crash, ends the trial. */
	device_restart();
	zassert_true(in_slot(SLOT0, 0, base, sizeof(base)));
	zassert_ok(checkin());
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_REVERTED);
	k_sleep(K_SECONDS(TRIAL + 1));
	zassert_equal(restarts.revert, 0, "the previous image isn't on trial");
}

ZTEST(ota, test_image_that_mcuboot_refuses)
{
	download_next();
	/* For example, its signature is wrong. */
	bootloader_refuses = true;
	device_restart();
	zassert_true(in_slot(SLOT0, 0, base, sizeof(base)));
	zassert_equal(update_state(), TRACEGALE_UPDATE_REPORTING);
	zassert_ok(checkin());
	zassert_equal(srv.results, 1);
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_NOT_STARTED);
	zassert_equal(srv.checks, 1);
	zassert_equal(restarts.install, 1);

	/* Held, and then tried again. */
	zassert_ok(checkin());
	zassert_equal(srv.checks, 1);
	k_sleep(K_SECONDS(HOLD + 1));
	zassert_ok(checkin());
	zassert_equal(srv.checks, 2);
	zassert_equal(update_state(), TRACEGALE_UPDATE_RESTART_PENDING);
}

ZTEST(ota, test_result_is_kept_until_the_server_has_it)
{
	offer(next, sizeof(next), 5);
	srv.corrupt_at = 9000;
	srv.drop_result_at = 1;
	zassert_equal(checkin(), -TRACEGALE_ERR_TIMEOUT);
	zassert_equal(update_state(), TRACEGALE_UPDATE_REPORTING);

	/* Through a power loss too. */
	device_restart();
	zassert_equal(update_state(), TRACEGALE_UPDATE_REPORTING);
	zassert_equal(update_result(), TRACEGALE_UPDATE_DAMAGED);
	srv.offline = true;
	zassert_equal(checkin(), -TRACEGALE_ERR_CONNECT);
	srv.offline = false;
	zassert_ok(checkin());
	zassert_equal(srv.results, 2);
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_DAMAGED);
	zassert_equal(srv.checks, 1);
	zassert_equal(update_state(), TRACEGALE_UPDATE_NONE);
	zassert_ok(checkin());
	zassert_equal(srv.results, 2, "it's sent once");
}

ZTEST(ota, test_result_the_server_refuses)
{
	struct record record = {.version = 1, .phase = TRACEGALE_UPDATE_REPORTING, .outcome = TRACEGALE_UPDATE_REVERTED,
				.failures = 1, .image_ref = 77, .total_len = 5000};

	/* A device in the field, with a result to report, whose rollout the server meanwhile deleted. */
	record_write(&record, sizeof(record));
	device_restart();
	zassert_equal(update_state(), TRACEGALE_UPDATE_REPORTING);

	/* A busy server is asked again. */
	srv.error_on_result = TG_WIRE_ERR_RATE_LIMITED;
	zassert_equal(checkin(), -TRACEGALE_ERR_SERVER);
	zassert_equal(update_state(), TRACEGALE_UPDATE_REPORTING);
	srv.error_on_result = TG_WIRE_ERR_SERVER_BUSY;
	zassert_equal(checkin(), -TRACEGALE_ERR_SERVER);

	/* A server that doesn't know the image any more is not. */
	srv.error_on_result = TG_WIRE_ERR_UNKNOWN_IMAGE;
	zassert_ok(checkin());
	zassert_equal(srv.results, 3);
	zassert_equal(srv.result_ref[2], 77);
	zassert_equal(last_outcome(), TRACEGALE_UPDATE_REVERTED);
	zassert_equal(update_state(), TRACEGALE_UPDATE_NONE);
	zassert_ok(checkin());
	zassert_equal(srv.results, 3);
}

static void pause_at_10(void)
{
	if (srv.gets == 10) {
		tracegale_update_pause(true);
	}
}

ZTEST(ota, test_pause)
{
	offer(next, sizeof(next), 5);
	tracegale_update_pause(true);
	zassert_ok(checkin());
	zassert_equal(srv.checks, 0, "a paused device doesn't ask");
	zassert_true(in_slot(SLOT1, 0, (const uint8_t[8]){0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 8));

	tracegale_update_pause(false);
	srv.on_get = pause_at_10;
	zassert_ok(checkin(), "a paused download isn't a failed check-in");
	zassert_equal(srv.gets, 10, "it stops within one message");
	zassert_equal(update_state(), TRACEGALE_UPDATE_DOWNLOADING);
	zassert_equal(update_offset(), 10 * BLOCK);
	zassert_equal(srv.results, 0);

	zassert_ok(checkin());
	zassert_equal(srv.checks, 1);
	zassert_equal(srv.gets, 10);

	srv.on_get = NULL;
	tracegale_update_pause(false);
	zassert_ok(checkin());
	zassert_equal(srv.first_offset, 10 * BLOCK);
	zassert_equal(srv.bytes_sent, sizeof(next));
	device_restart();
	expect_installed(next, sizeof(next), 5);
}

static void change_at_10(void)
{
	if (srv.gets == 10) {
		offer(other, sizeof(other), 6);
	}
}

ZTEST(ota, test_rollout_changes_during_a_download)
{
	offer(next, sizeof(next), 5);
	srv.on_get = change_at_10;
	zassert_ok(checkin());
	zassert_equal(srv.checks, 2, "told that the image is gone, the device asks what to do now");
	zassert_equal(srv.gets, 10 + DIV_ROUND_UP(sizeof(other), BLOCK));
	zassert_equal(srv.results, 0, "an image that was withdrawn isn't a failure");
	zassert_true(in_slot(SLOT1, image_start(), other, sizeof(other)));
	zassert_true(blank_from(SLOT1, image_start() + ROUND_UP(sizeof(other), PAGE)) == false);
	device_restart();
	expect_installed(other, sizeof(other), 6);
}

static void withdraw_at_10(void)
{
	if (srv.gets == 10) {
		srv.offer = false;
	}
}

ZTEST(ota, test_rollout_stopped_during_a_download)
{
	offer(next, sizeof(next), 5);
	srv.on_get = withdraw_at_10;
	zassert_ok(checkin());
	zassert_equal(srv.checks, 2);
	zassert_equal(srv.gets, 10);
	zassert_equal(srv.results, 0);
	zassert_equal(update_state(), TRACEGALE_UPDATE_DOWNLOADING, "what's in the slot stays, in case it's offered again");

	/* It is, and the download continues. */
	srv.on_get = NULL;
	srv.offer = true;
	zassert_ok(checkin());
	zassert_equal(srv.first_offset, 9 * BLOCK);
	zassert_equal(srv.bytes_sent, sizeof(next));
	zassert_equal(update_state(), TRACEGALE_UPDATE_RESTART_PENDING);
}

ZTEST(ota, test_server_keeps_refusing_the_download)
{
	offer(next, sizeof(next), 5);
	srv.error_on_get = TG_WIRE_ERR_BAD_OFFSET;
	zassert_equal(checkin(), -TRACEGALE_ERR_SERVER);
	zassert_equal(srv.checks, 2, "it asks once more, not forever");
	zassert_equal(srv.gets, 2);
	zassert_equal(srv.results, 0);

	srv.error_on_get = TG_WIRE_ERR_RATE_LIMITED;
	zassert_equal(checkin(), -TRACEGALE_ERR_SERVER);
	zassert_equal(srv.checks, 3);
	zassert_equal(srv.gets, 3);
}

ZTEST(ota, test_same_image_under_a_new_reference)
{
	offer(next, sizeof(next), 5);
	srv.drop_get_at = 20;
	zassert_equal(checkin(), -TRACEGALE_ERR_TIMEOUT);

	/* A new rollout of the same image. */
	srv.image_ref = 9;
	zassert_ok(checkin());
	zassert_equal(srv.first_offset, 19 * BLOCK, "the download continues");
	zassert_equal(srv.bytes_sent, sizeof(next));
	device_restart();
	expect_installed(next, sizeof(next), 9);
}

ZTEST(ota, test_another_image_replaces_an_unfinished_one)
{
	offer(next, sizeof(next), 5);
	srv.drop_get_at = 20;
	zassert_equal(checkin(), -TRACEGALE_ERR_TIMEOUT);

	offer(other, sizeof(other), 6);
	zassert_ok(checkin());
	zassert_equal(srv.first_offset, 0);
	zassert_true(in_slot(SLOT1, image_start(), other, sizeof(other)));
	zassert_equal(srv.results, 0);
	device_restart();
	expect_installed(other, sizeof(other), 6);
}

ZTEST(ota, test_server_errors_when_asked)
{
	offer(next, sizeof(next), 5);
	/* A refusal that asking again can't change doesn't fail the check-in. */
	srv.error_on_check = TG_WIRE_ERR_NOT_AUTHORIZED;
	zassert_ok(checkin());
	zassert_equal(srv.checks, 1);
	zassert_equal(srv.gets, 0);

	srv.error_on_check = TG_WIRE_ERR_RATE_LIMITED;
	zassert_equal(checkin(), -TRACEGALE_ERR_SERVER);
	srv.error_on_check = TG_WIRE_ERR_SERVER_BUSY;
	zassert_equal(checkin(), -TRACEGALE_ERR_SERVER);
	zassert_equal(srv.gets, 0);
	zassert_equal(update_state(), TRACEGALE_UPDATE_NONE);
}

ZTEST(ota, test_stored_record_that_is_not_ours)
{
	struct record record = {.version = 2, .phase = TRACEGALE_UPDATE_REPORTING, .outcome = TRACEGALE_UPDATE_REVERTED,
				.image_ref = 77};

	/* Written by a later version of this firmware, before a downgrade. */
	record_write(&record, sizeof(record));
	device_restart();
	zassert_equal(update_state(), TRACEGALE_UPDATE_NONE);
	zassert_equal(update_result(), TRACEGALE_UPDATE_NO_RESULT);

	record.version = 1;
	record.phase = 9;
	record_write(&record, sizeof(record));
	device_restart();
	zassert_equal(update_state(), TRACEGALE_UPDATE_NONE);

	record.phase = TRACEGALE_UPDATE_REPORTING;
	record_write(&record, sizeof(record) - 1);
	device_restart();
	zassert_equal(update_state(), TRACEGALE_UPDATE_NONE);

	/* And updates still work. */
	download_next();
	device_restart();
	expect_installed(next, sizeof(next), 5);
}

ZTEST(ota, test_offline_during_trial_then_online)
{
	download_next();
	device_restart();
	srv.offline = true;
	zassert_equal(checkin(), -TRACEGALE_ERR_CONNECT);
	zassert_equal(update_state(), TRACEGALE_UPDATE_TESTING, "an image is confirmed only once it reached the server");
	srv.offline = false;
	expect_installed(next, sizeof(next), 5);
	k_sleep(K_SECONDS(TRIAL + 1));
	zassert_equal(restarts.revert, 0);
}

ZTEST_SUITE(ota, NULL, setup, before, NULL, NULL);
