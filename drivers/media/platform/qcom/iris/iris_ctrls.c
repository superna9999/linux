// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_ctrls.h"
#include "iris_instance.h"

static bool iris_valid_cap_id(enum platform_inst_fw_cap_type cap_id)
{
	return cap_id >= 1 && cap_id < INST_FW_CAP_MAX;
}

static enum platform_inst_fw_cap_type iris_get_cap_id(u32 id)
{
	switch (id) {
	case V4L2_CID_MPEG_VIDEO_DECODER_MPEG4_DEBLOCK_FILTER:
		return DEBLOCK;
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
		return PROFILE;
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
		return LEVEL;
	default:
		return INST_FW_CAP_MAX;
	}
}

static u32 iris_get_v4l2_id(enum platform_inst_fw_cap_type cap_id)
{
	if (!iris_valid_cap_id(cap_id))
		return 0;

	switch (cap_id) {
	case DEBLOCK:
		return V4L2_CID_MPEG_VIDEO_DECODER_MPEG4_DEBLOCK_FILTER;
	case PROFILE:
		return V4L2_CID_MPEG_VIDEO_H264_PROFILE;
	case LEVEL:
		return V4L2_CID_MPEG_VIDEO_H264_LEVEL;
	default:
		return 0;
	}
}

static int iris_vdec_op_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	enum platform_inst_fw_cap_type cap_id;
	struct iris_inst *inst = NULL;

	inst = container_of(ctrl->handler, struct iris_inst, ctrl_handler);
	switch (ctrl->id) {
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		ctrl->val = inst->buffers[BUF_OUTPUT].min_count;
		break;
	case V4L2_CID_MIN_BUFFERS_FOR_OUTPUT:
		ctrl->val = inst->buffers[BUF_INPUT].min_count;
		break;
	default:
		cap_id = iris_get_cap_id(ctrl->id);
		if (iris_valid_cap_id(cap_id))
			ctrl->val = inst->fw_cap[cap_id].value;
		else
			return -EINVAL;
	}

	return 0;
}

static int iris_vdec_op_s_ctrl(struct v4l2_ctrl *ctrl)
{
	enum platform_inst_fw_cap_type cap_id;
	struct platform_inst_fw_cap *cap;
	struct iris_inst *inst;

	inst = container_of(ctrl->handler, struct iris_inst, ctrl_handler);
	cap = &inst->fw_cap[0];

	cap_id = iris_get_cap_id(ctrl->id);
	if (!iris_valid_cap_id(cap_id))
		return -EINVAL;

	cap[cap_id].flags |= CAP_FLAG_CLIENT_SET;

	inst->fw_cap[cap_id].value = ctrl->val;

	return 0;
}

static const struct v4l2_ctrl_ops iris_ctrl_ops = {
	.s_ctrl = iris_vdec_op_s_ctrl,
	.g_volatile_ctrl = iris_vdec_op_g_volatile_ctrl,
};

int iris_ctrls_init(struct iris_inst *inst)
{
	struct platform_inst_fw_cap *cap;
	int num_ctrls = 0, ctrl_idx = 0;
	int idx = 0, ret;
	u32 v4l2_id;

	cap = &inst->fw_cap[0];

	for (idx = 1; idx < INST_FW_CAP_MAX; idx++) {
		if (iris_get_v4l2_id(cap[idx].cap_id))
			num_ctrls++;
	}
	if (!num_ctrls)
		return -EINVAL;

	ret = v4l2_ctrl_handler_init(&inst->ctrl_handler, num_ctrls);
	if (ret)
		return ret;

	for (idx = 1; idx < INST_FW_CAP_MAX; idx++) {
		struct v4l2_ctrl *ctrl;

		v4l2_id = iris_get_v4l2_id(cap[idx].cap_id);
		if (!v4l2_id)
			continue;

		if (ctrl_idx >= num_ctrls) {
			ret = -EINVAL;
			goto error;
		}

		if (cap[idx].flags & CAP_FLAG_MENU) {
			ctrl = v4l2_ctrl_new_std_menu(&inst->ctrl_handler,
						      &iris_ctrl_ops,
						      v4l2_id,
						      cap[idx].max,
						      ~(cap[idx].step_or_mask),
						      cap[idx].value);
		} else {
			ctrl = v4l2_ctrl_new_std(&inst->ctrl_handler,
						 &iris_ctrl_ops,
						 v4l2_id,
						 cap[idx].min,
						 cap[idx].max,
						 cap[idx].step_or_mask,
						 cap[idx].value);
		}
		if (!ctrl) {
			ret = -EINVAL;
			goto error;
		}

		ret = inst->ctrl_handler.error;
		if (ret)
			goto error;

		if ((cap[idx].flags & CAP_FLAG_VOLATILE) ||
		    (ctrl->id == V4L2_CID_MIN_BUFFERS_FOR_CAPTURE ||
		     ctrl->id == V4L2_CID_MIN_BUFFERS_FOR_OUTPUT))
			ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

		ctrl->flags |= V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
		ctrl_idx++;
	}

	return 0;
error:
	v4l2_ctrl_handler_free(&inst->ctrl_handler);

	return ret;
}

int iris_session_init_caps(struct iris_core *core)
{
	struct platform_inst_fw_cap *inst_plat_cap_data;
	int i, num_inst_cap;
	u32 cap_id;

	inst_plat_cap_data = core->iris_platform_data->inst_fw_cap_data;
	if (!inst_plat_cap_data)
		return -EINVAL;

	num_inst_cap = core->iris_platform_data->inst_fw_cap_data_size;

	for (i = 0; i < num_inst_cap && i < INST_FW_CAP_MAX - 1; i++) {
		cap_id = inst_plat_cap_data[i].cap_id;
		if (!iris_valid_cap_id(cap_id))
			continue;

		core->inst_fw_cap[cap_id].cap_id = inst_plat_cap_data[i].cap_id;
		core->inst_fw_cap[cap_id].min = inst_plat_cap_data[i].min;
		core->inst_fw_cap[cap_id].max = inst_plat_cap_data[i].max;
		core->inst_fw_cap[cap_id].step_or_mask = inst_plat_cap_data[i].step_or_mask;
		core->inst_fw_cap[cap_id].value = inst_plat_cap_data[i].value;
		core->inst_fw_cap[cap_id].flags = inst_plat_cap_data[i].flags;
		core->inst_fw_cap[cap_id].hfi_id = inst_plat_cap_data[i].hfi_id;
	}

	return 0;
}
