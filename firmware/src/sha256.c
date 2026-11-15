/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * SHA-256 from FIPS 180-4, section 6.2. Tested against the standard vectors in tests/wire.
 */

#include <string.h>

#include "sha256.h"

static const uint32_t k[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static inline uint32_t ror(uint32_t x, unsigned int n)
{
	return (x >> n) | (x << (32 - n));
}

static void compress(uint32_t state[8], const uint8_t block[64])
{
	uint32_t w[64];
	uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
	uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

	for (int t = 0; t < 16; t++) {
		w[t] = ((uint32_t)block[4 * t] << 24) | ((uint32_t)block[4 * t + 1] << 16) |
		       ((uint32_t)block[4 * t + 2] << 8) | block[4 * t + 3];
	}
	for (int t = 16; t < 64; t++) {
		uint32_t s0 = ror(w[t - 15], 7) ^ ror(w[t - 15], 18) ^ (w[t - 15] >> 3);
		uint32_t s1 = ror(w[t - 2], 17) ^ ror(w[t - 2], 19) ^ (w[t - 2] >> 10);

		w[t] = w[t - 16] + s0 + w[t - 7] + s1;
	}
	for (int t = 0; t < 64; t++) {
		uint32_t t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + k[t] + w[t];
		uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));

		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
	state[5] += f;
	state[6] += g;
	state[7] += h;
}

void tg_sha256_init(struct tg_sha256 *ctx)
{
	static const uint32_t initial[8] = {
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
	};

	memcpy(ctx->state, initial, sizeof(initial));
	ctx->length = 0;
	ctx->used = 0;
}

void tg_sha256_update(struct tg_sha256 *ctx, const uint8_t *data, size_t len)
{
	ctx->length += len;
	while (len > 0) {
		size_t take = sizeof(ctx->block) - ctx->used;

		if (take > len) {
			take = len;
		}
		memcpy(&ctx->block[ctx->used], data, take);
		ctx->used += take;
		data += take;
		len -= take;
		if (ctx->used == sizeof(ctx->block)) {
			compress(ctx->state, ctx->block);
			ctx->used = 0;
		}
	}
}

void tg_sha256_final(struct tg_sha256 *ctx, uint8_t digest[TG_SHA256_SIZE])
{
	uint64_t bits = ctx->length * 8;

	ctx->block[ctx->used++] = 0x80;
	if (ctx->used > 56) {
		memset(&ctx->block[ctx->used], 0, sizeof(ctx->block) - ctx->used);
		compress(ctx->state, ctx->block);
		ctx->used = 0;
	}
	memset(&ctx->block[ctx->used], 0, 56 - ctx->used);
	for (int i = 0; i < 8; i++) {
		ctx->block[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
	}
	compress(ctx->state, ctx->block);
	for (int i = 0; i < 8; i++) {
		digest[4 * i] = (uint8_t)(ctx->state[i] >> 24);
		digest[4 * i + 1] = (uint8_t)(ctx->state[i] >> 16);
		digest[4 * i + 2] = (uint8_t)(ctx->state[i] >> 8);
		digest[4 * i + 3] = (uint8_t)ctx->state[i];
	}
	memset(ctx, 0, sizeof(*ctx));
}
