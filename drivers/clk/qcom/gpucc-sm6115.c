// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,sm6115-gpucc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "clk-regmap-phy-mux.h"
#include "gdsc.h"
#include "reset.h"

#define CX_GMU_CBCR_SLEEP_MASK		0xf
#define CX_GMU_CBCR_SLEEP_SHIFT		4
#define CX_GMU_CBCR_WAKE_MASK		0xf
#define CX_GMU_CBCR_WAKE_SHIFT		8

enum {
	DT_BI_TCXO,
	DT_GCC_GPU_GPLL0_CLK_SRC,
	DT_GCC_GPU_GPLL0_DIV_CLK_SRC,
};

enum {
	P_BI_TCXO,
	P_GPLL0_OUT_MAIN,
	P_GPLL0_OUT_MAIN_DIV,
	P_GPUCC_PLL0_OUT_AUX2,
	P_GPUCC_PLL0_OUT_MAIN,
	P_GPUCC_PLL1_OUT_AUX,
	P_GPUCC_PLL1_OUT_MAIN,
};

static struct pll_vco default_vco[] = {
	{ 1000000000, 2000000000, 0 },
};

static struct pll_vco pll1_vco[] = {
	{ 500000000, 1000000000, 2 },
};

static const struct alpha_pll_config gpucc_pll0_config = {
	.l = 0x3e,
	.alpha = 0,
	.alpha_hi = 0x80,
	.vco_val = 0x0 << 20,
	.vco_mask = GENMASK(21, 20),
	.alpha_en_mask = BIT(24),
	.main_output_mask = BIT(0),
	.aux_output_mask = BIT(1),
	.aux2_output_mask = BIT(2),
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi1_val = 0x1,
};

/* 1200MHz configuration */
static struct clk_alpha_pll gpucc_pll0 = {
	.offset = 0x0,
	.vco_table = default_vco,
	.num_vco = ARRAY_SIZE(default_vco),
	.flags = SUPPORTS_DYNAMIC_UPDATE,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_pll0",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpucc_pll0_out_aux2[] = {
	{ 0x0, 1 },
	{ }
};

static struct clk_alpha_pll_postdiv gpucc_pll0_out_aux2 = {
	.offset = 0x0,
	.post_div_shift = 8,
	.post_div_table = post_div_table_gpucc_pll0_out_aux2,
	.num_post_div = ARRAY_SIZE(post_div_table_gpucc_pll0_out_aux2),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpucc_pll0_out_aux2",
		.parent_data = &(const struct clk_parent_data){
			.hw = &gpucc_pll0.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

/* 640MHz configuration */
static const struct alpha_pll_config gpucc_pll1_config = {
	.l = 0x21,
	.alpha = 0x55555555,
	.alpha_hi = 0x55,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x2 << 20,
	.vco_mask = GENMASK(21, 20),
	.main_output_mask = BIT(0),
	.aux_output_mask = BIT(1),
	.config_ctl_val = 0x4001055b,
	.test_ctl_hi1_val = 0x1,
};

static struct clk_alpha_pll gpucc_pll1 = {
	.offset = 0x100,
	.vco_table = pll1_vco,
	.num_vco = ARRAY_SIZE(pll1_vco),
	.flags = SUPPORTS_DYNAMIC_UPDATE,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_pll1",
			.parent_data = &(const struct clk_parent_data){
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},
	},
};

static const struct clk_div_table post_div_table_gpucc_pll1_out_aux[] = {
	{ 0x0, 1 },
	{ }
};

static struct clk_alpha_pll_postdiv gpucc_pll1_out_aux = {
	.offset = 0x100,
	.post_div_shift = 15,
	.post_div_table = post_div_table_gpucc_pll1_out_aux,
	.num_post_div = ARRAY_SIZE(post_div_table_gpucc_pll1_out_aux),
	.width = 3,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_DEFAULT],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpucc_pll1_out_aux",
		.parent_data = &(const struct clk_parent_data){
			.hw = &gpucc_pll1.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_ops,
	},
};

static const struct parent_map gpucc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPUCC_PLL0_OUT_MAIN, 1 },
	{ P_GPUCC_PLL1_OUT_MAIN, 3 },
	{ P_GPLL0_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_MAIN_DIV, 6 },
};

static const struct clk_parent_data gpucc_parent_data_0[] = {
	{ .index = P_BI_TCXO },
	{ .hw = &gpucc_pll0.clkr.hw },
	{ .hw = &gpucc_pll1.clkr.hw },
	{ .index = DT_GCC_GPU_GPLL0_CLK_SRC },
	{ .index = DT_GCC_GPU_GPLL0_DIV_CLK_SRC },
};

static const struct parent_map gpucc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPUCC_PLL0_OUT_AUX2, 2 },
	{ P_GPUCC_PLL1_OUT_AUX, 3 },
	{ P_GPLL0_OUT_MAIN, 5 },
};

static const struct clk_parent_data gpucc_parent_data_1[] = {
	{ .index = P_BI_TCXO },
	{ .hw = &gpucc_pll0_out_aux2.clkr.hw },
	{ .hw = &gpucc_pll1_out_aux.clkr.hw },
	{ .index = DT_GCC_GPU_GPLL0_CLK_SRC },
};

static const struct freq_tbl ftbl_gpucc_gmu_clk_src[] = {
	F(200000000, P_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 gpucc_gmu_clk_src = {
	.cmd_rcgr = 0x1120,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpucc_parent_map_0,
	.freq_tbl = ftbl_gpucc_gmu_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpucc_gmu_clk_src",
		.parent_data = gpucc_parent_data_0,
		.num_parents = ARRAY_SIZE(gpucc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_gpucc_gx_gfx3d_clk_src[] = {
	F(320000000, P_GPUCC_PLL1_OUT_AUX, 2, 0, 0),
	F(465000000, P_GPUCC_PLL1_OUT_AUX, 2, 0, 0),
	F(600000000, P_GPUCC_PLL0_OUT_AUX2, 2, 0, 0),
	F(745000000, P_GPUCC_PLL0_OUT_AUX2, 2, 0, 0),
	F(820000000, P_GPUCC_PLL0_OUT_AUX2, 2, 0, 0),
	F(900000000, P_GPUCC_PLL0_OUT_AUX2, 2, 0, 0),
	F(950000000, P_GPUCC_PLL0_OUT_AUX2, 2, 0, 0),
	F(980000000, P_GPUCC_PLL0_OUT_AUX2, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gpucc_gx_gfx3d_clk_src = {
	.cmd_rcgr = 0x101c,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpucc_parent_map_1,
	.freq_tbl = ftbl_gpucc_gx_gfx3d_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpucc_gx_gfx3d_clk_src",
		.parent_data = gpucc_parent_data_1,
		.num_parents = ARRAY_SIZE(gpucc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gpucc_ahb_clk = {
	.halt_reg = 0x1078,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1078,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_ahb_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_crc_ahb_clk = {
	.halt_reg = 0x107c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x107c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_crc_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_cx_gfx3d_clk = {
	.halt_reg = 0x10a4,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x10a4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_cx_gfx3d_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gpucc_gx_gfx3d_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_cx_gmu_clk = {
	.halt_reg = 0x1098,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1098,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_cx_gmu_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gpucc_gmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_cx_snoc_dvm_clk = {
	.halt_reg = 0x108c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x108c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_cx_snoc_dvm_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_cxo_aon_clk = {
	.halt_reg = 0x1004,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_cxo_aon_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_cxo_clk = {
	.halt_reg = 0x109c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x109c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_cxo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_gx_cxo_clk = {
	.halt_reg = 0x1060,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1060,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_gx_cxo_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_gx_gfx3d_clk = {
	.halt_reg = 0x1054,
	.halt_check = BRANCH_HALT_SKIP,
	.clkr = {
		.enable_reg = 0x1054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_gx_gfx3d_clk",
			.parent_data = &(const struct clk_parent_data){
				.hw = &gpucc_gx_gfx3d_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_sleep_clk = {
	.halt_reg = 0x1090,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1090,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_hlos1_vote_gpu_smmu_clk = {
	.halt_reg = 0x5000,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x5000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			 .name = "gpucc_hlos1_vote_gpu_smmu_clk",
			 .ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc gpu_cx_gdsc = {
	.gdscr = 0x106c,
	.gds_hw_ctrl = 0x1540,
	.pd = {
		.name = "gpu_cx_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc gpu_gx_gdsc = {
	.gdscr = 0x100c,
	.clamp_io_ctrl = 0x1508,
	.resets = (unsigned int []){ GPU_GX_BCR },
	.reset_count = 1,
	.pd = {
		.name = "gpu_gx_gdsc",
	},
	.parent = &gpu_cx_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
	.flags = CLAMP_IO | SW_RESET | VOTABLE,
};

static struct clk_regmap *gpucc_sm6115_clocks[] = {
	[GPUCC_AHB_CLK] = &gpucc_ahb_clk.clkr,
	[GPUCC_CRC_AHB_CLK] = &gpucc_crc_ahb_clk.clkr,
	[GPUCC_CX_GFX3D_CLK] = &gpucc_cx_gfx3d_clk.clkr,
	[GPUCC_CX_GMU_CLK] = &gpucc_cx_gmu_clk.clkr,
	[GPUCC_CX_SNOC_DVM_CLK] = &gpucc_cx_snoc_dvm_clk.clkr,
	[GPUCC_CXO_AON_CLK] = &gpucc_cxo_aon_clk.clkr,
	[GPUCC_CXO_CLK] = &gpucc_cxo_clk.clkr,
	[GPUCC_GMU_CLK_SRC] = &gpucc_gmu_clk_src.clkr,
	[GPUCC_GX_CXO_CLK] = &gpucc_gx_cxo_clk.clkr,
	[GPUCC_GX_GFX3D_CLK] = &gpucc_gx_gfx3d_clk.clkr,
	[GPUCC_GX_GFX3D_CLK_SRC] = &gpucc_gx_gfx3d_clk_src.clkr,
	[GPUCC_PLL0] = &gpucc_pll0.clkr,
	[GPUCC_PLL0_OUT_AUX2] = &gpucc_pll0_out_aux2.clkr,
	[GPUCC_PLL1] = &gpucc_pll1.clkr,
	[GPUCC_PLL1_OUT_AUX] = &gpucc_pll1_out_aux.clkr,
	[GPUCC_SLEEP_CLK] = &gpucc_sleep_clk.clkr,
	[GPUCC_HLOS1_VOTE_GPU_SMMU_CLK] = &gpucc_hlos1_vote_gpu_smmu_clk.clkr,
};

static const struct qcom_reset_map gpucc_sm6115_resets[] = {
	[GPU_GX_BCR] = { 0x1008 },
};

static struct gdsc *gpucc_sm6115_gdscs[] = {
	[GPU_CX_GDSC] = &gpu_cx_gdsc,
	[GPU_GX_GDSC] = &gpu_gx_gdsc,
};

static const struct regmap_config gpucc_sm6115_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x9000,
	.fast_io = true,
};

static const struct qcom_cc_desc gpucc_sm6115_desc = {
	.config = &gpucc_sm6115_regmap_config,
	.clks = gpucc_sm6115_clocks,
	.num_clks = ARRAY_SIZE(gpucc_sm6115_clocks),
	.resets = gpucc_sm6115_resets,
	.num_resets = ARRAY_SIZE(gpucc_sm6115_resets),
	.gdscs = gpucc_sm6115_gdscs,
	.num_gdscs = ARRAY_SIZE(gpucc_sm6115_gdscs),
};

static const struct of_device_id gpucc_sm6115_match_table[] = {
	{ .compatible = "qcom,sm6115-gpucc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpucc_sm6115_match_table);

static int gpucc_sm6115_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	unsigned int value, mask;

	regmap = qcom_cc_map(pdev, &gpucc_sm6115_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_alpha_pll_configure(&gpucc_pll0, regmap, &gpucc_pll0_config);
	clk_alpha_pll_configure(&gpucc_pll1, regmap, &gpucc_pll1_config);

	/* Recommended WAKEUP/SLEEP settings for the gpucc_cx_gmu_clk */
	mask = CX_GMU_CBCR_WAKE_MASK << CX_GMU_CBCR_WAKE_SHIFT;
	mask |= CX_GMU_CBCR_SLEEP_MASK << CX_GMU_CBCR_SLEEP_SHIFT;
	value = 0xf << CX_GMU_CBCR_WAKE_SHIFT | 0xf << CX_GMU_CBCR_SLEEP_SHIFT;
	regmap_update_bits(regmap, gpucc_cx_gmu_clk.clkr.enable_reg, mask, value);

	/* Set up PERIPH/MEM retain on the GPU core clock */
	regmap_update_bits(regmap, gpucc_gx_gfx3d_clk.halt_reg,
			   (BIT(14) | BIT(13)), (BIT(14) | BIT(13)));

	return qcom_cc_really_probe(pdev, &gpucc_sm6115_desc, regmap);
}

static struct platform_driver gpucc_sm6115_driver = {
	.probe = gpucc_sm6115_probe,
	.driver = {
		.name = "sm6115-gpucc",
		.of_match_table = gpucc_sm6115_match_table,
	},
};

static int __init gpucc_sm6115_init(void)
{
	return platform_driver_register(&gpucc_sm6115_driver);
}
subsys_initcall(gpucc_sm6115_init);

static void __exit gpucc_sm6115_exit(void)
{
	platform_driver_unregister(&gpucc_sm6115_driver);
}
module_exit(gpucc_sm6115_exit);

MODULE_DESCRIPTION("QTI GPUCC SM6115 Driver");
MODULE_LICENSE("GPL");
