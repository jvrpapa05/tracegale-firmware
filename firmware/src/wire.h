/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * The message format from docs/protocol/README.md. Everything received is untrusted.
 */

#ifndef TG_WIRE_H_
#define TG_WIRE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum tg_type {
	TG_HELLO = 1,
	TG_ACK = 2,
	TG_ERROR = 3,
	TG_METRICS = 4,
	TG_LOGS = 5,
	TG_XFER_START = 10,
	TG_XFER_CHUNK = 11,
	TG_XFER_STATUS = 12,
	TG_IMG_CHECK = 20,
	TG_IMG_OFFER = 21,
	TG_IMG_GET = 22,
	TG_IMG_DATA = 23,
	TG_IMG_RESULT = 24,
};

/* Bits of ACK's "pending" field. */
#define TG_PENDING_UPDATE 0x1U

/* Error codes the device acts on. */
enum tg_wire_error {
	TG_WIRE_ERR_TOO_LARGE = 5,
	TG_WIRE_ERR_BAD_OFFSET = 6,
	TG_WIRE_ERR_HASH_MISMATCH = 7,
	TG_WIRE_ERR_UNKNOWN_TRANSFER = 8,
	TG_WIRE_ERR_UNKNOWN_IMAGE = 9,
	TG_WIRE_ERR_RATE_LIMITED = 10,
	TG_WIRE_ERR_NOT_AUTHORIZED = 11,
	TG_WIRE_ERR_SERVER_BUSY = 12,
	TG_WIRE_ERR_UNSUPPORTED_KIND = 13,
};

/* XFER_START kinds. */
#define TG_XFER_KIND_COREDUMP 1

struct tg_writer {
	uint8_t *buf;
	size_t cap;
	size_t len;
	bool overflow;
};

/** Starts a message with the given number of body fields. Fields must then be added in ascending key order. */
void tg_write_begin(struct tg_writer *w, uint8_t *buf, size_t cap, uint32_t type, uint32_t id, uint32_t fields);
void tg_write_uint(struct tg_writer *w, uint32_t key, uint32_t value);
void tg_write_bytes(struct tg_writer *w, uint32_t key, const uint8_t *data, size_t len);
void tg_write_text(struct tg_writer *w, uint32_t key, const char *text);
void tg_write_bool(struct tg_writer *w, uint32_t key, bool value);
/** Adds a byte string of this length, and returns where its contents go, or NULL if it doesn't fit. */
uint8_t *tg_write_bytes_reserve(struct tg_writer *w, uint32_t key, size_t len);
/** Starts a field that's a map of this many pairs, each then added with tg_write_uint(), in ascending key order. */
void tg_write_map(struct tg_writer *w, uint32_t key, uint32_t pairs);
/** Starts a field that's an array of this many items, each then added with tg_write_item_...(). */
void tg_write_array(struct tg_writer *w, uint32_t key, uint32_t items);
void tg_write_item_text(struct tg_writer *w, const char *text);
void tg_write_item_textn(struct tg_writer *w, const char *text, size_t len);
void tg_write_item_int(struct tg_writer *w, int32_t value);
/* 64-bit values, and an array inside an array. */
void tg_write_uint64(struct tg_writer *w, uint32_t key, uint64_t value);
void tg_write_item_uint64(struct tg_writer *w, uint64_t value);
void tg_write_item_array(struct tg_writer *w, uint32_t items);
size_t tg_uint64_size(uint64_t value);
/** @return the bytes that an unsigned or a signed integer takes in a message. */
size_t tg_uint_size(uint32_t value);
size_t tg_int_size(int32_t value);

struct tg_message {
	uint32_t type;
	uint32_t id;
	const uint8_t *body;
	size_t body_len;
	uint32_t fields;
};

/**
 * Checks a whole message against the specification's strict rules.
 *
 * @return 0, or -EBADMSG if it breaks any rule, or -ENOTSUP if only its version is different.
 */
int tg_read_message(const uint8_t *data, size_t len, struct tg_message *msg);

/** Finds a field in a checked message. @return true if the field exists and has this type. */
bool tg_read_uint(const struct tg_message *msg, uint32_t key, uint32_t *value);
bool tg_read_bytes(const struct tg_message *msg, uint32_t key, const uint8_t **data, size_t *len);
bool tg_read_bool(const struct tg_message *msg, uint32_t key, bool *value);

#endif /* TG_WIRE_H_ */
