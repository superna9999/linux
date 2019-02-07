// SPDX-License-Identifier: GPL-2.0
/*
 * USB Glue for Amlogic G12A SoCs
 *
 * Copyright (c) 2019 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

/*
 * The USB is organized with a glue around the DWC3 Controller IP as :
 * - Control registers for each USB2 Ports
 * - Control registers for the USB PHY layer
 * - SuperSpeed PHY can be enabled only if port is used
 *
 * TOFIX:
 * - Add dynamic OTG switching with ID change interrupt
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/reset.h>
#include <linux/of_graph.h>
#include <linux/phy/phy.h>
#include <linux/usb/otg.h>
#include <linux/debugfs.h>

/* USB Glue Control Registers */

#define USB_R0							0x00
	#define USB_R0_P30_LANE0_TX2RX_LOOPBACK			BIT(17)
	#define USB_R0_P30_LANE0_EXT_PCLK_REQ			BIT(18)
	#define USB_R0_P30_PCS_RX_LOS_MASK_VAL_MASK		GENMASK(28, 19)
	#define USB_R0_U2D_SS_SCALEDOWN_MODE_MASK		GENMASK(30, 29)
	#define USB_R0_U2D_ACT					BIT(31)

#define USB_R1							0x04
	#define USB_R1_U3H_BIGENDIAN_GS				BIT(0)
	#define USB_R1_U3H_PME_ENABLE				BIT(1)
	#define USB_R1_U3H_HUB_PORT_OVERCURRENT_MASK		GENMASK(4, 2)
	#define USB_R1_U3H_HUB_PORT_PERM_ATTACH_MASK		GENMASK(9, 7)
	#define USB_R1_U3H_HOST_U2_PORT_DISABLE_MASK		GENMASK(13, 12)
	#define USB_R1_U3H_HOST_U3_PORT_DISABLE			BIT(16)
	#define USB_R1_U3H_HOST_PORT_POWER_CONTROL_PRESENT	BIT(17)
	#define USB_R1_U3H_HOST_MSI_ENABLE			BIT(18)
	#define USB_R1_U3H_FLADJ_30MHZ_REG_MASK			GENMASK(24, 19)
	#define USB_R1_P30_PCS_TX_SWING_FULL_MASK		GENMASK(31, 25)

#define USB_R2							0x08
	#define USB_R2_P30_PCS_TX_DEEMPH_3P5DB_MASK		GENMASK(25, 20)
	#define USB_R2_P30_PCS_TX_DEEMPH_6DB_MASK		GENMASK(31, 26)

#define USB_R3							0x0c
	#define USB_R3_P30_SSC_ENABLE				BIT(0)
	#define USB_R3_P30_SSC_RANGE_MASK			GENMASK(3, 1)
	#define USB_R3_P30_SSC_REF_CLK_SEL_MASK			GENMASK(12, 4)
	#define USB_R3_P30_REF_SSP_EN				BIT(13)

#define USB_R4							0x10
	#define USB_R4_P21_PORT_RESET_0				BIT(0)
	#define USB_R4_P21_SLEEP_M0				BIT(1)
	#define USB_R4_MEM_PD_MASK				GENMASK(3, 2)
	#define USB_R4_P21_ONLY					BIT(4)

#define USB_R5							0x14
	#define USB_R5_ID_DIG_SYNC				BIT(0)
	#define USB_R5_ID_DIG_REG				BIT(1)
	#define USB_R5_ID_DIG_CFG_MASK				GENMASK(3, 2)
	#define USB_R5_ID_DIG_EN_0				BIT(4)
	#define USB_R5_ID_DIG_EN_1				BIT(5)
	#define USB_R5_ID_DIG_CURR				BIT(6)
	#define USB_R5_ID_DIG_IRQ				BIT(7)
	#define USB_R5_ID_DIG_TH_MASK				GENMASK(15, 8)
	#define USB_R5_ID_DIG_CNT_MASK				GENMASK(23, 16)

/* USB2 Ports Control Registers */

#define U2P_R0							0x0
	#define U2P_R0_HOST_DEVICE				BIT(0)
	#define U2P_R0_POWER_OK					BIT(1)
	#define U2P_R0_HAST_MODE				BIT(2)
	#define U2P_R0_POWER_ON_RESET				BIT(3)
	#define U2P_R0_ID_PULLUP				BIT(4)
	#define U2P_R0_DRV_VBUS					BIT(5)

#define U2P_R1							0x4
	#define U2P_R1_PHY_READY				BIT(0)
	#define U2P_R1_ID_DIG					BIT(1)
	#define U2P_R1_OTG_SESSION_VALID			BIT(2)
	#define U2P_R1_VBUS_VALID				BIT(3)

#define MAX_PHY							5

#define USB2_MAX_PHY						4
#define USB3_PHY						4

struct dwc3_meson_g12a {
	struct device		*dev;
	struct regmap		*regmap;
	struct clk		*clk;
	struct reset_control	*reset;
	struct phy		*phys[5];
	enum usb_dr_mode	phy_modes[5];
	enum phy_mode		otg_phy_mode;
	unsigned int		usb2_ports;
	unsigned int		usb3_ports;
	struct dentry		*root;
};

#define U2P_REG_SIZE						0x20
#define USB_REG_OFFSET						0x80

static void dwc3_meson_g12a_usb2_set_mode(struct dwc3_meson_g12a *priv,
					  int i, enum usb_dr_mode mode)
{
	switch (mode) {
	case USB_DR_MODE_HOST:
	case USB_DR_MODE_OTG:
	case USB_DR_MODE_UNKNOWN:
		regmap_update_bits(priv->regmap, U2P_R0 + (U2P_REG_SIZE * i),
				U2P_R0_HOST_DEVICE,
				U2P_R0_HOST_DEVICE);
		break;

	case USB_DR_MODE_PERIPHERAL:
		regmap_update_bits(priv->regmap, U2P_R0 + (U2P_REG_SIZE * i),
				U2P_R0_HOST_DEVICE, 0);
		break;
	}
}

static int dwc3_meson_g12a_usb2_init(struct dwc3_meson_g12a *priv)
{
	enum usb_dr_mode id_mode;
	u32 val;
	int i;

	/* Read ID current level */
	regmap_read(priv->regmap, USB_R5, &val);
	if (val & USB_R5_ID_DIG_CURR)
		id_mode = USB_DR_MODE_PERIPHERAL;
	else
		id_mode = USB_DR_MODE_HOST;

	dev_info(priv->dev, "ID mode %s\n",
		 id_mode ==  USB_DR_MODE_HOST ? "host" : "peripheral");

	for (i = 0 ; i < USB2_MAX_PHY ; ++i) {
		if (!priv->phys[i])
			continue;

		regmap_update_bits(priv->regmap, U2P_R0 + (U2P_REG_SIZE * i),
				   U2P_R0_POWER_ON_RESET,
				   U2P_R0_POWER_ON_RESET);

		if (priv->phy_modes[i] == USB_DR_MODE_PERIPHERAL ||
		    (priv->phy_modes[i] == USB_DR_MODE_OTG &&
		     id_mode == USB_DR_MODE_PERIPHERAL)) {
			dwc3_meson_g12a_usb2_set_mode(priv, i,
						      USB_DR_MODE_PERIPHERAL);

			if (priv->phy_modes[i] == USB_DR_MODE_OTG)
				priv->otg_phy_mode = PHY_MODE_USB_DEVICE;
		} else {
			dwc3_meson_g12a_usb2_set_mode(priv, i,
						      USB_DR_MODE_HOST);

			if (priv->phy_modes[i] == USB_DR_MODE_OTG)
				priv->otg_phy_mode = PHY_MODE_USB_HOST;
		}

		regmap_update_bits(priv->regmap, U2P_R0 + (U2P_REG_SIZE * i),
				   U2P_R0_POWER_ON_RESET, 0);
	}

	return 0;
}

static void dwc3_meson_g12a_usb3_init(struct dwc3_meson_g12a *priv)
{
	regmap_update_bits(priv->regmap, USB_REG_OFFSET + USB_R3,
			USB_R3_P30_SSC_RANGE_MASK |
			USB_R3_P30_REF_SSP_EN,
			USB_R3_P30_SSC_ENABLE |
			FIELD_PREP(USB_R3_P30_SSC_RANGE_MASK, 2) |
			USB_R3_P30_REF_SSP_EN);
	udelay(2);

	regmap_update_bits(priv->regmap, USB_REG_OFFSET + USB_R2,
			USB_R2_P30_PCS_TX_DEEMPH_3P5DB_MASK,
			FIELD_PREP(USB_R2_P30_PCS_TX_DEEMPH_3P5DB_MASK, 0x15));

	regmap_update_bits(priv->regmap, USB_REG_OFFSET + USB_R2,
			USB_R2_P30_PCS_TX_DEEMPH_6DB_MASK,
			FIELD_PREP(USB_R2_P30_PCS_TX_DEEMPH_6DB_MASK, 0x20));

	udelay(2);

	regmap_update_bits(priv->regmap, USB_REG_OFFSET + USB_R1,
			USB_R1_U3H_HOST_PORT_POWER_CONTROL_PRESENT,
			USB_R1_U3H_HOST_PORT_POWER_CONTROL_PRESENT);

	regmap_update_bits(priv->regmap, USB_REG_OFFSET + USB_R1,
			USB_R1_P30_PCS_TX_SWING_FULL_MASK,
			FIELD_PREP(USB_R1_P30_PCS_TX_SWING_FULL_MASK, 127));
}

static void dwc3_meson_g12a_usb_init_mode(struct dwc3_meson_g12a *priv,
					  bool is_peripheral)
{
	if (is_peripheral) {
		regmap_update_bits(priv->regmap, USB_REG_OFFSET + USB_R0,
				USB_R0_U2D_ACT, USB_R0_U2D_ACT);
		regmap_update_bits(priv->regmap, USB_REG_OFFSET + USB_R0,
				USB_R0_U2D_SS_SCALEDOWN_MODE_MASK, 0);
		regmap_update_bits(priv->regmap, USB_REG_OFFSET + USB_R4,
				USB_R4_P21_SLEEP_M0, USB_R4_P21_SLEEP_M0);
	} else {
		regmap_update_bits(priv->regmap, USB_REG_OFFSET + USB_R0,
				USB_R0_U2D_ACT, 0);
		regmap_update_bits(priv->regmap, USB_REG_OFFSET + USB_R4,
				USB_R4_P21_SLEEP_M0, 0);
	}
}

static int dwc3_meson_g12a_usb_init(struct dwc3_meson_g12a *priv)
{
	int ret;

	ret = dwc3_meson_g12a_usb2_init(priv);
	if (ret)
		return ret;

	regmap_update_bits(priv->regmap, USB_REG_OFFSET + USB_R1,
			USB_R1_U3H_FLADJ_30MHZ_REG_MASK,
			FIELD_PREP(USB_R1_U3H_FLADJ_30MHZ_REG_MASK, 0x20));

	regmap_update_bits(priv->regmap, USB_REG_OFFSET + USB_R5,
			USB_R5_ID_DIG_EN_0,
			USB_R5_ID_DIG_EN_0);
	regmap_update_bits(priv->regmap, USB_REG_OFFSET + USB_R5,
			USB_R5_ID_DIG_EN_1,
			USB_R5_ID_DIG_EN_1);
	regmap_update_bits(priv->regmap, USB_REG_OFFSET + USB_R5,
			USB_R5_ID_DIG_TH_MASK,
			FIELD_PREP(USB_R5_ID_DIG_TH_MASK, 0xff));

	/* If we have an actual SuperSpeed port, initialize it */
	if (priv->usb3_ports)
		dwc3_meson_g12a_usb3_init(priv);

	dwc3_meson_g12a_usb_init_mode(priv,
				(priv->otg_phy_mode == PHY_MODE_USB_DEVICE));

	return 0;
}

static const struct regmap_config phy_meson_g12a_usb3_regmap_conf = {
	.reg_bits = 8,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = USB_REG_OFFSET + USB_R5,
};

static int dwc3_meson_g12a_get_phys(struct dwc3_meson_g12a *priv)
{
	struct device_node *port, *phy_node;
	struct of_phandle_args args;
	enum usb_dr_mode mode;
	const char *dr_mode;
	struct phy *phy;
	int ret, i;

	for (i = 0 ; i < MAX_PHY ; ++i) {
		port = of_graph_get_port_by_id(priv->dev->of_node, i);

		/* Ignore port if not defined or disabled */
		if (!of_device_is_available(port)) {
			of_node_put(port);
			continue;
		}

		/* Get associated PHY */
		phy = of_phy_get(port, NULL);
		if (IS_ERR(phy)) {
			of_node_put(port);
			ret = PTR_ERR(phy);
			goto err_phy_get;
		}

		of_node_put(port);

		/* Get phy dr_mode */
		ret = of_parse_phandle_with_args(port, "phys", "#phy-cells",
						 0, &args);
		if (ret) {
			of_node_put(port);
			goto err_phy_get;
		}

		phy_node = args.np;

		ret = of_property_read_string(phy_node, "dr_mode", &dr_mode);
		if (ret) {
			dr_mode = "unknown";
			mode = USB_DR_MODE_UNKNOWN;
		} else {
			if (!strcmp(dr_mode, "host"))
				mode = USB_DR_MODE_HOST;
			else if (!strcmp(dr_mode, "otg"))
				mode = USB_DR_MODE_OTG;
			else if (!strcmp(dr_mode, "peripheral"))
				mode = USB_DR_MODE_PERIPHERAL;
			else {
				mode = USB_DR_MODE_UNKNOWN;
				dr_mode = "unknown";
			}
		}

		dev_info(priv->dev, "port%d: %s mode %s\n",
			 i, of_node_full_name(phy_node), dr_mode);

		of_node_put(phy_node);

		priv->phy_modes[i] = mode;
		priv->phys[i] = phy;

		if (i == USB3_PHY)
			priv->usb3_ports++;
		else
			priv->usb2_ports++;
	}

	dev_info(priv->dev, "usb2 ports: %d\n", priv->usb2_ports);
	dev_info(priv->dev, "usb3 ports: %d\n", priv->usb3_ports);

	return 0;

err_phy_get:
	for (i = 0 ; i < MAX_PHY ; ++i)
		if (priv->phys[i])
			phy_put(priv->phys[i]);

	return ret;
}

static int dwc3_meson_g12a_mode_force_get(void *data, u64 *val)
{
	struct dwc3_meson_g12a *priv = data;

	if (priv->otg_phy_mode == PHY_MODE_USB_HOST)
		*val = 1;
	else if (priv->otg_phy_mode == PHY_MODE_USB_DEVICE)
		*val = 0;
	else
		return -EINVAL;

	return 0;
}

static int dwc3_meson_g12a_mode_force_set(void *data, u64 val)
{
	struct dwc3_meson_g12a *priv = data;
	int i;

	if ((val && priv->otg_phy_mode == PHY_MODE_USB_HOST) ||
	    (!val && priv->otg_phy_mode == PHY_MODE_USB_DEVICE))
		return 0;

	for (i = 0 ; i < USB2_MAX_PHY ; ++i) {
		if (!priv->phys[i])
			continue;

		if (priv->phy_modes[i] != USB_DR_MODE_OTG)
			continue;

		if (val) {
			dev_info(priv->dev, "switching to Host Mode\n");

			dwc3_meson_g12a_usb2_set_mode(priv, i,
						      USB_DR_MODE_HOST);

			dwc3_meson_g12a_usb_init_mode(priv, false);

			priv->otg_phy_mode = PHY_MODE_USB_HOST;
		} else {
			dev_info(priv->dev, "switching to Device Mode\n");

			dwc3_meson_g12a_usb2_set_mode(priv, i,
						      USB_DR_MODE_PERIPHERAL);

			dwc3_meson_g12a_usb_init_mode(priv, true);

			priv->otg_phy_mode = PHY_MODE_USB_DEVICE;
		}

		return phy_set_mode(priv->phys[i], priv->otg_phy_mode);
	}

	return -EINVAL;
}

DEFINE_DEBUGFS_ATTRIBUTE(dwc3_meson_g12a_mode_force_fops,
			 dwc3_meson_g12a_mode_force_get,
			 dwc3_meson_g12a_mode_force_set, "%llu\n");

static int dwc3_meson_g12a_otg_id_get(void *data, u64 *val)
{
	struct dwc3_meson_g12a *priv = data;
	u32 reg;

	regmap_read(priv->regmap, USB_R5, &reg);

	*val = (reg & USB_R5_ID_DIG_CURR);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(dwc3_meson_g12a_otg_id_fops,
			 dwc3_meson_g12a_otg_id_get, NULL, "%llu\n");

/* We provide a DebugFS interface to get the ID value and force OTG switch */
static int dwc3_meson_g12a_debugfs_init(struct dwc3_meson_g12a *priv)
{
	priv->root = debugfs_create_dir("dwc3-meson-g12a", NULL);
	if (IS_ERR(priv->root))
		return PTR_ERR(priv->root);

	debugfs_create_file("mode_force", 0600, priv->root, priv,
			    &dwc3_meson_g12a_mode_force_fops);

	debugfs_create_file("otg_id", 0400, priv->root, priv,
			    &dwc3_meson_g12a_otg_id_fops);

	return 0;
}

static int dwc3_meson_g12a_probe(struct platform_device *pdev)
{
	struct dwc3_meson_g12a	*priv;
	struct device		*dev = &pdev->dev;
	struct device_node	*np = dev->of_node;
	void __iomem *base;
	struct resource *res;
	int ret, i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->regmap = devm_regmap_init_mmio(dev, base,
					     &phy_meson_g12a_usb3_regmap_conf);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->clk = devm_clk_get(dev, "usb");
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, priv);
	priv->dev = dev;

	priv->reset = devm_reset_control_get(dev, "usb");
	if (IS_ERR(priv->reset)) {
		ret = PTR_ERR(priv->reset);
		dev_err(dev, "failed to get device reset, err=%d\n", ret);
		return ret;
	}

	ret = reset_control_reset(priv->reset);
	if (ret)
		return ret;

	ret = dwc3_meson_g12a_get_phys(priv);
	if (ret)
		return ret;

	dwc3_meson_g12a_usb_init(priv);

	/* Init PHYs */
	for (i = 0 ; i < MAX_PHY ; ++i) {
		if (priv->phys[i]) {
			ret = phy_init(priv->phys[i]);
			if (ret)
				goto err_phys_put;
		}
	}

	/* Set OTG PHY mode */
	for (i = 0 ; i < MAX_PHY ; ++i) {
		if (priv->phys[i] && priv->phy_modes[i] == USB_DR_MODE_OTG) {
			ret = phy_set_mode(priv->phys[i], priv->otg_phy_mode);
			if (ret)
				goto err_phys_put;
		}
	}

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret) {
		clk_disable_unprepare(priv->clk);
		clk_put(priv->clk);

		goto err_phys_exit;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	if (dwc3_meson_g12a_debugfs_init(priv))
		dev_dbg(dev, "Failed to add DebugFS interface\n");

	return 0;

err_phys_exit:
	for (i = 0 ; i < MAX_PHY ; ++i)
		if (priv->phys[i])
			phy_exit(priv->phys[i]);

err_phys_put:
	for (i = 0 ; i < MAX_PHY ; ++i)
		if (priv->phys[i])
			phy_put(priv->phys[i]);

	return ret;
}

static int dwc3_meson_g12a_remove(struct platform_device *pdev)
{
	struct dwc3_meson_g12a *priv = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int i;

	debugfs_remove_recursive(priv->root);

	of_platform_depopulate(dev);

	for (i = 0 ; i < MAX_PHY ; ++i)
		if (priv->phys[i])
			phy_exit(priv->phys[i]);

	for (i = 0 ; i < MAX_PHY ; ++i)
		if (priv->phys[i])
			phy_put(priv->phys[i]);

	clk_disable_unprepare(priv->clk);
	clk_put(priv->clk);

	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_set_suspended(dev);

	return 0;
}

static int __maybe_unused dwc3_meson_g12a_runtime_suspend(struct device *dev)
{
	struct dwc3_meson_g12a	*priv = dev_get_drvdata(dev);

	clk_disable(priv->clk);

	return 0;
}

static int __maybe_unused dwc3_meson_g12a_runtime_resume(struct device *dev)
{
	struct dwc3_meson_g12a	*priv = dev_get_drvdata(dev);

	return clk_enable(priv->clk);
}

static int __maybe_unused dwc3_meson_g12a_suspend(struct device *dev)
{
	struct dwc3_meson_g12a *priv = dev_get_drvdata(dev);
	int i;

	for (i = 0 ; i < MAX_PHY ; ++i)
		if (priv->phys[i])
			phy_exit(priv->phys[i]);

	reset_control_assert(priv->reset);

	return 0;
}

static int __maybe_unused dwc3_meson_g12a_resume(struct device *dev)
{
	struct dwc3_meson_g12a *priv = dev_get_drvdata(dev);
	int i, ret;

	reset_control_deassert(priv->reset);

	dwc3_meson_g12a_usb_init(priv);

	/* Init PHYs */
	for (i = 0 ; i < MAX_PHY ; ++i) {
		if (priv->phys[i]) {
			ret = phy_init(priv->phys[i]);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static const struct dev_pm_ops dwc3_meson_g12a_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_meson_g12a_suspend, dwc3_meson_g12a_resume)
	SET_RUNTIME_PM_OPS(dwc3_meson_g12a_runtime_suspend,
			dwc3_meson_g12a_runtime_resume, NULL)
};

static const struct of_device_id dwc3_meson_g12a_match[] = {
	{ .compatible = "amlogic,meson-g12a-usb-ctrl" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, dwc3_meson_g12a_match);

static struct platform_driver dwc3_meson_g12a_driver = {
	.probe		= dwc3_meson_g12a_probe,
	.remove		= dwc3_meson_g12a_remove,
	.driver		= {
		.name	= "dwc3-meson-g12a",
		.of_match_table = dwc3_meson_g12a_match,
		.pm	= &dwc3_meson_g12a_dev_pm_ops,
	},
};

module_platform_driver(dwc3_meson_g12a_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Amlogic Meson G12A USB Glue Layer");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
