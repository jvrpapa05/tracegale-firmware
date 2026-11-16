/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * CONFIG_TRACEGALE_NET_WAKE: after a failed attempt, Tracegale tries again soon after the network comes up, on any link,
 * instead of waiting out the backoff. A device that's already connected keeps its schedule.
 */

#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "fake_creds.h"
#include "transport.h"
#include "wire.h"

/* Longer than the random wait in prj.conf, so a retry it brings forward always falls inside it. */
#define WAIT_S 5
#if defined(CONFIG_TRACEGALE_NET_WAKE_JITTER_S)
BUILD_ASSERT(CONFIG_TRACEGALE_NET_WAKE_JITTER_S + 2 <= WAIT_S);
#endif

static atomic_t link_down = ATOMIC_INIT(1);
static atomic_t opened;

static int fake_open(int *detail)
{
	atomic_inc(&opened);
	*detail = 0;
	/* What a device sees before its link is up: no route to the server. */
	return atomic_get(&link_down) ? -TRACEGALE_ERR_CONNECT : 0;
}

static int fake_exchange(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_cap, size_t *rx_len, int *detail)
{
	struct tg_message msg;
	struct tg_writer w;

	ARG_UNUSED(detail);
	zassert_ok(tg_read_message(tx, tx_len, &msg));
	tg_write_begin(&w, rx, rx_cap, TG_ACK, msg.id, 0);
	*rx_len = w.len;
	return 0;
}

static void fake_close(void)
{
}

const struct tg_transport *tg_transport_get(void)
{
	static const struct tg_transport transport = {.open = fake_open, .exchange = fake_exchange, .close = fake_close};

	return &transport;
}

static enum tracegale_state state_now(void)
{
	struct tracegale_status status;

	tracegale_get_status(&status);
	return status.state;
}

static bool wait_for_state(enum tracegale_state wanted, int seconds)
{
	for (int i = 0; i < seconds * 10; i++) {
		if (state_now() == wanted) {
			return true;
		}
		k_msleep(100);
	}
	return false;
}

static void network_up(void)
{
	net_mgmt_event_notify(NET_EVENT_L4_CONNECTED, net_if_get_default());
}

ZTEST(netwake, test_retries_when_the_network_comes_up_and_only_then)
{
	struct tracegale_status status;
	int before;

	FAKE_PROVISION();

	/* The first attempt fails, because the link isn't up yet, and the backoff is long. */
	zassert_true(wait_for_state(TRACEGALE_STATE_BACKING_OFF, 5), "the first attempt should fail");
	before = atomic_get(&opened);
	k_sleep(K_SECONDS(WAIT_S));
	zassert_equal(atomic_get(&opened), before, "nothing should retry before the network comes up");

	/* The link comes up: Tracegale tries again within the random wait, and succeeds. */
	atomic_set(&link_down, 0);
	network_up();
	zassert_true(wait_for_state(TRACEGALE_STATE_IDLE, WAIT_S),
		     "the network coming up should bring the retry forward");
	tracegale_get_status(&status);
	zassert_equal(status.successes, 1);

	/* Connected: the link coming and going again doesn't cause extra check-ins. */
	before = atomic_get(&opened);
	network_up();
	k_sleep(K_SECONDS(WAIT_S));
	zassert_equal(atomic_get(&opened), before, "a connected device keeps its schedule");
	zassert_equal(state_now(), TRACEGALE_STATE_IDLE);
}

ZTEST_SUITE(netwake, NULL, NULL, NULL, NULL, NULL);
