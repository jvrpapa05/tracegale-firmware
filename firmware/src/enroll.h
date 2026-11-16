/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Enrollment: a device built with an enrollment key's build file, and given its token, makes its own key pair and
 * asks the server for its certificate, so nothing is copied per device. See "Enrollment" in docs/firmware/README.md.
 */

#ifndef TG_ENROLL_H_
#define TG_ENROLL_H_

#include <stddef.h>
#include <stdint.h>

/** Loads what's stored. Called once by Tracegale's thread, after the store. */
void tg_enroll_load(void);

/** @return true if the device can enroll: the build has an enrollment key, a token is known, and it isn't provisioned. */
bool tg_enroll_wanted(void);

/**
 * One enrollment, through the transport, with buf as the message buffer. Called with the store's attempt lock held.
 * On success the device is provisioned, and its credentials stored.
 *
 * @param retry_s gets the server's time to wait, for TRACEGALE_ERR_WAITING_FOR_APPROVAL.
 * @param server_error gets the server's error code, for TRACEGALE_ERR_SERVER.
 * @return 0, or a negative tracegale_error, with *detail set if useful.
 */
int tg_enroll_run(uint8_t *buf, size_t cap, uint32_t id, uint32_t *retry_s, uint32_t *server_error, int *detail);

/** The serial number the device enrolls with, for the shell. */
const char *tg_enroll_serial(void);

/** Forgets the token and the key made for enrolling, when the device is erased. */
void tg_enroll_forget(void);

#endif /* TG_ENROLL_H_ */
