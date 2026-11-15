/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * The bytes that Zephyr's network stack counts, with a datagram sent over the loopback interface.
 */

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "core.h"
#include "fake.h"
#include "metrics.h"

ZTEST(metrics_net, test_bytes_are_counted_for_each_period)
{
	static const uint8_t payload[200];
	struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(4242)};
	int receiver = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	int sender = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	zassert_true(receiver >= 0 && sender >= 0);
	zassert_equal(zsock_inet_pton(AF_INET, "127.0.0.1", &address.sin_addr), 1);
	zassert_ok(zsock_bind(receiver, (struct sockaddr *)&address, sizeof(address)));
	zassert_equal(zsock_sendto(sender, payload, sizeof(payload), 0, (struct sockaddr *)&address, sizeof(address)),
		      sizeof(payload));
	k_sleep(K_MSEC(200));

	fake_reset();
	zassert_ok(tg_core_attempt());
	zassert_true(srv.report[0].has[TG_SYS_NET_SENT] && srv.report[0].has[TG_SYS_NET_RECEIVED]);
	zassert_true(srv.report[0].system[TG_SYS_NET_SENT] >= sizeof(payload), "sent %u",
		     srv.report[0].system[TG_SYS_NET_SENT]);
	zassert_true(srv.report[0].system[TG_SYS_NET_RECEIVED] >= sizeof(payload), "received %u",
		     srv.report[0].system[TG_SYS_NET_RECEIVED]);

	fake_reset();
	zassert_ok(tg_core_attempt());
	zassert_equal(srv.report[0].system[TG_SYS_NET_SENT], 0, "a counter starts again once it's reported");
	zassert_equal(srv.report[0].system[TG_SYS_NET_RECEIVED], 0);
	zsock_close(sender);
	zsock_close(receiver);
}

ZTEST_SUITE(metrics_net, NULL, NULL, fake_before, NULL, NULL);
