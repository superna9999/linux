// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_buffer.h"
#include "iris_instance.h"
#include "iris_vb2.h"
#include "iris_vdec.h"
#include "iris_vpu_buffer.h"

int iris_vb2_queue_setup(struct vb2_queue *q,
			 unsigned int *num_buffers, unsigned int *num_planes,
			 unsigned int sizes[], struct device *alloc_devs[])
{
	enum iris_buffer_type buffer_type = 0;
	struct iris_buffers *buffers;
	struct iris_inst *inst;
	struct iris_core *core;
	struct v4l2_format *f;
	int ret = 0;

	if (!q || !num_buffers || !num_planes || !sizes)
		return -EINVAL;

	inst = vb2_get_drv_priv(q);
	if (!inst || !inst->core)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (inst->state == IRIS_INST_ERROR) {
		ret = -EBUSY;
		goto unlock;
	}

	core = inst->core;
	f = V4L2_TYPE_IS_OUTPUT(q->type) ? inst->fmt_src : inst->fmt_dst;

	if (inst->state == IRIS_INST_STREAMING) {
		ret = -EINVAL;
		goto unlock;
	}

	if (*num_planes) {
		if (*num_planes != f->fmt.pix_mp.num_planes ||
		    sizes[0] < f->fmt.pix_mp.plane_fmt[0].sizeimage) {
			ret = -EINVAL;
			goto unlock;
		}
	}

	buffer_type = iris_v4l2_type_to_driver(q->type);
	if (buffer_type == -EINVAL) {
		ret = -EINVAL;
		goto unlock;
	}

	if (!inst->once_per_session_set) {
		inst->once_per_session_set = true;

		mutex_lock(&core->lock);
		if (core->state == IRIS_CORE_ERROR) {
			mutex_unlock(&core->lock);
			ret = -EIO;
			goto unlock;
		}
		mutex_unlock(&core->lock);

		ret = core->hfi_ops->session_open(inst);
		if (ret) {
			ret = -EINVAL;
			dev_err(core->dev, "session open failed\n");
			goto unlock;
		}

		ret = iris_inst_change_state(inst, IRIS_INST_INIT);
		if (ret)
			goto unlock;
	}

	buffers = &inst->buffers[buffer_type];
	if (!buffers) {
		ret = -EINVAL;
		goto unlock;
	}

	buffers->min_count = iris_vpu_buf_count(inst, buffer_type);
	if (*num_buffers < buffers->min_count)
		*num_buffers = buffers->min_count;
	buffers->actual_count = *num_buffers;
	*num_planes = 1;

	buffers->size = iris_get_buffer_size(inst, buffer_type);

	if (sizes[0] < buffers->size) {
		f->fmt.pix_mp.plane_fmt[0].sizeimage = buffers->size;
		sizes[0] = f->fmt.pix_mp.plane_fmt[0].sizeimage;
	}

	q->dev = core->dev;

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

int iris_vb2_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct iris_inst *inst;
	int ret = 0;

	if (!q)
		return -EINVAL;

	inst = vb2_get_drv_priv(q);
	if (!inst || !inst->core)
		return -EINVAL;

	mutex_lock(&inst->lock);
	if (inst->state == IRIS_INST_ERROR) {
		ret = -EBUSY;
		goto error;
	}

	if (!V4L2_TYPE_IS_OUTPUT(q->type) &&
	    !V4L2_TYPE_IS_CAPTURE(q->type)) {
		ret = -EINVAL;
		goto error;
	}

	if (V4L2_TYPE_IS_OUTPUT(q->type))
		ret = iris_vdec_streamon_input(inst);
	else if (V4L2_TYPE_IS_CAPTURE(q->type))
		ret = iris_vdec_streamon_output(inst);
	if (ret)
		goto error;

	mutex_unlock(&inst->lock);

	return ret;

error:
	iris_inst_change_state(inst, IRIS_INST_ERROR);
	mutex_unlock(&inst->lock);

	return ret;
}

void iris_vb2_stop_streaming(struct vb2_queue *q)
{
	struct iris_inst *inst;

	if (!q)
		return;

	inst = vb2_get_drv_priv(q);
	if (!inst)
		return;

	mutex_lock(&inst->lock);

	if (!V4L2_TYPE_IS_OUTPUT(q->type) &&
	    !V4L2_TYPE_IS_CAPTURE(q->type))
		goto exit;

	iris_vdec_session_streamoff(inst, q->type);

exit:
	mutex_unlock(&inst->lock);
}
