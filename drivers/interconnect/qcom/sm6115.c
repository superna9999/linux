// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Linaro Limited
 */

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

#include <dt-bindings/interconnect/qcom,sm6115.h>

#include "icc-rpm.h"
#include "smd-rpm.h"

enum {
	/* BIMC */
	SM6115_MASTER_AMPSS_M0 = 1,
	SM6115_MASTER_SNOC_BIMC_RT,
	SM6115_MASTER_SNOC_BIMC_NRT,
	SM6115_MASTER_SNOC_BIMC,
	SM6115_MASTER_GRAPHICS_3D,
	SM6115_MASTER_TCU_0,

	/* CNOC */
	SM6115_MASTER_SNOC_CNOC,
	SM6115_MASTER_QDSS_DAP,


	/* SNOC */
	SM6115_MASTER_SNOC_CFG,
	SM6115_MASTER_TIC,
	SM6115_MASTER_ANOC_SNOC,
	SM6115_MASTER_BIMC_SNOC,
	SM6115_MASTER_PIMEM,
	SM6115_MASTER_CRVIRT_A1NOC,
	SM6115_MASTER_QDSS_BAM,
	SM6115_MASTER_QPIC,
	SM6115_MASTER_QUP_0,
	SM6115_MASTER_IPA,
	SM6115_MASTER_QDSS_ETR,
	SM6115_MASTER_SDCC_1,
	SM6115_MASTER_SDCC_2,
	SM6115_MASTER_USB3,

	/* CLK VIRT */
	SM6115_MASTER_QUP_CORE_0,
	SM6115_MASTER_CRYPTO_CORE0,

	/* MMNRT Virtual */
	SM6115_MASTER_CAMNOC_SF,
	SM6115_MASTER_VIDEO_P0,
	SM6115_MASTER_VIDEO_PROC,

	/* MMRT Virtual */
	SM6115_MASTER_CAMNOC_HF,
	SM6115_MASTER_MDP_PORT0,

	/* BIMC */
	SM6115_SLAVE_EBI_CH0,
	SM6115_SLAVE_BIMC_SNOC,

	/* CNOC */
	SM6115_SLAVE_AHB2PHY_USB,
	SM6115_SLAVE_APSS_THROTTLE_CFG,
	SM6115_SLAVE_BIMC_CFG,
	SM6115_SLAVE_BOOT_ROM,
	SM6115_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	SM6115_SLAVE_CAMERA_RT_THROTTLE_CFG,
	SM6115_SLAVE_CAMERA_CFG,
	SM6115_SLAVE_CLK_CTL,
	SM6115_SLAVE_RBCPR_CX_CFG,
	SM6115_SLAVE_RBCPR_MX_CFG,
	SM6115_SLAVE_CRYPTO_0_CFG,
	SM6115_SLAVE_DCC_CFG,
	SM6115_SLAVE_DDR_PHY_CFG,
	SM6115_SLAVE_DDR_SS_CFG,
	SM6115_SLAVE_DISPLAY_CFG,
	SM6115_SLAVE_DISPLAY_THROTTLE_CFG,
	SM6115_SLAVE_GPU_CFG,
	SM6115_SLAVE_GPU_THROTTLE_CFG,
	SM6115_SLAVE_HWKM_CORE,
	SM6115_SLAVE_IMEM_CFG,
	SM6115_SLAVE_IPA_CFG,
	SM6115_SLAVE_LPASS,
	SM6115_SLAVE_MAPSS,
	SM6115_SLAVE_MDSP_MPU_CFG,
	SM6115_SLAVE_MESSAGE_RAM,
	SM6115_SLAVE_CNOC_MSS,
	SM6115_SLAVE_PDM,
	SM6115_SLAVE_PIMEM_CFG,
	SM6115_SLAVE_PKA_CORE,
	SM6115_SLAVE_PMIC_ARB,
	SM6115_SLAVE_QDSS_CFG,
	SM6115_SLAVE_QM_CFG,
	SM6115_SLAVE_QM_MPU_CFG,
	SM6115_SLAVE_QPIC,
	SM6115_SLAVE_QUP_0,
	SM6115_SLAVE_RPM,
	SM6115_SLAVE_SDCC_1,
	SM6115_SLAVE_SDCC_2,
	SM6115_SLAVE_SECURITY,
	SM6115_SLAVE_SNOC_CFG,
	SM6115_SLAVE_TCSR,
	SM6115_SLAVE_TLMM,
	SM6115_SLAVE_USB3,
	SM6115_SLAVE_VENUS_CFG,
	SM6115_SLAVE_VENUS_THROTTLE_CFG,
	SM6115_SLAVE_VSENSE_CTRL_CFG,
	SM6115_SLAVE_SERVICE_CNOC,

	/* SNOC */
	SM6115_SLAVE_APPSS,
	SM6115_SLAVE_SNOC_CNOC,
	SM6115_SLAVE_OCIMEM,
	SM6115_SLAVE_PIMEM,
	SM6115_SLAVE_SNOC_BIMC,
	SM6115_SLAVE_SERVICE_SNOC,
	SM6115_SLAVE_QDSS_STM,
	SM6115_SLAVE_TCU,
	SM6115_SLAVE_ANOC_SNOC,

	/* CLK VIRT */
	SM6115_SLAVE_QUP_CORE_0,
	SM6115_SLAVE_CRVIRT_A1NOC,

	/* MMNRT Virtual */
	SM6115_SLAVE_SNOC_BIMC_NRT,

	/* MMRT Virtual */
	SM6115_SLAVE_SNOC_BIMC_RT,
};

static const u16 apps_proc_links[] = {
	SLAVE_EBI_CH0,
	SLAVE_BIMC_SNOC,
};

static struct qcom_icc_node apps_proc = {
	.name = "apps_proc",
	.id = SM6115_MASTER_AMPSS_M0,
	.buswidth = 16,
	.mas_rpm_id = 0,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(apps_proc_links),
	.links = apps_proc_links,
};

static const u16 mas_snoc_bimc_rt_links[] = {
	SLAVE_EBI_CH0,
};

static struct qcom_icc_node mas_snoc_bimc_rt = {
	.name = "mas_snoc_bimc_rt",
	.id = SM6115_MASTER_SNOC_BIMC_RT,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_snoc_bimc_rt_links),
	.links = mas_snoc_bimc_rt_links,
};

static const u16 mas_snoc_bimc_nrt_links[] = {
	SLAVE_EBI_CH0,
};

static struct qcom_icc_node mas_snoc_bimc_nrt = {
	.name = "mas_snoc_bimc_nrt",
	.id = SM6115_MASTER_SNOC_BIMC_NRT,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_snoc_bimc_nrt_links),
	.links = mas_snoc_bimc_nrt_links,
};

static const u16 mas_snoc_bimc_links[] = {
	SLAVE_EBI_CH0,
};

static struct qcom_icc_node mas_snoc_bimc = {
	.name = "mas_snoc_bimc",
	.id = SM6115_MASTER_SNOC_BIMC,
	.buswidth = 16,
	.mas_rpm_id = 3,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_snoc_bimc_links),
	.links = mas_snoc_bimc_links,
};

static const u16 qnm_gpu_links[] = {
	SLAVE_EBI_CH0,
	SLAVE_BIMC_SNOC,
};

static struct qcom_icc_node qnm_gpu = {
	.name = "qnm_gpu",
	.id = SM6115_MASTER_GRAPHICS_3D,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_gpu_links),
	.links = qnm_gpu_links,
};

static const u16 tcu_0_links[] = {
	SLAVE_EBI_CH0,
	SLAVE_BIMC_SNOC,
};

static struct qcom_icc_node tcu_0 = {
	.name = "tcu_0",
	.id = SM6115_MASTER_TCU_0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(tcu_0_links),
	.links = tcu_0_links,
};

static const u16 qup0_core_master_links[] = {
	SLAVE_QUP_CORE_0,
};

static struct qcom_icc_node qup0_core_master = {
	.name = "qup0_core_master",
	.id = SM6115_MASTER_QUP_CORE_0,
	.buswidth = 4,
	.mas_rpm_id = 170,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qup0_core_master_links),
	.links = qup0_core_master_links,
};

static const u16 crypto_c0_links[] = {
	SLAVE_CRVIRT_A1NOC,
};

static struct qcom_icc_node crypto_c0 = {
	.name = "crypto_c0",
	.id = SM6115_MASTER_CRYPTO_CORE0,
	.buswidth = 650,
	.mas_rpm_id = 23,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(crypto_c0_links),
	.links = crypto_c0_links,
};

static const u16 mas_snoc_cnoc_links[] = {
	SLAVE_AHB2PHY_USB,
	SLAVE_APSS_THROTTLE_CFG,
	SLAVE_BIMC_CFG,
	SLAVE_BOOT_ROM,
	SLAVE_CAMERA_NRT_THROTTLE_CFG,
	SLAVE_CAMERA_RT_THROTTLE_CFG,
	SLAVE_CAMERA_CFG,
	SLAVE_CLK_CTL,
	SLAVE_RBCPR_CX_CFG,
	SLAVE_RBCPR_MX_CFG,
	SLAVE_CRYPTO_0_CFG,
	SLAVE_DCC_CFG,
	SLAVE_DDR_PHY_CFG,
	SLAVE_DDR_SS_CFG,
	SLAVE_DISPLAY_CFG,
	SLAVE_DISPLAY_THROTTLE_CFG,
	SLAVE_GPU_CFG,
	SLAVE_GPU_THROTTLE_CFG,
	SLAVE_HWKM_CORE,
	SLAVE_IMEM_CFG,
	SLAVE_IPA_CFG,
	SLAVE_LPASS,
	SLAVE_MAPSS,
	SLAVE_MDSP_MPU_CFG,
	SLAVE_MESSAGE_RAM,
	SLAVE_CNOC_MSS,
	SLAVE_PDM,
	SLAVE_PIMEM_CFG,
	SLAVE_PKA_CORE,
	SLAVE_PMIC_ARB,
	SLAVE_QDSS_CFG,
	SLAVE_QM_CFG,
	SLAVE_QM_MPU_CFG,
	SLAVE_QPIC,
	SLAVE_QUP_0,
	SLAVE_RPM,
	SLAVE_SDCC_1,
	SLAVE_SDCC_2,
	SLAVE_SECURITY,
	SLAVE_SNOC_CFG,
	SLAVE_TCSR,
	SLAVE_TLMM,
	SLAVE_USB3,
	SLAVE_VENUS_CFG,
	SLAVE_VENUS_THROTTLE_CFG,
	SLAVE_VSENSE_CTRL_CFG,
	SLAVE_SERVICE_CNOC,
};

static struct qcom_icc_node mas_snoc_cnoc = {
	.name = "mas_snoc_cnoc",
	.id = SM6115_MASTER_SNOC_CNOC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_snoc_cnoc_links),
	.links = mas_snoc_cnoc_links,
};

static const u16 xm_dap_links[] = {
	SLAVE_AHB2PHY_USB,
	SLAVE_APSS_THROTTLE_CFG,
	SLAVE_BIMC_CFG,
	SLAVE_BOOT_ROM,
	SLAVE_CAMERA_NRT_THROTTLE_CFG,
	SLAVE_CAMERA_RT_THROTTLE_CFG,
	SLAVE_CAMERA_CFG,
	SLAVE_CLK_CTL,
	SLAVE_RBCPR_CX_CFG,
	SLAVE_RBCPR_MX_CFG,
	SLAVE_CRYPTO_0_CFG,
	SLAVE_DCC_CFG,
	SLAVE_DDR_PHY_CFG,
	SLAVE_DDR_SS_CFG,
	SLAVE_DISPLAY_CFG,
	SLAVE_DISPLAY_THROTTLE_CFG,
	SLAVE_GPU_CFG,
	SLAVE_GPU_THROTTLE_CFG,
	SLAVE_HWKM_CORE,
	SLAVE_IMEM_CFG,
	SLAVE_IPA_CFG,
	SLAVE_LPASS,
	SLAVE_MAPSS,
	SLAVE_MDSP_MPU_CFG,
	SLAVE_MESSAGE_RAM,
	SLAVE_CNOC_MSS,
	SLAVE_PDM,
	SLAVE_PIMEM_CFG,
	SLAVE_PKA_CORE,
	SLAVE_PMIC_ARB,
	SLAVE_QDSS_CFG,
	SLAVE_QM_CFG,
	SLAVE_QM_MPU_CFG,
	SLAVE_QPIC,
	SLAVE_QUP_0,
	SLAVE_RPM,
	SLAVE_SDCC_1,
	SLAVE_SDCC_2,
	SLAVE_SECURITY,
	SLAVE_SNOC_CFG,
	SLAVE_TCSR,
	SLAVE_TLMM,
	SLAVE_USB3,
	SLAVE_VENUS_CFG,
	SLAVE_VENUS_THROTTLE_CFG,
	SLAVE_VSENSE_CTRL_CFG,
	SLAVE_SERVICE_CNOC,
};

static struct qcom_icc_node xm_dap = {
	.name = "xm_dap",
	.id = SM6115_MASTER_QDSS_DAP,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(xm_dap_links),
	.links = xm_dap_links,
};

static const u16 qnm_camera_nrt_links[] = {
	SLAVE_SNOC_BIMC_NRT,
};

static struct qcom_icc_node qnm_camera_nrt = {
	.name = "qnm_camera_nrt",
	.id = SM6115_MASTER_CAMNOC_SF,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_camera_nrt_links),
	.links = qnm_camera_nrt_links,
};

static struct qcom_icc_node qxm_venus0 = {
	.name = "qxm_venus0",
	.id = SM6115_MASTER_VIDEO_P0,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_camera_nrt_links),
	.links = qnm_camera_nrt_links,
};

static struct qcom_icc_node qxm_venus_cpu = {
	.name = "qxm_venus_cpu",
	.id = SM6115_MASTER_VIDEO_PROC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_camera_nrt_links),
	.links = qnm_camera_nrt_links,
};

static const u16 qnm_camera_rt_links[] = {
	SLAVE_SNOC_BIMC_RT,
};

static struct qcom_icc_node qnm_camera_rt = {
	.name = "qnm_camera_rt",
	.id = SM6115_MASTER_CAMNOC_HF,
	.buswidth = 32,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_camera_rt_links),
	.links = qnm_camera_rt_links,
};

static struct qcom_icc_node qxm_mdp0 = {
	.name = "qxm_mdp0",
	.id = SM6115_MASTER_MDP_PORT0,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qnm_camera_rt_links),
	.links = qnm_camera_rt_links,
};

static const u16 qhm_snoc_cfg_links[] = {
	SLAVE_SERVICE_SNOC,
};

static struct qcom_icc_node qhm_snoc_cfg = {
	.name = "qhm_snoc_cfg",
	.id = SM6115_MASTER_SNOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qhm_snoc_cfg_links),
	.links = qhm_snoc_cfg_links,
};

static const u16 qhm_tic_links[] = {
	SLAVE_APPSS,
	SLAVE_SNOC_CNOC,
	SLAVE_OCIMEM,
	SLAVE_PIMEM,
	SLAVE_SNOC_BIMC,
	SLAVE_QDSS_STM,
	SLAVE_TCU,
};

static struct qcom_icc_node qhm_tic = {
	.name = "qhm_tic",
	.id = SM6115_MASTER_TIC,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qhm_tic_links),
	.links = qhm_tic_links,
};

static struct qcom_icc_node mas_anoc_snoc = {
	.name = "mas_anoc_snoc",
	.id = SM6115_MASTER_ANOC_SNOC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qhm_tic_links),
	.links = qhm_tic_links,
};

static const u16 mas_bimc_snoc_links[] = {
	SLAVE_APPSS,
	SLAVE_SNOC_CNOC,
	SLAVE_OCIMEM,
	SLAVE_PIMEM,
	SLAVE_QDSS_STM,
	SLAVE_TCU,
};

static struct qcom_icc_node mas_bimc_snoc = {
	.name = "mas_bimc_snoc",
	.id = SM6115_MASTER_BIMC_SNOC,
	.buswidth = 8,
	.mas_rpm_id = 21,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_bimc_snoc_links),
	.links = mas_bimc_snoc_links,
};

static const u16 qxm_pimem_links[] = {
	SLAVE_OCIMEM,
	SLAVE_SNOC_BIMC,
};

static struct qcom_icc_node qxm_pimem = {
	.name = "qxm_pimem",
	.id = SM6115_MASTER_PIMEM,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qxm_pimem_links),
	.links = qxm_pimem_links,
};

static const u16 mas_cr_virt_a1noc_links[] = {
	SLAVE_ANOC_SNOC,
};

static struct qcom_icc_node mas_cr_virt_a1noc = {
	.name = "mas_cr_virt_a1noc",
	.id = SM6115_MASTER_CRVIRT_A1NOC,
	.buswidth = 8,
	.mas_rpm_id = 136,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_cr_virt_a1noc_links),
	.links = mas_cr_virt_a1noc_links,
};

static struct qcom_icc_node qhm_qdss_bam = {
	.name = "qhm_qdss_bam",
	.id = SM6115_MASTER_QDSS_BAM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_cr_virt_a1noc_links),
	.links = mas_cr_virt_a1noc_links,
};

static struct qcom_icc_node qhm_qpic = {
	.name = "qhm_qpic",
	.id = SM6115_MASTER_QPIC,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_cr_virt_a1noc_links),
	.links = mas_cr_virt_a1noc_links,
};

static struct qcom_icc_node qhm_qup0 = {
	.name = "qhm_qup0",
	.id = SM6115_MASTER_QUP_0,
	.buswidth = 4,
	.mas_rpm_id = 166,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_cr_virt_a1noc_links),
	.links = mas_cr_virt_a1noc_links,
};

static struct qcom_icc_node qxm_ipa = {
	.name = "qxm_ipa",
	.id = SM6115_MASTER_IPA,
	.buswidth = 8,
	.mas_rpm_id = 59,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_cr_virt_a1noc_links),
	.links = mas_cr_virt_a1noc_links,
};

static struct qcom_icc_node xm_qdss_etr = {
	.name = "xm_qdss_etr",
	.id = SM6115_MASTER_QDSS_ETR,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_cr_virt_a1noc_links),
	.links = mas_cr_virt_a1noc_links,
};

static struct qcom_icc_node xm_sdc1 = {
	.name = "xm_sdc1",
	.id = SM6115_MASTER_SDCC_1,
	.buswidth = 8,
	.mas_rpm_id = 33,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_cr_virt_a1noc_links),
	.links = mas_cr_virt_a1noc_links,
};

static struct qcom_icc_node xm_sdc2 = {
	.name = "xm_sdc2",
	.id = SM6115_MASTER_SDCC_2,
	.buswidth = 8,
	.mas_rpm_id = 35,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_cr_virt_a1noc_links),
	.links = mas_cr_virt_a1noc_links,
};

static struct qcom_icc_node xm_usb3_0 = {
	.name = "xm_usb3_0",
	.id = SM6115_MASTER_USB3,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(mas_cr_virt_a1noc_links),
	.links = mas_cr_virt_a1noc_links,
};

static struct qcom_icc_node ebi = {
	.name = "ebi",
	.id = SM6115_SLAVE_EBI_CH0,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 0,
	.num_links = 0,
};

static const u16 slv_bimc_snoc_links[] = {
	MASTER_BIMC_SNOC,
};

static struct qcom_icc_node slv_bimc_snoc = {
	.name = "slv_bimc_snoc",
	.id = SM6115_SLAVE_BIMC_SNOC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 2,
	.num_links = ARRAY_SIZE(slv_bimc_snoc_links),
	.links = slv_bimc_snoc_links,
};

static struct qcom_icc_node qup0_core_slave = {
	.name = "qup0_core_slave",
	.id = SM6115_SLAVE_QUP_CORE_0,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static const u16 slv_cr_virt_a1noc_links[] = {
	MASTER_CRVIRT_A1NOC,
};

static struct qcom_icc_node slv_cr_virt_a1noc = {
	.name = "slv_cr_virt_a1noc",
	.id = SM6115_SLAVE_CRVIRT_A1NOC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_cr_virt_a1noc_links),
	.links = slv_cr_virt_a1noc_links,
};

static struct qcom_icc_node qhs_ahb2phy_usb = {
	.name = "qhs_ahb2phy_usb",
	.id = SM6115_SLAVE_AHB2PHY_USB,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_apss_throttle_cfg = {
	.name = "qhs_apss_throttle_cfg",
	.id = SM6115_SLAVE_APSS_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_bimc_cfg = {
	.name = "qhs_bimc_cfg",
	.id = SM6115_SLAVE_BIMC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_boot_rom = {
	.name = "qhs_boot_rom",
	.id = SM6115_SLAVE_BOOT_ROM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_camera_nrt_throttle_cfg = {
	.name = "qhs_camera_nrt_throttle_cfg",
	.id = SM6115_SLAVE_CAMERA_NRT_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_camera_rt_throttle_cfg = {
	.name = "qhs_camera_rt_throttle_cfg",
	.id = SM6115_SLAVE_CAMERA_RT_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_camera_ss_cfg = {
	.name = "qhs_camera_ss_cfg",
	.id = SM6115_SLAVE_CAMERA_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_clk_ctl = {
	.name = "qhs_clk_ctl",
	.id = SM6115_SLAVE_CLK_CTL,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_cx = {
	.name = "qhs_cpr_cx",
	.id = SM6115_SLAVE_RBCPR_CX_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_cpr_mx = {
	.name = "qhs_cpr_mx",
	.id = SM6115_SLAVE_RBCPR_MX_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_crypto0_cfg = {
	.name = "qhs_crypto0_cfg",
	.id = SM6115_SLAVE_CRYPTO_0_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_dcc_cfg = {
	.name = "qhs_dcc_cfg",
	.id = SM6115_SLAVE_DCC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ddr_phy_cfg = {
	.name = "qhs_ddr_phy_cfg",
	.id = SM6115_SLAVE_DDR_PHY_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ddr_ss_cfg = {
	.name = "qhs_ddr_ss_cfg",
	.id = SM6115_SLAVE_DDR_SS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_disp_ss_cfg = {
	.name = "qhs_disp_ss_cfg",
	.id = SM6115_SLAVE_DISPLAY_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_display_throttle_cfg = {
	.name = "qhs_display_throttle_cfg",
	.id = SM6115_SLAVE_DISPLAY_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_gpu_cfg = {
	.name = "qhs_gpu_cfg",
	.id = SM6115_SLAVE_GPU_CFG,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_gpu_throttle_cfg = {
	.name = "qhs_gpu_throttle_cfg",
	.id = SM6115_SLAVE_GPU_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_hwkm = {
	.name = "qhs_hwkm",
	.id = SM6115_SLAVE_HWKM_CORE,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_imem_cfg = {
	.name = "qhs_imem_cfg",
	.id = SM6115_SLAVE_IMEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_ipa_cfg = {
	.name = "qhs_ipa_cfg",
	.id = SM6115_SLAVE_IPA_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_lpass = {
	.name = "qhs_lpass",
	.id = SM6115_SLAVE_LPASS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_mapss = {
	.name = "qhs_mapss",
	.id = SM6115_SLAVE_MAPSS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_mdsp_mpu_cfg = {
	.name = "qhs_mdsp_mpu_cfg",
	.id = SM6115_SLAVE_MDSP_MPU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_mesg_ram = {
	.name = "qhs_mesg_ram",
	.id = SM6115_SLAVE_MESSAGE_RAM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_mss = {
	.name = "qhs_mss",
	.id = SM6115_SLAVE_CNOC_MSS,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pdm = {
	.name = "qhs_pdm",
	.id = SM6115_SLAVE_PDM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pimem_cfg = {
	.name = "qhs_pimem_cfg",
	.id = SM6115_SLAVE_PIMEM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pka_wrapper = {
	.name = "qhs_pka_wrapper",
	.id = SM6115_SLAVE_PKA_CORE,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_pmic_arb = {
	.name = "qhs_pmic_arb",
	.id = SM6115_SLAVE_PMIC_ARB,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qdss_cfg = {
	.name = "qhs_qdss_cfg",
	.id = SM6115_SLAVE_QDSS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qm_cfg = {
	.name = "qhs_qm_cfg",
	.id = SM6115_SLAVE_QM_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qm_mpu_cfg = {
	.name = "qhs_qm_mpu_cfg",
	.id = SM6115_SLAVE_QM_MPU_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qpic = {
	.name = "qhs_qpic",
	.id = SM6115_SLAVE_QPIC,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_qup0 = {
	.name = "qhs_qup0",
	.id = SM6115_SLAVE_QUP_0,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_rpm = {
	.name = "qhs_rpm",
	.id = SM6115_SLAVE_RPM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_sdc1 = {
	.name = "qhs_sdc1",
	.id = SM6115_SLAVE_SDCC_1,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_sdc2 = {
	.name = "qhs_sdc2",
	.id = SM6115_SLAVE_SDCC_2,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_security = {
	.name = "qhs_security",
	.id = SM6115_SLAVE_SECURITY,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static const u16 qhs_snoc_cfg_links[] = {
	MASTER_SNOC_CFG,
};

static struct qcom_icc_node qhs_snoc_cfg = {
	.name = "qhs_snoc_cfg",
	.id = SM6115_SLAVE_SNOC_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(qhs_snoc_cfg_links),
	.links = qhs_snoc_cfg_links,
};

static struct qcom_icc_node qhs_tcsr = {
	.name = "qhs_tcsr",
	.id = SM6115_SLAVE_TCSR,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_tlmm = {
	.name = "qhs_tlmm",
	.id = SM6115_SLAVE_TLMM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_usb3 = {
	.name = "qhs_usb3",
	.id = SM6115_SLAVE_USB3,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_venus_cfg = {
	.name = "qhs_venus_cfg",
	.id = SM6115_SLAVE_VENUS_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_venus_throttle_cfg = {
	.name = "qhs_venus_throttle_cfg",
	.id = SM6115_SLAVE_VENUS_THROTTLE_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node qhs_vsense_ctrl_cfg = {
	.name = "qhs_vsense_ctrl_cfg",
	.id = SM6115_SLAVE_VSENSE_CTRL_CFG,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node srvc_cnoc = {
	.name = "srvc_cnoc",
	.id = SM6115_SLAVE_SERVICE_CNOC,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static const u16 slv_snoc_bimc_nrt_links[] = {
	MASTER_SNOC_BIMC_NRT,
};

static struct qcom_icc_node slv_snoc_bimc_nrt = {
	.name = "slv_snoc_bimc_nrt",
	.id = SM6115_SLAVE_SNOC_BIMC_NRT,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_snoc_bimc_nrt_links),
	.links = slv_snoc_bimc_nrt_links,
};

static const u16 slv_snoc_bimc_rt_links[] = {
	MASTER_SNOC_BIMC_RT,
};

static struct qcom_icc_node slv_snoc_bimc_rt = {
	.name = "slv_snoc_bimc_rt",
	.id = SM6115_SLAVE_SNOC_BIMC_RT,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_snoc_bimc_rt_links),
	.links = slv_snoc_bimc_rt_links,
};

static struct qcom_icc_node qhs_apss = {
	.name = "qhs_apss",
	.id = SM6115_SLAVE_APPSS,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static const u16 slv_snoc_cnoc_links[] = {
	MASTER_SNOC_CNOC,
};

static struct qcom_icc_node slv_snoc_cnoc = {
	.name = "slv_snoc_cnoc",
	.id = SM6115_SLAVE_SNOC_CNOC,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 25,
	.num_links = ARRAY_SIZE(slv_snoc_cnoc_links),
	.links = slv_snoc_cnoc_links,
};

static struct qcom_icc_node qxs_imem = {
	.name = "qxs_imem",
	.id = SM6115_SLAVE_OCIMEM,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = 26,
	.num_links = 0,
};

static struct qcom_icc_node qxs_pimem = {
	.name = "qxs_pimem",
	.id = SM6115_SLAVE_PIMEM,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static const u16 slv_snoc_bimc_links[] = {
	MASTER_SNOC_BIMC,
};

static struct qcom_icc_node slv_snoc_bimc = {
	.name = "slv_snoc_bimc",
	.id = SM6115_SLAVE_SNOC_BIMC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = 24,
	.num_links = ARRAY_SIZE(slv_snoc_bimc_links),
	.links = slv_snoc_bimc_links,
};

static struct qcom_icc_node srvc_snoc = {
	.name = "srvc_snoc",
	.id = SM6115_SLAVE_SERVICE_SNOC,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static struct qcom_icc_node xs_qdss_stm = {
	.name = "xs_qdss_stm",
	.id = SM6115_SLAVE_QDSS_STM,
	.buswidth = 4,
	.mas_rpm_id = -1,
	.slv_rpm_id = 30,
	.num_links = 0,
};

static struct qcom_icc_node xs_sys_tcu_cfg = {
	.name = "xs_sys_tcu_cfg",
	.id = SM6115_SLAVE_TCU,
	.buswidth = 8,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = 0,
};

static const u16 slv_anoc_snoc_links[] = {
	MASTER_ANOC_SNOC,
};

static struct qcom_icc_node slv_anoc_snoc = {
	.name = "slv_anoc_snoc",
	.id = SM6115_SLAVE_ANOC_SNOC,
	.buswidth = 16,
	.mas_rpm_id = -1,
	.slv_rpm_id = -1,
	.num_links = ARRAY_SIZE(slv_anoc_snoc_links),
	.links = slv_anoc_snoc_links,
};

static struct qcom_icc_node * const sm6115_bimc_nodes[] = {
	[MASTER_AMPSS_M0] = &apps_proc,
	[MASTER_SNOC_BIMC_RT] = &mas_snoc_bimc_rt,
	[MASTER_SNOC_BIMC_NRT] = &mas_snoc_bimc_nrt,
	[MASTER_SNOC_BIMC] = &mas_snoc_bimc,
	[MASTER_GRAPHICS_3D] = &qnm_gpu,
	[MASTER_TCU_0] = &tcu_0,
	[SLAVE_EBI_CH0] = &ebi,
	[SLAVE_BIMC_SNOC] = &slv_bimc_snoc,
};

static const struct qcom_icc_desc sm6115_bimc = {
	.type = QCOM_ICC_BIMC,
	.nodes = sm6115_bimc_nodes,
	.num_nodes = ARRAY_SIZE(sm6115_bimc_nodes),
};

static struct qcom_icc_node * const sm6115_clk_virt_nodes[] = {
	[MASTER_QUP_CORE_0] = &qup0_core_master,
	[MASTER_CRYPTO_CORE0] = &crypto_c0,
	[SLAVE_QUP_CORE_0] = &qup0_core_slave,
	[SLAVE_CRVIRT_A1NOC] = &slv_cr_virt_a1noc,
};

static const struct qcom_icc_desc sm6115_clk_virt = {
	.type = QCOM_ICC_QNOC,
	.nodes = sm6115_clk_virt_nodes,
	.num_nodes = ARRAY_SIZE(sm6115_clk_virt_nodes),
};

static struct qcom_icc_node * const sm6115_cnoc_nodes[] = {
	[MASTER_SNOC_CNOC] = &mas_snoc_cnoc,
	[MASTER_QDSS_DAP] = &xm_dap,
	[SLAVE_AHB2PHY_USB] = &qhs_ahb2phy_usb,
	[SLAVE_APSS_THROTTLE_CFG] = &qhs_apss_throttle_cfg,
	[SLAVE_BIMC_CFG] = &qhs_bimc_cfg,
	[SLAVE_BOOT_ROM] = &qhs_boot_rom,
	[SLAVE_CAMERA_NRT_THROTTLE_CFG] = &qhs_camera_nrt_throttle_cfg,
	[SLAVE_CAMERA_RT_THROTTLE_CFG] = &qhs_camera_rt_throttle_cfg,
	[SLAVE_CAMERA_CFG] = &qhs_camera_ss_cfg,
	[SLAVE_CLK_CTL] = &qhs_clk_ctl,
	[SLAVE_RBCPR_CX_CFG] = &qhs_cpr_cx,
	[SLAVE_RBCPR_MX_CFG] = &qhs_cpr_mx,
	[SLAVE_CRYPTO_0_CFG] = &qhs_crypto0_cfg,
	[SLAVE_DCC_CFG] = &qhs_dcc_cfg,
	[SLAVE_DDR_PHY_CFG] = &qhs_ddr_phy_cfg,
	[SLAVE_DDR_SS_CFG] = &qhs_ddr_ss_cfg,
	[SLAVE_DISPLAY_CFG] = &qhs_disp_ss_cfg,
	[SLAVE_DISPLAY_THROTTLE_CFG] = &qhs_display_throttle_cfg,
	[SLAVE_GPU_CFG] = &qhs_gpu_cfg,
	[SLAVE_GPU_THROTTLE_CFG] = &qhs_gpu_throttle_cfg,
	[SLAVE_HWKM_CORE] = &qhs_hwkm,
	[SLAVE_IMEM_CFG] = &qhs_imem_cfg,
	[SLAVE_IPA_CFG] = &qhs_ipa_cfg,
	[SLAVE_LPASS] = &qhs_lpass,
	[SLAVE_MAPSS] = &qhs_mapss,
	[SLAVE_MDSP_MPU_CFG] = &qhs_mdsp_mpu_cfg,
	[SLAVE_MESSAGE_RAM] = &qhs_mesg_ram,
	[SLAVE_CNOC_MSS] = &qhs_mss,
	[SLAVE_PDM] = &qhs_pdm,
	[SLAVE_PIMEM_CFG] = &qhs_pimem_cfg,
	[SLAVE_PKA_CORE] = &qhs_pka_wrapper,
	[SLAVE_PMIC_ARB] = &qhs_pmic_arb,
	[SLAVE_QDSS_CFG] = &qhs_qdss_cfg,
	[SLAVE_QM_CFG] = &qhs_qm_cfg,
	[SLAVE_QM_MPU_CFG] = &qhs_qm_mpu_cfg,
	[SLAVE_QPIC] = &qhs_qpic,
	[SLAVE_QUP_0] = &qhs_qup0,
	[SLAVE_RPM] = &qhs_rpm,
	[SLAVE_SDCC_1] = &qhs_sdc1,
	[SLAVE_SDCC_2] = &qhs_sdc2,
	[SLAVE_SECURITY] = &qhs_security,
	[SLAVE_SNOC_CFG] = &qhs_snoc_cfg,
	[SLAVE_TCSR] = &qhs_tcsr,
	[SLAVE_TLMM] = &qhs_tlmm,
	[SLAVE_USB3] = &qhs_usb3,
	[SLAVE_VENUS_CFG] = &qhs_venus_cfg,
	[SLAVE_VENUS_THROTTLE_CFG] = &qhs_venus_throttle_cfg,
	[SLAVE_VSENSE_CTRL_CFG] = &qhs_vsense_ctrl_cfg,
	[SLAVE_SERVICE_CNOC] = &srvc_cnoc,
};

static const struct qcom_icc_desc sm6115_cnoc = {
	.type = QCOM_ICC_NOC,
	.nodes = sm6115_cnoc_nodes,
	.num_nodes = ARRAY_SIZE(sm6115_cnoc_nodes),
};

static struct qcom_icc_node * const sm6115_mmrt_virt_nodes[] = {
	[MASTER_CAMNOC_HF] = &qnm_camera_rt,
	[MASTER_MDP_PORT0] = &qxm_mdp0,
	[SLAVE_SNOC_BIMC_RT] = &slv_snoc_bimc_rt,
};

static const struct qcom_icc_desc sm6115_mmrt_virt = {
	.type = QCOM_ICC_QNOC,
	.nodes = sm6115_mmrt_virt_nodes,
	.num_nodes = ARRAY_SIZE(sm6115_mmrt_virt_nodes),
};

static struct qcom_icc_node * const sm6115_mmnrt_virt_nodes[] = {
	[MASTER_CAMNOC_SF] = &qnm_camera_nrt,
	[MASTER_VIDEO_P0] = &qxm_venus0,
	[MASTER_VIDEO_PROC] = &qxm_venus_cpu,
	[SLAVE_SNOC_BIMC_NRT] = &slv_snoc_bimc_nrt,
};

static const struct qcom_icc_desc sm6115_mmnrt_virt = {
	.type = QCOM_ICC_QNOC,
	.nodes = sm6115_mmnrt_virt_nodes,
	.num_nodes = ARRAY_SIZE(sm6115_mmnrt_virt_nodes),
};

static struct qcom_icc_node * const sm6115_snoc_nodes[] = {
	[MASTER_SNOC_CFG] = &qhm_snoc_cfg,
	[MASTER_TIC] = &qhm_tic,
	[MASTER_ANOC_SNOC] = &mas_anoc_snoc,
	[MASTER_BIMC_SNOC] = &mas_bimc_snoc,
	[MASTER_PIMEM] = &qxm_pimem,
	[MASTER_CRVIRT_A1NOC] = &mas_cr_virt_a1noc,
	[MASTER_QDSS_BAM] = &qhm_qdss_bam,
	[MASTER_QPIC] = &qhm_qpic,
	[MASTER_QUP_0] = &qhm_qup0,
	[MASTER_IPA] = &qxm_ipa,
	[MASTER_QDSS_ETR] = &xm_qdss_etr,
	[MASTER_SDCC_1] = &xm_sdc1,
	[MASTER_SDCC_2] = &xm_sdc2,
	[MASTER_USB3] = &xm_usb3_0,
	[SLAVE_APPSS] = &qhs_apss,
	[SLAVE_SNOC_CNOC] = &slv_snoc_cnoc,
	[SLAVE_OCIMEM] = &qxs_imem,
	[SLAVE_PIMEM] = &qxs_pimem,
	[SLAVE_SNOC_BIMC] = &slv_snoc_bimc,
	[SLAVE_SERVICE_SNOC] = &srvc_snoc,
	[SLAVE_QDSS_STM] = &xs_qdss_stm,
	[SLAVE_TCU] = &xs_sys_tcu_cfg,
	[SLAVE_ANOC_SNOC] = &slv_anoc_snoc,
};

static const struct qcom_icc_desc sm6115_snoc = {
	.type = QCOM_ICC_QNOC,
	.nodes = sm6115_snoc_nodes,
	.num_nodes = ARRAY_SIZE(sm6115_snoc_nodes),
};

static const struct of_device_id sm6115_qnoc_of_match[] = {
	{ .compatible = "qcom,sm6115-bimc", .data = &sm6115_bimc },
	{ .compatible = "qcom,sm6115-clk-virt", .data = &sm6115_clk_virt },
	{ .compatible = "qcom,sm6115-cnoc", .data = &sm6115_cnoc },
	{ .compatible = "qcom,sm6115-mmrt-virt", .data = &sm6115_mmrt_virt },
	{ .compatible = "qcom,sm6115-mmnrt-virt", .data = &sm6115_mmnrt_virt },
	{ .compatible = "qcom,sm6115-snoc", .data = &sm6115_snoc },
	{ },
};
MODULE_DEVICE_TABLE(of, sm6115_qnoc_of_match);

static struct platform_driver sm6115_qnoc_driver = {
	.probe = qnoc_probe,
	.remove = qnoc_remove,
	.driver = {
		.name = "qnoc-sm6115",
		.of_match_table = sm6115_qnoc_of_match,
	},
};
module_platform_driver(sm6115_qnoc_driver);

MODULE_DESCRIPTION("Qualcomm SM6115 NoC driver");
MODULE_LICENSE("GPL v2");
