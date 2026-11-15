/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fuzzes the firmware's reader of messages from the server (src/wire.c), with libFuzzer and AddressSanitizer and
 * UndefinedBehaviorSanitizer, on the host. Every reply a device acts on passes through here, and a server could be
 * compromised, so this must never read out of bounds, crash, or hand back a field outside the message. Run by
 * scripts/ci/fuzz-firmware.sh.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wire.h"

static void inside(const uint8_t *data, size_t len, const uint8_t *p, size_t n)
{
	if (n > 0 && (p < data || n > len || p > data + len - n)) {
		__builtin_trap();
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len)
{
	struct tg_message msg;

	if (tg_read_message(data, len, &msg) != 0) {
		return 0;
	}
	inside(data, len, msg.body, msg.body_len);
	/* Every key a field can have, and one beyond, read as each type, as the firmware may. */
	for (uint32_t key = 0; key <= 24; key++) {
		uint32_t number;
		const uint8_t *bytes;
		size_t bytes_len;
		bool flag;

		(void)tg_read_uint(&msg, key, &number);
		if (tg_read_bytes(&msg, key, &bytes, &bytes_len)) {
			inside(data, len, bytes, bytes_len);
		}
		(void)tg_read_bool(&msg, key, &flag);
	}
	return 0;
}
