/* SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering */
/* SPDX-License-Identifier: Apache-2.0 */

/* Keeps the GNU build ID note in flash, where the firmware can read it. */
SECTION_PROLOGUE(.tg_build_id,,)
{
	__tg_build_id_start = .;
	KEEP(*(.note.gnu.build-id))
	__tg_build_id_end = .;
} GROUP_LINK_IN(ROMABLE_REGION)
