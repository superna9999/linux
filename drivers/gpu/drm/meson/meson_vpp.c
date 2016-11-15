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
	pr_info("%s:%s\n", __FILE__, __func__);

	writel_bits_relaxed(VPP_OSD1_POSTBLEND, VPP_OSD1_POSTBLEND,
			    priv->io_base + _REG(VPP_MISC));
}

void meson_vpp_disable_osd1(struct meson_drm *priv)
{
	pr_info("%s:%s\n", __FILE__, __func__);

	writel_bits_relaxed(VPP_OSD1_POSTBLEND, 0,
			    priv->io_base + _REG(VPP_MISC));
}

void meson_vpp_enable_postblend(struct meson_drm *priv)
{
	pr_info("%s:%s\n", __FILE__, __func__);

	writel_bits_relaxed(VPP_POSTBLEND_ENABLE, VPP_POSTBLEND_ENABLE,
			    priv->io_base + _REG(VPP_MISC));
}

void meson_vpp_disable_postblend(struct meson_drm *priv)
{
	pr_info("%s:%s\n", __FILE__, __func__);

	writel_bits_relaxed(VPP_OSD1_POSTBLEND, 0,
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

void meson_vpp_init(struct meson_drm *priv)
{
	/* set dummy data default YUV black */
	if (of_machine_is_compatible("amlogic,meson-gxbb"))
		writel_relaxed(0x108080,
				priv->io_base + _REG(VPP_DUMMY_DATA1));
	else if (of_machine_is_compatible("amlogic,meson-gxm") ||
		   of_machine_is_compatible("amlogic,meson-gxl"))
		writel_relaxed(0x1020080,
				priv->io_base + _REG(VPP_DUMMY_DATA1));

	meson_vpp_load_matrix(priv);

	/* Disable Scalers */
	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_SC_CTRL0));
	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_VSC_CTRL0));
	writel_relaxed(0, priv->io_base + _REG(VPP_OSD_HSC_CTRL0));

	/* Force all planes off */
	writel_bits_relaxed(VPP_OSD1_POSTBLEND | VPP_OSD2_POSTBLEND |
			    VPP_VD1_POSTBLEND | VPP_VD2_POSTBLEND, 0,
			    priv->io_base + _REG(VPP_MISC));

	/* Turn off POSTBLEND */
	meson_vpp_disable_postblend(priv);
}
