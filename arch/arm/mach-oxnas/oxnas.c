/*
 * Copyright (C) 2016 Neil Armstrong <narmstrong@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <asm/mach/arch.h>
#include <linux/of.h>
#include <linux/of_platform.h>

static const char * const oxnas_dt_compat[] __initconst = {
	"plxtech,ox810se",
	NULL,
};

static void __init oxnas_init(void)
{
	pr_info("OXNAS Device Tree boot\n");

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

DT_MACHINE_START(OXNAS, "PLX Technology OXNAS Family")
	.dt_compat	= oxnas_dt_compat,
	.init_machine	= oxnas_init,
MACHINE_END
