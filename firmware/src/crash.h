/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Crash reports: a coredump kept over a reboot, then uploaded with XFER_START and XFER_CHUNK.
 */

#ifndef TG_CRASH_H_
#define TG_CRASH_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include "transport.h"

/* Set in the store when the dump didn't fit, so some memory was left out. */
#define TG_CRASH_FLAG_CUT BIT(0)
/* Set when Zephyr's output wasn't a dump this code understands. */
#define TG_CRASH_FLAG_UNREADABLE BIT(1)

/** @return the size of a stored crash waiting to be sent, or 0. */
size_t tg_crash_pending(void);

/**
 * Sends a stored crash on an open connection, and erases it once the server has all of it.
 *
 * @param buf a buffer for messages, and its size.
 * @param next_id gives each message a new ID.
 * @return 0 if sent or if nothing was stored, or a negative tracegale_error. *detail is a server error code when the
 *         server refused it.
 */
int tg_crash_send(const struct tg_transport *transport, uint8_t *buf, size_t cap, uint32_t (*next_id)(void),
		  int *detail);

/** The build ID recorded with the stored crash. @return its length, or 0. */
size_t tg_crash_stored_build_id(uint8_t *out, size_t cap);
uint8_t tg_crash_stored_flags(void);

#if defined(CONFIG_TRACEGALE_CRASH_KEEP)
/**
 * A copy of an unsent crash in settings storage, so that it survives a power loss. All of these run on a thread, never while
 * the device is crashing, and need settings to be initialised.
 */

/** At boot, after settings are ready: brings back a kept crash if RAM has none. */
void tg_crash_kept_load(void);

/** After a failed attempt: keeps the crash in RAM, unless an older one is already kept. Tries once for each crash. */
void tg_crash_keep(void);

/** After the crash in RAM was sent or given up: deletes its kept copy, or brings back an older kept crash to send next. */
void tg_crash_kept_next(void);

/** Deletes the kept copy. @return 0, or a negative errno. */
int tg_crash_kept_forget(void);
#endif

#endif /* TG_CRASH_H_ */
