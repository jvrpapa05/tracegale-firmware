/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tracegale/tracegale.h>

#include "ack.h"
#include "wire.h"

int tg_exchange_ack(const struct tg_transport *transport, uint8_t *buf, size_t cap, size_t len, uint32_t id, int *detail)
{
	struct tg_message reply;
	size_t reply_len = 0;
	uint32_t code = 0;
	int rc = transport->exchange(buf, len, buf, cap, &reply_len, detail);

	if (rc != 0) {
		return rc;
	}
	*detail = 0;
	if (tg_read_message(buf, reply_len, &reply) != 0 || reply.id != id) {
		return -TRACEGALE_ERR_BAD_REPLY;
	}
	if (reply.type == TG_ERROR) {
		(void)tg_read_uint(&reply, 0, &code);
		*detail = code;
		return -TRACEGALE_ERR_SERVER;
	}
	return reply.type == TG_ACK ? 0 : -TRACEGALE_ERR_BAD_REPLY;
}

bool tg_refusal_is_temporary(int detail)
{
	return detail == TG_WIRE_ERR_RATE_LIMITED || detail == TG_WIRE_ERR_SERVER_BUSY;
}
