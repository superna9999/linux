/*
 * drivers/irqchip/irq-rps.c
 *
 * Copyright (C) 2009 Oxford Semiconductor Ltd
 * Copyright (C) 2013 Ma Haijun <mahaijuns@gmail.com>
 * Copyright (C) 2016 Neil Armstrong <narmstrong@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/version.h>
#include <linux/irqchip.h>

#include <asm/exception.h>

struct rps_chip_data {
	void __iomem *base;
	struct irq_domain *domain;
} rps_data;

enum {
	RPS_IRQ_COUNT = 32,

	RPS_STATUS = 0,
	RPS_RAW_STATUS = 4,
	RPS_UNMASK = 8,
	RPS_MASK = 0xc,
};

/* Routines to acknowledge, disable and enable interrupts */
static void rps_mask_irq(struct irq_data *d)
{
	u32 mask = BIT(d->hwirq);

	iowrite32(mask, rps_data.base + RPS_MASK);
}

static void rps_unmask_irq(struct irq_data *d)
{
	u32 mask = BIT(d->hwirq);

	iowrite32(mask, rps_data.base + RPS_UNMASK);
}

static void rps_ack_irq(struct irq_data *d)
{
	/* NOP */
}

static void __exception_irq_entry handle_irq(struct pt_regs *regs)
{
	u32 irqstat;
	int hwirq;

	irqstat = ioread32(rps_data.base + RPS_STATUS);
	hwirq = __ffs(irqstat);

	do {
		handle_IRQ(irq_find_mapping(rps_data.domain, hwirq), regs);

		irqstat = ioread32(rps_data.base + RPS_STATUS);
		hwirq = __ffs(irqstat);
	} while (irqstat);
}

int __init rps_of_init(struct device_node *node, struct device_node *parent)
{
	int ret;
	struct irq_chip_generic *gc;

	if (WARN_ON(!node))
		return -ENODEV;

	rps_data.base = of_iomap(node, 0);
	WARN(!rps_data.base, "unable to map rps registers\n");

	rps_data.domain = irq_domain_add_linear(node, RPS_IRQ_COUNT,
						&irq_generic_chip_ops,
						NULL);
	if (!rps_data.domain) {
		pr_err("%s: could add irq domain\n",
		       node->full_name);
		return -ENOMEM;
	}

	ret = irq_alloc_domain_generic_chips(rps_data.domain, RPS_IRQ_COUNT, 1,
					     "RPS", handle_level_irq,
					     0, 0, IRQ_GC_INIT_NESTED_LOCK);
	if (ret) {
		pr_err("%s: could not allocate generic chip\n",
		       node->full_name);
		irq_domain_remove(rps_data.domain);
		return -EINVAL;
	}

	gc = irq_get_domain_generic_chip(rps_data.domain, 0);
	gc->chip_types[0].chip.irq_ack = rps_ack_irq;
	gc->chip_types[0].chip.irq_mask = rps_mask_irq;
	gc->chip_types[0].chip.irq_unmask = rps_unmask_irq;

	/* Disable all IRQs */
	iowrite32(~0, rps_data.base + RPS_MASK);

	set_handle_irq(handle_irq);

	pr_info("Registered %d rps interrupts\n", RPS_IRQ_COUNT);

	return 0;
}

IRQCHIP_DECLARE(nas782x, "plxtech,nas782x-rps", rps_of_init);
