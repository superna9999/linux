/*
 * drm kms/fb cma (contiguous memory allocator) helper functions
 *
 * Copyright (C) 2012 Analog Device Inc.
 *   Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Based on udl_fbdev.c
 *  Copyright (C) 2012 Red Hat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_print.h>
#include <linux/module.h>

/**
 * DOC: framebuffer cma helper functions
 *
 * Provides helper functions for creating a cma (contiguous memory allocator)
 * backed framebuffer.
 *
 * drm_gem_fb_create() is used in the &drm_mode_config_funcs.fb_create
 * callback function to create a cma backed framebuffer.
 */

/**
 * drm_fb_cma_get_gem_obj() - Get CMA GEM object for framebuffer
 * @fb: The framebuffer
 * @plane: Which plane
 *
 * Return the CMA GEM object for given framebuffer.
 *
 * This function will usually be called from the CRTC callback functions.
 */
struct drm_gem_cma_object *drm_fb_cma_get_gem_obj(struct drm_framebuffer *fb,
						  unsigned int plane)
{
	struct drm_gem_object *gem;

	gem = drm_gem_fb_get_obj(fb, plane);
	if (!gem)
		return NULL;

	return to_drm_gem_cma_obj(gem);
}
EXPORT_SYMBOL_GPL(drm_fb_cma_get_gem_obj);

/**
 * drm_fb_cma_get_gem_addr() - Get physical address for framebuffer
 * @fb: The framebuffer
 * @state: Which state of drm plane
 * @plane: Which plane
 * Return the CMA GEM address for given framebuffer.
 *
 * This function will usually be called from the PLANE callback functions.
 */
dma_addr_t drm_fb_cma_get_gem_addr(struct drm_framebuffer *fb,
				   struct drm_plane_state *state,
				   unsigned int plane)
{
	struct drm_gem_cma_object *obj;
	dma_addr_t paddr;
	u8 h_div = 1, v_div = 1;

	obj = drm_fb_cma_get_gem_obj(fb, plane);
	if (!obj)
		return 0;

	paddr = obj->paddr + fb->offsets[plane];

	if (plane > 0) {
		h_div = fb->format->hsub;
		v_div = fb->format->vsub;
	}

	paddr += (fb->format->cpp[plane] * (state->src_x >> 16)) / h_div;
	paddr += (fb->pitches[plane] * (state->src_y >> 16)) / v_div;

	return paddr;
}
EXPORT_SYMBOL_GPL(drm_fb_cma_get_gem_addr);
