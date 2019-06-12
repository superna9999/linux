/*
 * Copyright (c) 2017 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>

/* AO Offsets */

#define AO_RTI_GEN_PWR_SLEEP0		(0x3a << 2)
#define AO_RTI_GEN_PWR_ISO0		(0x3b << 2)

/* HHI Offsets */

#define HHI_MEM_PD_REG0			(0x40 << 2)

#define SM1_PWRC_MAX_DOMAIN	2

struct meson_sm1_pwrc;

struct meson_sm1_pwrc_domain_desc {
	char *name;
	unsigned int sleep_bit;
	unsigned int iso_bit;
	unsigned long mem_mask;
};

static struct meson_sm1_pwrc_domain_desc sm1_pwrc_domains[] =
{
	{ "USB2_PHY", 18, 18, GENMASK(29, 26) },
	{ "USB3_PCIE_PHY", 17, 17, GENMASK(32, 30) },
};

struct meson_sm1_pwrc_domain {
	struct generic_pm_domain base;
	bool enabled;
	struct meson_sm1_pwrc *pwrc;
	struct meson_sm1_pwrc_domain_desc *desc;
};

struct meson_sm1_pwrc {
	struct regmap *regmap_ao;
	struct regmap *regmap_hhi;
	struct meson_sm1_pwrc_domain domains[SM1_PWRC_MAX_DOMAIN];
	struct genpd_onecell_data xlate;
};

static int meson_sm1_pwrc_off(struct generic_pm_domain *domain)
{
	struct meson_sm1_pwrc_domain *sm1_pwrc_domain =
		container_of(domain, struct meson_sm1_pwrc_domain, base);

	regmap_update_bits(sm1_pwrc_domain->pwrc->regmap_ao,
			   AO_RTI_GEN_PWR_SLEEP0,
			   sm1_pwrc_domain->desc->sleep_bit,
			   sm1_pwrc_domain->desc->sleep_bit);
	udelay(20);

	regmap_update_bits(sm1_pwrc_domain->pwrc->regmap_hhi, HHI_MEM_PD_REG0,
			   sm1_pwrc_domain->desc->mem_mask,
			   sm1_pwrc_domain->desc->mem_mask);


	udelay(20);

	regmap_update_bits(sm1_pwrc_domain->pwrc->regmap_ao,
			   AO_RTI_GEN_PWR_ISO0,
			   sm1_pwrc_domain->desc->iso_bit,
			   sm1_pwrc_domain->desc->iso_bit);

	return 0;
}

static int meson_sm1_pwrc_on(struct generic_pm_domain *domain)
{
	struct meson_sm1_pwrc_domain *sm1_pwrc_domain =
		container_of(domain, struct meson_sm1_pwrc_domain, base);

	regmap_update_bits(sm1_pwrc_domain->pwrc->regmap_ao,
			   AO_RTI_GEN_PWR_SLEEP0,
			   sm1_pwrc_domain->desc->sleep_bit, 0);
	udelay(20);

	regmap_update_bits(sm1_pwrc_domain->pwrc->regmap_hhi, HHI_MEM_PD_REG0,
			   sm1_pwrc_domain->desc->mem_mask, 0);

	udelay(20);

	regmap_update_bits(sm1_pwrc_domain->pwrc->regmap_ao,
			   AO_RTI_GEN_PWR_ISO0,
			   sm1_pwrc_domain->desc->iso_bit, 0);

	return 0;
}

static int meson_sm1_pwrc_probe(struct platform_device *pdev)
{
	struct regmap *regmap_ao, *regmap_hhi;
	struct meson_sm1_pwrc *sm1_pwrc;
	int i;

	sm1_pwrc = devm_kzalloc(&pdev->dev, sizeof(*sm1_pwrc), GFP_KERNEL);
	if (!sm1_pwrc)
		return -ENOMEM;

	sm1_pwrc->xlate.domains =
		devm_kcalloc(&pdev->dev,
			     SM1_PWRC_MAX_DOMAIN,
			     sizeof(*sm1_pwrc->xlate.domains),
			     GFP_KERNEL);
	if (!sm1_pwrc->xlate.domains)
		return -ENOMEM;

	sm1_pwrc->xlate.num_domains = SM1_PWRC_MAX_DOMAIN;

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

	memcpy(sm1_pwrc->domains, sm1_pwrc_domains, sizeof(sm1_pwrc_domains));

	for (i = 0 ; i < SM1_PWRC_MAX_DOMAIN ; ++i) {
		struct meson_sm1_pwrc_domain *dom = &sm1_pwrc->domains[i];

		dom->pwrc = sm1_pwrc;
		dom->desc = &sm1_pwrc_domains[i];

		dom->base.name = dom->desc->name;
		dom->base.power_on = meson_sm1_pwrc_on;
		dom->base.power_off = meson_sm1_pwrc_off;

		pm_genpd_init(&dom->base, NULL, true);

		sm1_pwrc->xlate.domains[i] = &dom->base;
	}

	of_genpd_add_provider_onecell(pdev->dev.of_node, &sm1_pwrc->xlate);

	return 0;
}

static const struct of_device_id meson_sm1_pwrc_match_table[] = {
	{ .compatible = "amlogic,meson-sm1-pwrc" },
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
