/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * The test's stand-in for the nRF Connect SDK's modem key management API: only the calls the backend makes, as the SDK documents
 * them. The fake modem behind it is in main.c.
 */

#ifndef TG_TEST_MODEM_KEY_MGMT_H_
#define TG_TEST_MODEM_KEY_MGMT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t nrf_sec_tag_t;

enum modem_key_mgmt_cred_type {
	MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
	MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT,
	MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT,
};

int modem_key_mgmt_write(nrf_sec_tag_t sec_tag, enum modem_key_mgmt_cred_type cred_type, const void *buf, size_t len);
int modem_key_mgmt_delete(nrf_sec_tag_t sec_tag, enum modem_key_mgmt_cred_type cred_type);
int modem_key_mgmt_exists(nrf_sec_tag_t sec_tag, enum modem_key_mgmt_cred_type cred_type, bool *exists);

#endif /* TG_TEST_MODEM_KEY_MGMT_H_ */
