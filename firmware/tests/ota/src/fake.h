/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * What the update tests share: a fake server, a stand-in for MCUboot, and images to offer.
 */

#ifndef TG_TEST_FAKE_H_
#define TG_TEST_FAKE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/storage/flash_map.h>

#include <tracegale/tracegale.h>

#include "compat.h"
#include "sha256.h"

#define SLOT0 TG_PARTITION_ID(slot0_partition)
#define SLOT1 TG_PARTITION_ID(slot1_partition)
#define PAGE 4096
/* The most image data one IMG_GET asks for with these tests' message size: whole write blocks under 256 - 25. */
#define BLOCK 224
/* Results the server remembers. */
#define RESULTS 8

struct fake_server {
	/* What it has for the device. */
	bool pending;
	bool offer;
	uint32_t image_ref;
	uint32_t kind;
	const uint8_t *image;
	uint32_t total_len;
	uint8_t sha[TG_SHA256_SIZE];
	/* The most data it puts in one IMG_DATA. */
	uint32_t chunk_max;

	/* What goes wrong. */
	bool offline;
	/* The connection drops at this IMG_GET or IMG_RESULT, counted from 1. */
	int drop_get_at;
	int drop_result_at;
	/* Error codes it answers with, or 0. */
	uint32_t error_on_check;
	uint32_t error_on_get;
	uint32_t error_on_result;
	/* A byte of the image that's sent wrong, or -1. */
	int corrupt_at;
	bool wrong_ref;
	bool wrong_offset;
	/* Called at each IMG_GET, before it's answered. */
	void (*on_get)(void);

	/* What it saw. */
	int hellos;
	int checks;
	int gets;
	int results;
	uint32_t bytes_sent;
	/* The offset of the first IMG_GET after the last IMG_CHECK. */
	uint32_t first_offset;
	bool first_seen;
	uint32_t result_ref[RESULTS];
	uint32_t result_outcome[RESULTS];
};

extern struct fake_server srv;

/* Starts the server again: nothing to offer, nothing seen and nothing going wrong. */
void fake_server_reset(void);

/* Has the server announce and offer this image. */
void offer(const uint8_t *image, size_t len, uint32_t image_ref);

/* The last outcome the device reported. */
uint32_t last_outcome(void);

/* How often Tracegale asked for a restart, and why. The device doesn't restart by itself in these tests. */
struct restarts {
	int install;
	int revert;
};

extern struct restarts restarts;

/* Where MCUboot's mode in this build expects an image in the second slot. */
size_t image_start(void);

/* How much image fits the second slot. */
size_t slot_room(void);

/* The stand-in for MCUboot refuses the next image, as it would with a wrong signature. */
extern bool bootloader_refuses;

/* A restart: MCUboot installs a requested image, or brings the previous one back if the new one wasn't confirmed. Then
 * Tracegale starts.
 */
void device_restart(void);

/* Puts the device back as new: this image in the first slot, an empty second slot, and no record of any update. */
void device_reset(const uint8_t *running, size_t len);

/* The largest image these tests use. The stand-in for MCUboot moves this much of a slot. */
#define IMAGE_MAX (4 * PAGE)

/* Fills an image: MCUboot's header with version 1.<minor>.0, then bytes that depend on the seed. */
void make_image(uint8_t *out, size_t len, uint8_t minor, uint32_t seed);

/* One whole check-in, as Tracegale's thread makes it. @return 0, or a negative tracegale_error. */
int checkin(void);

/* What tracegale_get_status() says about updates. */
enum tracegale_update_state update_state(void);
enum tracegale_update_result update_result(void);
uint32_t update_offset(void);

/* @return true if the slot holds exactly these bytes there. */
bool in_slot(uint8_t area, size_t at, const uint8_t *data, size_t len);

/* @return true if the slot is blank from there to its end. */
bool blank_from(uint8_t area, size_t at);

/* Writes bytes into a slot, at a multiple of 8. */
void scribble(uint8_t area, size_t at, const uint8_t *data, size_t len);

/* MCUboot's confirmation of the running image, as an application gives it. */
void confirm_running(void);

/* What Tracegale keeps in settings storage. A device in the field has this, so it must not change. */
struct record {
	uint8_t version;
	uint8_t phase;
	uint8_t outcome;
	uint8_t failures;
	uint8_t streak;
	uint8_t reserved[3];
	uint32_t image_ref;
	uint32_t total_len;
	uint8_t sha256[TG_SHA256_SIZE];
};

/* Reads the record. @return false when there's none. */
bool record_read(struct record *out);
void record_write(const void *in, size_t len);

#endif /* TG_TEST_FAKE_H_ */
