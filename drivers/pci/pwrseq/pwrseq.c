// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024 Linaro Ltd.
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci-pwrseq.h>
#include <linux/property.h>
#include <linux/slab.h>

static int pci_pwrseq_notify(struct notifier_block *nb, unsigned long action,
			     void *data)
{
	struct pci_pwrseq *pwrseq = container_of(nb, struct pci_pwrseq, nb);
	struct device *dev = data;

	if (dev_fwnode(dev) != dev_fwnode(pwrseq->dev))
		return NOTIFY_DONE;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		device_set_of_node_from_dev(dev, pwrseq->dev);
		break;
	case BUS_NOTIFY_BOUND_DRIVER:
		pwrseq->link = device_link_add(dev, pwrseq->dev,
					       DL_FLAG_AUTOREMOVE_CONSUMER);
		if (!pwrseq->link)
			dev_err(pwrseq->dev, "Failed to add device link\n");
		break;
	case BUS_NOTIFY_UNBOUND_DRIVER:
		device_link_del(pwrseq->link);
		break;
	}

	return NOTIFY_DONE;
}

int pci_pwrseq_device_enable(struct pci_pwrseq *pwrseq)
{
	if (!pwrseq->dev)
		return -ENODEV;

	pwrseq->nb.notifier_call = pci_pwrseq_notify;
	bus_register_notifier(&pci_bus_type, &pwrseq->nb);

	pci_lock_rescan_remove();
	pci_rescan_bus(to_pci_dev(pwrseq->dev->parent)->bus);
	pci_unlock_rescan_remove();

	return 0;
}
EXPORT_SYMBOL_GPL(pci_pwrseq_device_enable);

void pci_pwrseq_device_disable(struct pci_pwrseq *pwrseq)
{
	bus_unregister_notifier(&pci_bus_type, &pwrseq->nb);
}
EXPORT_SYMBOL_GPL(pci_pwrseq_device_disable);

static void devm_pci_pwrseq_device_disable(void *data)
{
	struct pci_pwrseq *pwrseq = data;

	pci_pwrseq_device_disable(pwrseq);
}

int devm_pci_pwrseq_device_enable(struct device *dev,
				  struct pci_pwrseq *pwrseq)
{
	int ret;

	ret = pci_pwrseq_device_enable(pwrseq);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, devm_pci_pwrseq_device_disable,
					pwrseq);
}
EXPORT_SYMBOL_GPL(devm_pci_pwrseq_device_enable);
