/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_POWER_H_
#define _IRIS_POWER_H_

struct iris_inst;

int iris_scale_power(struct iris_inst *inst);
int iris_power_on(struct iris_core *core);
void iris_power_off(struct iris_core *core);

#endif
