// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <media/v4l2-mem2mem.h>

#include "iris_buffer.h"
#include "iris_instance.h"

#define PIXELS_4K 4096
#define MAX_WIDTH 4096
#define MAX_HEIGHT 2304
#define NUM_MBS_4K (DIV_ROUND_UP(MAX_WIDTH, 16) * DIV_ROUND_UP(MAX_HEIGHT, 16))

/*
 * NV12:
 * YUV 4:2:0 image with a plane of 8 bit Y samples followed
 * by an interleaved U/V plane containing 8 bit 2x2 subsampled
 * colour difference samples.
 *
 * <-------- Y/UV_Stride -------->
 * <------- Width ------->
 * Y Y Y Y Y Y Y Y Y Y Y Y . . . .  ^           ^
 * Y Y Y Y Y Y Y Y Y Y Y Y . . . .  |           |
 * Y Y Y Y Y Y Y Y Y Y Y Y . . . .  Height      |
 * Y Y Y Y Y Y Y Y Y Y Y Y . . . .  |          y_scanlines
 * Y Y Y Y Y Y Y Y Y Y Y Y . . . .  |           |
 * Y Y Y Y Y Y Y Y Y Y Y Y . . . .  |           |
 * Y Y Y Y Y Y Y Y Y Y Y Y . . . .  |           |
 * Y Y Y Y Y Y Y Y Y Y Y Y . . . .  V           |
 * . . . . . . . . . . . . . . . .              |
 * . . . . . . . . . . . . . . . .              |
 * . . . . . . . . . . . . . . . .              |
 * . . . . . . . . . . . . . . . .              V
 * U V U V U V U V U V U V . . . .  ^
 * U V U V U V U V U V U V . . . .  |
 * U V U V U V U V U V U V . . . .  |
 * U V U V U V U V U V U V . . . .  uv_scanlines
 * . . . . . . . . . . . . . . . .  |
 * . . . . . . . . . . . . . . . .  V
 * . . . . . . . . . . . . . . . .  --> Buffer size alignment
 *
 * y_stride : Width aligned to 128
 * uv_stride : Width aligned to 128
 * y_scanlines: Height aligned to 32
 * uv_scanlines: Height/2 aligned to 16
 * Total size = align((y_stride * y_scanlines
 *          + uv_stride * uv_scanlines , 4096)
 */
static u32 iris_output_buffer_size_nv12(struct iris_inst *inst)
{
	u32 y_plane, uv_plane, y_stride, uv_stride, y_scanlines, uv_scanlines;
	struct v4l2_format *f;

	f = inst->fmt_dst;
	y_stride = ALIGN(f->fmt.pix_mp.width, 128);
	uv_stride = ALIGN(f->fmt.pix_mp.width, 128);
	y_scanlines = ALIGN(f->fmt.pix_mp.height, 32);
	uv_scanlines = ALIGN((f->fmt.pix_mp.height + 1) >> 1, 16);
	y_plane = y_stride * y_scanlines;
	uv_plane = uv_stride * uv_scanlines;

	return ALIGN(y_plane + uv_plane, PIXELS_4K);
}

/*
 * QC08C:
 * Compressed Macro-tile format for NV12.
 * Contains 4 planes in the following order -
 * (A) Y_Meta_Plane
 * (B) Y_UBWC_Plane
 * (C) UV_Meta_Plane
 * (D) UV_UBWC_Plane
 *
 * Y_Meta_Plane consists of meta information to decode compressed
 * tile data in Y_UBWC_Plane.
 * Y_UBWC_Plane consists of Y data in compressed macro-tile format.
 * UBWC decoder block will use the Y_Meta_Plane data together with
 * Y_UBWC_Plane data to produce loss-less uncompressed 8 bit Y samples.
 *
 * UV_Meta_Plane consists of meta information to decode compressed
 * tile data in UV_UBWC_Plane.
 * UV_UBWC_Plane consists of UV data in compressed macro-tile format.
 * UBWC decoder block will use UV_Meta_Plane data together with
 * UV_UBWC_Plane data to produce loss-less uncompressed 8 bit 2x2
 * subsampled color difference samples.
 *
 * Each tile in Y_UBWC_Plane/UV_UBWC_Plane is independently decodable
 * and randomly accessible. There is no dependency between tiles.
 *
 * <----- y_meta_stride ---->
 * <-------- Width ------>
 * M M M M M M M M M M M M . .      ^           ^
 * M M M M M M M M M M M M . .      |           |
 * M M M M M M M M M M M M . .      Height      |
 * M M M M M M M M M M M M . .      |         y_meta_scanlines
 * M M M M M M M M M M M M . .      |           |
 * M M M M M M M M M M M M . .      |           |
 * M M M M M M M M M M M M . .      |           |
 * M M M M M M M M M M M M . .      V           |
 * . . . . . . . . . . . . . .                  |
 * . . . . . . . . . . . . . .                  |
 * . . . . . . . . . . . . . .      -------> Buffer size aligned to 4k
 * . . . . . . . . . . . . . .                  V
 * <--Compressed tile y_stride--->
 * <------- Width ------->
 * Y* Y* Y* Y* Y* Y* Y* Y* . . . .  ^           ^
 * Y* Y* Y* Y* Y* Y* Y* Y* . . . .  |           |
 * Y* Y* Y* Y* Y* Y* Y* Y* . . . .  Height      |
 * Y* Y* Y* Y* Y* Y* Y* Y* . . . .  |        Macro_tile y_scanlines
 * Y* Y* Y* Y* Y* Y* Y* Y* . . . .  |           |
 * Y* Y* Y* Y* Y* Y* Y* Y* . . . .  |           |
 * Y* Y* Y* Y* Y* Y* Y* Y* . . . .  |           |
 * Y* Y* Y* Y* Y* Y* Y* Y* . . . .  V           |
 * . . . . . . . . . . . . . . . .              |
 * . . . . . . . . . . . . . . . .              |
 * . . . . . . . . . . . . . . . .  -------> Buffer size aligned to 4k
 * . . . . . . . . . . . . . . . .              V
 * <----- uv_meta_stride ---->
 * M M M M M M M M M M M M . .      ^
 * M M M M M M M M M M M M . .      |
 * M M M M M M M M M M M M . .      |
 * M M M M M M M M M M M M . .      uv_meta_scanlines
 * . . . . . . . . . . . . . .      |
 * . . . . . . . . . . . . . .      V
 * . . . . . . . . . . . . . .      -------> Buffer size aligned to 4k
 * <--Compressed tile uv_stride--->
 * U* V* U* V* U* V* U* V* . . . .  ^
 * U* V* U* V* U* V* U* V* . . . .  |
 * U* V* U* V* U* V* U* V* . . . .  |
 * U* V* U* V* U* V* U* V* . . . .  uv_scanlines
 * . . . . . . . . . . . . . . . .  |
 * . . . . . . . . . . . . . . . .  V
 * . . . . . . . . . . . . . . . .  -------> Buffer size aligned to 4k
 *
 * y_stride = align(Width, 128)
 * uv_stride = align(Width, 128)
 * y_scanlines = align(Height, 32)
 * uv_scanlines = align(Height/2, 16)
 * y_plane = align(y_stride * y_scanlines, 4096)
 * uv_plane = align(uv_stride * uv_scanlines, 4096)
 * y_meta_stride = align(roundup(Width, Y_TileWidth), 64)
 * y_meta_scanlines = align(roundup(Height, Y_TileHeight), 16)
 * y_meta_plane = align(y_meta_stride * y_meta_scanlines, 4096)
 * uv_meta_stride = align(roundup(Width, UV_TileWidth), 64)
 * uv_meta_scanlines = align(roundup(Height, UV_TileHeight), 16)
 * uv_meta_plane = align(uv_meta_stride * uv_meta_scanlines, 4096)
 *
 * Total size = align( y_plane + uv_plane +
 *           y_meta_plane + uv_meta_plane, 4096)
 */
static u32 iris_output_buffer_size_qc08c(struct iris_inst *inst)
{
	u32 y_plane, uv_plane, y_stride, uv_stride;
	u32 uv_meta_stride, uv_meta_plane;
	u32 y_meta_stride, y_meta_plane;
	struct v4l2_format *f;

	f = inst->fmt_dst;
	y_meta_stride = ALIGN(DIV_ROUND_UP(f->fmt.pix_mp.width, 32), 64);
	y_meta_plane = y_meta_stride * ALIGN(DIV_ROUND_UP(f->fmt.pix_mp.height, 8), 16);
	y_meta_plane = ALIGN(y_meta_plane, PIXELS_4K);

	y_stride = ALIGN(f->fmt.pix_mp.width, 128);
	y_plane = ALIGN(y_stride * ALIGN(f->fmt.pix_mp.height, 32), PIXELS_4K);

	uv_meta_stride = ALIGN(DIV_ROUND_UP(f->fmt.pix_mp.width / 2, 16), 64);
	uv_meta_plane = uv_meta_stride * ALIGN(DIV_ROUND_UP(f->fmt.pix_mp.height / 2, 8), 16);
	uv_meta_plane = ALIGN(uv_meta_plane, PIXELS_4K);

	uv_stride = ALIGN(f->fmt.pix_mp.width, 128);
	uv_plane = ALIGN(uv_stride * ALIGN(f->fmt.pix_mp.height / 2, 32), PIXELS_4K);

	return ALIGN(y_meta_plane + y_plane + uv_meta_plane + uv_plane, PIXELS_4K);
}

static u32 iris_input_buffer_size(struct iris_inst *inst)
{
	u32 base_res_mbs = NUM_MBS_4K;
	u32 frame_size, num_mbs;
	u32 div_factor;

	num_mbs = iris_get_mbpf(inst);
	if (num_mbs > NUM_MBS_4K) {
		div_factor = 4;
		base_res_mbs = inst->driver_cap[MBPF].value;
	} else {
		base_res_mbs = NUM_MBS_4K;
		div_factor = 2;
	}

	/*
	 * frame_size = YUVsize / div_factor
	 * where YUVsize = resolution_in_MBs * MBs_in_pixel * 3 / 2
	 */

	frame_size = base_res_mbs * (16 * 16) * 3 / 2 / div_factor;

	return ALIGN(frame_size, PIXELS_4K);
}

int iris_get_buffer_size(struct iris_inst *inst,
			 enum iris_buffer_type buffer_type)
{
	switch (buffer_type) {
	case BUF_INPUT:
		return iris_input_buffer_size(inst);
	case BUF_OUTPUT:
		return iris_output_buffer_size_nv12(inst);
	case BUF_DPB:
		return iris_output_buffer_size_qc08c(inst);
	default:
		return 0;
	}
}

void iris_vb2_queue_error(struct iris_inst *inst)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct vb2_queue *q;

	q = v4l2_m2m_get_src_vq(m2m_ctx);
	vb2_queue_error(q);
	q = v4l2_m2m_get_dst_vq(m2m_ctx);
	vb2_queue_error(q);
}
