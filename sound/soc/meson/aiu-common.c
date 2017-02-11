/*
 * Copyright (C) 2017 BayLibre, SAS
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>


static const char* aiu_gate_names[] = { "aiu_top", "aiu_glue" };

static int aiu_common_register_clk_gates(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk *gate;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(aiu_gate_names); i++) {
		gate = devm_clk_get(dev, aiu_gate_names[i]);
		if (IS_ERR(gate)) {
			if (PTR_ERR(gate) != -EPROBE_DEFER)
				dev_err(dev, "Failed to get %s clock gate\n",
					aiu_gate_names[i]);
			return PTR_ERR(gate);
		}

		ret = clk_prepare_enable(gate);
		if (ret) {
			dev_err(dev, "failed to enable %s clock gate\n",
				aiu_gate_names[i]);
			return ret;
		}
	}

	return 0;
}

int aiu_common_register(struct platform_device *pdev)
{
	int ret;

	ret = aiu_common_register_clk_gates(pdev);
	if (ret)
		return ret;

	/* FIXME: We should also handle the AIU reset here */

	return 0;
}
EXPORT_SYMBOL_GPL(aiu_common_register);

MODULE_DESCRIPTION("Meson AIU common helper");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
