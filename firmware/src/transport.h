/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * The only thing Tracegale's features know about a connection. See "Bindings" in docs/protocol/README.md.
 */

#ifndef TG_TRANSPORT_H_
#define TG_TRANSPORT_H_

#include <stddef.h>
#include <stdint.h>

struct tg_transport {
	/** Connects to the provisioned server. @return 0, or a negative tracegale_error, with *detail set if useful. */
	int (*open)(int *detail);
	/** Sends one message, and waits for the reply message. @return 0, or a negative tracegale_error. */
	int (*exchange)(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_cap, size_t *rx_len, int *detail);
	void (*close)(void);
};

const struct tg_transport *tg_transport_get(void);

#endif /* TG_TRANSPORT_H_ */
