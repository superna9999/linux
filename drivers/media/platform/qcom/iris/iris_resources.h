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

int iris_init_resources(struct iris_core *core);

#endif
