// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023 Linaro Ltd.
 */

#include <linux/bitmap.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pci-pwrseq.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>

struct pci_pwrseq_qca6390_vreg {
	const char *name;
	unsigned int load_uA;
};

struct pci_pwrseq_qca6390_pdata {
	struct pci_pwrseq_qca6390_vreg *vregs;
	size_t num_vregs;
	unsigned int delay_msec;
};

struct pci_pwrseq_qca6390_ctx {
	struct pci_pwrseq pwrseq;
	const struct pci_pwrseq_qca6390_pdata *pdata;
	struct regulator_bulk_data *regs;
	struct gpio_descs *en_gpios;
	unsigned long *en_gpios_values;
	struct clk *clk;
};

static struct pci_pwrseq_qca6390_vreg pci_pwrseq_qca6390_vregs[] = {
	{
		.name = "vddio",
		.load_uA = 20000,
	},
	{
		.name = "vddaon",
		.load_uA = 100000,
	},
	{
		.name = "vddpmu",
		.load_uA = 1250000,
	},
	{
		.name = "vddpcie1",
		.load_uA = 35000,
	},
	{
		.name = "vddpcie2",
		.load_uA = 15000,
	},
	{
		.name = "vddrfa1",
		.load_uA = 200000,
	},
	{
		.name = "vddrfa2",
		.load_uA = 400000,
	},
	{
		.name = "vddrfa3",
		.load_uA = 400000,
	},
};

static struct pci_pwrseq_qca6390_pdata pci_pwrseq_qca6390_of_data = {
	.vregs = pci_pwrseq_qca6390_vregs,
	.num_vregs = ARRAY_SIZE(pci_pwrseq_qca6390_vregs),
	.delay_msec = 16,
};

static struct pci_pwrseq_qca6390_vreg pci_pwrseq_wcn7850_vregs[] = {
	{
		.name = "vdd",
	},
	{
		.name = "vddio",
	},
	{
		.name = "vddio12",
	},
	{
		.name = "vddaon",
	},
	{
		.name = "vdddig",
	},
	{
		.name = "vddrfa1",
	},
	{
		.name = "vddrfa2",
	},
};

static struct pci_pwrseq_qca6390_pdata pci_pwrseq_wcn7850_of_data = {
	.vregs = pci_pwrseq_wcn7850_vregs,
	.num_vregs = ARRAY_SIZE(pci_pwrseq_wcn7850_vregs),
	.delay_msec = 50,
};

static int pci_pwrseq_qca6390_power_on(struct pci_pwrseq_qca6390_ctx *ctx)
{
	int ret;

	ret = regulator_bulk_enable(ctx->pdata->num_vregs, ctx->regs);
	if (ret)
		return ret;

	ret = clk_prepare_enable(ctx->clk);
	if (ret)
		return ret;

	bitmap_fill(ctx->en_gpios_values, ctx->en_gpios->ndescs);

	ret = gpiod_set_array_value_cansleep(ctx->en_gpios->ndescs,
					     ctx->en_gpios->desc,
					     ctx->en_gpios->info,
					     ctx->en_gpios_values);
	if (ret) {
		regulator_bulk_disable(ctx->pdata->num_vregs, ctx->regs);
		return ret;
	}

	if (ctx->pdata->delay_msec)
		msleep(ctx->pdata->delay_msec);

	return 0;
}

static int pci_pwrseq_qca6390_power_off(struct pci_pwrseq_qca6390_ctx *ctx)
{
	int ret;

	bitmap_zero(ctx->en_gpios_values, ctx->en_gpios->ndescs);

	ret = gpiod_set_array_value_cansleep(ctx->en_gpios->ndescs,
					     ctx->en_gpios->desc,
					     ctx->en_gpios->info,
					     ctx->en_gpios_values);
	if (ret)
		return ret;

	clk_disable_unprepare(ctx->clk);

	return regulator_bulk_disable(ctx->pdata->num_vregs, ctx->regs);
}

static void devm_pci_pwrseq_qca6390_power_off(void *data)
{
	struct pci_pwrseq_qca6390_ctx *ctx = data;

	pci_pwrseq_qca6390_power_off(ctx);
}

static int pci_pwrseq_qca6390_probe(struct platform_device *pdev)
{
	struct pci_pwrseq_qca6390_ctx *ctx;
	struct device *dev = &pdev->dev;
	int ret, i;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->pdata = of_device_get_match_data(dev);
	if (!ctx->pdata)
		return dev_err_probe(dev, -ENODEV,
				     "Failed to obtain platform data\n");

	if (ctx->pdata->vregs) {
		ctx->regs = devm_kcalloc(dev, ctx->pdata->num_vregs,
					 sizeof(*ctx->regs), GFP_KERNEL);
		if (!ctx->regs)
			return -ENOMEM;

		for (i = 0; i < ctx->pdata->num_vregs; i++)
			ctx->regs[i].supply = ctx->pdata->vregs[i].name;

		ret = devm_regulator_bulk_get(dev, ctx->pdata->num_vregs,
					      ctx->regs);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "Failed to get all regulators\n");

		for (i = 0; i < ctx->pdata->num_vregs; i++) {
			if (!ctx->pdata->vregs[1].load_uA)
				continue;

			ret = regulator_set_load(ctx->regs[i].consumer,
						 ctx->pdata->vregs[i].load_uA);
			if (ret)
				return dev_err_probe(dev, ret,
						     "Failed to set vreg load\n");
		}
	}

	ctx->clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(ctx->clk))
		return dev_err_probe(dev, PTR_ERR(ctx->clk),
				     "Failed to get clock\n");

	ctx->en_gpios = devm_gpiod_get_array_optional(dev, "enable",
						      GPIOD_OUT_LOW);
	if (IS_ERR(ctx->en_gpios))
		return dev_err_probe(dev, PTR_ERR(ctx->en_gpios),
				     "Failed to get enable GPIOs\n");

	ctx->en_gpios_values = devm_bitmap_zalloc(dev, ctx->en_gpios->ndescs,
						  GFP_KERNEL);
	if (!ctx->en_gpios_values)
		return -ENOMEM;

	ret = pci_pwrseq_qca6390_power_on(ctx);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to power on the device\n");

	ret = devm_add_action_or_reset(dev, devm_pci_pwrseq_qca6390_power_off,
				       ctx);
	if (ret)
		return ret;

	ctx->pwrseq.dev = dev;

	ret = devm_pci_pwrseq_device_enable(dev, &ctx->pwrseq);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register the pwrseq wrapper\n");

	return 0;
}

static const struct of_device_id pci_pwrseq_qca6390_of_match[] = {
	{
		.compatible = "pci17cb,1101",
		.data = &pci_pwrseq_qca6390_of_data,
	},
	{
		.compatible = "pci17cb,1107",
		.data = &pci_pwrseq_wcn7850_of_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, pci_pwrseq_qca6390_of_match);

static struct platform_driver pci_pwrseq_qca6390_driver = {
	.driver = {
		.name = "pci-pwrseq-qca6390",
		.of_match_table = pci_pwrseq_qca6390_of_match,
	},
	.probe = pci_pwrseq_qca6390_probe,
};
module_platform_driver(pci_pwrseq_qca6390_driver);

MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@linaro.org>");
MODULE_DESCRIPTION("PCI Power Sequencing module for QCA6390");
MODULE_LICENSE("GPL");
