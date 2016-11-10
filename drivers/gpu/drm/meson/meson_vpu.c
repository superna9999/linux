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
#include "meson_vpu.h"
#include "meson_registers.h"


/* HHI Registers */
#define HHI_MEM_PD_REG0		0x100 /* 0x40 offset in data sheet */
#define HHI_VPU_MEM_PD_REG0	0x104 /* 0x41 offset in data sheet */
#define HHI_VPU_MEM_PD_REG1	0x108 /* 0x42 offset in data sheet */

static void meson_vpu_setclk(struct meson_drm *priv)
{
	/*
	clk_prepare_enable(priv->clk_vpu);
	clk_prepare_enable(priv->clk_vpu0);
	clk_set_parent(priv->clk_vpu0, "fclk_div3");
	rate_fclk_div3 = clk_get_rate(priv->fclk_div3);
	clk_set_rate(priv->clk_vpu0, rate_fclk_div3);
	clk_set_parent(priv->clk_vpu, "vpu0");
	*/
}

static void meson_vpu_reset(struct meson_drm *priv)
{
	/* VIU VENC vcbus Hdmitx_capb3 */
	/* BT656 */
	/* Hdmi system reset */
	/* DVIN_RESET RDMA VENCI VENCP VDAC VDI6 VENCL */
	/* vid_lock */
	/*
	   resets_count = count_phandle("resets");
	   for (i = 0 ; i < resets_count ; ++i) {
	   	reset = of_reset_control_get_exclusive_by_index(dev, i);
		reset_control_reset(reset);
	 }
	*/
}

static void meson_vpu_poweron(struct meson_drm *priv)
{
	/*
	   HDMI Power On
	   [8] 0=power on
	   writel(readl(priv->io_ao_rti + AO_RTI_GEN_PWR_SLEEP0) & ~BIT(8),
	   		priv->io_ao_rti + AO_RTI_GEN_PWR_SLEEP0);
	*/

	/* HW Blocks Power Up */
	regmap_write(priv->hhi, HHI_VPU_MEM_PD_REG0, 0);
	regmap_write(priv->hhi, HHI_VPU_MEM_PD_REG1, 0);

	/* HDMI Memory Power Up */
	regmap_update_bits(priv->hhi, HHI_MEM_PD_REG0, (0xff << 8), 0);

	meson_vpu_reset(priv);

	/*
	   [9] VPU_HDMI 1=isolated
	   writel(readl(priv->io_ao_rti + AO_RTI_GEN_PWR_SLEEP0) & ~BIT(9),
	   		priv->io_ao_rti + AO_RTI_GEN_PWR_SLEEP0);
	*/
}

void meson_vpu_init(struct meson_drm *priv)
{
	pr_info("%s:%s\n", __FILE__, __func__);

	meson_vpu_setclk(priv);
	meson_vpu_poweron(priv);
}
