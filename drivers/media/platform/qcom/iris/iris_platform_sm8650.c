// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_ctrls.h"
#include "iris_hfi_gen2.h"
#include "iris_hfi_gen2_defines.h"
#include "iris_platform_common.h"
#include "iris_resources.h"
#include "iris_vpu_common.h"

#define VIDEO_ARCH_LX 1

static struct platform_inst_driver_cap instance_driver_cap_data_sm8650[] = {
	{
		.cap_id = FRAME_WIDTH,
		.min = 96,
		.max = 8192,
		.value = 1920,
	},
	{
		.cap_id = FRAME_HEIGHT,
		.min = 96,
		.max = 8192,
		.value = 1920,
	},
	{
		.cap_id = MBPF,
		.min = 36,
		.max = 138240,
		.value = 138240,
	},
	{
		.cap_id = MB_CYCLES_VPP,
		.min = 200,
		.max = 200,
		.value = 200,
	},
	{
		.cap_id = MB_CYCLES_FW,
		.min = 489583,
		.max = 489583,
		.value = 489583,
	},
	{
		.cap_id = MB_CYCLES_FW_VPP,
		.min = 66234,
		.max = 66234,
		.value = 66234,
	},
	{
		.cap_id = NUM_COMV,
		.min = 0,
		.max = INT_MAX,
		.value = 0,
	},
};

static struct platform_inst_fw_cap instance_fw_cap_data_sm8650[] = {
	{
		.cap_id = PROFILE,
		.min = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE,
		.max = V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_HIGH),
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
		.flags = CAP_FLAG_NONE,
		.set = iris_set_stage,
	},
	{
		.cap_id = PIPE,
		.min = PIPE_1,
		.max = PIPE_4,
		.step_or_mask = 1,
		.value = PIPE_4,
		.hfi_id = HFI_PROP_PIPE,
		.flags = CAP_FLAG_NONE,
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
		.cap_id = DEFAULT_HEADER,
		.min = 0,
		.max = 1,
		.step_or_mask = 1,
		.value = 0,
		.hfi_id = HFI_PROP_DEC_DEFAULT_HEADER,
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

static void iris_set_sm8650_preset_registers(struct iris_core *core)
{
	writel(0x0, core->reg_base + 0xB0088);
}

static const struct icc_info sm8650_icc_table[] = {
	{ "cpu-cfg",    1000, 1000     },
	{ "video-mem",  1000, 15000000 },
};

static const char * const sm8650_clk_reset_table[] = { "bus" };

static const struct bw_info sm8650_bw_table_dec[] = {
	{ ((4096 * 2160) / 256) * 60, 1608000 },
	{ ((4096 * 2160) / 256) * 30,  826000 },
	{ ((1920 * 1080) / 256) * 60,  567000 },
	{ ((1920 * 1080) / 256) * 30,  294000 },
};

static const char * const sm8650_pmdomain_table[] = { "venus", "vcodec0" };

static const char * const sm8650_opp_pd_table[] = { "mxc", "mmcx", NULL };

static const struct platform_clk_data sm8650_clk_table[] = {
	{IRIS_AXI_CLK,  "iface"        },
	{IRIS_CTRL_CLK, "core"         },
	{IRIS_HW_CLK,   "vcodec0_core" },
};

static struct ubwc_config_data ubwc_config_sm8650 = {
	.max_channels = 8,
	.mal_length = 32,
	.highest_bank_bit = 16,
	.bank_swzl_level = 0,
	.bank_swz2_level = 1,
	.bank_swz3_level = 1,
	.bank_spreading = 1,
};

static struct tz_cp_config tz_cp_config_sm8650 = {
	.cp_start = 0,
	.cp_size = 0x25800000,
	.cp_nonpixel_start = 0x01000000,
	.cp_nonpixel_size = 0x24800000,
};

static const u32 sm8650_vdec_input_config_params[] = {
	HFI_PROP_BITSTREAM_RESOLUTION,
	HFI_PROP_CROP_OFFSETS,
	HFI_PROP_CODED_FRAMES,
	HFI_PROP_BUFFER_FW_MIN_OUTPUT_COUNT,
	HFI_PROP_PIC_ORDER_CNT_TYPE,
	HFI_PROP_PROFILE,
	HFI_PROP_LEVEL,
	HFI_PROP_SIGNAL_COLOR_INFO,
};

static const u32 sm8650_vdec_output_config_params[] = {
	HFI_PROP_COLOR_FORMAT,
	HFI_PROP_LINEAR_STRIDE_SCANLINE,
};

static const u32 sm8650_vdec_subscribe_input_properties[] = {
	HFI_PROP_NO_OUTPUT,
};

static const u32 sm8650_vdec_subscribe_output_properties[] = {
	HFI_PROP_PICTURE_TYPE,
	HFI_PROP_CABAC_SESSION,
};

static const u32 sm8650_dec_ip_int_buf_tbl[] = {
	BUF_BIN,
	BUF_COMV,
	BUF_NON_COMV,
	BUF_LINE,
};

static const u32 sm8650_dec_op_int_buf_tbl[] = {
	BUF_DPB,
};

struct iris_platform_data sm8650_data = {
	.get_instance = iris_hfi_gen2_get_instance,
	.init_hfi_command_ops = iris_hfi_gen2_command_ops_init,
	.init_hfi_response_ops = iris_hfi_gen2_response_ops_init,
	.vpu_ops = &iris_vpu3_ops,
	.set_preset_registers = iris_set_sm8650_preset_registers,
	.icc_tbl = sm8650_icc_table,
	.icc_tbl_size = ARRAY_SIZE(sm8650_icc_table),
	.clk_rst_tbl = sm8650_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(sm8650_clk_reset_table),
	.bw_tbl_dec = sm8650_bw_table_dec,
	.bw_tbl_dec_size = ARRAY_SIZE(sm8650_bw_table_dec),
	.pmdomain_tbl = sm8650_pmdomain_table,
	.pmdomain_tbl_size = ARRAY_SIZE(sm8650_pmdomain_table),
	.opp_pd_tbl = sm8650_opp_pd_table,
	.opp_pd_tbl_size = ARRAY_SIZE(sm8650_opp_pd_table),
	.clk_tbl = sm8650_clk_table,
	.clk_tbl_size = ARRAY_SIZE(sm8650_clk_table),
	.dma_mask = GENMASK(31, 29) - 1,
	.fwname = "qcom/vpu/vpu33_p4.mbn",
	.pas_id = IRIS_PAS_ID,
	.inst_driver_cap_data = instance_driver_cap_data_sm8650,
	.inst_driver_cap_data_size = ARRAY_SIZE(instance_driver_cap_data_sm8650),
	.inst_fw_cap_data = instance_fw_cap_data_sm8650,
	.inst_fw_cap_data_size = ARRAY_SIZE(instance_fw_cap_data_sm8650),
	.tz_cp_config_data = &tz_cp_config_sm8650,
	.core_arch = VIDEO_ARCH_LX,
	.hw_response_timeout = HW_RESPONSE_TIMEOUT_VALUE,
	.ubwc_config = &ubwc_config_sm8650,
	.num_vpp_pipe = 4,
	.max_session_count = 16,
	.max_mbpf = ((8192 * 4352) / 256) * 2,
	.input_config_params =
		sm8650_vdec_input_config_params,
	.input_config_params_size =
		ARRAY_SIZE(sm8650_vdec_input_config_params),
	.output_config_params =
		sm8650_vdec_output_config_params,
	.output_config_params_size =
		ARRAY_SIZE(sm8650_vdec_output_config_params),
	.dec_input_prop = sm8650_vdec_subscribe_input_properties,
	.dec_input_prop_size = ARRAY_SIZE(sm8650_vdec_subscribe_input_properties),
	.dec_output_prop = sm8650_vdec_subscribe_output_properties,
	.dec_output_prop_size = ARRAY_SIZE(sm8650_vdec_subscribe_output_properties),

	.dec_ip_int_buf_tbl = sm8650_dec_ip_int_buf_tbl,
	.dec_ip_int_buf_tbl_size = ARRAY_SIZE(sm8650_dec_ip_int_buf_tbl),
	.dec_op_int_buf_tbl = sm8650_dec_op_int_buf_tbl,
	.dec_op_int_buf_tbl_size = ARRAY_SIZE(sm8650_dec_op_int_buf_tbl),
};
