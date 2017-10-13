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
#include <linux/reset.h>

/* AO Offsets */

#define AO_RTI_GEN_PWR_SLEEP0		(0x3a << 2)

#define GEN_PWR_VPU_HDMI		BIT(8)
#define GEN_PWR_VPU_HDMI_ISO		BIT(9)

/* HHI Offsets */

#define HHI_MEM_PD_REG0			(0x40 << 2)
#define HHI_VPU_MEM_PD_REG0		(0x41 << 2)
#define HHI_VPU_MEM_PD_REG1		(0x42 << 2)

struct meson_gx_pwrc_vpu {
	struct generic_pm_domain genpd;
	struct regmap *regmap_ao;
	struct regmap *regmap_hhi;
	struct reset_control *rstc;
};

static inline 
struct meson_gx_pwrc_vpu *genpd_to_pd(struct generic_pm_domain *d)
{
	return container_of(d, struct meson_gx_pwrc_vpu, genpd);
}

static int meson_gx_pwrc_vpu_power_off(struct generic_pm_domain *genpd)
{
	struct meson_gx_pwrc_vpu *pd = genpd_to_pd(genpd);
	int i;

	regmap_update_bits(pd->regmap_ao, AO_RTI_GEN_PWR_SLEEP0, GEN_PWR_VPU_HDMI_ISO, GEN_PWR_VPU_HDMI_ISO);

	/* Power Down Memories */
	for (i = 0; i < 32; i+=2) {
		regmap_update_bits(pd->regmap_hhi, HHI_VPU_MEM_PD_REG0, 0x2 << i, 0x3 << i);
		udelay(5);
	}
	for (i = 0; i < 32; i+=2) {
		regmap_update_bits(pd->regmap_hhi, HHI_VPU_MEM_PD_REG1, 0x2 << i, 0x3 << i);
		udelay(5);
	}
	for (i = 8; i < 16; i++) {
		regmap_update_bits(pd->regmap_hhi, HHI_MEM_PD_REG0, BIT(i), BIT(i));
		udelay(5);
	}

	regmap_update_bits(pd->regmap_ao, AO_RTI_GEN_PWR_SLEEP0, GEN_PWR_VPU_HDMI, GEN_PWR_VPU_HDMI);

	return 0;
}

static int meson_gx_pwrc_vpu_power_on(struct generic_pm_domain *genpd)
{
	struct meson_gx_pwrc_vpu *pd = genpd_to_pd(genpd);
	int i;

	regmap_update_bits(pd->regmap_ao, AO_RTI_GEN_PWR_SLEEP0, GEN_PWR_VPU_HDMI, 0);

	/* Power Up Memories */
	for (i = 0; i < 32; i+=2) {
		regmap_update_bits(pd->regmap_hhi, HHI_VPU_MEM_PD_REG0, 0x2 << i, 0);
		udelay(5);
	}
	for (i = 0; i < 32; i+=2) {
		regmap_update_bits(pd->regmap_hhi, HHI_VPU_MEM_PD_REG1, 0x2 << i, 0);
		udelay(5);
	}
	for (i = 8; i < 16; i++) {
		regmap_update_bits(pd->regmap_hhi, HHI_MEM_PD_REG0, BIT(i), 0);
		udelay(5);
	}

	reset_control_assert(pd->rstc);

	regmap_update_bits(pd->regmap_ao, AO_RTI_GEN_PWR_SLEEP0, GEN_PWR_VPU_HDMI_ISO, 0);

	reset_control_deassert(pd->rstc);

	return 0;
}

static bool meson_gx_pwrc_vpu_get_power( struct meson_gx_pwrc_vpu *pd)
{
	u32 reg;

	regmap_read(pd->regmap_ao, AO_RTI_GEN_PWR_SLEEP0, &reg);

	return (reg & GEN_PWR_VPU_HDMI);
}

static struct meson_gx_pwrc_vpu vpu_hdmi_pd = {
	.genpd = {
		.name = "vpu_hdmi",
		.power_off = meson_gx_pwrc_vpu_power_off,
		.power_on = meson_gx_pwrc_vpu_power_on,
	},
};

static int meson_gx_pwrc_vpu_probe(struct platform_device *pdev)
{
	struct regmap *regmap_ao, *regmap_hhi;
	struct reset_control *rstc;

	regmap_ao = syscon_node_to_regmap(of_get_parent(pdev->dev.of_node));
	if (IS_ERR(regmap_ao)) {
		dev_err(&pdev->dev, "failed to get regmap\n");
		return -ENODEV;
	}

	regmap_hhi = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "amlogic,hhi-sysctrl");
	if (IS_ERR(regmap_ao)) {
		dev_err(&pdev->dev, "failed to get regmap\n");
		return -ENODEV;
	}

	rstc = devm_reset_control_array_get(&pdev->dev, false, false);
	if (IS_ERR(rstc)) {
		dev_err(&pdev->dev, "failed to get reset lines\n");
		return -EINVAL;
	}

	vpu_hdmi_pd.regmap_ao = regmap_ao;
	vpu_hdmi_pd.regmap_hhi = regmap_hhi;
	vpu_hdmi_pd.rstc = rstc;
	
	pm_genpd_init(&vpu_hdmi_pd.genpd, &pm_domain_always_on_gov,
		      meson_gx_pwrc_vpu_get_power(&vpu_hdmi_pd));

	return of_genpd_add_provider_simple(pdev->dev.of_node, &vpu_hdmi_pd.genpd);
}

static const struct of_device_id meson_gx_pwrc_vpu_match_table[] = {
	{ .compatible = "amlogic,meson-gx-pwrc-vpu" },
	{ /* sentinel */ }
};

static struct platform_driver meson_gx_pwrc_vpu_driver = {
	.probe	= meson_gx_pwrc_vpu_probe,
	.driver = {
		.name		= "meson_gx_pwrc_vpu",
		.of_match_table	= meson_gx_pwrc_vpu_match_table,
	},
};
builtin_platform_driver(meson_gx_pwrc_vpu_driver);
