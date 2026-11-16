/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief For a connection Tracegale doesn't provide: CONFIG_TRACEGALE_TRANSPORT_CUSTOM.
 *
 * Use it for a link Tracegale has no binding for, such as a cellular modem that runs TLS and MQTT itself on AT commands. The
 * application provides tg_transport_get(). This header is the whole contract: it stays stable between releases, and nothing
 * else in Tracegale's source is needed.
 *
 * Credentials. Provisioning and enrollment store the server's CA, the device certificate and its private key, in DER form,
 * and open() reads them with tg_store_credential(). A link that keeps credentials of its own, such as a modem's file system,
 * writes them there in open(), when they differ from what it holds. Tracegale's credential backends aren't used with a custom
 * transport. The private key then stays in Tracegale's settings storage, in plain flash unless the application encrypts it,
 * as with Zephyr's own TLS.
 *
 * Lifecycle. All calls come from one thread at a time: Tracegale's own, or the caller of tracegale_check_in() without
 * CONFIG_TRACEGALE_AUTOSTART. Each attempt is:
 *
 *     open() -> exchange() ... exchange() -> close()
 *
 * - close() follows every open() that returned 0, even when an exchange() failed. It is NOT called after a failed open(),
 *   so open() cleans up after itself when it fails.
 * - exchange() sends one Tracegale message and returns the next reply. The core checks the reply's type and message ID, so a
 *   transport passes on whatever came back, and needn't read the messages. The bytes are binary CBOR: the link must carry
 *   every byte value. An MQTT stack that insists on UTF-8 payloads must have that check turned off.
 * - Each call returns 0 or a negative tracegale_error. *detail is free for the transport's own code, such as an errno, a
 *   modem's error code or the step that failed. It's shown in tracegale_status.last_error_detail and the shell, and
 *   nowhere else, so make it useful to whoever reads the device's log.
 * - Errors that matter to the core: TRACEGALE_ERR_TIMEOUT and TRACEGALE_ERR_CONNECT, TRACEGALE_ERR_DNS, TRACEGALE_ERR_TLS
 *   and TRACEGALE_ERR_REFUSED all just fail the attempt, and the core backs off. TRACEGALE_ERR_TOO_LARGE means the reply
 *   didn't fit rx_cap. None of them are retried within the attempt.
 *
 * The server must always be verified: its certificate chain and signature, against the CA from tg_store_credential(), and
 * the host name. Checking expiry needs the time, which a device without a clock doesn't have before it connects. Tracegale's
 * own TLS on Zephyr checks expiry only if the application turns on CONFIG_MBEDTLS_HAVE_TIME_DATE and keeps the clock set.
 * The server's CA is private to one server, so an expired certificate isn't one a stranger could have obtained. A modem that
 * refuses to connect without a clock can be told the time, or have its expiry check turned off; the chain, signature and
 * name checks must stay on.
 *
 * If the link is usually off, as with a modem powered only for a moment, see tracegale_link_set() and tracegale_check_in()
 * in tracegale.h.
 */

#ifndef TRACEGALE_TRANSPORT_H_
#define TRACEGALE_TRANSPORT_H_

#include <stddef.h>
#include <stdint.h>

#include <tracegale/tracegale.h>

#ifdef __cplusplus
extern "C" {
#endif

struct tg_transport {
	/** Connects to the provisioned server: tg_store_host() and tg_store_port(). */
	int (*open)(int *detail);
	/** Sends one message of tx_len bytes, and waits for the reply, which goes in rx, up to rx_cap bytes. */
	int (*exchange)(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_cap, size_t *rx_len, int *detail);
	/** Disconnects, after an open() that worked. */
	void (*close)(void);
};

/** Provided by the application with CONFIG_TRACEGALE_TRANSPORT_CUSTOM. */
const struct tg_transport *tg_transport_get(void);

/**
 * @brief A credential in use, in DER form: the server's CA, the device certificate, or the device's private key.
 *
 * Valid only during an attempt, from open() to close(). The key is NULL with *len 0 where a credential backend has taken it,
 * see tg_cred_forget_key().
 */
const uint8_t *tg_store_credential(enum tracegale_credential type, size_t *len);

/** The server's host name, valid during an attempt. Check it against the server's certificate. */
const char *tg_store_host(void);

/** The server's port, valid during an attempt. */
uint16_t tg_store_port(void);

/** The longest identity Tracegale's server issues: "enroll-" and 32 hexadecimal digits. */
#define TG_IDENTITY_MAX 39

/**
 * @brief A device's identity: the common name of its certificate's subject. Over MQTT, its topics are
 * tg/v1/<identity>/up and tg/v1/<identity>/down, and its client ID is the identity.
 *
 * @param der the certificate, from tg_store_credential(TRACEGALE_CREDENTIAL_DEVICE_CERT).
 * @param out gets the name, ended by a NUL, if it's 1 to cap - 1 letters, digits, '-' and '_'.
 * @return 0, or -1 if the certificate can't be read or names nothing usable.
 */
int tg_identity_from_cert(const uint8_t *der, size_t len, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* TRACEGALE_TRANSPORT_H_ */
