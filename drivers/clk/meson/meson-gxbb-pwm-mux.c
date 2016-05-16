/*
 * Copyright (c) 2016 BayLibre, Inc.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
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
#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

static const char * const pwm_parents[] = {
	"xtal", "vid_pll", "fclk_div4", "fclk_div3", NULL
};

#define MUX_COUNT		2
#define MISC_CLK_SEL_WIDTH	2

static const unsigned int pwm_reg_shifts[MUX_COUNT] = {4, 6};

struct meson_gxbb_pwm_mux_data {
	void __iomem *base;
	struct clk *clks[MUX_COUNT];
	struct clk_onecell_data of_data;
	spinlock_t lock;
};

static const struct of_device_id meson_gxbb_pwm_mux_matches[] = {
	{ .compatible = "amlogic,meson-gxbb-pwm-mux", },
	{},
};
MODULE_DEVICE_TABLE(of, meson_gxbb_pwm_mux_matches);

static int meson_gxbb_pwm_mux_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct meson_gxbb_pwm_mux_data *data;
	const char *clk_name;
	struct clk *clk;
	int ret, i;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	data->base = of_iomap(dev->of_node, 0);
	if (!data->base)
		return -ENXIO;

	for (i = 0 ; i < MUX_COUNT ; ++i) {
		ret = of_property_read_string_index(dev->of_node,
						  "clock-output-names",
						  i, &clk_name);
		if (ret) {
			dev_err(dev, "Unable to get clock-output-name(%d)\n",
				i);
			goto err_clks;
		}

		clk = clk_register_mux(dev, clk_name,
					pwm_parents, 1 << MISC_CLK_SEL_WIDTH,
					0,
					data->base, pwm_reg_shifts[i],
					MISC_CLK_SEL_WIDTH,
					0, &data->lock);
		if (IS_ERR(clk)) {
			dev_err(dev, "Failed to register %s\n", clk_name);
			ret = PTR_ERR(clk);
			goto err_clks;
		}

		data->clks[i] = clk;
	}

	data->of_data.clk_num = MUX_COUNT;
	data->of_data.clks = data->clks;

	ret = of_clk_add_provider(dev->of_node, of_clk_src_onecell_get,
				  &data->of_data);
	if (ret)
		goto err_clks;

	platform_set_drvdata(pdev, data);

	return 0;

err_clks:
	for (i = 0 ; i < MUX_COUNT ; ++i)
		if (data->clks[i])
			clk_put(data->clks[i]);

	return ret;
}

static int meson_gxbb_pwm_mux_remove(struct platform_device *pdev)
{
	struct meson_gxbb_pwm_mux_data *data = platform_get_drvdata(pdev);
	int i;

	of_clk_del_provider(pdev->dev.of_node);

	for (i = 0 ; i < MUX_COUNT ; ++i)
		clk_put(data->clks[i]);

	return 0;
}

static struct platform_driver meson_gxbb_pwm_mux_driver = {
	.driver		= {
		.name	= "meson-gxbb-pwm-mux",
		.of_match_table = of_match_ptr(meson_gxbb_pwm_mux_matches),
	},
	.probe		= meson_gxbb_pwm_mux_probe,
	.remove		= meson_gxbb_pwm_mux_remove,
};
module_platform_driver(meson_gxbb_pwm_mux_driver);

MODULE_ALIAS("platform:meson-gxbb-pwm-mux");
