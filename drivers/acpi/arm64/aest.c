// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Linaro Limited
 *
 * Based on iort.c:
 * 	Copyright (C) 2016, Semihalf
 *	Author: Tomasz Nowicki <tn@semihalf.com>
 *
 * Based on Ampere Computing's unmerged patches
 * 	(no copyright notice)
 */

#define pr_fmt(fmt)	"ACPI: AEST: " fmt

#include <linux/acpi_aest.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/iommu.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-map-ops.h>

#include <acpi/actbl2.h>

#define AEST_TYPE_MASK(type)	(1 << (type))
#define AEST_MSI_TYPE		(1 << ACPI_AEST_NODE_ITS_GROUP)
#define AEST_IOMMU_TYPE		((1 << ACPI_AEST_NODE_SMMU) |	\
				(1 << ACPI_AEST_NODE_SMMU_V3))

struct aest_fwnode {
	struct list_head list;
	struct acpi_aest_node *aest_node;
	struct fwnode_handle *fwnode;
};
static LIST_HEAD(aest_fwnode_list);
static DEFINE_SPINLOCK(aest_fwnode_lock);

/**
 * aest_set_fwnode() - Create aest_fwnode and use it to register
 *		       iommu data in the aest_fwnode_list
 *
 * @aest_node: AEST table node associated with the IOMMU
 * @fwnode: fwnode associated with the AEST node
 *
 * Returns: 0 on success
 *          <0 on failure
 */
static inline int aest_set_fwnode(struct acpi_aest_node *aest_node,
				  struct fwnode_handle *fwnode)
{
	struct aest_fwnode *np;

	np = kzalloc(sizeof(struct aest_fwnode), GFP_ATOMIC);

	if (WARN_ON(!np))
		return -ENOMEM;

	INIT_LIST_HEAD(&np->list);
	np->aest_node = aest_node;
	np->fwnode = fwnode;

	spin_lock(&aest_fwnode_lock);
	list_add_tail(&np->list, &aest_fwnode_list);
	spin_unlock(&aest_fwnode_lock);

	return 0;
}

/**
 * aest_get_fwnode() - Retrieve fwnode associated with an AEST node
 *
 * @node: AEST table node to be looked-up
 *
 * Returns: fwnode_handle pointer on success, NULL on failure
 */
static inline struct fwnode_handle *aest_get_fwnode(
			struct acpi_aest_node *node)
{
	struct aest_fwnode *curr;
	struct fwnode_handle *fwnode = NULL;

	spin_lock(&aest_fwnode_lock);
	list_for_each_entry(curr, &aest_fwnode_list, list) {
		if (curr->aest_node == node) {
			fwnode = curr->fwnode;
			break;
		}
	}
	spin_unlock(&aest_fwnode_lock);

	return fwnode;
}

/**
 * aest_delete_fwnode() - Delete fwnode associated with an AEST node
 *
 * @node: AEST table node associated with fwnode to delete
 */
static inline void aest_delete_fwnode(struct acpi_aest_node *node)
{
	struct aest_fwnode *curr, *tmp;

	spin_lock(&aest_fwnode_lock);
	list_for_each_entry_safe(curr, tmp, &aest_fwnode_list, list) {
		if (curr->aest_node == node) {
			list_del(&curr->list);
			kfree(curr);
			break;
		}
	}
	spin_unlock(&aest_fwnode_lock);
}

typedef acpi_status (*aest_find_node_callback)
	(struct acpi_aest_node *node, void *context);

/* Root pointer to the mapped AEST table */
static struct acpi_table_header *aest_table;

static void __init acpi_aest_register_irq(int hwirq, const char *name,
					  int trigger,
					  struct resource *res)
{
	int irq = acpi_register_gsi(NULL, hwirq, trigger, ACPI_ACTIVE_HIGH);

	if (irq <= 0) {
		pr_err("Could not register gsi hwirq %d name [%s]\n", hwirq, name);
		return;
	}

	res->start = irq;
	res->end = irq;
	res->flags = IORESOURCE_IRQ;
	res->name = name;
}

#define RAS_ERR_REC_GRP_WIDTH			0x1000 /* Arm ARM RAS Supplement Chapter 4.1 / (RTPFWF) */
#define RAS_SINGLE_ERR_REC_WIDTH		0x40
#define AEST_INTR_TYPE_LEVEL			0b1
static int __init aest_init_node(acpi_aest_node *node, struct resource *r, acpi_aest_node *data)
{
	int i, num_res = 0;
	u8 intr_type;
	u64 data_sz;

	memcpy(&data->hdr, &node->hdr, sizeof(struct acpi_aest_hdr));

	switch (data->hdr.type) {
	case ACPI_AEST_PROCESSOR_ERROR_NODE:
		data_sz = sizeof(node->data.processor.proc);
		memcpy(&data->data.processor.proc, &node->data, sizeof(acpi_aest_processor));
		switch (data->data.processor.proc.resource_type) {
		case ACPI_AEST_CACHE_RESOURCE:
			data_sz += sizeof(acpi_aest_processor_cache);
			memcpy(&data->data.processor.proc_sub, &node->data.processor.proc,
			       sizeof(acpi_aest_processor_cache));
			break;
		case ACPI_AEST_TLB_RESOURCE:
			data_sz += sizeof(acpi_aest_processor_tlb);
			memcpy(&data->data.processor.proc_sub, &node->data.processor.proc,
			       sizeof(acpi_aest_processor_tlb));
			break;
		case ACPI_AEST_GENERIC_RESOURCE:
			data_sz += sizeof(acpi_aest_processor_generic);
			memcpy(&data->data.processor.proc_sub, &node->data.processor.proc,
			       sizeof(acpi_aest_processor_generic));
			break;
		default:
			/* Other values are reserved */
			return -EINVAL;
		}
		break;

	case ACPI_AEST_MEMORY_ERROR_NODE:
		data_sz = sizeof(node->data.mem);
		break;

	case ACPI_AEST_SMMU_ERROR_NODE:
		data_sz = sizeof(node->data.smmu);
		break;

	case ACPI_AEST_VENDOR_ERROR_NODE:
		data_sz = sizeof(node->data.vendor);
		break;

	case ACPI_AEST_GIC_ERROR_NODE:
		data_sz = sizeof(node->data.gic);
		break;

	default:
		/* Other values are reserved */
		return -EINVAL;
	}

	/* Copy the data field into the union (except for proc nodes which were handled already) */
	if (data->hdr.type != ACPI_AEST_PROCESSOR_ERROR_NODE)
		memcpy(&data->data, &node->data, data_sz);

	/* The data field can have different sizes, so we need some clever pointer math here */
	memcpy(&data->intf, (void *)((u64)&node->data + data_sz),
	       sizeof(acpi_aest_node_interface));

	data_sz += sizeof(acpi_aest_node_interface);

	memcpy(&data->intr, (void *)((u64)&node->data + data_sz),
	       data->hdr.node_interrupt_count * sizeof(acpi_aest_node_interrupt));

	if (node->intf.address) {
		if (node->intf.type == ACPI_AEST_NODE_SYSTEM_REGISTER)
			pr_err("Faulty table! MMIO address specified for a SR interface!\n");

		r[num_res].start = node->intf.address;
		/*
		 * TODO: The 4.3.1.4 Memory-mapped single error record view might be repeated in
		 * the control registers for a memory-mapped component that implements a small
		 * number of error records. Each error record has its own IMPLEMENTATION DEFINED
		 * base within the control registers of the component.
		 *  - Arm ARM RAS Supplement Chapter 4.1 / (RDHYDC)
		 */

		/* BIG TODO: what if multiple nodes point to the same error record group? :// */

		//TODO: this if condition is a guesstimate
		if (node->intf.error_record_count == 1)
			r[num_res].end = node->intf.address + RAS_SINGLE_ERR_REC_WIDTH - 1;
		else
			r[num_res].end = node->intf.address + RAS_ERR_REC_GRP_WIDTH - 1;
		r[num_res].flags = IORESOURCE_MEM;
		num_res++;
	}

	num_res += node->hdr.node_interrupt_count;

	/* Register interrupts, they will be requested in the EDAC driver later. */
	// TODO: handle MSIs?
	for (i = 0; i < node->hdr.node_interrupt_count; i ++) {
		intr_type = node->intr[i].flags = AEST_INTR_TYPE_LEVEL ?
						  ACPI_LEVEL_SENSITIVE : ACPI_EDGE_SENSITIVE;
		if (node->intr[i].type == ACPI_AEST_NODE_FAULT_HANDLING)
			acpi_aest_register_irq(node->intr[i].gsiv, "fault",
					       intr_type, &r[num_res++]);
		if (node->intr[i].type == ACPI_AEST_NODE_ERROR_RECOVERY)
			acpi_aest_register_irq(node->intr[i].gsiv, "err",
					       intr_type, &r[num_res++]);
		else
			pr_err("Faulty table! Illegal interrupt type %u\n", node->intr[i].type);
	}

	return num_res;
}

#define ARM_RAS_RES_COUNT 3 /* Error interrupt + Fault interrupt + MMIO base */
/**
 * aest_add_platform_device() - Allocate a platform device for AEST node
 * @node: Pointer to device ACPI AEST node
 * @ops: Pointer to AEST device config struct
 *
 * Returns: 0 on success, <0 failure
 */
static int __init aest_add_platform_device(acpi_aest_node *node)
{
	struct fwnode_handle *fwnode;
	struct platform_device *pdev;
	acpi_aest_node *data;
	struct resource *r;
	int ret, count;

	pdev = platform_device_alloc("arm-ras-edac", PLATFORM_DEVID_AUTO);
	if (!pdev)
		return -ENOMEM;

	r = kcalloc(ARM_RAS_RES_COUNT, sizeof(*r), GFP_KERNEL);
	if (!r) {
		ret = -ENOMEM;
		goto dev_put;
	}

	data = kzalloc(sizeof(data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	count = aest_init_node(node, r, data);

	ret = platform_device_add_resources(pdev, r, count);
	/*
	 * Resources are duplicated in platform_device_add_resources,
	 * free their allocated memory
	 */
	kfree(r);

	if (ret)
		goto dev_put;

	ret = platform_device_add_data(pdev, &node, sizeof(node));
	if (ret)
		goto dev_put;

	fwnode = aest_get_fwnode(node);
	if (!fwnode) {
		ret = -ENODEV;
		goto dev_put;
	}

	pdev->dev.fwnode = fwnode;

	ret = platform_device_add(pdev);

	return ret;

dev_put:
	platform_device_put(pdev);

	return ret;
}

static void __init aest_init_platform_devices(void)
{
	acpi_aest_node *aest_node, *aest_end;
	struct acpi_table_aest *aest;
	struct fwnode_handle *fwnode;
	int i, ret, tbl_sz, node_count = 0;

	/*
	 * aest_table and aest both point to the start of AEST table, but
	 * have different struct types
	 */
	aest = (struct acpi_table_aest *)aest_table;

	/* Get the first AEST node */
	aest_node = ACPI_ADD_PTR(struct acpi_aest_node, aest, sizeof(struct acpi_aest_hdr));

	/* We aren't given the number of nodes, so we gotta figure them out on our own.. */
	tbl_sz = aest->header.length;
	aest_node = (acpi_aest_node *) aest->node_array;

	/* Subtract the constant size of the AEST header */
	tbl_sz -= sizeof(struct acpi_aest_hdr);

	while (tbl_sz > 0) {
		/* Subtract the length of the current node from the total size */
		tbl_sz -= aest_node->hdr.length;

		/* Jump to the next node */
		aest_node += aest_node->hdr.length;
		node_count++;

		if (tbl_sz < 0)
			pr_err("Faulty table! Header and nodes lengths don't sum up!\n");
	}
	pr_info("Found %d AEST nodes!\n", node_count);

	/* Now we can go back to the usual business */

	/* Get the first AEST node, again */
	aest_node = ACPI_ADD_PTR(struct acpi_aest_node, aest, sizeof(struct acpi_aest_hdr));

	/* Pointer to the end of the AEST table */
	aest_end = ACPI_ADD_PTR(struct acpi_aest_node, aest, aest_table->length);

	for (i = 0; i < node_count; i++) {
		if (aest_node + aest_node->hdr.length > aest_end) {
			pr_err("AEST node pointer overflows, bad table\n");
			return;
		}

		fwnode = acpi_alloc_fwnode_static();
		if (!fwnode)
			return;

		aest_set_fwnode(aest_node, fwnode);

		ret = aest_add_platform_device(aest_node);
		if (ret) {
			aest_delete_fwnode(aest_node);
			acpi_free_fwnode_static(fwnode);
			return;
		}

		aest_node = ACPI_ADD_PTR(struct acpi_aest_node, aest_node, aest_node->hdr.length);
	}
}

void __init acpi_aest_init(void)
{
	acpi_status status;

	status = acpi_get_table(ACPI_SIG_AEST, 0, &aest_table);
	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND) {
			const char *msg = acpi_format_exception(status);

			pr_err("Failed to get table, %s\n", msg);
		}

		return;
	}

	aest_init_platform_devices();

	acpi_put_table(aest_table);
}
