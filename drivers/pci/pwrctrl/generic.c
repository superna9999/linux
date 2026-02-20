// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/pci-pwrctrl.h>
#include <linux/platform_device.h>
#include <linux/pwrseq/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

struct generic_pwrctrl {
	struct pci_pwrctrl pwrctrl;
	struct regulator_bulk_data *supplies;
	int num_supplies;
	struct clk *clk;
	struct pwrseq_desc *pwrseq;
};

static int generic_pwrctrl_power_on(struct pci_pwrctrl *pwrctrl)
{
	struct generic_pwrctrl *generic =
				container_of(pwrctrl,
					     struct generic_pwrctrl, pwrctrl);
	int ret;

	if (generic->pwrseq) {
		pwrseq_power_on(generic->pwrseq);
		return 0;
	}

	ret = regulator_bulk_enable(generic->num_supplies, generic->supplies);
	if (ret < 0) {
		dev_err(generic->pwrctrl.dev, "Failed to enable generic regulators\n");
		return ret;
	}

	return clk_prepare_enable(generic->clk);
}

static int generic_pwrctrl_power_off(struct pci_pwrctrl *pwrctrl)
{
	struct generic_pwrctrl *generic =
				container_of(pwrctrl,
					     struct generic_pwrctrl, pwrctrl);

	if (generic->pwrseq) {
		pwrseq_power_off(generic->pwrseq);
		return 0;
	}

	regulator_bulk_disable(generic->num_supplies, generic->supplies);
	clk_disable_unprepare(generic->clk);

	return 0;
}

static void devm_generic_pwrctrl_release(void *data)
{
	struct generic_pwrctrl *generic = data;

	generic_pwrctrl_power_off(&generic->pwrctrl);
	regulator_bulk_free(generic->num_supplies, generic->supplies);
}

static int generic_pwrctrl_probe(struct platform_device *pdev)
{
	struct generic_pwrctrl *generic;
	struct device *dev = &pdev->dev;
	int ret;

	generic = devm_kzalloc(dev, sizeof(*generic), GFP_KERNEL);
	if (!generic)
		return -ENOMEM;

	if (of_graph_is_present(dev_of_node(dev))) {
		generic->pwrseq = devm_pwrseq_get(dev, "pcie");
		if (IS_ERR(generic->pwrseq))
			return dev_err_probe(dev, PTR_ERR(generic->pwrseq),
				     "Failed to get the power sequencer\n");

		goto skip_resources;
	}

	ret = of_regulator_bulk_get_all(dev, dev_of_node(dev),
					&generic->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get generic regulators\n");

	generic->num_supplies = ret;

	generic->clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(generic->clk))
		return dev_err_probe(dev, PTR_ERR(generic->clk),
				     "Failed to enable generic clock\n");

skip_resources:
	generic->pwrctrl.power_on = generic_pwrctrl_power_on;
	generic->pwrctrl.power_off = generic_pwrctrl_power_off;

	ret = devm_add_action_or_reset(dev, devm_generic_pwrctrl_release, generic);
	if (ret)
		return ret;

	pci_pwrctrl_init(&generic->pwrctrl, dev);

	ret = devm_pci_pwrctrl_device_set_ready(dev, &generic->pwrctrl);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register pwrctrl driver\n");

	return 0;
}

static const struct of_device_id generic_pwrctrl_of_match[] = {
	{
		.compatible = "pciclass,0604",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, generic_pwrctrl_of_match);

static struct platform_driver generic_pwrctrl_driver = {
	.driver = {
		.name = "pci-pwrctrl-generic",
		.of_match_table = generic_pwrctrl_of_match,
	},
	.probe = generic_pwrctrl_probe,
};
module_platform_driver(generic_pwrctrl_driver);

MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_DESCRIPTION("Generic PCI Power Control driver for PCI Slots and Endpoints");
MODULE_LICENSE("GPL");
