/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Over-the-air updates: an image is downloaded into MCUboot's second slot, and MCUboot decides whether it runs.
 */

#ifndef TG_OTA_H_
#define TG_OTA_H_

#include <stddef.h>
#include <stdint.h>

#include <tracegale/tracegale.h>

#include "transport.h"

/** At boot, after settings are ready: works out what became of an update that was in progress. */
void tg_ota_boot(void);

/** After the server accepted HELLO: a new image that can reach the server is confirmed, so MCUboot keeps it. */
void tg_ota_connected(void);

/**
 * On an open connection: reports the last update, or asks for one and downloads it.
 *
 * @param pending the "pending" field of the server's ACK.
 * @return 0, or a negative tracegale_error. *detail is a server error code when the server refused something. An
 *         unfinished download isn't an error: it continues at the next check-in.
 */
int tg_ota_run(const struct tg_transport *transport, uint8_t *buf, size_t cap, uint32_t (*next_id)(void),
	       uint32_t pending, int *detail);

/** After the connection closed: restarts if a downloaded image is waiting to be installed. */
void tg_ota_closed(void);

/** Fills in the update fields. */
void tg_ota_status(struct tracegale_status *out);

#endif /* TG_OTA_H_ */
