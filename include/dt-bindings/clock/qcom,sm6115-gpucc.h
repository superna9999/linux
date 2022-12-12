/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Linaro Limited
 */

#ifndef _DT_BINDINGS_CLK_QCOM_GPU_CC_SM6115_H
#define _DT_BINDINGS_CLK_QCOM_GPU_CC_SM6115_H

/* GPU_CC clocks */
#define GPUCC_PLL0			0
#define GPUCC_PLL0_OUT_AUX2		1
#define GPUCC_PLL1			2
#define GPUCC_PLL1_OUT_AUX		3
#define GPUCC_AHB_CLK			4
#define GPUCC_CRC_AHB_CLK		5
#define GPUCC_CX_GFX3D_CLK		6
#define GPUCC_CX_GMU_CLK		7
#define GPUCC_CX_SNOC_DVM_CLK		8
#define GPUCC_CXO_AON_CLK		9
#define GPUCC_CXO_CLK			10
#define GPUCC_GMU_CLK_SRC		11
#define GPUCC_GX_CXO_CLK		12
#define GPUCC_GX_GFX3D_CLK		13
#define GPUCC_GX_GFX3D_CLK_SRC		14
#define GPUCC_SLEEP_CLK		15
#define GPUCC_HLOS1_VOTE_GPU_SMMU_CLK	16

/* Resets */
#define GPU_GX_BCR			0

/* GDSCs */
#define GPU_CX_GDSC			0
#define GPU_GX_GDSC			1

#endif
