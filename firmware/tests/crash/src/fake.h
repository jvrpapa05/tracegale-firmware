/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * What the crash tests share: a dump builder and a fake server, both in main.c.
 */

#ifndef TG_TEST_FAKE_H_
#define TG_TEST_FAKE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "build_id.h"
#include "sha256.h"

#if defined(CONFIG_TRACEGALE_CRASH_RAM)
#define STORE_SIZE CONFIG_TRACEGALE_CRASH_RAM_SIZE
#else
/* Room for a dump from one of Zephyr's own backends, which aren't limited to Tracegale's store. */
#define STORE_SIZE 8192
#endif
/* The memory block the store adds to every dump, holding its crash counter and cycle count. */
#define MARK (3 + 2 * sizeof(void *) + 8)

struct fake_server {
	uint8_t data[STORE_SIZE];
	size_t have;
	size_t total;
	uint8_t sha[TG_SHA256_SIZE];
	uint8_t build_id[TG_BUILD_ID_MAX];
	size_t build_id_len;
	bool started;
	bool complete;
	uint32_t chunk_max;
	int starts;
	int chunks;
	int bytes_received;
	int exchanges;
	/* Uploads the server got all of. */
	int completed;
	/* What to do wrong. */
	int fail_exchange_at;
	/* Handles the message, then loses the reply. */
	int lose_reply_at;
	uint32_t error_on_start;
	bool forget_once;
	bool stall;
	bool garbage;
	/* The device can't connect at all. */
	bool offline;
};

extern struct fake_server srv;

/* Starts the server again, with nothing received and nothing going wrong. */
void fake_server_reset(void);

/* Sends the stored crash to the server. @return 0, a negative tracegale_error, or -1000 minus the server's error code. */
int send_crash(void);

/* The rest is for Tracegale's own store, in main.c. */
extern uint8_t dump[2048];
extern size_t dump_len;

/* Build a dump in dump[]: the file header, a block of registers or thread data, and a block of memory. */
void dump_begin(void);
void dump_block(char id, size_t len);
void dump_memory(uint32_t start, size_t len);

/* Hands the dump to the store in pieces of this size, as Zephyr's coredump code would. */
void feed(size_t piece);

/* Copies the stored dump out. @return its size, or 0. */
size_t stored(uint8_t *out, size_t cap);

/* Stores a dump of a few hundred bytes. */
void store_dump(void);

/* Empties the store and the server. */
void before_upload(void *fixture);

/* Damages the store, as a fault in RAM or a power loss would. */
void tg_crash_ram_damage(void);

#endif /* TG_TEST_FAKE_H_ */
