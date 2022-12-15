// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Linaro Limited
 */

#include <dt-bindings/interconnect/qcom,sm6115.h>
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
#include "sm6115.h"

static const char * const snocc_clocks[] = {
	"bus",
	"bus_a",
	"bus_periph",
	"bus_periph_a",
	"bus_lpass",
	"bus_lpass_a",
};

static const u16 apps_proc_links[] = {
	SM6115_SLAVE_EBI_CH0,
	SM6115_BIMC_SNOC_SLV
};

static struct qcom_icc_node apps_proc = {
	.name = "apps_proc",
	.id = SM6115_MASTER_AMPSS_M0,
	.buswidth = 16,
	.mas_rpm_id = 0,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(apps_proc_links),
	.links = apps_proc_links
};

static const u16 mas_snoc_bimc_rt_links[] = {
	SM6115_SLAVE_EBI_CH0,
};

static struct qcom_icc_node mas_snoc_bimc_rt = {
	.name = "snoc_bimc_rt",
	.id = SM6115_MASTER_SNOC_BIMC_RT,
	.buswidth = 16,
	.mas_rpm_id = 163,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(mas_snoc_bimc_rt_links),
	.links = mas_snoc_bimc_rt_links
};

static const u16 mas_snoc_bimc_nrt_links[] = {
	SM6115_SLAVE_EBI_CH0
};

static struct qcom_icc_node mas_snoc_bimc_nrt = {
	.name = "snoc_bimc_nrt",
	.id = SM6115_MASTER_SNOC_BIMC_NRT,
	.buswidth = 16,
	.mas_rpm_id = 164,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 3,
	.num_links = ARRAY_SIZE(mas_snoc_bimc_nrt_links),
	.links = mas_snoc_bimc_nrt_links
};

static const u16 mas_snoc_bimc_links[] = {
	SM6115_SLAVE_EBI_CH0
};

static struct qcom_icc_node mas_snoc_bimc = {
	.name = "snoc_bimc",
	.id = SM6115_SNOC_BIMC_MAS,
	.buswidth = 16,
	.mas_rpm_id = 3,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 6,
	.num_links = ARRAY_SIZE(mas_snoc_bimc_links),
	.links = mas_snoc_bimc_links
};

static const u16 mas_gpu_cdsp_bimc_links[] = {
	SM6115_SLAVE_EBI_CH0,
	SM6115_BIMC_SNOC_SLV
};

static struct qcom_icc_node mas_gpu_cdsp_bimc = {
	.name = "gpu_cdsp_bimc",
	.id = SM6115_MASTER_GPU_CDSP_PROC,
	.buswidth = 32,
	.mas_rpm_id = 165,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_BYPASS,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 1,
	.num_links = ARRAY_SIZE(mas_gpu_cdsp_bimc_links),
	.links = mas_gpu_cdsp_bimc_links
};

static const u16 tcu_0_links[] = {
	SM6115_BIMC_SNOC_SLV,
	SM6115_SLAVE_EBI_CH0,
};

static struct qcom_icc_node tcu_0 = {
	.name = "tcu_0",
	.id = SM6115_MASTER_TCU_0,
	.buswidth = 8,
	.mas_rpm_id = 102,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 4,
	.num_links = ARRAY_SIZE(tcu_0_links),
	.links = tcu_0_links
};

static const u16 mas_snoc_cnoc_links[] = {
	SM6115_SLAVE_SDCC_1,
	SM6115_SLAVE_SDCC_2,
	SM6115_SLAVE_DISPLAY_CFG,
	SM6115_SLAVE_PRNG,
	SM6115_SLAVE_GPU_CFG,
	SM6115_SLAVE_TLMM_SOUTH,
	SM6115_SLAVE_TLMM_EAST,
	SM6115_SLAVE_LPASS,
	SM6115_SLAVE_CAMERA_CFG,
	SM6115_SLAVE_PIMEM_CFG,
	SM6115_SLAVE_SNOC_CFG,
	SM6115_SLAVE_VENUS_CFG,
	SM6115_SLAVE_DISPLAY_THROTTLE_CFG,
	SM6115_SLAVE_IMEM_CFG,
	SM6115_SLAVE_QUP_0,
	SM6115_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	SM6115_SLAVE_UFS_MEM_CFG,
	SM6115_SLAVE_IPA_CFG,
	SM6115_SLAVE_USB3,
	SM6115_SLAVE_TCSR,
	SM6115_SLAVE_QM_MPU_CFG,
	SM6115_SLAVE_CAMERA_RT_THROTTLE_CFG,
	SM6115_SLAVE_QDSS_CFG,
	SM6115_SLAVE_MESSAGE_RAM,
	SM6115_SLAVE_CRYPTO_0_CFG,
	SM6115_SLAVE_CDSP_THROTTLE_CFG,
	SM6115_SLAVE_TLMM_WEST,
	SM6115_SLAVE_VSENSE_CTRL_CFG,
	SM6115_SLAVE_SERVICE_CNOC,
	SM6115_SLAVE_QM_CFG,
	SM6115_SLAVE_BIMC_CFG,
	SM6115_SLAVE_PDM,
	SM6115_SLAVE_PMIC_ARB,
	SM6115_SLAVE_CLK_CTL,
	SM6115_SLAVE_VENUS_THROTTLE_CFG
};

static struct qcom_icc_node mas_snoc_cnoc = {
	.name = "snoc_cnoc",
	.id = SM6115_SNOC_CNOC_MAS,
	.buswidth = 8,
	.mas_rpm_id = 52,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(mas_snoc_cnoc_links),
	.links = mas_snoc_cnoc_links
};

static const u16 xm_dap_links[] = {
	SM6115_SLAVE_SDCC_1,
	SM6115_SLAVE_SDCC_2,
	SM6115_SLAVE_DISPLAY_CFG,
	SM6115_SLAVE_PRNG,
	SM6115_SLAVE_GPU_CFG,
	SM6115_SLAVE_TLMM_SOUTH,
	SM6115_SLAVE_TLMM_EAST,
	SM6115_SLAVE_LPASS,
	SM6115_SLAVE_CAMERA_CFG,
	SM6115_SLAVE_PIMEM_CFG,
	SM6115_SLAVE_SNOC_CFG,
	SM6115_SLAVE_VENUS_CFG,
	SM6115_SLAVE_DISPLAY_THROTTLE_CFG,
	SM6115_SLAVE_IMEM_CFG,
	SM6115_SLAVE_QUP_0,
	SM6115_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	SM6115_SLAVE_UFS_MEM_CFG,
	SM6115_SLAVE_IPA_CFG,
	SM6115_SLAVE_USB3,
	SM6115_SLAVE_TCSR,
	SM6115_SLAVE_QM_MPU_CFG,
	SM6115_SLAVE_CAMERA_RT_THROTTLE_CFG,
	SM6115_SLAVE_QDSS_CFG,
	SM6115_SLAVE_MESSAGE_RAM,
	SM6115_SLAVE_CRYPTO_0_CFG,
	SM6115_SLAVE_CDSP_THROTTLE_CFG,
	SM6115_SLAVE_TLMM_WEST,
	SM6115_SLAVE_VSENSE_CTRL_CFG,
	SM6115_SLAVE_SERVICE_CNOC,
	SM6115_SLAVE_QM_CFG,
	SM6115_SLAVE_BIMC_CFG,
	SM6115_SLAVE_PDM,
	SM6115_SLAVE_PMIC_ARB,
	SM6115_SLAVE_CLK_CTL,
	SM6115_SLAVE_VENUS_THROTTLE_CFG
};

static struct qcom_icc_node xm_dap = {
	.name = "xm_dap",
	.id = SM6115_MASTER_QDSS_DAP,
	.buswidth = 8,
	.mas_rpm_id = 49,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(xm_dap_links),
	.links = xm_dap_links
};

static const u16 crypto_c0_links[] = {
	SM6115_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node crypto_c0 = {
	.name = "crypto_c0",
	.id = SM6115_MASTER_CRYPTO_CORE0,
	.buswidth = 8,
	.mas_rpm_id = 23,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 0,
	.qos.qos_port = 22,
	.num_links = ARRAY_SIZE(crypto_c0_links),
	.links = crypto_c0_links
};

static const u16 qup_core_master_0_links[] = {
	SM6115_SLAVE_QUP_CORE_0,
};

static struct qcom_icc_node qup_core_master_0 = {
	.name = "qup_core_master_0",
	.id = SM6115_MASTER_QUP_CORE_0,
	.buswidth = 4,
	.mas_rpm_id = 170,
	.slv_rpm_id = -1,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(qup_core_master_0_links),
	.links = qup_core_master_0_links
};

static const u16 mas_snoc_cfg_links[] = {
	SM6115_SLAVE_SERVICE_SNOC,
};

static struct qcom_icc_node mas_snoc_cfg = {
	.name = "snoc_cfg",
	.id = SM6115_MASTER_SNOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = 20,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(mas_snoc_cfg_links),
	.links = mas_snoc_cfg_links
};

static const u16 qhm_tic_links[] = {
	SM6115_SNOC_BIMC_SLV,
	SM6115_SNOC_CNOC_SLV,
	SM6115_SLAVE_QDSS_STM,
	SM6115_SLAVE_PIMEM,
	SM6115_SLAVE_OCIMEM,
	SM6115_SLAVE_TCU,
	SM6115_SLAVE_APPSS
};

static struct qcom_icc_node qhm_tic = {
	.name = "qhm_tic",
	.id = SM6115_MASTER_TIC,
	.buswidth = 4,
	.mas_rpm_id = 51,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(qhm_tic_links),
	.links = qhm_tic_links
};

static const u16 mas_anoc_snoc_links[] = {
	SM6115_SNOC_BIMC_SLV,
	SM6115_SNOC_CNOC_SLV,
	SM6115_SLAVE_QDSS_STM,
	SM6115_SLAVE_PIMEM,
	SM6115_SLAVE_OCIMEM,
	SM6115_SLAVE_TCU,
	SM6115_SLAVE_APPSS
};

static struct qcom_icc_node mas_anoc_snoc = {
	.name = "anoc_snoc",
	.id = SM6115_MASTER_ANOC_SNOC,
	.buswidth = 16,
	.mas_rpm_id = 110,
	.slv_rpm_id = -1,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(mas_anoc_snoc_links),
	.links = mas_anoc_snoc_links
};

static const u16 qnm_camera_nrt_links[] = {
	SM6115_SLAVE_SNOC_BIMC_NRT
};

static struct qcom_icc_node qnm_camera_nrt = {
	.name = "qnm_camera_nrt",
	.id = SM6115_MASTER_CAMNOC_SF,
	.buswidth = 32,
	.mas_rpm_id = 172,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 3,
	.qos.prio_level = 0,
	.qos.qos_port = 4,
	.num_links = ARRAY_SIZE(qnm_camera_nrt_links),
	.links = qnm_camera_nrt_links
};

static const u16 qnm_camera_rt_links[] = {
	SM6115_SLAVE_SNOC_BIMC_RT
};

static struct qcom_icc_node qnm_camera_rt = {
	.name = "qnm_camera_rt",
	.id = SM6115_MASTER_CAMNOC_HF,
	.buswidth = 32,
	.mas_rpm_id = 173,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 0,
	.qos.qos_port = 10,
	.num_links = ARRAY_SIZE(qnm_camera_rt_links),
	.links = qnm_camera_rt_links
};

static const u16 mas_bimc_snoc_links[] = {
	SM6115_SNOC_CNOC_SLV,
	SM6115_SLAVE_QDSS_STM,
	SM6115_SLAVE_PIMEM,
	SM6115_SLAVE_OCIMEM,
	SM6115_SLAVE_TCU,
	SM6115_SLAVE_APPSS
};

static struct qcom_icc_node mas_bimc_snoc = {
	.name = "bimc_snoc",
	.id = SM6115_BIMC_SNOC_MAS,
	.buswidth = 8,
	.mas_rpm_id = 21,
	.slv_rpm_id = -1,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(mas_bimc_snoc_links),
	.links = mas_bimc_snoc_links
};

static const u16 qxm_mdp0_links[] = {
	SM6115_SLAVE_SNOC_BIMC_RT
};

static struct qcom_icc_node qxm_mdp0 = {
	.name = "qxm_mdp0",
	.id = SM6115_MASTER_MDP_PORT0,
	.buswidth = 16,
	.mas_rpm_id = 8,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 0,
	.qos.qos_port = 5,
	.num_links = ARRAY_SIZE(qxm_mdp0_links),
	.links = qxm_mdp0_links
};

static const u16 qxm_pimem_links[] = {
	SM6115_SLAVE_OCIMEM,
	SM6115_SNOC_BIMC_SLV,
};

static struct qcom_icc_node qxm_pimem = {
	.name = "qxm_pimem",
	.id = SM6115_MASTER_PIMEM,
	.buswidth = 8,
	.mas_rpm_id = 113,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 0,
	.qos.qos_port = 20,
	.num_links = ARRAY_SIZE(qxm_pimem_links),
	.links = qxm_pimem_links
};

static const u16 qxm_venus0_links[] = {
	SM6115_SLAVE_SNOC_BIMC_NRT,
};

static struct qcom_icc_node qxm_venus0 = {
	.name = "qxm_venus0",
	.id = SM6115_MASTER_VIDEO_P0,
	.buswidth = 16,
	.mas_rpm_id = 9,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 0,
	.qos.qos_port = 9,
	.num_links = ARRAY_SIZE(qxm_venus0_links),
	.links = qxm_venus0_links
};

static const u16 qxm_venus_cpu_links[] = {
	SM6115_SLAVE_SNOC_BIMC_NRT
};

static struct qcom_icc_node qxm_venus_cpu = {
	.name = "qxm_venus_cpu",
	.id = SM6115_MASTER_VIDEO_PROC,
	.buswidth = 8,
	.mas_rpm_id = 168,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 0,
	.qos.qos_port = 13,
	.num_links = ARRAY_SIZE(qxm_venus_cpu_links),
	.links = qxm_venus_cpu_links
};

static const u16 qhm_qdss_bam_links[] = {
	SM6115_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SM6115_MASTER_QDSS_BAM,
	.buswidth = 4,
	.mas_rpm_id = 19,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 0,
	.qos.qos_port = 2,
	.num_links = ARRAY_SIZE(qhm_qdss_bam_links),
	.links = qhm_qdss_bam_links
};

static const u16 qhm_qup0_links[] = {
	SM6115_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.id = SM6115_MASTER_QUP_0,
	.buswidth = 4,
	.mas_rpm_id = 166,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 0,
	.qos.qos_port = 0,
	.num_links = ARRAY_SIZE(qhm_qup0_links),
	.links = qhm_qup0_links
};

static const u16 qxm_ipa_links[] = {
	SM6115_SLAVE_ANOC_SNOC
};

static struct qcom_icc_node qxm_ipa = {
	.name = "qxm_ipa",
	.id = SM6115_MASTER_IPA,
	.buswidth = 8,
	.mas_rpm_id = 59,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 0,
	.qos.qos_port = 3,
	.num_links = ARRAY_SIZE(qxm_ipa_links),
	.links = qxm_ipa_links
};

static const u16 xm_qdss_etr_links[] = {
	SM6115_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.id = SM6115_MASTER_QDSS_ETR,
	.buswidth = 8,
	.mas_rpm_id = 31,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 0,
	.qos.qos_port = 12,
	.num_links = ARRAY_SIZE(xm_qdss_etr_links),
	.links = xm_qdss_etr_links
};

static const u16 xm_sdc1_links[] = {
	SM6115_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node xm_sdc1 = {
	.name = "xm_sdc1",
	.id = SM6115_MASTER_SDCC_1,
	.buswidth = 8,
	.mas_rpm_id = 33,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 0,
	.qos.qos_port = 17,
	.num_links = ARRAY_SIZE(xm_sdc1_links),
	.links = xm_sdc1_links
};

static const u16 xm_sdc2_links[] = {
	SM6115_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SM6115_MASTER_SDCC_2,
	.buswidth = 8,
	.mas_rpm_id = 35,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 0,
	.qos.qos_port = 23,
	.num_links = ARRAY_SIZE(xm_sdc2_links),
	.links = xm_sdc2_links
};

static const u16 xm_ufs_mem_links[] = {
	SM6115_SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node xm_ufs_mem = {
	.name = "xm_ufs_mem",
	.id = SM6115_MASTER_UFS_MEM,
	.buswidth = 8,
	.mas_rpm_id = 167,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 0,
	.qos.qos_port = 25,
	.num_links = ARRAY_SIZE(xm_ufs_mem_links),
	.links = xm_ufs_mem_links
};

static const u16 xm_usb3_0_links[] = {
	SM6115_SLAVE_ANOC_SNOC
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SM6115_MASTER_USB3,
	.buswidth = 8,
	.mas_rpm_id = 32,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 0,
	.qos.qos_port = 24,
	.num_links = ARRAY_SIZE(xm_usb3_0_links),
	.links = xm_usb3_0_links
};

static struct qcom_icc_node qnm_gpu_qos = {
	.name = "qnm_gpu_qos",
	.id = SM6115_MASTER_GRAPHICS_3D_PORT1,
	.buswidth = 32,
	.mas_rpm_id = 6,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 0,
	.qos.prio_level = 0,
	.qos.qos_port = 16,
};

static const u16 qnm_gpu_links[] = {
	SM6115_SLAVE_GPU_CDSP_BIMC,
};

static struct qcom_icc_node qnm_gpu = {
	.name = "qnm_gpu",
	.id = SM6115_MASTER_GRAPHICS_3D,
	.buswidth = 32,
	.mas_rpm_id = 6,
	.slv_rpm_id = -1,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(qnm_gpu_links),
	.links = qnm_gpu_links
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.id = SM6115_SLAVE_EBI_CH0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 0,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static const u16 slv_bimc_snoc_links[] = {
	SM6115_BIMC_SNOC_MAS,
};

static struct qcom_icc_node slv_bimc_snoc = {
	.name = "bimc_snoc",
	.id = SM6115_BIMC_SNOC_SLV,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 2,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(slv_bimc_snoc_links),
	.links = slv_bimc_snoc_links
};

static struct qcom_icc_node qhs_bimc_cfg = {
	.name = "qhs_bimc_cfg",
	.id = SM6115_SLAVE_BIMC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 56,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_camera_nrt_throtle_cfg = {
	.name = "qhs_camera_nrt_throtle_cfg",
	.id = SM6115_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 271,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_camera_rt_throttle_cfg = {
	.name = "qhs_camera_rt_throttle_cfg",
	.id = SM6115_SLAVE_CAMERA_RT_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 279,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_camera_ss_cfg = {
	.name = "qhs_camera_ss_cfg",
	.id = SM6115_SLAVE_CAMERA_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 3,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_cdsp_throttle_cfg = {
	.name = "qhs_cdsp_throttle_cfg",
	.id = SM6115_SLAVE_CDSP_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 272,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SM6115_SLAVE_CLK_CTL,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 47,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SM6115_SLAVE_CRYPTO_0_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 52,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_disp_ss_cfg = {
	.name = "qhs_disp_ss_cfg",
	.id = SM6115_SLAVE_DISPLAY_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 4,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_display_throttle_cfg = {
	.name = "qhs_display_throttle_cfg",
	.id = SM6115_SLAVE_DISPLAY_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 156,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_gpu_cfg = {
	.name = "qhs_gpu_cfg",
	.id = SM6115_SLAVE_GPU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 275,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SM6115_SLAVE_IMEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 54,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_ipa_cfg = {
	.name = "qhs_ipa_cfg",
	.id = SM6115_SLAVE_IPA_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 183,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_lpass = {
	.name = "qhs_lpass",
	.id = SM6115_SLAVE_LPASS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 21,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_mesg_ram = {
	.name = "qhs_mesg_ram",
	.id = SM6115_SLAVE_MESSAGE_RAM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 55,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_pdm = {
	.name = "qhs_pdm",
	.id = SM6115_SLAVE_PDM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 41,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SM6115_SLAVE_PIMEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 167,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_pmic_arb = {
	.name = "qhs_pmic_arb",
	.id = SM6115_SLAVE_PMIC_ARB,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 59,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_prng = {
	.name = "qhs_prng",
	.id = SM6115_SLAVE_PRNG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 44,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SM6115_SLAVE_QDSS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 63,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_qm_cfg = {
	.name = "qhs_qm_cfg",
	.id = SM6115_SLAVE_QM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 212,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_qm_mpu_cfg = {
	.name = "qhs_qm_mpu_cfg",
	.id = SM6115_SLAVE_QM_MPU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 231,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SM6115_SLAVE_QUP_0,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 261,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_sdc1 = {
	.name = "qhs_sdc1",
	.id = SM6115_SLAVE_SDCC_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 31,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SM6115_SLAVE_SDCC_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 33,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static const u16 slv_snoc_cfg_links[] = {
	SM6115_MASTER_SNOC_CFG,
};

static struct qcom_icc_node slv_snoc_cfg = {
	.name = "snoc_cfg",
	.id = SM6115_SLAVE_SNOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 70,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(slv_snoc_cfg_links),
	.links = slv_snoc_cfg_links
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SM6115_SLAVE_TCSR,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 50,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_tlmm_east = {
	.name = "qhs_tlmm_east",
	.id = SM6115_SLAVE_TLMM_EAST,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 213,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_tlmm_south = {
	.name = "qhs_tlmm_south",
	.id = SM6115_SLAVE_TLMM_SOUTH,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 216,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_tlmm_west = {
	.name = "qhs_tlmm_west",
	.id = SM6115_SLAVE_TLMM_WEST,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 215,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_ufs_mem_cfg = {
	.name = "qhs_ufs_mem_cfg",
	.id = SM6115_SLAVE_UFS_MEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 262,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_usb3 = {
	.name = "qhs_usb3",
	.id = SM6115_SLAVE_USB3,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 22,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SM6115_SLAVE_VENUS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 10,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_venus_throttle_cfg = {
	.name = "qhs_venus_throttle_cfg",
	.id = SM6115_SLAVE_VENUS_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 178,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SM6115_SLAVE_VSENSE_CTRL_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 263,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node srvc_cnoc = {
	.name = "srvc_cnoc",
	.id = SM6115_SLAVE_SERVICE_CNOC,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 76,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qup_core_slave_0 = {
	.name = "qup_core_slave_0",
	.id = SM6115_SLAVE_QUP_CORE_0,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 264,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qhs_apss = {
	.name = "qhs_apss",
	.id = SM6115_SLAVE_APPSS,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 20,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static const u16 slv_snoc_bimc_nrt_links[] = {
	SM6115_MASTER_SNOC_BIMC_NRT,
};

static struct qcom_icc_node slv_snoc_bimc_nrt = {
	.name = "snoc_bimc_nrt",
	.id = SM6115_SLAVE_SNOC_BIMC_NRT,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 259,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(slv_snoc_bimc_nrt_links),
	.links = slv_snoc_bimc_nrt_links
};

static const u16 slv_snoc_bimc_rt_links[] = {
	SM6115_MASTER_SNOC_BIMC_RT,
};

static struct qcom_icc_node slv_snoc_bimc_rt = {
	.name = "snoc_bimc_rt",
	.id = SM6115_SLAVE_SNOC_BIMC_RT,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 260,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(slv_snoc_bimc_rt_links),
	.links = slv_snoc_bimc_rt_links
};

static const u16 slv_snoc_cnoc_links[] = {
	SM6115_SNOC_CNOC_MAS
};

static struct qcom_icc_node slv_snoc_cnoc = {
	.name = "snoc_cnoc",
	.id = SM6115_SNOC_CNOC_SLV,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 25,
	.qos.qos_mode = NOC_QOS_MODE_FIXED,
	.qos.areq_prio = 2,
	.qos.prio_level = 0,
	.qos.qos_port = 8,
	.num_links = ARRAY_SIZE(slv_snoc_cnoc_links),
	.links = slv_snoc_cnoc_links
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.id = SM6115_SLAVE_OCIMEM,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 26,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node qxs_pimem = {
	.name = "qxs_pimem",
	.id = SM6115_SLAVE_PIMEM,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 166,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static const u16 slv_snoc_bimc_links[] = {
	SM6115_SNOC_BIMC_MAS
};

static struct qcom_icc_node slv_snoc_bimc = {
	.name = "snoc_bimc",
	.id = SM6115_SNOC_BIMC_SLV,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 24,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(slv_snoc_bimc_links),
	.links = slv_snoc_bimc_links
};

static struct qcom_icc_node srvc_snoc = {
	.name = "srvc_snoc",
	.id = SM6115_SLAVE_SERVICE_SNOC,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 29,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SM6115_SLAVE_QDSS_STM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 30,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SM6115_SLAVE_TCU,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 133,
	.qos.ap_owned = true,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
};

static const u16 slv_anoc_snoc_links[] = {
	SM6115_MASTER_ANOC_SNOC
};

static struct qcom_icc_node slv_anoc_snoc = {
	.name = "anoc_snoc",
	.id = SM6115_SLAVE_ANOC_SNOC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 141,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(slv_anoc_snoc_links),
	.links = slv_anoc_snoc_links
};

static const u16 slv_gpu_cdsp_bimc_links[] = {
	SM6115_MASTER_GPU_CDSP_PROC
};

static struct qcom_icc_node slv_gpu_cdsp_bimc = {
	.name = "gpu_cdsp_bimc",
	.id = SM6115_SLAVE_GPU_CDSP_BIMC,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = 266,
	.qos.qos_mode = NOC_QOS_MODE_INVALID,
	.qos.qos_port = -1,
	.num_links = ARRAY_SIZE(slv_gpu_cdsp_bimc_links),
	.links = slv_gpu_cdsp_bimc_links
};

static struct qcom_icc_node * const bimc_nodes[] = {
	[MASTER_AMPSS_M0] = &apps_proc,
	[MASTER_SNOC_BIMC_RT] = &mas_snoc_bimc_rt,
	[MASTER_SNOC_BIMC_NRT] = &mas_snoc_bimc_nrt,
	[SNOC_BIMC_MAS] = &mas_snoc_bimc,
	[MASTER_GPU_CDSP_PROC] = &mas_gpu_cdsp_bimc,
	[MASTER_TCU_0] = &tcu_0,
	[SLAVE_EBI_CH0] = &ebi,
	[BIMC_SNOC_SLV] = &slv_bimc_snoc,
};

static const struct regmap_config sm6115_bimc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x80000 - 0x8000,
	.fast_io	= true,
};

static const struct qcom_icc_desc sm6115_bimc = {
	.nodes = bimc_nodes,
	.num_nodes = ARRAY_SIZE(bimc_nodes),
	.regmap_cfg = &sm6115_bimc_regmap_config,
	.type = QCOM_ICC_BIMC,
};

static struct qcom_icc_node * const cnoc_nodes[] = {
	[SNOC_CNOC_MAS] = &mas_snoc_cnoc,
	[MASTER_QDSS_DAP] = &xm_dap,
	[SLAVE_BIMC_CFG] = &qhs_bimc_cfg,
	[SLAVE_CAMERA_NRT_THROTTLE_CFG] = &qhs_camera_nrt_throtle_cfg,
	[SLAVE_CAMERA_RT_THROTTLE_CFG] = &qhs_camera_rt_throttle_cfg,
	[SLAVE_CAMERA_CFG] = &qhs_camera_ss_cfg,
	[SLAVE_CDSP_THROTTLE_CFG] = &qhs_cdsp_throttle_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_disp_ss_cfg,
	[SLAVE_DISPLAY_THROTTLE_CFG] = &qhs_display_throttle_cfg,
	[SLAVE_GPU_CFG] = &qhs_gpu_cfg,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa_cfg,
	[SLAVE_LPASS] = &qhs_lpass,
	[SLAVE_MESSAGE_RAM] = &qhs_mesg_ram,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PMIC_ARB] = &qhs_pmic_arb,
	[SLAVE_PRNG] = &qhs_prng,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QM_CFG] = &qhs_qm_cfg,
	[SLAVE_QM_MPU_CFG] = &qhs_qm_mpu_cfg,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_SDCC_1] = &qhs_sdc1,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SNOC_CFG] = &slv_snoc_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM_EAST] = &qhs_tlmm_east,
	[SLAVE_TLMM_SOUTH] = &qhs_tlmm_south,
	[SLAVE_TLMM_WEST] = &qhs_tlmm_west,
	[SLAVE_UFS_MEM_CFG] = &qhs_ufs_mem_cfg,
	[SLAVE_USB3] = &qhs_usb3,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &qhs_venus_throttle_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc,
};

static const struct regmap_config sm6115_cnoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x8200,
	.fast_io	= true,
};

static const struct qcom_icc_desc sm6115_cnoc = {
	.nodes = cnoc_nodes,
	.num_nodes = ARRAY_SIZE(cnoc_nodes),
	.regmap_cfg = &sm6115_cnoc_regmap_config,
	.type = QCOM_ICC_NOC,
};

static struct qcom_icc_node * const snoc_nodes[] = {
	[MASTER_CRYPTO_CORE0] = &crypto_c0,
	[MASTER_SNOC_CFG] = &mas_snoc_cfg,
	[MASTER_TIC] = &qhm_tic,
	[MASTER_ANOC_SNOC] = &mas_anoc_snoc,
	[BIMC_SNOC_MAS] = &mas_bimc_snoc,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_1] = &xm_sdc1,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_UFS_MEM] = &xm_ufs_mem,
	[MASTER_USB3] = &xm_usb3_0,
	[MASTER_GRAPHICS_3D_PORT1] = &qnm_gpu_qos,
	[SLAVE_APPSS] = &qhs_apss,
	[SNOC_CNOC_SLV] = &slv_snoc_cnoc,
	[SLAVE_OCIMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SNOC_BIMC_SLV] = &slv_snoc_bimc,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
	[SLAVE_ANOC_SNOC] = &slv_anoc_snoc,
};

static const struct regmap_config sm6115_snoc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x60200,
	.fast_io	= true,
};

static const struct qcom_icc_desc sm6115_snoc = {
	.nodes = snoc_nodes,
	.num_nodes = ARRAY_SIZE(snoc_nodes),
	.type = QCOM_ICC_QNOC,
	.regmap_cfg = &sm6115_snoc_regmap_config,
	.qos_offset = 0x15000,
};

static struct qcom_icc_node * const clk_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup_core_master_0,
	[SLAVE_QUP_CORE_0] = &qup_core_slave_0,
};

static const struct qcom_icc_desc sm6115_clk_virt = {
	.nodes = clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(clk_virt_nodes),
	.regmap_cfg = &sm6115_snoc_regmap_config,
	.type = QCOM_ICC_NOC,
};

static struct qcom_icc_node * const gpu_virt_nodes[] = {
	[MASTER_GRAPHICS_3D] = &qnm_gpu,
	[SLAVE_GPU_CDSP_BIMC] = &slv_gpu_cdsp_bimc,
};

static const struct qcom_icc_desc sm6115_gpu_virt = {
	.nodes = gpu_virt_nodes,
	.num_nodes = ARRAY_SIZE(gpu_virt_nodes),
	.regmap_cfg = &sm6115_snoc_regmap_config,
	.type = QCOM_ICC_NOC,
};

static struct qcom_icc_node * const mmnrt_virt_nodes[] = {
	[MASTER_CAMNOC_SF] = &qnm_camera_nrt,
	[MASTER_VIDEO_P0] = &qxm_venus0,
	[MASTER_VIDEO_PROC] = &qxm_venus_cpu,
	[SLAVE_SNOC_BIMC_NRT] = &slv_snoc_bimc_nrt,
};

static const struct qcom_icc_desc sm6115_mmnrt_virt = {
	.nodes = mmnrt_virt_nodes,
	.num_nodes = ARRAY_SIZE(mmnrt_virt_nodes),
	.type = QCOM_ICC_QNOC,
	.regmap_cfg = &sm6115_snoc_regmap_config,
	.qos_offset = 0x15000,
};

static struct qcom_icc_node * const mmrt_virt_nodes[] = {
	[MASTER_CAMNOC_HF] = &qnm_camera_rt,
	[MASTER_MDP_PORT0] = &qxm_mdp0,
	[SLAVE_SNOC_BIMC_RT] = &slv_snoc_bimc_rt,
};

static const struct qcom_icc_desc sm6115_mmrt_virt = {
	.nodes = mmrt_virt_nodes,
	.num_nodes = ARRAY_SIZE(mmrt_virt_nodes),
	.type = QCOM_ICC_QNOC,
	.regmap_cfg = &sm6115_snoc_regmap_config,
	.qos_offset = 0x15000,
};

static const struct of_device_id qnoc_of_match[] = {
	{ .compatible = "qcom,sm6115-bimc", .data = &sm6115_bimc},
	{ .compatible = "qcom,sm6115-clk-virt", .data = &sm6115_clk_virt},
	{ .compatible = "qcom,sm6115-cnoc", .data = &sm6115_cnoc},
	{ .compatible = "qcom,sm6115-gpu-virt", .data = &sm6115_gpu_virt},
	{ .compatible = "qcom,sm6115-mmnrt-virt", .data = &sm6115_mmnrt_virt},
	{ .compatible = "qcom,sm6115-mmrt-virt", .data = &sm6115_mmrt_virt},
	{ .compatible = "qcom,sm6115-snoc", .data = &sm6115_snoc},
	{ }
};
MODULE_DEVICE_TABLE(of, qnoc_of_match);

static struct platform_driver qnoc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-sm6115",
		.of_match_table = qnoc_of_match,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(qnoc_driver);

MODULE_DESCRIPTION("Qualcomm SM6115 NoC driver");
MODULE_LICENSE("GPL");
