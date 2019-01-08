// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright (c) 2018 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 *
 * Sample clock generator divider:
 * This HW divider gates with value 0 
 *
 * val >= 1
 * divider = val + 1 if ONE_BASED is not set, otherwise divider = val.
 *
 * The duty cycle may also be set for the LR clock variant. The duty cycle
 * ratio is:
 *
 * hi = [0 - val]
 * duty_cycle = (1 + hi) / (1 + val) if ONE_BASED is not set, otherwise:
 * duty_cycle = hi / (1 + val)
 */

#include <linux/clk-provider.h>
#include <linux/module.h>

#include "clk-regmap.h"
#include "sclk-div.h"

static inline int sclk_get_reg(int val, unsigned char flag)
{
	if ((flag & MESON_SCLK_ONE_BASED) || !val)
		return val;
	else
		return val - 1;
}

static inline int sclk_get_divider(int reg, unsigned char flag)
{
	if (flag & MESON_SCLK_ONE_BASED)
		return reg;
	else
		return reg + 1;
}

static inline struct meson_sclk_div_data *
meson_sclk_div_data(struct clk_regmap *clk)
{
	return (struct meson_sclk_div_data *)clk->data;
}

static int sclk_div_maxdiv(struct meson_sclk_div_data *sclk)
{
	unsigned int reg = clk_div_mask(sclk->div.width);

	return sclk_get_divider(reg, sclk->flags);
}

static int sclk_div_getdiv(struct clk_hw *hw, unsigned long rate,
			   unsigned long prate, int maxdiv)
{
	int div = DIV_ROUND_CLOSEST_ULL((u64)prate, rate);
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_sclk_div_data *sclk = meson_sclk_div_data(clk);
	int mindiv = sclk_get_divider(1, sclk->flags);

	return clamp(div, mindiv, maxdiv);
}

static int sclk_div_bestdiv(struct clk_hw *hw, unsigned long rate,
			    unsigned long *prate,
			    struct meson_sclk_div_data *sclk)
{
	struct clk_hw *parent = clk_hw_get_parent(hw);
	int bestdiv = 0, i, mindiv;
	unsigned long maxdiv, now, parent_now;
	unsigned long best = 0, best_parent = 0;

	if (!rate)
		rate = 1;

	maxdiv = sclk_div_maxdiv(sclk);

	if (!(clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT))
		return sclk_div_getdiv(hw, rate, *prate, maxdiv);

	/*
	 * The maximum divider we can use without overflowing
	 * unsigned long in rate * i below
	 */
	maxdiv = min(ULONG_MAX / rate, maxdiv);
	mindiv = sclk_get_divider(1, sclk->flags);

	for (i = mindiv; i <= maxdiv; i++) {
		/*
		 * It's the most ideal case if the requested rate can be
		 * divided from parent clock without needing to change
		 * parent rate, so return the divider immediately.
		 */
		if (rate * i == *prate)
			return i;

		parent_now = clk_hw_round_rate(parent, rate * i);
		now = DIV_ROUND_UP_ULL((u64)parent_now, i);

		if (abs(rate - now) < abs(rate - best)) {
			bestdiv = i;
			best = now;
			best_parent = parent_now;
		}
	}

	if (!bestdiv)
		bestdiv = sclk_div_maxdiv(sclk);
	else
		*prate = best_parent;

	return bestdiv;
}

static long sclk_div_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_sclk_div_data *sclk = meson_sclk_div_data(clk);
	int div;

	div = sclk_div_bestdiv(hw, rate, prate, sclk);

	return DIV_ROUND_UP_ULL((u64)*prate, div);
}

static void sclk_apply_ratio(struct clk_regmap *clk,
			     struct meson_sclk_div_data *sclk)
{
	unsigned int hi = DIV_ROUND_CLOSEST(sclk->cached_div *
					    sclk->cached_duty.num,
					    sclk->cached_duty.den);

	meson_parm_write(clk->map, &sclk->hi, sclk_get_reg(hi, sclk->flags));
}

static int sclk_div_set_duty_cycle(struct clk_hw *hw,
				   struct clk_duty *duty)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_sclk_div_data *sclk = meson_sclk_div_data(clk);

	if (MESON_PARM_APPLICABLE(&sclk->hi)) {
		memcpy(&sclk->cached_duty, duty, sizeof(*duty));
		sclk_apply_ratio(clk, sclk);
	}

	return 0;
}

static int sclk_div_get_duty_cycle(struct clk_hw *hw,
				   struct clk_duty *duty)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_sclk_div_data *sclk = meson_sclk_div_data(clk);
	int hi;

	if (!MESON_PARM_APPLICABLE(&sclk->hi)) {
		duty->num = 1;
		duty->den = 2;
		return 0;
	}

	hi = meson_parm_read(clk->map, &sclk->hi);
	duty->num = sclk_get_divider(hi, sclk->flags);
	duty->den = sclk->cached_div;
	return 0;
}

static void sclk_apply_divider(struct clk_regmap *clk,
			       struct meson_sclk_div_data *sclk)
{
	unsigned int div;

	if (MESON_PARM_APPLICABLE(&sclk->hi))
		sclk_apply_ratio(clk, sclk);

	div = sclk_get_reg(sclk->cached_div, sclk->flags);
	meson_parm_write(clk->map, &sclk->div, div);
}

static int sclk_div_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long prate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_sclk_div_data *sclk = meson_sclk_div_data(clk);
	unsigned long maxdiv = sclk_div_maxdiv(sclk);

	sclk->cached_div = sclk_div_getdiv(hw, rate, prate, maxdiv);

	if (clk_hw_is_enabled(hw))
		sclk_apply_divider(clk, sclk);

	return 0;
}

static unsigned long sclk_div_recalc_rate(struct clk_hw *hw,
					  unsigned long prate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_sclk_div_data *sclk = meson_sclk_div_data(clk);

	return DIV_ROUND_UP_ULL((u64)prate, sclk->cached_div);
}

static int sclk_div_enable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_sclk_div_data *sclk = meson_sclk_div_data(clk);

	sclk_apply_divider(clk, sclk);

	return 0;
}

static void sclk_div_disable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_sclk_div_data *sclk = meson_sclk_div_data(clk);

	meson_parm_write(clk->map, &sclk->div, 0);
}

static int sclk_div_is_enabled(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_sclk_div_data *sclk = meson_sclk_div_data(clk);

	if (meson_parm_read(clk->map, &sclk->div))
		return 1;

	return 0;
}

static int sclk_div_init(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_sclk_div_data *sclk = meson_sclk_div_data(clk);
	unsigned int val;

	val = meson_parm_read(clk->map, &sclk->div);

	/* if the divider is initially disabled, assume max */
	if (!val)
		sclk->cached_div = sclk_div_maxdiv(sclk);
	else
		sclk->cached_div = sclk_get_divider(val, sclk->flags);

	sclk_div_get_duty_cycle(hw, &sclk->cached_duty);

	return 0;
}

const struct clk_ops meson_sclk_div_ops = {
	.recalc_rate	= sclk_div_recalc_rate,
	.round_rate	= sclk_div_round_rate,
	.set_rate	= sclk_div_set_rate,
	.enable		= sclk_div_enable,
	.disable	= sclk_div_disable,
	.is_enabled	= sclk_div_is_enabled,
	.get_duty_cycle	= sclk_div_get_duty_cycle,
	.set_duty_cycle = sclk_div_set_duty_cycle,
	.init		= sclk_div_init,
};
EXPORT_SYMBOL_GPL(meson_sclk_div_ops);

MODULE_DESCRIPTION("Amlogic Sample divider driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
