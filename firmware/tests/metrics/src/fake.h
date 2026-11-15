/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * A connection that answers as a server would, and keeps every METRICS message it was sent, decoded.
 */

#ifndef FAKE_H_
#define FAKE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_REPORTS 6
#define MAX_PAIRS 24

struct pair {
	char name[33];
	int32_t value;
};

struct report {
	uint32_t id;
	uint32_t period;
	size_t len;
	bool has_system;
	/* By the key in the "system" map. */
	bool has[16];
	uint32_t system[16];
	int gauges;
	int counters;
	struct pair gauge[MAX_PAIRS];
	struct pair counter[MAX_PAIRS];
};

struct fake_server {
	/* The METRICS message of this attempt, counted from 1, whose reply is lost. */
	int lose_at;
	/* The METRICS message of this attempt that's answered with ERROR, and the code. */
	int refuse_at;
	uint32_t refuse_code;
	/* Called while a METRICS message is on its way, as an interrupt or another thread could run. */
	void (*during)(void);
	/* Messages of each type seen since the last reset. */
	int hellos;
	uint32_t hello_id;
	int metrics;
	struct report report[MAX_REPORTS];
};

extern struct fake_server srv;

/** Forgets what was seen and asked for. */
void fake_reset(void);

/** For ZTEST_SUITE: sends a report that's acknowledged, so that each test starts with nothing waiting, then resets. */
void fake_before(void *fixture);

/** @return the value of a named metric in one message, or NULL. */
const struct pair *find_gauge(const struct report *report, const char *name);
const struct pair *find_counter(const struct report *report, const char *name);

#endif /* FAKE_H_ */
