/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2024 Linaro Ltd.
 */

#ifndef __PCI_PWRSEQ_H__
#define __PCI_PWRSEQ_H__

#include <linux/notifier.h>

struct device;

struct pci_pwrseq {
	struct notifier_block nb;
	struct device *dev;
	struct device_link *link;
};

int pci_pwrseq_device_enable(struct pci_pwrseq *pwrseq);
void pci_pwrseq_device_disable(struct pci_pwrseq *pwrseq);
int devm_pci_pwrseq_device_enable(struct device *dev,
				  struct pci_pwrseq *pwrseq);

#endif /* __PCI_PWRSEQ_H__ */
