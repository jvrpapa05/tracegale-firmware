/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * The MQTT binding from docs/protocol/README.md: MQTT 3.1.1 over TLS, publishing each message to tg/v1/<identity>/up and
 * reading the reply from tg/v1/<identity>/down, at QoS 1. Nothing else in Tracegale depends on this file.
 *
 * Only what the binding needs is here: connect, subscribe, publish and acknowledge. A check-in connects, sends its messages one
 * at a time, and disconnects, as over HTTPS. The payload is read straight into the caller's buffer, so the binding needs no
 * buffer the size of a message.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include <tracegale/tracegale.h>

#include "identity.h"
#include "store.h"
#include "tls_socket.h"
#include "transport.h"

LOG_MODULE_DECLARE(tracegale, CONFIG_TRACEGALE_LOG_LEVEL);

/* Packet types, in the high four bits of the first byte. */
#define CONNECT     0x10
#define CONNACK     0x20
#define PUBLISH     0x30
#define PUBACK      0x40
#define SUBSCRIBE   0x82
#define SUBACK      0x90
#define DISCONNECT  0xe0

/* Long enough for a check-in, whose messages keep the connection alive anyway, and short enough that the broker soon
 * forgets a device that vanished.
 */
#define KEEPALIVE_S 120

/* "tg/v1/" + the identity + "/down". */
#define TOPIC_MAX (6 + TG_IDENTITY_MAX + 5)

static int sock = -1;
static char identity[TG_IDENTITY_MAX + 1];
static char up[TOPIC_MAX + 1];
static char down[TOPIC_MAX + 1];
static uint16_t packet_id;

static int send_all(const uint8_t *data, size_t len)
{
	while (len > 0) {
		ssize_t sent = zsock_send(sock, data, len, 0);

		if (sent < 0) {
			return -errno;
		}
		data += sent;
		len -= sent;
	}
	return 0;
}

/* Reads exactly len bytes before the deadline, into data, or throws them away when data is NULL. */
static int recv_exact(uint8_t *data, size_t len, k_timepoint_t deadline)
{
	uint8_t sink[32];

	while (len > 0) {
		struct zsock_pollfd fd = {.fd = sock, .events = ZSOCK_POLLIN};
		int64_t left = k_ticks_to_ms_ceil64(sys_timepoint_timeout(deadline).ticks);
		size_t want = data != NULL ? len : MIN(len, sizeof(sink));
		ssize_t got;

		if (sys_timepoint_expired(deadline)) {
			return -ETIMEDOUT;
		}
		if (zsock_poll(&fd, 1, (int)MIN(left, INT32_MAX)) <= 0) {
			return -ETIMEDOUT;
		}
		got = zsock_recv(sock, data != NULL ? data : sink, want, 0);
		if (got <= 0) {
			return got == 0 ? -ECONNRESET : -errno;
		}
		if (data != NULL) {
			data += got;
		}
		len -= got;
	}
	return 0;
}

/* Reads a packet's fixed header: its first byte, and the length of the rest. */
static int recv_header(uint8_t *first, uint32_t *remaining, k_timepoint_t deadline)
{
	uint8_t b;
	int rc = recv_exact(first, 1, deadline);

	*remaining = 0;
	for (int i = 0; rc == 0 && i < 4; i++) {
		rc = recv_exact(&b, 1, deadline);
		*remaining |= (uint32_t)(b & 0x7f) << (7 * i);
		if (rc == 0 && (b & 0x80) == 0) {
			return 0;
		}
	}
	return rc != 0 ? rc : -EBADMSG;
}

/* Writes a fixed header. @return its length. */
static size_t put_header(uint8_t *out, uint8_t first, size_t remaining)
{
	size_t n = 0;

	out[n++] = first;
	do {
		uint8_t b = remaining & 0x7f;

		remaining >>= 7;
		out[n++] = b | (remaining > 0 ? 0x80 : 0);
	} while (remaining > 0);
	return n;
}

static size_t put_string(uint8_t *out, const char *s, size_t len)
{
	out[0] = len >> 8;
	out[1] = len & 0xff;
	memcpy(&out[2], s, len);
	return 2 + len;
}

static int as_error(int rc, int *detail)
{
	*detail = -rc;
	return rc == -ETIMEDOUT ? -TRACEGALE_ERR_TIMEOUT : -TRACEGALE_ERR_SEND;
}

static void drop(void)
{
	zsock_close(sock);
	sock = -1;
}

static int mqtt_open(int *detail)
{
	uint8_t packet[16 + TOPIC_MAX + TG_IDENTITY_MAX];
	k_timepoint_t deadline = sys_timepoint_calc(K_MSEC(CONFIG_TRACEGALE_TIMEOUT_MS));
	size_t cert_len, id_len, n, var;
	const uint8_t *cert = tg_store_credential(TRACEGALE_CREDENTIAL_DEVICE_CERT, &cert_len);
	uint8_t first, ack[2];
	uint32_t remaining;
	int rc;

	/* The broker takes the identity from the certificate too, and allows this device only its own topics. */
	if (tg_identity_from_cert(cert, cert_len, identity, sizeof(identity)) != 0) {
		LOG_ERR("The device certificate names no identity");
		return -TRACEGALE_ERR_IDENTITY;
	}
	id_len = strlen(identity);
	snprintk(up, sizeof(up), "tg/v1/%s/up", identity);
	snprintk(down, sizeof(down), "tg/v1/%s/down", identity);
	rc = tg_tls_open(&sock, detail);
	if (rc != 0) {
		return rc;
	}

	/* CONNECT: MQTT 3.1.1, a clean session, and the identity as the client ID. */
	var = 10 + 2 + id_len;
	n = put_header(packet, CONNECT, var);
	n += put_string(&packet[n], "MQTT", 4);
	packet[n++] = 4;
	packet[n++] = 0x02;
	packet[n++] = KEEPALIVE_S >> 8;
	packet[n++] = KEEPALIVE_S & 0xff;
	n += put_string(&packet[n], identity, id_len);
	rc = send_all(packet, n);
	if (rc == 0) {
		rc = recv_header(&first, &remaining, deadline);
	}
	if (rc == 0 && (first != CONNACK || remaining != 2)) {
		rc = -EBADMSG;
	}
	if (rc == 0) {
		rc = recv_exact(ack, 2, deadline);
	}
	if (rc != 0) {
		drop();
		return rc == -EBADMSG ? -TRACEGALE_ERR_BAD_REPLY : as_error(rc, detail);
	}
	if (ack[1] != 0) {
		*detail = ack[1];
		LOG_WRN("The broker refused the connection: %d", ack[1]);
		drop();
		return -TRACEGALE_ERR_REFUSED;
	}

	/* SUBSCRIBE to the replies, at QoS 1. */
	n = put_header(packet, SUBSCRIBE, 2 + 2 + strlen(down) + 1);
	packet[n++] = 0;
	packet[n++] = 1;
	n += put_string(&packet[n], down, strlen(down));
	packet[n++] = 1;
	rc = send_all(packet, n);
	if (rc == 0) {
		rc = recv_header(&first, &remaining, deadline);
	}
	if (rc == 0 && (first != SUBACK || remaining != 3)) {
		rc = -EBADMSG;
	}
	if (rc == 0) {
		uint8_t sub[3];

		rc = recv_exact(sub, 3, deadline);
		if (rc == 0 && sub[2] == 0x80) {
			*detail = 0x80;
			drop();
			return -TRACEGALE_ERR_REFUSED;
		}
	}
	if (rc != 0) {
		drop();
		return rc == -EBADMSG ? -TRACEGALE_ERR_BAD_REPLY : as_error(rc, detail);
	}
	packet_id = 1;
	return 0;
}

/* Reads one incoming PUBLISH whose first byte has been read. @return 1 for a reply in rx, 0 for anything else, or an error. */
static int recv_publish(uint8_t first, uint32_t remaining, uint8_t *rx, size_t rx_cap, size_t *rx_len, k_timepoint_t deadline)
{
	uint8_t len[2], topic[TOPIC_MAX], id[2];
	uint8_t qos = (first >> 1) & 0x03;
	size_t topic_len, payload;
	int rc = recv_exact(len, 2, deadline);

	if (rc != 0) {
		return rc;
	}
	topic_len = (len[0] << 8) | len[1];
	if (topic_len + 2 + (qos ? 2 : 0) > remaining) {
		return -EBADMSG;
	}
	payload = remaining - 2 - topic_len - (qos ? 2 : 0);
	if (topic_len > sizeof(topic)) {
		rc = recv_exact(NULL, topic_len, deadline);
		topic_len = 0;
	} else {
		rc = recv_exact(topic, topic_len, deadline);
	}
	if (rc == 0 && qos > 0) {
		rc = recv_exact(id, 2, deadline);
	}
	if (rc != 0) {
		return rc;
	}
	bool mine = topic_len == strlen(down) && memcmp(topic, down, topic_len) == 0;

	if (mine && payload <= rx_cap) {
		rc = recv_exact(rx, payload, deadline);
		*rx_len = payload;
	} else {
		rc = recv_exact(NULL, payload, deadline);
	}
	if (rc == 0 && qos == 1) {
		uint8_t puback[4] = {PUBACK, 2, id[0], id[1]};

		rc = send_all(puback, sizeof(puback));
	}
	if (rc != 0) {
		return rc;
	}
	if (mine && payload > rx_cap) {
		return -EMSGSIZE;
	}
	return mine ? 1 : 0;
}

static int mqtt_exchange(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_cap, size_t *rx_len, int *detail)
{
	uint8_t head[8 + TOPIC_MAX];
	k_timepoint_t deadline = sys_timepoint_calc(K_MSEC(CONFIG_TRACEGALE_TIMEOUT_MS));
	size_t n, topic_len = strlen(up);
	int rc;

	if (sock < 0) {
		return -TRACEGALE_ERR_SEND;
	}
	packet_id = packet_id == UINT16_MAX ? 1 : packet_id + 1;
	n = put_header(head, PUBLISH | (1 << 1), 2 + topic_len + 2 + tx_len);
	n += put_string(&head[n], up, topic_len);
	head[n++] = packet_id >> 8;
	head[n++] = packet_id & 0xff;
	rc = send_all(head, n);
	if (rc == 0) {
		rc = send_all(tx, tx_len);
	}
	if (rc != 0) {
		return as_error(rc, detail);
	}
	/* The reply comes on the down topic. The broker's PUBACK for this message may come before or after it, and is passed
	 * over, since a reply proves the server has the message.
	 */
	for (;;) {
		uint8_t first;
		uint32_t remaining;

		rc = recv_header(&first, &remaining, deadline);
		if (rc != 0) {
			break;
		}
		if ((first & 0xf0) == PUBLISH) {
			rc = recv_publish(first, remaining, rx, rx_cap, rx_len, deadline);
			if (rc == 1) {
				return 0;
			}
			if (rc != 0) {
				break;
			}
		} else {
			rc = recv_exact(NULL, remaining, deadline);
			if (rc != 0) {
				break;
			}
		}
	}
	if (rc == -EMSGSIZE) {
		return -TRACEGALE_ERR_TOO_LARGE;
	}
	if (rc == -EBADMSG) {
		return -TRACEGALE_ERR_BAD_REPLY;
	}
	return as_error(rc, detail);
}

static void mqtt_close(void)
{
	if (sock >= 0) {
		static const uint8_t bye[] = {DISCONNECT, 0};

		(void)send_all(bye, sizeof(bye));
		drop();
	}
}

const struct tg_transport *tg_transport_get(void)
{
	static const struct tg_transport mqtt = {.open = mqtt_open, .exchange = mqtt_exchange, .close = mqtt_close};

	return &mqtt;
}
