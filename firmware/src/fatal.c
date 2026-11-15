/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Restarts after a fatal error. Zephyr has already written the coredump by the time this runs, and a restart doesn't clear
 * Tracegale's RAM store, so the crash is sent once the device is back.
 */

#include <zephyr/fatal.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_DECLARE(tracegale, CONFIG_TRACEGALE_LOG_LEVEL);

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);
	LOG_PANIC();
	LOG_ERR("Fatal error %u: restarting, and the crash will be sent to the server", reason);
	sys_reboot(SYS_REBOOT_WARM);
	CODE_UNREACHABLE;
}
