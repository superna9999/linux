/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2014 Endless Mobile
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
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
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#ifndef _UAPI_MESON_DRM_H_
#define _UAPI_MESON_DRM_H_

#include <drm/drm.h>

struct drm_meson_gem_create {
	__u64 size;
	__u32 flags;
	__u32 handle;
};

#define DRM_MESON_GEM_CREATE		0x00

#define DRM_IOCTL_MESON_GEM_CREATE	DRM_IOWR(DRM_COMMAND_BASE + DRM_MESON_GEM_CREATE, \
						 struct drm_meson_gem_create)

#endif
