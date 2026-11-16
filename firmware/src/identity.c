/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reads a device's identity from its certificate: the common name of the subject. It walks the DER encoding to the subject
 * rather than searching for the name's object identifier, because the issuer, which comes first, has a common name too.
 */

#include <string.h>

#include "der.h"
#include "identity.h"

int tg_identity_from_cert(const uint8_t *der, size_t len, char *out, size_t cap)
{
	static const uint8_t common_name[] = {0x55, 0x04, 0x03};
	const uint8_t *at = der, *end = der + len;
	struct tg_der t;

	if (der == NULL || cap < 2 || tg_der_enter(&at, &end, TG_DER_SEQUENCE) != 0 || tg_der_enter(&at, &end, TG_DER_SEQUENCE) != 0) {
		return -1;
	}
	/* tbsCertificate: [0] version, which is optional, then serial, signature algorithm, issuer, validity, and subject. */
	if (end - at > 0 && *at == 0xa0 && tg_der_skip(&at, end) != 0) {
		return -1;
	}
	for (int i = 0; i < 4; i++) {
		if (tg_der_skip(&at, end) != 0) {
			return -1;
		}
	}
	if (tg_der_enter(&at, &end, TG_DER_SEQUENCE) != 0) {
		return -1;
	}
	/* The subject: a sequence of sets, each of sequences of an attribute's OID and value. */
	while (at < end) {
		const uint8_t *set_at = at, *set_end = end;

		if (tg_der_enter(&set_at, &set_end, TG_DER_SET) != 0 || tg_der_skip(&at, end) != 0) {
			return -1;
		}
		while (set_at < set_end) {
			const uint8_t *attr_at = set_at, *attr_end = set_end;
			struct tg_der oid;

			if (tg_der_enter(&attr_at, &attr_end, TG_DER_SEQUENCE) != 0 || tg_der_skip(&set_at, set_end) != 0 || tg_der_next(&attr_at, attr_end, &oid) != 0 ||
			    oid.tag != TG_DER_OID) {
				return -1;
			}
			if (oid.len != sizeof(common_name) || memcmp(oid.value, common_name, sizeof(common_name)) != 0) {
				continue;
			}
			if (tg_der_next(&attr_at, attr_end, &t) != 0 || t.len == 0 || t.len >= cap) {
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
