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

static struct platform_inst_driver_cap instance_driver_cap_data_sm8250[] = {
	{
		.cap_id = FRAME_WIDTH,
		.min = 128,
		.max = 8192,
		.value = 1920,
	},
	{
		.cap_id = FRAME_HEIGHT,
		.min = 128,
		.max = 8192,
		.value = 1920,
	},
	{
		.cap_id = MBPF,
		.min = 64,
		.max = 138240,
		.value = 138240,
	},
};

static struct platform_inst_fw_cap instance_fw_cap_data_sm8250[] = {
	{
		.cap_id = PIPE,
		.min = PIPE_1,
		.max = PIPE_4,
		.step_or_mask = 1,
		.value = PIPE_4,
		.hfi_id = HFI_PROPERTY_PARAM_WORK_ROUTE,
		.flags = CAP_FLAG_NONE,
		.set = iris_set_pipe,
	},
	{
		.cap_id = STAGE,
		.min = STAGE_1,
		.max = STAGE_2,
		.step_or_mask = 1,
		.value = STAGE_2,
		.hfi_id = HFI_PROPERTY_PARAM_WORK_MODE,
		.flags = CAP_FLAG_NONE,
		.set = iris_set_stage,
	},
	{
		.cap_id = DEBLOCK,
		.min = 0,
		.max = 1,
		.step_or_mask = 1,
		.value = 0,
		.hfi_id = HFI_PROPERTY_CONFIG_VDEC_POST_LOOP_DEBLOCKER,
		.flags = CAP_FLAG_NONE,
		.set = iris_set_u32,
	},
};

static void iris_set_sm8250_preset_registers(struct iris_core *core)
{
	writel(0x0, core->reg_base + 0xB0088);
}

static const struct icc_info sm8250_icc_table[] = {
	{ "cpu-cfg",    1000, 1000     },
	{ "video-mem",  1000, 15000000 },
};

static const char * const sm8250_clk_reset_table[] = { "bus", "core" };

static const char * const sm8250_pmdomain_table[] = { "venus", "vcodec0" };

static const char * const sm8250_opp_pd_table[] = { "mx", NULL };

static const struct platform_clk_data sm8250_clk_table[] = {
	{IRIS_AXI_CLK,  "iface"        },
	{IRIS_CTRL_CLK, "core"         },
	{IRIS_HW_CLK,   "vcodec0_core" },
};

static struct tz_cp_config tz_cp_config_sm8250 = {
	.cp_start = 0,
	.cp_size = 0x25800000,
	.cp_nonpixel_start = 0x01000000,
	.cp_nonpixel_size = 0x24800000,
};

static const u32 sm8250_vdec_input_config_param[] = {
	HFI_PROPERTY_PARAM_FRAME_SIZE,
	HFI_PROPERTY_CONFIG_VIDEOCORES_USAGE,
	HFI_PROPERTY_PARAM_UNCOMPRESSED_FORMAT_SELECT,
	HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO,
	HFI_PROPERTY_PARAM_BUFFER_COUNT_ACTUAL,
	HFI_PROPERTY_PARAM_VDEC_MULTI_STREAM,
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

struct iris_platform_data sm8250_data = {
	.get_instance = iris_hfi_gen1_get_instance,
	.init_hfi_command_ops = &iris_hfi_gen1_command_ops_init,
	.init_hfi_response_ops = iris_hfi_gen1_response_ops_init,
	.vpu_ops = &iris_vpu2_ops,
	.set_preset_registers = iris_set_sm8250_preset_registers,
	.icc_tbl = sm8250_icc_table,
	.icc_tbl_size = ARRAY_SIZE(sm8250_icc_table),
	.clk_rst_tbl = sm8250_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(sm8250_clk_reset_table),
	.pmdomain_tbl = sm8250_pmdomain_table,
	.pmdomain_tbl_size = ARRAY_SIZE(sm8250_pmdomain_table),
	.opp_pd_tbl = sm8250_opp_pd_table,
	.opp_pd_tbl_size = ARRAY_SIZE(sm8250_opp_pd_table),
	.clk_tbl = sm8250_clk_table,
	.clk_tbl_size = ARRAY_SIZE(sm8250_clk_table),
	.dma_mask = GENMASK(31, 29) - 1,
	.fwname = "qcom/vpu-1.0/venus.mbn",
	.pas_id = IRIS_PAS_ID,
	.inst_driver_cap_data = instance_driver_cap_data_sm8250,
	.inst_driver_cap_data_size = ARRAY_SIZE(instance_driver_cap_data_sm8250),
	.inst_fw_cap_data = instance_fw_cap_data_sm8250,
	.inst_fw_cap_data_size = ARRAY_SIZE(instance_fw_cap_data_sm8250),
	.tz_cp_config_data = &tz_cp_config_sm8250,
	.hw_response_timeout = HW_RESPONSE_TIMEOUT_VALUE,
	.num_vpp_pipe = 4,
	.max_session_count = 16,
	.max_mbpf = (8192 * 4352) / 256,
	.input_config_params =
		sm8250_vdec_input_config_param,
	.input_config_params_size =
		ARRAY_SIZE(sm8250_vdec_input_config_param),

	.dec_ip_int_buf_tbl = sm8250_dec_ip_int_buf_tbl,
	.dec_ip_int_buf_tbl_size = ARRAY_SIZE(sm8250_dec_ip_int_buf_tbl),
	.dec_op_int_buf_tbl = sm8250_dec_op_int_buf_tbl,
	.dec_op_int_buf_tbl_size = ARRAY_SIZE(sm8250_dec_op_int_buf_tbl),
};
