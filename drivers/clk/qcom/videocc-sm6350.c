// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Linaro Limited
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,sm6350-videocc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "common.h"
#include "reset.h"
#include "gdsc.h"

enum {
	P_BI_TCXO,
	P_CHIP_SLEEP_CLK,
	P_VIDEO_PLL0_OUT_EVEN,
};

static const struct pll_vco fabia_vco[] = {
	{ 125000000, 1000000000, 1 },
};

/* 600 MHz */
static const struct alpha_pll_config video_pll0_config = {
	.l = 0x1f,
	.alpha = 0x4000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002067,
	.test_ctl_val = 0x40000000,
	.test_ctl_hi_val = 0x00000002,
	.user_ctl_val = 0x00000101,
	.user_ctl_hi_val = 0x00004005,
};

static struct clk_alpha_pll video_pll0 = {
	.offset = 0x0,
	.vco_table = fabia_vco,
	.num_vco = ARRAY_SIZE(fabia_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "video_pll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fabia_ops,
		},
	},
};

static const struct clk_div_table post_div_table_video_pll0_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv video_pll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 8,
	.post_div_table = post_div_table_video_pll0_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_video_pll0_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_pll0_out_even",
		.parent_hws = (const struct clk_hw*[]){
			&video_pll0.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_fabia_ops,
	},
};


static const struct parent_map video_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_VIDEO_PLL0_OUT_EVEN, 3 },
};

static const struct clk_parent_data video_cc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &video_pll0_out_even.clkr.hw },
};

static const struct freq_tbl ftbl_video_cc_iris_clk_src[] = {
	F(133250000, P_VIDEO_PLL0_OUT_EVEN, 2, 0, 0),
	F(240000000, P_VIDEO_PLL0_OUT_EVEN, 1.5, 0, 0),
	F(300000000, P_VIDEO_PLL0_OUT_EVEN, 1, 0, 0),
	F(380000000, P_VIDEO_PLL0_OUT_EVEN, 1, 0, 0),
	F(460000000, P_VIDEO_PLL0_OUT_EVEN, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_iris_clk_src = {
	.cmd_rcgr = 0x1000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = video_cc_parent_map_0,
	.freq_tbl = ftbl_video_cc_iris_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_cc_iris_clk_src",
		.parent_data = video_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(video_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_video_cc_sleep_clk_src[] = {
	F(32764, P_CHIP_SLEEP_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 video_cc_sleep_clk_src = {
	.cmd_rcgr = 0x701c,
	.mnd_width = 0,
	.hid_width = 5,
	.freq_tbl = ftbl_video_cc_sleep_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "video_cc_sleep_clk_src",
		.parent_data = &(const struct clk_parent_data){
			.fw_name = "sleep_clk",
		},
		.num_parents = 1,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch video_cc_iris_ahb_clk = {
	.halt_reg = 0x5004,
	.halt_check = BRANCH_VOTED,
	.clkr = {
		.enable_reg = 0x5004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_iris_ahb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&video_cc_iris_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0_axi_clk = {
	.halt_reg = 0x800c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x800c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_mvs0_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvs0_core_clk = {
	.halt_reg = 0x3010,
	.halt_check = BRANCH_VOTED,
	.hwcg_reg = 0x3010,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x3010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_mvs0_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&video_cc_iris_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvsc_core_clk = {
	.halt_reg = 0x2014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_mvsc_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&video_cc_iris_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_mvsc_ctl_axi_clk = {
	.halt_reg = 0x8004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x8004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_mvsc_ctl_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_sleep_clk = {
	.halt_reg = 0x7034,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x7034,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_sleep_clk",
			.parent_hws = (const struct clk_hw*[]){
				&video_cc_sleep_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_venus_ahb_clk = {
	.halt_reg = 0x801c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x801c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_venus_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch video_cc_xo_clk = {
	.halt_reg = 0x7018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x7018,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "video_cc_xo_clk",
			.flags = CLK_IS_CRITICAL,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc mvsc_gdsc = {
	.gdscr = 0x2004,
	.pd = {
		.name = "mvsc_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc mvs0_gdsc = {
	.gdscr = 0x3004,
	.pd = {
		.name = "mvs0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc *video_cc_sm6350_gdscs[] = {
	[MVSC_GDSC] = &mvsc_gdsc,
	[MVS0_GDSC] = &mvs0_gdsc,
};

static struct clk_regmap *video_cc_sm6350_clocks[] = {
	[VIDEO_CC_IRIS_AHB_CLK] = &video_cc_iris_ahb_clk.clkr,
	[VIDEO_CC_IRIS_CLK_SRC] = &video_cc_iris_clk_src.clkr,
	[VIDEO_CC_MVS0_AXI_CLK] = &video_cc_mvs0_axi_clk.clkr,
	[VIDEO_CC_MVS0_CORE_CLK] = &video_cc_mvs0_core_clk.clkr,
	[VIDEO_CC_MVSC_CORE_CLK] = &video_cc_mvsc_core_clk.clkr,
	[VIDEO_CC_MVSC_CTL_AXI_CLK] = &video_cc_mvsc_ctl_axi_clk.clkr,
	[VIDEO_CC_SLEEP_CLK] = &video_cc_sleep_clk.clkr,
	[VIDEO_CC_SLEEP_CLK_SRC] = &video_cc_sleep_clk_src.clkr,
	[VIDEO_CC_VENUS_AHB_CLK] = &video_cc_venus_ahb_clk.clkr,
	[VIDEO_CC_XO_CLK] = &video_cc_xo_clk.clkr,
	[VIDEO_PLL0] = &video_pll0.clkr,
	[VIDEO_PLL0_OUT_EVEN] = &video_pll0_out_even.clkr,
};

static const struct regmap_config video_cc_sm6350_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xb000,
	.fast_io = true,
};

static const struct qcom_cc_desc video_cc_sm6350_desc = {
	.config = &video_cc_sm6350_regmap_config,
	.clks = video_cc_sm6350_clocks,
	.num_clks = ARRAY_SIZE(video_cc_sm6350_clocks),
	.gdscs = video_cc_sm6350_gdscs,
	.num_gdscs = ARRAY_SIZE(video_cc_sm6350_gdscs),
};

static const struct of_device_id video_cc_sm6350_match_table[] = {
	{ .compatible = "qcom,sm6350-videocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, video_cc_sm6350_match_table);

static int video_cc_sm6350_probe(struct platform_device *pdev)
{
	struct regmap *regmap;

	regmap = qcom_cc_map(pdev, &video_cc_sm6350_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_fabia_pll_configure(&video_pll0, regmap, &video_pll0_config);

	return qcom_cc_really_probe(pdev, &video_cc_sm6350_desc, regmap);
}

static struct platform_driver video_cc_sm6350_driver = {
	.probe = video_cc_sm6350_probe,
	.driver = {
		.name = "video_cc-sm6350",
		.of_match_table = video_cc_sm6350_match_table,
	},
};

static int __init video_cc_sm6350_init(void)
{
	return platform_driver_register(&video_cc_sm6350_driver);
}
core_initcall(video_cc_sm6350_init);

static void __exit video_cc_sm6350_exit(void)
{
	platform_driver_unregister(&video_cc_sm6350_driver);
}
module_exit(video_cc_sm6350_exit);

MODULE_DESCRIPTION("QTI VIDEO_CC SM6350 Driver");
MODULE_LICENSE("GPL v2");
