/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_VPU_BUFFER_H_
#define _IRIS_VPU_BUFFER_H_

struct iris_inst;

#define MIN_BUFFERS			4

u32 iris_vpu_dec_dpb_size(struct iris_inst *inst);
int iris_vpu_buf_count(struct iris_inst *inst, enum iris_buffer_type buffer_type);

#endif
