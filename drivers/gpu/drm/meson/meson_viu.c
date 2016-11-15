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
#include "meson_viu.h"
#include "meson_vpp.h"
#include "meson_venc.h"
#include "meson_canvas.h"
#include "meson_registers.h"

enum osd_w0_bitflags {
	OSD_ENDIANNESS_BE = (0x00 << 15),
	OSD_ENDIANNESS_LE = (0x01 << 15),

	OSD_BLK_MODE_422 = (0x03 << 8),
	OSD_BLK_MODE_16  = (0x04 << 8),
	OSD_BLK_MODE_32  = (0x05 << 8),
	OSD_BLK_MODE_24  = (0x07 << 8),

	OSD_OUTPUT_COLOR_YUV = (0x00 << 7),
	OSD_OUTPUT_COLOR_RGB = (0x01 << 7),

	OSD_COLOR_MATRIX_32_RGBA = (0x00 << 2),
	OSD_COLOR_MATRIX_32_ARGB = (0x01 << 2),
	OSD_COLOR_MATRIX_32_ABGR = (0x02 << 2),
	OSD_COLOR_MATRIX_32_BGRA = (0x03 << 2),

	OSD_INTERLACE_ENABLED  = (0x01 << 1),
	OSD_INTERLACE_ODD      = (0x01 << 0),
	OSD_INTERLACE_EVEN     = (0x00 << 0),
};

/* Takes a fixed 16.16 number and converts it to integer. */
static inline int64_t fixed16_to_int(int64_t value)
{
	return value >> 16;
}

void meson_viu_update_osd1(struct meson_drm *priv, struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct drm_rect src = {
		.x1 = (state->src_x),
		.y1 = (state->src_y),
		.x2 = (state->src_x + state->src_w),
		.y2 = (state->src_y + state->src_h),
	};
	struct drm_rect dest = {
		.x1 = state->crtc_x,
		.y1 = state->crtc_y,
		.x2 = state->crtc_x + state->crtc_w,
		.y2 = state->crtc_y + state->crtc_h,
	};
	unsigned long flags;

	pr_info("%s:%s\n", __FILE__, __func__);

	spin_lock_irqsave(&priv->drm->event_lock, flags);

	/* Enable OSD and BLK0. */
	priv->viu.osd1_ctrl_stat = BIT(21)	| /* Enable OSD */
				   (0xFF << 12) | /* Alpha is 0xFF */
				   BIT(0);    	  /* Enable BLK0 */

	/* Set up BLK0 to point to the right canvas */
	priv->viu.osd1_blk0_cfg[0] = ((MESON_CANVAS_ID_OSD1 << 16) |
				      OSD_ENDIANNESS_LE | OSD_BLK_MODE_32 |
				      OSD_OUTPUT_COLOR_RGB |
				      OSD_COLOR_MATRIX_32_ARGB);

	if (state->crtc->mode.flags & DRM_MODE_FLAG_INTERLACE) {
		priv->viu.osd1_blk0_cfg[0] |= OSD_INTERLACE_ENABLED;
		priv->viu.osd1_interlace_sync = true;
	}

	/* The format of these registers is (x2 << 16 | x1), where x2 is exclusive.
	 * e.g. +30x1920 would be (1919 << 16) | 30. */
	priv->viu.osd1_blk0_cfg[1] = ((fixed16_to_int(src.x2) - 1) << 16) |
					fixed16_to_int(src.x1);
	priv->viu.osd1_blk0_cfg[2] = ((fixed16_to_int(src.y2) - 1) << 16) |
					fixed16_to_int(src.y1);
	priv->viu.osd1_blk0_cfg[3] = ((dest.x2 - 1) << 16) | dest.x1;
	priv->viu.osd1_blk0_cfg[4] = ((dest.y2 - 1) << 16) | dest.y1;

	spin_unlock_irqrestore(&priv->drm->event_lock, flags);
}

void meson_viu_commit_osd1(struct meson_drm *priv)
{
	if (priv->viu.osd1_enabled) {
		writel_relaxed(priv->viu.osd1_ctrl_stat,
				priv->io_base + _REG(VIU_OSD1_CTRL_STAT));
		writel_relaxed(priv->viu.osd1_blk0_cfg[0],
				priv->io_base + _REG(VIU_OSD1_BLK0_CFG_W0));
		writel_relaxed(priv->viu.osd1_blk0_cfg[1],
				priv->io_base + _REG(VIU_OSD1_BLK0_CFG_W1));
		writel_relaxed(priv->viu.osd1_blk0_cfg[2],
				priv->io_base + _REG(VIU_OSD1_BLK0_CFG_W2));
		writel_relaxed(priv->viu.osd1_blk0_cfg[3],
				priv->io_base + _REG(VIU_OSD1_BLK0_CFG_W3));
		writel_relaxed(priv->viu.osd1_blk0_cfg[4],
				priv->io_base + _REG(VIU_OSD1_BLK0_CFG_W4));

		meson_vpp_enable_osd1(priv);
	}
	else {
		meson_vpp_disable_osd1(priv);
	}
}

void meson_viu_sync_osd1(struct meson_drm *priv)
{
	/* Update the currect field */
	if (priv->viu.osd1_enabled &&
	    priv->viu.osd1_interlace_sync) {
		priv->viu.osd1_blk0_cfg[0] =
			(priv->viu.osd1_blk0_cfg[0] & ~BIT(0)) |
			!!meson_venci_get_field(priv);

		writel_relaxed(priv->viu.osd1_blk0_cfg[0],
				priv->io_base + _REG(VIU_OSD1_BLK0_CFG_W0));
	}
}

void meson_viu_init(struct meson_drm *priv)
{
	/* In its default configuration, the display controller can be starved
	 * of memory bandwidth when the CPU and GPU are busy, causing scanout
	 * to sometimes get behind where it should be (with parts of the
	 * display appearing momentarily shifted to the right).
	 * Increase the priority and burst size of RAM access using the same
	 * values as Amlogic's driver. */
	writel_bits_relaxed(BIT(0), BIT(0), /* Urgent DDR request priority */
		priv->io_base + VIU_OSD1_FIFO_CTRL_STAT);

	/* Increase burst length from 24 to 64 */
	writel_bits_relaxed(3 << 10, 3 << 10,
		priv->io_base + VIU_OSD1_FIFO_CTRL_STAT);

	/* Increase the number of lines that the display controller waits
	 * after vsync before starting RAM access. This gives the vsync
	 * interrupt handler more time to update the registers, avoiding
	 * visual glitches. */
	writel_bits_relaxed(0x1f << 5, 12 << 5,
		priv->io_base + VIU_OSD1_FIFO_CTRL_STAT);

	priv->viu.osd1_enabled = false;
	priv->viu.osd1_interlace_sync = false;
}
