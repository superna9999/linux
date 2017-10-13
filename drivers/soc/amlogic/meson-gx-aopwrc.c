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

#include <dt-bindings/power/amlogic,meson-gx-aopwrc.h>

#define AO_RTI_PWR_CNTL_REG1		(0x03 << 2)
#define AO_RTI_PWR_CNTL_REG0		(0x04 << 2)
#define AO_RTI_PWR_SYS_CPU_CNTL0	(0x38 << 2)
#define AO_RTI_PWR_SYS_CPU_CNTL1	(0x39 << 2)
#define AO_RTI_GEN_PWR_SLEEP0		(0x3a << 2)
#define AO_RTI_GEN_PWR_ISO0		(0x3b << 2)
#define AO_RTI_GEN_PWR_ACK0		(0x3c << 2)
#define AO_RTI_PWR_SYS_CPU_MEM_PD0	(0x3d << 2)
#define AO_RTI_PWR_SYS_CPU_MEM_PD1	(0x3e << 2)

#define GEN_PWR_VPU_HDMI		BIT(8)
#define GEN_PWR_VPU_HDMI_ISO		BIT(9)

struct meson_gx_aopwrc_pd {
	struct generic_pm_domain genpd;
	struct regmap *regmap;	
	unsigned int reg;
	unsigned int mask;
};

static inline 
struct meson_gx_aopwrc_pd *genpd_to_pd(struct generic_pm_domain *d)
{
	return container_of(d, struct meson_gx_aopwrc_pd, genpd);
}

/* Simple Power Domains ops */

static int meson_gx_aopwrc_power_off(struct generic_pm_domain *genpd)
{
	struct meson_gx_aopwrc_pd *pd = genpd_to_pd(genpd);

	return regmap_update_bits(pd->regmap, pd->reg, pd->mask, pd->mask);
}

static int meson_gx_aopwrc_power_on(struct generic_pm_domain *genpd)
{
	struct meson_gx_aopwrc_pd *pd = genpd_to_pd(genpd);

	return regmap_update_bits(pd->regmap, pd->reg, pd->mask, 0);
}

static bool meson_gx_aopwrc_get_power( struct meson_gx_aopwrc_pd *pd)
{
	u32 reg;

	regmap_read(pd->regmap, pd->reg, &reg);

	return (reg & pd->mask);
}

/* Power Domains */

static struct meson_gx_aopwrc_pd vpu_hdmi_pd = {
	.genpd = {
		.name = "vpu_hdmi",
		.power_off = meson_gx_aopwrc_power_off,
		.power_on = meson_gx_aopwrc_power_on,
	},
	.reg = AO_RTI_GEN_PWR_SLEEP0,
	.mask = GEN_PWR_VPU_HDMI,
};

static struct meson_gx_aopwrc_pd vpu_hdmi_iso_pd = {
	.genpd = {
		.name = "vpu_hdmi_iso",
		.power_off = meson_gx_aopwrc_power_off,
		.power_on = meson_gx_aopwrc_power_on,
	},
	.reg = AO_RTI_GEN_PWR_SLEEP0,
	.mask = GEN_PWR_VPU_HDMI_ISO,
};

/* Power Domains table */
static struct meson_gx_aopwrc_pd *meson_gx_aopwrc_domains[] = {
	&vpu_hdmi_pd,
	&vpu_hdmi_iso_pd,
};

struct generic_pm_domain *meson_gx_aopwrc_pm_domains[] = {
	[PWR_AO_VPU_HDMI] = &vpu_hdmi_pd.genpd,
	[PWR_AO_VPU_HDMI_ISO] = &vpu_hdmi_iso_pd.genpd,
};

/* DT Onecell Table */
static struct genpd_onecell_data meson_gx_aopwrc_onecell_data = {
	.domains = meson_gx_aopwrc_pm_domains,
	.num_domains = 2,
};

static int meson_gx_aopwrc_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int pd, ret;

	regmap = syscon_node_to_regmap(of_get_parent(pdev->dev.of_node));
	if (IS_ERR(regmap)) {
		dev_err(&pdev->dev, "failed to get regmap\n");
		return -ENODEV;
	}

	for (pd = 0 ; pd < ARRAY_SIZE(meson_gx_aopwrc_domains) ; ++pd) {
		struct meson_gx_aopwrc_pd *domain = meson_gx_aopwrc_domains[pd];

		domain->regmap = regmap;

		ret = pm_genpd_init(&domain->genpd, &pm_domain_always_on_gov,
				    meson_gx_aopwrc_get_power(domain));
		if (ret)
			dev_warn(&pdev->dev, "failed to init '%s' domain (%d)\n",
				 domain->genpd.name, ret);
	}

	return of_genpd_add_provider_onecell(pdev->dev.of_node,
					     &meson_gx_aopwrc_onecell_data);
}

static const struct of_device_id meson_gx_aopwrc_match_table[] = {
	{ .compatible = "amlogic,meson-gx-aopwrc" },
	{ /* sentinel */ }
};

static struct platform_driver meson_gx_aopwrc_driver = {
	.probe	= meson_gx_aopwrc_probe,
	.driver = {
		.name		= "meson_gx_aopwrc",
		.of_match_table	= meson_gx_aopwrc_match_table,
	},
};
builtin_platform_driver(meson_gx_aopwrc_driver);
