/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Logs: a Zephyr log backend that keeps records in RAM until a check-in sends them with LOGS.
 */

#ifndef TG_LOGS_H_
#define TG_LOGS_H_

#include <stddef.h>
#include <stdint.h>

#include "transport.h"

/**
 * Sends the records waiting, on an open connection, and forgets those the server acknowledged.
 *
 * @return 0, or a negative tracegale_error. -TRACEGALE_ERR_SERVER means the server answered and refused, with its error code
 *         in *detail, and the connection is still good. Anything else means the connection isn't.
 */
int tg_logs_send(const struct tg_transport *transport, uint8_t *buf, size_t cap, uint32_t (*next_id)(void), int *detail);

/** What the buffer holds. */
struct tg_logs_state {
	uint32_t waiting;
	uint32_t dropped;
	uint32_t sent;
	uint32_t next_seq;
	size_t used;
};

void tg_logs_state_get(struct tg_logs_state *out);

/** One waiting record, for the shell. */
struct tg_logs_record {
	uint32_t seq;
	uint64_t uptime_ms;
	uint8_t level;
	const char *module;
	char text[CONFIG_TRACEGALE_LOGS_LINE_MAX + 1];
};

/** Reads the waiting record at a position from the oldest. @return false past the newest. */
bool tg_logs_peek(uint32_t index, struct tg_logs_record *out);

/** Adds a record as the backend does, for tests. */
void tg_logs_add(uint8_t level, const char *module, const char *text, size_t len);

/** Forgets every waiting record and starts again, for tests. */
void tg_logs_reset(void);

#endif /* TG_LOGS_H_ */
