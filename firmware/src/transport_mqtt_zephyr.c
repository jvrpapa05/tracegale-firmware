/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * The MQTT binding from docs/protocol/README.md, on Zephyr's own MQTT library, for applications that already use it to talk
 * to their own broker: they then carry one MQTT client, not two. transport_mqtt.c is the same binding with Tracegale's own
 * client, for everyone else. Both behave the same: MQTT 3.1.1 over TLS, each message published to tg/v1/<identity>/up at
 * QoS 1 and its reply read from tg/v1/<identity>/down, one connection per check-in.
 *
 * Zephyr's library sends a message's payload from the caller's buffer and reads a reply's payload into it, so its own
 * buffers hold only packet headers.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>

#include <tracegale/tracegale.h>

#include "compat.h"
#include "identity.h"
#include "store.h"
#include "transport.h"

LOG_MODULE_DECLARE(tracegale, CONFIG_TRACEGALE_LOG_LEVEL);

BUILD_ASSERT(IS_ENABLED(CONFIG_MQTT_LIB_TLS), "Tracegale's MQTT binding needs CONFIG_MQTT_LIB_TLS");

/* As in transport_mqtt.c. */
#define KEEPALIVE_S 120

/* "tg/v1/" + the identity + "/down". */
#define TOPIC_MAX (6 + TG_IDENTITY_MAX + 5)

/* The longest packet header either way, at MQTT 3.1.1, which has no properties: a SUBSCRIBE or PUBLISH with a topic (fixed
 * header 5, IDs and lengths 5, topic), or a CONNECT with the identity (fixed header 5, variable header 10, length 2, identity).
 */
#define HEADER_BUF (10 + MAX(TOPIC_MAX, 7 + TG_IDENTITY_MAX))

static struct mqtt_client client;
static uint8_t rx_buf[HEADER_BUF];
static uint8_t tx_buf[HEADER_BUF];
static struct tg_sockaddr_storage broker;
static char identity[TG_IDENTITY_MAX + 1];
static char up[TOPIC_MAX + 1];
static char down[TOPIC_MAX + 1];
static uint16_t packet_id;
static bool connected;

/* What the event handler saw, for the step that's waiting on it. */
static struct {
	bool connack, suback, reply, gone;
	int code;
	uint8_t *rx;
	size_t rx_cap;
	size_t *rx_len;
} seen;

/* Reads and throws away len bytes of a publication's payload. */
static int discard(struct mqtt_client *c, size_t len)
{
	uint8_t sink[32];

	while (len > 0) {
		int got = mqtt_read_publish_payload_blocking(c, sink, MIN(len, sizeof(sink)));

		if (got <= 0) {
			return got == 0 ? -ECONNRESET : got;
		}
		len -= got;
	}
	return 0;
}

static void on_publish(struct mqtt_client *c, const struct mqtt_publish_param *p)
{
	const struct mqtt_utf8 *topic = &p->message.topic.topic;
	size_t len = p->message.payload.len;
	bool mine = topic->size == strlen(down) && memcmp(topic->utf8, down, topic->size) == 0;
	int rc;

	if (mine && seen.rx != NULL && len <= seen.rx_cap) {
		rc = mqtt_readall_publish_payload(c, seen.rx, len);
		*seen.rx_len = len;
	} else {
		rc = discard(c, len);
	}
	if (rc == 0 && p->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
		struct mqtt_puback_param ack = {.message_id = p->message_id};

		rc = mqtt_publish_qos1_ack(c, &ack);
	}
	if (rc != 0) {
		seen.code = rc;
		seen.gone = true;
	} else if (mine && seen.rx != NULL) {
		seen.code = len <= seen.rx_cap ? 0 : -EMSGSIZE;
		seen.reply = true;
	}
}

static void on_event(struct mqtt_client *c, const struct mqtt_evt *evt)
{
	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		seen.code = evt->param.connack.return_code;
		seen.connack = true;
		break;
	case MQTT_EVT_SUBACK:
		seen.code = evt->param.suback.return_codes.len > 0 ? evt->param.suback.return_codes.data[0] : 0x80;
		seen.suback = true;
		break;
	case MQTT_EVT_PUBLISH:
		on_publish(c, &evt->param.publish);
		break;
	case MQTT_EVT_DISCONNECT:
		connected = false;
		seen.gone = true;
		break;
	default:
		/* The broker's PUBACK for a message is passed over, since the reply proves the server has it. */
		break;
	}
}

/* Reads packets until the handler sets *flag, the connection drops, or the deadline passes. @return 0 or a negative errno. */
static int wait_for(const bool *flag, k_timepoint_t deadline)
{
	while (!*flag) {
		struct zsock_pollfd fd = {.fd = client.transport.tls.sock, .events = ZSOCK_POLLIN};
		int64_t left = k_ticks_to_ms_ceil64(sys_timepoint_timeout(deadline).ticks);
		int rc;

		if (seen.gone) {
			return seen.code != 0 ? seen.code : -ECONNRESET;
		}
		if (sys_timepoint_expired(deadline) || zsock_poll(&fd, 1, (int)MIN(left, INT32_MAX)) <= 0) {
			return -ETIMEDOUT;
		}
		rc = mqtt_input(&client);
		if (rc != 0 && !*flag) {
			return rc;
		}
	}
	return 0;
}

static int as_error(int rc, int *detail)
{
	*detail = -rc;
	return rc == -ETIMEDOUT ? -TRACEGALE_ERR_TIMEOUT : -TRACEGALE_ERR_SEND;
}

static void drop(void)
{
	(void)mqtt_abort(&client);
	connected = false;
}

/* Starts a TLS connection to one address, and sends CONNECT. @return 0, or a negative tracegale_error. */
static int connect_to(const struct zsock_addrinfo *addr, const char *host, int *detail)
{
	static const sec_tag_t tags[] = {CONFIG_TRACEGALE_SEC_TAG};
	struct mqtt_sec_config *tls = &client.transport.tls.config;
	int rc;

	mqtt_client_init(&client);
	memcpy(&broker, addr->ai_addr, MIN(addr->ai_addrlen, sizeof(broker)));
	client.broker = &broker;
	client.evt_cb = on_event;
	client.client_id.utf8 = (const uint8_t *)identity;
	client.client_id.size = strlen(identity);
	client.protocol_version = MQTT_VERSION_3_1_1;
	client.clean_session = 1;
	client.keepalive = KEEPALIVE_S;
	client.rx_buf = rx_buf;
	client.rx_buf_size = sizeof(rx_buf);
	client.tx_buf = tx_buf;
	client.tx_buf_size = sizeof(tx_buf);
	client.transport.type = MQTT_TRANSPORT_SECURE;
	/* The server's certificate and name are always verified. There is no way to turn this off. */
	tls->peer_verify = TG_TLS_PEER_VERIFY_REQUIRED;
	tls->sec_tag_list = tags;
	tls->sec_tag_count = ARRAY_SIZE(tags);
	tls->hostname = host;

	rc = mqtt_connect(&client);
	if (rc == 0) {
		return 0;
	}
	*detail = -rc;
	LOG_WRN("Can't connect to %s: %d", host, *detail);
	/* As in tls_socket.c: Zephyr's TLS layer reports a failed handshake as ECONNABORTED. */
	return rc == -ECONNABORTED ? -TRACEGALE_ERR_TLS : -TRACEGALE_ERR_CONNECT;
}

static int mqtt_open(int *detail)
{
	struct zsock_addrinfo hints = {.ai_family = TG_AF_UNSPEC, .ai_socktype = TG_SOCK_STREAM};
	struct zsock_addrinfo *found = NULL;
	k_timepoint_t deadline = sys_timepoint_calc(K_MSEC(CONFIG_TRACEGALE_TIMEOUT_MS));
	const char *host = tg_store_host();
	size_t cert_len;
	const uint8_t *cert = tg_store_credential(TRACEGALE_CREDENTIAL_DEVICE_CERT, &cert_len);
	char port[6];
	int on = 1;
	int rc;

	/* The broker takes the identity from the certificate too, and allows this device only its own topics. */
	if (tg_identity_from_cert(cert, cert_len, identity, sizeof(identity)) != 0) {
		LOG_ERR("The device certificate names no identity");
		return -TRACEGALE_ERR_IDENTITY;
	}
	snprintk(up, sizeof(up), "tg/v1/%s/up", identity);
	snprintk(down, sizeof(down), "tg/v1/%s/down", identity);

	snprintk(port, sizeof(port), "%u", tg_store_port());
	rc = zsock_getaddrinfo(host, port, &hints, &found);
	if (rc != 0) {
		*detail = rc;
		return -TRACEGALE_ERR_DNS;
	}
	/* Every address is tried, as in tls_socket.c, and a TLS failure is the more useful one to report. */
	rc = -TRACEGALE_ERR_CONNECT;
	for (const struct zsock_addrinfo *addr = found; addr != NULL; addr = addr->ai_next) {
		int attempt_detail = 0;
		int attempt = connect_to(addr, host, &attempt_detail);

		if (attempt == 0) {
			rc = 0;
			break;
		}
		if (rc != -TRACEGALE_ERR_TLS) {
			rc = attempt;
			*detail = attempt_detail;
		}
	}
	zsock_freeaddrinfo(found);
	if (rc != 0) {
		return rc;
	}
	connected = true;
	memset(&seen, 0, sizeof(seen));

	/* CONNACK. */
	rc = wait_for(&seen.connack, deadline);
	if (seen.connack && seen.code != 0) {
		*detail = seen.code;
		LOG_WRN("The broker refused the connection: %d", seen.code);
		drop();
		return -TRACEGALE_ERR_REFUSED;
	}
	if (rc != 0) {
		drop();
		return as_error(rc, detail);
	}
	/* Each exchange was measured to get faster with TCP batching off. It isn't required, so a failure doesn't matter. */
	(void)zsock_setsockopt(client.transport.tls.sock, TG_PROTO_TCP, TG_TCP_NODELAY, &on, sizeof(on));

	/* SUBSCRIBE to the replies, at QoS 1. */
	struct mqtt_topic topic = {
		.topic = {.utf8 = (const uint8_t *)down, .size = strlen(down)},
		.qos = MQTT_QOS_1_AT_LEAST_ONCE,
	};
	struct mqtt_subscription_list list = {.list = &topic, .list_count = 1, .message_id = 1};

	seen.code = 0;
	rc = mqtt_subscribe(&client, &list);
	if (rc == 0) {
		rc = wait_for(&seen.suback, deadline);
	}
	if (rc == 0 && seen.code == 0x80) {
		*detail = 0x80;
		drop();
		return -TRACEGALE_ERR_REFUSED;
	}
	if (rc != 0) {
		drop();
		return as_error(rc, detail);
	}
	packet_id = 1;
	return 0;
}

static int mqtt_exchange(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_cap, size_t *rx_len, int *detail)
{
	k_timepoint_t deadline = sys_timepoint_calc(K_MSEC(CONFIG_TRACEGALE_TIMEOUT_MS));
	struct mqtt_publish_param param = {
		.message = {
			.topic = {.topic = {.utf8 = (const uint8_t *)up, .size = strlen(up)}, .qos = MQTT_QOS_1_AT_LEAST_ONCE},
			.payload = {.data = (uint8_t *)tx, .len = tx_len},
		},
	};
	int rc;

	if (!connected) {
		return -TRACEGALE_ERR_SEND;
	}
	packet_id = packet_id == UINT16_MAX ? 2 : packet_id + 1;
	param.message_id = packet_id;
	seen.reply = false;
	seen.code = 0;
	seen.rx = rx;
	seen.rx_cap = rx_cap;
	seen.rx_len = rx_len;
	rc = mqtt_publish(&client, &param);
	if (rc == 0) {
		rc = wait_for(&seen.reply, deadline);
	}
	if (rc == 0) {
		rc = seen.code;
	}
	seen.rx = NULL;
	if (rc == -EMSGSIZE) {
		return -TRACEGALE_ERR_TOO_LARGE;
	}
	if (rc == -EBADMSG) {
		return -TRACEGALE_ERR_BAD_REPLY;
	}
	return rc == 0 ? 0 : as_error(rc, detail);
}

static void mqtt_close(void)
{
	if (connected) {
#if KERNEL_VERSION_NUMBER >= ZEPHYR_VERSION(4, 2, 0)
		/* Zephyr v4.2 added the MQTT 5.0 parameters, which NULL leaves out at 3.1.1. */
		(void)mqtt_disconnect(&client, NULL);
#else
		(void)mqtt_disconnect(&client);
#endif
	}
	drop();
}

const struct tg_transport *tg_transport_get(void)
{
	static const struct tg_transport mqtt = {.open = mqtt_open, .exchange = mqtt_exchange, .close = mqtt_close};

	return &mqtt;
}
