/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * A Zephyr log backend that keeps records in a RAM buffer, and sends them with LOGS at each check-in.
 *
 * The buffer holds records one after another, never split. When a record doesn't fit at the end, the rest of the end is left
 * empty and the record goes at the start, if the oldest records have left room there. When the buffer is full, the oldest
 * records go, and are counted as dropped. While a message is on its way, the records in it stay put: a record that arrives
 * then and finds no room is the one dropped. So sending needs the lock only for moments, never for a whole message.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_msg.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/cbprintf.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <tracegale/tracegale.h>

#include "ack.h"
#include "logs.h"
#include "wire.h"

#define CAPACITY       CONFIG_TRACEGALE_LOGS_BUFFER_SIZE
#define MAX_RECORDS    256U
#define MAX_MODULE     32U
#define MAX_MESSAGES   8
/* The outer array, version, type, an ID of up to 5 bytes, and the body's map. */
#define ENVELOPE_SIZE  9U

struct record {
	uint32_t seq;
	/* Bytes from this header to the next record. */
	uint16_t size;
	uint8_t level;
	uint8_t len;
	uint64_t uptime_ms;
	const char *module;
	char text[];
};

static uint8_t ring[CAPACITY] __aligned(8);
static struct k_spinlock lock;
/* The oldest record, where the next goes, and how many there are. */
static size_t head, tail;
static uint32_t count;
/* Set when records continue from the start of the buffer, with the end unused from wrap_at. */
static bool wrapped;
static size_t wrap_at;
static uint32_t next_seq;
static uint32_t dropped;
static uint32_t sent;
static bool sending;
static uint32_t boot;

static struct record *at(size_t pos)
{
	return (struct record *)&ring[pos];
}

static size_t after(size_t pos)
{
	pos += at(pos)->size;
	return wrapped && pos == wrap_at ? 0 : pos;
}

static void forget_oldest(void)
{
	head += at(head)->size;
	if (wrapped && head == wrap_at) {
		head = 0;
		wrapped = false;
	}
	count--;
	if (count == 0) {
		head = tail = 0;
		wrapped = false;
	}
}

/* Where a record of this size can go, or -1. */
static int room_for(size_t size)
{
	if (count == 0) {
		head = tail = 0;
		wrapped = false;
		return 0;
	}
	if (wrapped) {
		return head - tail >= size ? (int)tail : -1;
	}
	if (CAPACITY - tail >= size) {
		return tail;
	}
	if (head >= size) {
		wrapped = true;
		wrap_at = tail;
		tail = 0;
		return 0;
	}
	return -1;
}

/* Only printable ASCII is kept, so the text is always valid UTF-8 and can't move a terminal's cursor. */
static char clean(char c)
{
	return (c >= 0x20 && c < 0x7f) ? c : '?';
}

void tg_logs_add(uint8_t level, const char *module, const char *text, size_t len)
{
	size_t size;
	k_spinlock_key_t key;
	int pos;

	while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
		len--;
	}
	len = MIN(len, CONFIG_TRACEGALE_LOGS_LINE_MAX);
	size = ROUND_UP(sizeof(struct record) + len, 8);
	key = k_spin_lock(&lock);
	while ((pos = room_for(size)) < 0) {
		if (sending) {
			/* The records on their way stay. This one goes instead. */
			dropped++;
			k_spin_unlock(&lock, key);
			return;
		}
		forget_oldest();
		dropped++;
	}
	struct record *r = at(pos);

	r->seq = next_seq++;
	r->size = size;
	r->level = level;
	r->len = len;
	r->uptime_ms = k_uptime_get();
	r->module = module != NULL ? module : "";
	for (size_t i = 0; i < len; i++) {
		r->text[i] = clean(text[i]);
	}
	tail = pos + size;
	count++;
	k_spin_unlock(&lock, key);
}

void tg_logs_reset(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	head = tail = 0;
	count = 0;
	wrapped = false;
	next_seq = 0;
	dropped = 0;
	sent = 0;
	sending = false;
	k_spin_unlock(&lock, key);
}

void tg_logs_state_get(struct tg_logs_state *out)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	size_t used = 0;

	for (uint32_t i = 0, pos = head; i < count; i++, pos = after(pos)) {
		used += at(pos)->size;
	}
	*out = (struct tg_logs_state){.waiting = count, .dropped = dropped, .sent = sent, .next_seq = next_seq, .used = used};
	k_spin_unlock(&lock, key);
}

bool tg_logs_peek(uint32_t index, struct tg_logs_record *out)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool found = index < count;

	if (found) {
		size_t pos = head;

		for (uint32_t i = 0; i < index; i++) {
			pos = after(pos);
		}
		struct record *r = at(pos);

		out->seq = r->seq;
		out->uptime_ms = r->uptime_ms;
		out->level = r->level;
		out->module = r->module;
		memcpy(out->text, r->text, r->len);
		out->text[r->len] = '\0';
	}
	k_spin_unlock(&lock, key);
	return found;
}

/* The bytes a text of this length takes, with its head. */
static size_t text_size(size_t len)
{
	return (len < 24 ? 1 : 2) + len;
}

/* strnlen() is POSIX, not C, so not in every C library Zephyr builds with. */
static size_t module_len(const char *module)
{
	size_t len = 0;

	while (len < MAX_MODULE && module[len] != '\0') {
		len++;
	}
	return len;
}

static size_t record_size(const struct record *r, size_t len)
{
	return 1 + tg_uint_size(r->seq) + tg_uint64_size(r->uptime_ms) + 1 + text_size(module_len(r->module)) + text_size(len);
}

/* One message: as many records as fit, from the oldest. @return 0, or as tg_exchange_ack. *more is set if records are left. */
static int send_one(const struct tg_transport *transport, uint8_t *buf, size_t cap, uint32_t (*next_id)(void), int *detail,
		    bool *more)
{
	struct tg_writer w;
	k_spinlock_key_t key = k_spin_lock(&lock);
	uint32_t waiting = count;
	uint32_t lost = dropped;
	size_t first = head;
	uint64_t now = k_uptime_get();
	size_t used = ENVELOPE_SIZE + 2 + tg_uint_size(boot) + 2 + tg_uint64_size(now) + 1 + 3 + (lost > 0 ? 1 + tg_uint_size(lost) : 0);
	uint32_t n = 0;
	size_t first_len = 0;
	size_t pos;
	uint32_t id;
	int rc;

	sending = waiting > 0;
	k_spin_unlock(&lock, key);
	*more = false;
	if (waiting == 0 && lost == 0) {
		return 0;
	}

	/* The records in [first, first + waiting) can't move or go until sending is cleared. */
	pos = first;
	for (uint32_t i = 0; i < waiting && n < MAX_RECORDS; i++) {
		struct record *r = at(pos);
		size_t size = record_size(r, r->len);

		if (used + size > cap) {
			if (n == 0) {
				/* Larger than a whole message, which only a very small buffer allows. Its text is cut to fit. */
				size_t spare = cap - used - record_size(r, 0);

				first_len = MIN(r->len, spare > 2 ? spare - 2 : 0);
				n = 1;
			}
			break;
		}
		used += size;
		n++;
		key = k_spin_lock(&lock);
		pos = after(pos);
		k_spin_unlock(&lock, key);
	}
	*more = n < waiting;

	id = next_id();
	tg_write_begin(&w, buf, cap, TG_LOGS, id, lost > 0 ? 4 : 3);
	tg_write_uint(&w, 0, boot);
	tg_write_uint64(&w, 1, now);
	tg_write_array(&w, 2, n);
	pos = first;
	for (uint32_t i = 0; i < n; i++) {
		struct record *r = at(pos);

		tg_write_item_array(&w, 5);
		tg_write_item_uint64(&w, r->seq);
		tg_write_item_uint64(&w, r->uptime_ms);
		tg_write_item_uint64(&w, r->level);
		tg_write_item_textn(&w, r->module, module_len(r->module));
		tg_write_item_textn(&w, r->text, (i == 0 && first_len > 0) ? first_len : r->len);
		key = k_spin_lock(&lock);
		pos = after(pos);
		k_spin_unlock(&lock, key);
	}
	if (lost > 0) {
		tg_write_uint(&w, 3, lost);
	}
	if (w.overflow) {
		rc = -TRACEGALE_ERR_TOO_LARGE;
		*detail = 0;
	} else {
		rc = tg_exchange_ack(transport, buf, cap, w.len, id, detail);
	}

	key = k_spin_lock(&lock);
	if (rc == 0 || (rc == -TRACEGALE_ERR_SERVER && !tg_refusal_is_temporary(*detail))) {
		/* The server has them, or will never take them. */
		for (uint32_t i = 0; i < n; i++) {
			forget_oldest();
		}
		dropped -= lost;
		sent += rc == 0 ? n : 0;
	}
	sending = false;
	k_spin_unlock(&lock, key);
	return rc;
}

int tg_logs_send(const struct tg_transport *transport, uint8_t *buf, size_t cap, uint32_t (*next_id)(void), int *detail)
{
	bool more = true;
	int rc = 0;

	if (boot == 0) {
		/* Not at start-up, where the random generator may not be ready. Records before the first send share it anyway. */
		boot = sys_rand32_get() | 1U;
	}
	/* A limit for each check-in, so a device that logs without pause still gets to its update. */
	for (int i = 0; i < MAX_MESSAGES && more && rc == 0; i++) {
		rc = send_one(transport, buf, cap, next_id, detail, &more);
	}
	return rc;
}

/* The backend. */

struct line {
	char text[CONFIG_TRACEGALE_LOGS_LINE_MAX];
	size_t len;
};

static int out(int c, void *ctx)
{
	struct line *line = ctx;

	if (line->len < sizeof(line->text)) {
		line->text[line->len++] = (char)c;
	}
	return 0;
}

static void process(const struct log_backend *const backend, union log_msg_generic *generic)
{
	struct log_msg *msg = &generic->log;
	uint8_t level = log_msg_get_level(msg);
	int16_t source = log_msg_get_source_id(msg);
	const char *module = NULL;
	struct line line = {.len = 0};
	size_t package_len, data_len;
	uint8_t *package = log_msg_get_package(msg, &package_len);

	ARG_UNUSED(backend);
	/* 0 is printk and raw output, which has no level. */
	if (level == LOG_LEVEL_NONE || level > CONFIG_TRACEGALE_LOGS_LEVEL) {
		return;
	}
	if (source >= 0) {
		module = log_source_name_get(log_msg_get_domain(msg), source);
	}
	if (package_len > 0) {
		(void)cbpprintf((cbprintf_cb)out, &line, package);
	}
	(void)log_msg_get_data(msg, &data_len);
	if (data_len > 0) {
		/* A hex dump's bytes could hold anything, so only their number is kept. */
		char note[24];
		int n = snprintk(note, sizeof(note), " (%u bytes of data)", (unsigned int)data_len);

		for (int i = 0; i < n && i < (int)sizeof(note); i++) {
			(void)out(note[i], &line);
		}
	}
	tg_logs_add(level, module, line.text, line.len);
}

static void dropped_by_zephyr(const struct log_backend *const backend, uint32_t cnt)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	ARG_UNUSED(backend);
	dropped += cnt;
	k_spin_unlock(&lock, key);
}

static void panic(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);
	/* Records are kept in RAM, and nothing is written anywhere at a panic. */
}

static const struct log_backend_api api = {
	.process = process,
	.dropped = dropped_by_zephyr,
	.panic = panic,
};

LOG_BACKEND_DEFINE(tracegale_logs, api, true);
