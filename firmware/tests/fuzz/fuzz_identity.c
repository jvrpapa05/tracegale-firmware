/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fuzzes the firmware's reader of its own certificate's common name (src/identity.c), which the MQTT binding uses as the
 * device's identity. The certificate arrives at provisioning; this must never read out of bounds or write past its output,
 * whatever it's given.
 */

#include <stdint.h>
#include <string.h>

#include "identity.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len)
{
	char out[TG_IDENTITY_MAX + 1];

	memset(out, 'x', sizeof(out));
	if (tg_identity_from_cert(data, len, out, sizeof(out)) == 0 && memchr(out, '\0', sizeof(out)) == NULL) {
		__builtin_trap();
	}
	return 0;
}
