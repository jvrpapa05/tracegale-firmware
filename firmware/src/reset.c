/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>

#include "reset.h"

/* The most specific cause comes first, because hardware can set several bits at once. */
uint32_t tg_reset_reason_from_cause(uint32_t cause)
{
	static const struct {
		uint32_t bit;
		uint32_t reason;
	} table[] = {
		{RESET_WATCHDOG, 4}, {RESET_CPU_LOCKUP, 7}, {RESET_BROWNOUT, 5},  {RESET_SOFTWARE, 3}, {RESET_DEBUG, 6},
		{RESET_PIN, 2},      {RESET_POR, 1},        {RESET_LOW_POWER_WAKE, 8},
	};

	for (size_t i = 0; i < ARRAY_SIZE(table); i++) {
		if (cause & table[i].bit) {
			return table[i].reason;
		}
	}
	return 0;
}

uint32_t tg_reset_reason(void)
{
	static bool read;
	static uint32_t reason;

	if (!read) {
		uint32_t cause = 0;

		read = true;
		/* Boards without support report this as an error, and the reason stays 0, meaning unknown. */
		if (hwinfo_get_reset_cause(&cause) == 0) {
			reason = tg_reset_reason_from_cause(cause);
			(void)hwinfo_clear_reset_cause();
		}
	}
	return reason;
}
