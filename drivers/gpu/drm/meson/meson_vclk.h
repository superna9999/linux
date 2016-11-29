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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* Video Clock */

#ifndef __MESON_VCLK_H
#define __MESON_VCLK_H

enum {
	MESON_VCLK_TARGET_CVBS = 0,
	MESON_VCLK_TARGET_HDMI = 0,
};

/* Frequency Profiles */
#define MESON_VCLK_FREQ_MASK	0xFFFFFF

/* 27MHz is the CVBS Pixel Clock */
#define MESON_VCLK_CVBS			0

/* PLL	O1 O2 O3 VP DV     EN TX */
/* 4320 /4 /4 /1 /5 /1  => /2 /2 */
#define MESON_VCLK_HDMI_ENCI_27000	1
/* 4320 /4 /4 /1 /5 /1  => /1 /2 */
#define MESON_VCLK_HDMI_DDR_27000	2
/* 2970 /4 /1 /1 /5 /1  => /1 /2 */
#define MESON_VCLK_HDMI_DDR_74250	3
/* 2970 /2 /2 /2 /5 /1  => /1 /1 */
#define MESON_VCLK_HDMI_74250		4
/* 2970 /1 /2 /2 /5 /1  => /1 /1 */
#define MESON_VCLK_HDMI_148500		5
/* 2970 /1 /1 /1 /5 /2  => /1 /1 */
#define MESON_VCLK_HDMI_297000		6
/* 5940 /1 /1 /2 /5 /1  => /1 /1 */
#define MESON_VCLK_HDMI_594000		7

void meson_vclk_setup(struct meson_drm *priv, unsigned int target,
		      unsigned int freq);

#endif /* __MESON_VCLK_H */
