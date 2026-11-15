/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Where TLS finds the device's credentials. Zephyr's TLS credentials by default (cred_tls.c), or a modem's own storage when the
 * modem does TLS itself (cred_nrf_modem.c). The store calls these with its lock held.
 */

#ifndef TG_CRED_H_
#define TG_CRED_H_

#include <stddef.h>
#include <stdint.h>

#include <tracegale/tracegale.h>

/**
 * Makes these credentials the ones TLS uses. They stay at these addresses until tg_cred_release().
 * @return 0, or a negative tracegale_error.
 */
int tg_cred_install(const uint8_t cred[TRACEGALE_CREDENTIAL_COUNT][CONFIG_TRACEGALE_CRED_MAX_SIZE],
		    const uint16_t len[TRACEGALE_CREDENTIAL_COUNT]);

/**
 * Called after a successful tg_cred_install with a copy of the private key the store holds. Where a device such as a modem
 * keeps the key itself, it's replaced with a marker that holds only its hash, so no copy stays in the store's memory or in
 * settings storage. Zephyr's TLS credentials need the key itself, and leave it.
 */
void tg_cred_forget_key(uint8_t key[CONFIG_TRACEGALE_CRED_MAX_SIZE], uint16_t *len);

/** Called before the installed credentials' memory changes, so TLS stops reading it. */
void tg_cred_release(void);

/** Removes the credentials for good, when the device is erased. @return 0, or a negative tracegale_error. */
int tg_cred_erase(void);

#endif /* TG_CRED_H_ */
