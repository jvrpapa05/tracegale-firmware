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
 * @brief Makes these credentials the ones the link uses. They stay at these addresses until tg_cred_release().
 *
 * It may be lazy: a modem that's off when the device is provisioned can keep the pointers, and write the credentials to the
 * modem in open(). Return an error only if they can never work, since it fails provisioning.
 *
 * @return 0, or a negative tracegale_error.
 */
int tg_cred_install(const uint8_t cred[TRACEGALE_CREDENTIAL_COUNT][CONFIG_TRACEGALE_CRED_MAX_SIZE],
		    const uint16_t len[TRACEGALE_CREDENTIAL_COUNT]);

/**
 * @brief Called after tg_cred_install() worked, with the store's copy of the private key.
 *
 * Where the link keeps the key for good, replace it with something that isn't the key, and set *len, so no copy stays in the
 * store's memory or settings storage. A backend that must write the key at every session, such as one whose modem forgets
 * it at power-off, leaves it as it is: the key then stays in settings storage, in plain flash unless the application
 * encrypts it. That is the same as with Zephyr's own TLS.
 */
void tg_cred_forget_key(uint8_t key[CONFIG_TRACEGALE_CRED_MAX_SIZE], uint16_t *len);

/** @brief Called before the installed credentials' memory changes, so the link stops reading it. */
void tg_cred_release(void);

/**
 * @brief Removes the credentials for good, when the device is erased.
 *
 * A link that's off may remove them at its next open() instead, before connecting: it must never connect with erased
 * credentials. @return 0, or a negative tracegale_error.
 */
int tg_cred_erase(void);

#endif /* TG_CRED_H_ */
