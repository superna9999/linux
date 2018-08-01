/*
 * Copyright (C) 2018 Maxime Jourdan
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 * Copyright (C) 2014 Endless Mobile
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

#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/soc/amlogic/meson-canvas.h>
#include <asm/io.h>

#define NUM_CANVAS 256

/* DMC Registers */
#define DMC_CAV_LUT_DATAL	0x48 /* 0x12 offset in data sheet */
	#define CANVAS_WIDTH_LBIT	29
	#define CANVAS_WIDTH_LWID       3
#define DMC_CAV_LUT_DATAH	0x4c /* 0x13 offset in data sheet */
	#define CANVAS_WIDTH_HBIT       0
	#define CANVAS_HEIGHT_BIT       9
	#define CANVAS_BLKMODE_BIT      24
#define DMC_CAV_LUT_ADDR	0x50 /* 0x14 offset in data sheet */
	#define CANVAS_LUT_WR_EN        (0x2 << 8)
	#define CANVAS_LUT_RD_EN        (0x1 << 8)

struct meson_canvas {
	struct device *dev;
	struct regmap *regmap_dmc;
	struct mutex lock;
	u8 used[NUM_CANVAS];
};

static struct meson_canvas canvas = { 0 };

static int meson_canvas_setup(uint8_t canvas_index, uint32_t addr,
			uint32_t stride, uint32_t height,
			unsigned int wrap,
			unsigned int blkmode,
			unsigned int endian)
{
	struct regmap *regmap = canvas.regmap_dmc;
	u32 val;

	mutex_lock(&canvas.lock);

	if (!canvas.used[canvas_index]) {
		dev_err(canvas.dev,
			"Trying to setup non allocated canvas %u\n",
			canvas_index);
		mutex_unlock(&canvas.lock);
		return -EINVAL;
	}

	regmap_write(regmap, DMC_CAV_LUT_DATAL,
		((addr + 7) >> 3) |
		(((stride + 7) >> 3) << CANVAS_WIDTH_LBIT));

	regmap_write(regmap, DMC_CAV_LUT_DATAH,
		((((stride + 7) >> 3) >> CANVAS_WIDTH_LWID) <<
						CANVAS_WIDTH_HBIT) |
		(height << CANVAS_HEIGHT_BIT) |
		(wrap << 22) |
		(blkmode << CANVAS_BLKMODE_BIT) |
		(endian << 26));

	regmap_write(regmap, DMC_CAV_LUT_ADDR,
		CANVAS_LUT_WR_EN | canvas_index);

	/* Force a read-back to make sure everything is flushed. */
	regmap_read(regmap, DMC_CAV_LUT_DATAH, &val);
	mutex_unlock(&canvas.lock);

	return 0;
}

static int meson_canvas_alloc(uint8_t *canvas_index)
{
	int i;

	mutex_lock(&canvas.lock);
	for (i = 0; i < NUM_CANVAS; ++i) {
		if (!canvas.used[i]) {
			canvas.used[i] = 1;
			mutex_unlock(&canvas.lock);
			*canvas_index = i;
			return 0;
		}
	}
	mutex_unlock(&canvas.lock);
	dev_err(canvas.dev, "No more canvas available\n");

	return -ENODEV;
}

static int meson_canvas_free(uint8_t canvas_index)
{
	mutex_lock(&canvas.lock);
	if (!canvas.used[canvas_index]) {
		dev_err(canvas.dev,
			"Trying to free unused canvas %u\n", canvas_index);
		mutex_unlock(&canvas.lock);
		return -EINVAL;
	}
	canvas.used[canvas_index] = 0;
	mutex_unlock(&canvas.lock);

	return 0;
}

static struct meson_canvas_platform_data canvas_platform_data = {
	.alloc = meson_canvas_alloc,
	.free = meson_canvas_free,
	.setup = meson_canvas_setup,
};

static int meson_canvas_probe(struct platform_device *pdev)
{
	struct regmap *regmap_dmc;
	struct device *dev;

	dev = &pdev->dev;

	regmap_dmc = syscon_node_to_regmap(of_get_parent(dev->of_node));
	if (IS_ERR(regmap_dmc)) {
		dev_err(&pdev->dev, "failed to get DMC regmap\n");
		return PTR_ERR(regmap_dmc);
	}

	canvas.dev = dev;
	canvas.regmap_dmc = regmap_dmc;
	mutex_init(&canvas.lock);

	dev->platform_data = &canvas_platform_data;

	return 0;
}

static int meson_canvas_remove(struct platform_device *pdev)
{
	mutex_destroy(&canvas.lock);
	return 0;
}

static const struct of_device_id canvas_dt_match[] = {
	{ .compatible = "amlogic,meson-canvas" },
	{}
};
MODULE_DEVICE_TABLE(of, canvas_dt_match);

static struct platform_driver meson_canvas_driver = {
	.probe = meson_canvas_probe,
	.remove = meson_canvas_remove,
	.driver = {
		.name = "meson-canvas",
		.of_match_table = canvas_dt_match,
	},
};
module_platform_driver(meson_canvas_driver);

MODULE_ALIAS("platform:meson-canvas");
MODULE_DESCRIPTION("AMLogic Meson Canvas driver");
MODULE_AUTHOR("Maxime Jourdan <maxi.jourdan@wanadoo.fr>");
MODULE_LICENSE("GPL v2");
