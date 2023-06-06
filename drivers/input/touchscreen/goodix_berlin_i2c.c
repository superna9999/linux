// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Goodix Berlin Touchscreen Driver
 *
 * Copyright (C) 2020 - 2021 Goodix, Inc.
 * Copyright (C) 2023 Linaro Ltd.
 *
 * Based on goodix_ts_berlin driver.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <asm/unaligned.h>

#include "goodix_berlin.h"

#define I2C_MAX_TRANSFER_SIZE		256

static const struct regmap_config goodix_berlin_i2c_regmap_conf = {
	.reg_bits = 32,
	.val_bits = 8,
	.max_raw_read = I2C_MAX_TRANSFER_SIZE,
	.max_raw_write = I2C_MAX_TRANSFER_SIZE,
};

static const struct input_id goodix_berlin_i2c_input_id = {
	.bustype = BUS_I2C,
	.vendor = 0x0416,
	.product = 0x1001,
};

static int goodix_berlin_i2c_probe(struct i2c_client *client)
{
	struct regmap *map;

	map = devm_regmap_init_i2c(client, &goodix_berlin_i2c_regmap_conf);
	if (IS_ERR(map))
		return PTR_ERR(map);

	return goodix_berlin_probe(&client->dev, client->irq,
				   &goodix_berlin_i2c_input_id, map);
}

static void goodix_berlin_i2c_remove(struct i2c_client *client)
{
	goodix_berlin_remove(&client->dev);
}

static const struct i2c_device_id goodix_berlin_i2c_id[] = {
	{ "gt9916", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, goodix_berlin_i2c_id);

static const struct of_device_id goodix_berlin_i2c_of_match[] = {
	{ .compatible = "goodix,gt9916", },
	{ },
};

static struct i2c_driver goodix_berlin_i2c_driver = {
	.driver = {
		.name = "goodix-berlin-i2c",
		.of_match_table = goodix_berlin_i2c_of_match,
		.pm = pm_sleep_ptr(&goodix_berlin_pm_ops),
	},
	.probe = goodix_berlin_i2c_probe,
	.remove = goodix_berlin_i2c_remove,
	.id_table = goodix_berlin_i2c_id,
};
module_i2c_driver(goodix_berlin_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Goodix Berlin I2C Touchscreen driver");
MODULE_AUTHOR("Neil Armstrong <neil.armstrong@linaro.org>");
