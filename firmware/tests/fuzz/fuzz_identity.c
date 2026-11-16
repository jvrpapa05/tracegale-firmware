/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fuzzes the firmware's reader of its own certificate's common name (src/identity.c), which the MQTT binding uses as the
 * device's identity, and the DER checks provisioning runs on credentials (src/der.c). Credentials arrive at provisioning and
 * enrollment; these must never read out of bounds or write past their output, whatever they're given.
 */

#include <stdint.h>
#include <string.h>

#include "der.h"
#include "identity.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len)
{
	char out[TG_IDENTITY_MAX + 1];

	memset(out, 'x', sizeof(out));
	if (tg_identity_from_cert(data, len, out, sizeof(out)) == 0 && memchr(out, '\0', sizeof(out)) == NULL) {
		__builtin_trap();
	}

	const uint8_t *key;
	size_t key_len;

	(void)tg_der_whole(data, len);
	if (tg_der_cert_public_key(data, len, &key, &key_len) == 0 && (key < data || key + key_len > data + len)) {
		__builtin_trap();
	}
	if (tg_der_private_key_public_key(data, len, &key, &key_len) == 0 && (key < data || key + key_len > data + len)) {
		__builtin_trap();
	}
	return 0;
}
