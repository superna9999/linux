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

#include <linux/clk-provider.h>

#include "clk-regmap.h"
#include "clk-phase-delay.h"

static inline struct meson_clk_phase_delay_data *
meson_clk_get_phase_delay_data(struct clk_regmap *clk)
{
	return clk->data;
}

static int meson_clk_phase_delay_get_phase(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_phase_delay_data *ph;
	unsigned long period_ps, p, d;
	int degrees;

	ph = meson_clk_get_phase_delay_data(clk);
	p = meson_parm_read(clk->map, &ph->phase);
	degrees = p * 360 / (1 << (ph->phase.width));

	period_ps = DIV_ROUND_UP_ULL(NSEC_PER_SEC * 1000ull,
				     clk_hw_get_rate(hw));

	d = meson_parm_read(clk->map, &ph->delay);
	degrees += d * ph->delay_step_ps * 360 / period_ps;
	degrees %= 360;

	return degrees;
}

static int meson_clk_phase_delay_set_phase(struct clk_hw *hw, int degrees)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_phase_delay_data *ph;
	unsigned long period_ps, d = 0;
	unsigned int p;

	ph = meson_clk_get_phase_delay_data(clk);
	period_ps = DIV_ROUND_UP_ULL(NSEC_PER_SEC * 1000ull,
				     clk_hw_get_rate(hw));

	/*
	 * First compute the phase index (p), the remainder (r) is the
	 * part we'll try to acheive using the delays (d).
	 */
	p = 360 / 1 << (ph->phase.width);
	degrees = degrees / p;
	d = DIV_ROUND_CLOSEST((degrees % p) * period_ps,
			      360 * ph->delay_step_ps);
	d = min(d, PMASK(ph->delay.width));

	meson_parm_write(clk->map, &ph->phase, degrees);
	meson_parm_write(clk->map, &ph->delay, d);
	return 0;
}

const struct clk_ops meson_clk_phase_delay_ops = {
	.get_phase = meson_clk_phase_delay_get_phase,
	.set_phase = meson_clk_phase_delay_set_phase,
};
EXPORT_SYMBOL_GPL(meson_clk_phase_delay_ops);
