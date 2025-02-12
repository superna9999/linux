// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_ctrls.h"
#include "iris_hfi_gen2.h"
#include "iris_hfi_gen2_defines.h"
#include "iris_platform_common.h"
#include "iris_vpu_common.h"

/* Common SM8550 & variants */
static struct platform_inst_fw_cap inst_fw_cap_sm8550[] = {
	{
		.cap_id = PROFILE,
		.min = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE,
		.max = V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_HIGH) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH),
		.value = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
		.hfi_id = HFI_PROP_PROFILE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_u32_enum,
	},
	{
		.cap_id = LEVEL,
		.min = V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
		.max = V4L2_MPEG_VIDEO_H264_LEVEL_6_2,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1B) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_3) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_2_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_2_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_2_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_3_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_3_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_3_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_4_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_4_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_4_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_5_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_5_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_5_2) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_6_0) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_6_1) |
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_6_2),
		.value = V4L2_MPEG_VIDEO_H264_LEVEL_6_1,
		.hfi_id = HFI_PROP_LEVEL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_u32_enum,
	},
	{
		.cap_id = INPUT_BUF_HOST_MAX_COUNT,
		.min = DEFAULT_MAX_HOST_BUF_COUNT,
		.max = DEFAULT_MAX_HOST_BURST_BUF_COUNT,
		.step_or_mask = 1,
		.value = DEFAULT_MAX_HOST_BUF_COUNT,
		.hfi_id = HFI_PROP_BUFFER_HOST_MAX_COUNT,
		.flags = CAP_FLAG_INPUT_PORT,
		.set = iris_set_u32,
	},
	{
		.cap_id = STAGE,
		.min = STAGE_1,
		.max = STAGE_2,
		.step_or_mask = 1,
		.value = STAGE_2,
		.hfi_id = HFI_PROP_STAGE,
		.set = iris_set_stage,
	},
	{
		.cap_id = PIPE,
		.min = PIPE_1,
		.max = PIPE_4,
		.step_or_mask = 1,
		.value = PIPE_4,
		.hfi_id = HFI_PROP_PIPE,
		.set = iris_set_pipe,
	},
	{
		.cap_id = POC,
		.min = 0,
		.max = 2,
		.step_or_mask = 1,
		.value = 1,
		.hfi_id = HFI_PROP_PIC_ORDER_CNT_TYPE,
	},
	{
		.cap_id = CODED_FRAMES,
		.min = CODED_FRAMES_PROGRESSIVE,
		.max = CODED_FRAMES_PROGRESSIVE,
		.step_or_mask = 0,
		.value = CODED_FRAMES_PROGRESSIVE,
		.hfi_id = HFI_PROP_CODED_FRAMES,
	},
	{
		.cap_id = BIT_DEPTH,
		.min = BIT_DEPTH_8,
		.max = BIT_DEPTH_8,
		.step_or_mask = 1,
		.value = BIT_DEPTH_8,
		.hfi_id = HFI_PROP_LUMA_CHROMA_BIT_DEPTH,
	},
	{
		.cap_id = RAP_FRAME,
		.min = 0,
		.max = 1,
		.step_or_mask = 1,
		.value = 1,
		.hfi_id = HFI_PROP_DEC_START_FROM_RAP_FRAME,
		.flags = CAP_FLAG_INPUT_PORT,
		.set = iris_set_u32,
	},
};

static struct platform_inst_caps platform_inst_cap_sm8550 = {
	.min_frame_width = 96,
	.max_frame_width = 8192,
	.min_frame_height = 96,
	.max_frame_height = 8192,
	.max_mbpf = (8192 * 4352) / 256,
	.mb_cycles_vpp = 200,
	.mb_cycles_fw = 489583,
	.mb_cycles_fw_vpp = 66234,
	.num_comv = 0,
};

static struct ubwc_config_data ubwc_config_sm8550 = {
	.max_channels = 8,
	.mal_length = 32,
	.highest_bank_bit = 16,
	.bank_swzl_level = 0,
	.bank_swz2_level = 1,
	.bank_swz3_level = 1,
	.bank_spreading = 1,
};

static struct tz_cp_config tz_cp_config_sm8550 = {
	.cp_start = 0,
	.cp_size = 0x25800000,
	.cp_nonpixel_start = 0x01000000,
	.cp_nonpixel_size = 0x24800000,
};

static const u32 sm8550_vdec_input_config_params[] = {
	HFI_PROP_BITSTREAM_RESOLUTION,
	HFI_PROP_CROP_OFFSETS,
	HFI_PROP_CODED_FRAMES,
	HFI_PROP_BUFFER_FW_MIN_OUTPUT_COUNT,
	HFI_PROP_PIC_ORDER_CNT_TYPE,
	HFI_PROP_PROFILE,
	HFI_PROP_LEVEL,
	HFI_PROP_SIGNAL_COLOR_INFO,
};

static const u32 sm8550_vdec_output_config_params[] = {
	HFI_PROP_COLOR_FORMAT,
	HFI_PROP_LINEAR_STRIDE_SCANLINE,
};

static const u32 sm8550_vdec_subscribe_input_properties[] = {
	HFI_PROP_NO_OUTPUT,
};

static const u32 sm8550_vdec_subscribe_output_properties[] = {
	HFI_PROP_PICTURE_TYPE,
	HFI_PROP_CABAC_SESSION,
};

static const u32 sm8550_dec_ip_int_buf_tbl[] = {
	BUF_BIN,
	BUF_COMV,
	BUF_NON_COMV,
	BUF_LINE,
};

static const u32 sm8550_dec_op_int_buf_tbl[] = {
	BUF_DPB,
};

/* platforms catalogs */
#include "iris_catalog_sm8550.h"
#include "iris_catalog_sm8650.h"
