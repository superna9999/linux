/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/component.h>
#include <linux/of_graph.h>
#include <linux/gpio/consumer.h>
#include <linux/reset.h>

#include <drm/drmP.h>
#include <drm/drm_edid.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/bridge/dw_hdmi.h>

#include "meson_drv.h"
#include "meson_venc.h"
#include "meson_vclk.h"
#include "meson_dw_hdmi.h"
#include "meson_registers.h"

/*
 * DRM Chain of call :
 *
 * encoder->set_mode	(venc config, pll, csc, phy)
 * bridge->set_mode	(only stores the mode)
 * encoder->enable	(enc_vpu_bridge_reset)
 * bridge->enable	(av_compose,|, enable_video, AVI, CSC, sample)
 *                                  |-> phy_init (pll, phy setup)
 */

/* PHY Communication Channel */
#define HDMITX_TOP_ADDR_REG		0x0
#define HDMITX_TOP_DATA_REG		0x4
#define HDMITX_TOP_CTRL_REG		0x8

/* Controller Communication Channel */
#define HDMITX_DWC_ADDR_REG		0x10
#define HDMITX_DWC_DATA_REG		0x14
#define HDMITX_DWC_CTRL_REG		0x18

/* DWC Registers */
#define HDMITX_DWC_FC_AUDICONF0         0x1025
#define HDMITX_DWC_FC_AUDICONF1         0x1026
#define HDMITX_DWC_FC_AUDICONF2         0x1027
#define HDMITX_DWC_FC_AUDICONF3         0x1028

#define HDMITX_DWC_FC_AUDSCONF          0x1063
#define HDMITX_DWC_FC_AUDSV             0x1065
#define HDMITX_DWC_FC_AUDSU             0x1066
#define HDMITX_DWC_FC_AUDSCHNLS0        0x1067
#define HDMITX_DWC_FC_AUDSCHNLS1        0x1068
#define HDMITX_DWC_FC_AUDSCHNLS2        0x1069
#define HDMITX_DWC_FC_AUDSCHNLS3        0x106A
#define HDMITX_DWC_FC_AUDSCHNLS4        0x106B
#define HDMITX_DWC_FC_AUDSCHNLS5        0x106C
#define HDMITX_DWC_FC_AUDSCHNLS6        0x106D
#define HDMITX_DWC_FC_AUDSCHNLS7        0x106E
#define HDMITX_DWC_FC_AUDSCHNLS8        0x106F
#define HDMITX_DWC_FC_CTRLQHIGH         0x1073
#define HDMITX_DWC_FC_CTRLQLOW          0x1074

#define HDMITX_DWC_FC_DATAUTO0          0x10B3
#define HDMITX_DWC_FC_DATAUTO1          0x10B4
#define HDMITX_DWC_FC_DATAUTO2          0x10B5
#define HDMITX_DWC_FC_DATMAN            0x10B6
#define HDMITX_DWC_FC_DATAUTO3          0x10B7
#define HDMITX_DWC_FC_RDRB0             0x10B8
#define HDMITX_DWC_FC_RDRB1             0x10B9
#define HDMITX_DWC_FC_RDRB2             0x10BA
#define HDMITX_DWC_FC_RDRB3             0x10BB
#define HDMITX_DWC_FC_RDRB4             0x10BC
#define HDMITX_DWC_FC_RDRB5             0x10BD
#define HDMITX_DWC_FC_RDRB6             0x10BE
#define HDMITX_DWC_FC_RDRB7             0x10BF
#define HDMITX_DWC_FC_RDRB8             0x10C0
#define HDMITX_DWC_FC_RDRB9             0x10C1
#define HDMITX_DWC_FC_RDRB10            0x10C2
#define HDMITX_DWC_FC_RDRB11            0x10C3

#define HDMITX_DWC_FC_MULTISTREAM_CTRL  0x10E2
#define HDMITX_DWC_FC_PACKET_TX_EN      0x10E3

#define HDMITX_DWC_AUD_CONF0            0x3100
#define HDMITX_DWC_AUD_CONF1            0x3101
#define HDMITX_DWC_AUD_INT              0x3102
#define HDMITX_DWC_AUD_CONF2            0x3103
#define HDMITX_DWC_AUD_INT1             0x3104
#define HDMITX_DWC_AUD_SPDIFINT         0x3302
#define HDMITX_DWC_AUD_SPDIFINT1        0x3303
#define HDMITX_DWC_AUD_SPDIF0           0x3300
#define HDMITX_DWC_AUD_SPDIF1           0x3301

#define HDMITX_DWC_MC_CLKDIS            0x4001
#define HDMITX_DWC_MC_LOCKONCLOCK	0x4006
#define HDMITX_DWC_MC_FLOWCTRL		0x4004
#define HDMITX_DWC_FC_SCRAMBLER_CTRL	0x10E1

/* HHI Registers */
#define HHI_MEM_PD_REG0		0x100 /* 0x40 */
#define HHI_GCLK_MPEG2 		0x148 /* 0x52 */
#define HHI_GCLK_OTHER		0x150 /* 0x54 */
#define HHI_HDMI_CLK_CNTL	0x1cc /* 0x73 */
#define HHI_HDMI_PHY_CNTL0	0x3a0 /* 0xe8 */
#define HHI_HDMI_PHY_CNTL1	0x3a4 /* 0xe9 */
#define HHI_HDMI_PHY_CNTL2	0x3a8 /* 0xea */
#define HHI_HDMI_PHY_CNTL3	0x3ac /* 0xeb */

static DEFINE_SPINLOCK(reg_lock);

enum meson_venc_source {
	MESON_VENC_SOURCE_NONE = 0,
	MESON_VENC_SOURCE_ENCI = 1,
	MESON_VENC_SOURCE_ENCP = 2,
};

struct meson_dw_hdmi {
	struct drm_encoder encoder;
	struct dw_hdmi_plat_data dw_plat_data;
	struct meson_drm *priv;
	struct device *dev;
	void __iomem *hdmitx;
	struct gpio_desc *hpd;
	struct reset_control *hdmitx_apb, *hdmitx_ctrl, *hdmitx_phy;	
};
#define encoder_to_meson_dw_hdmi(x) \
	container_of(x, struct meson_dw_hdmi, encoder)

#define plat_data_to_meson_dw_hdmi(x) \
	container_of(x, struct meson_dw_hdmi, dw_plat_data)

static inline int dw_hdmi_is_compatible(struct meson_dw_hdmi *dw_hdmi,
					const char *compat)
{
	return of_device_is_compatible(dw_hdmi->dev->of_node, compat);
}

static inline void dw_hdmi_top_write(struct meson_dw_hdmi *dw_hdmi,
				     unsigned int addr, unsigned int data)
{
	unsigned long flags;

	spin_lock_irqsave(&reg_lock, flags);
	writel(addr & 0xffff, dw_hdmi->hdmitx + HDMITX_TOP_ADDR_REG);
	writel(addr & 0xffff, dw_hdmi->hdmitx + HDMITX_TOP_ADDR_REG);
	writel(data, dw_hdmi->hdmitx + HDMITX_TOP_DATA_REG);
	spin_unlock_irqrestore(&reg_lock, flags);
}

static unsigned int dw_hdmi_top_read(struct meson_dw_hdmi *dw_hdmi,
				     unsigned int addr)
{
	unsigned long flags;
	unsigned int data;

	spin_lock_irqsave(&reg_lock, flags);
	writel(addr & 0xffff, dw_hdmi->hdmitx + HDMITX_TOP_ADDR_REG);
	writel(addr & 0xffff, dw_hdmi->hdmitx + HDMITX_TOP_ADDR_REG);
	data = readl(dw_hdmi->hdmitx + HDMITX_TOP_DATA_REG);
	data = readl(dw_hdmi->hdmitx + HDMITX_TOP_DATA_REG);
	spin_unlock_irqrestore(&reg_lock, flags);

	return data;
}

static inline void dw_hdmi_top_write_bits(struct meson_dw_hdmi *dw_hdmi,
					  unsigned int addr,
					  unsigned int mask,
					  unsigned int val)
{
	unsigned int data = dw_hdmi_top_read(dw_hdmi, addr);

	data &= ~mask;
	data |= val;

	dw_hdmi_top_write(dw_hdmi, addr, data);
}

static inline void dw_hdmi_dwc_write(struct meson_dw_hdmi *dw_hdmi,
				     unsigned int addr, unsigned int data)
{
	unsigned long flags;

	spin_lock_irqsave(&reg_lock, flags);
	writel(addr & 0xffff, dw_hdmi->hdmitx + HDMITX_DWC_ADDR_REG);
	writel(addr & 0xffff, dw_hdmi->hdmitx + HDMITX_DWC_ADDR_REG);
	writel(data, dw_hdmi->hdmitx + HDMITX_DWC_DATA_REG);
	spin_unlock_irqrestore(&reg_lock, flags);
}

static unsigned int dw_hdmi_dwc_read(struct meson_dw_hdmi *dw_hdmi,
				     unsigned int addr)
{
	unsigned long flags;
	unsigned int data;

	spin_lock_irqsave(&reg_lock, flags);
	writel(addr & 0xffff, dw_hdmi->hdmitx + HDMITX_DWC_ADDR_REG);
	writel(addr & 0xffff, dw_hdmi->hdmitx + HDMITX_DWC_ADDR_REG);
	data = readl(dw_hdmi->hdmitx + HDMITX_DWC_DATA_REG);
	data = readl(dw_hdmi->hdmitx + HDMITX_DWC_DATA_REG);
	spin_unlock_irqrestore(&reg_lock, flags);

	return data;
}

static inline void dw_hdmi_dwc_write_bits(struct meson_dw_hdmi *dw_hdmi,
					  unsigned int addr,
					  unsigned int mask,
					  unsigned int val)
{
	unsigned int data = dw_hdmi_dwc_read(dw_hdmi, addr);

	data &= ~mask;
	data |= val;

	dw_hdmi_dwc_write(dw_hdmi, addr, data);
}

/* Bridge */

static void meson_hdmi_phy_setup_mode(struct meson_dw_hdmi *dw_hdmi,
				      struct drm_display_mode *mode)
{
	struct meson_drm *priv = dw_hdmi->priv;
	unsigned int phy_mode = 0;
	int vic = drm_match_cea_mode(mode);

	/* Bandwidth modes */
	switch (vic) {
	case 16: /* 1080p60 */
	case 31: /* 1080p50 */
		phy_mode = 3;
	/* TODO advanced 4k2k modes */
	default:
		phy_mode = 4;
	}

	if (dw_hdmi_is_compatible(dw_hdmi, "amlogic,meson-gxl-dw-hdmi") ||
	    dw_hdmi_is_compatible(dw_hdmi, "amlogic,meson-gxl-dw-hdmi")) {
		switch (phy_mode) {
		case 1: /* 5.94Gbps, 3.7125Gbsp */
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0x333d3282);
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL3, 0x2136315b);
			break;
		case 2: /* 2.97Gbps */
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0x33303382);
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL3, 0x2036315b);
			break;
		case 3: /* 1.485Gbps */
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0x33303362);
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL3, 0x2016315b);
			break;
		default: /* 742.5Mbps, and below */
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0x33604142);
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL3, 0x0016315b);
			break;
		}
	} else if (dw_hdmi_is_compatible(dw_hdmi,
					 "amlogic,meson-gxbb-dw-hdmi")) {
		switch (phy_mode) {
		case 1: /* 5.94Gbps, 3.7125Gbsp */
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0x33353245);
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL3, 0x2100115b);
			break;
		case 2: /* 2.97Gbps */
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0x33634283);
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL3, 0xb000115b);
			break;
		case 3: /* 1.485Gbps, and below */
		default:
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0x33632122);
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL3, 0x2000115b);
			break;
		}
	}
}

static inline void dw_hdmi_phy_reset(struct meson_dw_hdmi *dw_hdmi)
{
	struct meson_drm *priv = dw_hdmi->priv;

	/* Enable and software reset */
	regmap_update_bits(priv->hhi, HHI_HDMI_PHY_CNTL1, 0xf, 0xf);
	
	mdelay(2);

	/* Enable and unreset */
	regmap_update_bits(priv->hhi, HHI_HDMI_PHY_CNTL1, 0xf, 0xe);
	
	mdelay(2); 
}

static void dw_hdmi_set_vclk(struct meson_dw_hdmi *dw_hdmi,
			     struct drm_display_mode *mode)
{
	struct meson_drm *priv = dw_hdmi->priv;
	int vic = drm_match_cea_mode(mode);
	unsigned vclk_freq;
	unsigned venc_freq;
	unsigned hdmi_freq;

	vclk_freq = mode->clock;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		vclk_freq *= 2;

	venc_freq = vclk_freq;
	hdmi_freq = vclk_freq;

	if (meson_venc_hdmi_venc_repeat(vic))
		venc_freq *= 2;

	vclk_freq = max(venc_freq, hdmi_freq);

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		venc_freq /= 2;

	pr_info("%s: vclk:%d venc=%d hdmi=%d enci=%d\n",
		__func__, vclk_freq, venc_freq, hdmi_freq,
		priv->venc.hdmi_use_enci);
	
	meson_vclk_setup(priv, MESON_VCLK_TARGET_HDMI, vclk_freq,
			 venc_freq, hdmi_freq, priv->venc.hdmi_use_enci);
}

static int dw_hdmi_phy_init(const struct dw_hdmi_plat_data *data,
			    struct drm_display_mode *mode, bool cscon)
{
	struct meson_dw_hdmi *dw_hdmi = plat_data_to_meson_dw_hdmi(data);
	struct meson_drm *priv = dw_hdmi->priv;
	unsigned int wr_clk;

	pr_info("%s:%d\n", __func__, __LINE__);

	/* Enable clocks */
	regmap_update_bits(priv->hhi, HHI_HDMI_CLK_CNTL, 0xffff, 0x100);
	regmap_update_bits(priv->hhi, HHI_GCLK_MPEG2, BIT(4), BIT(4));

	/* Bring HDMITX MEM output of power down */
	regmap_update_bits(priv->hhi, HHI_MEM_PD_REG0, 0xff << 8, 0);

	/* Bring out of reset */
	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_SW_RESET,  0);

	/* Enable internal pixclk, tmds_clk, spdif_clk, i2s_clk, cecclk */
	dw_hdmi_top_write_bits(dw_hdmi, HDMITX_TOP_CLK_CNTL,
			       0x3, 0x3);
	dw_hdmi_top_write_bits(dw_hdmi, HDMITX_TOP_CLK_CNTL,
			       0x3 << 4, 0x3 << 4);

	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_MC_LOCKONCLOCK, 0xff);

	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_MC_CLKDIS, 0);

	/* Enable normal output to PHY */
	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_BIST_CNTL, BIT(12));

	/* Configure Color Space Converter */
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_MC_FLOWCTRL, !!cscon);

	/* Configure Audio */
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_AUD_INT, BIT(2) | BIT(3));
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_AUD_INT1, BIT(4));
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_MULTISTREAM_CTRL, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_AUD_CONF0, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_AUD_CONF1, 24);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_AUD_CONF2, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_AUD_SPDIFINT, BIT(2) | BIT(3));
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_AUD_SPDIFINT1, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_AUD_SPDIF0, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_AUD_SPDIF1, 24);

	/* write Audio Infoframe packet configuration */
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_AUDICONF0, BIT(4));
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_AUDICONF1, 3 << 4);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_AUDICONF2, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_AUDICONF3, BIT(5));
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_AUDSCONF, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_AUDSV, BIT(0) | BIT(4));
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_AUDSU, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_AUDSCHNLS0, 0x01);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_AUDSCHNLS1, 0x23);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_AUDSCHNLS2, 0x45);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_AUDSCHNLS3, 0x67);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_AUDSCHNLS4, 0x89);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_AUDSCHNLS5, 0xab);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_AUDSCHNLS6, 0xcd);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_AUDSCHNLS7, 0x2f);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_AUDSCHNLS8, 0xf0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_CTRLQHIGH, 15);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_CTRLQLOW, 3);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_DATAUTO0, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_DATAUTO1, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_DATAUTO2, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_DATMAN, 0);
	/* No HDR */
	dw_hdmi_dwc_write_bits(dw_hdmi, HDMITX_DWC_FC_DATAUTO3, 0x3f, 0xe);
	dw_hdmi_dwc_write_bits(dw_hdmi, HDMITX_DWC_FC_PACKET_TX_EN, BIT(7), 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_RDRB0, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_RDRB1, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_RDRB2, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_RDRB3, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_RDRB4, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_RDRB5, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_RDRB6, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_RDRB7, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_RDRB8, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_RDRB9, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_RDRB10, 0);
	dw_hdmi_dwc_write(dw_hdmi, HDMITX_DWC_FC_RDRB11, 0);
	dw_hdmi_dwc_write_bits(dw_hdmi, HDMITX_DWC_FC_PACKET_TX_EN, BIT(1), BIT(1));
	dw_hdmi_dwc_write_bits(dw_hdmi, HDMITX_DWC_FC_PACKET_TX_EN, BIT(2), BIT(2));

	/* clk40 */
	/* TOFIX clk40 for 4k2k */
	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_TMDS_CLK_PTTN_01, 0);
	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_TMDS_CLK_PTTN_23, 0x03ff03ff);
	dw_hdmi_top_write_bits(dw_hdmi, HDMITX_DWC_FC_SCRAMBLER_CTRL,
				BIT(0), 0);
	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_TMDS_CLK_PTTN_CNTL, 0x1);
	msleep(20);
	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_TMDS_CLK_PTTN_CNTL, 0x2);

	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_INTR_STAT_CLR, 0x1f);

	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_INTR_MASKN, 0);
	//BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4));

	/* Setup PHY parameters */
	meson_hdmi_phy_setup_mode(dw_hdmi, mode);

	/* Setup PHY */
	regmap_update_bits(priv->hhi, HHI_HDMI_PHY_CNTL1, 
			   0xffff << 16, 0x0390 << 16);

	/* BIT_INVERT */
	if (dw_hdmi_is_compatible(dw_hdmi, "amlogic,meson-gxl-dw-hdmi") ||
	    dw_hdmi_is_compatible(dw_hdmi, "amlogic,meson-gxl-dw-hdmi"))
		regmap_update_bits(priv->hhi, HHI_HDMI_PHY_CNTL1,
				   BIT(17), 0);
	else
		regmap_update_bits(priv->hhi, HHI_HDMI_PHY_CNTL1,
				   BIT(17), BIT(17));

	/* Disable clock, fifo, fifo_wr */
	regmap_update_bits(priv->hhi, HHI_HDMI_PHY_CNTL1, 0xf, 0);
	
	msleep(100);
	
	dw_hdmi_phy_reset(dw_hdmi);
	dw_hdmi_phy_reset(dw_hdmi);
	dw_hdmi_phy_reset(dw_hdmi);

	wr_clk = readl_relaxed(priv->io_base + _REG(VPU_HDMI_SETTING));

	/* VPU Bridge Reset */
	if (priv->venc.hdmi_use_enci)
		writel_relaxed(0, priv->io_base + _REG(ENCI_VIDEO_EN));
	else
		writel_relaxed(0, priv->io_base + _REG(ENCP_VIDEO_EN));

	writel_bits_relaxed(0x3, 0,
			    priv->io_base + _REG(VPU_HDMI_SETTING));
	writel_bits_relaxed(0xf << 8, 0,
			    priv->io_base + _REG(VPU_HDMI_SETTING));
	
	if (priv->venc.hdmi_use_enci)
		writel_relaxed(1, priv->io_base + _REG(ENCI_VIDEO_EN));
	else
		writel_relaxed(1, priv->io_base + _REG(ENCP_VIDEO_EN));
	
	writel_bits_relaxed(0xf << 8, wr_clk & (0xf << 8),
			    priv->io_base + _REG(VPU_HDMI_SETTING));


	if (priv->venc.hdmi_use_enci)
		writel_bits_relaxed(0x3, MESON_VENC_SOURCE_ENCI,
				    priv->io_base + _REG(VPU_HDMI_SETTING));
	else
		writel_bits_relaxed(0x3, MESON_VENC_SOURCE_ENCP,
				    priv->io_base + _REG(VPU_HDMI_SETTING));

	return 0;
}

static void dw_hdmi_phy_disable(const struct dw_hdmi_plat_data *data)
{
	struct meson_dw_hdmi *dw_hdmi = plat_data_to_meson_dw_hdmi(data);
	struct meson_drm *priv = dw_hdmi->priv;

	pr_info("%s:%d\n", __func__, __LINE__);

	regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0);
}

/*
 * Workaround until we find a way to use the PHY HPD level irq
 * and read the pad value...
 */
static bool dw_hdmi_read_hpd(const struct dw_hdmi_plat_data *data)
{
	struct meson_dw_hdmi *dw_hdmi = plat_data_to_meson_dw_hdmi(data);
	bool is_connected = !!gpiod_get_value(dw_hdmi->hpd);

	pr_info("%s:%d %d\n", __func__, __LINE__, is_connected);

	dw_hdmi_setup_rx_sense(dw_hdmi->dev, is_connected, is_connected);

	return is_connected;
}

static enum drm_mode_status dw_hdmi_mode_valid(struct drm_connector *connector,
					       struct drm_display_mode *mode)
{
	unsigned vclk_freq;
	unsigned venc_freq;
	unsigned hdmi_freq;
	int vic = drm_match_cea_mode(mode);

	pr_info("%s:%d\n", __func__, __LINE__);

	pr_info("Modeline %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x\n",
		mode->base.id, mode->name, mode->vrefresh, mode->clock,
		mode->hdisplay, mode->hsync_start,
		mode->hsync_end, mode->htotal,
		mode->vdisplay, mode->vsync_start,
		mode->vsync_end, mode->vtotal, mode->type, mode->flags);

	/* For now, only accept VIC modes */
	if (!vic)
		return MODE_BAD;

	/* For now, filter by supported VIC modes */
	if (!meson_venc_hdmi_supported_vic(vic))
		return MODE_BAD;

	vclk_freq = mode->clock;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		vclk_freq *= 2;

	venc_freq = vclk_freq;
	hdmi_freq = vclk_freq;

	if (meson_venc_hdmi_venc_repeat(vic))
		venc_freq *= 2;

	vclk_freq = max(venc_freq, hdmi_freq);

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		venc_freq /= 2;

	pr_info("%s: vclk:%d venc=%d hdmi=%d\n", __func__, vclk_freq, venc_freq, hdmi_freq);

	switch (vclk_freq) {
		case 54000:
		case 74250:
		case 148500:
		case 297000:
		case 594000:
			return MODE_OK;
	}

	return MODE_CLOCK_RANGE;
}

/* Encoder */

static void meson_venc_hdmi_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs meson_venc_hdmi_encoder_funcs = {
	.destroy        = meson_venc_hdmi_encoder_destroy,
};

static int meson_venc_hdmi_encoder_atomic_check(struct drm_encoder *encoder,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	pr_info("%s:%d\n", __func__, __LINE__);

	return 0;
}

static void meson_venc_hdmi_encoder_disable(struct drm_encoder *encoder)
{
	struct meson_dw_hdmi *dw_hdmi = encoder_to_meson_dw_hdmi(encoder);
	struct meson_drm *priv = dw_hdmi->priv;

	pr_info("%s:%d\n", __func__, __LINE__);

	writel_relaxed(0, priv->io_base + _REG(ENCI_VIDEO_EN));
	writel_relaxed(0, priv->io_base + _REG(ENCP_VIDEO_EN));

	writel_bits_relaxed(0x3, 0,
			    priv->io_base + _REG(VPU_HDMI_SETTING));
}

static void meson_venc_hdmi_encoder_enable(struct drm_encoder *encoder)
{
	struct meson_dw_hdmi *dw_hdmi = encoder_to_meson_dw_hdmi(encoder);
	struct meson_drm *priv = dw_hdmi->priv;
	/*unsigned int wr_clk =
		readl_relaxed(priv->io_base + _REG(VPU_HDMI_SETTING));*/

	pr_info("%s:%d\n", __func__, __LINE__);

#if 0
	if (priv->venc.hdmi_use_enci)
		writel_relaxed(0, priv->io_base + _REG(ENCI_VIDEO_EN));
	else
		writel_relaxed(0, priv->io_base + _REG(ENCP_VIDEO_EN));

	writel_bits_relaxed(0x3, 0,
			    priv->io_base + _REG(VPU_HDMI_SETTING));
	writel_bits_relaxed(0xf << 8, 0,
			    priv->io_base + _REG(VPU_HDMI_SETTING));
	
	if (priv->venc.hdmi_use_enci)
		writel_relaxed(1, priv->io_base + _REG(ENCI_VIDEO_EN));
	else
		writel_relaxed(1, priv->io_base + _REG(ENCP_VIDEO_EN));
	
	writel_bits_relaxed(0xf << 8, wr_clk & (0xf << 8),
			    priv->io_base + _REG(VPU_HDMI_SETTING));


	if (priv->venc.hdmi_use_enci)
		writel_bits_relaxed(0x3, MESON_VENC_SOURCE_ENCI,
				    priv->io_base + _REG(VPU_HDMI_SETTING));
	else
		writel_bits_relaxed(0x3, MESON_VENC_SOURCE_ENCP,
				    priv->io_base + _REG(VPU_HDMI_SETTING));
#endif
}

static void meson_venc_hdmi_encoder_mode_set(struct drm_encoder *encoder,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{
	struct meson_dw_hdmi *dw_hdmi = encoder_to_meson_dw_hdmi(encoder);
	struct meson_drm *priv = dw_hdmi->priv;
	int vic = drm_match_cea_mode(mode);

	pr_info("%s: '%s' vic=%d\n", __func__, mode->name, vic);

	/* Should have been filtered */
	if (!vic)
		return;

	/* VENC + VENC-DVI Mode setup */
	meson_venc_hdmi_mode_set(priv, vic, mode);

	/* VCLK Set clock */
	dw_hdmi_set_vclk(dw_hdmi, mode);
}

static const struct drm_encoder_helper_funcs
				meson_venc_hdmi_encoder_helper_funcs = {
	.atomic_check	= meson_venc_hdmi_encoder_atomic_check,
	.disable	= meson_venc_hdmi_encoder_disable,
	.enable		= meson_venc_hdmi_encoder_enable,
	.mode_set	= meson_venc_hdmi_encoder_mode_set,
};

/* DW HDMI Regmap */

static int meson_dw_hdmi_reg_read(void *context, unsigned int reg,
				  unsigned int *result)
{
	pr_info("%s: reg=%x\n", __func__, reg);

	*result = dw_hdmi_dwc_read(context, reg);

	return 0;

}

static int meson_dw_hdmi_reg_write(void *context, unsigned int reg,
				   unsigned int val)
{
	pr_info("%s: reg=%x val=%x", __func__, reg, val);

	dw_hdmi_dwc_write(context, reg, val);

	return 0;
}

static const struct regmap_config meson_dw_hdmi_regmap_config = {
	.reg_bits = 32,
	.val_bits = 8,
 
	.reg_read = meson_dw_hdmi_reg_read,
	.reg_write = meson_dw_hdmi_reg_write,
 
	.max_register = 0x10000,
};

static bool meson_hdmi_connector_is_available(struct device *dev)
{
	struct device_node *ep, *remote;

	/* HDMI Connector is on the second port, first endpoint */
	ep = of_graph_get_endpoint_by_regs(dev->of_node, 1, 0);
	if (!ep)
		return false;

	/* If the endpoint node exists, consider it enabled */
	remote = of_graph_get_remote_port(ep);
	if (remote) {
		of_node_put(ep);
		return true;
	}

	of_node_put(ep);
	of_node_put(remote);

	return false;
}

static int meson_dw_hdmi_bind(struct device *dev, struct device *master,
				void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct meson_dw_hdmi *meson_dw_hdmi;
	struct drm_device *drm = data;
	struct meson_drm *priv = drm->dev_private;
	struct dw_hdmi_plat_data *dw_plat_data;
	struct drm_encoder *encoder;
	struct resource *res;
	int ret;
	pr_info("%s:%d\n", __func__, __LINE__);

	if (!meson_hdmi_connector_is_available(dev)) {
		dev_info(drm->dev, "HDMI Output connector not available\n");
		return -ENODEV;
	}
	pr_info("%s:%d\n", __func__, __LINE__);

	meson_dw_hdmi = devm_kzalloc(dev, sizeof(*meson_dw_hdmi),
				     GFP_KERNEL);
	if (!meson_dw_hdmi)
		return -ENOMEM;
	pr_info("%s:%d\n", __func__, __LINE__);

	meson_dw_hdmi->priv = priv;
	meson_dw_hdmi->dev = dev;
	dw_plat_data = &meson_dw_hdmi->dw_plat_data;
	encoder = &meson_dw_hdmi->encoder;
	pr_info("%s:%d\n", __func__, __LINE__);

	meson_dw_hdmi->hdmitx_apb = devm_reset_control_get_exclusive(dev,
						"hdmitx_apb");
	if (IS_ERR(meson_dw_hdmi->hdmitx_apb)) {
		dev_err(dev, "Failed to get hdmitx_apb reset\n");
		return PTR_ERR(meson_dw_hdmi->hdmitx_apb);
	}

	meson_dw_hdmi->hdmitx = devm_reset_control_get_exclusive(dev,
						"hdmitx");
	if (IS_ERR(meson_dw_hdmi->hdmitx)) {
		dev_err(dev, "Failed to get hdmitx reset\n");
		return PTR_ERR(meson_dw_hdmi->hdmitx);
	}

	meson_dw_hdmi->hdmitx_phy = devm_reset_control_get_exclusive(dev,
						"hdmitx_phy");
	if (IS_ERR(meson_dw_hdmi->hdmitx_phy)) {
		dev_err(dev, "Failed to get hdmitx_phy reset\n");
		return PTR_ERR(meson_dw_hdmi->hdmitx_phy);
	}

	meson_dw_hdmi->hpd = devm_gpiod_get(dev, "hpd", GPIOD_IN);
	if (IS_ERR(meson_dw_hdmi->hpd))
		return PTR_ERR(meson_dw_hdmi->hpd);
	pr_info("%s:%d\n", __func__, __LINE__);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	meson_dw_hdmi->hdmitx = devm_ioremap_resource(dev, res);
	if (IS_ERR(meson_dw_hdmi->hdmitx))
		return PTR_ERR(meson_dw_hdmi->hdmitx);
	pr_info("%s:%d\n", __func__, __LINE__);

	dw_plat_data->regm = devm_regmap_init(dev, NULL, meson_dw_hdmi,
					      &meson_dw_hdmi_regmap_config);
	if (IS_ERR(dw_plat_data->regm))
		return PTR_ERR(dw_plat_data->regm);
	pr_info("%s:%d\n", __func__, __LINE__);

	/* Encoder */

	drm_encoder_helper_add(encoder, &meson_venc_hdmi_encoder_helper_funcs);

	ret = drm_encoder_init(drm, encoder, &meson_venc_hdmi_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, "meson_hdmi");
	if (ret) {
		dev_err(priv->dev, "Failed to init HDMI encoder\n");
		return ret;
	}

	encoder->possible_crtcs = BIT(0);
	pr_info("%s:%d\n", __func__, __LINE__);

	/* Enable clocks */
	regmap_update_bits(priv->hhi, HHI_HDMI_CLK_CNTL, 0xffff, 0x100);
	regmap_update_bits(priv->hhi, HHI_GCLK_MPEG2, BIT(4), BIT(4));
	regmap_update_bits(priv->hhi, HHI_GCLK_OTHER, BIT(17), BIT(17));

	/* Bring HDMITX MEM output of power down */
	regmap_update_bits(priv->hhi, HHI_MEM_PD_REG0, 0xff << 8, 0);

	/* Reset HDMITX APB & TX & PHY */
	reset_control_reset(meson_dw_hdmi->hdmitx_apb);
	reset_control_reset(meson_dw_hdmi->hdmitx_ctrl);
	reset_control_reset(meson_dw_hdmi->hdmitx_phy);

	/* Enable APB3 fail on error */
	writel_bits_relaxed(BIT(15), BIT(15),
			    meson_dw_hdmi->hdmitx + HDMITX_TOP_CTRL_REG);
	writel_bits_relaxed(BIT(15), BIT(15),
			    meson_dw_hdmi->hdmitx + HDMITX_DWC_CTRL_REG);

	/* Bring out of reset */
	dw_hdmi_top_write(meson_dw_hdmi, HDMITX_TOP_SW_RESET,  0);

	msleep(1);
	
	dw_hdmi_top_write(meson_dw_hdmi, HDMITX_TOP_CLK_CNTL, 0xff);

	dw_hdmi_dwc_write(meson_dw_hdmi, HDMITX_DWC_MC_LOCKONCLOCK, 0xff);
	dw_hdmi_dwc_write(meson_dw_hdmi, HDMITX_DWC_MC_CLKDIS, 0x00);

	/* Bridge / Connector */

	dw_plat_data->dev_type = MESON_GX_HDMI;
	dw_plat_data->mode_valid = dw_hdmi_mode_valid;
	dw_plat_data->hdmi_phy_init = dw_hdmi_phy_init;
	dw_plat_data->hdmi_phy_disable = dw_hdmi_phy_disable;
	dw_plat_data->hdmi_read_hpd = dw_hdmi_read_hpd;
	pr_info("%s:%d\n", __func__, __LINE__);

	ret = dw_hdmi_bind(dev, master, data, encoder,
			   NULL, -1, &meson_dw_hdmi->dw_plat_data);
	if (ret)
		return ret;

	return 0;
}

static void meson_dw_hdmi_unbind(struct device *dev, struct device *master,
				   void *data)
{
	dw_hdmi_unbind(dev, master, data);
}

static const struct component_ops meson_dw_hdmi_ops = {
	.bind	= meson_dw_hdmi_bind,
	.unbind	= meson_dw_hdmi_unbind,
};

static int meson_dw_hdmi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &meson_dw_hdmi_ops);
}

static int meson_dw_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &meson_dw_hdmi_ops);

	return 0;
}

static const struct of_device_id meson_dw_hdmi_of_table[] = {
	{ .compatible = "amlogic,meson-gxbb-dw-hdmi" },
	{ .compatible = "amlogic,meson-gxl-dw-hdmi" },
	{ .compatible = "amlogic,meson-gxm-dw-hdmi" },
	{ }
};
MODULE_DEVICE_TABLE(of, meson_dw_hdmi_of_table);

static struct platform_driver meson_dw_hdmi_platform_driver = {
	.probe		= meson_dw_hdmi_probe,
	.remove		= meson_dw_hdmi_remove,
	.driver		= {
		.name		= "meson-dw-hdmi",
		.of_match_table	= meson_dw_hdmi_of_table,
	},
};
module_platform_driver(meson_dw_hdmi_platform_driver);
