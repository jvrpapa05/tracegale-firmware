/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include "wire.h"

#define MAX_MESSAGE 65535U
#define MAX_DEPTH 4
#define MAX_ITEMS 256U

enum { MAJOR_UINT, MAJOR_NEG, MAJOR_BYTES, MAJOR_TEXT, MAJOR_ARRAY, MAJOR_MAP, MAJOR_TAG, MAJOR_SIMPLE };

static void put(struct tg_writer *w, const void *data, size_t len)
{
	if (w->overflow || len > w->cap - w->len) {
		w->overflow = true;
		return;
	}
	memcpy(&w->buf[w->len], data, len);
	w->len += len;
}

/* Only the shortest form, because receivers reject anything else. */
static void put_head(struct tg_writer *w, uint8_t major, uint32_t value)
{
	uint8_t out[5];
	size_t size;

	if (value < 24) {
		out[0] = (major << 5) | value;
		size = 1;
	} else if (value <= 0xff) {
		out[0] = (major << 5) | 24;
		out[1] = value;
		size = 2;
	} else if (value <= 0xffff) {
		out[0] = (major << 5) | 25;
		out[1] = value >> 8;
		out[2] = value;
		size = 3;
	} else {
		out[0] = (major << 5) | 26;
		out[1] = value >> 24;
		out[2] = value >> 16;
		out[3] = value >> 8;
		out[4] = value;
		size = 5;
	}
	put(w, out, size);
}

void tg_write_begin(struct tg_writer *w, uint8_t *buf, size_t cap, uint32_t type, uint32_t id, uint32_t fields)
{
	w->buf = buf;
	w->cap = cap;
	w->len = 0;
	w->overflow = false;
	put_head(w, MAJOR_ARRAY, 4);
	put_head(w, MAJOR_UINT, 1);
	put_head(w, MAJOR_UINT, type);
	put_head(w, MAJOR_UINT, id);
	put_head(w, MAJOR_MAP, fields);
}

void tg_write_uint(struct tg_writer *w, uint32_t key, uint32_t value)
{
	put_head(w, MAJOR_UINT, key);
	put_head(w, MAJOR_UINT, value);
}

void tg_write_bytes(struct tg_writer *w, uint32_t key, const uint8_t *data, size_t len)
{
	put_head(w, MAJOR_UINT, key);
	put_head(w, MAJOR_BYTES, len);
	put(w, data, len);
}

void tg_write_bool(struct tg_writer *w, uint32_t key, bool value)
{
	uint8_t simple = (MAJOR_SIMPLE << 5) | (value ? 21 : 20);

	put_head(w, MAJOR_UINT, key);
	put(w, &simple, 1);
}

uint8_t *tg_write_bytes_reserve(struct tg_writer *w, uint32_t key, size_t len)
{
	put_head(w, MAJOR_UINT, key);
	put_head(w, MAJOR_BYTES, len);
	if (w->overflow || len > w->cap - w->len) {
		w->overflow = true;
		return NULL;
	}
	w->len += len;
	return &w->buf[w->len - len];
}

void tg_write_text(struct tg_writer *w, uint32_t key, const char *text)
{
	size_t len = strlen(text);

	put_head(w, MAJOR_UINT, key);
	put_head(w, MAJOR_TEXT, len);
	put(w, text, len);
}

void tg_write_map(struct tg_writer *w, uint32_t key, uint32_t pairs)
{
	put_head(w, MAJOR_UINT, key);
	put_head(w, MAJOR_MAP, pairs);
}

/* A value that can pass 32 bits, such as an uptime in milliseconds, which does after 49 days. */
static void put_head64(struct tg_writer *w, uint8_t major, uint64_t value)
{
	uint8_t out[9];

	if (value <= UINT32_MAX) {
		put_head(w, major, (uint32_t)value);
		return;
	}
	out[0] = (major << 5) | 27;
	for (int i = 0; i < 8; i++) {
		out[1 + i] = (uint8_t)(value >> (56 - 8 * i));
	}
	put(w, out, sizeof(out));
}

void tg_write_uint64(struct tg_writer *w, uint32_t key, uint64_t value)
{
	put_head(w, MAJOR_UINT, key);
	put_head64(w, MAJOR_UINT, value);
}

void tg_write_item_uint64(struct tg_writer *w, uint64_t value)
{
	put_head64(w, MAJOR_UINT, value);
}

void tg_write_item_array(struct tg_writer *w, uint32_t items)
{
	put_head(w, MAJOR_ARRAY, items);
}

size_t tg_uint64_size(uint64_t value)
{
	return value <= UINT32_MAX ? tg_uint_size((uint32_t)value) : 9;
}

void tg_write_array(struct tg_writer *w, uint32_t key, uint32_t items)
{
	put_head(w, MAJOR_UINT, key);
	put_head(w, MAJOR_ARRAY, items);
}

void tg_write_item_text(struct tg_writer *w, const char *text)
{
	tg_write_item_textn(w, text, strlen(text));
}

void tg_write_item_textn(struct tg_writer *w, const char *text, size_t len)
{
	put_head(w, MAJOR_TEXT, len);
	put(w, text, len);
}

/* CBOR writes a negative integer n as the unsigned integer -1 - n, which is n with every bit turned over. */
void tg_write_item_int(struct tg_writer *w, int32_t value)
{
	if (value < 0) {
		put_head(w, MAJOR_NEG, ~(uint32_t)value);
	} else {
		put_head(w, MAJOR_UINT, value);
	}
}

size_t tg_uint_size(uint32_t value)
{
	return value < 24 ? 1 : value <= 0xff ? 2 : value <= 0xffff ? 3 : 5;
}

size_t tg_int_size(int32_t value)
{
	return tg_uint_size(value < 0 ? ~(uint32_t)value : (uint32_t)value);
}

struct reader {
	const uint8_t *data;
	size_t len;
	size_t pos;
};

static int get_head(struct reader *r, uint8_t *major, uint64_t *value)
{
	uint8_t first, info;
	size_t size;
	uint64_t v = 0;

	if (r->pos >= r->len) {
		return -EBADMSG;
	}
	first = r->data[r->pos++];
	*major = first >> 5;
	info = first & 0x1f;
	if (info < 24) {
		*value = info;
		return 0;
	}
	if (info > 27) {
		return -EBADMSG;
	}
	size = 1U << (info - 24);
	if (size > r->len - r->pos) {
		return -EBADMSG;
	}
	for (size_t i = 0; i < size; i++) {
		v = (v << 8) | r->data[r->pos++];
	}
	/* Each value has one valid encoding. */
	if ((size == 1 && v < 24) || (size > 1 && v < (1ULL << (4 * size)))) {
		return -EBADMSG;
	}
	*value = v;
	return 0;
}

static bool valid_utf8(const uint8_t *s, size_t len)
{
	for (size_t i = 0; i < len;) {
		uint8_t c = s[i];
		size_t extra;
		uint32_t cp;

		if (c < 0x80) {
			i++;
			continue;
		} else if ((c & 0xe0) == 0xc0) {
			extra = 1;
			cp = c & 0x1f;
		} else if ((c & 0xf0) == 0xe0) {
			extra = 2;
			cp = c & 0x0f;
		} else if ((c & 0xf8) == 0xf0) {
			extra = 3;
			cp = c & 0x07;
		} else {
			return false;
		}
		if (extra >= len - i) {
			return false;
		}
		for (size_t k = 1; k <= extra; k++) {
			if ((s[i + k] & 0xc0) != 0x80) {
				return false;
			}
			cp = (cp << 6) | (s[i + k] & 0x3f);
		}
		/* Rejects overlong forms, surrogates and values past the end of Unicode. */
		if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) || cp > 0x10ffff ||
		    (cp >= 0xd800 && cp <= 0xdfff)) {
			return false;
		}
		i += extra + 1;
	}
	return true;
}

/* Recursion is bounded by MAX_DEPTH, so stack use is too. */
static int skip_item(struct reader *r, int depth)
{
	uint8_t major;
	uint64_t value;
	int rc;

	if (depth > MAX_DEPTH) {
		return -EBADMSG;
	}
	rc = get_head(r, &major, &value);
	if (rc != 0) {
		return rc;
	}
	switch (major) {
	case MAJOR_UINT:
	case MAJOR_NEG:
		return 0;
	case MAJOR_BYTES:
	case MAJOR_TEXT:
		if (value > r->len - r->pos) {
			return -EBADMSG;
		}
		if (major == MAJOR_TEXT && !valid_utf8(&r->data[r->pos], value)) {
			return -EBADMSG;
		}
		r->pos += value;
		return 0;
	case MAJOR_ARRAY:
		if (value > MAX_ITEMS) {
			return -EBADMSG;
		}
		for (uint64_t i = 0; i < value; i++) {
			rc = skip_item(r, depth + 1);
			if (rc != 0) {
				return rc;
			}
		}
		return 0;
	case MAJOR_MAP: {
		bool first = true;
		uint64_t last = 0;

		if (value > MAX_ITEMS) {
			return -EBADMSG;
		}
		for (uint64_t i = 0; i < value; i++) {
			uint8_t key_major;
			uint64_t key;

			rc = get_head(r, &key_major, &key);
			if (rc != 0) {
				return rc;
			}
			if (key_major != MAJOR_UINT || (!first && key <= last)) {
				return -EBADMSG;
			}
			first = false;
			last = key;
			rc = skip_item(r, depth + 1);
			if (rc != 0) {
				return rc;
			}
		}
		return 0;
	}
	case MAJOR_SIMPLE:
		/* false, true and null, but no floats or other simple values. */
		return (value >= 20 && value <= 22) ? 0 : -EBADMSG;
	default:
		return -EBADMSG;
	}
}

int tg_read_message(const uint8_t *data, size_t len, struct tg_message *msg)
{
	struct reader r = {.data = data, .len = len};
	struct reader whole = {.data = data, .len = len};
	uint8_t major;
	uint64_t count, version, type, id;
	int rc;

	if (len > MAX_MESSAGE) {
		return -EBADMSG;
	}
	/* First the whole message, so nothing below can read past its end. */
	rc = skip_item(&whole, 0);
	if (rc != 0 || whole.pos != len) {
		return -EBADMSG;
	}
	if (get_head(&r, &major, &count) != 0 || major != MAJOR_ARRAY || count != 4) {
		return -EBADMSG;
	}
	if (get_head(&r, &major, &version) != 0 || major != MAJOR_UINT || version == 0) {
		return -EBADMSG;
	}
	if (get_head(&r, &major, &type) != 0 || major != MAJOR_UINT || type > UINT32_MAX) {
		return -EBADMSG;
	}
	if (get_head(&r, &major, &id) != 0 || major != MAJOR_UINT || id > UINT32_MAX) {
		return -EBADMSG;
	}
	if (get_head(&r, &major, &count) != 0 || major != MAJOR_MAP) {
		return -EBADMSG;
	}
	if (version != 1) {
		return -ENOTSUP;
	}
	msg->type = type;
	msg->id = id;
	msg->fields = count;
	msg->body = &data[r.pos];
	msg->body_len = len - r.pos;
	return 0;
}

static bool find(const struct tg_message *msg, uint32_t key, uint8_t *major, uint64_t *value, struct reader *r)
{
	r->data = msg->body;
	r->len = msg->body_len;
	r->pos = 0;
	for (uint32_t i = 0; i < msg->fields; i++) {
		uint64_t k;
		uint8_t m;

		if (get_head(r, &m, &k) != 0) {
			return false;
		}
		if (k == key) {
			return get_head(r, major, value) == 0;
		}
		if (skip_item(r, 1) != 0) {
			return false;
		}
	}
	return false;
}

bool tg_read_uint(const struct tg_message *msg, uint32_t key, uint32_t *value)
{
	struct reader r;
	uint8_t major;
	uint64_t v;

	if (!find(msg, key, &major, &v, &r) || major != MAJOR_UINT || v > UINT32_MAX) {
		return false;
	}
	*value = v;
	return true;
}

bool tg_read_bytes(const struct tg_message *msg, uint32_t key, const uint8_t **data, size_t *len)
{
	struct reader r;
	uint8_t major;
	uint64_t v;

	if (!find(msg, key, &major, &v, &r) || major != MAJOR_BYTES) {
		return false;
	}
	*data = &r.data[r.pos];
	*len = v;
	return true;
}

bool tg_read_text(const struct tg_message *msg, uint32_t key, const char **text, size_t *len)
{
	struct reader r;
	uint8_t major;
	uint64_t v;

	if (!find(msg, key, &major, &v, &r) || major != MAJOR_TEXT) {
		return false;
	}
	*text = (const char *)&r.data[r.pos];
	*len = v;
	return true;
}

bool tg_read_bool(const struct tg_message *msg, uint32_t key, bool *value)
{
	struct reader r;
	uint8_t major;
	uint64_t v;

	if (!find(msg, key, &major, &v, &r) || major != MAJOR_SIMPLE || (v != 20 && v != 21)) {
		return false;
	}
	*value = v == 21;
	return true;
}
