/*
 * drivers/dma/oxnas_adma.c
 *
 * Copyright (C) 2016 Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2008 Oxford Semiconductor Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>                                                         
#include <linux/module.h>                                                       
#include <linux/delay.h>                                                        
#include <linux/mutex.h>  
#include <linux/semaphore.h>  
#include <linux/dma-mapping.h>                                                  
#include <linux/spinlock.h>                                                     
#include <linux/interrupt.h>                                                    
#include <linux/platform_device.h>                                              
#include <linux/memory.h>
#include <linux/slab.h>
#include <linux/of.h>                                                           
#include <linux/of_address.h>                                                   
#include <linux/of_irq.h>
#include <linux/of_dma.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>

#include "dmaengine.h"
#include "virt-dma.h"

/* Normal (non-SG) registers */
#define DMA_REGS_PER_CHANNEL 8

#define DMA_CTRL_STATUS      0x0
#define DMA_BASE_SRC_ADR     0x4
#define DMA_BASE_DST_ADR     0x8
#define DMA_BYTE_CNT         0xC
#define DMA_CURRENT_SRC_ADR  0x10
#define DMA_CURRENT_DST_ADR  0x14
#define DMA_CURRENT_BYTE_CNT 0x18
#define DMA_INTR_ID          0x1C
#define DMA_INTR_CLEAR_REG   (DMA_CURRENT_SRC_ADR)

/* 8 quad-sized registers per channel arranged contiguously */
#define DMA_CALC_REG_ADR(channel, register) (((channel) << 5) + (register))

#define DMA_CTRL_STATUS_FAIR_SHARE_ARB            (1 << 0)
#define DMA_CTRL_STATUS_IN_PROGRESS               (1 << 1)
#define DMA_CTRL_STATUS_SRC_DREQ_MASK             (0x0000003C)
#define DMA_CTRL_STATUS_SRC_DREQ_SHIFT            2
#define DMA_CTRL_STATUS_DEST_DREQ_MASK            (0x000003C0)
#define DMA_CTRL_STATUS_DEST_DREQ_SHIFT           6
#define DMA_CTRL_STATUS_INTR                      (1 << 10)
#define DMA_CTRL_STATUS_NXT_FREE                  (1 << 11)
#define DMA_CTRL_STATUS_RESET                     (1 << 12)
#define DMA_CTRL_STATUS_DIR_MASK                  (0x00006000)
#define DMA_CTRL_STATUS_DIR_SHIFT                 13
#define DMA_CTRL_STATUS_SRC_ADR_MODE              (1 << 15)
#define DMA_CTRL_STATUS_DEST_ADR_MODE             (1 << 16)
#define DMA_CTRL_STATUS_TRANSFER_MODE_A           (1 << 17)
#define DMA_CTRL_STATUS_TRANSFER_MODE_B           (1 << 18)
#define DMA_CTRL_STATUS_SRC_WIDTH_MASK            (0x00380000)
#define DMA_CTRL_STATUS_SRC_WIDTH_SHIFT           19
#define DMA_CTRL_STATUS_DEST_WIDTH_MASK           (0x01C00000)
#define DMA_CTRL_STATUS_DEST_WIDTH_SHIFT          22
#define DMA_CTRL_STATUS_PAUSE                     (1 << 25)
#define DMA_CTRL_STATUS_INTERRUPT_ENABLE          (1 << 26)
#define DMA_CTRL_STATUS_SOURCE_ADDRESS_FIXED      (1 << 27)
#define DMA_CTRL_STATUS_DESTINATION_ADDRESS_FIXED (1 << 28)
#define DMA_CTRL_STATUS_STARVE_LOW_PRIORITY       (1 << 29)
#define DMA_CTRL_STATUS_INTR_CLEAR_ENABLE         (1 << 30)

#define DMA_BYTE_CNT_MASK                         ((1 << 21) - 1)
#define DMA_BYTE_CNT_INC4_SET_MASK                (1 << 28)
#define DMA_BYTE_CNT_HPROT_MASK                   (1 << 29)
#define DMA_BYTE_CNT_WR_EOT_MASK                  (1 << 30)
#define DMA_BYTE_CNT_RD_EOT_MASK                  (1 << 31)

#define DMA_INTR_ID_GET_NUM_CHANNELS(reg_contents) (((reg_contents) >> 16) & 0xFF)
#define DMA_INTR_ID_GET_VERSION(reg_contents)      (((reg_contents) >> 24) & 0xFF)
#define DMA_INTR_ID_INT_BIT         0
#define DMA_INTR_ID_INT_NUM_BITS    (MAX_OXNAS_DMA_CHANNELS)
#define DMA_INTR_ID_INT_MASK        (((1 << DMA_INTR_ID_INT_NUM_BITS) - 1) << DMA_INTR_ID_INT_BIT)

#define DMA_HAS_V4_INTR_CLEAR(version) ((version) > 3)

/* H/W scatter gather controller registers */
#define OXNAS_DMA_NUM_SG_REGS 4

#define DMA_SG_CONTROL  0x0
#define DMA_SG_STATUS   0x04
#define DMA_SG_REQ_PTR  0x08
#define DMA_SG_RESETS   0x0C

#define DMA_SG_CALC_REG_ADR(channel, register) (((channel) << 4) + (register))

/* SG DMA controller control register field definitions */
#define DMA_SG_CONTROL_START_BIT            0
#define DMA_SG_CONTROL_QUEUING_ENABLE_BIT   1
#define DMA_SG_CONTROL_HBURST_ENABLE_BIT    2

/* SG DMA controller status register field definitions */
#define DMA_SG_STATUS_ERROR_CODE_BIT        0
#define DMA_SG_STATUS_ERROR_CODE_NUM_BITS   6
#define DMA_SG_STATUS_BUSY_BIT              7

/* SG DMA controller sub-block resets register field definitions */
#define DMA_SG_RESETS_CONTROL_BIT 0
#define DMA_SG_RESETS_ARBITER_BIT 1
#define DMA_SG_RESETS_AHB_BIT	   2

// oxnas_dma_sg_info_t qualifier field definitions
#define OXNAS_DMA_SG_QUALIFIER_BIT      0
#define OXNAS_DMA_SG_QUALIFIER_NUM_BITS 16
#define OXNAS_DMA_SG_DST_EOT_BIT        16
#define OXNAS_DMA_SG_DST_EOT_NUM_BITS   2
#define OXNAS_DMA_SG_SRC_EOT_BIT        20
#define OXNAS_DMA_SG_SRC_EOT_NUM_BITS   2
#define OXNAS_DMA_SG_CHANNEL_BIT        24
#define OXNAS_DMA_SG_CHANNEL_NUM_BITS   8

#define OXNAS_DMA_ADR_MASK       (0x3FFFFFFF)
#define OXNAS_DMA_MAX_TRANSFER_LENGTH ((1 << 21) - 1)

/* The available buses to which the DMA controller is attached */
typedef enum oxnas_dma_transfer_bus
{
    OXNAS_DMA_SIDE_A,
    OXNAS_DMA_SIDE_B
} oxnas_dma_transfer_bus_t;

/* Direction of data flow between the DMA controller's pair of interfaces */
typedef enum oxnas_dma_transfer_direction
{
    OXNAS_DMA_A_TO_A,
    OXNAS_DMA_B_TO_A,
    OXNAS_DMA_A_TO_B,
    OXNAS_DMA_B_TO_B
} oxnas_dma_transfer_direction_t;

/* The available data widths */
typedef enum oxnas_dma_transfer_width
{
    OXNAS_DMA_TRANSFER_WIDTH_8BITS,
    OXNAS_DMA_TRANSFER_WIDTH_16BITS,
    OXNAS_DMA_TRANSFER_WIDTH_32BITS
} oxnas_dma_transfer_width_t;

/* The mode of the DMA transfer */
typedef enum oxnas_dma_transfer_mode
{
    OXNAS_DMA_TRANSFER_MODE_SINGLE,
    OXNAS_DMA_TRANSFER_MODE_BURST
} oxnas_dma_transfer_mode_t;

/* The available transfer targets */
typedef enum oxnas_dma_dreq
{
    OXNAS_DMA_DREQ_PATA     = 0,
    OXNAS_DMA_DREQ_SATA     = 0,
    OXNAS_DMA_DREQ_DPE_RX   = 1,
    OXNAS_DMA_DREQ_DPE_TX   = 2,
    OXNAS_DMA_DREQ_AUDIO_TX = 5,
    OXNAS_DMA_DREQ_AUDIO_RX = 6,    
    OXNAS_DMA_DREQ_MEMORY   = 15
} oxnas_dma_dreq_t;

#define MAX_OXNAS_DMA_CHANNELS 	5
#define MAX_OXNAS_SG_ENTRIES	512

/* Will be exchanged with SG DMA controller */
typedef struct oxnas_dma_sg_entry {
	dma_addr_t                 data_addr;   // The physical address of the buffer described by this descriptor
	unsigned long              data_length; // The length of the buffer described by this descriptor
	dma_addr_t                 p_next_entry; // The physical address of the next descriptor
	struct oxnas_dma_sg_entry *next_entry;   // The virtual address of the next descriptor
	dma_addr_t                 this_paddr;  // The physical address of this descriptor
	struct list_head	   entry;  /* Linked list entry */
} __attribute ((aligned(4),packed)) oxnas_dma_sg_entry_t;

/* Will be exchanged with SG DMA controller */
typedef struct oxnas_dma_sg_info {
	unsigned long         qualifier;
	unsigned long         control;
	dma_addr_t            p_srcEntries; // The physical address of the first source SG descriptor
	dma_addr_t            p_dstEntries; // The physical address of the first destination SG descriptor
	oxnas_dma_sg_entry_t *srcEntries; // The virtual address of the first source SG descriptor
	oxnas_dma_sg_entry_t *dstEntries; // The virtual address of the first destination SG descriptor
} __attribute ((aligned(4),packed)) oxnas_dma_sg_info_t;

typedef struct oxnas_dma_sg_data {
	oxnas_dma_sg_entry_t entries[MAX_OXNAS_SG_ENTRIES];
	oxnas_dma_sg_info_t infos[MAX_OXNAS_DMA_CHANNELS];
} __attribute ((aligned(4))) oxnas_dma_sg_data_t;

typedef struct oxnas_dma_device oxnas_dma_device_t;
typedef struct oxnas_dma_channel oxnas_dma_channel_t;

enum {
	OXNAS_DMA_TYPE_SIMPLE = 0,
	OXNAS_DMA_TYPE_SG,
};

typedef struct oxnas_dma_desc {
	struct virt_dma_desc vd;
	oxnas_dma_channel_t *channel;
	unsigned long ctrl;
	unsigned long len;
	dma_addr_t src_adr;
	dma_addr_t dst_adr;
	unsigned type;
	oxnas_dma_sg_info_t sg_info;
	unsigned entries;
	struct list_head sg_entries;
} oxnas_dma_desc_t;

struct oxnas_dma_channel {
	struct virt_dma_chan 	vc;
	struct list_head 	node;
	oxnas_dma_device_t	*dmadev;
	unsigned		id;
	unsigned		irq;

	struct dma_slave_config	cfg;

	dma_addr_t		p_sg_info;	/* Physical address of the array of sg_info structures */
	oxnas_dma_sg_info_t	*sg_info;    /* Virtual address of the array of sg_info structures */
	
	atomic_t		active;

	oxnas_dma_desc_t 	*cur;
};

struct oxnas_dma_device {
	struct platform_device *pdev;
	struct dma_device 	common;
	void __iomem 		*dma_base;
	void __iomem 		*sgdma_base;
	struct reset_control	*dma_rst;
	struct reset_control	*sgdma_rst;
	struct clk		*dma_clk;

	unsigned 		channels_count;

	oxnas_dma_channel_t 	channels[MAX_OXNAS_DMA_CHANNELS];

	int 			hwversion;
	
	spinlock_t		lock;
	struct tasklet_struct 	tasklet;

	struct list_head	pending;

	struct {
		dma_addr_t 	start;
		dma_addr_t 	end;
		unsigned	type;
	} 			*authorized_types;
	unsigned		authorized_types_count;

	struct list_head 	free_entries;
	atomic_t		free_entries_count;
	dma_addr_t		p_sg_data;
	oxnas_dma_sg_data_t	*sg_data;
};

static void oxnas_dma_start_next(oxnas_dma_channel_t *channel);

static irqreturn_t oxnas_dma_interrupt(int irq, void *dev_id)
{
	oxnas_dma_channel_t *channel = dev_id;
	oxnas_dma_device_t *dmadev = channel->dmadev;
	unsigned long error_code;
	unsigned long flags;

	dev_vdbg(&dmadev->pdev->dev, "irq for channel %d\n", channel->id);

	while (readl(dmadev->dma_base + DMA_CALC_REG_ADR(0, DMA_INTR_ID)) 
		& (1 << channel->id)) {
		
		dev_dbg(&dmadev->pdev->dev, "Acking interrupt for channel %u\n",
			channel->id);

		/* Write to the interrupt clear register to clear interrupt */
		writel(0, dmadev->dma_base + DMA_CALC_REG_ADR(channel->id, 
							DMA_INTR_CLEAR_REG));

	}

	if(channel->cur && channel->cur->type == OXNAS_DMA_TYPE_SG) {
		error_code = readl(dmadev->sgdma_base +
				DMA_SG_CALC_REG_ADR(channel->id, DMA_SG_STATUS));
		error_code &= (BIT(DMA_SG_STATUS_ERROR_CODE_NUM_BITS) - 1);

		/* TODO use it somewhere... */
		if (error_code)
			dev_err(&dmadev->pdev->dev, "ch%d: sgdma err %x\n",
					channel->id, (unsigned int)error_code);

		writel(1, dmadev->sgdma_base +
				DMA_SG_CALC_REG_ADR(channel->id, DMA_SG_STATUS));
	}

	spin_lock_irqsave(&channel->vc.lock, flags);

	if (atomic_read(&channel->active)) {
		oxnas_dma_desc_t *cur = channel->cur;
		oxnas_dma_start_next(channel);
		if (cur)
			vchan_cookie_complete(&cur->vd);
	} else
		dev_warn(&dmadev->pdev->dev, "spurious irq for channel %d\n",
				channel->id);

	spin_unlock_irqrestore(&channel->vc.lock, flags);

	return IRQ_HANDLED;
}

static void oxnas_dma_start_next(oxnas_dma_channel_t *channel)
{
	oxnas_dma_device_t *dmadev = channel->dmadev;
	struct virt_dma_desc *vd = vchan_next_desc(&channel->vc);
	oxnas_dma_desc_t *desc;
	unsigned long ctrl_status;
	
	if (!vd) {
		channel->cur = NULL;
		return;
	}

	list_del(&vd->node);

	channel->cur = desc = container_of(&vd->tx, oxnas_dma_desc_t, vd.tx);

	if (desc->type == OXNAS_DMA_TYPE_SIMPLE) {
		/* Write the control/status value to the DMAC */
		writel(desc->ctrl, dmadev->dma_base + 
			DMA_CALC_REG_ADR(channel->id, DMA_CTRL_STATUS));

		/* Ensure control/status word makes it to the DMAC before
		 * we write address/length info
		 */
		wmb();

		/* Write the source addresses to the DMAC */
		writel(desc->src_adr & OXNAS_DMA_ADR_MASK,  dmadev->dma_base + 
			DMA_CALC_REG_ADR(channel->id, DMA_BASE_SRC_ADR));

		/* Write the destination addresses to the DMAC */
		writel(desc->dst_adr & OXNAS_DMA_ADR_MASK,  dmadev->dma_base + 
			DMA_CALC_REG_ADR(channel->id, DMA_BASE_DST_ADR));

		/* Write the length, with EOT configuration
		 * for the single transfer
		 */
		writel(desc->len, dmadev->dma_base + 
				DMA_CALC_REG_ADR(channel->id, DMA_BYTE_CNT));

		/* Ensure adr/len info makes it to DMAC before later modifications to
		 * control/status register due to starting the transfer, which happens in
		 * oxnas_dma_start()
		 */
		wmb();

		/* Setup channel data */
		atomic_set(&channel->active, 1);

		/* Single transfer mode, so unpause the DMA controller channel */
		ctrl_status = readl(dmadev->dma_base + 
			DMA_CALC_REG_ADR(channel->id, DMA_CTRL_STATUS));
		writel(ctrl_status & ~DMA_CTRL_STATUS_PAUSE, dmadev->dma_base + 
			DMA_CALC_REG_ADR(channel->id, DMA_CTRL_STATUS));

		dev_dbg(&dmadev->pdev->dev, "ch%d: started req %d from %08x to %08x, %lubytes\n",
			 channel->id, vd->tx.cookie,
			 desc->src_adr, desc->dst_adr,
			 desc->len & OXNAS_DMA_MAX_TRANSFER_LENGTH);
	} else if (desc->type == OXNAS_DMA_TYPE_SG) {
		/* Write to the SG-DMA channel's reset register to reset the control
		 * in case the previous SG-DMA transfer failed in some way, thus
		 * leaving the SG-DMA controller hung up part way through processing
		 * its SG list. The reset bits are self-clearing */
		writel(BIT(DMA_SG_RESETS_CONTROL_BIT), dmadev->sgdma_base + 
			DMA_SG_CALC_REG_ADR(channel->id, DMA_SG_RESETS));
		
		/* Copy the sg_info structure */
		memcpy(channel->sg_info, &desc->sg_info, sizeof(oxnas_dma_sg_info_t));
		wmb();

		/* Write the pointer to the SG info struct into the Request Pointer reg */
        	writel(channel->p_sg_info,  dmadev->sgdma_base + 
			DMA_SG_CALC_REG_ADR(channel->id, DMA_SG_REQ_PTR));

		/* Setup channel data */
		atomic_set(&channel->active, 1);

		/* Start the transfert */
		writel(BIT(DMA_SG_CONTROL_START_BIT) |
		       BIT(DMA_SG_CONTROL_QUEUING_ENABLE_BIT) |
		       BIT(DMA_SG_CONTROL_HBURST_ENABLE_BIT),
		       dmadev->sgdma_base +
		       	DMA_SG_CALC_REG_ADR(channel->id, DMA_SG_CONTROL));

		dev_dbg(&dmadev->pdev->dev, "ch%d: started %d sg req with %d entries\n",
			 channel->id, vd->tx.cookie,
			 desc->entries);
	}
}

static void oxnas_dma_sched(unsigned long data)
{
	oxnas_dma_device_t *dmadev = (oxnas_dma_device_t*)data;
	LIST_HEAD(head);

	spin_lock_irq(&dmadev->lock);
	list_splice_tail_init(&dmadev->pending, &head);
	spin_unlock_irq(&dmadev->lock);

	while (!list_empty(&head)) {
		oxnas_dma_channel_t *ch = list_first_entry(&head,
			oxnas_dma_channel_t, node);

		spin_lock_irq(&ch->vc.lock);
		list_del_init(&ch->node);
		oxnas_dma_start_next(ch);
		spin_unlock_irq(&ch->vc.lock);
	}
}

static int oxnas_check_address(oxnas_dma_device_t *dmadev, dma_addr_t address)
{
	int i;
	
	for (i = 0 ; i <  dmadev->authorized_types_count ; ++i) {
		if (address >= dmadev->authorized_types[i].start &&
		    address < dmadev->authorized_types[i].end)
			return dmadev->authorized_types[i].type;
	}

	return -1;
}

static struct dma_async_tx_descriptor *oxnas_dma_prep_slave_sg(
	struct dma_chan *chan, struct scatterlist *sgl, unsigned sglen,
	enum dma_transfer_direction dir, unsigned long flags, void *context)
{
	oxnas_dma_channel_t *channel = container_of(chan, oxnas_dma_channel_t, vc.chan);
	oxnas_dma_device_t *dmadev = channel->dmadev;
	oxnas_dma_desc_t *desc;
	struct scatterlist *sgent;
	oxnas_dma_sg_entry_t *entry_mem = NULL, *prev_entry_mem = NULL;
	oxnas_dma_sg_entry_t *entry_dev = NULL;
	unsigned i;
	int src_memory = OXNAS_DMA_DREQ_MEMORY;
	int dst_memory = OXNAS_DMA_DREQ_MEMORY;

	if (dir == DMA_DEV_TO_MEM) {
		src_memory = oxnas_check_address(dmadev, channel->cfg.src_addr);
		if (src_memory == -1) {
			dev_err(&dmadev->pdev->dev, "invalid memory address %08x\n",
					channel->cfg.src_addr);
			return NULL;
		}
		if (src_memory == OXNAS_DMA_DREQ_MEMORY) {
			dev_err(&dmadev->pdev->dev, "In DEV_TO_MEM, src cannot be memory\n");
			return NULL;
		}
	}
	else if (dir == DMA_MEM_TO_DEV) {
		dst_memory = oxnas_check_address(dmadev, channel->cfg.dst_addr);
		if (dst_memory == -1) {
			dev_err(&dmadev->pdev->dev, "invalid memory address %08x\n",
					channel->cfg.dst_addr);
			return NULL;
		}
		if (dst_memory == OXNAS_DMA_DREQ_MEMORY) {
			dev_err(&dmadev->pdev->dev, "In MEM_TO_DEV, dst cannot be memory\n");
			return NULL;
		}
	}
	else {
		dev_err(&dmadev->pdev->dev, "invalid direction\n");
		return NULL;
	}

	if (atomic_read(&dmadev->free_entries_count) < (sglen + 1)) {
		dev_err(&dmadev->pdev->dev, "Missing sg entries...\n");
		return NULL;
	}

	desc = kzalloc(sizeof(oxnas_dma_desc_t), GFP_KERNEL);
	if (unlikely(!desc))
		return NULL;
	desc->channel = channel;

	INIT_LIST_HEAD(&desc->sg_entries);
	desc->entries = 0;

	/* Device single entry */
	entry_dev = list_first_entry_or_null(&dmadev->free_entries,
					     oxnas_dma_sg_entry_t, entry);
	if (!entry_dev) {
		dev_err(&dmadev->pdev->dev, "Fatal error: Missing dev sg entry...\n");
		goto entries_cleanup;
	}
	atomic_dec(&dmadev->free_entries_count);
	list_move(&entry_dev->entry, &desc->sg_entries);
	++desc->entries;
	dev_info(&dmadev->pdev->dev, "got entry %p (%08x)\n", entry_dev, entry_dev->this_paddr);

	entry_dev->next_entry = NULL;
	entry_dev->p_next_entry = 0;
	entry_dev->data_length = 0; /* Completed by mem sg entries */

	if (dir == DMA_DEV_TO_MEM) {
		entry_dev->data_addr = channel->cfg.src_addr & OXNAS_DMA_ADR_MASK;
		desc->sg_info.srcEntries = entry_dev;
		desc->sg_info.p_srcEntries = entry_dev->this_paddr;
		dev_info(&dmadev->pdev->dev, "src set %p\n", entry_dev);
	} else if (dir == DMA_MEM_TO_DEV) {
		entry_dev->data_addr = channel->cfg.dst_addr & OXNAS_DMA_ADR_MASK;
		desc->sg_info.dstEntries = entry_dev;
		desc->sg_info.p_dstEntries = entry_dev->this_paddr;
		dev_info(&dmadev->pdev->dev, "dst set %p\n", entry_dev);
	}
	dev_info(&dmadev->pdev->dev, "src = %p (%08x) dst = %p (%08x)\n",
			desc->sg_info.srcEntries, desc->sg_info.p_srcEntries,
			desc->sg_info.dstEntries, desc->sg_info.p_dstEntries);

	/* Memory entries */
	for_each_sg(sgl, sgent, sglen, i) {
		entry_mem = list_first_entry_or_null(&dmadev->free_entries,
						     oxnas_dma_sg_entry_t, entry);
		if (!entry_mem) {
			dev_err(&dmadev->pdev->dev, "Fatal error: Missing mem sg entries...\n");
			goto entries_cleanup;
		}
		atomic_dec(&dmadev->free_entries_count);
		list_move(&entry_mem->entry, &desc->sg_entries);
		++desc->entries;
		dev_info(&dmadev->pdev->dev, "got entry %p (%08x)\n", entry_mem, entry_mem->this_paddr);

		/* Fill the linked list */
		if (prev_entry_mem) {
			prev_entry_mem->next_entry = entry_mem;
			prev_entry_mem->p_next_entry = entry_mem->this_paddr;
		}
		else {
			if (dir == DMA_DEV_TO_MEM) {
				desc->sg_info.dstEntries = entry_mem;
				desc->sg_info.p_dstEntries = entry_mem->this_paddr;
				dev_info(&dmadev->pdev->dev, "src set %p\n", entry_mem);
			} else if (dir == DMA_MEM_TO_DEV) {
				desc->sg_info.srcEntries = entry_mem;
				desc->sg_info.p_srcEntries = entry_mem->this_paddr;
				dev_info(&dmadev->pdev->dev, "dst set %p\n", entry_mem);
			}
			dev_info(&dmadev->pdev->dev, "src = %p (%08x) dst = %p (%08x)\n",
			desc->sg_info.srcEntries, desc->sg_info.p_srcEntries,
			desc->sg_info.dstEntries, desc->sg_info.p_dstEntries);
		}
		prev_entry_mem = entry_mem;

		/* Fill the entry from the SG */
		entry_mem->next_entry = NULL;
		entry_mem->p_next_entry = 0;

		entry_mem->data_addr = sg_dma_address(sgent) & OXNAS_DMA_ADR_MASK;
		entry_mem->data_length = sg_dma_len(sgent);
		dev_info(&dmadev->pdev->dev, "sg = %08x len = %d\n",
					sg_dma_address(sgent),
					sg_dma_len(sgent));

		/* Add to dev sg length */
		entry_dev->data_length += sg_dma_len(sgent);
	}
	dev_dbg(&dmadev->pdev->dev, "allocated %d sg entries\n", desc->entries);

	desc->sg_info.qualifier = (channel->id << OXNAS_DMA_SG_CHANNEL_BIT) |
				  BIT(OXNAS_DMA_SG_QUALIFIER_BIT);
	if (dir == DMA_DEV_TO_MEM)
		desc->sg_info.qualifier |= (2 << OXNAS_DMA_SG_SRC_EOT_BIT);
	else if (dir == DMA_MEM_TO_DEV)
		desc->sg_info.qualifier |= (2 << OXNAS_DMA_SG_DST_EOT_BIT);

	desc->sg_info.control = (DMA_CTRL_STATUS_INTERRUPT_ENABLE |
				 DMA_CTRL_STATUS_FAIR_SHARE_ARB |
				 DMA_CTRL_STATUS_INTR_CLEAR_ENABLE);
	desc->sg_info.control |= (src_memory << DMA_CTRL_STATUS_SRC_DREQ_SHIFT);
	desc->sg_info.control |= (dst_memory << DMA_CTRL_STATUS_DEST_DREQ_SHIFT);

	if (dir == DMA_DEV_TO_MEM) {
		desc->sg_info.control |= DMA_CTRL_STATUS_SRC_ADR_MODE;
		desc->sg_info.control &= ~DMA_CTRL_STATUS_SOURCE_ADDRESS_FIXED;
		desc->sg_info.control |= DMA_CTRL_STATUS_DEST_ADR_MODE;
		desc->sg_info.control &= ~DMA_CTRL_STATUS_DESTINATION_ADDRESS_FIXED;
	} else if (dir == DMA_MEM_TO_DEV) {
		desc->sg_info.control |= DMA_CTRL_STATUS_SRC_ADR_MODE;
		desc->sg_info.control &= DMA_CTRL_STATUS_SOURCE_ADDRESS_FIXED;
		desc->sg_info.control |= DMA_CTRL_STATUS_DEST_ADR_MODE;
		desc->sg_info.control &= ~DMA_CTRL_STATUS_DESTINATION_ADDRESS_FIXED;
	}
	desc->sg_info.control |= DMA_CTRL_STATUS_TRANSFER_MODE_A;
	desc->sg_info.control |= DMA_CTRL_STATUS_TRANSFER_MODE_B;
	desc->sg_info.control |= (OXNAS_DMA_A_TO_B << DMA_CTRL_STATUS_DIR_SHIFT);

	desc->sg_info.control |= (OXNAS_DMA_TRANSFER_WIDTH_32BITS << DMA_CTRL_STATUS_SRC_WIDTH_SHIFT);
	desc->sg_info.control |= (OXNAS_DMA_TRANSFER_WIDTH_32BITS << DMA_CTRL_STATUS_DEST_WIDTH_SHIFT);
	desc->sg_info.control &= ~DMA_CTRL_STATUS_STARVE_LOW_PRIORITY;

	desc->type = OXNAS_DMA_TYPE_SG;

#if 1
	{
		oxnas_dma_sg_entry_t *entry;
		dev_info(&dmadev->pdev->dev, "SG DMA TX dir %s types src %d dst %d\n",
				(dir == DMA_DEV_TO_MEM ? "DEV_TO_MEM" : "MEM_TO_DEV"),
				src_memory, dst_memory);
		if (dir == DMA_DEV_TO_MEM)
			dev_info(&dmadev->pdev->dev, "\tDev addr 0x%08x", channel->cfg.src_addr);
		else
			dev_info(&dmadev->pdev->dev, "\tDev addr 0x%08x", channel->cfg.dst_addr);
		dev_info(&dmadev->pdev->dev, "\t SG Memory Dump :\n");
		for_each_sg(sgl, sgent, sglen, i) {
			dev_info(&dmadev->pdev->dev, "\t\t0x%08x : %x\n",
					sg_dma_address(sgent),
					sg_dma_len(sgent));
		}
		dev_info(&dmadev->pdev->dev, "\t Entries Memory Dump :\n");
		if (dir == DMA_DEV_TO_MEM)
			entry = desc->sg_info.dstEntries;
		else
			entry = desc->sg_info.srcEntries;
		while(entry) {
			dev_info(&dmadev->pdev->dev, "\t\t0x%08x : %x (cur 0x%08x next 0x%08X)\n",
					entry->data_addr,
					entry->data_length,
					entry->this_paddr,
					entry->p_next_entry);
			entry = entry->next_entry;
		}
		dev_info(&dmadev->pdev->dev, "\t Entries Dev Dump :\n");
		if (dir == DMA_DEV_TO_MEM)
			entry = desc->sg_info.srcEntries;
		else
			entry = desc->sg_info.dstEntries;
		while(entry) {
			dev_info(&dmadev->pdev->dev, "\t\t0x%08x : %x (cur 0x%08x next 0x%08X)\n",
					entry->data_addr,
					entry->data_length,
					entry->this_paddr,
					entry->p_next_entry);
			entry = entry->next_entry;
		}
		dev_info(&dmadev->pdev->dev, "\tqualifier %x\n", desc->sg_info.qualifier);
		dev_info(&dmadev->pdev->dev, "\tcontrol %x\n", desc->sg_info.control);
	}
#endif

	return vchan_tx_prep(&channel->vc, &desc->vd, flags);

entries_cleanup:
	/* Put back all entries in the free entries... */
	list_splice_tail_init(&desc->sg_entries, &dmadev->free_entries);
	atomic_add(desc->entries, &dmadev->free_entries_count);
	dev_dbg(&dmadev->pdev->dev, "freed %d sg entries\n", desc->entries);

	kfree(desc);

	return NULL;
}

/** Allocate descriptors capable of mapping the requested length of memory */
static struct dma_async_tx_descriptor 
		*oxnas_dma_prep_dma_memcpy(struct dma_chan *chan, 
					    dma_addr_t dst, dma_addr_t src,
					    size_t len, unsigned long flags)
{
	oxnas_dma_channel_t *channel = container_of(chan, oxnas_dma_channel_t, vc.chan);
	oxnas_dma_device_t *dmadev = channel->dmadev;
	oxnas_dma_desc_t *desc;
	int src_memory = OXNAS_DMA_DREQ_MEMORY;
	int dst_memory = OXNAS_DMA_DREQ_MEMORY;

	if (len > OXNAS_DMA_MAX_TRANSFER_LENGTH)
		return NULL;

	src_memory = oxnas_check_address(dmadev, src);
	if (src_memory == -1) {
		dev_err(&dmadev->pdev->dev, "invalid memory address %08x\n",
				src);
		return NULL;
	}
	dst_memory = oxnas_check_address(dmadev, dst);
	if (dst_memory == -1) {
		dev_err(&dmadev->pdev->dev, "invalid memory address %08x\n",
				src);
		return NULL;
	}
	
	desc = kzalloc(sizeof(oxnas_dma_desc_t), GFP_KERNEL);
	if (unlikely(!desc))
		return NULL;
	desc->channel = channel;

	dev_dbg(&dmadev->pdev->dev, "preparing memcpy from %08x to %08x, %lubytes (flags %x)\n",
		 src,
		 dst,
		 (unsigned long)len,
		 (unsigned int)flags);

	/* CTRL STATUS Preparation */

	/* Pause while start */
	desc->ctrl = DMA_CTRL_STATUS_PAUSE;

	/* Interrupts enabled
	 * High priority
	 * Use new interrupt clearing register */
	desc->ctrl |= (DMA_CTRL_STATUS_INTERRUPT_ENABLE |
			DMA_CTRL_STATUS_FAIR_SHARE_ARB |
			DMA_CTRL_STATUS_INTR_CLEAR_ENABLE);

	/* Type Memory */
	desc->ctrl |= (src_memory << DMA_CTRL_STATUS_SRC_DREQ_SHIFT);
	desc->ctrl |= (dst_memory << DMA_CTRL_STATUS_DEST_DREQ_SHIFT);
	
	/* Setup the transfer direction and burst/single mode for the two DMA busses */
	desc->ctrl |= DMA_CTRL_STATUS_TRANSFER_MODE_A;
	desc->ctrl |= DMA_CTRL_STATUS_TRANSFER_MODE_B;
	desc->ctrl |= (OXNAS_DMA_A_TO_B << DMA_CTRL_STATUS_DIR_SHIFT);

	/* Incrementing addresses */
	desc->ctrl |= DMA_CTRL_STATUS_SRC_ADR_MODE;
	desc->ctrl &= ~DMA_CTRL_STATUS_SOURCE_ADDRESS_FIXED;
	desc->ctrl |= DMA_CTRL_STATUS_DEST_ADR_MODE;
	desc->ctrl &= ~DMA_CTRL_STATUS_DESTINATION_ADDRESS_FIXED;

	/* Set up the width of the transfers on the DMA buses */
	desc->ctrl |= (OXNAS_DMA_TRANSFER_WIDTH_32BITS << DMA_CTRL_STATUS_SRC_WIDTH_SHIFT);
	desc->ctrl |= (OXNAS_DMA_TRANSFER_WIDTH_32BITS << DMA_CTRL_STATUS_DEST_WIDTH_SHIFT);

	/* Setup the priority arbitration scheme */
	desc->ctrl &= ~DMA_CTRL_STATUS_STARVE_LOW_PRIORITY;

	/* LENGTH and End Of Transfert Preparation */
	desc->len = len | 
	       DMA_BYTE_CNT_INC4_SET_MASK |    /* Always enable INC4 transfers */
	       DMA_BYTE_CNT_HPROT_MASK |       /* Always enable HPROT assertion */
	       DMA_BYTE_CNT_RD_EOT_MASK;       /* EOT at last Read */

	desc->src_adr = src;
	desc->dst_adr = dst;
	desc->type = OXNAS_DMA_TYPE_SIMPLE;

	return vchan_tx_prep(&channel->vc, &desc->vd, flags);
}

static int oxnas_dma_slave_config(struct dma_chan *chan, struct dma_slave_config *cfg)
{
	oxnas_dma_channel_t *channel = container_of(chan, oxnas_dma_channel_t, vc.chan);

	memcpy(&channel->cfg, cfg, sizeof(channel->cfg));

	return 0;
}

static void oxnas_dma_desc_free(struct virt_dma_desc *vd)
{
	oxnas_dma_desc_t *desc = container_of(&vd->tx, oxnas_dma_desc_t, vd.tx);
	oxnas_dma_channel_t *channel = desc->channel;
	oxnas_dma_device_t *dmadev = channel->dmadev;

	/* Free SG entries */
	if (desc->type == OXNAS_DMA_TYPE_SG) {
		list_splice_tail_init(&desc->sg_entries, &dmadev->free_entries);
		atomic_add(desc->entries, &dmadev->free_entries_count);
		dev_dbg(&dmadev->pdev->dev, "freed %d sg entries\n", desc->entries);
	}

	kfree(container_of(vd, oxnas_dma_desc_t, vd));
}

/** Poll for the DMA channel's active status. There can be multiple transfers
 *  queued with the DMA channel identified by cookies, so should be checking
 *  lists containing all pending transfers and all completed transfers that have
 *  not yet been polled for completion
 */
enum dma_status oxnas_dma_tx_status(struct dma_chan *chan,
					    dma_cookie_t cookie,
					    struct dma_tx_state *txstate)
{
	oxnas_dma_channel_t *channel = container_of(chan, oxnas_dma_channel_t, vc.chan);
	struct virt_dma_desc *vd;
	enum dma_status ret;
	unsigned long flags;
	
	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE || !txstate)
		return ret;

	spin_lock_irqsave(&channel->vc.lock, flags);
	vd = vchan_find_desc(&channel->vc, cookie);
	if (vd) {
		oxnas_dma_desc_t *desc = container_of(&vd->tx, oxnas_dma_desc_t, vd.tx);
		txstate->residue = desc->len & OXNAS_DMA_MAX_TRANSFER_LENGTH;
	} else {
		txstate->residue = 0;
	}
	spin_unlock_irqrestore(&channel->vc.lock, flags);

	return ret;
}

/** To push outstanding transfers to h/w. This should use the list of pending
 *  transfers identified by cookies to select the next transfer and pass this to
 *  the hardware
 */
static void oxnas_dma_issue_pending(struct dma_chan *chan)
{
	oxnas_dma_channel_t *channel = container_of(chan, oxnas_dma_channel_t, vc.chan);
	oxnas_dma_device_t *dmadev = channel->dmadev;
	unsigned long flags;

	spin_lock_irqsave(&channel->vc.lock, flags);
	if (vchan_issue_pending(&channel->vc) && !channel->cur) {
		spin_lock(&dmadev->lock);

		if (list_empty(&channel->node))
			list_add_tail(&channel->node, &dmadev->pending);
		
		spin_unlock(&dmadev->lock);
		
		tasklet_schedule(&dmadev->tasklet);
	}

	spin_unlock_irqrestore(&channel->vc.lock, flags);
}

static void oxnas_dma_free_chan_resources(struct dma_chan *chan)
{
	oxnas_dma_channel_t *channel = container_of(chan, oxnas_dma_channel_t, vc.chan);

	vchan_free_chan_resources(&channel->vc);
}

static int oxnas_dma_probe(struct platform_device *pdev)
{
	oxnas_dma_device_t *dmadev;
	struct resource *res;
	int hwid, i, ret;

	dmadev = devm_kzalloc(&pdev->dev, sizeof(oxnas_dma_device_t), GFP_KERNEL);
	if (!dmadev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dmadev->dma_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dmadev->dma_base))
		return PTR_ERR(dmadev->dma_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	dmadev->sgdma_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dmadev->sgdma_base))
		return PTR_ERR(dmadev->sgdma_base);

	dmadev->dma_rst = devm_reset_control_get(&pdev->dev, "dma");
	if (IS_ERR(dmadev->dma_rst))
		return PTR_ERR(dmadev->dma_rst);

	dmadev->sgdma_rst = devm_reset_control_get(&pdev->dev, "sgdma");
	if (IS_ERR(dmadev->sgdma_rst))
		return PTR_ERR(dmadev->sgdma_rst);

	dmadev->dma_clk = of_clk_get(pdev->dev.of_node, 0);
	if (IS_ERR(dmadev->dma_clk))
		return PTR_ERR(dmadev->dma_clk);

	ret = of_property_count_elems_of_size(pdev->dev.of_node,
					      "plxtech,targets-types",
					      4);
	if (ret <= 0 || (ret % 3) != 0) {
		dev_err(&pdev->dev, "malformed or missing plxtech,targets-types\n");
		return -EINVAL;
	}
	dmadev->authorized_types_count = ret/3;
	dmadev->authorized_types = devm_kzalloc(&pdev->dev, 
				sizeof(*dmadev->authorized_types) * 
				dmadev->authorized_types_count, GFP_KERNEL);
	if (!dmadev->authorized_types)
		return -ENOMEM;
	for (i = 0 ; i < dmadev->authorized_types_count ; ++i) {
		u32 value;
		ret = of_property_read_u32_index(pdev->dev.of_node,
						 "plxtech,targets-types",
						 (i * 3), &value);
		if (ret < 0)
			return ret;
		dmadev->authorized_types[i].start = value;
		ret = of_property_read_u32_index(pdev->dev.of_node,
						 "plxtech,targets-types",
						 (i * 3) + 1, &value);
		if (ret < 0)
			return ret;
		dmadev->authorized_types[i].end = value;
		ret = of_property_read_u32_index(pdev->dev.of_node,
						 "plxtech,targets-types",
						 (i * 3) + 2, &value);
		if (ret < 0)
			return ret;
		dmadev->authorized_types[i].type = value;
	}

	dev_info(&pdev->dev, "Authorized memory ranges :\n");
	dev_info(&pdev->dev, " Start    - End      = Type\n");
	for (i = 0 ; i <  dmadev->authorized_types_count ; ++i)
		dev_info(&pdev->dev, "0x%08x-0x%08x = %d\n",
			 dmadev->authorized_types[i].start,
			 dmadev->authorized_types[i].end,
			 dmadev->authorized_types[i].type);

	dmadev->pdev = pdev;

	spin_lock_init(&dmadev->lock);

	tasklet_init(&dmadev->tasklet, oxnas_dma_sched, (unsigned long)dmadev);
	INIT_LIST_HEAD(&dmadev->common.channels);
	INIT_LIST_HEAD(&dmadev->pending);
	INIT_LIST_HEAD(&dmadev->free_entries);

	/* Enable HW & Clocks */
	reset_control_reset(dmadev->dma_rst);
	reset_control_reset(dmadev->sgdma_rst);
	clk_prepare_enable(dmadev->dma_clk);

	/* Discover the number of channels available */
	hwid = readl(dmadev->dma_base + DMA_CALC_REG_ADR(0, DMA_INTR_ID));
	dmadev->channels_count = DMA_INTR_ID_GET_NUM_CHANNELS(hwid);
	dmadev->hwversion = DMA_INTR_ID_GET_VERSION(hwid);

	dev_info(&pdev->dev, "OXNAS DMA v%x with %d channels\n",
		 dmadev->hwversion, dmadev->channels_count);

	/* Limit channels count */
	if (dmadev->channels_count > MAX_OXNAS_DMA_CHANNELS)
		dmadev->channels_count = MAX_OXNAS_DMA_CHANNELS;

	/* Allocate coherent memory for sg descriptors */
	dmadev->sg_data = dma_alloc_coherent(&pdev->dev, sizeof(oxnas_dma_sg_data_t),
					     &dmadev->p_sg_data, GFP_KERNEL);
	if (!dmadev->sg_data) {
		dev_err(&pdev->dev, "unable to allocate coherent\n");
		return -ENOMEM;
	}

	/* Reset SG descritors */
	memset(dmadev->sg_data, 0, sizeof(oxnas_dma_sg_data_t));
	atomic_set(&dmadev->free_entries_count, 0);

	/* Initialize and add all sg entries to the free list */
	for (i = 0 ; i < MAX_OXNAS_SG_ENTRIES ; ++i) {
		dmadev->sg_data->entries[i].this_paddr = 
			(dma_addr_t)
			 &(((oxnas_dma_sg_data_t *)
			  dmadev->p_sg_data)->entries[i]);
		wmb();
		INIT_LIST_HEAD(&dmadev->sg_data->entries[i].entry);
		list_add_tail(&dmadev->sg_data->entries[i].entry,
			      &dmadev->free_entries);
		atomic_inc(&dmadev->free_entries_count);
	}

	/* Init all channels */
	for (i = 0 ; i < dmadev->channels_count ; ++i) {
		oxnas_dma_channel_t *ch = &dmadev->channels[i];
		ch->dmadev = dmadev;
		ch->id = i;

		ch->irq = irq_of_parse_and_map(pdev->dev.of_node, i);
		if (ch->irq <= 0) {
			dev_err(&pdev->dev, "invalid irq%d from platform\n", i);
			goto probe_err;
		}

		ret = devm_request_irq(&pdev->dev, ch->irq,
				       oxnas_dma_interrupt, 0,
				       "DMA", ch);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to request irq%d\n", i);
			goto probe_err;
		}
		
		ch->p_sg_info =
			(dma_addr_t)&((oxnas_dma_sg_data_t *)
					dmadev->p_sg_data)->infos[i];
		ch->sg_info = &dmadev->sg_data->infos[i];
		memset(ch->sg_info, 0, sizeof(oxnas_dma_sg_info_t));
		wmb();

		atomic_set(&ch->active, 0);

		ch->vc.desc_free = oxnas_dma_desc_free;
		vchan_init(&ch->vc, &dmadev->common);
		INIT_LIST_HEAD(&ch->node);
	}

	platform_set_drvdata(pdev, dmadev);

	dma_cap_set(DMA_MEMCPY, dmadev->common.cap_mask);
	dmadev->common.chancnt = dmadev->channels_count;
	dmadev->common.device_free_chan_resources = oxnas_dma_free_chan_resources;
	dmadev->common.device_tx_status = oxnas_dma_tx_status;
	dmadev->common.device_issue_pending = oxnas_dma_issue_pending;
	dmadev->common.device_prep_dma_memcpy = oxnas_dma_prep_dma_memcpy;
	dmadev->common.device_prep_slave_sg = oxnas_dma_prep_slave_sg;
	dmadev->common.device_config = oxnas_dma_slave_config;
	dmadev->common.copy_align = DMAENGINE_ALIGN_4_BYTES;
	dmadev->common.src_addr_widths = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dmadev->common.dst_addr_widths = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dmadev->common.directions = BIT(DMA_MEM_TO_MEM);
	dmadev->common.residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;
	dmadev->common.dev = &pdev->dev;

	ret = dma_async_device_register(&dmadev->common);
	if (ret)
		goto probe_err;

	ret = of_dma_controller_register(pdev->dev.of_node,
					 of_dma_xlate_by_chan_id,
					 &dmadev->common);
	if (ret)
		dev_warn(&pdev->dev, "Failed to register OF\n");
	
	dev_info(&pdev->dev, "OXNAS DMA Registered\n");

	return 0;

probe_err:
	/*dma_free_coherent(&pdev->dev, sizeof(oxnas_dma_sg_data_t),
			  &dmadev->p_sg_data, GFP_KERNEL);*/

	return ret;
}

static int oxnas_dma_remove(struct platform_device *dev)
{
	oxnas_dma_device_t *dmadev = platform_get_drvdata(dev);
	//oxnas_dma_channel_t *ch;

	dma_async_device_unregister(&dmadev->common);

	/*
	   TODO
	list_for_each_entry_safe(ch, _channel, &dmadev->common.channels, device_node) {
		list_del(&ch->device_node);
	}
	kfree(oxnas_dma_device);
	*/

	return 0;
}

static const struct of_device_id oxnas_dma_of_dev_id[] = {
	{ .compatible = "plxtech,nas782x-dma", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_dma_of_dev_id);

static struct platform_driver oxnas_dma_driver = {
	.probe		= oxnas_dma_probe,
	.remove		= oxnas_dma_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "oxnas-dma",
		.of_match_table = oxnas_dma_of_dev_id,
	},
};

static int __init oxnas_dma_init_module(void)
{
	return platform_driver_register(&oxnas_dma_driver);
}
subsys_initcall(oxnas_dma_init_module);

static void __exit oxnas_dma_exit_module(void)
{
	platform_driver_unregister(&oxnas_dma_driver);
	return;
}
module_exit(oxnas_dma_exit_module);

MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oxford Semiconductor Ltd.");
