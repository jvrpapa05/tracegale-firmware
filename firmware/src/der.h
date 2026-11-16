/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Just enough of DER, the encoding of certificates and keys, to read a device's identity and to check credentials before
 * they're stored. It reads; it never decides whether a certificate is trusted, which TLS does.
 */

#ifndef TG_DER_H_
#define TG_DER_H_

#include <stddef.h>
#include <stdint.h>

#define TG_DER_SEQUENCE 0x30
#define TG_DER_SET      0x31
#define TG_DER_OID      0x06

struct tg_der {
	uint8_t tag;
	const uint8_t *value;
	size_t len;
};

/** Reads one element at *at, which must end by end, and moves *at past it. Lengths up to 64 KB. @return 0, or -1. */
int tg_der_next(const uint8_t **at, const uint8_t *end, struct tg_der *out);

/** Steps into an element of the given tag: *at and *end then bound its contents. @return 0, or -1. */
int tg_der_enter(const uint8_t **at, const uint8_t **end, uint8_t tag);

/** Moves *at past one element. @return 0, or -1. */
int tg_der_skip(const uint8_t **at, const uint8_t *end);

/**
 * Checks a credential is exactly one SEQUENCE, the outer form of a certificate and a key, with nothing missing or after it.
 * This catches a piece pasted twice or left out. @return 0, or -1.
 */
int tg_der_whole(const uint8_t *der, size_t len);

/** Finds the public key in a certificate: the contents of its subjectPublicKey, after the unused-bits byte. @return 0, or -1. */
int tg_der_cert_public_key(const uint8_t *der, size_t len, const uint8_t **key, size_t *key_len);

/**
 * Finds the public key in an EC private key, in PKCS#8 or SEC1 form, which may leave it out.
 * @return 0, 1 if the key is readable and has no public key, or -1 if it can't be read.
 */
int tg_der_private_key_public_key(const uint8_t *der, size_t len, const uint8_t **key, size_t *key_len);

#endif /* TG_DER_H_ */
