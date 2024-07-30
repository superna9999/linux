// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/pm_runtime.h>

#include "iris_firmware.h"
#include "iris_core.h"
#include "iris_hfi_common.h"
#include "iris_power.h"
#include "iris_vpu_common.h"

u32 iris_hfi_get_v4l2_color_primaries(u32 hfi_primaries)
{
	u32 primaries = V4L2_COLORSPACE_DEFAULT;

	switch (hfi_primaries) {
	case HFI_PRIMARIES_RESERVED:
		primaries = V4L2_COLORSPACE_DEFAULT;
		break;
	case HFI_PRIMARIES_BT709:
		primaries = V4L2_COLORSPACE_REC709;
		break;
	case HFI_PRIMARIES_BT470_SYSTEM_M:
		primaries = V4L2_COLORSPACE_470_SYSTEM_M;
		break;
	case HFI_PRIMARIES_BT470_SYSTEM_BG:
		primaries = V4L2_COLORSPACE_470_SYSTEM_BG;
		break;
	case HFI_PRIMARIES_BT601_525:
		primaries = V4L2_COLORSPACE_SMPTE170M;
		break;
	case HFI_PRIMARIES_SMPTE_ST240M:
		primaries = V4L2_COLORSPACE_SMPTE240M;
		break;
	case HFI_PRIMARIES_BT2020:
		primaries = V4L2_COLORSPACE_BT2020;
		break;
	case V4L2_COLORSPACE_DCI_P3:
		primaries = HFI_PRIMARIES_SMPTE_RP431_2;
		break;
	default:
		break;
	}

	return primaries;
}

u32 iris_hfi_get_v4l2_transfer_char(u32 hfi_characterstics)
{
	u32 characterstics = V4L2_XFER_FUNC_DEFAULT;

	switch (hfi_characterstics) {
	case HFI_TRANSFER_RESERVED:
		characterstics = V4L2_XFER_FUNC_DEFAULT;
		break;
	case HFI_TRANSFER_BT709:
		characterstics = V4L2_XFER_FUNC_709;
		break;
	case HFI_TRANSFER_SMPTE_ST240M:
		characterstics = V4L2_XFER_FUNC_SMPTE240M;
		break;
	case HFI_TRANSFER_SRGB_SYCC:
		characterstics = V4L2_XFER_FUNC_SRGB;
		break;
	case HFI_TRANSFER_SMPTE_ST2084_PQ:
		characterstics = V4L2_XFER_FUNC_SMPTE2084;
		break;
	default:
		break;
	}

	return characterstics;
}

u32 iris_hfi_get_v4l2_matrix_coefficients(u32 hfi_coefficients)
{
	u32 coefficients = V4L2_YCBCR_ENC_DEFAULT;

	switch (hfi_coefficients) {
	case HFI_MATRIX_COEFF_RESERVED:
		coefficients = V4L2_YCBCR_ENC_DEFAULT;
		break;
	case HFI_MATRIX_COEFF_BT709:
		coefficients = V4L2_YCBCR_ENC_709;
		break;
	case HFI_MATRIX_COEFF_BT470_SYS_BG_OR_BT601_625:
		coefficients = V4L2_YCBCR_ENC_XV601;
		break;
	case HFI_MATRIX_COEFF_BT601_525_BT1358_525_OR_625:
		coefficients = V4L2_YCBCR_ENC_601;
		break;
	case HFI_MATRIX_COEFF_SMPTE_ST240:
		coefficients = V4L2_YCBCR_ENC_SMPTE240M;
		break;
	case HFI_MATRIX_COEFF_BT2020_NON_CONSTANT:
		coefficients = V4L2_YCBCR_ENC_BT2020;
		break;
	case HFI_MATRIX_COEFF_BT2020_CONSTANT:
		coefficients = V4L2_YCBCR_ENC_BT2020_CONST_LUM;
		break;
	default:
		break;
	}

	return coefficients;
}

int iris_hfi_core_init(struct iris_core *core)
{
	const struct iris_hfi_command_ops *hfi_ops = core->hfi_ops;
	int ret;

	ret = hfi_ops->sys_init(core);
	if (ret)
		return ret;

	ret = hfi_ops->sys_image_version(core);
	if (ret)
		return ret;

	return hfi_ops->sys_interframe_powercollapse(core);
}

irqreturn_t iris_hfi_isr(int irq, void *data)
{
	disable_irq_nosync(irq);

	return IRQ_WAKE_THREAD;
}

irqreturn_t iris_hfi_isr_handler(int irq, void *data)
{
	struct iris_core *core = data;

	if (!core)
		return IRQ_NONE;

	mutex_lock(&core->lock);
	if (core->state != IRIS_CORE_INIT) {
		mutex_unlock(&core->lock);
		goto exit;
	}
	pm_runtime_mark_last_busy(core->dev);
	iris_vpu_clear_interrupt(core);
	mutex_unlock(&core->lock);

	core->hfi_response_ops->hfi_response_handler(core);

exit:
	if (!iris_vpu_watchdog(core, core->intr_status))
		enable_irq(irq);

	return IRQ_HANDLED;
}

int iris_hfi_pm_suspend(struct iris_core *core)
{
	int ret;

	if (!mutex_is_locked(&core->lock))
		return -EINVAL;

	if (core->state != IRIS_CORE_INIT)
		return -EINVAL;

	if (!core->power_enabled) {
		dev_err(core->dev, "power not enabled\n");
		return 0;
	}

	ret = iris_vpu_prepare_pc(core);
	if (ret) {
		dev_err(core->dev, "prepare pc ret %d\n", ret);
		pm_runtime_mark_last_busy(core->dev);
		return -EAGAIN;
	}

	ret = iris_set_hw_state(core, false);
	if (ret)
		return ret;

	iris_power_off(core);

	return 0;
}

int iris_hfi_pm_resume(struct iris_core *core)
{
	const struct iris_hfi_command_ops *ops;
	int ret;

	ops = core->hfi_ops;

	ret = iris_power_on(core);
	if (ret)
		goto error;

	ret = iris_set_hw_state(core, true);
	if (ret)
		goto err_power_off;

	ret = iris_vpu_boot_firmware(core);
	if (ret)
		goto err_suspend_hw;

	ret = ops->sys_interframe_powercollapse(core);
	if (ret)
		goto err_suspend_hw;

	return 0;

err_suspend_hw:
	iris_set_hw_state(core, false);
err_power_off:
	iris_power_off(core);
error:
	dev_err(core->dev, "failed to resume\n");

	return -EBUSY;
}
