/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright (c) 2017 AmLogic, Inc.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING
 *
 * BSD LICENSE
 *
 * Copyright (c) 2017 AmLogic, Inc.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Baylibre nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Audio clock divider: The algorythm of clk-divider used with a very precise
 * source clock such as the mpll tends to select low divider factor. This gives
 * very poor results with this particular divider especially with high
 * frequencies ( > 100 MHz)
 *
 * This driver try to select the maximum possible divider with the rate the mpll
 * can provide.
 */

#include <linux/clk-provider.h>
#include "clkc.h"

#define to_meson_clk_audio_divider(_hw) container_of(_hw, \
				struct meson_clk_audio_divider, hw)

static int _div_round(unsigned long parent_rate, unsigned long rate,
		      unsigned long flags)
{
	if (flags & CLK_DIVIDER_ROUND_CLOSEST)
		return DIV_ROUND_CLOSEST_ULL((u64)parent_rate, rate);

	return DIV_ROUND_UP_ULL((u64)parent_rate, rate);
}

static int _get_val(unsigned long parent_rate, unsigned long rate)
{
	return DIV_ROUND_UP_ULL((u64)parent_rate, rate) - 1;
}

static int _valid_divider(struct clk_hw *hw, int divider)
{
	struct meson_clk_audio_divider *adiv =
		to_meson_clk_audio_divider(hw);
	int max_divider;
	u8 width;

	width = adiv->div.width;
	max_divider = 1 << width;

	if (divider < 1)
		return 1;
	else if (divider > max_divider)
		return max_divider;

	return divider;
}

static unsigned long audio_divider_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct meson_clk_audio_divider *adiv =
		to_meson_clk_audio_divider(hw);
	struct parm *p;
	unsigned long reg, divider;

	p = &adiv->div;
	reg = readl(adiv->base + p->reg_off);
	divider = PARM_GET(p->width, p->shift, reg) + 1;

	return DIV_ROUND_UP_ULL((u64)parent_rate, divider);
}

static long audio_divider_round_rate(struct clk_hw *hw,
				     unsigned long rate,
				     unsigned long *parent_rate)
{
	struct meson_clk_audio_divider *adiv =
		to_meson_clk_audio_divider(hw);
	unsigned long max_prate;
	int divider;

	if (!(clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT)) {
		divider = _div_round(*parent_rate, rate, adiv->flags);
		divider = _valid_divider(hw, divider);
		return DIV_ROUND_UP_ULL((u64)*parent_rate, divider);
	}

	/* Get the maximum parent rate */
	max_prate = clk_hw_round_rate(clk_hw_get_parent(hw), ULONG_MAX);

	/* Get the corresponding rounded down divider */
	divider = max_prate / rate;
	divider = _valid_divider(hw, divider);

	/* Get actual rate of the parent */
	*parent_rate = clk_hw_round_rate(clk_hw_get_parent(hw),
					 divider * rate);

	return DIV_ROUND_UP_ULL((u64)*parent_rate, divider);
}

static int audio_divider_set_rate(struct clk_hw *hw,
				  unsigned long rate,
				  unsigned long parent_rate)
{
	struct meson_clk_audio_divider *adiv =
		to_meson_clk_audio_divider(hw);
	struct parm *p;
	unsigned long reg, flags = 0;
	int val;

	val = _get_val(parent_rate, rate);

	if (adiv->lock)
		spin_lock_irqsave(adiv->lock, flags);
	else
		__acquire(adiv->lock);

	p = &adiv->div;
	reg = readl(adiv->base + p->reg_off);
	reg = PARM_SET(p->width, p->shift, reg, val);
	writel(reg, adiv->base + p->reg_off);

	if (adiv->lock)
		spin_unlock_irqrestore(adiv->lock, flags);
	else
		__release(adiv->lock);

	return 0;
}

const struct clk_ops meson_clk_audio_divider_ro_ops = {
	.recalc_rate	= audio_divider_recalc_rate,
	.round_rate	= audio_divider_round_rate,
};

const struct clk_ops meson_clk_audio_divider_ops = {
	.recalc_rate	= audio_divider_recalc_rate,
	.round_rate	= audio_divider_round_rate,
	.set_rate	= audio_divider_set_rate,
};
