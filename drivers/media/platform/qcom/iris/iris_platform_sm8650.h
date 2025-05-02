/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_PLATFORM_SM8650_H__
#define __IRIS_PLATFORM_SM8650_H__

static const char * const sm8650_clk_reset_table[] = { "bus", "core" };

static const char * const sm8650_controller_reset_table[] = { "xo" };

static void iris_set_sm8650_preset_registers(struct iris_core *core)
{
	writel(0x0, core->reg_base + 0xB0088);
	writel(0x33332222, core->reg_base + 0x13030);
	writel(0x44444444, core->reg_base + 0x13034);
	writel(0x1022, core->reg_base + 0x13038);
	writel(0x0, core->reg_base + 0x13040);
	writel(0xFFFF, core->reg_base + 0x13048);
	writel(0x33332222, core->reg_base + 0x13430);
	writel(0x44444444, core->reg_base + 0x13434);
	writel(0x1022, core->reg_base + 0x13438);
	writel(0x0, core->reg_base + 0x13440);
	writel(0xFFFF, core->reg_base + 0x13448);
	writel(0x99, core->reg_base + 0xA013C);
}

#endif
