// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2022, Linaro Limited
 */

#include <linux/device.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/overflow.h>

#include <linux/usb/repeater.h>

static LIST_HEAD(usb_repeater_list);
static DEFINE_SPINLOCK(usb_repeater_lock);

/**
 * usb_put_repeater - release the USB repeater
 * @rptr: the repeater returned by usb_get_repeater()
 *
 * Releases a refcount the caller received from usb_get_repeater().
 *
 * For use by USB PHY drivers.
 */
void usb_put_repeater(struct usb_repeater *rptr)
{
	if (rptr) {
		put_device(rptr->dev);
		if (rptr->dev->driver && rptr->dev->driver->owner) {
			struct module *owner = rptr->dev->driver->owner;

			module_put(owner);
		}
	}
}
EXPORT_SYMBOL_GPL(usb_put_repeater);

static struct usb_repeater *of_usb_find_repeater(struct device_node *node)
{
	struct usb_repeater *rptr;

	if (!of_device_is_available(node))
		return ERR_PTR(-ENODEV);

	list_for_each_entry(rptr, &usb_repeater_list, head) {
		if (node != rptr->dev->of_node)
			continue;

		return rptr;
	}

	return ERR_PTR(-EPROBE_DEFER);
}

static void devm_usb_repeater_release(struct device *dev, void *res)
{
	struct usb_repeater *rptr = *(struct usb_repeater **)res;

	usb_put_repeater(rptr);
}

/**
 * devm_usb_get_repeater_by_node - find the USB repeater by device_node
 * @dev: device that requests this USB repeater
 * @node: the device_node for the USB repeater device.
 * @nb: a notifier_block to register with the USB repeater.
 *
 * Returns the USB repeater driver associated with the given device_node,
 * after getting a refcount to it, -ENODEV if there is no such repeater or
 * -EPROBE_DEFER if the device is not yet loaded. While at that, it
 * also associates the device with
 * the repeater using devres. On driver detach, release function is invoked
 * on the devres data, then, devres data is freed.
 *
 * For use by peripheral drivers for devices related to a repeater,
 * such as a charger.
 */
struct  usb_repeater *devm_usb_get_repeater_by_node(struct device *dev,
					  struct device_node *node,
					  struct notifier_block *nb)
{
	struct usb_repeater *rptr = ERR_PTR(-ENOMEM);
	struct usb_repeater *rptr_devm;
	unsigned long flags;

	rptr_devm = devres_alloc(devm_usb_repeater_release,
					sizeof(*rptr_devm), GFP_KERNEL);
	if (!rptr_devm) {
		dev_dbg(dev, "failed to allocate memory for devres\n");
		goto err0;
	}

	spin_lock_irqsave(&usb_repeater_lock, flags);

	rptr = of_usb_find_repeater(node);
	if (IS_ERR(rptr)) {
		devres_free(rptr_devm);
		goto err0;
	}

	if (!try_module_get(rptr->dev->driver->owner)) {
		rptr = ERR_PTR(-ENODEV);
		devres_free(rptr_devm);
		goto err0;
	}
	devres_add(dev, rptr_devm);

	get_device(rptr->dev);
err0:
	spin_unlock_irqrestore(&usb_repeater_lock, flags);

	return rptr;
}
EXPORT_SYMBOL_GPL(devm_usb_get_repeater_by_node);

/**
 * devm_usb_get_repeater_by_phandle - find the USB repeater by phandle
 * @dev: device that requests this repeater
 * @phandle: name of the property holding the repeater phandle value
 * @index: the index of the repeater
 *
 * Returns the USB repeater driver associated with the given phandle value,
 * after getting a refcount to it, -ENODEV if there is no such repeater or
 * -EPROBE_DEFER if there is a phandle to the repeater, but the device is
 * not yet loaded. While at that, it also associates the device with
 * the repeater using devres. On driver detach, release function is invoked
 * on the devres data, then, devres data is freed.
 *
 * For use by USB PHY drivers.
 */
struct usb_repeater *devm_usb_get_repeater_by_phandle(struct device *dev,
	const char *phandle, u8 index)
{
	struct device_node *node;
	struct usb_repeater *rptr;

	if (!dev->of_node) {
		dev_dbg(dev, "device does not have a device node entry\n");
		return ERR_PTR(-EINVAL);
	}

	node = of_parse_phandle(dev->of_node, phandle, index);
	if (!node) {
		dev_dbg(dev, "failed to get %s phandle in %pOF node\n", phandle,
			dev->of_node);
		return ERR_PTR(-ENODEV);
	}
	rptr = devm_usb_get_repeater_by_node(dev, node, NULL);
	of_node_put(node);
	return rptr;
}
EXPORT_SYMBOL_GPL(devm_usb_get_repeater_by_phandle);

/**
 * usb_add_repeater_dev - declare the USB repeater
 * @rptr: the USB repeater to be used; or NULL
 *
 * This call is exclusively for use by repeater drivers to
 * register the device to allow the USB phy driver to control it
 * via repeater specific ops.
 */
int usb_add_repeater_dev(struct usb_repeater *rptr)
{
	unsigned long flags;

	if (!rptr->dev) {
		dev_err(rptr->dev, "no device provided for repeater\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&usb_repeater_lock, flags);
	list_add_tail(&rptr->head, &usb_repeater_list);
	spin_unlock_irqrestore(&usb_repeater_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(usb_add_repeater_dev);

/**
 * usb_remove_repeater - remove the USB repeater
 * @rptr: the USB Repeater to be removed;
 *
 * This reverts the effects of usb_add_repeater_dev
 */
void usb_remove_repeater_dev(struct usb_repeater *rptr)
{
	unsigned long flags;

	spin_lock_irqsave(&usb_repeater_lock, flags);
	if (rptr)
		list_del(&rptr->head);
	spin_unlock_irqrestore(&usb_repeater_lock, flags);
}
EXPORT_SYMBOL_GPL(usb_remove_repeater_dev);

MODULE_DESCRIPTION("USB Repeater Framework");
MODULE_LICENSE("GPL");
