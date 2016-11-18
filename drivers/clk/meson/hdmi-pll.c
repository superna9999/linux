/*
 * Copyright (c) 2015 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * In the most basic form, a Meson PLL is composed as follows:
 *
 *                     PLL
 *      +------------------------------+
 *      |                              |
 * in -----[ /N ]---[ *M ]---[ >>OD ]----->> out
 *      |         ^        ^           |
 *      +------------------------------+
 *                |        |
 *               FREF     VCO
 *
 * out = (in * M / N) >> OD
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "clkc.h"

static unsigned long meson_hdmi_pll_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct meson_hdmi_pll *pll = to_meson_hdmi_pll(hw);
	const struct hdmi_pll_rate_table *rate_table = pll->rate_table;
	u32 reg, reg2;
	int i;

	/* Read first registers */
	reg = readl(pll->reg) & ~BIT(pll->reset_bit);
	reg2 = readl(pll->reg + (1 << 2));

	if (reg & BIT(pll->reset_bit))
		return parent_rate;

	/* Remove lock and reset bit */
	reg &= ~BIT(pll->reset_bit);
	reg &= ~BIT(pll->lock_bit);

	for (i = 0; i < pll->rate_count; i++) {
		if (rate_table[i].cntl[0] == reg &&
		    rate_table[i].cntl[1] == reg2)
			return rate_table[i].rate;
	}

	/* If rate is not found, return invalid parent rate */
	return parent_rate;
}

static long meson_hdmi_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *parent_rate)
{
	struct meson_hdmi_pll *pll = to_meson_hdmi_pll(hw);
	const struct hdmi_pll_rate_table *rate_table = pll->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++) {
		if (rate <= rate_table[i].rate)
			return rate_table[i].rate;
	}

	/* else return the smallest value */
	return rate_table[0].rate;
}

static const struct hdmi_pll_rate_table *
meson_hdmi_get_pll_settings(struct meson_hdmi_pll *pll, unsigned long rate)
{
	const struct hdmi_pll_rate_table *rate_table = pll->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++) {
		if (rate == rate_table[i].rate)
			return &rate_table[i];
	}
	return NULL;
}

static int meson_hdmi_pll_wait_lock(struct meson_hdmi_pll *pll)
{
	int delay = 24000000;
	u32 reg;

	while (delay > 0) {
		reg = readl(pll->reg);

		if (reg & BIT(pll->lock_bit))
			return 0;
		delay--;
	}
	return -ETIMEDOUT;
}

static int meson_hdmi_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct meson_hdmi_pll *pll = to_meson_hdmi_pll(hw);
	const struct hdmi_pll_rate_table *rate_set;
	unsigned long old_rate;
	int ret = 0;
	u32 reg;
	int i;

	if (parent_rate == 0 || rate == 0)
		return -EINVAL;

	old_rate = rate;

	rate_set = meson_hdmi_get_pll_settings(pll, rate);
	if (!rate_set)
		return -EINVAL;

	/* PLL reset with the first register write */
	reg = rate_set->cntl[0] | BIT(pll->reset_bit);
	writel(reg, pll->reg);

	for (i = 1; i < 6; ++i)
		writel(rate_set->cntl[i], pll->reg + (i << 2));

	/* PLL Unreset */
	reg = readl(pll->reg) & ~BIT(pll->reset_bit);
	writel(reg, pll->reg);

	ret = meson_hdmi_pll_wait_lock(pll);
	if (ret) {
		pr_warn("%s: pll did not lock, trying to restore old rate %lu\n",
			__func__, old_rate);
		meson_hdmi_pll_set_rate(hw, old_rate, parent_rate);
	}

	return ret;
}

const struct clk_ops meson_hdmi_pll_ops = {
	.recalc_rate	= meson_hdmi_pll_recalc_rate,
	.round_rate	= meson_hdmi_pll_round_rate,
	.set_rate	= meson_hdmi_pll_set_rate,
};
