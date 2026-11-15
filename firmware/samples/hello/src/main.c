/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/printk.h>

#ifdef CONFIG_TRACEGALE
#include <tracegale/version.h>
#endif

int main(void)
{
#ifdef CONFIG_TRACEGALE
	printk("Tracegale version: %s\n", tracegale_version());
#else
	/* Used by the size comparison, which builds with Tracegale off. */
	printk("Tracegale disabled\n");
#endif

	return 0;
}
