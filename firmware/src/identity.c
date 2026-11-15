/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reads a device's identity from its certificate: the common name of the subject. It walks the DER encoding to the subject
 * rather than searching for the name's object identifier, because the issuer, which comes first, has a common name too.
 */

#include <string.h>

#include "identity.h"

struct tlv {
	uint8_t tag;
	const uint8_t *value;
	size_t len;
};

/* Reads one TLV at *at, which must end by end, and moves *at past it. @return 0, or -1. */
static int next(const uint8_t **at, const uint8_t *end, struct tlv *out)
{
	const uint8_t *p = *at;
	size_t len;

	if (end - p < 2) {
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

/* Steps into a constructed TLV of the given tag. */
static int enter(const uint8_t **at, const uint8_t **end, uint8_t tag)
{
	struct tlv t;

	if (next(at, *end, &t) != 0 || t.tag != tag) {
		return -1;
	}
	*at = t.value;
	*end = t.value + t.len;
	return 0;
}

static int skip(const uint8_t **at, const uint8_t *end)
{
	struct tlv t;

	return next(at, end, &t);
}

#define SEQUENCE 0x30
#define SET      0x31
#define OID      0x06

int tg_identity_from_cert(const uint8_t *der, size_t len, char *out, size_t cap)
{
	static const uint8_t common_name[] = {0x55, 0x04, 0x03};
	const uint8_t *at = der, *end = der + len;
	struct tlv t;

	if (der == NULL || cap < 2 || enter(&at, &end, SEQUENCE) != 0 || enter(&at, &end, SEQUENCE) != 0) {
		return -1;
	}
	/* tbsCertificate: [0] version, which is optional, then serial, signature algorithm, issuer, validity, and subject. */
	if (end - at > 0 && *at == 0xa0 && skip(&at, end) != 0) {
		return -1;
	}
	for (int i = 0; i < 4; i++) {
		if (skip(&at, end) != 0) {
			return -1;
		}
	}
	if (enter(&at, &end, SEQUENCE) != 0) {
		return -1;
	}
	/* The subject: a sequence of sets, each of sequences of an attribute's OID and value. */
	while (at < end) {
		const uint8_t *set_at = at, *set_end = end;

		if (enter(&set_at, &set_end, SET) != 0 || skip(&at, end) != 0) {
			return -1;
		}
		while (set_at < set_end) {
			const uint8_t *attr_at = set_at, *attr_end = set_end;
			struct tlv oid;

			if (enter(&attr_at, &attr_end, SEQUENCE) != 0 || skip(&set_at, set_end) != 0 || next(&attr_at, attr_end, &oid) != 0 ||
			    oid.tag != OID) {
				return -1;
			}
			if (oid.len != sizeof(common_name) || memcmp(oid.value, common_name, sizeof(common_name)) != 0) {
				continue;
			}
			if (next(&attr_at, attr_end, &t) != 0 || t.len == 0 || t.len >= cap) {
				return -1;
			}
			for (size_t j = 0; j < t.len; j++) {
				char c = (char)t.value[j];

				if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) {
					return -1;
				}
				out[j] = c;
			}
			out[t.len] = '\0';
			return 0;
		}
	}
	return -1;
}
