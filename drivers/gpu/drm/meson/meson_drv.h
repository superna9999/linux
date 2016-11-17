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

#ifndef __MESON_DRV_H
#define __MESON_DRV_H

#include <linux/regmap.h>

struct meson_drm {
	struct platform_device *pdev;
	void __iomem *io_base;
	struct regmap *hhi;
	struct regmap *dmc;
	int vsync_irq;

	struct drm_device *drm;
	struct drm_crtc *crtc;
	struct drm_fbdev_cma *fbdev;
	struct drm_plane *primary_plane;

	/* Components Data */
	struct {
		bool osd1_enabled;
		bool osd1_interlace;
		bool osd1_commit;
		uint32_t osd1_ctrl_stat;
		uint32_t osd1_blk0_cfg[5];
	} viu;

	struct {

	} vpp;

	struct {

	} vpu;

	struct {
		unsigned current_mode;
		unsigned cvbs_enabled;
	} venc;
};

#endif /* __MESON_DRV_H */
