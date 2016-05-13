/*
 * Copyright (c) 2016 BayLibre, Inc.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * This file is dual-licensed: you can use it either under the terms
 * of the GPL or the X11 license, at your option. Note that this dual
 * licensing only applies to this file, and not this project as a
 * whole.
 *
 *  a) This library is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of the
 *     License, or (at your option) any later version.
 *
 *     This library is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 * Or, alternatively,
 *
 *  b) Permission is hereby granted, free of charge, to any person
 *     obtaining a copy of this software and associated documentation
 *     files (the "Software"), to deal in the Software without
 *     restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or
 *     sell copies of the Software, and to permit persons to whom the
 *     Software is furnished to do so, subject to the following
 *     conditions:
 *
 *     The above copyright notice and this permission notice shall be
 *     included in all copies or substantial portions of the Software.
 *
 *     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *     EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *     OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *     NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *     HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *     WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *     FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *     OTHER DEALINGS IN THE SOFTWARE.
 */
#include <linux/err.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/types.h>

#define REG_COUNT	8
#define BITS_PER_REG	32

struct meson_gxbb_reset {
	void __iomem *reg_base;
	struct reset_controller_dev rcdev;
};

static int meson_gxbb_reset_reset(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct meson_gxbb_reset *data =
		container_of(rcdev, struct meson_gxbb_reset, rcdev);
	unsigned int bank = id / BITS_PER_REG;
	unsigned int offset = id % BITS_PER_REG;
	void *reg_addr = data->reg_base + (bank << 2);

	if (bank >= REG_COUNT)
		return -EINVAL;

	writel(BIT(offset), reg_addr);

	return 0;
}

static const struct reset_control_ops meson_gxbb_reset_ops = {
	.reset		= meson_gxbb_reset_reset,
};

static const struct of_device_id meson_gxbb_reset_dt_ids[] = {
	 { .compatible = "amlogic,meson-gxbb-reset", },
	 { /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, meson_gxbb_reset_dt_ids);

static int meson_gxbb_reset_probe(struct platform_device *pdev)
{
	struct meson_gxbb_reset *data;
	struct resource *res;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->reg_base))
		return PTR_ERR(data->reg_base);

	platform_set_drvdata(pdev, data);

	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = REG_COUNT * BITS_PER_REG;
	data->rcdev.ops = &meson_gxbb_reset_ops;
	data->rcdev.of_node = pdev->dev.of_node;

	return reset_controller_register(&data->rcdev);
}

static int meson_gxbb_reset_remove(struct platform_device *pdev)
{
	struct meson_gxbb_reset *data = platform_get_drvdata(pdev);

	reset_controller_unregister(&data->rcdev);

	return 0;
}

static struct platform_driver meson_gxbb_reset_driver = {
	.probe	= meson_gxbb_reset_probe,
	.remove	= meson_gxbb_reset_remove,
	.driver = {
		.name		= "meson_gxbb_reset",
		.of_match_table	= meson_gxbb_reset_dt_ids,
	},
};

module_platform_driver(meson_gxbb_reset_driver);
