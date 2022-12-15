/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Qualcomm SM6115 interconnect IDs
 *
 * Copyright (c) 2022, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Linaro Limited
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_SM6115_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_SM6115_H

#define MASTER_AMPSS_M0			0
#define MASTER_SNOC_BIMC_RT		1
#define MASTER_SNOC_BIMC_NRT		2
#define SNOC_BIMC_MAS			3
#define MASTER_GPU_CDSP_PROC		4
#define MASTER_TCU_0			5
#define SLAVE_EBI_CH0			6
#define BIMC_SNOC_SLV			7

#define SNOC_CNOC_MAS			0
#define MASTER_QDSS_DAP			1
#define SLAVE_BIMC_CFG			2
#define SLAVE_CAMERA_NRT_THROTTLE_CFG	3
#define SLAVE_CAMERA_RT_THROTTLE_CFG	4
#define SLAVE_CAMERA_CFG		5
#define SLAVE_CDSP_THROTTLE_CFG		6
#define SLAVE_CLK_CTL			7
#define SLAVE_CRYPTO_0_CFG		8
#define SLAVE_DISPLAY_CFG		9
#define SLAVE_DISPLAY_THROTTLE_CFG	10
#define SLAVE_GPU_CFG			11
#define SLAVE_IMEM_CFG			12
#define SLAVE_IPA_CFG			13
#define SLAVE_LPASS			14
#define SLAVE_MESSAGE_RAM		15
#define SLAVE_PDM			16
#define SLAVE_PIMEM_CFG			17
#define SLAVE_PMIC_ARB			18
#define SLAVE_PRNG			19
#define SLAVE_QDSS_CFG			20
#define SLAVE_QM_CFG			21
#define SLAVE_QM_MPU_CFG		22
#define SLAVE_QUP_0			23
#define SLAVE_SDCC_1			24
#define SLAVE_SDCC_2			25
#define SLAVE_SNOC_CFG			26
#define SLAVE_TCSR			27
#define SLAVE_TLMM_EAST			28
#define SLAVE_TLMM_SOUTH		29
#define SLAVE_TLMM_WEST			30
#define SLAVE_UFS_MEM_CFG		31
#define SLAVE_USB3			32
#define SLAVE_VENUS_CFG			33
#define SLAVE_VENUS_THROTTLE_CFG	34
#define SLAVE_VSENSE_CTRL_CFG		35
#define SLAVE_SERVICE_CNOC		36

#define MASTER_QUP_CORE_0		0
#define SLAVE_QUP_CORE_0		1

#define MASTER_CRYPTO_CORE0		0
#define MASTER_SNOC_CFG			1
#define MASTER_TIC			2
#define MASTER_ANOC_SNOC		3
#define BIMC_SNOC_MAS			4
#define MASTER_PIMEM			5
#define MASTER_QDSS_BAM			6
#define MASTER_QUP_0			7
#define MASTER_IPA			8
#define MASTER_QDSS_ETR			9
#define MASTER_SDCC_1			10
#define MASTER_SDCC_2			11
#define MASTER_UFS_MEM			12
#define MASTER_USB3			13
#define MASTER_GRAPHICS_3D_PORT1	14
#define SLAVE_APPSS			15
#define SNOC_CNOC_SLV			16
#define SLAVE_OCIMEM			17
#define SLAVE_PIMEM			18
#define SNOC_BIMC_SLV			19
#define SLAVE_SERVICE_SNOC		20
#define SLAVE_QDSS_STM			21
#define SLAVE_TCU			22
#define SLAVE_ANOC_SNOC			23

#define MASTER_GRAPHICS_3D		0
#define SLAVE_GPU_CDSP_BIMC		1

#define MASTER_CAMNOC_SF		0
#define MASTER_VIDEO_P0			1
#define MASTER_VIDEO_PROC		2
#define SLAVE_SNOC_BIMC_NRT		3

#define MASTER_CAMNOC_HF		0
#define MASTER_MDP_PORT0		1
#define SLAVE_SNOC_BIMC_RT		2

#endif
