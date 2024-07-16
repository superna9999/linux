/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_HFI_GEN1_H_
#define _IRIS_HFI_GEN1_H_

struct iris_core;
struct iris_inst;

void iris_hfi_gen1_command_ops_init(struct iris_core *core);
void iris_hfi_gen1_response_ops_init(struct iris_core *core);
struct iris_inst *iris_hfi_gen1_get_instance(void);

#endif
