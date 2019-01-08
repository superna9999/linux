// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Amlogic Meson MMC Sub Clock Controller Driver
 *
 * Copyright (c) 2017 Baylibre SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 *
 * Copyright (c) 2018 Amlogic, inc.
 * Author: Yixun Lan <yixun.lan@amlogic.com>
 * Author: Jianxin Pan <jianxin.pan@amlogic.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/amlogic,mmc-clkc.h>

#include "clk-regmap.h"
#include "clk-phase.h"
#include "clk-phase-delay.h"
#include "sclk-div.h"

/* clock ID used by internal driver */

#define SD_EMMC_CLOCK		0
#define CLK_DELAY_STEP_PS	200
#define MUX_CLK_NUM_PARENTS	2
#define MMC_MAX_CLKS		4

struct mmc_clkc_data {
	struct meson_clk_phase_delay_data tx;
	struct meson_clk_phase_delay_data rx;
};

static struct clk_regmap_mux_data mmc_clkc_mux_data = {
	.offset = SD_EMMC_CLOCK,
	.mask   = 0x3,
	.shift  = 6,
};

static const struct meson_sclk_div_data mmc_clkc_div_data = {
	.div = {
		.reg_off = SD_EMMC_CLOCK,
		.width   = 6,
	},
	.flags = CLK_DIVIDER_ONE_BASED,
};

static struct meson_clk_phase_data mmc_clkc_core_phase = {
	.ph = {
		.reg_off = SD_EMMC_CLOCK,
		.shift   = 8,
		.width   = 2,
	}
};

static const struct mmc_clkc_data mmc_clkc_gx_data = {
	.tx = {
		.phase = {
			.reg_off = SD_EMMC_CLOCK,
			.shift   = 10,
			.width   = 2,
		},
		.delay = {
			.reg_off = SD_EMMC_CLOCK,
			.shift   = 16,
			.width   = 4,
		},
		.delay_step_ps = CLK_DELAY_STEP_PS,
	},
	.rx = {
		.phase = {
			.reg_off = SD_EMMC_CLOCK,
			.shift   = 12,
			.width   = 2,
		},
		.delay = {
			.reg_off = SD_EMMC_CLOCK,
			.shift   = 20,
			.width   = 4,
		},
		.delay_step_ps   = CLK_DELAY_STEP_PS,
	},
};

static const struct mmc_clkc_data mmc_clkc_axg_data = {
	.tx = {
		.phase = {
			.reg_off = SD_EMMC_CLOCK,
			.shift   = 10,
			.width   = 2,
		},
		.delay = {
			.reg_off = SD_EMMC_CLOCK,
			.shift   = 16,
			.width   = 6,
		},
		.delay_step_ps   = CLK_DELAY_STEP_PS,
	},
	.rx = {
		.phase = {
			.reg_off = SD_EMMC_CLOCK,
			.shift   = 12,
			.width   = 2,
		},
		.delay = {
			.reg_off = SD_EMMC_CLOCK,
			.shift   = 22,
			.width   = 6,
		},
		.delay_step_ps   = CLK_DELAY_STEP_PS,
	},
};

static const struct of_device_id mmc_clkc_match_table[] = {
	{
		.compatible	= "amlogic,gx-mmc-clkc",
		.data		= &mmc_clkc_gx_data
	},
	{
		.compatible	= "amlogic,axg-mmc-clkc",
		.data		= &mmc_clkc_axg_data
	},
	{}
};
MODULE_DEVICE_TABLE(of, mmc_clkc_match_table);

static struct clk_regmap *
mmc_clkc_register_clk(struct device *dev, struct regmap *map,
		      struct clk_init_data *init,
		      const char *suffix, void *data)
{
	struct clk_regmap *clk;
	char *name;
	int ret;

	clk = devm_kzalloc(dev, sizeof(*clk), GFP_KERNEL);
	if (!clk)
		return ERR_PTR(-ENOMEM);

	name = kasprintf(GFP_KERNEL, "%s#%s", dev_name(dev), suffix);
	if (!name)
		return ERR_PTR(-ENOMEM);

	init->name = name;
	clk->map = map;
	clk->data = data;
	clk->hw.init = init;
	ret = devm_clk_hw_register(dev, &clk->hw);
	if (ret)
		clk = ERR_PTR(ret);

	kfree(name);
	return clk;
}

static struct clk_regmap *mmc_clkc_register_mux(struct device *dev,
						struct regmap *map)
{
	const char *parent_names[MUX_CLK_NUM_PARENTS];
	struct clk_init_data init;
	struct clk_regmap *mux;
	struct clk *clk;
	int i;

	for (i = 0; i < MUX_CLK_NUM_PARENTS; i++) {
		char name[8];

		snprintf(name, sizeof(name), "clkin%d", i);
		clk = devm_clk_get(dev, name);
		if (IS_ERR(clk)) {
			if (clk != ERR_PTR(-EPROBE_DEFER))
				dev_err(dev, "Missing clock %s\n", name);
			return ERR_CAST(clk);
		}

		parent_names[i] = __clk_get_name(clk);
	}

	init.ops = &clk_regmap_mux_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = parent_names;
	init.num_parents = MUX_CLK_NUM_PARENTS;

	mux = mmc_clkc_register_clk(dev, map, &init, "mux", &mmc_clkc_mux_data);
	if (IS_ERR(mux))
		dev_err(dev, "Mux clock registration failed\n");

	return mux;
}

static struct clk_regmap *
mmc_clkc_register_clk_with_parent(struct device *dev, struct regmap *map,
				  char *suffix, const struct clk_hw *hw,
				  unsigned long flags,
				  const struct clk_ops *ops, void *data)
{
	struct clk_init_data init;
	struct clk_regmap *clk;
	const char *parent_name = clk_hw_get_name(hw);

	init.ops = ops;
	init.flags = flags;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clk = mmc_clkc_register_clk(dev, map, &init, suffix, data);
	if (IS_ERR(clk))
		dev_err(dev, "%s clock registration failed\n", suffix);

	return clk;
}

static int mmc_clkc_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *onecell_data;
	struct device *dev = &pdev->dev;
	struct mmc_clkc_data *data;
	struct regmap *map;
	struct clk_regmap *clk, *core;
	struct meson_sclk_div_data *div_data;

	/*cast to drop the const in match->data*/
	data = (struct mmc_clkc_data *)of_device_get_match_data(dev);
	if (!data)
		return -ENODEV;

	map = syscon_node_to_regmap(dev->of_node);
	if (IS_ERR(map)) {
		dev_err(dev, "could not find mmc clock controller\n");
		return PTR_ERR(map);
	}

	onecell_data = devm_kzalloc(dev, sizeof(*onecell_data) +
				    sizeof(*onecell_data->hws) * MMC_MAX_CLKS,
				    GFP_KERNEL);
	if (!onecell_data)
		return -ENOMEM;

	clk = mmc_clkc_register_mux(dev, map);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	div_data = devm_kzalloc(dev, sizeof(*div_data), GFP_KERNEL);
	if (!div_data)
		return -ENOMEM;

	memcpy(div_data, &mmc_clkc_div_data, sizeof(*div_data));
	clk = mmc_clkc_register_clk_with_parent(dev, map, "div",
						&clk->hw,
						CLK_SET_RATE_PARENT,
						&meson_sclk_div_ops,
						div_data);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	onecell_data->hws[CLKID_MMC_DIV] = &clk->hw,

	core = mmc_clkc_register_clk_with_parent(dev, map, "core",
						 &clk->hw,
						 CLK_SET_RATE_PARENT,
						 &meson_clk_phase_ops,
						 &mmc_clkc_core_phase);
	if (IS_ERR(core))
		return PTR_ERR(core);

	onecell_data->hws[CLKID_MMC_PHASE_CORE] = &core->hw,

	clk = mmc_clkc_register_clk_with_parent(dev, map, "rx",
						&core->hw,  0,
						&meson_clk_phase_delay_ops,
						&data->rx);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	onecell_data->hws[CLKID_MMC_PHASE_RX] = &clk->hw,
	clk = mmc_clkc_register_clk_with_parent(dev, map, "tx",
						&core->hw,  0,
						&meson_clk_phase_delay_ops,
						&data->tx);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	onecell_data->hws[CLKID_MMC_PHASE_TX] = &clk->hw,
	onecell_data->num = MMC_MAX_CLKS;
	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					   onecell_data);
}

static struct platform_driver mmc_clkc_driver = {
	.probe		= mmc_clkc_probe,
	.driver		= {
		.name	= "meson-mmc-clkc",
		.of_match_table = of_match_ptr(mmc_clkc_match_table),
	},
};

module_platform_driver(mmc_clkc_driver);

MODULE_DESCRIPTION("Amlogic AXG MMC clock driver");
MODULE_AUTHOR("Jianxin Pan <jianxin.pan@amlogic.com>");
MODULE_LICENSE("GPL v2");
