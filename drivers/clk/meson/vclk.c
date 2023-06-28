// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Neil Armstrong <neil.armstrong@linaro.org>
 */

#include <linux/module.h>
#include "vclk.h"

/* The VCLK gate has a supplementary reset bit to pulse after ungating */

static int clk_regmap_vclk_enable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_data *vclk = clk_get_regmap_vclk_data(clk);

	regmap_set_bits(clk->map, vclk->offset, BIT(vclk->enable_bit_idx));

	/* Do a reset pulse */
	regmap_set_bits(clk->map, vclk->offset, BIT(vclk->reset_bit_idx));
	regmap_clear_bits(clk->map, vclk->offset, BIT(vclk->reset_bit_idx));

	return 0;
}

static void clk_regmap_vclk_disable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_data *vclk = clk_get_regmap_vclk_data(clk);

	regmap_clear_bits(clk->map, vclk->offset, BIT(vclk->enable_bit_idx));
}

static int clk_regmap_vclk_is_enabled(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_data *vclk = clk_get_regmap_vclk_data(clk);
	unsigned int val;

	regmap_read(clk->map, vclk->offset, &val);

	return val & BIT(vclk->enable_bit_idx) ? 1 : 0;
}

const struct clk_ops clk_regmap_vclk_ops = {
	.enable = clk_regmap_vclk_enable,
	.disable = clk_regmap_vclk_disable,
	.is_enabled = clk_regmap_vclk_is_enabled,
};
EXPORT_SYMBOL_GPL(clk_regmap_vclk_ops);

/* The VCLK Divider has supplementary reset & enable bits */

static unsigned long clk_regmap_vclk_div_recalc_rate(struct clk_hw *hw,
						     unsigned long prate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_div_data *vclk = clk_get_regmap_vclk_div_data(clk);
	unsigned int val;
	int ret;

	ret = regmap_read(clk->map, vclk->offset, &val);
	if (ret)
		/* Gives a hint that something is wrong */
		return 0;

	val >>= vclk->shift;
	val &= clk_div_mask(vclk->width);

	return divider_recalc_rate(hw, prate, val, vclk->table, vclk->flags,
				   vclk->width);
}

static int clk_regmap_vclk_div_determine_rate(struct clk_hw *hw,
					      struct clk_rate_request *req)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_div_data *vclk = clk_get_regmap_vclk_div_data(clk);

	return divider_determine_rate(hw, req, vclk->table, vclk->width,
				      vclk->flags);
}

static int clk_regmap_vclk_div_set_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_div_data *vclk = clk_get_regmap_vclk_div_data(clk);
	unsigned int val;
	int ret;

	ret = divider_get_val(rate, parent_rate, vclk->table, vclk->width,
			      vclk->flags);
	if (ret < 0)
		return ret;

	val = (unsigned int)ret << vclk->shift;
	return regmap_update_bits(clk->map, vclk->offset,
				  clk_div_mask(vclk->width) << vclk->shift, val);
};

static int clk_regmap_vclk_div_enable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_div_data *vclk = clk_get_regmap_vclk_div_data(clk);

	/* Unreset the divider when ungating */
	regmap_clear_bits(clk->map, vclk->offset, BIT(vclk->reset_bit_idx));

	return regmap_set_bits(clk->map, vclk->offset, BIT(vclk->enable_bit_idx));
}

static void clk_regmap_vclk_div_disable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_div_data *vclk = clk_get_regmap_vclk_div_data(clk);

	/* Reset the divider when gating */
	regmap_clear_bits(clk->map, vclk->offset, BIT(vclk->enable_bit_idx));

	regmap_set_bits(clk->map, vclk->offset, BIT(vclk->reset_bit_idx));
}

static int clk_regmap_vclk_div_is_enabled(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct clk_regmap_vclk_div_data *vclk = clk_get_regmap_vclk_div_data(clk);
	unsigned int val;

	regmap_read(clk->map, vclk->offset, &val);

	return val & BIT(vclk->enable_bit_idx) ? 1 : 0;
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
