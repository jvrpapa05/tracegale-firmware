/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Credentials through Zephyr's TLS credentials, which keep pointers into the store's memory. The default.
 */

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/net/tls_credentials.h>

#include "compat.h"
#include "cred.h"

LOG_MODULE_DECLARE(tracegale, CONFIG_TRACEGALE_LOG_LEVEL);

static const int tls_types[TRACEGALE_CREDENTIAL_COUNT] = {TLS_CREDENTIAL_CA_CERTIFICATE, TG_TLS_DEVICE_CERT,
							  TLS_CREDENTIAL_PRIVATE_KEY};
static const char *const names[TRACEGALE_CREDENTIAL_COUNT] = {"ca", "cert", "key"};

void tg_cred_release(void)
{
	for (int i = 0; i < TRACEGALE_CREDENTIAL_COUNT; i++) {
		(void)tls_credential_delete(CONFIG_TRACEGALE_SEC_TAG, tls_types[i]);
	}
}

int tg_cred_install(const uint8_t cred[TRACEGALE_CREDENTIAL_COUNT][CONFIG_TRACEGALE_CRED_MAX_SIZE],
		    const uint16_t len[TRACEGALE_CREDENTIAL_COUNT])
{
	tg_cred_release();
	for (int i = 0; i < TRACEGALE_CREDENTIAL_COUNT; i++) {
		int rc = tls_credential_add(CONFIG_TRACEGALE_SEC_TAG, tls_types[i], cred[i], len[i]);

		if (rc != 0) {
			LOG_ERR("Can't use credential %s: %d", names[i], rc);
			return -TRACEGALE_ERR_STORAGE;
		}
	}
	return 0;
}

void tg_cred_forget_key(uint8_t key[CONFIG_TRACEGALE_CRED_MAX_SIZE], uint16_t *len)
{
	/* Zephyr's TLS reads the key from the store's memory at every connection. */
	ARG_UNUSED(key);
	ARG_UNUSED(len);
}

int tg_cred_erase(void)
{
	tg_cred_release();
	return 0;
}
