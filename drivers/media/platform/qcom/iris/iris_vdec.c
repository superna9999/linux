// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>

#include "iris_buffer.h"
#include "iris_ctrls.h"
#include "iris_instance.h"
#include "iris_power.h"
#include "iris_vdec.h"
#include "iris_vpu_buffer.h"

#define DEFAULT_WIDTH 320
#define DEFAULT_HEIGHT 240
#define DEFAULT_CODEC_ALIGNMENT 16
#define MAX_EVENTS 30

int iris_vdec_inst_init(struct iris_inst *inst)
{
	struct platform_inst_driver_cap *inst_plat_cap_data;
	struct iris_core *core = inst->core;
	struct v4l2_format *f;
	int i, num_inst_cap;
	u32 cap_id;

	inst->fmt_src  = kzalloc(sizeof(*inst->fmt_src), GFP_KERNEL);
	inst->fmt_dst  = kzalloc(sizeof(*inst->fmt_dst), GFP_KERNEL);

	inst->fw_min_count = MIN_BUFFERS;

	f = inst->fmt_src;
	f->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	f->fmt.pix_mp.width = DEFAULT_WIDTH;
	f->fmt.pix_mp.height = DEFAULT_HEIGHT;
	f->fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
	f->fmt.pix_mp.num_planes = 1;
	f->fmt.pix_mp.plane_fmt[0].bytesperline = 0;
	f->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_INPUT);
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	inst->buffers[BUF_INPUT].min_count = iris_vpu_buf_count(inst, BUF_INPUT);
	inst->buffers[BUF_INPUT].actual_count = inst->buffers[BUF_INPUT].min_count;
	inst->buffers[BUF_INPUT].size = f->fmt.pix_mp.plane_fmt[0].sizeimage;

	f = inst->fmt_dst;
	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	f->fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
	f->fmt.pix_mp.width = ALIGN(DEFAULT_WIDTH, 128);
	f->fmt.pix_mp.height = ALIGN(DEFAULT_HEIGHT, 32);
	f->fmt.pix_mp.num_planes = 1;
	f->fmt.pix_mp.plane_fmt[0].bytesperline = ALIGN(DEFAULT_WIDTH, 128);
	f->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_OUTPUT);
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
	f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
	f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
	inst->buffers[BUF_OUTPUT].min_count = iris_vpu_buf_count(inst, BUF_OUTPUT);
	inst->buffers[BUF_OUTPUT].actual_count = inst->buffers[BUF_OUTPUT].min_count;
	inst->buffers[BUF_OUTPUT].size = f->fmt.pix_mp.plane_fmt[0].sizeimage;

	memcpy(&inst->fw_cap[0], &core->inst_fw_cap[0],
	       INST_FW_CAP_MAX * sizeof(struct platform_inst_fw_cap));

	inst_plat_cap_data = core->iris_platform_data->inst_driver_cap_data;
	num_inst_cap = core->iris_platform_data->inst_driver_cap_data_size;

	for (i = 0; i < num_inst_cap && i < INST_DRIVER_CAP_MAX - 1; i++) {
		cap_id = inst_plat_cap_data[i].cap_id;

		inst->driver_cap[cap_id].cap_id = inst_plat_cap_data[i].cap_id;
		inst->driver_cap[cap_id].min = inst_plat_cap_data[i].min;
		inst->driver_cap[cap_id].max = inst_plat_cap_data[i].max;
		inst->driver_cap[cap_id].value = inst_plat_cap_data[i].value;
	}

	return iris_ctrls_init(inst);
}

void iris_vdec_inst_deinit(struct iris_inst *inst)
{
	kfree(inst->fmt_dst);
	kfree(inst->fmt_src);
}

int iris_vdec_enum_fmt(struct iris_inst *inst, struct v4l2_fmtdesc *f)
{
	if (f->index)
		return -EINVAL;

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		f->pixelformat = V4L2_PIX_FMT_H264;
		f->flags = V4L2_FMT_FLAG_COMPRESSED | V4L2_FMT_FLAG_DYN_RESOLUTION;
		strscpy(f->description, "codec", sizeof(f->description));
	} else if (V4L2_TYPE_IS_CAPTURE(f->type)) {
		f->pixelformat = V4L2_PIX_FMT_NV12;
		strscpy(f->description, "colorformat", sizeof(f->description));
	}

	if (!f->pixelformat)
		return -EINVAL;

	memset(f->reserved, 0, sizeof(f->reserved));

	return 0;
}

int iris_vdec_try_fmt(struct iris_inst *inst, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct v4l2_format *f_inst;
	struct vb2_queue *src_q;

	src_q = v4l2_m2m_get_src_vq(m2m_ctx);
	memset(pixmp->reserved, 0, sizeof(pixmp->reserved));
	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		if (f->fmt.pix_mp.pixelformat != V4L2_PIX_FMT_H264) {
			f_inst = inst->fmt_src;
			f->fmt.pix_mp.width = f_inst->fmt.pix_mp.width;
			f->fmt.pix_mp.height = f_inst->fmt.pix_mp.height;
			f->fmt.pix_mp.pixelformat = f_inst->fmt.pix_mp.pixelformat;
		}
	} else if (V4L2_TYPE_IS_CAPTURE(f->type)) {
		if (f->fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12) {
			f_inst = inst->fmt_dst;
			f->fmt.pix_mp.pixelformat = f_inst->fmt.pix_mp.pixelformat;
			f->fmt.pix_mp.width = f_inst->fmt.pix_mp.width;
			f->fmt.pix_mp.height = f_inst->fmt.pix_mp.height;
		}
		if (vb2_is_streaming(src_q)) {
			f_inst = inst->fmt_src;
			f->fmt.pix_mp.height = f_inst->fmt.pix_mp.height;
			f->fmt.pix_mp.width = f_inst->fmt.pix_mp.width;
		}
	} else {
		return -EINVAL;
	}

	if (pixmp->field == V4L2_FIELD_ANY)
		pixmp->field = V4L2_FIELD_NONE;

	pixmp->num_planes = 1;

	return 0;
}

int iris_vdec_s_fmt(struct iris_inst *inst, struct v4l2_format *f)
{
	struct v4l2_format *fmt, *output_fmt;
	struct vb2_queue *q;
	u32 codec_align;

	int ret = 0;

	q = v4l2_m2m_get_vq(inst->m2m_ctx, f->type);
	if (!q)
		return -EINVAL;

	iris_vdec_try_fmt(inst, f);

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		if (f->fmt.pix_mp.pixelformat != V4L2_PIX_FMT_H264)
			return -EINVAL;

		fmt = inst->fmt_src;
		fmt->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

		codec_align = DEFAULT_CODEC_ALIGNMENT;
		fmt->fmt.pix_mp.width = ALIGN(f->fmt.pix_mp.width, codec_align);
		fmt->fmt.pix_mp.height = ALIGN(f->fmt.pix_mp.height, codec_align);
		fmt->fmt.pix_mp.num_planes = 1;
		fmt->fmt.pix_mp.plane_fmt[0].bytesperline = 0;
		fmt->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_INPUT);
		inst->buffers[BUF_INPUT].min_count = iris_vpu_buf_count(inst, BUF_INPUT);
		if (inst->buffers[BUF_INPUT].actual_count < inst->buffers[BUF_INPUT].min_count)
			inst->buffers[BUF_INPUT].actual_count = inst->buffers[BUF_INPUT].min_count;

		inst->buffers[BUF_INPUT].size = fmt->fmt.pix_mp.plane_fmt[0].sizeimage;

		fmt->fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
		fmt->fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
		fmt->fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
		fmt->fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;

		output_fmt = inst->fmt_dst;
		output_fmt->fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
		output_fmt->fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
		output_fmt->fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
		output_fmt->fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;

		inst->crop.left = 0;
		inst->crop.top = 0;
		inst->crop.width = f->fmt.pix_mp.width;
		inst->crop.height = f->fmt.pix_mp.height;
	} else if (V4L2_TYPE_IS_CAPTURE(f->type)) {
		fmt = inst->fmt_dst;
		fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		if (q->streaming) {
			f->fmt.pix_mp.height = inst->fmt_src->fmt.pix_mp.height;
			f->fmt.pix_mp.width = inst->fmt_src->fmt.pix_mp.width;
		}
		if (fmt->fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12)
			return -EINVAL;
		fmt->fmt.pix_mp.pixelformat = f->fmt.pix_mp.pixelformat;
		fmt->fmt.pix_mp.width = ALIGN(f->fmt.pix_mp.width, 128);
		fmt->fmt.pix_mp.height = ALIGN(f->fmt.pix_mp.height, 32);
		fmt->fmt.pix_mp.num_planes = 1;
		fmt->fmt.pix_mp.plane_fmt[0].bytesperline = ALIGN(f->fmt.pix_mp.width, 128);
		fmt->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_OUTPUT);

		if (!q->streaming)
			inst->buffers[BUF_OUTPUT].min_count = iris_vpu_buf_count(inst, BUF_INPUT);
		if (inst->buffers[BUF_OUTPUT].actual_count < inst->buffers[BUF_OUTPUT].min_count)
			inst->buffers[BUF_OUTPUT].actual_count =
				inst->buffers[BUF_OUTPUT].min_count;

		inst->buffers[BUF_OUTPUT].size = fmt->fmt.pix_mp.plane_fmt[0].sizeimage;

		if (!q->streaming) {
			inst->crop.top = 0;
			inst->crop.left = 0;
			inst->crop.width = f->fmt.pix_mp.width;
			inst->crop.height = f->fmt.pix_mp.height;
		}
	} else {
		return -EINVAL;
	}
	memcpy(f, fmt, sizeof(*fmt));

	return ret;
}

int iris_vdec_subscribe_event(struct iris_inst *inst, const struct v4l2_event_subscription *sub)
{
	int ret = 0;

	switch (sub->type) {
	case V4L2_EVENT_EOS:
		ret = v4l2_event_subscribe(&inst->fh, sub, MAX_EVENTS, NULL);
		inst->subscriptions |= V4L2_EVENT_EOS;
		break;
	case V4L2_EVENT_SOURCE_CHANGE:
		ret = v4l2_src_change_event_subscribe(&inst->fh, sub);
		inst->subscriptions |= V4L2_EVENT_SOURCE_CHANGE;
		break;
	case V4L2_EVENT_CTRL:
		ret = v4l2_ctrl_subscribe_event(&inst->fh, sub);
		inst->subscriptions |= V4L2_EVENT_CTRL;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

void iris_vdec_src_change(struct iris_inst *inst)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct v4l2_event event = {0};
	struct vb2_queue *src_q;

	src_q = v4l2_m2m_get_src_vq(m2m_ctx);
	if (!vb2_is_streaming(src_q))
		return;

	event.type = V4L2_EVENT_SOURCE_CHANGE;
	event.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION;
	v4l2_event_queue_fh(&inst->fh, &event);
}

static int iris_vdec_get_num_queued_buffers(struct iris_inst *inst,
					    enum iris_buffer_type type)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct v4l2_m2m_buffer *buffer, *n;
	struct iris_buffer *buf = NULL;
	int count = 0;

	if (type == BUF_INPUT) {
		v4l2_m2m_for_each_src_buf_safe(m2m_ctx, buffer, n) {
			buf = to_iris_buffer(&buffer->vb);
			if (!(buf->attr & BUF_ATTR_QUEUED))
				continue;
			count++;
		}
	} else if (type == BUF_OUTPUT) {
		v4l2_m2m_for_each_dst_buf_safe(m2m_ctx, buffer, n) {
			buf = to_iris_buffer(&buffer->vb);
			if (!(buf->attr & BUF_ATTR_QUEUED))
				continue;
			count++;
		}
	} else {
		return count;
	}

	return count;
}

static void iris_vdec_flush_deferred_buffers(struct iris_inst *inst,
					     enum iris_buffer_type type)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct v4l2_m2m_buffer *buffer, *n;
	struct iris_buffer *buf = NULL;

	if (type == BUF_INPUT) {
		v4l2_m2m_for_each_src_buf_safe(m2m_ctx, buffer, n) {
			buf = to_iris_buffer(&buffer->vb);
			if (buf->attr & BUF_ATTR_DEFERRED) {
				if (!(buf->attr & BUF_ATTR_BUFFER_DONE)) {
					buf->attr |= BUF_ATTR_BUFFER_DONE;
					buf->data_size = 0;
					iris_vb2_buffer_done(inst, buf);
				}
			}
		}
	} else {
		v4l2_m2m_for_each_dst_buf_safe(m2m_ctx, buffer, n) {
			buf = to_iris_buffer(&buffer->vb);
			if (buf->attr & BUF_ATTR_DEFERRED) {
				if (!(buf->attr & BUF_ATTR_BUFFER_DONE)) {
					buf->attr |= BUF_ATTR_BUFFER_DONE;
					buf->data_size = 0;
					iris_vb2_buffer_done(inst, buf);
				}
			}
		}
	}
}

static void iris_vdec_kill_session(struct iris_inst *inst)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;

	if (!inst->session_id)
		return;

	hfi_ops->session_close(inst);
	iris_inst_change_state(inst, IRIS_INST_ERROR);
}

int iris_vdec_session_streamoff(struct iris_inst *inst, u32 plane)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
	enum iris_buffer_type buffer_type;
	int count = 0;
	int ret;

	if (V4L2_TYPE_IS_OUTPUT(plane))
		buffer_type = BUF_INPUT;
	else if (V4L2_TYPE_IS_CAPTURE(plane))
		buffer_type = BUF_OUTPUT;
	else
		return -EINVAL;

	ret = hfi_ops->session_stop(inst, plane);
	if (ret)
		goto error;

	count = iris_vdec_get_num_queued_buffers(inst, buffer_type);
	if (count) {
		ret = -EINVAL;
		goto error;
	}

	ret = iris_inst_state_change_streamoff(inst, plane);
	if (ret)
		goto error;

	iris_vdec_flush_deferred_buffers(inst, buffer_type);

	return 0;

error:
	iris_vdec_kill_session(inst);
	iris_vdec_flush_deferred_buffers(inst, buffer_type);

	return ret;
}

static int iris_vdec_process_streamon_input(struct iris_inst *inst)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
	enum iris_inst_sub_state set_sub_state = IRIS_INST_SUB_NONE;
	int ret;

	iris_scale_power(inst);

	ret = hfi_ops->session_start(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (ret)
		return ret;

	if (inst->sub_state & IRIS_INST_SUB_INPUT_PAUSE) {
		ret = iris_inst_change_sub_state(inst, IRIS_INST_SUB_INPUT_PAUSE, 0);
		if (ret)
			return ret;
	}

	if (inst->sub_state & IRIS_INST_SUB_DRC ||
	    inst->sub_state & IRIS_INST_SUB_DRAIN ||
	    inst->sub_state & IRIS_INST_SUB_FIRST_IPSC) {
		if (!(inst->sub_state & IRIS_INST_SUB_INPUT_PAUSE)) {
			if (hfi_ops->session_pause) {
				ret = hfi_ops->session_pause(inst,
							     V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
				if (ret)
					return ret;
			}
			set_sub_state = IRIS_INST_SUB_INPUT_PAUSE;
		}
	}

	ret = iris_inst_state_change_streamon(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (ret)
		return ret;

	ret = iris_inst_change_sub_state(inst, 0, set_sub_state);

	return ret;
}

int iris_vdec_streamon_input(struct iris_inst *inst)
{
	int ret;

	ret = iris_set_properties(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (ret)
		return ret;

	ret = iris_alloc_and_queue_persist_bufs(inst);
	if (ret)
		return ret;

	iris_get_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

	ret = iris_destroy_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (ret)
		return ret;

	ret = iris_create_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (ret)
		return ret;

	ret = iris_queue_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (ret)
		return ret;

	ret = iris_vdec_process_streamon_input(inst);
	if (ret)
		return ret;

	return ret;
}

static int iris_vdec_process_streamon_output(struct iris_inst *inst)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
	enum iris_inst_sub_state clear_sub_state = IRIS_INST_SUB_NONE;
	bool drain_active = false, drc_active = false;
	int ret;

	iris_scale_power(inst);

	drain_active = inst->sub_state & IRIS_INST_SUB_DRAIN &&
		inst->sub_state & IRIS_INST_SUB_DRAIN_LAST;

	drc_active = inst->sub_state & IRIS_INST_SUB_DRC &&
		inst->sub_state & IRIS_INST_SUB_DRC_LAST;

	if (drc_active)
		clear_sub_state = IRIS_INST_SUB_DRC | IRIS_INST_SUB_DRC_LAST;
	else if (drain_active)
		clear_sub_state = IRIS_INST_SUB_DRAIN | IRIS_INST_SUB_DRAIN_LAST;

	if (inst->sub_state & IRIS_INST_SUB_INPUT_PAUSE) {
		ret = iris_alloc_and_queue_input_int_bufs(inst);
		if (ret)
			return ret;
		ret = iris_set_stage(inst, STAGE);
		if (ret)
			return ret;
		ret = iris_set_pipe(inst, PIPE);
		if (ret)
			return ret;
	}

	if (inst->state == IRIS_INST_INPUT_STREAMING &&
	    inst->sub_state & IRIS_INST_SUB_INPUT_PAUSE) {
		if (!drain_active) {
			ret = hfi_ops->session_resume_drc(inst,
							  V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
			if (ret)
				return ret;
		} else {
			if (hfi_ops->session_resume_drain) {
				ret =
				hfi_ops->session_resume_drain(inst,
							      V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
				if (ret)
					return ret;
			}
		}
		clear_sub_state |= IRIS_INST_SUB_INPUT_PAUSE;
	}

	if (inst->sub_state & IRIS_INST_SUB_FIRST_IPSC)
		clear_sub_state |= IRIS_INST_SUB_FIRST_IPSC;

	ret = hfi_ops->session_start(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (ret)
		return ret;

	if (inst->sub_state & IRIS_INST_SUB_OUTPUT_PAUSE)
		clear_sub_state |= IRIS_INST_SUB_OUTPUT_PAUSE;

	ret = iris_inst_state_change_streamon(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (ret)
		return ret;

	ret = iris_inst_change_sub_state(inst, clear_sub_state, 0);

	return ret;
}

int iris_vdec_streamon_output(struct iris_inst *inst)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
	int ret;

	ret = hfi_ops->session_set_config_params(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (ret)
		return ret;

	iris_get_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	ret = iris_destroy_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (ret)
		goto error;

	ret = iris_create_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (ret)
		goto error;

	ret = iris_vdec_process_streamon_output(inst);
	if (ret)
		goto error;

	ret = iris_queue_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (ret)
		return ret;

	return ret;

error:
	iris_vdec_session_streamoff(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	return ret;
}

static int
iris_vdec_vb2_buffer_to_driver(struct vb2_buffer *vb2, struct iris_buffer *buf)
{
	struct vb2_v4l2_buffer *vbuf;
	u32 buf_type;

	if (!vb2 || !buf)
		return -EINVAL;

	vbuf = to_vb2_v4l2_buffer(vb2);

	buf->fd = vb2->planes[0].m.fd;
	buf->data_offset = vb2->planes[0].data_offset;
	buf->data_size = vb2->planes[0].bytesused - vb2->planes[0].data_offset;
	buf->buffer_size = vb2->planes[0].length;
	buf->timestamp = vb2->timestamp;
	buf->flags = vbuf->flags;
	buf->attr = 0;

	buf_type = iris_v4l2_type_to_driver(vb2->type);
	if (buf_type == -EINVAL)
		return -EINVAL;

	buf->type = iris_v4l2_type_to_driver(vb2->type);
	buf->index = vb2->index;

	return 0;
}

int iris_vdec_qbuf(struct iris_inst *inst, struct vb2_v4l2_buffer *vbuf)
{
	struct vb2_buffer *vb2 = &vbuf->vb2_buf;
	struct iris_buffer *buf = NULL;
	int ret = 0;

	buf = to_iris_buffer(vbuf);

	ret = iris_vdec_vb2_buffer_to_driver(vb2, buf);
	if (ret)
		return ret;

	iris_scale_power(inst);

	return iris_queue_buffer(inst, buf);
}

int iris_vdec_start_cmd(struct iris_inst *inst)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
	enum iris_inst_sub_state clear_sub_state = IRIS_INST_SUB_NONE;
	struct vb2_queue *dst_vq;
	int ret;

	dst_vq = v4l2_m2m_get_vq(inst->m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	if (inst->sub_state & IRIS_INST_SUB_DRC &&
	    inst->sub_state & IRIS_INST_SUB_DRC_LAST) {
		vb2_clear_last_buffer_dequeued(dst_vq);
		clear_sub_state = IRIS_INST_SUB_DRC | IRIS_INST_SUB_DRC_LAST;

		if (inst->sub_state & IRIS_INST_SUB_INPUT_PAUSE) {
			ret = hfi_ops->session_resume_drc(inst,
							  V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
			if (ret)
				return ret;
			clear_sub_state |= IRIS_INST_SUB_INPUT_PAUSE;
		}
		if (inst->sub_state & IRIS_INST_SUB_OUTPUT_PAUSE) {
			ret = hfi_ops->session_resume_drc(inst,
							  V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
			if (ret)
				return ret;
			clear_sub_state |= IRIS_INST_SUB_OUTPUT_PAUSE;
		}
	} else if (inst->sub_state & IRIS_INST_SUB_DRAIN &&
			   inst->sub_state & IRIS_INST_SUB_DRAIN_LAST) {
		vb2_clear_last_buffer_dequeued(dst_vq);
		clear_sub_state = IRIS_INST_SUB_DRAIN | IRIS_INST_SUB_DRAIN_LAST;
		if (inst->sub_state & IRIS_INST_SUB_INPUT_PAUSE) {
			if (hfi_ops->session_resume_drain) {
				ret =
				hfi_ops->session_resume_drain(inst,
							      V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
				if (ret)
					return ret;
			}

			clear_sub_state |= IRIS_INST_SUB_INPUT_PAUSE;
		}
		if (inst->sub_state & IRIS_INST_SUB_OUTPUT_PAUSE) {
			if (hfi_ops->session_resume_drain) {
				ret =
				hfi_ops->session_resume_drain(inst,
							      V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
				if (ret)
					return ret;
			}

			clear_sub_state |= IRIS_INST_SUB_OUTPUT_PAUSE;
		}
	} else {
		dev_err(inst->core->dev, "start called before receiving last_flag\n");
		iris_inst_change_state(inst, IRIS_INST_ERROR);
		return -EBUSY;
	}

	ret = iris_inst_change_sub_state(inst, clear_sub_state, 0);

	return ret;
}

int iris_vdec_stop_cmd(struct iris_inst *inst)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
	int ret;

	ret = hfi_ops->session_drain(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (ret)
		return ret;

	iris_inst_change_sub_state(inst, 0, IRIS_INST_SUB_DRAIN);

	return ret;
}
