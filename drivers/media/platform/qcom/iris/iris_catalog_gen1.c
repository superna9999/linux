// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_ctrls.h"
#include "iris_platform_common.h"
#include "iris_resources.h"
#include "iris_hfi_gen1.h"
#include "iris_hfi_gen1_defines.h"
#include "iris_vpu_common.h"

/* Common SM8250 & variants */
static struct platform_inst_fw_cap inst_fw_cap_sm8250[] = {
	{
		.cap_id = PIPE,
		.min = PIPE_1,
		.max = PIPE_4,
		.step_or_mask = 1,
		.value = PIPE_4,
		.hfi_id = HFI_PROPERTY_PARAM_WORK_ROUTE,
		.set = iris_set_pipe,
	},
	{
		.cap_id = STAGE,
		.min = STAGE_1,
		.max = STAGE_2,
		.step_or_mask = 1,
		.value = STAGE_2,
		.hfi_id = HFI_PROPERTY_PARAM_WORK_MODE,
		.set = iris_set_stage,
	},
	{
		.cap_id = DEBLOCK,
		.min = 0,
		.max = 1,
		.step_or_mask = 1,
		.value = 0,
		.hfi_id = HFI_PROPERTY_CONFIG_VDEC_POST_LOOP_DEBLOCKER,
		.set = iris_set_u32,
	},
};

static struct platform_inst_caps platform_inst_cap_sm8250 = {
	.min_frame_width = 128,
	.max_frame_width = 8192,
	.min_frame_height = 128,
	.max_frame_height = 8192,
	.max_mbpf = 138240,
	.mb_cycles_vsp = 25,
	.mb_cycles_vpp = 200,
};

static struct tz_cp_config tz_cp_config_sm8250 = {
	.cp_start = 0,
	.cp_size = 0x25800000,
	.cp_nonpixel_start = 0x01000000,
	.cp_nonpixel_size = 0x24800000,
};

static const u32 sm8250_vdec_input_config_param_default[] = {
	HFI_PROPERTY_CONFIG_VIDEOCORES_USAGE,
	HFI_PROPERTY_PARAM_UNCOMPRESSED_FORMAT_SELECT,
	HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO,
	HFI_PROPERTY_PARAM_BUFFER_COUNT_ACTUAL,
	HFI_PROPERTY_PARAM_VDEC_MULTI_STREAM,
	HFI_PROPERTY_PARAM_FRAME_SIZE,
	HFI_PROPERTY_PARAM_BUFFER_SIZE_ACTUAL,
	HFI_PROPERTY_PARAM_BUFFER_ALLOC_MODE,
};

static const u32 sm8250_dec_ip_int_buf_tbl[] = {
	BUF_BIN,
	BUF_SCRATCH_1,
};

static const u32 sm8250_dec_op_int_buf_tbl[] = {
	BUF_DPB,
};

/* platforms catalogs */
#include "iris_catalog_sm8250.h"
