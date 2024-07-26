/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_VIDC_H_
#define _IRIS_VIDC_H_

struct iris_core;

void iris_init_ops(struct iris_core *core);
int iris_open(struct file *filp);
int iris_close(struct file *filp);

#endif
