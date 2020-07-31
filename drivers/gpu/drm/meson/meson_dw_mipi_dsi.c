// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>

#include <video/mipi_display.h>

#include <drm/bridge/dw_mipi_dsi.h>
#include <drm/drm_mipi_dsi.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_print.h>

#include "meson_drv.h"
#include "meson_dw_mipi_dsi.h"
#include "meson_registers.h"
#include "meson_vclk.h"
#include "meson_venc.h"

#define DRIVER_NAME "meson-dw-mipi-dsi"
#define DRIVER_DESC "Amlogic Meson MIPI-DSI DRM driver"

/*  MIPI DSI/VENC Color Format Definitions */
#define MIPI_DSI_VENC_COLOR_30B   0x0
#define MIPI_DSI_VENC_COLOR_24B   0x1
#define MIPI_DSI_VENC_COLOR_18B   0x2
#define MIPI_DSI_VENC_COLOR_16B   0x3

#define COLOR_16BIT_CFG_1         0x0
#define COLOR_16BIT_CFG_2         0x1
#define COLOR_16BIT_CFG_3         0x2
#define COLOR_18BIT_CFG_1         0x3
#define COLOR_18BIT_CFG_2         0x4
#define COLOR_24BIT               0x5
#define COLOR_20BIT_LOOSE         0x6
#define COLOR_24_BIT_YCBCR        0x7
#define COLOR_16BIT_YCBCR         0x8
#define COLOR_30BIT               0x9
#define COLOR_36BIT               0xa
#define COLOR_12BIT               0xb
#define COLOR_RGB_111             0xc
#define COLOR_RGB_332             0xd
#define COLOR_RGB_444             0xe

/*  MIPI DSI Relative REGISTERs Definitions */
/* For MIPI_DSI_TOP_CNTL */
#define BIT_DPI_COLOR_MODE        20
#define BIT_IN_COLOR_MODE         16
#define BIT_CHROMA_SUBSAMPLE      14
#define BIT_COMP2_SEL             12
#define BIT_COMP1_SEL             10
#define BIT_COMP0_SEL              8
#define BIT_DE_POL                 6
#define BIT_HSYNC_POL              5
#define BIT_VSYNC_POL              4
#define BIT_DPICOLORM              3
#define BIT_DPISHUTDN              2
#define BIT_EDPITE_INTR_PULSE      1
#define BIT_ERR_INTR_PULSE         0

/**
 * DOC: MIPI DSI
 *
 */

struct meson_dw_mipi_dsi {
	struct drm_encoder encoder;
	struct meson_drm *priv;
	struct device *dev;
	void __iomem *base;
	struct phy *phy;
	union phy_configure_opts phy_opts;
	struct dw_mipi_dsi *dmd;
	struct dw_mipi_dsi_plat_data pdata;
	struct mipi_dsi_device *dsi_device;

	unsigned int lane_mbps; /* per lane */
	u32 format;	
};
#define encoder_to_meson_dw_mipi_dsi(x) \
	container_of(x, struct meson_dw_mipi_dsi, encoder)

static void dw_mipi_dsi_set_vclk(struct meson_dw_mipi_dsi *mipi_dsi,
			    	 struct drm_display_mode *mode)
{
	struct meson_drm *priv = mipi_dsi->priv;
	unsigned int div;

	div = 1; //DIV_ROUND_UP(mipi_dsi->lane_mbps * 1000, mode->clock);

	DRM_DEBUG_DRIVER("mbps %d div %d clk %d pll %d\n", mipi_dsi->lane_mbps, div, mode->clock, mode->clock * div);	

	meson_vclk_setup(priv, MESON_VCLK_TARGET_MIPI_DSI, mode->clock, mode->clock,
			 mode->clock, mode->clock * 3, false);
}

static int dw_mipi_dsi_host_init(void *priv_data)
{
	unsigned int dpi_data_format, venc_data_width;
	struct meson_dw_mipi_dsi *mipi_dsi = priv_data;
	u32 temp;

	pr_info("%s\n", __func__);

	switch (mipi_dsi->format) {
	case MIPI_DSI_FMT_RGB888:
		dpi_data_format = COLOR_24BIT;
		venc_data_width = MIPI_DSI_VENC_COLOR_24B;
		break;
	case MIPI_DSI_FMT_RGB666:
		dpi_data_format = COLOR_18BIT_CFG_2;
		venc_data_width = MIPI_DSI_VENC_COLOR_18B;
		break;
	default:
		DRM_DEV_ERROR(mipi_dsi->dev,
			      "invalid pixel format %d\n",
			      mipi_dsi->format);
		return -EINVAL;
	};

	/* 2.2.1 Configure Set color format for DPI register */
	temp = readl_relaxed(mipi_dsi->base + MIPI_DSI_TOP_CNTL) &
		~(0xf<<BIT_DPI_COLOR_MODE) &
		~(0x7<<BIT_IN_COLOR_MODE) &
		~(0x3<<BIT_CHROMA_SUBSAMPLE);

	writel_relaxed(temp |
		(dpi_data_format  << BIT_DPI_COLOR_MODE)  |
		(venc_data_width  << BIT_IN_COLOR_MODE),
		mipi_dsi->base + MIPI_DSI_TOP_CNTL);

	writel_bits_relaxed(BIT(9) | BIT(19), BIT(9) | BIT(19),
			    mipi_dsi->base + MIPI_DSI_TOP_MEAS_CNTL);

	phy_configure(mipi_dsi->phy, &mipi_dsi->phy_opts);
	phy_power_on(mipi_dsi->phy);

	return 0;
}

static void dw_mipi_dsi_phy_power_on(void *priv_data)
{
	struct meson_dw_mipi_dsi *mipi_dsi = priv_data;

	pr_info("%s\n", __func__);

	//phy_configure(mipi_dsi->phy, &mipi_dsi->phy_opts);
	//phy_power_on(mipi_dsi->phy);
}

static void dw_mipi_dsi_phy_power_off(void *priv_data)
{
	struct meson_dw_mipi_dsi *mipi_dsi = priv_data;

	pr_info("%s\n", __func__);

	phy_power_off(mipi_dsi->phy);
}

static int
dw_mipi_dsi_get_lane_mbps(void *priv_data, const struct drm_display_mode *mode,
			  unsigned long mode_flags, u32 lanes, u32 format,
			  unsigned int *lane_mbps)
{
	struct meson_dw_mipi_dsi *mipi_dsi = priv_data;
	unsigned long mpclk, target_mbps = 1000;
	int bpp;

	pr_info("%s\n", __func__);

	mipi_dsi->format = format;
	bpp = mipi_dsi_pixel_format_to_bpp(mipi_dsi->format);
	if (bpp < 0) {
		DRM_DEV_ERROR(mipi_dsi->dev,
			      "failed to get bpp for pixel format %d\n",
			      mipi_dsi->format);
		return bpp;
	}

	mpclk = DIV_ROUND_UP(mode->clock, MSEC_PER_SEC);

	/* take 1 / 0.8, since mbps must big than bandwidth of RGB */
	if (mpclk)
		target_mbps = mpclk * (bpp / lanes) * 10 / 8;

	*lane_mbps = target_mbps;
	mipi_dsi->lane_mbps = target_mbps;

	phy_mipi_dphy_get_default_config(mode->clock * 1000 * 10 / 8,
					 bpp, lanes,
					 &mipi_dsi->phy_opts.mipi_dphy);

	return 0;
}

static int
dw_mipi_dsi_phy_get_timing(void *priv_data, unsigned int lane_mbps,
			   struct dw_mipi_dsi_dphy_timing *timing)
{
	/* TOFIX handle other cases */

	pr_info("%s\n", __func__);

	timing->clk_lp2hs = 37;
	timing->clk_hs2lp = 135;
	timing->data_lp2hs = 50;
	timing->data_hs2lp = 3;

	return 0;
}

static const struct dw_mipi_dsi_phy_ops meson_dw_mipi_dsi_phy_ops = {
	.init = dw_mipi_dsi_host_init,
	.power_on = dw_mipi_dsi_phy_power_on,
	.power_off = dw_mipi_dsi_phy_power_off,
	.get_lane_mbps = dw_mipi_dsi_get_lane_mbps,
	.get_timing = dw_mipi_dsi_phy_get_timing,
};

/* Encoder */

static void meson_mipi_dsi_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs meson_mipi_dsi_encoder_funcs = {
	.destroy        = meson_mipi_dsi_encoder_destroy,
};

static int meson_mipi_dsi_encoder_atomic_check(struct drm_encoder *encoder,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{

	DRM_DEBUG_DRIVER("\n");	
	return 0;
}

static void meson_mipi_dsi_encoder_disable(struct drm_encoder *encoder)
{
	struct meson_dw_mipi_dsi *mipi_dsi =
			encoder_to_meson_dw_mipi_dsi(encoder);
	struct meson_drm *priv = mipi_dsi->priv;

	DRM_DEBUG_DRIVER("\n");	

	writel_relaxed(0, priv->io_base + _REG(ENCL_VIDEO_EN));
}

static void meson_mipi_dsi_encoder_enable(struct drm_encoder *encoder)
{
	struct meson_dw_mipi_dsi *mipi_dsi =
			encoder_to_meson_dw_mipi_dsi(encoder);
	struct meson_drm *priv = mipi_dsi->priv;

	DRM_DEBUG_DRIVER("\n");	

	writel_relaxed(1, priv->io_base + _REG(ENCL_VIDEO_EN));
}

static void meson_mipi_dsi_encoder_mode_set(struct drm_encoder *encoder,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{
	struct meson_dw_mipi_dsi *mipi_dsi = encoder_to_meson_dw_mipi_dsi(encoder);
	struct meson_drm *priv = mipi_dsi->priv;

	DRM_DEBUG_DRIVER("\n");	

	meson_venc_mipi_dsi_mode_set(priv, mode);

	dw_mipi_dsi_set_vclk(mipi_dsi, mode);
}

static const struct drm_encoder_helper_funcs
				meson_mipi_dsi_encoder_helper_funcs = {
	.atomic_check	= meson_mipi_dsi_encoder_atomic_check,
	.disable	= meson_mipi_dsi_encoder_disable,
	.enable		= meson_mipi_dsi_encoder_enable,
	.mode_set	= meson_mipi_dsi_encoder_mode_set,
};

static void meson_dw_mipi_dsi_init(struct meson_dw_mipi_dsi *mipi_dsi)
{
	DRM_DEBUG_DRIVER("\n");	

	writel_relaxed((1 << 4) | (1 << 5) | (0 << 6),
			mipi_dsi->base + MIPI_DSI_TOP_CNTL);
	writel_bits_relaxed(0xf, 0xf,
			    mipi_dsi->base + MIPI_DSI_TOP_SW_RESET);
	writel_bits_relaxed(0xf, 0,
			    mipi_dsi->base + MIPI_DSI_TOP_SW_RESET);
	writel_bits_relaxed(0x3, 0x3,
			    mipi_dsi->base + MIPI_DSI_TOP_CLK_CNTL);
	writel_relaxed(0, mipi_dsi->base + MIPI_DSI_TOP_MEM_PD);

	msleep(10);

	phy_init(mipi_dsi->phy);
}

static struct clk_bulk_data meson_dw_mipi_dsi_clocks[] = {
	{ .id = "px_clk" },
};

static int meson_dw_mipi_dsi_bind(struct device *dev, struct device *master,
				void *data)
{
	struct meson_dw_mipi_dsi *mipi_dsi = dev_get_drvdata(dev);
	struct drm_device *drm = data;
	struct meson_drm *priv = drm->dev_private;
	struct drm_encoder *encoder;
	int ret;

	DRM_DEBUG_DRIVER("%08x\n", mipi_dsi);
	DRM_DEBUG_DRIVER("%08x\n", mipi_dsi->dmd);

	/* Check before if we are supposed to have a sub-device... */
	if (!mipi_dsi->dsi_device)
		return -EPROBE_DEFER;

	encoder = &mipi_dsi->encoder;
	mipi_dsi->priv = priv;

	/* Encoder */
	ret = drm_encoder_init(drm, encoder, &meson_mipi_dsi_encoder_funcs,
			       DRM_MODE_ENCODER_DSI, "meson_mipi_dsi");
	if (ret) {
		dev_err(priv->dev, "Failed to init DSI encoder\n");
		return ret;
	}

	drm_encoder_helper_add(encoder, &meson_mipi_dsi_encoder_helper_funcs);

	encoder->possible_crtcs = BIT(0);

	DRM_DEBUG_DRIVER("encoder initialized %08x\n", mipi_dsi->dmd);

	ret = dw_mipi_dsi_bind(mipi_dsi->dmd, encoder);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to bind: %d\n", ret);
		return ret;
	}

	return 0;
}

static void meson_dw_mipi_dsi_unbind(struct device *dev, struct device *master,
				   void *data)
{
	struct meson_dw_mipi_dsi *mipi_dsi = dev_get_drvdata(dev);

	dw_mipi_dsi_remove(mipi_dsi->dmd);
}

static const struct component_ops meson_dw_mipi_dsi_ops = {
	.bind	= meson_dw_mipi_dsi_bind,
	.unbind	= meson_dw_mipi_dsi_unbind,
};

static int meson_dw_mipi_dsi_host_attach(void *priv_data,
					 struct mipi_dsi_device *device)
{
	struct meson_dw_mipi_dsi *mipi_dsi = priv_data;

	mipi_dsi->dsi_device = device;

	return 0;
}

static int meson_dw_mipi_dsi_host_detach(void *priv_data,
					 struct mipi_dsi_device *device)
{
	struct meson_dw_mipi_dsi *mipi_dsi = priv_data;

	if (device == mipi_dsi->dsi_device)
		mipi_dsi->dsi_device = NULL;
	else
		return -EINVAL;

	return 0;
}

static const struct dw_mipi_dsi_host_ops meson_dw_mipi_dsi_host_ops = {
	.attach = meson_dw_mipi_dsi_host_attach,
	.detach = meson_dw_mipi_dsi_host_detach,
};

static int meson_dw_mipi_dsi_probe(struct platform_device *pdev)
{
	struct meson_dw_mipi_dsi *mipi_dsi;
	struct resource *res;
	int ret;

	mipi_dsi = devm_kzalloc(&pdev->dev, sizeof(*mipi_dsi), GFP_KERNEL);
	if (!mipi_dsi)
		return -ENOMEM;

	mipi_dsi->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mipi_dsi->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mipi_dsi->base))
		return PTR_ERR(mipi_dsi->base);

	mipi_dsi->phy = devm_phy_get(&pdev->dev, "dphy");
	if (IS_ERR(mipi_dsi->phy)) {
		ret = PTR_ERR(mipi_dsi->phy);
		dev_err(&pdev->dev, "failed to get mipi dphy: %d\n", ret);
		return ret;
	}

	ret = devm_clk_bulk_get(&pdev->dev,
				ARRAY_SIZE(meson_dw_mipi_dsi_clocks),
				meson_dw_mipi_dsi_clocks);
	if (ret) {
		dev_err(&pdev->dev, "Unable to get clocks\n");
		return ret;
	}

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(meson_dw_mipi_dsi_clocks),
				      meson_dw_mipi_dsi_clocks);
	if (ret) {
		dev_err(&pdev->dev, "Unable to prepare/enable clocks\n");
		return ret;
	}

	/* MIPI DSI Controller */

	mipi_dsi->pdata.base = mipi_dsi->base;
	mipi_dsi->pdata.max_data_lanes = 4;
	mipi_dsi->pdata.phy_ops = &meson_dw_mipi_dsi_phy_ops;
	mipi_dsi->pdata.host_ops = &meson_dw_mipi_dsi_host_ops;
	mipi_dsi->pdata.priv_data = mipi_dsi;
	platform_set_drvdata(pdev, mipi_dsi);

	mipi_dsi->dmd = dw_mipi_dsi_probe(pdev, &mipi_dsi->pdata);
	if (IS_ERR(mipi_dsi->dmd)) {
		ret = PTR_ERR(mipi_dsi->dmd);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"Failed to probe dw_mipi_dsi: %d\n", ret);
		goto err_clkdisable;
	}

	meson_dw_mipi_dsi_init(mipi_dsi);

	pr_info("mipi_dsi %08x\n", mipi_dsi);
	pr_info("dmd %08x\n", mipi_dsi->dmd);

	return component_add(mipi_dsi->dev, &meson_dw_mipi_dsi_ops);

err_clkdisable:
	clk_bulk_disable_unprepare(ARRAY_SIZE(meson_dw_mipi_dsi_clocks),
				   meson_dw_mipi_dsi_clocks);

	return ret;
}

static int meson_dw_mipi_dsi_remove(struct platform_device *pdev)
{
	struct meson_dw_mipi_dsi *mipi_dsi = dev_get_drvdata(&pdev->dev);

	component_del(mipi_dsi->dev, &meson_dw_mipi_dsi_ops);

	return 0;
}

static const struct of_device_id meson_dw_mipi_dsi_of_table[] = {
	{ .compatible = "amlogic,meson-axg-dw-mipi-dsi", },
	{ }
};
MODULE_DEVICE_TABLE(of, meson_dw_mipi_dsi_of_table);

static struct platform_driver meson_dw_mipi_dsi_platform_driver = {
	.probe		= meson_dw_mipi_dsi_probe,
	.remove		= meson_dw_mipi_dsi_remove,
	.driver		= {
		.name		= DRIVER_NAME,
		.of_match_table	= meson_dw_mipi_dsi_of_table,
	},
};
module_platform_driver(meson_dw_mipi_dsi_platform_driver);

MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
