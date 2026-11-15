/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Just enough of Zephyr's kernel API for firmware parsers to build on the host for fuzzing. Time never runs out here, so the
 * fuzzer controls how a connection ends, not a clock.
 */

#ifndef TG_FUZZ_SHIM_KERNEL_H_
#define TG_FUZZ_SHIM_KERNEL_H_

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#define CONFIG_TRACEGALE_TIMEOUT_MS 30000
#define CONFIG_TRACEGALE_LOG_LEVEL  0

typedef struct {
	int64_t unused;
} k_timepoint_t;
typedef struct {
	int64_t ticks;
} k_timeout_t;

#define K_MSEC(ms)                         ((k_timeout_t){.ticks = (ms)})
#define sys_timepoint_calc(timeout)        ((k_timepoint_t){.unused = (timeout).ticks})
#define sys_timepoint_expired(point)       ((void)(point), 0)
#define sys_timepoint_timeout(point)       ((void)(point), (k_timeout_t){.ticks = 1000})
#define k_ticks_to_ms_ceil64(ticks)        ((int64_t)(ticks))
#define snprintk                           snprintf
#ifndef MIN
#define MIN(a, b)                          ((a) < (b) ? (a) : (b))
#endif

#endif /* TG_FUZZ_SHIM_KERNEL_H_ */
