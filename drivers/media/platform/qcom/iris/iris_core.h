/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_CORE_H_
#define _IRIS_CORE_H_

#include <media/v4l2-device.h>

/**
 * struct iris_core - holds core parameters valid for all instances
 *
 * @dev: reference to device structure
 * @reg_base: IO memory base address
 * @irq: iris irq
 * @v4l2_dev: a holder for v4l2 device structure
 * @vdev_dec: iris video device structure for decoder
 */

struct iris_core {
	struct device				*dev;
	void __iomem				*reg_base;
	int					irq;
	struct v4l2_device			v4l2_dev;
	struct video_device			*vdev_dec;
};

#endif
