/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "core.h"
#include "fake.h"
#include "logs.h"

LOG_MODULE_REGISTER(test, LOG_LEVEL_DBG);

/* In deferred mode, records reach the backend from Zephyr's log thread, a moment later. */
static void flush(void)
{
	fake_flush();
}

/* The records this test logged, from every message the server kept, in the order they arrived. */
static int mine(const struct rec **out, int max)
{
	int n = 0;

	for (int m = 0; m < srv.messages; m++) {
		for (int i = 0; i < srv.msg[m].count; i++) {
			if (strcmp(srv.msg[m].rec[i].module, "test") == 0 && n < max) {
				out[n++] = &srv.msg[m].rec[i];
			}
		}
	}
	return n;
}

static void check_in(void)
{
	flush();
	fake_reset();
	zassert_ok(tg_core_attempt());
	zassert_equal(srv.hellos, 1);
}

ZTEST(logs, test_records_go_with_the_check_in)
{
	const struct rec *r[8];
	struct tracegale_status before, after;
	uint64_t logged_at = k_uptime_get();

	tracegale_get_status(&before);
	LOG_INF("Pump started at %d rpm", 1450);
	LOG_WRN("Pressure %s", "high");
	LOG_ERR("Valve %d stuck", 3);
	LOG_DBG("Below the level Tracegale keeps");
	check_in();
	zassert_true(srv.messages >= 1);
	zassert_equal(mine(r, ARRAY_SIZE(r)), 3);
	zassert_equal(r[0]->level, 3);
	zassert_str_equal(r[0]->text, "Pump started at 1450 rpm");
	zassert_equal(r[1]->level, 2);
	zassert_str_equal(r[1]->text, "Pressure high");
	zassert_equal(r[2]->level, 1);
	zassert_str_equal(r[2]->text, "Valve 3 stuck");
	zassert_true(r[0]->uptime_ms >= logged_at && r[0]->uptime_ms <= k_uptime_get());

	tracegale_get_status(&after);
	zassert_true(after.logs_sent >= before.logs_sent + 3);

	/* Sent once only. */
	check_in();
	zassert_equal(mine(r, ARRAY_SIZE(r)), 0);
}

ZTEST(logs, test_text_is_made_safe)
{
	const struct rec *r[8];
	static const uint8_t data[16];

	/* Straight to the buffer, because the console would pass these bytes to the terminal of whoever runs the test. */
	static const char unsafe[] = "Line one\nline two\x1b[2J \xc3\xa9\n";

	tg_logs_add(3, "test", unsafe, sizeof(unsafe) - 1);
	LOG_HEXDUMP_INF(data, sizeof(data), "Frame");
	check_in();
	zassert_equal(mine(r, ARRAY_SIZE(r)), 2);
	/* A trailing new line goes. Anything else outside printable ASCII becomes "?". */
	zassert_str_equal(r[0]->text, "Line one?line two?[2J ??");
	zassert_str_equal(r[1]->text, "Frame (16 bytes of data)", "a hex dump's bytes stay on the device");
}

ZTEST(logs, test_long_text_is_cut)
{
	const struct rec *r[4];
	char long_text[300];

	memset(long_text, 'x', sizeof(long_text) - 1);
	long_text[sizeof(long_text) - 1] = '\0';
	LOG_WRN("%s", long_text);
	check_in();
	zassert_equal(mine(r, ARRAY_SIZE(r)), 1);
	/* Cut to the configured length, or shorter still where that's longer than a whole message. */
	zassert_true(r[0]->text_len <= CONFIG_TRACEGALE_LOGS_LINE_MAX && r[0]->text_len >= 100, "%zu bytes", r[0]->text_len);
	zassert_true(srv.msg[0].len <= CONFIG_TRACEGALE_MAX_MESSAGE);
}

ZTEST(logs, test_a_lost_reply_sends_the_same_records_again)
{
	const struct rec *r[8];
	uint32_t first_seq, boot;

	LOG_INF("Before the connection broke");
	flush();
	fake_reset();
	srv.lose_at = 1;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_TIMEOUT);
	zassert_equal(mine(r, ARRAY_SIZE(r)), 1);
	first_seq = r[0]->seq;
	boot = srv.msg[0].boot;

	LOG_INF("After");
	check_in();
	zassert_equal(mine(r, ARRAY_SIZE(r)), 2);
	zassert_equal(r[0]->seq, first_seq, "the same record, with the same seq, so the server stores it once");
	zassert_str_equal(r[0]->text, "Before the connection broke");
	zassert_str_equal(r[1]->text, "After");
	zassert_equal(srv.msg[0].boot, boot, "one start, one boot");
}

ZTEST(logs, test_a_busy_server_gets_them_later)
{
	const struct rec *r[8];
	struct tracegale_status status;

	LOG_INF("Kept for later");
	flush();
	fake_reset();
	srv.refuse_at = 1;
	srv.refuse_code = 12;
	zassert_ok(tg_core_attempt(), "a refusal never fails the check-in");
	tracegale_get_status(&status);
	zassert_equal(status.last_server_error, 12);
	check_in();
	zassert_equal(mine(r, ARRAY_SIZE(r)), 1);
	zassert_str_equal(r[0]->text, "Kept for later");
}

ZTEST(logs, test_a_refusal_for_good_drops_them)
{
	const struct rec *r[8];

	LOG_INF("A server older than logs can't take this");
	flush();
	fake_reset();
	srv.refuse_at = 1;
	srv.refuse_code = 3;
	zassert_ok(tg_core_attempt());
	check_in();
	zassert_equal(mine(r, ARRAY_SIZE(r)), 0);
}

ZTEST(logs, test_a_full_buffer_drops_the_oldest)
{
	const struct rec *r[MAX_MESSAGES * MAX_RECORDS];
	struct tg_logs_state state;
	int n, total = CONFIG_TRACEGALE_LOGS_BUFFER_SIZE / 16;
	uint32_t dropped = 0;

	for (int i = 0; i < total; i++) {
		LOG_INF("Record number %04d, which takes some room", i);
		if (IS_ENABLED(CONFIG_LOG_MODE_DEFERRED) && i % 8 == 0) {
			flush();
		}
	}
	flush();
	tg_logs_state_get(&state);
	zassert_true(state.dropped > 0, "the buffer can't hold %d records", total);
	zassert_true(state.used <= CONFIG_TRACEGALE_LOGS_BUFFER_SIZE);
	check_in();
	n = mine(r, ARRAY_SIZE(r));
	zassert_true(n > 0 && n < total);
	char newest[64];

	snprintf(newest, sizeof(newest), "Record number %04d, which takes some room", total - 1);
	zassert_str_equal(r[n - 1]->text, newest, "the newest records are the ones kept");
	for (int m = 0; m < srv.messages; m++) {
		dropped += srv.msg[m].dropped;
	}
	zassert_true(dropped >= state.dropped, "the server hears how many were lost");

	/* Once the server knows, it isn't told again. */
	check_in();
	for (int m = 0; m < srv.messages; m++) {
		zassert_false(srv.msg[m].has_dropped);
	}
}

ZTEST(logs, test_many_records_take_several_messages)
{
	const struct rec *r[MAX_MESSAGES * MAX_RECORDS];
	int n;

	/* As many as the buffer holds, with room for Tracegale's own. */
	int count = CONFIG_TRACEGALE_LOGS_BUFFER_SIZE >= 2048 ? 20 : 6;

	for (int i = 0; i < count; i++) {
		LOG_INF("Reading %02d, with text that takes room", i);
	}
	check_in();
	zassert_true(srv.messages >= 2, "%d records in messages of %d bytes", count, CONFIG_TRACEGALE_MAX_MESSAGE);
	n = mine(r, ARRAY_SIZE(r));
	zassert_equal(n, count);
	for (int i = 0; i < n; i++) {
		char want[48];

		snprintf(want, sizeof(want), "Reading %02d, with text that takes room", i);
		zassert_str_equal(r[i]->text, want);
		zassert_true(i == 0 || r[i]->seq > r[i - 1]->seq);
	}
	for (int m = 1; m < srv.messages; m++) {
		zassert_not_equal(srv.msg[m].id, srv.msg[m - 1].id);
		zassert_equal(srv.msg[m].rec[0].seq, srv.msg[m - 1].rec[srv.msg[m - 1].count - 1].seq + 1, "no gaps between messages");
	}
}

static void log_while_sending(void)
{
	LOG_WRN("Logged while a message was on its way");
}

ZTEST(logs, test_what_is_logged_while_sending_goes_next)
{
	const struct rec *r[8];

	LOG_INF("First");
	flush();
	fake_reset();
	srv.during = log_while_sending;
	zassert_ok(tg_core_attempt());
	srv.during = NULL;
	zassert_true(mine(r, ARRAY_SIZE(r)) >= 1);
	zassert_str_equal(r[0]->text, "First");
	if (mine(r, ARRAY_SIZE(r)) == 1) {
		check_in();
		zassert_equal(mine(r, ARRAY_SIZE(r)), 1);
	}
	zassert_true(strstr(r[mine(r, ARRAY_SIZE(r)) - 1]->text, "while a message") != NULL);
}

/* Many check-ins with records of every length, so records wrap around the end of the buffer many times. */
ZTEST(logs, test_every_record_survives_the_buffer_wrapping)
{
	const struct rec *r[MAX_MESSAGES * MAX_RECORDS];
	static const char filler[] = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
	int next = 0, expected = 0;

	for (int round = 0; round < 30; round++) {
		/* At most four, which the smallest buffer holds with room to spare. */
		int count = 1 + round % 4;

		for (int i = 0; i < count; i++, next++) {
			LOG_INF("%05d %.*s", next, (next * 7) % 60, filler);
		}
		check_in();
		int n = mine(r, ARRAY_SIZE(r));

		zassert_equal(n, count, "round %d", round);
		for (int i = 0; i < n; i++, expected++) {
			char want[16];

			snprintf(want, sizeof(want), "%05d", expected);
			zassert_mem_equal(r[i]->text, want, 5, "round %d: got %s", round, r[i]->text);
			zassert_equal(r[i]->text_len, 6 + (expected * 7) % 60);
		}
	}
}

ZTEST_SUITE(logs, NULL, NULL, fake_before, NULL, NULL);
