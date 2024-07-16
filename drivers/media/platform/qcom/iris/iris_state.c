// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_state.h"

void iris_change_core_state(struct iris_core *core,
			    enum iris_core_state request_state)
{
	mutex_lock(&core->lock);
	core->state = request_state;
	mutex_unlock(&core->lock);
}
