/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * A report is sent at each check-in. Nothing is given up until the server has acknowledged it: a counter keeps its total and
 * a gauge its reading through any number of failed attempts, and what the application adds while a report is on its way
 * belongs to the next one.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_TRACEGALE_METRICS_HEAP)
#include <zephyr/sys/sys_heap.h>
#endif
#if defined(CONFIG_TRACEGALE_METRICS_NET)
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_stats.h>
#endif

#include <tracegale/tracegale.h>

#include "ack.h"
#include "metrics.h"
#include "wire.h"

/* The metric has a value that no acknowledged report holds. */
#define FLAG_SET BIT(0)
/* The metric is in the message being sent, with the value in "sent". */
#define FLAG_FLIGHT BIT(1)
/* The metric has been in a message of this report, and each metric goes in one only. */
#define FLAG_DONE BIT(2)

/* The protocol's limit for each of the two arrays in one message. */
#define MAX_PER_MESSAGE 128U

/* The start of a message: the outer array, the version, the type, an ID of up to 5 bytes, and the body's map. */
#define ENVELOPE_SIZE 9U
/* A key, and an array's head for up to 256 items. */
#define ARRAY_HEAD_SIZE 4U

/* The readings that a report is measured against. They move on only when the server acknowledges a report. */
struct base {
	uint32_t uptime_s;
	uint32_t failures;
	uint64_t cpu_all;
	uint64_t cpu_busy;
	uint64_t net_sent;
	uint64_t net_received;
};

static struct k_spinlock lock;
static struct base base;
static uint32_t failures;
static uint32_t checkin_ms;
static uint32_t reports_sent;

void tracegale_metric_set(struct tracegale_metric *metric, int32_t value)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	metric->value = value;
	metric->flags |= FLAG_SET;
	k_spin_unlock(&lock, key);
}

void tracegale_metric_add(struct tracegale_metric *metric, int32_t amount)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	int64_t sum = (int64_t)metric->value + amount;

	/* A total that's too large stays at the limit, where it's visibly wrong, rather than wrapping to a believable value. */
	metric->value = CLAMP(sum, INT32_MIN, INT32_MAX);
	metric->flags |= FLAG_SET;
	k_spin_unlock(&lock, key);
}

void tg_metrics_checkin(uint32_t ms)
{
	checkin_ms = ms;
}

void tg_metrics_failure(void)
{
	failures++;
}

uint32_t tg_metrics_sent(void)
{
	return reports_sent;
}

#if defined(CONFIG_TRACEGALE_METRICS_HEAP) && defined(K_HEAP_MEM_POOL_SIZE) && (K_HEAP_MEM_POOL_SIZE > 0)
#define HAVE_HEAP 1
/* The kernel's heap, which k_malloc() uses. No public header declares it, and Zephyr's own shell reads it the same way. */
extern struct k_heap _system_heap;
#endif

#if defined(CONFIG_TRACEGALE_METRICS_STACK)
static void stack_low(const struct k_thread *thread, void *user_data)
{
	uint32_t *low = user_data;
	size_t unused;

	if (k_thread_stack_space_get(thread, &unused) == 0 && unused < *low) {
		*low = unused;
	}
}
#endif

static void add(struct tg_metrics_system *out, uint8_t key, uint32_t value)
{
	out->key[out->count] = key;
	out->value[out->count] = value;
	out->count++;
}

#if defined(CONFIG_TRACEGALE_METRICS_NET)
/* The change in a counter of Zephyr's that's 32 bits wide on some versions, where it wraps, and 64 on others. */
static uint32_t since(uint64_t now, uint64_t before, size_t width)
{
	uint64_t change = now - before;

	if (width == sizeof(uint32_t)) {
		return (uint32_t)change;
	}
	return (uint32_t)MIN(change, (uint64_t)UINT32_MAX);
}
#endif

/* Reads everything, in key order. *now gets the readings that become the base once this is acknowledged. */
static void measure(struct tg_metrics_system *out, struct base *now)
{
	*now = base;
	now->uptime_s = (uint32_t)(k_uptime_get() / MSEC_PER_SEC);
	now->failures = failures;
	out->count = 0;
	add(out, TG_SYS_UPTIME_S, now->uptime_s);
#if defined(HAVE_HEAP)
	struct sys_memory_stats heap;
	k_spinlock_key_t key = k_spin_lock(&_system_heap.lock);
	int rc = sys_heap_runtime_stats_get(&_system_heap.heap, &heap);

	k_spin_unlock(&_system_heap.lock, key);
	if (rc == 0) {
		add(out, TG_SYS_HEAP_FREE, heap.free_bytes);
		add(out, TG_SYS_HEAP_LOW, heap.free_bytes + heap.allocated_bytes - heap.max_allocated_bytes);
	}
#endif
#if defined(CONFIG_TRACEGALE_METRICS_STACK)
	uint32_t low = UINT32_MAX;

	/* Each stack is read from its end to the first byte that was ever used, which takes long enough to matter with
	 * interrupts off.
	 */
	if (IS_ENABLED(CONFIG_TRACEGALE_METRICS_STACK_LOCKED)) {
		k_thread_foreach(stack_low, &low);
	} else {
		k_thread_foreach_unlocked(stack_low, &low);
	}
	if (low != UINT32_MAX) {
		add(out, TG_SYS_STACK_LOW, low);
	}
#endif
#if defined(CONFIG_TRACEGALE_METRICS_CPU)
	k_thread_runtime_stats_t cpu;

	if (k_thread_runtime_stats_all_get(&cpu) == 0) {
		uint64_t all, busy;

		now->cpu_all = cpu.execution_cycles;
		now->cpu_busy = cpu.total_cycles;
		all = now->cpu_all - base.cpu_all;
		busy = now->cpu_busy - base.cpu_busy;
		/* Scaled down together, which keeps the ratio and needs no 64-bit division. */
		while (all >> 21 != 0) {
			all >>= 1;
			busy >>= 1;
		}
		if (all > 0) {
			add(out, TG_SYS_CPU_LOAD, MIN((uint32_t)busy * 1000U / (uint32_t)all, 1000U));
		}
	}
#endif
#if defined(CONFIG_TRACEGALE_METRICS_NET)
	struct net_stats_bytes bytes;

	if (net_mgmt(NET_REQUEST_STATS_GET_BYTES, NULL, &bytes, sizeof(bytes)) == 0) {
		now->net_sent = bytes.sent;
		now->net_received = bytes.received;
		add(out, TG_SYS_NET_SENT, since(now->net_sent, base.net_sent, sizeof(bytes.sent)));
		add(out, TG_SYS_NET_RECEIVED, since(now->net_received, base.net_received, sizeof(bytes.received)));
	}
#endif
	add(out, TG_SYS_CHECKIN_FAILURES, now->failures - base.failures);
	add(out, TG_SYS_CHECKIN_MS, checkin_ms);
}

void tg_metrics_system_get(struct tg_metrics_system *out)
{
	struct base now;

	measure(out, &now);
}

/* Picks the application's metrics for the next message, as many as fit in "room" bytes, and takes their values.
 * @return true if some are left for another message.
 */
static bool pick(size_t room, uint32_t count[2])
{
	bool more = false;

	count[TRACEGALE_METRIC_GAUGE] = 0;
	count[TRACEGALE_METRIC_COUNTER] = 0;
	STRUCT_SECTION_FOREACH(tracegale_metric, metric) {
		k_spinlock_key_t key = k_spin_lock(&lock);

		if ((metric->flags & (FLAG_SET | FLAG_DONE)) == FLAG_SET) {
			size_t name = strlen(metric->name);
			size_t size = tg_uint_size(name) + name + tg_int_size(metric->value);

			if (size <= room && count[metric->kind] < MAX_PER_MESSAGE) {
				room -= size;
				count[metric->kind]++;
				metric->sent = metric->value;
				metric->flags = (metric->flags & ~FLAG_SET) | FLAG_FLIGHT;
			} else {
				more = true;
			}
		}
		k_spin_unlock(&lock, key);
	}
	return more;
}

/* Ends a message. If the server has its values, or refused them for good, they're given up. If not, they're kept. */
static void settle(bool give_up)
{
	STRUCT_SECTION_FOREACH(tracegale_metric, metric) {
		k_spinlock_key_t key = k_spin_lock(&lock);

		if ((metric->flags & FLAG_FLIGHT) != 0) {
			metric->flags = (metric->flags & ~FLAG_FLIGHT) | FLAG_DONE;
			if (!give_up) {
				metric->flags |= FLAG_SET;
			} else if (metric->kind == TRACEGALE_METRIC_COUNTER) {
				/* What was added meanwhile stays, for the next report. */
				metric->value = CLAMP((int64_t)metric->value - metric->sent, INT32_MIN, INT32_MAX);
			}
		}
		k_spin_unlock(&lock, key);
	}
}

static void write_kind(struct tg_writer *w, uint32_t key, uint8_t kind, uint32_t count)
{
	if (count == 0) {
		return;
	}
	tg_write_array(w, key, 2 * count);
	STRUCT_SECTION_FOREACH(tracegale_metric, metric) {
		if ((metric->flags & FLAG_FLIGHT) != 0 && metric->kind == kind) {
			tg_write_item_text(w, metric->name);
			tg_write_item_int(w, metric->sent);
		}
	}
}

int tg_metrics_send(const struct tg_transport *transport, uint8_t *buf, size_t cap, uint32_t (*next_id)(void),
		    int *detail)
{
	struct tg_metrics_system system;
	struct base now;
	uint32_t period;
	bool first = true;
	bool more;
	int rc;

	measure(&system, &now);
	period = now.uptime_s - base.uptime_s;
	do {
		size_t fixed = ENVELOPE_SIZE + 1 + tg_uint_size(period) + 2 * ARRAY_HEAD_SIZE;
		uint32_t count[2];
		struct tg_writer w;
		uint32_t id;
		bool give_up;

		if (first) {
			fixed += 2;
			for (size_t i = 0; i < system.count; i++) {
				fixed += 1 + tg_uint_size(system.value[i]);
			}
		}
		more = pick(cap > fixed ? cap - fixed : 0, count);
		if (!first && count[TRACEGALE_METRIC_GAUGE] + count[TRACEGALE_METRIC_COUNTER] == 0) {
			/* Can't happen with a buffer of the smallest size allowed, where any one metric fits. */
			rc = 0;
			break;
		}
		id = next_id();
		tg_write_begin(&w, buf, cap, TG_METRICS, id,
			       1 + (first ? 1 : 0) + (count[TRACEGALE_METRIC_GAUGE] > 0 ? 1 : 0) +
				       (count[TRACEGALE_METRIC_COUNTER] > 0 ? 1 : 0));
		tg_write_uint(&w, 0, period);
		if (first) {
			tg_write_map(&w, 1, system.count);
			for (size_t i = 0; i < system.count; i++) {
				tg_write_uint(&w, system.key[i], system.value[i]);
			}
		}
		write_kind(&w, 2, TRACEGALE_METRIC_GAUGE, count[TRACEGALE_METRIC_GAUGE]);
		write_kind(&w, 3, TRACEGALE_METRIC_COUNTER, count[TRACEGALE_METRIC_COUNTER]);
		if (w.overflow) {
			settle(false);
			*detail = 0;
			rc = -TRACEGALE_ERR_TOO_LARGE;
			break;
		}
		rc = tg_exchange_ack(transport, buf, cap, w.len, id, detail);
		/* A busy server gets the values again later. Any other refusal would be the same next time, so they're dropped,
		 * which also keeps a server that doesn't know METRICS from being sent ever larger reports.
		 */
		give_up = rc == 0 || (rc == -TRACEGALE_ERR_SERVER && !tg_refusal_is_temporary(*detail));
		settle(give_up);
		if (give_up && first) {
			/* The period starts again here even if a later message of this report is lost. What that message
			 * held is then reported with a period that's too short, once.
			 */
			base = now;
		}
		if (rc == 0) {
			reports_sent += first ? 1 : 0;
		}
		first = false;
	} while (rc == 0 && more);
	STRUCT_SECTION_FOREACH(tracegale_metric, metric) {
		k_spinlock_key_t key = k_spin_lock(&lock);

		metric->flags &= ~FLAG_DONE;
		k_spin_unlock(&lock, key);
	}
	return rc;
}
