/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * SHA-256 from FIPS 180-4, used only to check that uploads arrive intact. TLS protects them, so this isn't a security
 * function, and it has no dependency on a crypto library, whose interface differs between Zephyr versions.
 */

#ifndef TG_SHA256_H_
#define TG_SHA256_H_

#include <stddef.h>
#include <stdint.h>

#define TG_SHA256_SIZE 32

struct tg_sha256 {
	uint32_t state[8];
	uint64_t length;
	uint8_t block[64];
	size_t used;
};

void tg_sha256_init(struct tg_sha256 *ctx);
void tg_sha256_update(struct tg_sha256 *ctx, const uint8_t *data, size_t len);
void tg_sha256_final(struct tg_sha256 *ctx, uint8_t digest[TG_SHA256_SIZE]);

#endif /* TG_SHA256_H_ */
