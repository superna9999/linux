// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Linaro Limited
 */

#include <dt-bindings/interconnect/qcom,sm6375.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "icc-rpm.h"
#include "smd-rpm.h"

enum {
	SM6375_MASTER_AMPSS_M0,
	SM6375_MASTER_SNOC_BIMC_RT,
	SM6375_MASTER_SNOC_BIMC_NRT,
	SM6375_MASTER_SNOC_BIMC,
	SM6375_MASTER_GRAPHICS_3D,
	SM6375_MASTER_CDSP_PROC,
	SM6375_MASTER_TCU_0,
	SM6375_MASTER_SNOC_CNOC,
	SM6375_MASTER_QDSS_DAP,
	SM6375_MASTER_SNOC_CFG,
	SM6375_MASTER_TIC,
	SM6375_MASTER_A1NOC_SNOC,
	SM6375_MASTER_A2NOC_SNOC,
	SM6375_MASTER_BIMC_SNOC,
	SM6375_MASTER_PIMEM,
	SM6375_MASTER_QUP_0,
	SM6375_MASTER_QUP_1,
	SM6375_MASTER_EMMC,
	SM6375_MASTER_SDCC_2,
	SM6375_MASTER_UFS_MEM,
	SM6375_MASTER_CRYPTO_CORE0,
	SM6375_MASTER_QDSS_BAM,
	SM6375_MASTER_IPA,
	SM6375_MASTER_QDSS_ETR,
	SM6375_MASTER_USB3_0,
	SM6375_MASTER_CAMNOC_SF_SNOC,
	SM6375_MASTER_CAMNOC_HF_SNOC,
	SM6375_MASTER_MDP_PORT0_SNOC,
	SM6375_MASTER_VIDEO_P0_SNOC,
	SM6375_MASTER_VIDEO_PROC_SNOC,
	SM6375_MASTER_QUP_CORE_0,
	SM6375_MASTER_QUP_CORE_1,
	SM6375_MASTER_CAMNOC_HF,
	SM6375_MASTER_MDP_PORT0,
	SM6375_MASTER_SNOC_RT,
	SM6375_MASTER_CAMNOC_SF,
	SM6375_MASTER_VIDEO_P0,
	SM6375_MASTER_VIDEO_PROC,
	SM6375_MASTER_SNOC_NRT,

	SM6375_SLAVE_EBI,
	SM6375_SLAVE_BIMC_SNOC,
	SM6375_SLAVE_BIMC_CFG,
	SM6375_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	SM6375_SLAVE_CAMERA_RT_THROTTLE_CFG,
	SM6375_SLAVE_CAMERA_CFG,
	SM6375_SLAVE_CLK_CTL,
	SM6375_SLAVE_DSP_CFG,
	SM6375_SLAVE_RBCPR_CX_CFG,
	SM6375_SLAVE_RBCPR_MX_CFG,
	SM6375_SLAVE_CRYPTO_0_CFG,
	SM6375_SLAVE_DCC_CFG,
	SM6375_SLAVE_DDR_PHY_CFG,
	SM6375_SLAVE_DDR_SS_CFG,
	SM6375_SLAVE_DISPLAY_CFG,
	SM6375_SLAVE_DISPLAY_THROTTLE_CFG,
	SM6375_SLAVE_EMMC_CFG,
	SM6375_SLAVE_GRAPHICS_3D_CFG,
	SM6375_SLAVE_HWKM,
	SM6375_SLAVE_IMEM_CFG,
	SM6375_SLAVE_IPA_CFG,
	SM6375_SLAVE_LPASS,
	SM6375_SLAVE_MAPSS,
	SM6375_SLAVE_MESSAGE_RAM,
	SM6375_SLAVE_PDM,
	SM6375_SLAVE_PIMEM_CFG,
	SM6375_SLAVE_PKA_CORE,
	SM6375_SLAVE_PMIC_ARB,
	SM6375_SLAVE_QDSS_CFG,
	SM6375_SLAVE_QM_CFG,
	SM6375_SLAVE_QM_MPU_CFG,
	SM6375_SLAVE_QUP_0,
	SM6375_SLAVE_QUP_1,
	SM6375_SLAVE_RPM,
	SM6375_SLAVE_SDCC_2,
	SM6375_SLAVE_SECURITY,
	SM6375_SLAVE_SNOC_CFG,
	SM6375_SLAVE_TCSR,
	SM6375_SLAVE_TLMM,
	SM6375_SLAVE_UFS_MEM_CFG,
	SM6375_SLAVE_USB3,
	SM6375_SLAVE_VENUS_CFG,
	SM6375_SLAVE_VENUS_THROTTLE_CFG,
	SM6375_SLAVE_VSENSE_CTRL_CFG,
	SM6375_SLAVE_APPSS,
	SM6375_SLAVE_SNOC_CNOC,
	SM6375_SLAVE_OCIMEM,
	SM6375_SLAVE_PIMEM,
	SM6375_SLAVE_SNOC_BIMC,
	SM6375_SLAVE_SERVICE_SNOC,
	SM6375_SLAVE_QDSS_STM,
	SM6375_SLAVE_TCU,
	SM6375_SLAVE_A1NOC_SNOC,
	SM6375_SLAVE_A2NOC_SNOC,
	SM6375_SLAVE_SNOC_RT,
	SM6375_SLAVE_SNOC_NRT,
	SM6375_SLAVE_QUP_CORE_0,
	SM6375_SLAVE_QUP_CORE_1,
	SM6375_SLAVE_SNOC_BIMC_RT,
	SM6375_SLAVE_CAMNOC_HF_SNOC,
	SM6375_SLAVE_MDP_PORT0_SNOC,
	SM6375_SLAVE_SNOC_BIMC_NRT,
	SM6375_SLAVE_CAMNOC_SF_SNOC,
	SM6375_SLAVE_VIDEO_P0_SNOC,
	SM6375_SLAVE_VIDEO_PROC_SNOC,
};

static const u16 apps_proc_links[] = {
	SM6375_SLAVE_EBI,
	SM6375_SLAVE_BIMC_SNOC,
};

static struct qcom_icc_node apps_proc = {
	.name = "apps_proc",
	.id = SM6375_MASTER_AMPSS_M0,
	.buswidth = 16,
	.qos.qos_port = 2,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.prio_level = 0,
	.qos.areq_prio = 0,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 2,
	.links = apps_proc_links,
};

static const u16 mas_snoc_rt_links[] = {
	SM6375_SLAVE_SNOC_BIMC_RT,
};

static struct qcom_icc_node mas_snoc_rt = {
	.name = "mas_snoc_rt",
	.id = SM6375_MASTER_SNOC_RT,
	.buswidth = 256,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = mas_snoc_rt_links,
};

static const u16 mas_snoc_bimc_rt_links[] = {
	SM6375_SLAVE_EBI,
};

static struct qcom_icc_node mas_snoc_bimc_rt = {
	.name = "mas_snoc_bimc_rt",
	.id = SM6375_MASTER_SNOC_BIMC_RT,
	.buswidth = 16,
	.qos.qos_port = 4,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = mas_snoc_bimc_rt_links,
};

static const u16 mas_snoc_nrt_links[] = {
	SM6375_SLAVE_SNOC_BIMC_NRT,
};

static struct qcom_icc_node mas_snoc_nrt = {
	.name = "mas_snoc_nrt",
	.id = SM6375_MASTER_SNOC_NRT,
	.buswidth = 256,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = mas_snoc_nrt_links,
};

static const u16 mas_snoc_bimc_nrt_links[] = {
	SM6375_SLAVE_EBI,
};

static struct qcom_icc_node mas_snoc_bimc_nrt = {
	.name = "mas_snoc_bimc_nrt",
	.id = SM6375_MASTER_SNOC_BIMC_NRT,
	.buswidth = 16,
	.qos.qos_port = 5,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = mas_snoc_bimc_nrt_links,
};

static const u16 mas_snoc_bimc_links[] = {
	SM6375_SLAVE_EBI,
};

static struct qcom_icc_node mas_snoc_bimc = {
	.name = "mas_snoc_bimc",
	.id = SM6375_MASTER_SNOC_BIMC,
	.buswidth = 16,
	.qos.qos_port = 9,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.mas_rpm_id = 3,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = mas_snoc_bimc_links,
};

static const u16 qnm_gpu_links[] = {
	SM6375_SLAVE_EBI,
	SM6375_SLAVE_BIMC_SNOC,
};

static struct qcom_icc_node qnm_gpu = {
	.name = "qnm_gpu",
	.id = SM6375_MASTER_GRAPHICS_3D,
	.buswidth = 32,
	.qos.qos_port = 9,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.prio_level = 0,
	.qos.areq_prio = 0,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 2,
	.links = qnm_gpu_links,
};


static const u16 qnm_cdsp_links[] = {
	SM6375_SLAVE_EBI,
	SM6375_SLAVE_BIMC_SNOC,
};

static struct qcom_icc_node qnm_cdsp = {
	.name = "qnm_cdsp",
	.id = SM6375_MASTER_CDSP_PROC,
	.buswidth = 32,
	.qos.qos_port = 8,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.prio_level = 0,
	.qos.areq_prio = 0,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 2,
	.links = qnm_cdsp_links,
};


static const u16 tcu_0_links[] = {
	SM6375_SLAVE_EBI,
	SM6375_SLAVE_BIMC_SNOC,
};

static struct qcom_icc_node tcu_0 = {
	.name = "tcu_0",
	.id = SM6375_MASTER_TCU_0,
	.buswidth = 8,
	.qos.qos_port = 6,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.prio_level = 0,
	.qos.areq_prio = 0,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 2,
	.links = tcu_0_links,
};

static const u16 qup0_core_master_links[] = {
	SM6375_SLAVE_QUP_CORE_0,
};

static struct qcom_icc_node qup0_core_master = {
	.name = "qup0_core_master",
	.id = SM6375_MASTER_QUP_CORE_0,
	.buswidth = 4,
	.mas_rpm_id = 170,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = qup0_core_master_links,
};

static const u16 qup1_core_master_links[] = {
	SLAVE_QUP_CORE_1,
};

static struct qcom_icc_node qup1_core_master = {
	.name = "qup1_core_master",
	.id = SM6375_MASTER_QUP_CORE_1,
	.buswidth = 4,
	.mas_rpm_id = 171,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = qup1_core_master_links,
};


static const u16 mas_snoc_cnoc_links[] = {
	SM6375_SLAVE_BIMC_CFG,
	SM6375_SLAVE_APPSS,
	SM6375_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	SM6375_SLAVE_CAMERA_RT_THROTTLE_CFG,
	SM6375_SLAVE_CAMERA_CFG,
	SM6375_SLAVE_CLK_CTL,
	SM6375_SLAVE_DSP_CFG,
	SM6375_SLAVE_RBCPR_CX_CFG,
	SM6375_SLAVE_RBCPR_MX_CFG,
	SM6375_SLAVE_CRYPTO_0_CFG,
	SM6375_SLAVE_DCC_CFG,
	SM6375_SLAVE_DDR_PHY_CFG,
	SM6375_SLAVE_DDR_SS_CFG,
	SM6375_SLAVE_DISPLAY_CFG,
	SM6375_SLAVE_DISPLAY_THROTTLE_CFG,
	SM6375_SLAVE_EMMC_CFG,
	SM6375_SLAVE_GRAPHICS_3D_CFG,
	SM6375_SLAVE_HWKM,
	SM6375_SLAVE_IMEM_CFG,
	SM6375_SLAVE_IPA_CFG,
	SM6375_SLAVE_LPASS,
	SM6375_SLAVE_MAPSS,
	SM6375_SLAVE_MESSAGE_RAM,
	SM6375_SLAVE_PDM,
	SM6375_SLAVE_PIMEM_CFG,
	SM6375_SLAVE_PKA_CORE,
	SM6375_SLAVE_PMIC_ARB,
	SM6375_SLAVE_QDSS_CFG,
	SM6375_SLAVE_QM_CFG,
	SM6375_SLAVE_QM_MPU_CFG,
	SM6375_SLAVE_QUP_0,
	SM6375_SLAVE_QUP_1,
	SM6375_SLAVE_RPM,
	SM6375_SLAVE_SDCC_2,
	SM6375_SLAVE_SECURITY,
	SM6375_SLAVE_SNOC_CFG,
	SM6375_SLAVE_TCSR,
	SM6375_SLAVE_TLMM,
	SM6375_SLAVE_UFS_MEM_CFG,
	SM6375_SLAVE_USB3,
	SM6375_SLAVE_VENUS_CFG,
	SM6375_SLAVE_VENUS_THROTTLE_CFG,
	SM6375_SLAVE_VSENSE_CTRL_CFG,
};

static struct qcom_icc_node mas_snoc_cnoc = {
	.name = "mas_snoc_cnoc",
	.id = SM6375_MASTER_SNOC_CNOC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 43,
	.links = mas_snoc_cnoc_links,
};

static const u16 xm_qdss_dap_links[] = {
	SM6375_SLAVE_BIMC_CFG,
	SM6375_SLAVE_APPSS,
	SM6375_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	SM6375_SLAVE_CAMERA_RT_THROTTLE_CFG,
	SM6375_SLAVE_CAMERA_CFG,
	SM6375_SLAVE_CLK_CTL,
	SM6375_SLAVE_DSP_CFG,
	SM6375_SLAVE_RBCPR_CX_CFG,
	SM6375_SLAVE_RBCPR_MX_CFG,
	SM6375_SLAVE_CRYPTO_0_CFG,
	SM6375_SLAVE_DCC_CFG,
	SM6375_SLAVE_DDR_PHY_CFG,
	SM6375_SLAVE_DDR_SS_CFG,
	SM6375_SLAVE_DISPLAY_CFG,
	SM6375_SLAVE_DISPLAY_THROTTLE_CFG,
	SM6375_SLAVE_EMMC_CFG,
	SM6375_SLAVE_GRAPHICS_3D_CFG,
	SM6375_SLAVE_HWKM,
	SM6375_SLAVE_IMEM_CFG,
	SM6375_SLAVE_IPA_CFG,
	SM6375_SLAVE_LPASS,
	SM6375_SLAVE_MAPSS,
	SM6375_SLAVE_MESSAGE_RAM,
	SM6375_SLAVE_PDM,
	SM6375_SLAVE_PIMEM_CFG,
	SM6375_SLAVE_PKA_CORE,
	SM6375_SLAVE_PMIC_ARB,
	SM6375_SLAVE_QDSS_CFG,
	SM6375_SLAVE_QM_CFG,
	SM6375_SLAVE_QM_MPU_CFG,
	SM6375_SLAVE_QUP_0,
	SM6375_SLAVE_QUP_1,
	SM6375_SLAVE_RPM,
	SM6375_SLAVE_SDCC_2,
	SM6375_SLAVE_SECURITY,
	SM6375_SLAVE_SNOC_CFG,
	SM6375_SLAVE_TCSR,
	SM6375_SLAVE_TLMM,
	SM6375_SLAVE_UFS_MEM_CFG,
	SM6375_SLAVE_USB3,
	SM6375_SLAVE_VENUS_CFG,
	SM6375_SLAVE_VENUS_THROTTLE_CFG,
	SM6375_SLAVE_VSENSE_CTRL_CFG,
};

static struct qcom_icc_node xm_qdss_dap = {
	.name = "xm_qdss_dap",
	.id = SM6375_MASTER_QDSS_DAP,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 43,
	.links = xm_qdss_dap_links,
};

static const u16 qnm_camera_nrt_links[] = {
	SM6375_SLAVE_CAMNOC_SF_SNOC,
};

static struct qcom_icc_node qnm_camera_nrt = {
	.name = "qnm_camera_nrt",
	.id = SM6375_MASTER_CAMNOC_SF,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = qnm_camera_nrt_links,
};


static const u16 qnm_camera_nrt_snoc_links[] = {
	SM6375_SLAVE_SNOC_NRT,
};

static struct qcom_icc_node qnm_camera_nrt_snoc = {
	.name = "qnm_camera_nrt_snoc",
	.id = SM6375_MASTER_CAMNOC_SF_SNOC,
	.buswidth = 256,
	.qos.qos_port = 25,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = qnm_camera_nrt_snoc_links,
};

static const u16 qnm_camera_rt_links[] = {
	SM6375_SLAVE_CAMNOC_HF_SNOC,
};

static struct qcom_icc_node qnm_camera_rt = {
	.name = "qnm_camera_rt",
	.id = SM6375_MASTER_CAMNOC_HF,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = qnm_camera_rt_links,
};


static const u16 qnm_camera_rt_snoc_links[] = {
	SM6375_SLAVE_SNOC_RT,
};

static struct qcom_icc_node qnm_camera_rt_snoc = {
	.name = "qnm_camera_rt_snoc",
	.id = SM6375_MASTER_CAMNOC_HF_SNOC,
	.buswidth = 256,
	.qos.qos_port = 31,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.urg_fwd_en = true,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = qnm_camera_rt_snoc_links,
};

static const u16 qxm_mdp0_links[] = {
	SM6375_SLAVE_MDP_PORT0_SNOC,
};

static struct qcom_icc_node qxm_mdp0 = {
	.name = "qxm_mdp0",
	.id = SM6375_MASTER_MDP_PORT0,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = qxm_mdp0_links,
};

static const u16 qxm_mdp0_snoc_links[] = {
	SM6375_SLAVE_SNOC_RT,
};

static struct qcom_icc_node qxm_mdp0_snoc = {
	.name = "qxm_mdp0_snoc",
	.id = SM6375_MASTER_MDP_PORT0_SNOC,
	.buswidth = 256,
	.qos.qos_port = 26,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.urg_fwd_en = true,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = qxm_mdp0_snoc_links,
};

static const u16 qxm_venus0_links[] = {
	SM6375_SLAVE_VIDEO_P0_SNOC,
};

static struct qcom_icc_node qxm_venus0 = {
	.name = "qxm_venus0",
	.id = SM6375_MASTER_VIDEO_P0,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = qxm_venus0_links,
};

static const u16 qxm_venus0_snoc_links[] = {
	SM6375_SLAVE_SNOC_NRT,
};

static struct qcom_icc_node qxm_venus0_snoc = {
	.name = "qxm_venus0_snoc",
	.id = SM6375_MASTER_VIDEO_P0_SNOC,
	.buswidth = 256,
	.qos.qos_port = 30,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.urg_fwd_en = true,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = qxm_venus0_snoc_links,
};

static const u16 qxm_venus_cpu_links[] = {
	SM6375_SLAVE_VIDEO_PROC_SNOC,
};

static struct qcom_icc_node qxm_venus_cpu = {
	.name = "qxm_venus_cpu",
	.id = SM6375_MASTER_VIDEO_PROC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = qxm_venus_cpu_links,
};

static const u16 qxm_venus_cpu_snoc_links[] = {
	SM6375_SLAVE_SNOC_NRT,
};

static struct qcom_icc_node qxm_venus_cpu_snoc = {
	.name = "qxm_venus_cpu_snoc",
	.id = SM6375_MASTER_VIDEO_PROC_SNOC,
	.buswidth = 256,
	.qos.qos_port = 34,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = qxm_venus_cpu_snoc_links,
};

static const u16 mas_snoc_cfg_links[] = {
	SM6375_SLAVE_SERVICE_SNOC,
};

static struct qcom_icc_node mas_snoc_cfg = {
	.name = "mas_snoc_cfg",
	.id = SM6375_MASTER_SNOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = mas_snoc_cfg_links,
};


static const u16 qhm_tic_links[] = {
	SM6375_SLAVE_APPSS,
	SM6375_SLAVE_SNOC_CNOC,
	SM6375_SLAVE_OCIMEM,
	SM6375_SLAVE_PIMEM,
	SM6375_SLAVE_SNOC_BIMC,
	SM6375_SLAVE_QDSS_STM,
	SM6375_SLAVE_TCU,
};

static struct qcom_icc_node qhm_tic = {
	.name = "qhm_tic",
	.id = SM6375_MASTER_TIC,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 7,
	.links = qhm_tic_links,
};

static const u16 mas_a1noc_snoc_links[] = {
	SM6375_SLAVE_APPSS,
	SM6375_SLAVE_SNOC_CNOC,
	SM6375_SLAVE_OCIMEM,
	SM6375_SLAVE_PIMEM,
	SM6375_SLAVE_SNOC_BIMC,
	SM6375_SLAVE_QDSS_STM,
};

static struct qcom_icc_node mas_a1noc_snoc = {
	.name = "mas_a1noc_snoc",
	.id = SM6375_MASTER_A1NOC_SNOC,
	.buswidth = 8,
	.mas_rpm_id = 111,
	.slv_rpm_id = -1,
	.num_links = 6,
	.links = mas_a1noc_snoc_links,
};


static const u16 mas_a2noc_snoc_links[] = {
	SM6375_SLAVE_APPSS,
	SM6375_SLAVE_SNOC_CNOC,
	SM6375_SLAVE_OCIMEM,
	SM6375_SLAVE_PIMEM,
	SM6375_SLAVE_SNOC_BIMC,
	SM6375_SLAVE_QDSS_STM,
	SM6375_SLAVE_TCU,
};

static struct qcom_icc_node mas_a2noc_snoc = {
	.name = "mas_a2noc_snoc",
	.id = SM6375_MASTER_A2NOC_SNOC,
	.buswidth = 16,
	.mas_rpm_id = 112,
	.slv_rpm_id = -1,
	.num_links = 7,
	.links = mas_a2noc_snoc_links,
};


static const u16 mas_bimc_snoc_links[] = {
	SM6375_SLAVE_APPSS,
	SM6375_SLAVE_SNOC_CNOC,
	SM6375_SLAVE_OCIMEM,
	SM6375_SLAVE_PIMEM,
	SM6375_SLAVE_QDSS_STM,
	SM6375_SLAVE_TCU,
};

static struct qcom_icc_node mas_bimc_snoc = {
	.name = "mas_bimc_snoc",
	.id = SM6375_MASTER_BIMC_SNOC,
	.buswidth = 8,
	.qos.qos_port = 29,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 21,
	.slv_rpm_id = -1,
	.num_links = 6,
	.links = mas_bimc_snoc_links,
};

static const u16 qxm_pimem_links[] = {
	SM6375_SLAVE_OCIMEM,
	SM6375_SLAVE_SNOC_BIMC,
};

static struct qcom_icc_node qxm_pimem = {
	.name = "qxm_pimem",
	.id = SM6375_MASTER_PIMEM,
	.buswidth = 8,
	.qos.qos_port = 41,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 2,
	.links = qxm_pimem_links,
};

static const u16 links_to_slave_a1noc_snoc[] = {
	SM6375_SLAVE_A1NOC_SNOC,
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.id = SM6375_MASTER_QUP_0,
	.buswidth = 4,
	.qos.qos_port = 21,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 166,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = links_to_slave_a1noc_snoc,
};

static struct qcom_icc_node qhm_qup1 = {
	.name = "qhm_qup1",
	.id = SM6375_MASTER_QUP_1,
	.buswidth = 4,
	.qos.qos_port = 22,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 41,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = links_to_slave_a1noc_snoc,
};

static struct qcom_icc_node xm_emmc = {
	.name = "xm_emmc",
	.id = SM6375_MASTER_EMMC,
	.buswidth = 8,
	.qos.qos_port = 38,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = links_to_slave_a1noc_snoc,
};

static struct qcom_icc_node xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SM6375_MASTER_SDCC_2,
	.buswidth = 8,
	.qos.qos_port = 44,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 35,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = links_to_slave_a1noc_snoc,
};

static struct qcom_icc_node xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = SM6375_MASTER_UFS_MEM,
	.buswidth = 8,
	.qos.qos_port = 46,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = links_to_slave_a1noc_snoc,
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SM6375_MASTER_USB3_0,
	.buswidth = 8,
	.qos.qos_port = 45,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = links_to_slave_a1noc_snoc,
};

static const u16 links_to_slave_a2noc_snoc[] = {
	SM6375_SLAVE_A2NOC_SNOC,
};

static struct qcom_icc_node mas_crypto_c0 = {
	.name = "mas_crypto_c0",
	.id = SM6375_MASTER_CRYPTO_CORE0,
	.buswidth = 8,
	.qos.qos_port = 43,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 23,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = links_to_slave_a2noc_snoc,
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SM6375_MASTER_QDSS_BAM,
	.buswidth = 4,
	.qos.qos_port = 23,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = links_to_slave_a2noc_snoc,
};

static struct qcom_icc_node qxm_ipa = {
	.name = "qxm_ipa",
	.id = SM6375_MASTER_IPA,
	.buswidth = 8,
	.qos.qos_port = 24,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = 59,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = links_to_slave_a2noc_snoc,
};

static struct qcom_icc_node xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.id = SM6375_MASTER_QDSS_ETR,
	.buswidth = 8,
	.qos.qos_port = 33,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = links_to_slave_a2noc_snoc,
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.id = SM6375_SLAVE_EBI,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 0,
};

static const u16 slv_bimc_snoc_links[] = {
	SM6375_MASTER_BIMC_SNOC,
};

static struct qcom_icc_node slv_bimc_snoc = {
	.name = "slv_bimc_snoc",
	.id = SM6375_SLAVE_BIMC_SNOC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 2,
	.num_links = 1,
	.links = slv_bimc_snoc_links,
};

static struct qcom_icc_node qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = SM6375_SLAVE_QUP_CORE_0,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qup1_core_slave = {
	.name = "qup1_core_slave",
	.id = SM6375_SLAVE_QUP_CORE_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_bimc_cfg = {
	.name = "qhs_bimc_cfg",
	.id = SM6375_SLAVE_BIMC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_camera_nrt_throttle_cfg = {
	.name = "qhs_camera_nrt_throttle_cfg",
	.id = SM6375_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_camera_rt_throttle_cfg = {
	.name = "qhs_camera_rt_throttle_cfg",
	.id = SM6375_SLAVE_CAMERA_RT_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_camera_ss_cfg = {
	.name = "qhs_camera_ss_cfg",
	.id = SM6375_SLAVE_CAMERA_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SM6375_SLAVE_CLK_CTL,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_compute_dsp_cfg = {
	.name = "qhs_compute_dsp_cfg",
	.id = SM6375_SLAVE_DSP_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SM6375_SLAVE_RBCPR_CX_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = SM6375_SLAVE_RBCPR_MX_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SM6375_SLAVE_CRYPTO_0_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_dcc_cfg = {
	.name = "qhs_dcc_cfg",
	.id = SM6375_SLAVE_DCC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_ddr_phy_cfg = {
	.name = "qhs_ddr_phy_cfg",
	.id = SM6375_SLAVE_DDR_PHY_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_ddr_ss_cfg = {
	.name = "qhs_ddr_ss_cfg",
	.id = SM6375_SLAVE_DDR_SS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_disp_ss_cfg = {
	.name = "qhs_disp_ss_cfg",
	.id = SM6375_SLAVE_DISPLAY_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_display_throttle_cfg = {
	.name = "qhs_display_throttle_cfg",
	.id = SM6375_SLAVE_DISPLAY_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_emmc_cfg = {
	.name = "qhs_emmc_cfg",
	.id = SM6375_SLAVE_EMMC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_gpuss_cfg = {
	.name = "qhs_gpuss_cfg",
	.id = SM6375_SLAVE_GRAPHICS_3D_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_hw_km = {
	.name = "qhs_hw_km",
	.id = SM6375_SLAVE_HWKM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SM6375_SLAVE_IMEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_ipa_cfg = {
	.name = "qhs_ipa_cfg",
	.id = SM6375_SLAVE_IPA_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_lpass = {
	.name = "qhs_lpass",
	.id = SM6375_SLAVE_LPASS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_mapss = {
	.name = "qhs_mapss",
	.id = SM6375_SLAVE_MAPSS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_mesg_ram = {
	.name = "qhs_mesg_ram",
	.id = SM6375_SLAVE_MESSAGE_RAM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_pdm = {
	.name = "qhs_pdm",
	.id = SM6375_SLAVE_PDM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SM6375_SLAVE_PIMEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_pka_wrapper = {
	.name = "qhs_pka_wrapper",
	.id = SM6375_SLAVE_PKA_CORE,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_pmic_arb = {
	.name = "qhs_pmic_arb",
	.id = SM6375_SLAVE_PMIC_ARB,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SM6375_SLAVE_QDSS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_qm_cfg = {
	.name = "qhs_qm_cfg",
	.id = SM6375_SLAVE_QM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_qm_mpu_cfg = {
	.name = "qhs_qm_mpu_cfg",
	.id = SM6375_SLAVE_QM_MPU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SM6375_SLAVE_QUP_0,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_qup1 = {
	.name = "qhs_qup1",
	.id = SM6375_SLAVE_QUP_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_rpm = {
	.name = "qhs_rpm",
	.id = SM6375_SLAVE_RPM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SM6375_SLAVE_SDCC_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_security = {
	.name = "qhs_security",
	.id = SM6375_SLAVE_SECURITY,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 slv_snoc_cfg_links[] = {
	SM6375_MASTER_SNOC_CFG,
};

static struct qcom_icc_node slv_snoc_cfg = {
	.name = "slv_snoc_cfg",
	.id = SM6375_SLAVE_SNOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = slv_snoc_cfg_links,
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SM6375_SLAVE_TCSR,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = SM6375_SLAVE_TLMM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SM6375_SLAVE_UFS_MEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_usb3_0 = {
	.name = "qhs_usb3_0",
	.id = SM6375_SLAVE_USB3,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SM6375_SLAVE_VENUS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_venus_throttle_cfg = {
	.name = "qhs_venus_throttle_cfg",
	.id = SM6375_SLAVE_VENUS_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SM6375_SLAVE_VSENSE_CTRL_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 slv_camera_nrt_snoc_links[] = {
	SM6375_MASTER_CAMNOC_SF_SNOC,
};

static struct qcom_icc_node slv_camera_nrt_snoc = {
	.name = "slv_camera_nrt_snoc",
	.id = SM6375_SLAVE_CAMNOC_SF_SNOC,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = slv_camera_nrt_snoc_links,
};

static const u16 slv_venus0_snoc_links[] = {
	SM6375_MASTER_VIDEO_P0_SNOC,
};

static struct qcom_icc_node slv_venus0_snoc = {
	.name = "slv_venus0_snoc",
	.id = SM6375_SLAVE_VIDEO_P0_SNOC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = slv_venus0_snoc_links,
};

static const u16 slv_venus_cpu_snoc_links[] = {
	SM6375_MASTER_VIDEO_PROC_SNOC,
};

static struct qcom_icc_node slv_venus_cpu_snoc = {
	.name = "slv_venus_cpu_snoc",
	.id = SM6375_SLAVE_VIDEO_PROC_SNOC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = slv_venus_cpu_snoc_links,
};

static const u16 slv_snoc_nrt_links[] = {
	SM6375_MASTER_SNOC_NRT
};

static struct qcom_icc_node slv_snoc_nrt = {
	.name = "slv_snoc_nrt",
	.id = SM6375_SLAVE_SNOC_NRT,
	.buswidth = 256,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = slv_snoc_nrt_links,
};

static const u16 slv_snoc_bimc_nrt_links[] = {
	SM6375_MASTER_SNOC_BIMC_NRT
};

static struct qcom_icc_node slv_snoc_bimc_nrt = {
	.name = "slv_snoc_bimc_nrt",
	.id = SM6375_SLAVE_SNOC_BIMC_NRT,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = slv_snoc_bimc_nrt_links,
};

static const u16 slv_camera_rt_snoc_links[] = {
	SM6375_MASTER_CAMNOC_HF_SNOC
};

static struct qcom_icc_node slv_camera_rt_snoc = {
	.name = "slv_camera_rt_snoc",
	.id = SM6375_SLAVE_CAMNOC_HF_SNOC,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = slv_camera_rt_snoc_links,
};

static const u16 slv_mdp0_snoc_links[] = {
	SM6375_MASTER_MDP_PORT0_SNOC
};

static struct qcom_icc_node slv_mdp0_snoc = {
	.name = "slv_mdp0_snoc",
	.id = SM6375_SLAVE_MDP_PORT0_SNOC,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = slv_mdp0_snoc_links,
};

static const u16 slv_snoc_rt_links[] = {
	SM6375_MASTER_SNOC_RT
};

static struct qcom_icc_node slv_snoc_rt = {
	.name = "slv_snoc_rt",
	.id = SM6375_SLAVE_SNOC_RT,
	.buswidth = 256,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = slv_snoc_rt_links,
};

static const u16 slv_snoc_bimc_rt_links[] = {
	SM6375_MASTER_SNOC_BIMC_RT
};

static struct qcom_icc_node slv_snoc_bimc_rt = {
	.name = "slv_snoc_bimc_rt",
	.id = SM6375_SLAVE_SNOC_BIMC_RT,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 1,
	.links = slv_snoc_bimc_rt_links,
};

static struct qcom_icc_node qhs_apss = {
	.name = "qhs_apss",
	.id = SM6375_SLAVE_APPSS,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 slv_snoc_cnoc_links[] = {
	SM6375_MASTER_SNOC_CNOC
};

static struct qcom_icc_node slv_snoc_cnoc = {
	.name = "slv_snoc_cnoc",
	.id = SM6375_SLAVE_SNOC_CNOC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 25,
	.num_links = 1,
	.links = slv_snoc_cnoc_links,
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.id = SM6375_SLAVE_OCIMEM,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 26,
};

static struct qcom_icc_node qxs_pimem = {
	.name = "qxs_pimem",
	.id = SM6375_SLAVE_PIMEM,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 slv_snoc_bimc_links[] = {
	SM6375_MASTER_SNOC_BIMC,
};

static struct qcom_icc_node slv_snoc_bimc = {
	.name = "slv_snoc_bimc",
	.id = SM6375_SLAVE_SNOC_BIMC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 24,
	.num_links = 1,
	.links = slv_snoc_bimc_links,
};

static struct qcom_icc_node srvc_snoc = {
	.name = "srvc_snoc",
	.id = SM6375_SLAVE_SERVICE_SNOC,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SM6375_SLAVE_QDSS_STM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 30,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SM6375_SLAVE_TCU,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
};

static const u16 slv_a1noc_snoc_links[] = {
	SM6375_MASTER_A1NOC_SNOC,
};

static struct qcom_icc_node slv_a1noc_snoc = {
	.name = "slv_a1noc_snoc",
	.id = SM6375_SLAVE_A1NOC_SNOC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 142,
	.num_links = 1,
	.links = slv_a1noc_snoc_links,
};

static const u16 slv_a2noc_snoc_links[] = {
	SM6375_MASTER_A2NOC_SNOC,
};

static struct qcom_icc_node slv_a2noc_snoc = {
	.name = "slv_a2noc_snoc",
	.id = SM6375_SLAVE_A2NOC_SNOC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 143,
	.num_links = 1,
	.links = slv_a2noc_snoc_links,
};

static struct qcom_icc_node *sm6375_bimc_nodes[] = {
	[MASTER_AMPSS_M0] = &apps_proc,
	[MASTER_SNOC_BIMC_RT] = &mas_snoc_bimc_rt,
	[MASTER_SNOC_BIMC_NRT] = &mas_snoc_bimc_nrt,
	[SNOC_BIMC_MAS] = &mas_snoc_bimc,
	[MASTER_GRAPHICS_3D] = &qnm_gpu,
	[MASTER_CDSP_PROC] = &qnm_cdsp,
	[MASTER_TCU_0] = &tcu_0,
	[SLAVE_EBI] = &ebi,
	[BIMC_SNOC_SLV] = &slv_bimc_snoc,
};

static const struct regmap_config sm6375_bimc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x80000,
	.fast_io	= true,
};

static struct qcom_icc_desc sm6375_bimc = {
	.type = QCOM_ICC_BIMC,
	.nodes = sm6375_bimc_nodes,
	.num_nodes = ARRAY_SIZE(sm6375_bimc_nodes),
	.regmap_cfg = &sm6375_bimc_regmap_config,
};

static struct qcom_icc_node *sm6375_config_noc_nodes[] = {
	[MASTER_SNOC_CNOC] = &mas_snoc_cnoc,
	[MASTER_QDSS_DAP] = &xm_qdss_dap,
	[SLAVE_BIMC_CFG] = &qhs_bimc_cfg,
	[SLAVE_CAMERA_NRT_THROTTLE_CFG] = &qhs_camera_nrt_throttle_cfg,
	[SLAVE_CAMERA_RT_THROTTLE_CFG] = &qhs_camera_rt_throttle_cfg,
	[SLAVE_CAMERA_CFG] = &qhs_camera_ss_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_DSP_CFG] = &qhs_compute_dsp_cfg,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_DCC_CFG] = &qhs_dcc_cfg,
	[SLAVE_DDR_PHY_CFG] = &qhs_ddr_phy_cfg,
	[SLAVE_DDR_SS_CFG] = &qhs_ddr_ss_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_disp_ss_cfg,
	[SLAVE_DISPLAY_THROTTLE_CFG] = &qhs_display_throttle_cfg,
	[SLAVE_EMMC_CFG] = &qhs_emmc_cfg,
	[SLAVE_GRAPHICS_3D_CFG] = &qhs_gpuss_cfg,
	[SLAVE_HWKM] = &qhs_hw_km,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa_cfg,
	[SLAVE_LPASS] = &qhs_lpass,
	[SLAVE_MAPSS] = &qhs_mapss,
	[SLAVE_MESSAGE_RAM] = &qhs_mesg_ram,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PKA_CORE] = &qhs_pka_wrapper,
	[SLAVE_PMIC_ARB] = &qhs_pmic_arb,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QM_CFG] = &qhs_qm_cfg,
	[SLAVE_QM_MPU_CFG] = &qhs_qm_mpu_cfg,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_QUP_1] = &qhs_qup1,
	[SLAVE_RPM] = &qhs_rpm,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SECURITY] = &qhs_security,
	[SLAVE_SNOC_CFG] = &slv_snoc_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3] = &qhs_usb3_0,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &qhs_venus_throttle_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
};

static const struct regmap_config sm6375_cnoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x6200,
	.fast_io	= true,
};

static struct qcom_icc_desc sm6375_config_noc = {
	.type = QCOM_ICC_QNOC,
	.nodes = sm6375_config_noc_nodes,
	.num_nodes = ARRAY_SIZE(sm6375_config_noc_nodes),
	.regmap_cfg = &sm6375_cnoc_regmap_config
};

static struct qcom_icc_node *sm6375_sys_noc_nodes[] = {
	[MASTER_SNOC_CFG] = &mas_snoc_cfg,
	[MASTER_TIC] = &qhm_tic,
	[A1NOC_SNOC_MAS] = &mas_a1noc_snoc,
	[A2NOC_SNOC_MAS] = &mas_a2noc_snoc,
	[BIMC_SNOC_MAS] = &mas_bimc_snoc,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_QUP_1] = &qhm_qup1,
	[MASTER_EMMC] = &xm_emmc,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_CRYPTO_CORE0] = &mas_crypto_c0,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_USB3_0] = &xm_usb3_0,
	[MASTER_CAMNOC_SF_SNOC] = &qnm_camera_nrt_snoc,
	[MASTER_CAMNOC_HF_SNOC] = &qnm_camera_rt_snoc,
	[MASTER_MDP_PORT0_SNOC] = &qxm_mdp0_snoc,
	[MASTER_VIDEO_P0_SNOC] = &qxm_venus0_snoc,
	[MASTER_VIDEO_PROC_SNOC] = &qxm_venus_cpu_snoc,
	[SLAVE_APPSS] = &qhs_apss,
	[SNOC_CNOC_SLV] = &slv_snoc_cnoc,
	[SLAVE_OCIMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SNOC_BIMC_SLV] = &slv_snoc_bimc,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
	[A1NOC_SNOC_SLV] = &slv_a1noc_snoc,
	[A2NOC_SNOC_SLV] = &slv_a2noc_snoc,
	[SLAVE_SNOC_RT] = &slv_snoc_rt,
	[SLAVE_SNOC_NRT] = &slv_snoc_nrt,
};

static const struct regmap_config sm6375_sys_noc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x5f080,
	.fast_io	= true,
};

static struct qcom_icc_desc sm6375_sys_noc = {
	.type = QCOM_ICC_QNOC,
	.nodes = sm6375_sys_noc_nodes,
	.num_nodes = ARRAY_SIZE(sm6375_sys_noc_nodes),
	.regmap_cfg = &sm6375_sys_noc_regmap_config,
};

static struct qcom_icc_node *sm6375_clk_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_QUP_CORE_1] = &qup1_core_master,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_QUP_CORE_1] = &qup1_core_slave,
};

static struct qcom_icc_desc sm6375_clk_virt = {
	.type = QCOM_ICC_QNOC,
	.nodes = sm6375_clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(sm6375_clk_virt_nodes),
	.regmap_cfg = &sm6375_sys_noc_regmap_config,
};

static struct qcom_icc_node *sm6375_mmrt_virt_nodes[] = {
	[MASTER_CAMNOC_HF] = &qnm_camera_rt,
	[MASTER_MDP_PORT0] = &qxm_mdp0,
	[MASTER_SNOC_RT] = &mas_snoc_rt,
	[SLAVE_SNOC_BIMC_RT] = &slv_snoc_bimc_rt,
	[SLAVE_CAMNOC_HF_SNOC] = &slv_camera_rt_snoc,
	[SLAVE_MDP_PORT0_SNOC] = &slv_mdp0_snoc,
};

static struct qcom_icc_desc sm6375_mmrt_virt = {
	.type = QCOM_ICC_QNOC,
	.nodes = sm6375_mmrt_virt_nodes,
	.num_nodes = ARRAY_SIZE(sm6375_mmrt_virt_nodes),
	.regmap_cfg = &sm6375_sys_noc_regmap_config,
};

static struct qcom_icc_node *sm6375_mmnrt_virt_nodes[] = {
	[MASTER_CAMNOC_SF] = &qnm_camera_nrt,
	[MASTER_VIDEO_P0] = &qxm_venus0,
	[MASTER_VIDEO_PROC] = &qxm_venus_cpu,
	[MASTER_SNOC_NRT] = &mas_snoc_nrt,
	[SLAVE_SNOC_BIMC_NRT] = &slv_snoc_bimc_nrt,
	[SLAVE_CAMNOC_SF_SNOC] = &slv_camera_nrt_snoc,
	[SLAVE_VIDEO_P0_SNOC] = &slv_venus0_snoc,
	[SLAVE_VIDEO_PROC_SNOC] = &slv_venus_cpu_snoc,
};

static struct qcom_icc_desc sm6375_mmnrt_virt = {
	.type = QCOM_ICC_QNOC,
	.nodes = sm6375_mmnrt_virt_nodes,
	.num_nodes = ARRAY_SIZE(sm6375_mmnrt_virt_nodes),
	.regmap_cfg = &sm6375_sys_noc_regmap_config,
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sm6375-bimc", .data = &sm6375_bimc },
	{ .compatible = "qcom,sm6375-clk-virt", .data = &sm6375_clk_virt },
	{ .compatible = "qcom,sm6375-cnoc", .data = &sm6375_config_noc },
	{ .compatible = "qcom,sm6375-mmrt-virt", .data = &sm6375_mmrt_virt },
	{ .compatible = "qcom,sm6375-mmnrt-virt", .data = &sm6375_mmnrt_virt },
	{ .compatible = "qcom,sm6375-snoc", .data = &sm6375_sys_noc },
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver sm6375_noc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-sm6375",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(sm6375_noc_driver)

MODULE_DESCRIPTION("SM6375 NoC driver");
MODULE_LICENSE("GPL");
