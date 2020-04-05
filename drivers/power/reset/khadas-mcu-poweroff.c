// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Khadas MCU Power Off driver
 *
 * Copyright (C) 2020 BayLibre SAS
 * Author(s): Neil Armstrong <narmstrong@baylibre.com>
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/mfd/khadas-mcu.h>
#include <linux/regmap.h>
#include <linux/slab.h>

struct khadas_mcu_poweroff {
	struct device *dev;
	struct khadas_mcu *mcu;
};

static struct khadas_mcu_poweroff *khadas_mcu_pm_poweroff;

static void khadas_mcu_pm_power_off(void)
{
	int ret;

	ret = regmap_write(khadas_mcu_pm_poweroff->mcu->map,
			   KHADAS_MCU_PWR_OFF_CMD_REG, 1);
	if (ret < 0)
		dev_err(khadas_mcu_pm_poweroff->dev,
			"PWR_OFF_CMD write failed, %d\n", ret);
}

static int khadas_mcu_poweroff_probe(struct platform_device *pdev)
{
	struct khadas_mcu_poweroff *khadas_mcu_poweroff;

	khadas_mcu_poweroff = devm_kzalloc(&pdev->dev,
					   sizeof(*khadas_mcu_poweroff),
					   GFP_KERNEL);
	if (!khadas_mcu_poweroff)
		return -ENOMEM;

	khadas_mcu_poweroff->mcu = dev_get_drvdata(pdev->dev.parent);
	khadas_mcu_poweroff->dev = &pdev->dev;
	khadas_mcu_pm_poweroff = khadas_mcu_poweroff;
	if (!pm_power_off)
		pm_power_off = khadas_mcu_pm_power_off;

	return 0;
}

static int khadas_mcu_poweroff_remove(struct platform_device *pdev)
{
	if (pm_power_off == khadas_mcu_pm_power_off)
		pm_power_off = NULL;
	khadas_mcu_pm_poweroff = NULL;

	return 0;
}

static const struct platform_device_id khadas_mcu_poweroff_id_table[] = {
	{ .name = "khadas-mcu-pwr-ctrl", },
	{},
};
MODULE_DEVICE_TABLE(platform, khadas_mcu_poweroff_id_table);

static struct platform_driver khadas_mcu_poweroff_driver = {
	.driver = {
		.name = "khadas-mcu-power-off",
	},
	.probe = khadas_mcu_poweroff_probe,
	.remove = khadas_mcu_poweroff_remove,
	.id_table = khadas_mcu_poweroff_id_table,
};

module_platform_driver(khadas_mcu_poweroff_driver);

MODULE_DESCRIPTION("Power off driver for Khadas MCU");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("GPL v2");
