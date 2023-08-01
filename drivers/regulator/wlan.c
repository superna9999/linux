// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/of_regulator.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/delay.h>

struct wlan_data {
	struct regulator_desc desc;
	struct regulator_dev *dev;
	struct regulator **regulators;
	unsigned int count;
	struct gpio_desc *enable;
	bool enabled;
};

static int regulator_wlan_enable(struct regulator_dev *rdev)
{
	struct wlan_data *priv = rdev_get_drvdata(rdev);
	int ret, i;

	if (priv->enabled)
		return 0;

	for (i = 0; i < priv->count; ++i) {
		ret = regulator_enable(priv->regulators[i]);
		if (ret < 0) {
			return ret;
		}

		msleep(20);
	}

	gpiod_set_value(priv->enable, 1);

	msleep(200);

	priv->enabled = true;

	return 0;
}

static int regulator_wlan_disable(struct regulator_dev *rdev)
{
	struct wlan_data *priv = rdev_get_drvdata(rdev);
	int ret, i;

	if (!priv->enabled)
		return 0;

	gpiod_set_value(priv->enable, 0);
	
	for (i = 0; i < priv->count; ++i) {
		ret = regulator_disable(priv->regulators[i]);
		if (ret < 0)
			return ret;
	}

	priv->enabled = false;

	return 0;
}

static int regulator_wlan_is_enabled(struct regulator_dev *rdev)
{
	struct wlan_data *priv = rdev_get_drvdata(rdev);

	return priv->enabled;
}

static const struct regulator_ops regulator_wlan_ops = {
	.enable = regulator_wlan_enable,
	.disable = regulator_wlan_disable,
	.is_enabled = regulator_wlan_is_enabled,
};

static const struct of_device_id regulator_wlan_of_match[] = {
	{ .compatible = "regulator-wlan" },
	{},
};
MODULE_DEVICE_TABLE(of, regulator_wlan_of_match);

static int regulator_wlan_probe(struct platform_device *pdev)
{
	struct regulator_config cfg = { };
	struct wlan_data *drvdata;
	int ret, i;
	u32 count;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct wlan_data),
			       GFP_KERNEL);
	if (drvdata == NULL)
		return -ENOMEM;

	drvdata->enable = devm_gpiod_get(&pdev->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(drvdata->enable))
		return dev_err_probe(&pdev->dev, PTR_ERR(drvdata->enable),
				     "Failed to obtain enable gpio\n");

	ret = device_property_read_u32(&pdev->dev, "supply-count", &count);
	if (ret < 0)
		return ret;
	if (count == 0)
		return -EINVAL;

	drvdata->regulators = devm_kmalloc_array(&pdev->dev, sizeof(struct regulator *),
						 count, GFP_KERNEL);

	for (i = 0; i < count; ++i) {
		char prop_name[16];
		snprintf(prop_name, 16, "vin%d", i);
		drvdata->regulators[i] = devm_regulator_get(&pdev->dev, prop_name);
		if (IS_ERR(drvdata->regulators[i]))
			return dev_err_probe(&pdev->dev, PTR_ERR(drvdata->regulators[i]),
					     "Failed to obtain supply '%d'\n", i);
	}
	drvdata->count = count;

	drvdata->desc.name = devm_kstrdup(&pdev->dev, dev_name(&pdev->dev), GFP_KERNEL);
	drvdata->desc.type = REGULATOR_VOLTAGE;
	drvdata->desc.owner = THIS_MODULE;
	drvdata->desc.ops = &regulator_wlan_ops;

	cfg.dev = &pdev->dev;
	cfg.init_data = of_get_regulator_init_data(&pdev->dev, pdev->dev.of_node, &drvdata->desc);
	cfg.driver_data = drvdata;
	cfg.of_node = pdev->dev.of_node;

	drvdata->dev = devm_regulator_register(&pdev->dev, &drvdata->desc, &cfg);
	if (IS_ERR(drvdata->dev)) {
		ret = dev_err_probe(&pdev->dev, PTR_ERR(drvdata->dev),
				    "Failed to register regulator: %ld\n",
				    PTR_ERR(drvdata->dev));
		return ret;
	}

	platform_set_drvdata(pdev, drvdata);

	return 0;
}

static struct platform_driver regulator_wlan_driver = {
	.probe		= regulator_wlan_probe,
	.driver		= {
		.name		= "wlan-regulator",
		.of_match_table = of_match_ptr(regulator_wlan_of_match),
	},
};

module_platform_driver(regulator_wlan_driver);

MODULE_DESCRIPTION("Wlan regulator");
MODULE_LICENSE("GPL");
