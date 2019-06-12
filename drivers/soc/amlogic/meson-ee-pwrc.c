// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2019 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/reset-controller.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <dt-bindings/power/meson-g12a-power.h>
#include <dt-bindings/power/meson-sm1-power.h>

/* AO Offsets */

#define AO_RTI_GEN_PWR_SLEEP0		(0x3a << 2)
#define AO_RTI_GEN_PWR_ISO0		(0x3b << 2)

/* HHI Offsets */

#define HHI_MEM_PD_REG0			(0x40 << 2)
#define HHI_VPU_MEM_PD_REG0		(0x41 << 2)
#define HHI_VPU_MEM_PD_REG1		(0x42 << 2)
#define HHI_VPU_MEM_PD_REG3		(0x43 << 2)
#define HHI_VPU_MEM_PD_REG4		(0x44 << 2)
#define HHI_AUDIO_MEM_PD_REG0		(0x45 << 2)
#define HHI_NANOQ_MEM_PD_REG0		(0x46 << 2)
#define HHI_NANOQ_MEM_PD_REG1		(0x47 << 2)
#define HHI_VPU_MEM_PD_REG2		(0x4d << 2)

struct meson_ee_pwrc;
struct meson_ee_pwrc_domain;

struct meson_ee_pwrc_mem_domain {
	unsigned int reg;
	unsigned int mask;
};

struct meson_ee_pwrc_top_domain {
	unsigned int sleep_reg;
	unsigned int sleep_mask;
	unsigned int iso_reg;
	unsigned int iso_mask;
};

struct meson_ee_pwrc_domain_desc {
	char *name;
	char **reset_names;
	unsigned int reset_names_count;
	char **clk_names;
	unsigned int clk_names_count;
	struct meson_ee_pwrc_top_domain *top_pd;
	unsigned int mem_pd_count;
	struct meson_ee_pwrc_mem_domain *mem_pd;
	bool (*get_power)(struct meson_ee_pwrc_domain *pwrc_domain);
};

struct meson_ee_pwrc_domain_data {
	unsigned int count;
	struct meson_ee_pwrc_domain_desc *domains;
};

/* Clock and Resets lists */

static char *g12a_pwrc_vpu_resets[] = {
	"viu", "venc", "vcbus", "bt656",
	"rdma", "venci", "vencp", "vdac",
	"vdi6", "vencl", "vid_lock",
};

static char *g12a_pwrc_vpu_clks[] = {
	"vpu", "vapb",
};

/* TOP Power Domains */

static struct meson_ee_pwrc_top_domain g12a_pwrc_vpu = {
	.sleep_reg = AO_RTI_GEN_PWR_SLEEP0,
	.sleep_mask = BIT(8),
	.iso_reg = AO_RTI_GEN_PWR_SLEEP0,
	.iso_mask = BIT(9),
};

#define SM1_EE_PD(__bit)					\
	{							\
		.sleep_reg = AO_RTI_GEN_PWR_SLEEP0, 		\
		.sleep_mask = BIT(__bit), 			\
		.iso_reg = AO_RTI_GEN_PWR_ISO0, 		\
		.iso_mask = BIT(__bit), 			\
	}

static struct meson_ee_pwrc_top_domain sm1_pwrc_vpu = SM1_EE_PD(8);
static struct meson_ee_pwrc_top_domain sm1_pwrc_nna = SM1_EE_PD(16);
static struct meson_ee_pwrc_top_domain sm1_pwrc_usb = SM1_EE_PD(17);
static struct meson_ee_pwrc_top_domain sm1_pwrc_pci = SM1_EE_PD(18);
static struct meson_ee_pwrc_top_domain sm1_pwrc_ge2d = SM1_EE_PD(19);

/* Memory PD Domains */

#define VPU_MEMPD(__reg)					\
	{ __reg, GENMASK(1, 0) },				\
	{ __reg, GENMASK(3, 2) },				\
	{ __reg, GENMASK(5, 4) },				\
	{ __reg, GENMASK(7, 6) },				\
	{ __reg, GENMASK(9, 8) },				\
	{ __reg, GENMASK(11, 10) },				\
	{ __reg, GENMASK(13, 12) },				\
	{ __reg, GENMASK(15, 14) },				\
	{ __reg, GENMASK(17, 16) },				\
	{ __reg, GENMASK(19, 18) },				\
	{ __reg, GENMASK(21, 20) },				\
	{ __reg, GENMASK(23, 22) },				\
	{ __reg, GENMASK(25, 24) },				\
	{ __reg, GENMASK(27, 26) },				\
	{ __reg, GENMASK(29, 28) },				\
	{ __reg, GENMASK(31, 30) }

#define VPU_HHI_MEMPD(__reg)					\
	{ __reg, BIT(8) },					\
	{ __reg, BIT(9) },					\
	{ __reg, BIT(10) },					\
	{ __reg, BIT(11) },					\
	{ __reg, BIT(12) },					\
	{ __reg, BIT(13) },					\
	{ __reg, BIT(14) },					\
	{ __reg, BIT(15) }

static struct meson_ee_pwrc_mem_domain g12a_pwrc_mem_vpu[] = {
	VPU_MEMPD(HHI_VPU_MEM_PD_REG0),
	VPU_MEMPD(HHI_VPU_MEM_PD_REG1),
	VPU_MEMPD(HHI_VPU_MEM_PD_REG2),
	VPU_HHI_MEMPD(HHI_MEM_PD_REG0),
};

static struct meson_ee_pwrc_mem_domain g12a_pwrc_mem_eth[] = {
	{ HHI_MEM_PD_REG0, GENMASK(3, 2) },
};

static struct meson_ee_pwrc_mem_domain sm1_pwrc_mem_vpu[] = {
	VPU_MEMPD(HHI_VPU_MEM_PD_REG0),
	VPU_MEMPD(HHI_VPU_MEM_PD_REG1),
	VPU_MEMPD(HHI_VPU_MEM_PD_REG2),
	VPU_MEMPD(HHI_VPU_MEM_PD_REG3),
	{ HHI_VPU_MEM_PD_REG4, GENMASK(1, 0) },
	{ HHI_VPU_MEM_PD_REG4, GENMASK(3, 2) },
	{ HHI_VPU_MEM_PD_REG4, GENMASK(5, 4) },
	{ HHI_VPU_MEM_PD_REG4, GENMASK(7, 6) },
	VPU_HHI_MEMPD(HHI_MEM_PD_REG0),
};

static struct meson_ee_pwrc_mem_domain sm1_pwrc_mem_nna[] = {
	{ HHI_NANOQ_MEM_PD_REG0, 0xff },
	{ HHI_NANOQ_MEM_PD_REG1, 0xff },
};

static struct meson_ee_pwrc_mem_domain sm1_pwrc_mem_usb[] = {
	{ HHI_MEM_PD_REG0, GENMASK(31, 30) },
};

static struct meson_ee_pwrc_mem_domain sm1_pwrc_mem_pcie[] = {
	{ HHI_MEM_PD_REG0, GENMASK(29, 26) },
};

static struct meson_ee_pwrc_mem_domain sm1_pwrc_mem_ge2d[] = {
	{ HHI_MEM_PD_REG0, GENMASK(25, 18) },
};

static struct meson_ee_pwrc_mem_domain sm1_pwrc_mem_audio[] = {
	{ HHI_MEM_PD_REG0, GENMASK(5, 4) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(1, 0) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(3, 2) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(5, 4) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(7, 6) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(13, 12) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(15, 14) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(17, 16) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(19, 18) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(21, 20) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(23, 22) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(25, 24) },
	{ HHI_AUDIO_MEM_PD_REG0, GENMASK(27, 26) },
};

#define VPU_PD(__name, __resets, __clks, __top_pd, __mem, __get_power)	\
	{								\
		.name = __name,						\
		.reset_names_count = ARRAY_SIZE(__resets),		\
		.reset_names = __resets,				\
		.clk_names_count = ARRAY_SIZE(__clks),			\
		.clk_names = __clks,					\
		.top_pd = __top_pd,					\
		.mem_pd_count = ARRAY_SIZE(__mem),			\
		.mem_pd = __mem,					\
		.get_power = __get_power,				\
	}

#define TOP_PD(__name, __top_pd, __mem)					\
	{								\
		.name = __name,						\
		.top_pd = __top_pd,					\
		.mem_pd_count = ARRAY_SIZE(__mem),			\
		.mem_pd = __mem,					\
	}

#define MEM_PD(__name, __mem)						\
	TOP_PD(__name, NULL, __mem)

static bool pwrc_vpu_get_power(struct meson_ee_pwrc_domain *pwrc_domain);

static struct meson_ee_pwrc_domain_desc g12a_pwrc_domains[] = {
	[PWRC_G12A_VPU_ID]  = VPU_PD("VPU", g12a_pwrc_vpu_resets,
				     g12a_pwrc_vpu_clks, &g12a_pwrc_vpu,
				     g12a_pwrc_mem_vpu,
				     pwrc_vpu_get_power),
	[PWRC_G12A_ETH_ID] = MEM_PD("ETH", g12a_pwrc_mem_eth),
};

static struct meson_ee_pwrc_domain_desc sm1_pwrc_domains[] = {
	[PWRC_SM1_VPU_ID]  = VPU_PD("VPU", g12a_pwrc_vpu_resets,
				    g12a_pwrc_vpu_clks, &sm1_pwrc_vpu,
				    sm1_pwrc_mem_vpu,
				    pwrc_vpu_get_power),
	[PWRC_SM1_NNA_ID]  = TOP_PD("NNA", &sm1_pwrc_nna, sm1_pwrc_mem_nna),
	[PWRC_SM1_USB_ID]  = TOP_PD("USB", &sm1_pwrc_usb, sm1_pwrc_mem_usb),
	[PWRC_SM1_PCIE_ID] = TOP_PD("PCI", &sm1_pwrc_pci, sm1_pwrc_mem_pcie),
	[PWRC_SM1_GE2D_ID] = TOP_PD("GE2D", &sm1_pwrc_ge2d, sm1_pwrc_mem_ge2d),
	[PWRC_SM1_AUDIO_ID] = MEM_PD("AUDIO", sm1_pwrc_mem_audio),
	[PWRC_SM1_ETH_ID] = MEM_PD("ETH", g12a_pwrc_mem_eth),
};

struct meson_ee_pwrc_domain {
	struct generic_pm_domain base;
	bool enabled;
	struct meson_ee_pwrc *pwrc;
	struct meson_ee_pwrc_domain_desc desc;
	struct clk **clks;
	int num_clks;
	struct reset_control **rstc;
	int num_rstc;
};

struct meson_ee_pwrc {
	struct regmap *regmap_ao;
	struct regmap *regmap_hhi;
	struct meson_ee_pwrc_domain *domains;
	struct genpd_onecell_data xlate;
};

static bool pwrc_vpu_get_power(struct meson_ee_pwrc_domain *pwrc_domain)
{
	u32 reg;

	regmap_read(pwrc_domain->pwrc->regmap_ao,
		    pwrc_domain->desc.top_pd->sleep_reg, &reg);

	return (reg & pwrc_domain->desc.top_pd->sleep_mask);
}

static int meson_ee_reset_assert(struct meson_ee_pwrc_domain *pwrc_domain)
{
	int i, ret;

	for (i = 0 ; i < pwrc_domain->num_rstc ; ++i) {
		ret = reset_control_assert(pwrc_domain->rstc[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int meson_ee_reset_deassert(struct meson_ee_pwrc_domain *pwrc_domain)
{
	int i, ret;

	for (i = 0 ; i < pwrc_domain->num_rstc ; ++i) {
		ret = reset_control_deassert(pwrc_domain->rstc[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int meson_ee_clk_disable(struct meson_ee_pwrc_domain *pwrc_domain)
{
	int i;

	for (i = 0 ; i < pwrc_domain->num_clks ; ++i)
		clk_disable(pwrc_domain->clks[i]);

	for (i = 0 ; i < pwrc_domain->num_clks ; ++i)
		clk_unprepare(pwrc_domain->clks[i]);

	return 0;
}

static int meson_ee_clk_enable(struct meson_ee_pwrc_domain *pwrc_domain)
{
	int i, ret;

	for (i = 0 ; i < pwrc_domain->num_clks ; ++i) {
		ret = clk_prepare(pwrc_domain->clks[i]);
		if (ret)
			goto fail_prepare;
	}

	for (i = 0 ; i < pwrc_domain->num_clks ; ++i) {
		ret = clk_enable(pwrc_domain->clks[i]);
		if (ret)
			goto fail_enable;
	}

	return 0;

fail_enable:
	while (--i)
		clk_disable(pwrc_domain->clks[i]);

	/* Unprepare all clocks */
	i = pwrc_domain->num_clks;

fail_prepare:
	while (--i)
		clk_unprepare(pwrc_domain->clks[i]);

	return ret;
}

static int meson_ee_pwrc_off(struct generic_pm_domain *domain)
{
	struct meson_ee_pwrc_domain *pwrc_domain =
		container_of(domain, struct meson_ee_pwrc_domain, base);
	int i;

	if (pwrc_domain->desc.top_pd)
		regmap_update_bits(pwrc_domain->pwrc->regmap_ao,
				   pwrc_domain->desc.top_pd->sleep_reg,
				   pwrc_domain->desc.top_pd->sleep_mask,
				   pwrc_domain->desc.top_pd->sleep_mask);
	udelay(20);

	for (i = 0 ; i < pwrc_domain->desc.mem_pd_count ; ++i)
		regmap_update_bits(pwrc_domain->pwrc->regmap_hhi,
				   pwrc_domain->desc.mem_pd[i].reg,
				   pwrc_domain->desc.mem_pd[i].mask,
				   pwrc_domain->desc.mem_pd[i].mask);

	udelay(20);

	if (pwrc_domain->desc.top_pd)
		regmap_update_bits(pwrc_domain->pwrc->regmap_ao,
				   pwrc_domain->desc.top_pd->iso_reg,
				   pwrc_domain->desc.top_pd->iso_mask,
				   pwrc_domain->desc.top_pd->iso_mask);

	if (pwrc_domain->num_clks) {
		msleep(20);
		meson_ee_clk_disable(pwrc_domain);
	}

	return 0;
}

static int meson_ee_pwrc_on(struct generic_pm_domain *domain)
{
	struct meson_ee_pwrc_domain *pwrc_domain =
		container_of(domain, struct meson_ee_pwrc_domain, base);
	int i, ret;

	if (pwrc_domain->desc.top_pd)
		regmap_update_bits(pwrc_domain->pwrc->regmap_ao,
				   pwrc_domain->desc.top_pd->sleep_reg,
				   pwrc_domain->desc.top_pd->sleep_mask, 0);
	udelay(20);

	for (i = 0 ; i < pwrc_domain->desc.mem_pd_count ; ++i)
		regmap_update_bits(pwrc_domain->pwrc->regmap_hhi,
				   pwrc_domain->desc.mem_pd[i].reg,
				   pwrc_domain->desc.mem_pd[i].mask, 0);

	udelay(20);

	ret = meson_ee_reset_assert(pwrc_domain);
	if (ret)
		return ret;

	if (pwrc_domain->desc.top_pd)
		regmap_update_bits(pwrc_domain->pwrc->regmap_ao,
				   pwrc_domain->desc.top_pd->iso_reg,
				   pwrc_domain->desc.top_pd->iso_mask, 0);

	ret = meson_ee_reset_deassert(pwrc_domain);
	if (ret)
		return ret;

	return meson_ee_clk_enable(pwrc_domain);
}

static int meson_ee_pwrc_init_domain(struct platform_device *pdev,
				     struct meson_ee_pwrc *sm1_pwrc,
				     struct meson_ee_pwrc_domain *dom)
{
	dom->pwrc = sm1_pwrc;
	dom->num_rstc = dom->desc.reset_names_count;
	dom->num_clks = dom->desc.clk_names_count;

	if (dom->num_rstc) {
		int rst;

		dom->rstc = devm_kcalloc(&pdev->dev, dom->num_rstc,
				sizeof(struct reset_control *),	GFP_KERNEL);
		if (!dom->rstc)
			return -ENOMEM;

		for (rst = 0 ; rst < dom->num_rstc ; ++rst) {
			dom->rstc[rst] = devm_reset_control_get_exclusive(
					&pdev->dev,
					dom->desc.reset_names[rst]);
			if (IS_ERR(dom->rstc[rst]))
				return PTR_ERR(dom->rstc[rst]);
		}
	}

	if (dom->num_clks) {
		int clk;

		dom->clks = devm_kcalloc(&pdev->dev, dom->num_clks,
				sizeof(struct clk *), GFP_KERNEL);
		if (!dom->clks)
			return -ENOMEM;

		for (clk = 0 ; clk < dom->num_clks ; ++clk) {
			dom->clks[clk] = devm_clk_get(&pdev->dev,
					dom->desc.clk_names[clk]);
			if (IS_ERR(dom->clks[clk]))
				return PTR_ERR(dom->clks[clk]);
		}
	}

	dom->base.name = dom->desc.name;
	dom->base.power_on = meson_ee_pwrc_on;
	dom->base.power_off = meson_ee_pwrc_off;

	if (dom->desc.get_power) {
		bool powered_off = dom->desc.get_power(dom);
		pm_genpd_init(&dom->base, &pm_domain_always_on_gov,
			      powered_off);
	} else
		pm_genpd_init(&dom->base, NULL, true);

	return 0;
}

static int meson_ee_pwrc_probe(struct platform_device *pdev)
{
	const struct meson_ee_pwrc_domain_data *match;
	struct regmap *regmap_ao, *regmap_hhi;
	struct meson_ee_pwrc *sm1_pwrc;
	int i, ret;

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

	regmap_hhi = syscon_node_to_regmap(of_get_parent(pdev->dev.of_node));
	if (IS_ERR(regmap_hhi)) {
		dev_err(&pdev->dev, "failed to get HHI regmap\n");
		return PTR_ERR(regmap_hhi);
	}

	regmap_ao = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						    "amlogic,ao-sysctrl");
	if (IS_ERR(regmap_ao)) {
		dev_err(&pdev->dev, "failed to get AO regmap\n");
		return PTR_ERR(regmap_ao);
	}

	sm1_pwrc->regmap_ao = regmap_ao;
	sm1_pwrc->regmap_hhi = regmap_hhi;

	platform_set_drvdata(pdev, sm1_pwrc);

	for (i = 0 ; i < match->count ; ++i) {
		struct meson_ee_pwrc_domain *dom = &sm1_pwrc->domains[i];

		memcpy(&dom->desc, &match->domains[i], sizeof(dom->desc));

		ret = meson_ee_pwrc_init_domain(pdev, sm1_pwrc, dom);
		if (ret)
			return ret;

		sm1_pwrc->xlate.domains[i] = &dom->base;
	}

	of_genpd_add_provider_onecell(pdev->dev.of_node, &sm1_pwrc->xlate);

	return 0;
}

static struct meson_ee_pwrc_domain_data meson_ee_g12a_pwrc_data = {
	.count = ARRAY_SIZE(g12a_pwrc_domains),
	.domains = g12a_pwrc_domains,
};

static struct meson_ee_pwrc_domain_data meson_ee_sm1_pwrc_data = {
	.count = ARRAY_SIZE(sm1_pwrc_domains),
	.domains = sm1_pwrc_domains,
};

static const struct of_device_id meson_ee_pwrc_match_table[] = {
	{
		.compatible = "amlogic,meson-g12a-pwrc",
		.data = &meson_ee_g12a_pwrc_data,
	},
	{
		.compatible = "amlogic,meson-sm1-pwrc",
		.data = &meson_ee_sm1_pwrc_data,
	},
	{ /* sentinel */ }
};

static struct platform_driver meson_ee_pwrc_driver = {
	.probe	= meson_ee_pwrc_probe,
	.driver = {
		.name		= "meson_ee_pwrc",
		.of_match_table	= meson_ee_pwrc_match_table,
	},
};
builtin_platform_driver(meson_ee_pwrc_driver);
