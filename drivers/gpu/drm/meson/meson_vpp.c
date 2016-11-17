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
#include "meson_vpp.h"
#include "meson_registers.h"

void meson_vpp_enable_osd1(struct meson_drm *priv)
{
	pr_debug("%s:%s\n", __FILE__, __func__);

	writel_bits_relaxed(VPP_OSD1_POSTBLEND, VPP_OSD1_POSTBLEND,
			    priv->io_base + _REG(VPP_MISC));
}

void meson_vpp_disable_osd1(struct meson_drm *priv)
{
	pr_debug("%s:%s\n", __FILE__, __func__);

	writel_bits_relaxed(VPP_OSD1_POSTBLEND, 0,
			    priv->io_base + _REG(VPP_MISC));
}

void meson_vpp_enable_postblend(struct meson_drm *priv)
{
	pr_debug("%s:%s\n", __FILE__, __func__);

	writel_bits_relaxed(VPP_POSTBLEND_ENABLE, VPP_POSTBLEND_ENABLE,
			    priv->io_base + _REG(VPP_MISC));
}

void meson_vpp_disable_postblend(struct meson_drm *priv)
{
	pr_debug("%s:%s\n", __FILE__, __func__);

	writel_bits_relaxed(VPP_POSTBLEND_ENABLE, 0,
			    priv->io_base + _REG(VPP_MISC));
}

static void meson_vpp_load_matrix(struct meson_drm *priv)
{
	// TODO OETF, EOTF ....
}

void meson_vpp_setup_mux(struct meson_drm *priv, unsigned int mux)
{
	writel(mux, priv->io_base + _REG(VPU_VIU_VENC_MUX_CTRL));	
}

/* Setup Vertical Scaler to handle to interlace output case */
void meson_vpp_setup_interlace_vscaler_osd1(struct meson_drm *priv,
					    struct drm_rect *input) 
{
	writel_relaxed((1 << 3) /* Enable scaler */ |
		       (1 << 2), /* Select OSD1 */
			priv->io_base + _REG(VPP_OSD_SC_CTRL0));

	writel_relaxed(((drm_rect_width(input) - 1) << 16) |
		       (drm_rect_height(input) - 1),
			priv->io_base + _REG(VPP_OSD_SCI_WH_M1));
	/* 2:1 scaling */
	writel_relaxed(((input->x1) << 16) | (input->x2),
			priv->io_base + _REG(VPP_OSD_SCO_H_START_END));
	writel_relaxed(((input->y1 >> 1) << 16) | (input->y2 >> 1),
			priv->io_base + _REG(VPP_OSD_SCO_V_START_END));

	/* 2:1 scaling values */
	writel_relaxed(BIT(16), priv->io_base + _REG(VPP_OSD_VSC_INI_PHASE));
	writel_relaxed(BIT(25), priv->io_base + _REG(VPP_OSD_VSC_PHASE_STEP));

	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_HSC_CTRL0));

	writel_relaxed((4 << 0) /* osd_vsc_bank_length */ |
		       (4 << 3) /* osd_vsc_top_ini_rcv_num0 */ |
		       (1 << 8) /* osd_vsc_top_rpt_p0_num0 */ |
		       (6 << 11) /* osd_vsc_bot_ini_rcv_num0 */ |
		       (2 << 16) /* osd_vsc_bot_rpt_p0_num0 */ |
		       (1 << 23) /* osd_prog_interlace */ |
		       (1 << 24), /* Enable vertical scaler */
			priv->io_base + _REG(VPP_OSD_VSC_CTRL0));
}

void meson_vpp_disable_interlace_vscaler_osd1(struct meson_drm *priv)
{
	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_SC_CTRL0));
	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_VSC_CTRL0));
	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_HSC_CTRL0));
}

static unsigned int vpp_filter_coefs_4point_bspline[] = {
	0x15561500, 0x14561600, 0x13561700, 0x12561800,
	0x11551a00, 0x11541b00, 0x10541c00, 0x0f541d00,
	0x0f531e00, 0x0e531f00, 0x0d522100, 0x0c522200,
	0x0b522300, 0x0b512400, 0x0a502600, 0x0a4f2700,
	0x094e2900, 0x084e2a00, 0x084d2b00, 0x074c2c01,
	0x074b2d01, 0x064a2f01, 0x06493001, 0x05483201,
	0x05473301, 0x05463401, 0x04453601, 0x04433702,
	0x04423802, 0x03413a02, 0x03403b02, 0x033f3c02,
	0x033d3d03
};

static void meson_vpp_write_scaling_filter_coefs(struct meson_drm *priv,
						 const unsigned int *coefs,
						 bool is_horizontal)
{
	int i;

	writel_relaxed((is_horizontal ? 1 : 0) << 8,
			priv->io_base + _REG(VPP_OSD_SCALE_COEF_IDX));
	for (i = 0; i < 33; i++)
		writel_relaxed(coefs[i],
				priv->io_base + _REG(VPP_OSD_SCALE_COEF));
}

void meson_vpp_init(struct meson_drm *priv)
{
	pr_debug("%s:%s\n", __FILE__, __func__);

	/* set dummy data default YUV black */
	writel_relaxed(0x108080, priv->io_base + _REG(VPP_DUMMY_DATA1));

	meson_vpp_load_matrix(priv);

	/* Turn off POSTBLEND */
	meson_vpp_disable_postblend(priv);

	/* Force all planes off */
	writel_bits_relaxed(VPP_OSD1_POSTBLEND | VPP_OSD2_POSTBLEND |
			    VPP_VD1_POSTBLEND | VPP_VD2_POSTBLEND, 0,
			    priv->io_base + _REG(VPP_MISC));

	/* Disable Scalers */
	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_SC_CTRL0));
	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_VSC_CTRL0));
	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_HSC_CTRL0));
	
	/* Write in the proper filter coefficients. */
	meson_vpp_write_scaling_filter_coefs(priv,
				vpp_filter_coefs_4point_bspline, false);
	meson_vpp_write_scaling_filter_coefs(priv,
				vpp_filter_coefs_4point_bspline, true);
}
