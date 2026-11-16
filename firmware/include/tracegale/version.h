/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TRACEGALE_VERSION_H_
#define TRACEGALE_VERSION_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Set by scripts/release.sh, the same as the server's and the tools'. */
#define TRACEGALE_VERSION_STRING "1.0.0-rc.1"

/**
 * @brief Get the version of the Tracegale firmware stack.
 *
 * @return Version as a null-terminated string with static storage.
 */
const char *tracegale_version(void);

#ifdef __cplusplus
}
#endif

#endif /* TRACEGALE_VERSION_H_ */
