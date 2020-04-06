// SPDX-License-Identifier: GPL-2.0
/*
 * Meson G12A MIPI DSI Analog PHY
 *
 * Copyright (C) 2018 Amlogic, Inc. All rights reserved
 * Copyright (C) 2020 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <dt-bindings/phy/phy.h>
#include <linux/mfd/syscon.h>

#define HHI_MIPI_CNTL0                             0x00
#define HHI_MIPI_CNTL1                             0x04
#define HHI_MIPI_CNTL2                             0x08

#define DSI_LANE_0              (1 << 9)
#define DSI_LANE_1              (1 << 8)
#define DSI_LANE_CLK            (1 << 7)
#define DSI_LANE_2              (1 << 6)
#define DSI_LANE_3              (1 << 5)
#define DSI_LANE_MASK		(0x1F << 5)

struct phy_g12a_mipi_dphy_analog_priv {
	struct phy *phy;
	struct regmap *regmap;
	struct phy_configure_opts_mipi_dphy config;
};

static int phy_g12a_mipi_dphy_analog_configure(struct phy *phy,
					       union phy_configure_opts *opts)
{
	struct phy_g12a_mipi_dphy_analog_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = phy_mipi_dphy_config_validate(&opts->mipi_dphy);
	if (ret)
		return ret;

	memcpy(&priv->config, opts, sizeof(priv->config));

	return 0;
}

static int phy_g12a_mipi_dphy_analog_power_on(struct phy *phy)
{
	struct phy_g12a_mipi_dphy_analog_priv *priv = phy_get_drvdata(phy);
	unsigned int reg;

	regmap_write(priv->regmap, HHI_MIPI_CNTL0,
		     (0xa487 << 16) | (0x8 << 0));

	regmap_write(priv->regmap, HHI_MIPI_CNTL1,
		     (0x1 << 16) | (0x002e << 0));

	regmap_write(priv->regmap, HHI_MIPI_CNTL2,
		     (0x2680 << 16) | (0x45a << 0));

	reg = DSI_LANE_CLK;
	switch (priv->config.lanes) {
	case 4:
		reg |= DSI_LANE_3;
		fallthrough;
	case 3:
		reg |= DSI_LANE_2;
		fallthrough;
	case 2:
		reg |= DSI_LANE_1;
		fallthrough;
	case 1:
		reg |= DSI_LANE_0;
		break;
	default:
		reg = 0;
	}

	regmap_update_bits(priv->regmap, HHI_MIPI_CNTL2, DSI_LANE_MASK, reg);

	return 0;
}

static int phy_g12a_mipi_dphy_analog_power_off(struct phy *phy)
{
	struct phy_g12a_mipi_dphy_analog_priv *priv = phy_get_drvdata(phy);

	regmap_write(priv->regmap, HHI_MIPI_CNTL0, 0);
	regmap_write(priv->regmap, HHI_MIPI_CNTL1, 0);
	regmap_write(priv->regmap, HHI_MIPI_CNTL2, 0);

	return 0;
}

static const struct phy_ops phy_g12a_mipi_dphy_analog_ops = {
	.configure = phy_g12a_mipi_dphy_analog_configure,
	.power_on = phy_g12a_mipi_dphy_analog_power_on,
	.power_off = phy_g12a_mipi_dphy_analog_power_off,
	.owner = THIS_MODULE,
};

static int phy_g12a_mipi_dphy_analog_probe(struct platform_device *pdev)
{
	struct phy_provider *phy;
	struct device *dev = &pdev->dev;
	struct phy_g12a_mipi_dphy_analog_priv *priv;
	struct device_node *np = dev->of_node;
	int ret;

	priv = devm_kmalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = syscon_node_to_regmap(of_get_parent(pdev->dev.of_node));
	if (IS_ERR(priv->regmap)) {
		dev_err(&pdev->dev, "failed to get HHI regmap\n");
		return PTR_ERR(priv->regmap);
	}

	priv->phy = devm_phy_create(dev, np, &phy_g12a_mipi_dphy_analog_ops);
	if (IS_ERR(priv->phy)) {
		ret = PTR_ERR(priv->phy);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to create PHY\n");
		return ret;
	}

	phy_set_drvdata(priv->phy, priv);
	dev_set_drvdata(dev, priv);

	phy = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy);
}

static const struct of_device_id phy_g12a_mipi_dphy_analog_of_match[] = {
	{
		.compatible = "amlogic,g12a-mipi-dphy-analog",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, phy_g12a_mipi_dphy_analog_of_match);

static struct platform_driver phy_g12a_mipi_dphy_analog_driver = {
	.probe = phy_g12a_mipi_dphy_analog_probe,
	.driver = {
		.name = "phy-meson-g12a-mipi-dphy-analog",
		.of_match_table = phy_g12a_mipi_dphy_analog_of_match,
	},
};
module_platform_driver(phy_g12a_mipi_dphy_analog_driver);

MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_DESCRIPTION("Meson G12A MIPI_DSI Analog PHY driver");
MODULE_LICENSE("GPL v2");
