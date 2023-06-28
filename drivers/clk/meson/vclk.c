// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Neil Armstrong <neil.armstrong@linaro.org>
 */

#include <linux/module.h>
#include "vclk.h"

/* The VCLK gate has a supplementary reset bit to pulse after ungating */

static inline struct clk_regmap_vclk_data *
clk_get_regmap_vclk_data(struct clk_regmap *clk)
{
	return (struct clk_regmap_vclk_data *)clk->data;
}

static int clk_regmap_vclk_enable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_data *vclk = clk_get_regmap_vclk_data(clk);

	meson_parm_write(clk->map, &vclk->enable, 1);

	/* Do a reset pulse */
	meson_parm_write(clk->map, &vclk->reset, 1);
	meson_parm_write(clk->map, &vclk->reset, 0);

	return 0;
}

static void clk_regmap_vclk_disable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_data *vclk = clk_get_regmap_vclk_data(clk);

	meson_parm_write(clk->map, &vclk->enable, 0);
}

static int clk_regmap_vclk_is_enabled(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_data *vclk = clk_get_regmap_vclk_data(clk);

	return meson_parm_read(clk->map, &vclk->enable);
}

const struct clk_ops clk_regmap_vclk_ops = {
	.enable = clk_regmap_vclk_enable,
	.disable = clk_regmap_vclk_disable,
	.is_enabled = clk_regmap_vclk_is_enabled,
};
EXPORT_SYMBOL_GPL(clk_regmap_vclk_ops);

/* The VCLK Divider has supplementary reset & enable bits */

static inline struct clk_regmap_vclk_div_data *
clk_get_regmap_vclk_div_data(struct clk_regmap *clk)
{
	return (struct clk_regmap_vclk_div_data *)clk->data;
}

static unsigned long clk_regmap_vclk_div_recalc_rate(struct clk_hw *hw,
						     unsigned long prate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_div_data *vclk = clk_get_regmap_vclk_div_data(clk);

	return divider_recalc_rate(hw, prate, meson_parm_read(clk->map, &vclk->div),
				   vclk->table, vclk->flags, vclk->div.width);
}

static int clk_regmap_vclk_div_determine_rate(struct clk_hw *hw,
					      struct clk_rate_request *req)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_div_data *vclk = clk_get_regmap_vclk_div_data(clk);

	return divider_determine_rate(hw, req, vclk->table, vclk->div.width,
				      vclk->flags);
}

static int clk_regmap_vclk_div_set_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_div_data *vclk = clk_get_regmap_vclk_div_data(clk);
	int ret;

	ret = divider_get_val(rate, parent_rate, vclk->table, vclk->div.width,
			      vclk->flags);
	if (ret < 0)
		return ret;

	meson_parm_write(clk->map, &vclk->div, ret);

	return 0;
};

static int clk_regmap_vclk_div_enable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_div_data *vclk = clk_get_regmap_vclk_div_data(clk);

	/* Unreset the divider when ungating */
	meson_parm_write(clk->map, &vclk->reset, 0);
	meson_parm_write(clk->map, &vclk->enable, 1);

	return 0;
}

static void clk_regmap_vclk_div_disable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_div_data *vclk = clk_get_regmap_vclk_div_data(clk);

	/* Reset the divider when gating */
	meson_parm_write(clk->map, &vclk->enable, 0);
	meson_parm_write(clk->map, &vclk->reset, 1);
}

static int clk_regmap_vclk_div_is_enabled(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_div_data *vclk = clk_get_regmap_vclk_div_data(clk);

	return meson_parm_read(clk->map, &vclk->enable);
}

const struct clk_ops clk_regmap_vclk_div_ops = {
	.recalc_rate = clk_regmap_vclk_div_recalc_rate,
	.determine_rate = clk_regmap_vclk_div_determine_rate,
	.set_rate = clk_regmap_vclk_div_set_rate,
	.enable = clk_regmap_vclk_div_enable,
	.disable = clk_regmap_vclk_div_disable,
	.is_enabled = clk_regmap_vclk_div_is_enabled,
};
EXPORT_SYMBOL_GPL(clk_regmap_vclk_div_ops);

MODULE_DESCRIPTION("Amlogic vclk clock driver");
MODULE_AUTHOR("Neil Armstrong <neil.armstrong@linaro.org>");
MODULE_LICENSE("GPL v2");
