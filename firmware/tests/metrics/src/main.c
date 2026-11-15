/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "core.h"
#include "fake.h"
#include "metrics.h"
#include "wire.h"

TRACEGALE_METRIC_GAUGE_DEFINE(battery_mv);
TRACEGALE_METRIC_GAUGE_DEFINE(temperature_c);
TRACEGALE_METRIC_COUNTER_DEFINE(door_opened);
TRACEGALE_METRIC_COUNTER_DEFINE(bytes_logged);
TRACEGALE_METRIC_DECLARE(defined_elsewhere);

/* Twelve gauges and twelve counters with names of the greatest length, which don't fit one message. */
#define FILLERS(X) X(00) X(01) X(02) X(03) X(04) X(05) X(06) X(07) X(08) X(09) X(10) X(11)
#define FILLER_COUNT 24
#define DEFINE_FILLERS(n)                                                                                                    \
	TRACEGALE_METRIC_GAUGE_DEFINE(metrics_with_a_very_long_name_##n);                                                   \
	TRACEGALE_METRIC_COUNTER_DEFINE(counter_with_a_very_long_name_##n);
FILLERS(DEFINE_FILLERS)
BUILD_ASSERT(sizeof("metrics_with_a_very_long_name_00") == 33 && sizeof("counter_with_a_very_long_name_00") == 33);

/* Gauges get -5 to 6, and counters 100 to 111. */
#define SET_FILLERS(n)                                                                                                       \
	TRACEGALE_METRIC_SET(metrics_with_a_very_long_name_##n, 1##n - 105);                                                \
	TRACEGALE_METRIC_ADD(counter_with_a_very_long_name_##n, 1##n);

#if defined(CONFIG_TRACEGALE_METRICS_HEAP) && defined(K_HEAP_MEM_POOL_SIZE) && (K_HEAP_MEM_POOL_SIZE > 0)
#define HAVE_HEAP 1
#else
#define HAVE_HEAP 0
#endif
#define BLOCK 96

/* How often a metric is in the messages kept, and its value there. */
static int times(const char *name, bool gauge, int32_t *value)
{
	int found = 0;

	for (int i = 0; i < srv.metrics; i++) {
		const struct pair *pair = gauge ? find_gauge(&srv.report[i], name) : find_counter(&srv.report[i], name);

		if (pair != NULL) {
			found++;
			*value = pair->value;
		}
	}
	return found;
}

static void check_in(void)
{
	fake_reset();
	zassert_ok(tg_core_attempt());
	zassert_equal(srv.hellos, 1);
	zassert_true(srv.metrics >= 1);
}

ZTEST(metrics, test_a_report_follows_each_check_in)
{
	struct tracegale_status before, after;
	const struct report *report = &srv.report[0];

	tracegale_get_status(&before);
	k_sleep(K_MSEC(100));
	check_in();
	zassert_equal(srv.metrics, 1);
	zassert_true(report->has_system);
	zassert_true(report->has[TG_SYS_UPTIME_S]);
	zassert_true(report->has[TG_SYS_CHECKIN_FAILURES]);
	zassert_equal(report->system[TG_SYS_CHECKIN_FAILURES], 0);
	zassert_true(report->has[TG_SYS_CHECKIN_MS]);
	zassert_equal(report->gauges, 0, "a gauge that wasn't set is left out");
	zassert_equal(report->counters, 0, "and so is a counter that didn't change");

	zassert_equal(report->has[TG_SYS_HEAP_FREE], HAVE_HEAP);
	zassert_equal(report->has[TG_SYS_HEAP_LOW], HAVE_HEAP);
	zassert_equal(report->has[TG_SYS_STACK_LOW], IS_ENABLED(CONFIG_TRACEGALE_METRICS_STACK));
	zassert_equal(report->has[TG_SYS_CPU_LOAD], IS_ENABLED(CONFIG_TRACEGALE_METRICS_CPU));
	zassert_equal(report->has[TG_SYS_NET_SENT], IS_ENABLED(CONFIG_TRACEGALE_METRICS_NET));
	zassert_equal(report->has[TG_SYS_NET_RECEIVED], IS_ENABLED(CONFIG_TRACEGALE_METRICS_NET));
	if (IS_ENABLED(CONFIG_TRACEGALE_METRICS_STACK)) {
		zassert_true(report->system[TG_SYS_STACK_LOW] > 0 && report->system[TG_SYS_STACK_LOW] < 1024 * 1024,
			     "stack_low is %u", report->system[TG_SYS_STACK_LOW]);
	}
	zassert_true(report->system[TG_SYS_CPU_LOAD] <= 1000);
	/* The protocol's figure for a report of everything the firmware measures is 45 bytes. */
	zassert_true(report->len <= 64, "a report of %zu bytes", report->len);

	tracegale_get_status(&after);
	zassert_equal(after.metrics_sent, before.metrics_sent + 1);
	zassert_equal(after.last_message_id, srv.hello_id, "the check-in's ID, which the server's registry shows for the device");
}

ZTEST(metrics, test_heap_is_measured)
{
#if HAVE_HEAP
	uint32_t free_before, low_before;
	void *block;

	check_in();
	free_before = srv.report[0].system[TG_SYS_HEAP_FREE];
	low_before = srv.report[0].system[TG_SYS_HEAP_LOW];
	zassert_true(low_before <= free_before);

	/* Zephyr's own parts can ask for a heap of a few hundred bytes, which the application didn't. */
	block = k_malloc(BLOCK);
	zassert_not_null(block, "no %d bytes in a heap with %u free", BLOCK, free_before);
	check_in();
	zassert_true(srv.report[0].system[TG_SYS_HEAP_FREE] + BLOCK <= free_before);
	k_free(block);
	check_in();
	zassert_equal(srv.report[0].system[TG_SYS_HEAP_FREE], free_before);
	zassert_true(srv.report[0].system[TG_SYS_HEAP_LOW] + BLOCK <= free_before, "the lowest point stays on record");
#else
	/* Without a heap there's no k_malloc() to build against. */
	ztest_test_skip();
#endif
}

ZTEST(metrics, test_a_gauge_reports_its_latest_reading)
{
	int32_t value = 0;

	TRACEGALE_METRIC_SET(battery_mv, 3800);
	TRACEGALE_METRIC_SET(battery_mv, 3712);
	TRACEGALE_METRIC_SET(temperature_c, -4);
	check_in();
	zassert_equal(srv.report[0].gauges, 2);
	zassert_equal(times("battery_mv", true, &value), 1);
	zassert_equal(value, 3712);
	zassert_equal(times("temperature_c", true, &value), 1);
	zassert_equal(value, -4);

	check_in();
	zassert_equal(srv.report[0].gauges, 0, "no reading in this period");

	TRACEGALE_METRIC_SET(battery_mv, 3712);
	check_in();
	zassert_equal(srv.report[0].gauges, 1, "the same value again is still a reading");
}

ZTEST(metrics, test_a_counter_starts_again_once_reported)
{
	int32_t value = 0;

	TRACEGALE_METRIC_ADD(door_opened, 5);
	TRACEGALE_METRIC_ADD(door_opened, 7);
	TRACEGALE_METRIC_ADD(defined_elsewhere, 1);
	check_in();
	zassert_equal(srv.report[0].counters, 2);
	zassert_equal(times("door_opened", false, &value), 1);
	zassert_equal(value, 12);
	zassert_equal(times("defined_elsewhere", false, &value), 1);
	zassert_equal(value, 1);

	check_in();
	zassert_equal(srv.report[0].counters, 0);

	TRACEGALE_METRIC_ADD(door_opened, 3);
	check_in();
	zassert_equal(times("door_opened", false, &value), 1);
	zassert_equal(value, 3);
}

ZTEST(metrics, test_values_stay_inside_32_bits)
{
	int32_t value = 0;

	TRACEGALE_METRIC_ADD(bytes_logged, INT32_MAX);
	TRACEGALE_METRIC_ADD(bytes_logged, 5);
	TRACEGALE_METRIC_SET(temperature_c, INT32_MIN);
	check_in();
	zassert_equal(times("bytes_logged", false, &value), 1);
	zassert_equal(value, INT32_MAX, "a total that's too large stays at the limit");
	zassert_equal(times("temperature_c", true, &value), 1);
	zassert_equal(value, INT32_MIN);

	TRACEGALE_METRIC_ADD(bytes_logged, INT32_MIN);
	TRACEGALE_METRIC_ADD(bytes_logged, -5);
	check_in();
	zassert_equal(times("bytes_logged", false, &value), 1);
	zassert_equal(value, INT32_MIN);
}

ZTEST(metrics, test_nothing_is_lost_when_the_reply_is)
{
	int32_t value = 0;

	k_sleep(K_SECONDS(2));
	TRACEGALE_METRIC_ADD(door_opened, 5);
	TRACEGALE_METRIC_SET(battery_mv, 3712);
	fake_reset();
	srv.lose_at = 1;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_TIMEOUT);
	zassert_equal(times("door_opened", false, &value), 1, "it was sent");

	k_sleep(K_SECONDS(2));
	TRACEGALE_METRIC_ADD(door_opened, 1);
	check_in();
	zassert_equal(times("door_opened", false, &value), 1);
	zassert_equal(value, 6);
	zassert_equal(times("battery_mv", true, &value), 1);
	zassert_equal(value, 3712);
	zassert_equal(srv.report[0].system[TG_SYS_CHECKIN_FAILURES], 1);
	zassert_true(srv.report[0].period >= 4 && srv.report[0].period <= 6, "a period of %u s", srv.report[0].period);
	zassert_true(srv.report[0].system[TG_SYS_UPTIME_S] >= 4);

	check_in();
	zassert_equal(srv.report[0].system[TG_SYS_CHECKIN_FAILURES], 0);
	zassert_true(srv.report[0].period <= 1);
	zassert_equal(srv.report[0].counters, 0);
}

static void more_while_sending(void)
{
	TRACEGALE_METRIC_ADD(door_opened, 2);
	TRACEGALE_METRIC_SET(battery_mv, 3600);
}

ZTEST(metrics, test_what_changes_while_sending_goes_in_the_next_report)
{
	int32_t value = 0;

	TRACEGALE_METRIC_ADD(door_opened, 5);
	TRACEGALE_METRIC_SET(battery_mv, 3700);
	fake_reset();
	srv.during = more_while_sending;
	zassert_ok(tg_core_attempt());
	zassert_equal(times("door_opened", false, &value), 1);
	zassert_equal(value, 5);
	zassert_equal(times("battery_mv", true, &value), 1);
	zassert_equal(value, 3700);

	check_in();
	zassert_equal(times("door_opened", false, &value), 1);
	zassert_equal(value, 2);
	zassert_equal(times("battery_mv", true, &value), 1);
	zassert_equal(value, 3600);

	/* The same when the first report was lost: the counter holds both, and the gauge its newest reading. */
	TRACEGALE_METRIC_ADD(door_opened, 5);
	TRACEGALE_METRIC_SET(battery_mv, 3700);
	fake_reset();
	srv.during = more_while_sending;
	srv.lose_at = 1;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_TIMEOUT);
	check_in();
	zassert_equal(times("door_opened", false, &value), 1);
	zassert_equal(value, 7);
	zassert_equal(times("battery_mv", true, &value), 1);
	zassert_equal(value, 3600);
}

ZTEST(metrics, test_a_busy_server_gets_the_values_later)
{
	static const uint32_t busy[] = {TG_WIRE_ERR_RATE_LIMITED, TG_WIRE_ERR_SERVER_BUSY};
	struct tracegale_status before, after;
	int32_t value = 0;

	for (size_t i = 0; i < ARRAY_SIZE(busy); i++) {
		TRACEGALE_METRIC_ADD(door_opened, 4);
		TRACEGALE_METRIC_SET(battery_mv, 3712);
		tracegale_get_status(&before);
		fake_reset();
		srv.refuse_at = 1;
		srv.refuse_code = busy[i];
		zassert_ok(tg_core_attempt(), "a refused report mustn't fail the check-in, or hold back an update");
		tracegale_get_status(&after);
		zassert_equal(after.last_error, TRACEGALE_ERR_SERVER);
		zassert_equal(after.last_server_error, busy[i]);
		zassert_equal(after.metrics_sent, before.metrics_sent);
		zassert_equal(after.successes, before.successes + 1);

		check_in();
		zassert_equal(times("door_opened", false, &value), 1);
		zassert_equal(value, 4);
		zassert_equal(times("battery_mv", true, &value), 1);
		zassert_equal(srv.report[0].system[TG_SYS_CHECKIN_FAILURES], 0, "the check-in itself worked");
	}
}

ZTEST(metrics, test_a_report_refused_for_good_is_dropped)
{
	/* 3 is unsupported_type, which a server from before metrics answers. */
	TRACEGALE_METRIC_ADD(door_opened, 4);
	TRACEGALE_METRIC_SET(battery_mv, 3712);
	k_sleep(K_SECONDS(2));
	fake_reset();
	srv.refuse_at = 1;
	srv.refuse_code = 3;
	zassert_ok(tg_core_attempt());

	check_in();
	zassert_equal(srv.report[0].gauges, 0);
	zassert_equal(srv.report[0].counters, 0);
	zassert_true(srv.report[0].period <= 1, "the period started again too");
}

static void check_fillers(void)
{
	int32_t value = 0;

#define CHECK_FILLERS(n)                                                                                                     \
	zassert_equal(times("metrics_with_a_very_long_name_" #n, true, &value), 1);                                         \
	zassert_equal(value, 1##n - 105);                                                                                   \
	zassert_equal(times("counter_with_a_very_long_name_" #n, false, &value), 1);                                        \
	zassert_equal(value, 1##n);
	FILLERS(CHECK_FILLERS)
}

ZTEST(metrics, test_a_large_report_takes_several_messages)
{
	int total = 0;

	FILLERS(SET_FILLERS)
	k_sleep(K_SECONDS(1));
	check_in();
	zassert_true(srv.metrics >= 3, "24 metrics of about 36 bytes in messages of %d", CONFIG_TRACEGALE_MAX_MESSAGE);
	for (int i = 0; i < srv.metrics; i++) {
		zassert_equal(srv.report[i].has_system, i == 0, "what the firmware measured is sent once");
		zassert_equal(srv.report[i].period, srv.report[0].period, "one report has one period");
		zassert_true(i == 0 || srv.report[i].id != srv.report[i - 1].id);
		total += srv.report[i].gauges + srv.report[i].counters;
	}
	zassert_equal(total, FILLER_COUNT, "each metric is in one message only");
	check_fillers();

	check_in();
	zassert_equal(srv.metrics, 1);
	zassert_equal(srv.report[0].gauges + srv.report[0].counters, 0);
}

ZTEST(metrics, test_a_report_cut_short_sends_the_rest_later)
{
	int first, rest = 0;

	FILLERS(SET_FILLERS)
	fake_reset();
	srv.lose_at = 2;
	zassert_equal(tg_core_attempt(), -TRACEGALE_ERR_TIMEOUT);
	zassert_equal(srv.metrics, 2);
	first = srv.report[0].gauges + srv.report[0].counters;
	zassert_true(first > 0 && first < FILLER_COUNT);

	check_in();
	zassert_true(srv.report[0].has_system);
	zassert_equal(srv.report[0].system[TG_SYS_CHECKIN_FAILURES], 1);
	for (int i = 0; i < srv.metrics; i++) {
		rest += srv.report[i].gauges + srv.report[i].counters;
	}
	zassert_equal(first + rest, FILLER_COUNT, "what the server acknowledged isn't sent again, and the rest is");
}

ZTEST(metrics, test_the_shell_reads_without_reporting)
{
	struct tg_metrics_system system;

	TRACEGALE_METRIC_ADD(door_opened, 9);
	tg_metrics_system_get(&system);
	zassert_true(system.count >= 3 && system.count <= TG_SYS_COUNT);
	for (int i = 1; i < system.count; i++) {
		zassert_true(system.key[i] > system.key[i - 1], "keys ascend, as the message format needs");
	}
	check_in();
	zassert_equal(srv.report[0].counters, 1, "looking changed nothing");
}

ZTEST_SUITE(metrics, NULL, NULL, fake_before, NULL, NULL);
