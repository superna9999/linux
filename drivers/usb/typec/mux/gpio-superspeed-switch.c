// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Linaro Ltd.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/usb/typec_mux.h>

struct gpio_ss_switch {
	struct gpio_desc *enable_gpio;
	struct gpio_desc *select_gpio;

	struct typec_switch_dev *sw;

	bool enabled;
	bool swapped;
};

static int gpio_ss_switch_set(struct typec_switch_dev *sw,
			      enum typec_orientation orientation)
{
	struct gpio_ss_switch *gpio_ss_switch = typec_switch_get_drvdata(sw);
	bool enabled;
	bool swapped;

	enabled = gpio_ss_switch->enabled;
	swapped = gpio_ss_switch->swapped;

	switch (orientation) {
	case TYPEC_ORIENTATION_NONE:
		enabled = false;
		break;
	case TYPEC_ORIENTATION_NORMAL:
		swapped = false;
		break;
	case TYPEC_ORIENTATION_REVERSE:
		swapped = true;
		break;
	}

	if (enabled != gpio_ss_switch->enabled)
		gpiod_set_value_cansleep(gpio_ss_switch->enable_gpio, enabled);

	if (swapped != gpio_ss_switch->swapped)
		gpiod_set_value_cansleep(gpio_ss_switch->select_gpio, swapped);

	gpio_ss_switch->enabled = enabled;
	gpio_ss_switch->swapped = swapped;

	return 0;
}

static int gpio_ss_switch_probe(struct platform_device *pdev)
{
	struct typec_switch_desc sw_desc = { };
	struct device *dev = &pdev->dev;
	struct gpio_ss_switch *gpio_ss_switch;

	gpio_ss_switch = devm_kzalloc(dev, sizeof(*gpio_ss_switch), GFP_KERNEL);
	if (!gpio_ss_switch)
		return -ENOMEM;

	gpio_ss_switch->enable_gpio = devm_gpiod_get_optional(dev, "enable",
							      GPIOD_OUT_LOW);
	if (IS_ERR(gpio_ss_switch->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(gpio_ss_switch->enable_gpio),
				     "unable to acquire enable gpio\n");

	gpio_ss_switch->select_gpio = devm_gpiod_get(dev, "select", GPIOD_OUT_LOW);
	if (IS_ERR(gpio_ss_switch->select_gpio))
		return dev_err_probe(dev, PTR_ERR(gpio_ss_switch->select_gpio),
				     "unable to acquire select gpio\n");

	sw_desc.drvdata = gpio_ss_switch;
	sw_desc.fwnode = dev_fwnode(dev);
	sw_desc.set = gpio_ss_switch_set;

	gpio_ss_switch->sw = typec_switch_register(dev, &sw_desc);
	if (IS_ERR(gpio_ss_switch->sw))
		return dev_err_probe(dev, PTR_ERR(gpio_ss_switch->sw),
				     "failed to register gpio_ss_switch switch\n");

	platform_set_drvdata(pdev, gpio_ss_switch);

	return 0;
}

static void gpio_ss_switch_remove(struct platform_device *pdev)
{
	struct gpio_ss_switch *gpio_ss_switch = platform_get_drvdata(pdev);

	gpiod_set_value_cansleep(gpio_ss_switch->enable_gpio, 0);

	typec_switch_unregister(gpio_ss_switch->sw);
}

static const struct of_device_id gpio_ss_switch_match[] = {
	{ .compatible = "gpio-superspeed-switch" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, gpio_ss_switch_match);

static struct platform_driver gpio_ss_switch_driver = {
	.probe = gpio_ss_switch_probe,
	.remove = gpio_ss_switch_remove,
	.driver = {
		.name = "gpio_ss_switch",
		.of_match_table = gpio_ss_switch_match,
	},
};
module_platform_driver(gpio_ss_switch_driver);

MODULE_DESCRIPTION("GPIO based USB TYPE-C SuperSpeed switch driver");
MODULE_LICENSE("GPL");
