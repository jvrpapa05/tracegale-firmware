/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TG_IDENTITY_H_
#define TG_IDENTITY_H_

#include <stddef.h>
#include <stdint.h>

/** The longest identity Tracegale's server issues: "dev-" and 32 hexadecimal digits. */
#define TG_IDENTITY_MAX 36

/**
 * Reads the common name of a certificate's subject, which is a device's identity.
 *
 * @param der the certificate, in DER form.
 * @param out gets the name, ended by a NUL, if it's 1 to cap - 1 bytes of letters, digits, '-' and '_'.
 * @return 0, or -1 if the certificate can't be read or names nothing usable.
 */
int tg_identity_from_cert(const uint8_t *der, size_t len, char *out, size_t cap);

#endif /* TG_IDENTITY_H_ */
