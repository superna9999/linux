/*
 * Copyright Neil Armstrong <narmstrong@baylibre.com> (C) 2016.
 * Copyright OpenWrt.org (C) 2015.
 * Copyright Altera Corporation (C) 2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Adopted from dwmac-socfpga.c
 * Based on code found in mach-oxnas.c
 */

#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/stmmac.h>
#include <linux/version.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "stmmac.h"
#include "stmmac_platform.h"

#define SYS_GMAC_CTRL_REGOFFSET		0x78

struct oxnas_gmac {
	struct regmap *regmap;
};

#define SYS_CTRL_GMAC_RGMII         2
#define SYS_CTRL_GMAC_SIMPLE_MUX    1
#define SYS_CTRL_GMAC_CKEN_GTX      0

static int oxnas_gmac_init(struct platform_device *pdev, void *priv)
{
	struct oxnas_gmac *bsp_priv = priv;
	int ret = 0;
	unsigned value;

	ret = device_reset(&pdev->dev);
	if (ret)
		return ret;

	ret = regmap_read(bsp_priv->regmap, SYS_GMAC_CTRL_REGOFFSET, &value);
	if (ret)
		return ret;

	/* Enable GMII_GTXCLK to follow GMII_REFCLK, required for gigabit PHY */
	value |= BIT(SYS_CTRL_GMAC_CKEN_GTX);
	/* Use simple mux for 25/125 Mhz clock switching */
	value |= BIT(SYS_CTRL_GMAC_SIMPLE_MUX);

	regmap_write(bsp_priv->regmap, SYS_GMAC_CTRL_REGOFFSET, value);

	return 0;
}

static int oxnas_gmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	int			ret;
	struct device		*dev = &pdev->dev;
	struct oxnas_gmac	*bsp_priv;

	bsp_priv = devm_kzalloc(dev, sizeof(*bsp_priv), GFP_KERNEL);
	if (!bsp_priv)
		return -ENOMEM;

	bsp_priv->regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						       "plxtech,sys-ctrl");
	if (IS_ERR(bsp_priv->regmap)) {
		dev_err(&pdev->dev, "failed to get sys ctrl regmap\n");
		return -ENODEV;
	}

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = stmmac_probe_config_dt(pdev, &stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	plat_dat->bsp_priv = bsp_priv;
	plat_dat->init = oxnas_gmac_init;

	ret = oxnas_gmac_init(pdev, bsp_priv);
	if (ret)
		return ret;

	return stmmac_dvr_probe(dev, plat_dat, &stmmac_res);
}

static const struct of_device_id oxnas_gmac_match[] = {
	{ .compatible = "plxtech,nas782x-gmac" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, oxnas_gmac_match);

static struct platform_driver oxnas_gmac_driver = {
	.probe  = oxnas_gmac_probe,
	.remove = stmmac_pltfr_remove,
	.driver = {
		.name		= "oxnas-gmac",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = oxnas_gmac_match,
	},
};
module_platform_driver(oxnas_gmac_driver);

MODULE_LICENSE("GPL v2");
