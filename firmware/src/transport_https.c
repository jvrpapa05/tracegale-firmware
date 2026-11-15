/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * The HTTPS binding from docs/protocol/README.md. Nothing else in Tracegale depends on this file.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>

#include "compat.h"
#include "store.h"
#include "tls_socket.h"
#include "transport.h"

LOG_MODULE_DECLARE(tracegale, CONFIG_TRACEGALE_LOG_LEVEL);

#define CONTENT_TYPE "application/vnd.tracegale+cbor"

static int sock = -1;
static uint8_t http_buf[512];

struct reply {
	uint8_t *rx;
	size_t cap;
	size_t len;
	int status;
	bool overflow;
};

static void on_data(struct http_response *rsp, struct reply *reply)
{
	if (rsp->http_status_code != 0) {
		reply->status = rsp->http_status_code;
	}
	if (rsp->body_frag_start != NULL && rsp->body_frag_len > 0) {
		if (rsp->body_frag_len > reply->cap - reply->len) {
			reply->overflow = true;
		} else {
			memcpy(&reply->rx[reply->len], rsp->body_frag_start, rsp->body_frag_len);
			reply->len += rsp->body_frag_len;
		}
	}
}

#if KERNEL_VERSION_NUMBER >= ZEPHYR_VERSION(4, 3, 0)
static int on_response(struct http_response *rsp, enum http_final_call final, void *user_data)
{
	ARG_UNUSED(final);
	on_data(rsp, user_data);
	return 0;
}
#else
static void on_response(struct http_response *rsp, enum http_final_call final, void *user_data)
{
	ARG_UNUSED(final);
	on_data(rsp, user_data);
}
#endif

static int https_open(int *detail)
{
	return tg_tls_open(&sock, detail);
}

static int https_exchange(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_cap, size_t *rx_len, int *detail)
{
	struct http_request req = {0};
	struct reply reply = {.rx = rx, .cap = rx_cap};
	int rc;

	req.method = HTTP_POST;
	req.url = "/v1/m";
	req.host = tg_store_host();
	req.protocol = "HTTP/1.1";
	req.response = on_response;
	req.recv_buf = http_buf;
	req.recv_buf_len = sizeof(http_buf);
	req.payload = (const char *)tx;
	req.payload_len = tx_len;
	req.content_type_value = CONTENT_TYPE;
	rc = http_client_req(sock, &req, CONFIG_TRACEGALE_TIMEOUT_MS, &reply);
	if (rc < 0) {
		*detail = rc;
		return rc == -ETIMEDOUT || rc == -EAGAIN ? -TRACEGALE_ERR_TIMEOUT : -TRACEGALE_ERR_SEND;
	}
	if (reply.overflow) {
		return -TRACEGALE_ERR_TOO_LARGE;
	}
	if (reply.status != 200) {
		*detail = reply.status;
		return -TRACEGALE_ERR_HTTP_STATUS;
	}
	*rx_len = reply.len;
	return 0;
}

static void https_close(void)
{
	if (sock >= 0) {
		zsock_close(sock);
		sock = -1;
	}
}

const struct tg_transport *tg_transport_get(void)
{
	static const struct tg_transport https = {.open = https_open, .exchange = https_exchange, .close = https_close};

	return &https;
}
