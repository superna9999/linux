// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,lpassaudiocc-sm8650.h>

#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	P_BI_TCXO,
};

static const struct parent_map lpass_audio_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data lpass_audio_cc_parent_data_0[] = {
	{ .index = 0 },
};

static const struct parent_map lpass_aon_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
};

static const struct clk_parent_data lpass_aon_cc_parent_data_0[] = {
	{ .index = 0 },
};

static struct clk_rcg2 lpass_aon_cc_main_rcg_clk_src = {
	.cmd_rcgr = 0x19000,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = lpass_aon_cc_parent_map_0,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_aon_cc_main_rcg_clk_src",
		.parent_data = lpass_aon_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(lpass_aon_cc_parent_data_0),
		//.flags = CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 lpass_aon_cc_tx_mclk_clk_src = {
	.cmd_rcgr = 0x1b004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = lpass_aon_cc_parent_map_0,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_aon_cc_tx_mclk_clk_src",
		.parent_data = lpass_aon_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(lpass_aon_cc_parent_data_0),
		//.flags = CLK_OPS_PARENT_ENABLE,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 lpass_audio_cc_ext_if2_clk_src = {
	.cmd_rcgr = 0x19004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = lpass_audio_cc_parent_map_0,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_audio_cc_ext_if2_clk_src",
		.parent_data = lpass_audio_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(lpass_audio_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 lpass_audio_cc_wsa_mclk_clk_src = {
	.cmd_rcgr = 0x2a004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = lpass_audio_cc_parent_map_0,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_audio_cc_wsa_mclk_clk_src",
		.parent_data = lpass_audio_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(lpass_audio_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_rcg2 lpass_audio_cc_rx_mclk_clk_src = {
	.cmd_rcgr = 0x2c004,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = lpass_audio_cc_parent_map_0,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "lpass_audio_cc_rx_mclk_clk_src",
		.parent_data = lpass_audio_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(lpass_audio_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch lpass_aon_cc_audio_hm_h_clk;

static struct clk_branch lpass_audio_cc_codec_mem_clk = {
	.halt_reg = 0x2e000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2e000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_codec_mem_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_audio_hm_h_clk.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_codec_mem0_clk = {
	.halt_reg = 0x2e004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2e004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_codec_mem0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_audio_hm_h_clk.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_codec_mem1_clk = {
	.halt_reg = 0x2e010,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2e010,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_codec_mem1_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_audio_hm_h_clk.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_codec_mem2_clk = {
	.halt_reg = 0x2e01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2e01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_codec_mem2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_audio_hm_h_clk.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_codec_mem3_clk = {
	.halt_reg = 0x2e028,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2e028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_codec_mem3_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_audio_hm_h_clk.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_rx_mclk_clk = {
	.halt_reg = 0x2c0d4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2c0d4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_rx_mclk_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_audio_cc_rx_mclk_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_ext_if2_ibit_clk = {
	.halt_reg = 0x1901c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1901c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_ext_if2_ibit_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_audio_cc_ext_if2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_wsa_mclk_clk = {
	.halt_reg = 0x2a0d4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2a0d4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_wsa_mclk_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_audio_cc_wsa_mclk_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_wsa2_mclk_clk = {
	.halt_reg = 0x2d004,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2d004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_wsa2_mclk_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_audio_cc_wsa_mclk_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_aon_cc_tx_mclk_clk;

static struct clk_branch lpass_audio_cc_tx_mclk_wsa_clk = {
	.halt_reg = 0x2a0f4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x240d4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_tx_mclk_wsa_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_tx_mclk_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_tx_mclk_rx_clk = {
	.halt_reg = 0x2c0e0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2c0e0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_tx_mclk_rx_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_tx_mclk_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_audio_cc_tx_mclk_wsa2_clk = {
	.halt_reg = 0x2d00c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2d00c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_audio_cc_tx_mclk_wsa2_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_tx_mclk_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_aon_cc_audio_hm_h_clk = {
	.halt_reg = 0x11014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x11014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_aon_cc_audio_hm_h_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_main_rcg_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_aon_ops,
		},
	},
};

static struct clk_branch lpass_aon_cc_va_mem0_clk = {
	.halt_reg = 0x11060,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x11060,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_aon_cc_va_mem0_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_main_rcg_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch lpass_aon_cc_tx_mclk_clk = {
	.halt_reg = 0x1b014,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1b014,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "lpass_aon_cc_tx_mclk_clk",
			.parent_hws = (const struct clk_hw*[]){
				&lpass_aon_cc_tx_mclk_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc lpass_aon_cc_lpass_audio_hm_gdsc = {
	.gdscr = 0x11090,
	.pd = {
		.name = "lpass_aon_cc_lpass_audio_hm_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = RETAIN_FF_ENABLE,
};

static struct clk_regmap *lpass_aon_cc_sm8650_clocks[] = {
	[LPASS_AON_CC_MAIN_RCG_CLK_SRC] = &lpass_aon_cc_main_rcg_clk_src.clkr,
	[LPASS_AON_CC_AUDIO_HM_H_CLK] = &lpass_aon_cc_audio_hm_h_clk.clkr,
	[LPASS_AON_CC_VA_MEM0_CLK] = &lpass_aon_cc_va_mem0_clk.clkr,
	[LPASS_AON_CC_TX_MCLK_CLK_SRC] = &lpass_aon_cc_tx_mclk_clk_src.clkr,
	[LPASS_AON_CC_TX_MCLK_CLK] = &lpass_aon_cc_tx_mclk_clk.clkr,
};

static struct gdsc *lpass_aon_cc_sm8650_gdscs[] = {
	[LPASS_AON_CC_LPASS_AUDIO_HM_GDSC] = &lpass_aon_cc_lpass_audio_hm_gdsc,
};

static struct clk_regmap *lpass_audio_cc_sm8650_clocks[] = {
	[LPASS_AUDIO_CC_WSA_MCLK_CLK_SRC] = &lpass_audio_cc_wsa_mclk_clk_src.clkr,
	[LPASS_AUDIO_CC_RX_MCLK_CLK] = &lpass_audio_cc_rx_mclk_clk.clkr,
	[LPASS_AUDIO_CC_RX_MCLK_CLK_SRC] = &lpass_audio_cc_rx_mclk_clk_src.clkr,
	[LPASS_AUDIO_CC_WSA_MCLK_CLK] = &lpass_audio_cc_wsa_mclk_clk.clkr,
	[LPASS_AUDIO_CC_WSA2_MCLK_CLK] = &lpass_audio_cc_wsa2_mclk_clk.clkr,
	[LPASS_AUDIO_CC_CODEC_MEM_CLK] = &lpass_audio_cc_codec_mem_clk.clkr,
	[LPASS_AUDIO_CC_CODEC_MEM0_CLK] = &lpass_audio_cc_codec_mem0_clk.clkr,
	[LPASS_AUDIO_CC_CODEC_MEM1_CLK] = &lpass_audio_cc_codec_mem1_clk.clkr,
	[LPASS_AUDIO_CC_CODEC_MEM2_CLK] = &lpass_audio_cc_codec_mem2_clk.clkr,
	[LPASS_AUDIO_CC_CODEC_MEM3_CLK] = &lpass_audio_cc_codec_mem3_clk.clkr,
	[LPASS_AUDIO_CC_TX_MCLK_WSA_CLK] = &lpass_audio_cc_tx_mclk_wsa_clk.clkr,
	[LPASS_AUDIO_CC_TX_MCLK_RX_CLK] = &lpass_audio_cc_tx_mclk_rx_clk.clkr,	
	[LPASS_AUDIO_CC_TX_MCLK_WSA2_CLK] = &lpass_audio_cc_tx_mclk_wsa2_clk.clkr,
	[LPASS_AUDIO_CC_EXT_IF2_CLK_SRC] = &lpass_audio_cc_ext_if2_clk_src.clkr,
	[LPASS_AUDIO_CC_EXT_IF2_IBI_CLK] = &lpass_audio_cc_ext_if2_ibit_clk.clkr,
};

static struct regmap_config lpass_audio_cc_sm8650_regmap_config = {
	.name = "lpasscc_audio",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x2f000,
	.fast_io = true,
};

static const struct qcom_cc_desc lpass_audio_cc_sm8650_desc = {
	.config = &lpass_audio_cc_sm8650_regmap_config,
	.clks = lpass_audio_cc_sm8650_clocks,
	.num_clks = ARRAY_SIZE(lpass_audio_cc_sm8650_clocks),
};

static const struct of_device_id lpass_audio_cc_sm8650_match_table[] = {
	{ .compatible = "qcom,sm8650-lpassaudiocc" },
	{ }
};
MODULE_DEVICE_TABLE(of, lpass_audio_cc_sm8650_match_table);

static int lpass_audio_cc_sm8650_probe(struct platform_device *pdev)
{
	int ret;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	ret = devm_pm_clk_create(&pdev->dev);
	if (ret)
		return ret;

	ret = pm_clk_add(&pdev->dev, "iface");
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to acquire iface clock\n");
		return ret;
	}

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret)
		return ret;

	ret = qcom_cc_probe(pdev, &lpass_audio_cc_sm8650_desc);

	pm_runtime_put_sync(&pdev->dev);

	return ret;
}

static struct platform_driver lpass_audio_cc_sm8650_driver = {
	.probe = lpass_audio_cc_sm8650_probe,
	.driver = {
		.name = "lpass_audio_cc-sm8650",
		.of_match_table = lpass_audio_cc_sm8650_match_table,
	},
};

static struct regmap_config lpass_aon_cc_sm8650_regmap_config = {
	.name = "lpasscc_aon",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x2f000,
	.fast_io = true,
};

static const struct qcom_cc_desc lpass_aon_cc_sm8650_desc = {
	.config = &lpass_aon_cc_sm8650_regmap_config,
	.clks = lpass_aon_cc_sm8650_clocks,
	.num_clks = ARRAY_SIZE(lpass_aon_cc_sm8650_clocks),
	.gdscs = lpass_aon_cc_sm8650_gdscs,
	.num_gdscs = ARRAY_SIZE(lpass_aon_cc_sm8650_gdscs),
};

static int lpass_aon_cc_sm8650_probe(struct platform_device *pdev)
{
	int ret;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	ret = devm_pm_clk_create(&pdev->dev);
	if (ret)
		return ret;

	ret = pm_clk_add(&pdev->dev, "iface");
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to acquire iface clock\n");
		return ret;
	}

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret)
		return ret;

	ret = qcom_cc_probe(pdev, &lpass_aon_cc_sm8650_desc);

	pm_runtime_put_sync(&pdev->dev);

	return ret;
}

static const struct of_device_id lpass_aon_cc_sm8650_match_table[] = {
	{ .compatible = "qcom,sm8650-lpassaoncc" },
	{ }
};
MODULE_DEVICE_TABLE(of, lpass_aon_cc_sm8650_match_table);

static struct platform_driver lpass_aon_cc_sm8650_driver = {
	.probe = lpass_aon_cc_sm8650_probe,
	.driver = {
		.name = "lpass_aon_cc-sm8650",
		.of_match_table = lpass_aon_cc_sm8650_match_table,
	},
};

static int __init lpass_audio_cc_sm8650_init(void)
{
	int ret;

	ret = platform_driver_register(&lpass_aon_cc_sm8650_driver);
	if (ret)
		return ret;

	return platform_driver_register(&lpass_audio_cc_sm8650_driver);
}
subsys_initcall(lpass_audio_cc_sm8650_init);

static void __exit lpass_audio_cc_sm8650_exit(void)
{
	platform_driver_unregister(&lpass_audio_cc_sm8650_driver);
	platform_driver_unregister(&lpass_aon_cc_sm8650_driver);
}
module_exit(lpass_audio_cc_sm8650_exit);

MODULE_DESCRIPTION("LPASS Audio CC SM8650 Driver");
MODULE_LICENSE("GPL v2");
