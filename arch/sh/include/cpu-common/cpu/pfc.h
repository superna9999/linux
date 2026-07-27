/* SPDX-License-Identifier: GPL-2.0
 *
 * SH Pin Function Control Initialization
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
 */

#ifndef __ARCH_SH_CPU_PFC_H__
#define __ARCH_SH_CPU_PFC_H__

#include <linux/types.h>

struct resource;
struct software_node;

extern const struct software_node pfc_gpiochip_node;

int sh_pfc_register(const char *name,
		    struct resource *resource, u32 num_resources);

#endif /* __ARCH_SH_CPU_PFC_H__ */
