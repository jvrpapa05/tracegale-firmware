/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * The GNU build ID the linker puts in this image, which the server uses to find the matching symbol file.
 */

#ifndef TG_BUILD_ID_H_
#define TG_BUILD_ID_H_

#include <stddef.h>
#include <stdint.h>

#define TG_BUILD_ID_MAX 32

/** Points *id at this image's build ID. @return its length, or 0 if the image has none. */
size_t tg_build_id(const uint8_t **id);

#endif /* TG_BUILD_ID_H_ */
