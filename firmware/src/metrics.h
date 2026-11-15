/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Metrics: what the firmware measures by itself, and the application's own gauges and counters, sent with METRICS.
 */

#ifndef TG_METRICS_H_
#define TG_METRICS_H_

#include <stddef.h>
#include <stdint.h>

#include "transport.h"

/* The keys of METRICS' "system" map, from docs/protocol/README.md. */
enum tg_metrics_system_key {
	TG_SYS_UPTIME_S = 1,
	TG_SYS_HEAP_FREE = 2,
	TG_SYS_HEAP_LOW = 3,
	TG_SYS_STACK_LOW = 4,
	TG_SYS_CPU_LOAD = 5,
	TG_SYS_NET_SENT = 6,
	TG_SYS_NET_RECEIVED = 7,
	TG_SYS_CHECKIN_FAILURES = 8,
	TG_SYS_CHECKIN_MS = 9,
	TG_SYS_COUNT = 9,
};

/* What the firmware measured by itself, in ascending key order. */
struct tg_metrics_system {
	uint8_t count;
	uint8_t key[TG_SYS_COUNT];
	uint32_t value[TG_SYS_COUNT];
};

/**
 * Sends a report on an open connection: everything measured since the last report that the server acknowledged.
 *
 * @param buf a buffer for messages, and its size.
 * @param next_id gives each message a new ID.
 * @return 0, or a negative tracegale_error. -TRACEGALE_ERR_SERVER means the server answered and refused the report, with its
 *         error code in *detail: the connection is still good. Anything else means the connection isn't.
 */
int tg_metrics_send(const struct tg_transport *transport, uint8_t *buf, size_t cap, uint32_t (*next_id)(void),
		    int *detail);

/** Notes how long the last check-in took, from opening the connection to the reply to HELLO. */
void tg_metrics_checkin(uint32_t ms);

/** Notes an attempt that failed. */
void tg_metrics_failure(void);

/** Reads the current values, without reporting them. Counters hold what the next report would. */
void tg_metrics_system_get(struct tg_metrics_system *out);

/** @return the reports the server has acknowledged since boot. */
uint32_t tg_metrics_sent(void);

#endif /* TG_METRICS_H_ */
