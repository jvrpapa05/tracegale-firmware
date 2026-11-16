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

/* tg_store_credential(), tg_store_host() and tg_store_port() are public, for custom transports. */
#include <tracegale/transport.h>

/**
 * For enrollment: uses an enrollment key's claim credentials for one connection, without storing them or counting as
 * provisioned. Called with the attempt lock held; tg_store_drop_claim() undoes it. @return 0, or a negative tracegale_error.
 */
int tg_store_use_claim(const char *host, uint16_t port, const char *ca_b64, const char *cert_b64, const uint8_t *key, size_t key_len);
void tg_store_drop_claim(void);

/** For enrollment: sets a credential waiting for commit from base64, decoded in place. @return 0, or a negative tracegale_error. */
int tg_store_pending_b64(enum tracegale_credential type, const char *b64);

/** Adds to a credential being entered in pieces, for the shell. @return 0, or a negative tracegale_error. */
int tg_store_append(enum tracegale_credential type, const uint8_t *data, size_t len);

/** Starts a credential again, for the shell. */
void tg_store_clear_pending(enum tracegale_credential type);

/** Sizes of the values waiting for commit, for the shell. */
size_t tg_store_pending_len(enum tracegale_credential type);

#endif /* TG_STORE_H_ */
