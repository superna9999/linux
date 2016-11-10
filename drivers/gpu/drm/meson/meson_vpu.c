/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/of.h>
#include <drm/drmP.h>
#include "meson_drv.h"
#include "meson_vpu.h"
#include "meson_registers.h"

/*
 * VPU Handles the Global Video Processing, it includes management of the
 * clocks gates, blocks reset lines and power domains.
 *
 * We handle the following :
 * - Full reset of entire video processing HW blocks
 * - Setup of the VPU Clock
 *
 * What is missing :
 * - Bus clock gates
 * - Scaling of the VPU clock
 * - Powering up video processing HW blocks
 * - Powering Up HDMI controller and PHY
 */

static int meson_vpu_setclk(struct meson_drm *priv)
{
	int ret;

	ret = clk_set_parent(priv->clk_vpu0, priv->clk_fclk_div);
	if (WARN_ON(ret))
		return ret;

	ret = clk_set_parent(priv->clk_vpu, priv->clk_vpu0);
	if (WARN_ON(ret))
		return ret;

	ret = clk_set_rate(priv->clk_vpu, clk_get_rate(priv->clk_fclk_div));
	if (WARN_ON(ret))
		return ret;

	ret = clk_enable(priv->clk_vpu);
	if (WARN_ON(ret))
		return ret;

	return 0;
}

/*
 * Optional pipeline reset
 */
static int meson_vpu_reset(struct meson_drm *priv)
{
	struct device *dev = &priv->pdev->dev;
	struct device_node *np = dev->of_node;
	struct reset_control *rstc;
	int count;
	int ret;
	int i;

	count = of_count_phandle_with_args(np, "resets", "#reset-cells");
	for (i = 0 ; i < count ; ++i) {
		rstc = of_reset_control_get_exclusive_by_index(np, i);
		if (IS_ERR(rstc)) {
			dev_err(dev, "%s: Failed to get reset %d\n",
				__func__, i);
			return PTR_ERR(rstc);
		}

		ret = reset_control_reset(rstc);
		reset_control_put(rstc);

		if (ret) {
			dev_err(dev, "%s: Failed to get reset %d\n",
				__func__, i);
			return ret;
		}
	}

	return 0;
}

int meson_vpu_init(struct meson_drm *priv)
{
	int ret;

	ret = meson_vpu_setclk(priv);
	if (ret)
		return ret;

	return meson_vpu_reset(priv);
}
