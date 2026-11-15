/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reads the GNU build ID note that build_id.ld keeps in flash. A note is a 4-byte name size, a 4-byte data size and a
 * 4-byte type, then the name "GNU" and the ID, all in the target's byte order.
 */

#include <string.h>

#include "build_id.h"

#define NT_GNU_BUILD_ID 3

extern const uint8_t __tg_build_id_start[];
extern const uint8_t __tg_build_id_end[];

size_t tg_build_id(const uint8_t **id)
{
	uint32_t name_size, size, type;
	size_t available = __tg_build_id_end - __tg_build_id_start;

	*id = NULL;
	if (available < 16) {
		return 0;
	}
	memcpy(&name_size, &__tg_build_id_start[0], 4);
	memcpy(&size, &__tg_build_id_start[4], 4);
	memcpy(&type, &__tg_build_id_start[8], 4);
	if (name_size != 4 || type != NT_GNU_BUILD_ID || memcmp(&__tg_build_id_start[12], "GNU", 4) != 0 ||
	    size == 0 || size > TG_BUILD_ID_MAX || size > available - 16) {
		return 0;
	}
	*id = &__tg_build_id_start[16];
	return size;
}
