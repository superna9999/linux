// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_power.h"
#include "iris_vpu_common.h"

void iris_power_off(struct iris_core *core)
{
	if (!core->power_enabled)
		return;

	iris_vpu_power_off(core);
	core->power_enabled = false;
}

int iris_power_on(struct iris_core *core)
{
	int ret;

	if (core->power_enabled)
		return 0;

	ret = iris_vpu_power_on(core);
	if (ret) {
		dev_err(core->dev, "failed to power on, err: %d\n", ret);
		return ret;
	}

	core->power_enabled = true;

	return ret;
}
