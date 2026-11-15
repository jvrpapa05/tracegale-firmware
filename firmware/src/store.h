/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TG_STORE_H_
#define TG_STORE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tracegale/tracegale.h>

/** Loads anything stored earlier. Called once by the Tracegale thread. */
void tg_store_load(void);

/** @return true once a server and all credentials are in use. */
bool tg_store_ready(void);

/** Bracket a connection attempt. Provisioning waits for the attempt to finish, so TLS never sees credentials change. */
void tg_store_attempt_begin(void);
void tg_store_attempt_end(void);

/** The credential in use, or NULL with *len 0 when there's none. It stays valid until the attempt ends. */
const uint8_t *tg_store_credential(enum tracegale_credential type, size_t *len);

const char *tg_store_host(void);
uint16_t tg_store_port(void);

/** Adds to a credential being entered in pieces, for the shell. @return 0, or a negative tracegale_error. */
int tg_store_append(enum tracegale_credential type, const uint8_t *data, size_t len);

/** Starts a credential again, for the shell. */
void tg_store_clear_pending(enum tracegale_credential type);

/** Sizes of the values waiting for commit, for the shell. */
size_t tg_store_pending_len(enum tracegale_credential type);

#endif /* TG_STORE_H_ */
