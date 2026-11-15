/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fuzzes the firmware's MQTT client (src/transport_mqtt.c), built on the host with stand-ins for Zephyr's socket calls. The
 * fuzzer's input is everything the broker sends: replies to connecting and subscribing, then any packets, split into reads
 * of varying sizes. The client connects, makes two exchanges and closes, and must never crash, read out of bounds, or hand
 * back a reply larger than its buffer. A broker could be compromised, and every device reads what it sends.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>

#include "identity.h"
#include "store.h"
#include "tls_socket.h"
#include "transport.h"

static const uint8_t *input;
static size_t input_left;

int zsock_poll(struct zsock_pollfd *fds, int nfds, int timeout)
{
	(void)fds;
	(void)nfds;
	(void)timeout;
	return 1;
}

ssize_t zsock_recv(int sock, void *buf, size_t len, int flags)
{
	(void)sock;
	(void)flags;
	if (input_left == 0) {
		return 0;
	}
	/* Each read takes a varying part of what's wanted, as TCP delivers it, steered by the input itself. */
	size_t n = MIN(len, input_left);
	size_t cut = 1 + (input[0] % 64);

	n = MIN(n, cut);
	memcpy(buf, input, n);
	input += n;
	input_left -= n;
	return (ssize_t)n;
}

ssize_t zsock_send(int sock, const void *buf, size_t len, int flags)
{
	(void)sock;
	(void)buf;
	(void)flags;
	return (ssize_t)len;
}

int zsock_close(int sock)
{
	(void)sock;
	return 0;
}

/* What the binding takes from the rest of the firmware. */
const uint8_t *tg_store_credential(enum tracegale_credential type, size_t *len)
{
	static const uint8_t cert[] = {0x30, 0x00};

	(void)type;
	*len = sizeof(cert);
	return cert;
}

int tg_identity_from_cert(const uint8_t *der, size_t len, char *out, size_t cap)
{
	(void)der;
	(void)len;
	return snprintf(out, cap, "dev-0123456789abcdef") < (int)cap ? 0 : -EINVAL;
}

int tg_tls_open(int *out, int *detail)
{
	(void)detail;
	*out = 3;
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len)
{
	const struct tg_transport *mqtt = tg_transport_get();
	static const uint8_t hello[] = {0x84, 0x01, 0x01, 0x01, 0xa0};
	uint8_t rx[256];
	size_t rx_len;
	int detail = 0;

	input = data;
	input_left = len;
	if (mqtt->open(&detail) == 0) {
		for (int i = 0; i < 2; i++) {
			rx_len = SIZE_MAX;
			if (mqtt->exchange(hello, sizeof(hello), rx, sizeof(rx), &rx_len, &detail) == 0 && rx_len > sizeof(rx)) {
				__builtin_trap();
			}
		}
		mqtt->close();
	}
	return 0;
}
