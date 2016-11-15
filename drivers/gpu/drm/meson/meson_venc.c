/* 
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <drm/drmP.h>
#include "meson_drv.h"
#include "meson_venc.h"
#include "meson_vpu.h"
#include "meson_vpp.h"
#include "meson_registers.h"

/* HHI Registers */
#define HHI_VID_PLL_CLK_DIV	0x1a0 /* 0x68 offset in data sheet */
#define HHI_VIID_CLK_DIV	0x128 /* 0x4a offset in data sheet */
#define HHI_VIID_CLK_CNTL	0x12c /* 0x4b offset in data sheet */
#define HHI_VID_CLK_DIV		0x164 /* 0x59 offset in data sheet */
#define HHI_VID_CLK_CNTL2	0x194 /* 0x65 offset in data sheet */

#define HHI_VDAC_CNTL0		0x2F4 /* 0xbd offset in data sheet */
#define HHI_VDAC_CNTL1		0x2F8 /* 0xbe offset in data sheet */

#define HHI_HDMI_PLL_CNTL	0x320 /* 0xc8 offset in data sheet */
#define HHI_HDMI_PLL_CNTL2	0x324 /* 0xc9 offset in data sheet */
#define HHI_HDMI_PLL_CNTL3	0x328 /* 0xca offset in data sheet */
#define HHI_HDMI_PLL_CNTL4	0x32C /* 0xcb offset in data sheet */
#define HHI_HDMI_PLL_CNTL5	0x330 /* 0xcc offset in data sheet */
#define HHI_HDMI_PLL_CNTL6	0x334 /* 0xcd offset in data sheet */

struct meson_cvbs_enci_mode meson_cvbs_enci_pal = {
	.hso_begin = 3,
	.hso_end = 129,
	.vso_even = 3,
	.vso_odd = 260,
	.macv_max_amp = 7,
	.video_prog_mode = 0xff,
	.video_mode = 0x13,
	.sch_adjust = 0x28,
	.pixel_start = 251,
	.pixel_end = 1691,
	.top_field_line_start = 22,
	.top_field_line_end = 310,
	.bottom_field_line_start = 23,
	.bottom_field_line_end = 311,
	.video_saturation = 7,
	.video_contrast = 0,
	.video_brightness = 0,
	.video_hue = 0,
	.analog_sync_adj = 0,
};

struct meson_cvbs_enci_mode meson_cvbs_enci_ntsc = {
	.hso_begin = 5,
	.hso_end = 129,
	.vso_even = 3,
	.vso_odd = 260,
	.macv_max_amp = 0xb,
	.video_prog_mode = 0xf0,
	.video_mode = 0x8,
	.sch_adjust = 0x20,
	.pixel_start = 227,
	.pixel_end = 1667,
	.top_field_line_start = 18,
	.top_field_line_end = 258,
	.bottom_field_line_start = 19,
	.bottom_field_line_end = 259,
	.video_saturation = 18,
	.video_contrast = 3,
	.video_brightness = 0,
	.video_hue = 0,
	.analog_sync_adj = 0x9c00,
};

/* TOFIX: Convert to clock framework */
void meson_venci_cvbs_clock_config(struct meson_drm *priv)
{
	unsigned int val;

	if (of_machine_is_compatible("amlogic,meson-gxbb")) {
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL, 0x5800023d);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL2, 0x00404e00);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL3, 0x0d5c5091);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL4, 0x801da72c);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL5, 0x71486980);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL6, 0x00000e55);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL, 0x4800023d);
	} else if (of_machine_is_compatible("amlogic,meson-gxm") ||
		   of_machine_is_compatible("amlogic,meson-gxl")) {
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL, 0x4000027b);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL2, 0x800cb300);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL3, 0xa6212844);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL4, 0x0c4d000c);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL5, 0x001fa729);
		regmap_write(priv->hhi, HHI_HDMI_PLL_CNTL6, 0x01a31500);
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL, BIT(28), BIT(28));
		regmap_update_bits(priv->hhi, HHI_HDMI_PLL_CNTL, BIT(28), 0);
	}

	/* Poll for bit 31 */
	regmap_read_poll_timeout(priv->hhi, HHI_HDMI_PLL_CNTL, val,
				 (val & BIT(31)), 10, 0);

	/* Disable VCLK2 [19] */
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_CNTL, BIT(19), BIT(19));

	/* Disable the div output clock */
	/* GATE disable */
	regmap_update_bits(priv->hhi, HHI_VID_PLL_CLK_DIV, BIT(19), 0);
	/* init_set 0 */
	regmap_update_bits(priv->hhi, HHI_VID_PLL_CLK_DIV, BIT(15), 0);
	/* bypass */
	regmap_update_bits(priv->hhi, HHI_VID_PLL_CLK_DIV, BIT(18), BIT(18));

	/* Enable the final output clock */
	regmap_update_bits(priv->hhi, HHI_VID_PLL_CLK_DIV, BIT(19), BIT(19));

	/* setup the XD divider value */
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_DIV, 0xFF, (55 - 1));

	/* Bit[18:16] - v2_cntl_clk_in_sel */
	/* select vid_pll for vclk2 */
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_CNTL, (0x7 << 16), (4 << 16));
	/* enable vclk2 gate */
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_CNTL, BIT(19), BIT(19));

	/* [15:12] encl_clk_sel, select vclk2_div1 */
	/* select vclk_div1 for enci */
	regmap_update_bits(priv->hhi, HHI_VID_CLK_DIV, (0xf << 28), (8 << 28));
	/* select vclk_div1 for vdac */
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_DIV, (0xf << 28), (8 << 28));

	/* release vclk2_div_reset and enable vclk2_div */
	/* enable gate for vclk2 */
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_DIV, BIT(17) | BIT(16), BIT(16));

	/* enable vclk2_div1 gate */
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_CNTL, BIT(0), BIT(0));
	/* reset vclk2 */
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_CNTL, BIT(15), BIT(15));
	regmap_update_bits(priv->hhi, HHI_VIID_CLK_CNTL, BIT(15), 0);

	/* enable enci_clk */
	regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL2, BIT(0), BIT(0));
	/* enable vdac_clk */
	regmap_update_bits(priv->hhi, HHI_VID_CLK_CNTL2, BIT(4), BIT(4));
}

void meson_venci_cvbs_mode_set(struct meson_drm *priv,
			       struct meson_cvbs_enci_mode *mode)
{
	pr_info("%s:%s\n", __FILE__, __func__);

	/* CVBS Filter settings */
	writel_relaxed(0x12, priv->io_base + _REG(ENCI_CFILT_CTRL));
	writel_relaxed(0x12, priv->io_base + _REG(ENCI_CFILT_CTRL2));

	/* Digital Video Select : Interlace, clk27 clk, external */
	writel_relaxed(0, priv->io_base + _REG(VENC_DVI_SETTING));
	
	/* Reset Video Mode */
	writel_relaxed(0, priv->io_base + _REG(ENCI_VIDEO_MODE));
	writel_relaxed(0, priv->io_base + _REG(ENCI_VIDEO_MODE_ADV));
	
	/* Horizontal sync signal output */
	writel_relaxed(mode->hso_begin,
			priv->io_base + _REG(ENCI_SYNC_HSO_BEGIN));
	writel_relaxed(mode->hso_end,
			priv->io_base + _REG(ENCI_SYNC_HSO_END));
	
	/* Vertical Sync lines */
	writel_relaxed(mode->vso_even,
			priv->io_base + _REG(ENCI_SYNC_VSO_EVNLN));
	writel_relaxed(mode->vso_odd,
			priv->io_base + _REG(ENCI_SYNC_VSO_ODDLN));
	
	/* Macrovision max amplitude change */
	writel_relaxed(0x8100 + mode->macv_max_amp,
			priv->io_base + _REG(ENCI_MACV_MAX_AMP));
	
	/* Video mode */
	writel_relaxed(mode->video_prog_mode,
			priv->io_base + _REG(VENC_VIDEO_PROG_MODE));
	writel_relaxed(mode->video_mode,
			priv->io_base + _REG(ENCI_VIDEO_MODE));

	/* Advanced Video Mode :
	 * Demux shifting 0x2
	 * Blank line end at line17/22
	 * High bandwidth Luma Filter
	 * Low bandwidth Chroma Filter
	 * Bypass luma low pass filter
	 * No macrovision on CSYNC
	 */
	writel_relaxed(0x26, priv->io_base + _REG(ENCI_VIDEO_MODE_ADV));
	
	writel(mode->sch_adjust, priv->io_base + _REG(ENCI_VIDEO_SCH));

	/* Sync mode : MASTER Master mode, free run, send HSO/VSO out */
	writel_relaxed(0x07, priv->io_base + _REG(ENCI_SYNC_MODE));

	/* 0x3 Y, C, and Component Y delay */
	writel_relaxed(0x333, priv->io_base + _REG(ENCI_YC_DELAY));
	
	/* Timings */
	writel_relaxed(mode->pixel_start,
			priv->io_base + _REG(ENCI_VFIFO2VD_PIXEL_START));
	writel_relaxed(mode->pixel_end,
			priv->io_base + _REG(ENCI_VFIFO2VD_PIXEL_END));
	
	writel_relaxed(mode->top_field_line_start,
			priv->io_base + _REG(ENCI_VFIFO2VD_LINE_TOP_START));
	writel_relaxed(mode->top_field_line_end,
			priv->io_base + _REG(ENCI_VFIFO2VD_LINE_TOP_END));
	
	writel_relaxed(mode->bottom_field_line_start,
			priv->io_base + _REG(ENCI_VFIFO2VD_LINE_BOT_START));
	writel_relaxed(mode->bottom_field_line_end,
			priv->io_base + _REG(ENCI_VFIFO2VD_LINE_BOT_END));
	
	/* Internal Venc, Internal VIU Sync, Internal Vencoder */
	writel_relaxed(0, priv->io_base + _REG(VENC_SYNC_ROUTE));
	
	/* UNreset Interlaced TV Encoder */
	writel_relaxed(0, priv->io_base + _REG(ENCI_DBG_PX_RST));
	
	/* Enable Interlace encoder field change interrupt */
	writel_relaxed(2, priv->io_base + _REG(VENC_INTCTRL));
	
	/* Enable Vfifo2vd, Y_Cb_Y_Cr select */
	writel_relaxed(0x4e01, priv->io_base + _REG(ENCI_VFIFO2VD_CTL));
	
	/* Power UP Dacs */
	writel_relaxed(0, priv->io_base + _REG(VENC_VDAC_SETTING));
	
	/* Video Upsampling */
	writel_relaxed(0x0061, priv->io_base + _REG(VENC_UPSAMPLE_CTRL0));
	writel_relaxed(0x4061, priv->io_base + _REG(VENC_UPSAMPLE_CTRL1));
	writel_relaxed(0x5061, priv->io_base + _REG(VENC_UPSAMPLE_CTRL2));
	
	/* Select Interlace Y DACs */
	writel_relaxed(0, priv->io_base + _REG(VENC_VDAC_DACSEL0));
	writel_relaxed(0, priv->io_base + _REG(VENC_VDAC_DACSEL1));
	writel_relaxed(0, priv->io_base + _REG(VENC_VDAC_DACSEL2));
	writel_relaxed(0, priv->io_base + _REG(VENC_VDAC_DACSEL3));
	writel_relaxed(0, priv->io_base + _REG(VENC_VDAC_DACSEL4));
	writel_relaxed(0, priv->io_base + _REG(VENC_VDAC_DACSEL5));
	
	/* Select ENCI for VIU */
	meson_vpp_setup_mux(priv, 0x5);
	
	/* Enable ENCI FIFO */
	writel_relaxed(0x2000, priv->io_base + _REG(VENC_VDAC_FIFO_CTRL));
	
	/* Select ENCI DACs 0, 1, 4, and 5 */
	writel_relaxed(0x11, priv->io_base + _REG(ENCI_DACSEL_0));
	writel_relaxed(0x11, priv->io_base + _REG(ENCI_DACSEL_1));
	
	/* Interlace video enable */
	writel_relaxed(1, priv->io_base + _REG(ENCI_VIDEO_EN));
	
	/* Configure Video Saturation / Contrast / Brightness / Hue */
	writel_relaxed(mode->video_saturation,
			priv->io_base + _REG(ENCI_VIDEO_SAT));
	writel_relaxed(mode->video_contrast,
			priv->io_base + _REG(ENCI_VIDEO_CONT));
	writel_relaxed(mode->video_brightness,
			priv->io_base + _REG(ENCI_VIDEO_BRIGHT));
	writel_relaxed(mode->video_hue,
			priv->io_base + _REG(ENCI_VIDEO_HUE));
	
	/* Enable DAC0 Filter */
	writel_relaxed(0x1, priv->io_base + _REG(VENC_VDAC_DAC0_FILT_CTRL0));
	writel_relaxed(0xfc48, priv->io_base + _REG(VENC_VDAC_DAC0_FILT_CTRL1));
	
	/* 0 in Macrovision register 0 */
	writel_relaxed(0, priv->io_base + _REG(ENCI_MACV_N0));
	
	/* Analog Synchronization and color burst value adjust */
	writel_relaxed(mode->analog_sync_adj,
			priv->io_base + _REG(ENCI_SYNC_ADJ));

	meson_venci_cvbs_clock_config(priv);
}

void meson_venci_cvbs_enable(struct meson_drm *priv)
{
	pr_info("%s:%s\n", __FILE__, __func__);

	/* VDAC0 source is not from ATV */
	writel_bits_relaxed(BIT(5), 0, priv->io_base + _REG(VENC_VDAC_DACSEL0));

	if (of_machine_is_compatible("amlogic,meson-gxbb"))
		regmap_write(priv->hhi, HHI_VDAC_CNTL0, 1);
	else if (of_machine_is_compatible("amlogic,meson-gxbb") ||
		   of_machine_is_compatible("amlogic,meson-gxl"))
		regmap_write(priv->hhi, HHI_VDAC_CNTL0, 0xf0001);

	regmap_write(priv->hhi, HHI_VDAC_CNTL1, 0);
}

void meson_venci_cvbs_disable(struct meson_drm *priv)
{
	pr_info("%s:%s\n", __FILE__, __func__);

	regmap_write(priv->hhi, HHI_VDAC_CNTL0, 0);
	regmap_write(priv->hhi, HHI_VDAC_CNTL1, 0);
}

/* Returns the current ENCI field polarity */
unsigned meson_venci_get_field(struct meson_drm *priv)
{
	return readl_relaxed(priv->io_base + _REG(ENCI_INFO_READ)) & BIT(29);
}

void meson_venc_init(struct meson_drm *priv)
{
	/* Disable all encoders */
	writel_relaxed(0, priv->io_base + _REG(ENCI_VIDEO_EN));
	writel_relaxed(0, priv->io_base + _REG(ENCP_VIDEO_EN));
	writel_relaxed(0, priv->io_base + _REG(ENCL_VIDEO_EN));

	meson_venci_cvbs_disable(priv);
}
