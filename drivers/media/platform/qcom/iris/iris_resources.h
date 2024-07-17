/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_RESOURCES_H_
#define _IRIS_RESOURCES_H_

struct iris_core;

struct icc_info {
	const char		*name;
	u32			bw_min_kbps;
	u32			bw_max_kbps;
};

int iris_enable_power_domains(struct iris_core *core, struct device *pd_dev);
int iris_disable_power_domains(struct iris_core *core, struct device *pd_dev);
int iris_unset_icc_bw(struct iris_core *core);
int iris_set_icc_bw(struct iris_core *core, unsigned long icc_bw);
int iris_reset_ahb2axi_bridge(struct iris_core *core);
int iris_opp_set_rate(struct iris_core *core, u64 freq);
int iris_disable_unprepare_clock(struct iris_core *core, enum platform_clk_type clk_type);
int iris_prepare_enable_clock(struct iris_core *core, enum platform_clk_type clk_type);
int iris_init_resources(struct iris_core *core);

#endif
