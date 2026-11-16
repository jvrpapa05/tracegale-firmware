/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#include "der.h"

int tg_der_next(const uint8_t **at, const uint8_t *end, struct tg_der *out)
{
	const uint8_t *p = *at;
	size_t len;

	if (p == NULL || end - p < 2) {
		return -1;
	}
	out->tag = *p++;
	len = *p++;
	if (len & 0x80) {
		size_t bytes = len & 0x7f;

		/* Lengths up to 64 KB, which is far more than any certificate a device keeps. */
		if (bytes == 0 || bytes > 2 || (size_t)(end - p) < bytes) {
			return -1;
		}
		len = 0;
		while (bytes-- > 0) {
			len = (len << 8) | *p++;
		}
	}
	if ((size_t)(end - p) < len) {
		return -1;
	}
	out->value = p;
	out->len = len;
	*at = p + len;
	return 0;
}

int tg_der_enter(const uint8_t **at, const uint8_t **end, uint8_t tag)
{
	struct tg_der t;

	if (tg_der_next(at, *end, &t) != 0 || t.tag != tag) {
		return -1;
	}
	*at = t.value;
	*end = t.value + t.len;
	return 0;
}

int tg_der_skip(const uint8_t **at, const uint8_t *end)
{
	struct tg_der t;

	return tg_der_next(at, end, &t);
}

int tg_der_whole(const uint8_t *der, size_t len)
{
	const uint8_t *at = der;
	struct tg_der t;

	if (der == NULL || tg_der_next(&at, der + len, &t) != 0 || t.tag != TG_DER_SEQUENCE || at != der + len) {
		return -1;
	}
	return 0;
}

#define INTEGER      0x02
#define BIT_STRING   0x03
#define OCTET_STRING 0x04

/* A BIT STRING's contents start with the number of unused bits, which is 0 for a key. */
static int bit_string_key(const struct tg_der *t, const uint8_t **key, size_t *key_len)
{
	if (t->tag != BIT_STRING || t->len < 2 || t->value[0] != 0) {
		return -1;
	}
	*key = t->value + 1;
	*key_len = t->len - 1;
	return 0;
}

int tg_der_cert_public_key(const uint8_t *der, size_t len, const uint8_t **key, size_t *key_len)
{
	const uint8_t *at = der, *end = der + len;
	struct tg_der t;

	if (der == NULL || tg_der_enter(&at, &end, TG_DER_SEQUENCE) != 0 || tg_der_enter(&at, &end, TG_DER_SEQUENCE) != 0) {
		return -1;
	}
	/* tbsCertificate: [0] version, which is optional, then serial, signature algorithm, issuer, validity, subject, and the key. */
	if (end - at > 0 && *at == 0xa0 && tg_der_skip(&at, end) != 0) {
		return -1;
	}
	for (int i = 0; i < 5; i++) {
		if (tg_der_skip(&at, end) != 0) {
			return -1;
		}
	}
	/* SubjectPublicKeyInfo: the algorithm, then the key. */
	if (tg_der_enter(&at, &end, TG_DER_SEQUENCE) != 0 || tg_der_skip(&at, end) != 0 || tg_der_next(&at, end, &t) != 0) {
		return -1;
	}
	return bit_string_key(&t, key, key_len);
}

/* SEC1's ECPrivateKey: version 1, the private key, then [0] parameters and [1] the public key, both optional. */
static int sec1_public_key(const uint8_t *at, const uint8_t *end, const uint8_t **key, size_t *key_len)
{
	struct tg_der t;

	if (tg_der_enter(&at, &end, TG_DER_SEQUENCE) != 0 || tg_der_next(&at, end, &t) != 0 || t.tag != INTEGER ||
	    tg_der_next(&at, end, &t) != 0 || t.tag != OCTET_STRING) {
		return -1;
	}
	while (at < end) {
		if (tg_der_next(&at, end, &t) != 0) {
			return -1;
		}
		if (t.tag == 0xa1) {
			const uint8_t *in = t.value;
			struct tg_der bits;

			if (tg_der_next(&in, t.value + t.len, &bits) != 0) {
				return -1;
			}
			return bit_string_key(&bits, key, key_len);
		}
	}
	return 1;
}

int tg_der_private_key_public_key(const uint8_t *der, size_t len, const uint8_t **key, size_t *key_len)
{
	const uint8_t *at = der, *end = der + len;
	struct tg_der version, next;

	if (der == NULL || tg_der_whole(der, len) != 0) {
		return -1;
	}
	if (tg_der_enter(&at, &end, TG_DER_SEQUENCE) != 0 || tg_der_next(&at, end, &version) != 0 || version.tag != INTEGER ||
	    tg_der_next(&at, end, &next) != 0) {
		return -1;
	}
	if (next.tag == OCTET_STRING) {
		/* SEC1, whose second element is the private key itself. */
		return sec1_public_key(der, der + len, key, key_len);
	}
	/* PKCS#8: the algorithm, then the SEC1 key inside an OCTET STRING. */
	if (next.tag != TG_DER_SEQUENCE || tg_der_next(&at, end, &next) != 0 || next.tag != OCTET_STRING) {
		return -1;
	}
	return sec1_public_key(next.value, next.value + next.len, key, key_len);
}
