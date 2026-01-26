// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on upd720201.c:
 * Copyright (C) 2024 Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pci-pwrctrl.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

struct pci_pwrctrl_upd720201_data {
	struct pci_pwrctrl ctx;
	struct regulator_bulk_data *supplies;
	int num_supplies;
};

static void devm_pci_pwrctrl_upd720201_power_off(void *data)
{
	struct pci_pwrctrl_upd720201_data *upd720201 = data;

	regulator_bulk_disable(upd720201->num_supplies, upd720201->supplies);
	regulator_bulk_free(upd720201->num_supplies, upd720201->supplies);
}

static int pci_pwrctrl_upd720201_probe(struct platform_device *pdev)
{
	struct pci_pwrctrl_upd720201_data *upd720201;
	struct device *dev = &pdev->dev;
	int ret;

	upd720201 = devm_kzalloc(dev, sizeof(*upd720201), GFP_KERNEL);
	if (!upd720201)
		return -ENOMEM;

	ret = of_regulator_bulk_get_all(dev, dev_of_node(dev),
					&upd720201->supplies);
	if (ret < 0) {
		dev_err_probe(dev, ret, "Failed to get upd720201 regulators\n");
		return ret;
	}

	upd720201->num_supplies = ret;
	ret = regulator_bulk_enable(upd720201->num_supplies, upd720201->supplies);
	if (ret < 0) {
		dev_err_probe(dev, ret, "Failed to enable upd720201 regulators\n");
		regulator_bulk_free(upd720201->num_supplies, upd720201->supplies);
		return ret;
	}

	ret = devm_add_action_or_reset(dev, devm_pci_pwrctrl_upd720201_power_off,
				       upd720201);
	if (ret)
		return ret;

	pci_pwrctrl_init(&upd720201->ctx, dev);

	ret = devm_pci_pwrctrl_device_set_ready(dev, &upd720201->ctx);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register pwrctrl driver\n");

	return 0;
}

static const struct of_device_id pci_pwrctrl_upd720201_of_match[] = {
	{
		.compatible = "pci1912,0014",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, pci_pwrctrl_upd720201_of_match);

static struct platform_driver pci_pwrctrl_upd720201_driver = {
	.driver = {
		.name = "pci-pwrctrl-upd720201",
		.of_match_table = pci_pwrctrl_upd720201_of_match,
	},
	.probe = pci_pwrctrl_upd720201_probe,
};
module_platform_driver(pci_pwrctrl_upd720201_driver);

MODULE_AUTHOR("Neil Armstrong <neil.armstrong@linaro.org>");
MODULE_DESCRIPTION("PCI Power Control driver for UPD720201 USB3 Host Controller");
MODULE_LICENSE("GPL");
