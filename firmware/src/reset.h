/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TG_RESET_H_
#define TG_RESET_H_

#include <stdint.h>

/** Maps Zephyr's reset cause bits to the values in docs/protocol/README.md. */
uint32_t tg_reset_reason_from_cause(uint32_t cause);

/** Returns why the device last reset, which is read once per boot. */
uint32_t tg_reset_reason(void);

#endif /* TG_RESET_H_ */
