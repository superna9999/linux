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

/* 
 * Video Encoders
 * - ENCI : Interlace Video Encoder
 * - ENCP : Progressive Video Encoder
 */

#ifndef __MESON_VENC_H
#define __MESON_VENC_H

struct meson_cvbs_enci_mode {
	unsigned hso_begin; /* HSO begin position */
	unsigned hso_end; /* HSO end position */
	unsigned vso_even; /* VSO even line */
	unsigned vso_odd; /* VSO odd line */
	unsigned macv_max_amp; /* Macrovision max amplitude */
	unsigned video_prog_mode;
	unsigned video_mode;
	unsigned sch_adjust;
	unsigned pixel_start;
	unsigned pixel_end;
	unsigned top_field_line_start;
	unsigned top_field_line_end;
	unsigned bottom_field_line_start;
	unsigned bottom_field_line_end;
	unsigned video_saturation;
	unsigned video_contrast;
	unsigned video_brightness;
	unsigned video_hue;
	unsigned analog_sync_adj;
};

/* CVBS Timings and Parameters */
extern struct meson_cvbs_enci_mode meson_cvbs_enci_pal;
extern struct meson_cvbs_enci_mode meson_cvbs_enci_ntsc;

void meson_venci_cvbs_mode_set(struct meson_drm *priv,
			       struct meson_cvbs_enci_mode *mode);
void meson_venci_cvbs_enable(struct meson_drm *priv);
void meson_venci_cvbs_disable(struct meson_drm *priv);

unsigned meson_venci_get_field(struct meson_drm *priv);

void meson_venc_init(struct meson_drm *priv);

#endif /* __MESON_VENC_H */
