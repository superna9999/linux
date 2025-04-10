/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_CATALOG_SM8550_H
#define _IRIS_CATALOG_SM8550_H

#define VIDEO_ARCH_LX 1

static void iris_set_sm8550_preset_registers(struct iris_core *core)
{
	writel(0x0, core->reg_base + 0xB0088);
}

static const struct icc_info sm8550_icc_table[] = {
	{ "cpu-cfg",    1000, 1000     },
	{ "video-mem",  1000, 15000000 },
};

static const char * const sm8550_clk_reset_table[] = { "bus" };

static const struct bw_info sm8550_bw_table_dec[] = {
	{ ((4096 * 2160) / 256) * 60, 1608000 },
	{ ((4096 * 2160) / 256) * 30,  826000 },
	{ ((1920 * 1080) / 256) * 60,  567000 },
	{ ((1920 * 1080) / 256) * 30,  294000 },
};

static const char * const sm8550_pmdomain_table[] = { "venus", "vcodec0" };

static const char * const sm8550_opp_pd_table[] = { "mxc", "mmcx" };

static const struct platform_clk_data sm8550_clk_table[] = {
	{IRIS_AXI_CLK,  "iface"        },
	{IRIS_CTRL_CLK, "core"         },
	{IRIS_HW_CLK,   "vcodec0_core" },
};

struct iris_platform_data sm8550_data = {
	.get_instance = iris_hfi_gen2_get_instance,
	.init_hfi_command_ops = iris_hfi_gen2_command_ops_init,
	.init_hfi_response_ops = iris_hfi_gen2_response_ops_init,
	.vpu_ops = &iris_vpu3_ops,
	.set_preset_registers = iris_set_sm8550_preset_registers,
	.icc_tbl = sm8550_icc_table,
	.icc_tbl_size = ARRAY_SIZE(sm8550_icc_table),
	.clk_rst_tbl = sm8550_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(sm8550_clk_reset_table),
	.bw_tbl_dec = sm8550_bw_table_dec,
	.bw_tbl_dec_size = ARRAY_SIZE(sm8550_bw_table_dec),
	.pmdomain_tbl = sm8550_pmdomain_table,
	.pmdomain_tbl_size = ARRAY_SIZE(sm8550_pmdomain_table),
	.opp_pd_tbl = sm8550_opp_pd_table,
	.opp_pd_tbl_size = ARRAY_SIZE(sm8550_opp_pd_table),
	.clk_tbl = sm8550_clk_table,
	.clk_tbl_size = ARRAY_SIZE(sm8550_clk_table),
	/* Upper bound of DMA address range */
	.dma_mask = 0xe0000000 - 1,
	.fwname = "qcom/vpu/vpu30_p4.mbn",
	.pas_id = IRIS_PAS_ID,
	.inst_caps = &platform_inst_cap_sm8550,
	.inst_fw_caps = inst_fw_cap_sm8550,
	.inst_fw_caps_size = ARRAY_SIZE(inst_fw_cap_sm8550),
	.tz_cp_config_data = &tz_cp_config_sm8550,
	.core_arch = VIDEO_ARCH_LX,
	.hw_response_timeout = HW_RESPONSE_TIMEOUT_VALUE,
	.ubwc_config = &ubwc_config_sm8550,
	.num_vpp_pipe = 4,
	.max_session_count = 16,
	.max_core_mbpf = ((8192 * 4352) / 256) * 2,
	.input_config_params =
		sm8550_vdec_input_config_params,
	.input_config_params_size =
		ARRAY_SIZE(sm8550_vdec_input_config_params),
	.output_config_params =
		sm8550_vdec_output_config_params,
	.output_config_params_size =
		ARRAY_SIZE(sm8550_vdec_output_config_params),
	.dec_input_prop = sm8550_vdec_subscribe_input_properties,
	.dec_input_prop_size = ARRAY_SIZE(sm8550_vdec_subscribe_input_properties),
	.dec_output_prop = sm8550_vdec_subscribe_output_properties,
	.dec_output_prop_size = ARRAY_SIZE(sm8550_vdec_subscribe_output_properties),

	.dec_ip_int_buf_tbl = sm8550_dec_ip_int_buf_tbl,
	.dec_ip_int_buf_tbl_size = ARRAY_SIZE(sm8550_dec_ip_int_buf_tbl),
	.dec_op_int_buf_tbl = sm8550_dec_op_int_buf_tbl,
	.dec_op_int_buf_tbl_size = ARRAY_SIZE(sm8550_dec_op_int_buf_tbl),
};

#endif
