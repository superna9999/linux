/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2024 Linaro Ltd.
 */

#ifndef __PCIE_PWRSEQ_H__
#define __PCIE_PWRSEQ_H__

#include <linux/notifier.h>

struct device;

struct pcie_pwrseq {
	struct notifier_block nb;
	struct device *dev;
	struct device_link *link;
};

int pcie_pwrseq_device_enable(struct pcie_pwrseq *pwrseq);
void pcie_pwrseq_device_disable(struct pcie_pwrseq *pwrseq);
int devm_pcie_pwrseq_device_enable(struct device *dev,
				   struct pcie_pwrseq *pwrseq);

#endif /* __PCIE_PWRSEQ_H__ */
