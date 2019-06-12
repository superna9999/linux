// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2017 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <dt-bindings/power/meson-sm1-power.h>

/* AO Offsets */

#define AO_RTI_GEN_PWR_SLEEP0		(0x3a << 2)
#define AO_RTI_GEN_PWR_ISO0		(0x3b << 2)

/* HHI Offsets */

#define HHI_MEM_PD_REG0			(0x40 << 2)
#define HHI_NANOQ_MEM_PD_REG0		(0x46 << 2)
#define HHI_NANOQ_MEM_PD_REG1		(0x47 << 2)

struct meson_sm1_pwrc;

struct meson_sm1_pwrc_mem_domain {
	unsigned int reg;
	unsigned int mask;
};

struct meson_sm1_pwrc_domain_desc {
	char *name;
	unsigned int sleep_reg;
	unsigned int sleep_bit;
	unsigned int iso_reg;
	unsigned int iso_bit;
	unsigned int mem_pd_count;
	struct meson_sm1_pwrc_mem_domain *mem_pd;
};

struct meson_sm1_pwrc_domain_data {
	unsigned int count;
	struct meson_sm1_pwrc_domain_desc *domains;
};

static struct meson_sm1_pwrc_mem_domain sm1_pwrc_mem_nna[] = {
	{ HHI_NANOQ_MEM_PD_REG0, 0xff },
	{ HHI_NANOQ_MEM_PD_REG1, 0xff },
};

static struct meson_sm1_pwrc_mem_domain sm1_pwrc_mem_usb[] = {
	{ HHI_MEM_PD_REG0, GENMASK(31, 30) },
};

static struct meson_sm1_pwrc_mem_domain sm1_pwrc_mem_pcie[] = {
	{ HHI_MEM_PD_REG0, GENMASK(29, 26) },
};

static struct meson_sm1_pwrc_mem_domain sm1_pwrc_mem_ge2d[] = {
	{ HHI_MEM_PD_REG0, GENMASK(25, 18) },
};

#define SM1_PD(__name, __bit, __mem) \
	{ \
		.name = __name, \
		.sleep_reg = AO_RTI_GEN_PWR_SLEEP0, \
		.sleep_bit = __bit, \
		.iso_reg = AO_RTI_GEN_PWR_ISO0, \
		.iso_bit = __bit, \
		.mem_pd_count = ARRAY_SIZE(__mem), \
		.mem_pd = __mem, \
	}

static struct meson_sm1_pwrc_domain_desc sm1_pwrc_domains[] = {
	[PWRC_SM1_NNA_ID]  = SM1_PD("NNA", 16, sm1_pwrc_mem_nna),
	[PWRC_SM1_USB_ID]  = SM1_PD("USB", 17, sm1_pwrc_mem_usb),
	[PWRC_SM1_PCIE_ID] = SM1_PD("PCI", 18, sm1_pwrc_mem_pcie),
	[PWRC_SM1_GE2D_ID] = SM1_PD("GE2D", 19, sm1_pwrc_mem_ge2d),
};

struct meson_sm1_pwrc_domain {
	struct generic_pm_domain base;
	bool enabled;
	struct meson_sm1_pwrc *pwrc;
	struct meson_sm1_pwrc_domain_desc desc;
};

struct meson_sm1_pwrc {
	struct regmap *regmap_ao;
	struct regmap *regmap_hhi;
	struct meson_sm1_pwrc_domain *domains;
	struct genpd_onecell_data xlate;
};

static int meson_sm1_pwrc_off(struct generic_pm_domain *domain)
{
	struct meson_sm1_pwrc_domain *pwrc_domain =
		container_of(domain, struct meson_sm1_pwrc_domain, base);
	int i;

	regmap_update_bits(pwrc_domain->pwrc->regmap_ao,
			   pwrc_domain->desc.sleep_reg,
			   pwrc_domain->desc.sleep_bit,
			   pwrc_domain->desc.sleep_bit);
	udelay(20);

	for (i = 0 ; i < pwrc_domain->desc.mem_pd_count ; ++i)
		regmap_update_bits(pwrc_domain->pwrc->regmap_hhi,
				   pwrc_domain->desc.mem_pd[i].reg,
				   pwrc_domain->desc.mem_pd[i].mask,
				   pwrc_domain->desc.mem_pd[i].mask);

	udelay(20);

	regmap_update_bits(pwrc_domain->pwrc->regmap_ao,
			   pwrc_domain->desc.iso_reg,
			   pwrc_domain->desc.iso_bit,
			   pwrc_domain->desc.iso_bit);

	return 0;
}

static int meson_sm1_pwrc_on(struct generic_pm_domain *domain)
{
	struct meson_sm1_pwrc_domain *pwrc_domain =
		container_of(domain, struct meson_sm1_pwrc_domain, base);
	int i;

	regmap_update_bits(pwrc_domain->pwrc->regmap_ao,
			   pwrc_domain->desc.sleep_reg,
			   pwrc_domain->desc.sleep_bit, 0);
	udelay(20);

	for (i = 0 ; i < pwrc_domain->desc.mem_pd_count ; ++i)
		regmap_update_bits(pwrc_domain->pwrc->regmap_hhi,
				   pwrc_domain->desc.mem_pd[i].reg,
				   pwrc_domain->desc.mem_pd[i].mask, 0);

	udelay(20);

	regmap_update_bits(pwrc_domain->pwrc->regmap_ao,
			   pwrc_domain->desc.iso_reg,
			   pwrc_domain->desc.iso_bit, 0);

	return 0;
}

static int meson_sm1_pwrc_probe(struct platform_device *pdev)
{
	const struct meson_sm1_pwrc_domain_data *match;
	struct regmap *regmap_ao, *regmap_hhi;
	struct meson_sm1_pwrc *sm1_pwrc;
	int i;

	match = of_device_get_match_data(&pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "failed to get match data\n");
		return -ENODEV;
	}

	sm1_pwrc = devm_kzalloc(&pdev->dev, sizeof(*sm1_pwrc), GFP_KERNEL);
	if (!sm1_pwrc)
		return -ENOMEM;

	sm1_pwrc->xlate.domains =
		devm_kcalloc(&pdev->dev,
			     match->count,
			     sizeof(*sm1_pwrc->xlate.domains),
			     GFP_KERNEL);
	if (!sm1_pwrc->xlate.domains)
		return -ENOMEM;

	sm1_pwrc->domains =
		devm_kcalloc(&pdev->dev,
			     match->count,
			     sizeof(*sm1_pwrc->domains),
			     GFP_KERNEL);
	if (!sm1_pwrc->domains)
		return -ENOMEM;

	sm1_pwrc->xlate.num_domains = match->count;

	regmap_ao = syscon_node_to_regmap(of_get_parent(pdev->dev.of_node));
	if (IS_ERR(regmap_ao)) {
		dev_err(&pdev->dev, "failed to get regmap\n");
		return PTR_ERR(regmap_ao);
	}

	regmap_hhi = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						     "amlogic,hhi-sysctrl");
	if (IS_ERR(regmap_hhi)) {
		dev_err(&pdev->dev, "failed to get HHI regmap\n");
		return PTR_ERR(regmap_hhi);
	}

	sm1_pwrc->regmap_ao = regmap_ao;
	sm1_pwrc->regmap_hhi = regmap_hhi;

	platform_set_drvdata(pdev, sm1_pwrc);

	for (i = 0 ; i < match->count ; ++i) {
		struct meson_sm1_pwrc_domain *dom = &sm1_pwrc->domains[i];

		dom->pwrc = sm1_pwrc;

		memcpy(&dom->desc, &match->domains[i], sizeof(dom->desc));

		dom->base.name = dom->desc.name;
		dom->base.power_on = meson_sm1_pwrc_on;
		dom->base.power_off = meson_sm1_pwrc_off;

		pm_genpd_init(&dom->base, NULL, true);

		sm1_pwrc->xlate.domains[i] = &dom->base;
	}

	of_genpd_add_provider_onecell(pdev->dev.of_node, &sm1_pwrc->xlate);

	return 0;
}

static struct meson_sm1_pwrc_domain_data meson_sm1_pwrc_data = {
	.count = ARRAY_SIZE(sm1_pwrc_domains),
	.domains = sm1_pwrc_domains,
};

static const struct of_device_id meson_sm1_pwrc_match_table[] = {
	{
		.compatible = "amlogic,meson-sm1-pwrc",
		.data = &meson_sm1_pwrc_data,
	},
	{ /* sentinel */ }
};

static struct platform_driver meson_sm1_pwrc_driver = {
	.probe	= meson_sm1_pwrc_probe,
	.driver = {
		.name		= "meson_sm1_pwrc",
		.of_match_table	= meson_sm1_pwrc_match_table,
	},
};
builtin_platform_driver(meson_sm1_pwrc_driver);
