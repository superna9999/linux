// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Linaro Ltd.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/slab.h>
#include <linux/qcom_scm.h>

struct qcom_mpss_dsm_mem {
	phys_addr_t addr;
	phys_addr_t size;

	unsigned int perms;
};

static int qcom_mpss_dsm_mem_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct qcom_scm_vmperm perm;
	struct reserved_mem *rmem;
	struct qcom_mpss_dsm_mem *mpss_dsm_mem;
	int ret;

	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;

	rmem = of_reserved_mem_lookup(node);
	if (!rmem) {
		dev_err(&pdev->dev, "failed to acquire memory region\n");
		return -EINVAL;
	}

	mpss_dsm_mem = kzalloc(sizeof(*mpss_dsm_mem), GFP_KERNEL);
	if (!mpss_dsm_mem)
		return -ENOMEM;

	mpss_dsm_mem->addr = rmem->base;
	mpss_dsm_mem->size = rmem->size;

	perm.vmid = QCOM_SCM_VMID_MSS_MSA;
	perm.perm = QCOM_SCM_PERM_RW;

	mpss_dsm_mem->perms = BIT(QCOM_SCM_VMID_HLOS);
	ret = qcom_scm_assign_mem(mpss_dsm_mem->addr, mpss_dsm_mem->size,
				  &mpss_dsm_mem->perms, &perm, 1);
	if (ret < 0) {
		dev_err(&pdev->dev, "assign memory failed\n");
		return ret;
	}

	dev_set_drvdata(&pdev->dev, mpss_dsm_mem);

	return 0;
}

static int qcom_mpss_dsm_mem_remove(struct platform_device *pdev)
{
	struct qcom_mpss_dsm_mem *mpss_dsm_mem = dev_get_drvdata(&pdev->dev);
	struct qcom_scm_vmperm perm;

	perm.vmid = QCOM_SCM_VMID_HLOS;
	perm.perm = QCOM_SCM_PERM_RW;

	qcom_scm_assign_mem(mpss_dsm_mem->addr, mpss_dsm_mem->size,
			    &mpss_dsm_mem->perms, &perm, 1);

	return 0;
}

static const struct of_device_id qcom_mpss_dsm_mem_of_match[] = {
	{ .compatible = "qcom,mpss-dsm-mem" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_mpss_dsm_mem_of_match);

static struct platform_driver qcom_mpss_dsm_mem_driver = {
	.probe = qcom_mpss_dsm_mem_probe,
	.remove = qcom_mpss_dsm_mem_remove,
	.driver  = {
		.name  = "qcom_mpss_dsm_mem",
		.of_match_table = qcom_mpss_dsm_mem_of_match,
	},
};

module_platform_driver(qcom_mpss_dsm_mem_driver);

MODULE_AUTHOR("Linaro Ltd");
MODULE_DESCRIPTION("Qualcomm DSM memory driver");
MODULE_LICENSE("GPL");
