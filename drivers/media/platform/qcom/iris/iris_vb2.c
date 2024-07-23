// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_buffer.h"
#include "iris_instance.h"
#include "iris_vb2.h"
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

	core = inst->core;
	f = V4L2_TYPE_IS_OUTPUT(q->type) ? inst->fmt_src : inst->fmt_dst;

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
