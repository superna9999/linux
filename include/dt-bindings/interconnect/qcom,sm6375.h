/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Linaro Limited
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_SM6375_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_SM6375_H

/* BIMC */
#define MASTER_AMPSS_M0				0
#define MASTER_SNOC_BIMC_RT			1
#define MASTER_SNOC_BIMC_NRT			2
#define SNOC_BIMC_MAS				3
#define MASTER_GRAPHICS_3D			4
#define MASTER_CDSP_PROC			5
#define MASTER_TCU_0				6
#define SLAVE_EBI				7
#define BIMC_SNOC_SLV				8

/* CNOC */
#define MASTER_SNOC_CNOC			0
#define MASTER_QDSS_DAP				1
#define SLAVE_BIMC_CFG				2
#define SLAVE_CAMERA_NRT_THROTTLE_CFG		3
#define SLAVE_CAMERA_RT_THROTTLE_CFG		4
#define SLAVE_CAMERA_CFG			5
#define SLAVE_CLK_CTL				6
#define SLAVE_DSP_CFG				7
#define SLAVE_RBCPR_CX_CFG			8
#define SLAVE_RBCPR_MX_CFG			9
#define SLAVE_CRYPTO_0_CFG			10
#define SLAVE_DCC_CFG				11
#define SLAVE_DDR_PHY_CFG			12
#define SLAVE_DDR_SS_CFG			13
#define SLAVE_DISPLAY_CFG			14
#define SLAVE_DISPLAY_THROTTLE_CFG		15
#define SLAVE_EMMC_CFG				16
#define SLAVE_GRAPHICS_3D_CFG			17
#define SLAVE_HWKM				18
#define SLAVE_IMEM_CFG				19
#define SLAVE_IPA_CFG				20
#define SLAVE_LPASS				21
#define SLAVE_MAPSS				22
#define SLAVE_MESSAGE_RAM			23
#define SLAVE_PDM				24
#define SLAVE_PIMEM_CFG				25
#define SLAVE_PKA_CORE				26
#define SLAVE_PMIC_ARB				27
#define SLAVE_QDSS_CFG				28
#define SLAVE_QM_CFG				29
#define SLAVE_QM_MPU_CFG			30
#define SLAVE_QUP_0				31
#define SLAVE_QUP_1				32
#define SLAVE_RPM				33
#define SLAVE_SDCC_2				34
#define SLAVE_SECURITY				35
#define SLAVE_SNOC_CFG				36
#define SLAVE_TCSR				37
#define SLAVE_TLMM				38
#define SLAVE_UFS_MEM_CFG			39
#define SLAVE_USB3				40
#define SLAVE_VENUS_CFG				41
#define SLAVE_VENUS_THROTTLE_CFG		42
#define SLAVE_VSENSE_CTRL_CFG			43

/* SNOC */
#define MASTER_SNOC_CFG				0
#define MASTER_TIC				1
#define A1NOC_SNOC_MAS				2
#define A2NOC_SNOC_MAS				3
#define BIMC_SNOC_MAS				4
#define MASTER_PIMEM				5
#define MASTER_QUP_0				6
#define MASTER_QUP_1				7
#define MASTER_EMMC				8
#define MASTER_SDCC_2				9
#define MASTER_UFS_MEM				10
#define MASTER_CRYPTO_CORE0			11
#define MASTER_QDSS_BAM				12
#define MASTER_IPA				13
#define MASTER_QDSS_ETR				14
#define MASTER_USB3_0				15
#define MASTER_CAMNOC_SF_SNOC			16
#define MASTER_CAMNOC_HF_SNOC			17
#define MASTER_MDP_PORT0_SNOC			18
#define MASTER_VIDEO_P0_SNOC			19
#define MASTER_VIDEO_PROC_SNOC			20
#define SLAVE_APPSS				21
#define SNOC_CNOC_SLV				22
#define SLAVE_OCIMEM				23
#define SLAVE_PIMEM				24
#define SNOC_BIMC_SLV				25
#define SLAVE_SERVICE_SNOC			26
#define SLAVE_QDSS_STM				27
#define SLAVE_TCU				28
#define A1NOC_SNOC_SLV				29
#define A2NOC_SNOC_SLV				30
#define SLAVE_SNOC_RT				31
#define SLAVE_SNOC_NRT				32

/* CLK Virtual */
#define MASTER_QUP_CORE_0			0
#define MASTER_QUP_CORE_1			1
#define SLAVE_QUP_CORE_0			2
#define SLAVE_QUP_CORE_1			3

/* MMRT Virtual */
#define MASTER_CAMNOC_HF			0
#define MASTER_MDP_PORT0			1
#define MASTER_SNOC_RT				2
#define SLAVE_SNOC_BIMC_RT			3
#define SLAVE_CAMNOC_HF_SNOC			4
#define SLAVE_MDP_PORT0_SNOC			5

/* MMNRT Virtual */
#define MASTER_CAMNOC_SF			0
#define MASTER_VIDEO_P0				1
#define MASTER_VIDEO_PROC			2
#define MASTER_SNOC_NRT				3
#define SLAVE_SNOC_BIMC_NRT			4
#define SLAVE_CAMNOC_SF_SNOC			5
#define SLAVE_VIDEO_P0_SNOC			6
#define SLAVE_VIDEO_PROC_SNOC			7

#endif
