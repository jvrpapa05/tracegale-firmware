/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TG_ACK_H_
#define TG_ACK_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "transport.h"

/**
 * Sends a message that the server answers with ACK or ERROR, such as METRICS or LOGS. The reply overwrites buf.
 *
 * @return 0 for ACK. -TRACEGALE_ERR_SERVER for ERROR, with its code in *detail. Anything else means the connection or the
 *         reply was bad.
 */
int tg_exchange_ack(const struct tg_transport *transport, uint8_t *buf, size_t cap, size_t len, uint32_t id, int *detail);

/** @return true if a refusal means "later", so what was sent is kept: rate_limited or server_busy. */
bool tg_refusal_is_temporary(int detail);

#endif /* TG_ACK_H_ */
