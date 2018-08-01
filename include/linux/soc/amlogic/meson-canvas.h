/*
 * Copyright (c) 2018 Maxime Jourdan
 * Author: Maxime Jourdan <maxi.jourdan@wanadoo.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef MESON_CANVAS_H
#define MESON_CANVAS_H

#include <linux/kernel.h>

#define MESON_CANVAS_WRAP_NONE	0x00
#define MESON_CANVAS_WRAP_X	0x01
#define MESON_CANVAS_WRAP_Y	0x02

#define MESON_CANVAS_BLKMODE_LINEAR	0x00
#define MESON_CANVAS_BLKMODE_32x32	0x01
#define MESON_CANVAS_BLKMODE_64x64	0x02

struct meson_canvas_platform_data {
	int (*alloc)(uint8_t *canvas_index);
	int (*free) (uint8_t canvas_index);
	int (*setup)(uint8_t canvas_index, uint32_t addr,
			uint32_t stride, uint32_t height,
			unsigned int wrap,
			unsigned int blkmode,
			unsigned int endian);
};

#endif
