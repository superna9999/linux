// SPDX-License-Identifier: GPL-2.0
/*
 * SH Pin Function Control Initialization
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#include <cpu/pfc.h>

const struct software_node pfc_gpiochip_node = {
	.name = "sh-pfc",
};

int __init sh_pfc_register(const char *name,
			   struct resource *resource, u32 num_resources)
{
	struct platform_device_info pdev_info = {
		.name		= name,
		.id		= PLATFORM_DEVID_NONE,
		.res		= resource,
		.num_res	= num_resources,
		.swnode		= &pfc_gpiochip_node,
	};
	struct platform_device *pdev;

	pdev = platform_device_register_full(&pdev_info);
	return PTR_ERR_OR_ZERO(pdev);
}
