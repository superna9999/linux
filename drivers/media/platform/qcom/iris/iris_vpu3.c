// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/iopoll.h>

#include "iris_instance.h"
#include "iris_vpu_common.h"
#include "iris_vpu_register_defines.h"

#define AON_MVP_NOC_RESET			0x0001F000

#define WRAPPER_CORE_CLOCK_CONFIG		(WRAPPER_BASE_OFFS + 0x88)

#define CPU_CS_AHB_BRIDGE_SYNC_RESET		(CPU_CS_BASE_OFFS + 0x160)

#define AON_WRAPPER_MVP_NOC_RESET_REQ		(AON_MVP_NOC_RESET + 0x000)
#define AON_WRAPPER_MVP_NOC_RESET_ACK		(AON_MVP_NOC_RESET + 0x004)

#define VCODEC_SS_IDLE_STATUSN			(VCODEC_BASE_OFFS + 0x70)

static bool iris_vpu3_hw_power_collapsed(struct iris_core *core)
{
	u32 value = 0, pwr_status = 0;

	value = readl(core->reg_base + WRAPPER_CORE_POWER_STATUS);
	pwr_status = value & BIT(1);

	return pwr_status ? false : true;
}

static void iris_vpu3_power_off_hardware(struct iris_core *core)
{
	u32 reg_val = 0;
	u32 value = 0;
	int ret, i;

	if (iris_vpu3_hw_power_collapsed(core))
		goto disable_power;

	dev_err(core->dev, "video hw is power on\n");

	value = readl(core->reg_base + WRAPPER_CORE_CLOCK_CONFIG);
	if (value)
		writel(0, core->reg_base + WRAPPER_CORE_CLOCK_CONFIG);

	for (i = 0; i < core->iris_platform_data->num_vpp_pipe; i++) {
		ret = readl_poll_timeout(core->reg_base + VCODEC_SS_IDLE_STATUSN + 4 * i,
					 reg_val, reg_val & 0x400000, 2000, 20000);
		if (ret)
			goto disable_power;
	}

	writel(0x3, core->reg_base + AON_WRAPPER_MVP_NOC_RESET_REQ);

	ret = readl_poll_timeout(core->reg_base + AON_WRAPPER_MVP_NOC_RESET_ACK,
				 reg_val, reg_val & 0x3, 200, 2000);
	if (ret)
		goto disable_power;

	writel(0x0, core->reg_base + AON_WRAPPER_MVP_NOC_RESET_REQ);

	ret = readl_poll_timeout(core->reg_base + AON_WRAPPER_MVP_NOC_RESET_ACK,
				 reg_val, !(reg_val & 0x3), 200, 2000);
	if (ret)
		goto disable_power;

	writel(0x3, core->reg_base + CPU_CS_AHB_BRIDGE_SYNC_RESET);
	writel(0x2, core->reg_base + CPU_CS_AHB_BRIDGE_SYNC_RESET);
	writel(0x0, core->reg_base + CPU_CS_AHB_BRIDGE_SYNC_RESET);

disable_power:
	iris_vpu_power_off_hw(core);
}

static u64 iris_vpu3_calculate_frequency(struct iris_inst *inst, size_t data_size)
{
	u64 vsp_cycles = 0, vpp_cycles = 0, fw_cycles = 0;
	u64 fw_vpp_cycles = 0, bitrate = 0, freq = 0;
	u32 base_cycles = 0, fps, mbpf;
	u32 height = 0, width = 0;
	struct v4l2_format *inp_f;
	u32 mbs_per_second;

	inp_f = inst->fmt_src;
	width = max(inp_f->fmt.pix_mp.width, inst->crop.width);
	height = max(inp_f->fmt.pix_mp.height, inst->crop.height);

	mbpf = NUM_MBS_PER_FRAME(height, width);
	fps = DEFAULT_FPS;
	mbs_per_second = mbpf * fps;

	fw_cycles = fps * inst->driver_cap[MB_CYCLES_FW].value;
	fw_vpp_cycles = fps * inst->driver_cap[MB_CYCLES_FW_VPP].value;

	vpp_cycles = mbs_per_second * inst->driver_cap[MB_CYCLES_VPP].value /
		inst->fw_cap[PIPE].value;
	vpp_cycles += max(vpp_cycles / 20, fw_vpp_cycles);

	if (inst->fw_cap[PIPE].value > 1)
		vpp_cycles += div_u64(vpp_cycles * 59, 1000);

	bitrate = fps * data_size * 8;
	vsp_cycles = bitrate;

	base_cycles = 0;
	vsp_cycles = div_u64(vsp_cycles, 2);

	vsp_cycles = div_u64(vsp_cycles * 21, 20);

	if (inst->fw_cap[STAGE].value == STAGE_1)
		vsp_cycles = vsp_cycles * 3;

	vsp_cycles += mbs_per_second * base_cycles;

	freq = max3(vpp_cycles, vsp_cycles, fw_cycles);

	return freq;
}

const struct vpu_ops iris_vpu3_ops = {
	.power_off_hw = iris_vpu3_power_off_hardware,
	.calc_freq = iris_vpu3_calculate_frequency,
};
