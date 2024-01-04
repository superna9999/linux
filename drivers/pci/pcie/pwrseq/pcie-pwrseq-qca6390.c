// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023 Linaro Ltd.
 */

#include <linux/bitmap.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pcie-pwrseq.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>

struct pcie_pwrseq_qca6390_vreg {
	const char *name;
	unsigned int load_uA;
};

struct pcie_pwrseq_qca6390_pdata {
	struct pcie_pwrseq_qca6390_vreg *vregs;
	size_t num_vregs;
	unsigned int delay_msec;
};

struct pcie_pwrseq_qca6390_ctx {
	struct pcie_pwrseq pwrseq;
	const struct pcie_pwrseq_qca6390_pdata *pdata;
	struct regulator_bulk_data *regs;
	struct gpio_descs *en_gpios;
	unsigned long *en_gpios_values;
};

static struct pcie_pwrseq_qca6390_vreg pcie_pwrseq_qca6390_vregs[] = {
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
};

static struct pcie_pwrseq_qca6390_pdata pcie_pwrseq_qca6390_of_data = {
	.vregs = pcie_pwrseq_qca6390_vregs,
	.num_vregs = ARRAY_SIZE(pcie_pwrseq_qca6390_vregs),
	.delay_msec = 16,
};

static int pcie_pwrseq_qca6390_power_on(struct pcie_pwrseq_qca6390_ctx *ctx)
{
	int ret;

	ret = regulator_bulk_enable(ctx->pdata->num_vregs, ctx->regs);
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

static int pcie_pwrseq_qca6390_power_off(struct pcie_pwrseq_qca6390_ctx *ctx)
{
	int ret;

	bitmap_zero(ctx->en_gpios_values, ctx->en_gpios->ndescs);

	ret = gpiod_set_array_value_cansleep(ctx->en_gpios->ndescs,
					     ctx->en_gpios->desc,
					     ctx->en_gpios->info,
					     ctx->en_gpios_values);
	if (ret)
		return ret;

	return regulator_bulk_disable(ctx->pdata->num_vregs, ctx->regs);
}

static void devm_pcie_pwrseq_qca6390_power_off(void *data)
{
	struct pcie_pwrseq_qca6390_ctx *ctx = data;

	pcie_pwrseq_qca6390_power_off(ctx);
}

static int pcie_pwrseq_qca6309_probe(struct platform_device *pdev)
{
	struct pcie_pwrseq_qca6390_ctx *ctx;
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
			ret = regulator_set_load(ctx->regs[i].consumer,
						 ctx->pdata->vregs[i].load_uA);
			if (ret)
				return dev_err_probe(dev, ret,
						     "Failed to set vreg load\n");
		}
	}

	ctx->en_gpios = devm_gpiod_get_array_optional(dev, "enable",
						      GPIOD_OUT_LOW);
	if (IS_ERR(ctx->en_gpios))
		return dev_err_probe(dev, PTR_ERR(ctx->en_gpios),
				     "Failed to get enable GPIOs\n");

	ctx->en_gpios_values = devm_bitmap_zalloc(dev, ctx->en_gpios->ndescs,
						  GFP_KERNEL);
	if (!ctx->en_gpios_values)
		return -ENOMEM;

	ret = pcie_pwrseq_qca6390_power_on(ctx);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to power on the device\n");

	ret = devm_add_action_or_reset(dev, devm_pcie_pwrseq_qca6390_power_off,
				       ctx);
	if (ret)
		return ret;

	ctx->pwrseq.dev = dev;

	ret = devm_pcie_pwrseq_device_enable(dev, &ctx->pwrseq);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register the pwrseq wrapper\n");

	return 0;
}

static const struct of_device_id pcie_pwrseq_qca6309_of_match[] = {
	{
		.compatible = "pci17cb,1101",
		.data = &pcie_pwrseq_qca6390_of_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, pcie_pwrseq_qca6309_of_match);

static struct platform_driver pcie_pwrseq_qca6309_driver = {
	.driver = {
		.name = "pcie-pwrseq-qca6390",
		.of_match_table = pcie_pwrseq_qca6309_of_match,
	},
	.probe = pcie_pwrseq_qca6309_probe,
};
module_platform_driver(pcie_pwrseq_qca6309_driver);

MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@linaro.org>");
MODULE_DESCRIPTION("PCIe Power Sequencing module for QCA6390");
MODULE_LICENSE("GPL");
