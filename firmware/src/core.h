/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TG_CORE_H_
#define TG_CORE_H_

#include <stdint.h>

/** Makes one attempt: connect, HELLO, wait for the reply, close. @return 0, or a negative tracegale_error. */
int tg_core_attempt(void);

/** The application's firmware version, from its VERSION file, as sent to the server, or "unknown". */
const char *tg_core_fw_version(void);

/** Seconds to wait after a number of failures in a row, with jitter between 75% and 125%. */
uint32_t tg_core_backoff_s(uint32_t failures, uint32_t random);

#endif /* TG_CORE_H_ */
