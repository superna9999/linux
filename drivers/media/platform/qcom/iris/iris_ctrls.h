/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_CTRLS_H_
#define _IRIS_CTRLS_H_

struct iris_core;
struct iris_inst;

int iris_ctrls_init(struct iris_inst *inst);
int iris_session_init_caps(struct iris_core *core);

#endif
