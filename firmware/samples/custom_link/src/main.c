/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * A skeleton for a link Tracegale has no binding for, such as a cellular modem that runs TLS and MQTT itself on AT commands,
 * and that the application powers only now and then. It uses only <tracegale/transport.h>, the public contract. Fill in the
 * modem_*() functions with your driver; here they do nothing, so the sample builds and runs anywhere.
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <tracegale/tracegale.h>
#include <tracegale/transport.h>

LOG_MODULE_REGISTER(custom_link, LOG_LEVEL_INF);

/* Your modem driver. Each returns 0, or a code of your own, which Tracegale shows as the error's detail. */
static int modem_connect(const char *host, uint16_t port, const char *client_id, const uint8_t *ca, size_t ca_len,
			 const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len)
{
	LOG_INF("would connect to %s:%u as %s, with %u, %u and %u bytes of credentials", host, port, client_id,
		(unsigned int)ca_len, (unsigned int)cert_len, (unsigned int)key_len);
	ARG_UNUSED(ca);
	ARG_UNUSED(cert);
	ARG_UNUSED(key);
	return 1;
}

static int modem_publish_and_wait(const char *up, const char *down, const uint8_t *tx, size_t tx_len, uint8_t *rx,
				  size_t rx_cap, size_t *rx_len)
{
	ARG_UNUSED(up);
	ARG_UNUSED(down);
	ARG_UNUSED(tx);
	ARG_UNUSED(tx_len);
	ARG_UNUSED(rx);
	ARG_UNUSED(rx_cap);
	ARG_UNUSED(rx_len);
	return 1;
}

static void modem_disconnect(void)
{
}

static char identity[TG_IDENTITY_MAX + 1];

static int link_open(int *detail)
{
	size_t ca_len, cert_len, key_len;
	const uint8_t *ca = tg_store_credential(TRACEGALE_CREDENTIAL_CA, &ca_len);
	const uint8_t *cert = tg_store_credential(TRACEGALE_CREDENTIAL_DEVICE_CERT, &cert_len);
	const uint8_t *key = tg_store_credential(TRACEGALE_CREDENTIAL_DEVICE_KEY, &key_len);

	/* The MQTT binding's client ID and topics are the identity in the certificate. */
	if (cert == NULL || tg_identity_from_cert(cert, cert_len, identity, sizeof(identity)) != 0) {
		*detail = 0;
		return -TRACEGALE_ERR_IDENTITY;
	}
	*detail = modem_connect(tg_store_host(), tg_store_port(), identity, ca, ca_len, cert, cert_len, key, key_len);
	/* No close() follows a failed open(), so nothing may be left open here. */
	return *detail == 0 ? 0 : -TRACEGALE_ERR_CONNECT;
}

static int link_exchange(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_cap, size_t *rx_len, int *detail)
{
	char up[TG_IDENTITY_MAX + 16], down[TG_IDENTITY_MAX + 16];

	snprintf(up, sizeof(up), "tg/v1/%s/up", identity);
	snprintf(down, sizeof(down), "tg/v1/%s/down", identity);
	/* Binary: the modem must pass every byte value, so turn off any UTF-8 check it has. */
	*detail = modem_publish_and_wait(up, down, tx, tx_len, rx, rx_cap, rx_len);
	return *detail == 0 ? 0 : -TRACEGALE_ERR_SEND;
}

static void link_close(void)
{
	modem_disconnect();
}

const struct tg_transport *tg_transport_get(void)
{
	static const struct tg_transport transport = {.open = link_open, .exchange = link_exchange, .close = link_close};

	return &transport;
}

int main(void)
{
	/* The application's own schedule: here, once a minute. Tracegale attempts nothing while the link is down. */
	for (;;) {
		/* Power the modem on and wait for its network here. */
		int rc = tracegale_check_in(K_SECONDS(120));

		LOG_INF("check-in: %d", rc);
		/* Power the modem off here. */
		k_sleep(K_SECONDS(60));
	}
	return 0;
}
