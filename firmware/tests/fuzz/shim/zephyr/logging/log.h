/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Logging does nothing when fuzzing.
 */

#ifndef TG_FUZZ_SHIM_LOG_H_
#define TG_FUZZ_SHIM_LOG_H_

#define LOG_MODULE_DECLARE(...)
#define LOG_ERR(...) ((void)0)
#define LOG_WRN(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)

#endif /* TG_FUZZ_SHIM_LOG_H_ */
