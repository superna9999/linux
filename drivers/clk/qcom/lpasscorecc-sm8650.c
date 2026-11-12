// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,lpasscorecc-sm8650.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "common.h"
#include "gdsc.h"

enum {
	P_BI_TCXO,
	P_LPASS_CORE_CC_DIG_PLL_OUT_MAIN,
	P_LPASS_CORE_CC_DIG_PLL_OUT_MAIN_DIV_CLK_SRC,
	P_LPASS_CORE_CC_DIG_PLL_OUT_ODD,
};

static const struct pll_vco lucid_ole_vco[] = {
	{ 249600000, 2300000000, 0 },
};

static const struct alpha_pll_config lpass_core_cc_dig_pll_config = {
	/* 614.4 MHz */
	.l = 0x20,
	.alpha = 0x0,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00182261,
	.config_ctl_hi1_val = 0x82aa299c,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000003,
	.test_ctl_hi1_val = 0x00009000,
	.test_ctl_hi2_val = 0x00000034,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000005,
};

static struct clk_alpha_pll lpass_core_cc_dig_pll = {
	.offset = 0x0,
	.config = &lpass_core_cc_dig_pll_config,
	.vco_table = lucid_ole_vco,
	.num_vco = ARRAY_SIZE(lucid_ole_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "lpass_core_cc_dig_pll",
			.parent_data = &(const struct clk_parent_data){
				.index = 0,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_evo_ops,
		},
	},
};

static const struct clk_div_table post_div_table_lpass_core_cc_dig_pll_out_odd[] = {
	{ 0x5, 5 },
	{ }
};

static struct clk_alpha_pll_postdiv lpass_core_cc_dig_pll_out_odd = {
	.offset = 0x0,
	.post_div_shift = 14,
	.post_div_table = post_div_table_lpass_core_cc_dig_pll_out_odd,
	.num_post_div = ARRAY_SIZE(post_div_table_lpass_core_cc_dig_pll_out_odd),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr.hw.init = &(struct clk_init_data){
		.name = "lpass_core_cc_dig_pll_out_odd",
		.parent_hws = (const struct clk_hw*[]){
			&lpass_core_cc_dig_pll.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_alpha_pll_postdiv_lucid_ops,
	},
};

static struct clk_regmap_div lpass_core_cc_dig_pll_out_main_div_clk_src = {
	.reg = 0x10008,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "lpass_core_cc_dig_pll_out_main_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&lpass_core_cc_dig_pll.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static const struct parent_map lpass_core_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_LPASS_CORE_CC_DIG_PLL_OUT_ODD, 5 },
};

static const struct clk_parent_data lpass_core_cc_parent_data_0[] = {
	{ .index = 0 },
	{ .hw = &lpass_core_cc_dig_pll_out_odd.clkr.hw },
};

static const struct parent_map lpass_core_cc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_LPASS_CORE_CC_DIG_PLL_OUT_MAIN, 1 },
	{ P_LPASS_CORE_CC_DIG_PLL_OUT_MAIN_DIV_CLK_SRC, 2 },
};

static const struct clk_parent_data lpass_core_cc_parent_data_ao_2[] = {
	{ .index = 1 },
	{ .hw = &lpass_core_cc_dig_pll.clkr.hw },
	{ .hw = &lpass_core_cc_dig_pll_out_main_div_clk_src.clkr.hw },
};

static const struct freq_tbl ftbl_lpass_core_cc_core_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(51200000, P_LPASS_CORE_CC_DIG_PLL_OUT_MAIN_DIV_CLK_SRC, 6, 0, 0),
	F(102400000, P_LPASS_CORE_CC_DIG_PLL_OUT_MAIN_DIV_CLK_SRC, 3, 0, 0),
	F(204800000, P_LPASS_CORE_CC_DIG_PLL_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 lpass_core_cc_core_clk_src = {
	.cmd_rcgr = 0x25000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = lpass_core_cc_parent_map_2,
	.freq_tbl = ftbl_lpass_core_cc_core_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_core_cc_core_clk_src",
		.parent_data = lpass_core_cc_parent_data_ao_2,
		.num_parents = ARRAY_SIZE(lpass_core_cc_parent_data_ao_2),
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_lpass_core_cc_ext_if0_clk_src[] = {
	F(1536000, P_BI_TCXO, 1, 2, 25),
	F(2304000, P_BI_TCXO, 1, 3, 25),
	F(3072000, P_BI_TCXO, 1, 4, 25),
	F(9600000, P_BI_TCXO, 2, 0, 0),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 lpass_core_cc_ext_if0_clk_src = {
	.cmd_rcgr = 0x18000,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = lpass_core_cc_parent_map_0,
	.freq_tbl = ftbl_lpass_core_cc_ext_if0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_core_cc_ext_if0_clk_src",
		.parent_data = lpass_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(lpass_core_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 lpass_core_cc_ext_if1_clk_src = {
	.cmd_rcgr = 0x19000,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = lpass_core_cc_parent_map_0,
	.freq_tbl = ftbl_lpass_core_cc_ext_if0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_core_cc_ext_if1_clk_src",
		.parent_data = lpass_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(lpass_core_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 lpass_core_cc_ext_if2_clk_src = {
	.cmd_rcgr = 0x1a000,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = lpass_core_cc_parent_map_0,
	.freq_tbl = ftbl_lpass_core_cc_ext_if0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_core_cc_ext_if2_clk_src",
		.parent_data = lpass_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(lpass_core_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 lpass_core_cc_ext_mclk0_clk_src = {
	.cmd_rcgr = 0x28000,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = lpass_core_cc_parent_map_0,
	.freq_tbl = ftbl_lpass_core_cc_ext_if0_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_core_cc_ext_mclk0_clk_src",
		.parent_data = lpass_core_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(lpass_core_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch lpass_core_cc_core_clk = {
	.halt_reg = 0x27000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x27000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x27000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_core_cc_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_core_cc_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch lpass_core_cc_ext_if0_ibit_clk = {
	.halt_reg = 0x18018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x18018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_core_cc_ext_if0_ibit_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_core_cc_ext_if0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_core_cc_ext_if1_ibit_clk = {
	.halt_reg = 0x19018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x19018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_core_cc_ext_if1_ibit_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_core_cc_ext_if1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_core_cc_ext_if2_ibit_clk = {
	.halt_reg = 0x1a018,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1a018,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_core_cc_ext_if2_ibit_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_core_cc_ext_if2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_core_cc_lpm_core_clk = {
	.halt_reg = 0x26000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x26000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_core_cc_lpm_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_core_cc_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_core_cc_lpm_mem0_core_clk = {
	.halt_reg = 0x26004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x26004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_core_cc_lpm_mem0_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_core_cc_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_core_cc_ext_mclk0_clk = {
	.halt_reg = 0x28014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x28014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_core_cc_ext_mclk0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_core_cc_ext_mclk0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_core_cc_sysnoc_mport_core_clk = {
	.halt_reg = 0x2b000,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2b000,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2b000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_core_cc_sysnoc_mport_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_core_cc_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc lpass_core_cc_lpass_core_hm_gdsc = {
	.gdscr = 0x0,
	.pd = {
		.name = "lpass_core_cc_lpass_core_hm_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct clk_regmap *lpass_core_cc_sm8650_clocks[] = {
	[LPASS_CORE_CC_CORE_CLK] = &lpass_core_cc_core_clk.clkr,
	[LPASS_CORE_CC_CORE_CLK_SRC] = &lpass_core_cc_core_clk_src.clkr,
	[LPASS_CORE_CC_DIG_PLL] = &lpass_core_cc_dig_pll.clkr,
	[LPASS_CORE_CC_DIG_PLL_OUT_MAIN_DIV_CLK_SRC] =
		&lpass_core_cc_dig_pll_out_main_div_clk_src.clkr,
	[LPASS_CORE_CC_DIG_PLL_OUT_ODD] = &lpass_core_cc_dig_pll_out_odd.clkr,
	[LPASS_CORE_CC_EXT_IF0_CLK_SRC] = &lpass_core_cc_ext_if0_clk_src.clkr,
	[LPASS_CORE_CC_EXT_IF0_IBIT_CLK] = &lpass_core_cc_ext_if0_ibit_clk.clkr,
	[LPASS_CORE_CC_EXT_IF1_CLK_SRC] = &lpass_core_cc_ext_if1_clk_src.clkr,
	[LPASS_CORE_CC_EXT_IF1_IBIT_CLK] = &lpass_core_cc_ext_if1_ibit_clk.clkr,
	[LPASS_CORE_CC_EXT_IF2_CLK_SRC] = &lpass_core_cc_ext_if2_clk_src.clkr,
	[LPASS_CORE_CC_EXT_IF2_IBIT_CLK] = &lpass_core_cc_ext_if2_ibit_clk.clkr,
	[LPASS_CORE_CC_LPM_CORE_CLK] = &lpass_core_cc_lpm_core_clk.clkr,
	[LPASS_CORE_CC_LPM_MEM0_CORE_CLK] = &lpass_core_cc_lpm_mem0_core_clk.clkr,
	[LPASS_CORE_CC_SYSNOC_MPORT_CORE_CLK] = &lpass_core_cc_sysnoc_mport_core_clk.clkr,
	[LPASS_CORE_CC_EXT_MCLK0_CLK] = &lpass_core_cc_ext_mclk0_clk.clkr,
	[LPASS_CORE_CC_EXT_MCLK0_CLK_SRC] = &lpass_core_cc_ext_mclk0_clk_src.clkr,
};

static struct regmap_config lpass_core_cc_sm8650_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x56014,
	.fast_io = true,
};

static struct clk_alpha_pll *lpass_core_cc_sm8650_plls[] = {
	&lpass_core_cc_dig_pll,
};

static const struct qcom_cc_driver_data lpass_core_cc_sm8650_driver_data = {
	.alpha_plls = lpass_core_cc_sm8650_plls,
	.num_alpha_plls = ARRAY_SIZE(lpass_core_cc_sm8650_plls),
};

static const struct qcom_cc_desc lpass_core_cc_sm8650_desc = {
	.config = &lpass_core_cc_sm8650_regmap_config,
	.clks = lpass_core_cc_sm8650_clocks,
	.num_clks = ARRAY_SIZE(lpass_core_cc_sm8650_clocks),
	.driver_data = &lpass_core_cc_sm8650_driver_data,
	.use_rpm = true,
};

static const struct of_device_id lpass_core_cc_sm8650_match_table[] = {
	{ .compatible = "qcom,sm8650-lpasscorecc" },
	{ }
};
MODULE_DEVICE_TABLE(of, lpass_core_cc_sm8650_match_table);

static struct gdsc *lpass_core_hm_sm8650_gdscs[] = {
	[LPASS_CORE_HM_GDSC] = &lpass_core_cc_lpass_core_hm_gdsc,
};

static struct regmap_config lpass_core_hm_sm8650_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x9000,
	.fast_io = true,
};

static const u32 lpass_core_hm_sm8650_critical_cbcrs[] = {
	0x9000, /* LPASS_TOP_CC_LPASS_CORE_SWAY_AHB_LS */
};

static const struct qcom_cc_driver_data lpass_core_hm_sm8650_driver_data = {
	.clk_cbcrs = lpass_core_hm_sm8650_critical_cbcrs,
	.num_clk_cbcrs = ARRAY_SIZE(lpass_core_hm_sm8650_critical_cbcrs),
};

static const struct qcom_cc_desc lpass_core_hm_sm8650_desc = {
	.config = &lpass_core_hm_sm8650_regmap_config,
	.gdscs = lpass_core_hm_sm8650_gdscs,
	.num_gdscs = ARRAY_SIZE(lpass_core_hm_sm8650_gdscs),
	.driver_data = &lpass_core_hm_sm8650_driver_data,
	.use_rpm = true,
};

static int lpass_core_cc_sm8650_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &lpass_core_cc_sm8650_desc);
}

static struct platform_driver lpass_core_cc_sm8650_driver = {
	.probe = lpass_core_cc_sm8650_probe,
	.driver = {
		.name = "lpass_core_cc-sm8650",
		.of_match_table = lpass_core_cc_sm8650_match_table,
	},
};

static int lpass_hm_core_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &lpass_core_hm_sm8650_desc);
}

static const struct dev_pm_ops lpass_hm_sm8650_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_clk_suspend, pm_clk_resume, NULL)
};

static const struct of_device_id lpass_hm_sm8650_match_table[] = {
	{ .compatible = "qcom,sm8650-lpasshm" },
	{ }
};
MODULE_DEVICE_TABLE(of, lpass_hm_sm8650_match_table);

static struct platform_driver lpass_hm_sm8650_driver = {
	.probe = lpass_hm_core_probe,
	.driver = {
		.name = "lpass_hm-sm8650",
		.of_match_table = lpass_hm_sm8650_match_table,
		.pm = &lpass_hm_sm8650_pm_ops,
	},
};

static int __init lpass_core_cc_sm8650_init(void)
{
	int ret;

	ret = platform_driver_register(&lpass_hm_sm8650_driver);
	if (ret)
		return ret;

	return platform_driver_register(&lpass_core_cc_sm8650_driver);
}
subsys_initcall(lpass_core_cc_sm8650_init);

static void __exit lpass_core_cc_sm8650_exit(void)
{
	platform_driver_unregister(&lpass_core_cc_sm8650_driver);
	platform_driver_unregister(&lpass_hm_sm8650_driver);
}
module_exit(lpass_core_cc_sm8650_exit);

MODULE_DESCRIPTION("QTI LPASS_CORE_CC SM8650 Driver");
MODULE_LICENSE("GPL v2");
