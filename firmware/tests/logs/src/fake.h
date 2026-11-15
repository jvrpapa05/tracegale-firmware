/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * A server that answers HELLO and keeps every LOGS message it's sent, decoded, for the tests to check.
 */

#ifndef FAKE_H_
#define FAKE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_MESSAGES 8
#define MAX_RECORDS  24
#define TEXT_KEPT    64

struct rec {
	uint32_t seq;
	uint64_t uptime_ms;
	uint32_t level;
	char module[33];
	/* The text's start, and its full length. */
	char text[TEXT_KEPT + 1];
	size_t text_len;
};

struct logs_msg {
	uint32_t id;
	size_t len;
	uint32_t boot;
	bool has_uptime;
	uint64_t uptime_ms;
	bool has_dropped;
	uint32_t dropped;
	int count;
	struct rec rec[MAX_RECORDS];
};

struct fake_server {
	/* Which LOGS message, counting from 1, loses its reply or is refused, and with what code. */
	int lose_at;
	int refuse_at;
	uint32_t refuse_code;
	/* Called while a LOGS message is on its way. */
	void (*during)(void);
	int hellos;
	int messages;
	struct logs_msg msg[MAX_MESSAGES];
};

extern struct fake_server srv;

void fake_reset(void);
/* Sends what's waiting, so each test starts with an empty buffer, then resets the server. */
void fake_before(void *fixture);
/* Hands records waiting in Zephyr's deferred mode to the backends. */
void fake_flush(void);

#endif /* FAKE_H_ */
