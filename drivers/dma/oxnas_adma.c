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
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>

#include "dmaengine.h"
#include "virt-dma.h"

// Normal (non-SG) registers
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

// 8 quad-sized registers per channel arranged contiguously
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

// H/W scatter gather controller registers
#define OXNAS_DMA_NUM_SG_REGS 4

#define DMA_SG_CONTROL  0x0
#define DMA_SG_STATUS   0x04
#define DMA_SG_REQ_PTR  0x08
#define DMA_SG_RESETS   0x0C

#define DMA_SG_CALC_REG_ADR(channel, register) ((DMA_SG_BASE) + ((channel) << 4) + (register))

// SG DMA controller control register field definitions
#define DMA_SG_CONTROL_START_BIT            0
#define DMA_SG_CONTROL_QUEUING_ENABLE_BIT   1
#define DMA_SG_CONTROL_HBURST_ENABLE_BIT    2

// SG DMA controller status register field definitions
#define DMA_SG_STATUS_ERROR_CODE_BIT        0
#define DMA_SG_STATUS_ERROR_CODE_NUM_BITS   6
#define DMA_SG_STATUS_BUSY_BIT              7

// SG DMA controller sub-block resets register field definitions
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
	dma_addr_t                 addr;   // The physical address of the buffer described by this descriptor
	unsigned long              length; // The length of the buffer described by this descriptor
	dma_addr_t                 p_next; // The physical address of the next descriptor
	struct oxnas_dma_sg_entry *next;   // The virtual address of the next descriptor
	dma_addr_t                 paddr;  // The physical address of this descriptor
	struct oxnas_dma_sg_entry *s_next;   // To allow insertion into single-linked list
} __attribute ((aligned(4),packed)) oxnas_dma_sg_entry_t;

/* Will be exchanged with SG DMA controller */
typedef struct oxnas_dma_sg_info {
	unsigned long         qualifer;
	unsigned long         control;
	dma_addr_t            p_srcEntries_; // The physical address of the first source SG descriptor
	dma_addr_t            p_dstEntries_; // The physical address of the first destination SG descriptor
	oxnas_dma_sg_entry_t *srcEntries_; // The virtual address of the first source SG descriptor
	oxnas_dma_sg_entry_t *dstEntries_; // The virtual address of the first destination SG descriptor
} __attribute ((aligned(4),packed)) oxnas_dma_sg_info_t;

typedef struct oxnas_dma_sg_data {
	oxnas_dma_sg_entry_t entries[MAX_OXNAS_SG_ENTRIES];
	oxnas_dma_sg_info_t infos[MAX_OXNAS_DMA_CHANNELS];
} __attribute ((aligned(4))) oxnas_dma_sg_data_t;

typedef struct oxnas_dma_device oxnas_dma_device_t;
typedef struct oxnas_dma_channel oxnas_dma_channel_t;

enum {
	OXNAS_DMA_TYPE_SIMPLE = 0,
	OXNAS_DMA_TYPE_SG = 0,
};

typedef struct oxnas_adma_desc {
	struct virt_dma_desc vd;
	unsigned long ctrl;
	unsigned long len;
	dma_addr_t src_adr;
	dma_addr_t dst_adr;
	unsigned type;
} oxnas_adma_desc_t;

struct oxnas_dma_channel {
	struct virt_dma_chan 	vc;
	struct list_head 	node;
	oxnas_dma_device_t	*dmadev;
	unsigned		id;
	unsigned		irq;
	
#if 0
	spinlock_t		spinlock;

	/* Covered by spinlock */
	dma_addr_t		p_sg_info;	/* Physical address of the array of sg_info structures */
	oxnas_dma_sg_info_t	*sg_info;    /* Virtual address of the array of sg_info structures */

	atomic_t		run_bh;
	atomic_t		interrupt_count;
	atomic_t		next_cookie;
#endif
	
	atomic_t		active;

	oxnas_adma_desc_t 	*cur;
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

#if 0
	dma_addr_t		p_sg_data;
	oxnas_dma_sg_data_t	*sg_data;
	struct semaphore	csum_engine_sem;
	spinlock_t		alloc_spinlock;  /* Sync. for SG management */
	spinlock_t		channel_alloc_spinlock;  /* Sync. for channel management */
	oxnas_dma_sg_entry_t	*sg_entry_head;  /* Pointer to head of free list for 
						    oxnas_dma_sg_entry_t objects */
	struct semaphore	sg_entry_sem;
	unsigned		sg_entry_available;
	oxnas_dma_channel_t	*channel_head;
	struct semaphore	channel_sem;
#endif
};

#if 0
typedef enum oxnas_dma_mode {
    OXNAS_DMA_MODE_FIXED,
    OXNAS_DMA_MODE_INC
} oxnas_dma_mode_t;

typedef enum oxnas_dma_direction {
    OXNAS_DMA_TO_DEVICE,
    OXNAS_DMA_FROM_DEVICE
} oxnas_dma_direction_t;

//###############################


#define MAX_OXNAS_DMA_TRANSFER_LENGTH ((1 << 21) - 1)

struct oxnas_dma_channel;
typedef struct oxnas_dma_channel oxnas_dma_channel_t;


typedef enum oxnas_dma_eot_type {
    OXNAS_DMA_EOT_NONE,
    OXNAS_DMA_EOT_ALL,
    OXNAS_DMA_EOT_FINAL
} oxnas_dma_eot_type_t;

// Will be exchanged with SG DMA controller
typedef struct oxnas_dma_sg_entry {
    dma_addr_t                 addr_;   // The physical address of the buffer described by this descriptor
    unsigned long              length_; // The length of the buffer described by this descriptor
    dma_addr_t                 p_next_; // The physical address of the next descriptor
    struct oxnas_dma_sg_entry *v_next_; // The virtual address of the next descriptor
    dma_addr_t                 paddr_;  // The physical address of this descriptor
    struct oxnas_dma_sg_entry *next_;   // To allow insertion into single-linked list
} __attribute ((aligned(4),packed)) oxnas_dma_sg_entry_t;

// Will be exchanged with SG DMA controller
typedef struct oxnas_dma_sg_info {
    unsigned long         qualifer_;
    unsigned long         control_;
    dma_addr_t            p_srcEntries_; // The physical address of the first source SG descriptor
    dma_addr_t            p_dstEntries_; // The physical address of the first destination SG descriptor
    oxnas_dma_sg_entry_t *v_srcEntries_; // The virtual address of the first source SG descriptor
    oxnas_dma_sg_entry_t *v_dstEntries_; // The virtual address of the first destination SG descriptor
} __attribute ((aligned(4),packed)) oxnas_dma_sg_info_t;

struct oxnas_dma_channel {
    unsigned                     channel_number_;
    oxnas_dma_callback_t         notification_callback_;
    oxnas_callback_arg_t         notification_arg_;
    dma_addr_t                   p_sg_info_;    // Physical address of sg_info structure
    oxnas_dma_sg_info_t         *v_sg_info_;    // Virtual address of sg_info structure
    oxnas_dma_callback_status_t  error_code_;
#ifdef CONFIG_OXNAS_VERSION_0X800
    int                          checksumming_;
    u16                          checksum_;
#endif // CONFIG_OXNAS_VERSION_0X800
    unsigned                     rps_interrupt_;
    struct oxnas_dma_channel    *next_;
    struct semaphore             default_semaphore_;
    atomic_t                     interrupt_count_;
    atomic_t                     active_count_;
	int							  auto_sg_entries_;
};

typedef struct oxnas_dma_controller {
    oxnas_dma_channel_t        channels_[MAX_OXNAS_DMA_CHANNELS];
    unsigned                   numberOfChannels_;
    int                        version_;
    atomic_t                   run_bh_;
    spinlock_t                 spinlock_;
    struct                     tasklet_struct tasklet_;
    dma_addr_t                 p_sg_infos_;     // Physical address of the array of sg_info structures
    oxnas_dma_sg_info_t       *v_sg_infos_;     // Virtual address of the array of sg_info structures
    struct semaphore           csum_engine_sem_;
    spinlock_t                 alloc_spinlock_;  // Sync. for SG management
    spinlock_t                 channel_alloc_spinlock_;  // Sync. for channel management
    oxnas_dma_sg_entry_t      *sg_entry_head_;  // Pointer to head of free list for oxnas_dma_sg_entry_t objects
    struct semaphore           sg_entry_sem_;
    unsigned                   sg_entry_available_;
    oxnas_dma_channel_t       *channel_head_;
    struct semaphore           channel_sem_;
} oxnas_dma_controller_t;

typedef struct oxnas_dma_device_settings {
    unsigned long address_;
    unsigned      fifo_size_;   // Chained transfers must take account of FIFO offset at end of previous transfer
    unsigned char dreq_;
    unsigned      read_eot_policy_:2;
    unsigned      write_eot_policy_:2;
    unsigned      bus_:1;
    unsigned      width_:2;
    unsigned      transfer_mode_:1;
    unsigned      address_mode_:1;
    unsigned      address_really_fixed_:1;
} oxnas_dma_device_settings_t;

// Normal (non-SG) registers
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

// 8 quad-sized registers per channel arranged contiguously
#define DMA_CALC_REG_ADR(channel, register) (DMA_BASE + ((channel) << 5) + (register))

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

// H/W scatter gather controller registers
#define OXNAS_DMA_NUM_SG_REGS 4

#define DMA_SG_CONTROL  0x0
#define DMA_SG_STATUS   0x04
#define DMA_SG_REQ_PTR  0x08
#define DMA_SG_RESETS   0x0C

#define DMA_SG_CALC_REG_ADR(channel, register) ((DMA_SG_BASE) + ((channel) << 4) + (register))

// SG DMA controller control register field definitions
#define DMA_SG_CONTROL_START_BIT            0
#define DMA_SG_CONTROL_QUEUING_ENABLE_BIT   1
#define DMA_SG_CONTROL_HBURST_ENABLE_BIT    2

// SG DMA controller status register field definitions
#define DMA_SG_STATUS_ERROR_CODE_BIT        0
#define DMA_SG_STATUS_ERROR_CODE_NUM_BITS   6
#define DMA_SG_STATUS_BUSY_BIT              7

// SG DMA controller sub-block resets register field definitions
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

#define OXNAS_DMA_CSUM_ADR_MASK (OXNAS_DMA_ADR_MASK)
#define OXNAS_DMA_ADR_MASK       ((1UL << (MEM_MAP_ALIAS_SHIFT)) - 1)

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

/**
 * Acquisition of a SG DMA descriptor list entry
 * If called from non-atomic context the call could block.
 */
static oxnas_dma_sg_entry_t* alloc_sg_entry(int in_atomic)
{
    oxnas_dma_sg_entry_t* entry = 0;
    if (in_atomic) {
        if (down_trylock(&dmadev->sg_entry_sem_)) {
            return (oxnas_dma_sg_entry_t*)0;
        }
    } else {
        // Wait for an entry to be available
        while (down_interruptible(&dmadev->sg_entry_sem_));
    }

    // Serialise while manipulating free list
    spin_lock_bh(&dmadev->alloc_spinlock_);

    // It's an error if there isn't a buffer available at this point
    BUG_ON(!dmadev->sg_entry_head_);

    // Unlink the head entry on the free list and return it to caller
    entry = dmadev->sg_entry_head_;
    dmadev->sg_entry_head_ = dmadev->sg_entry_head_->next_;
    --dmadev->sg_entry_available_;

    // Finished manipulating free list
    spin_unlock_bh(&dmadev->alloc_spinlock_);

    return entry;
}

static void free_sg_entry(oxnas_dma_sg_entry_t* entry)
{
	// Serialise while manipulating free list
	spin_lock(&dmadev->alloc_spinlock_);

	// Insert the freed buffer at the head of the free list
	entry->next_ = dmadev->sg_entry_head_;
	dmadev->sg_entry_head_ = entry;
	++dmadev->sg_entry_available_;

	// Finished manipulating free list
	spin_unlock(&dmadev->alloc_spinlock_);

	// Make freed buffer available for allocation
	up(&dmadev->sg_entry_sem_);
}

void oxnas_dma_free_sg_entries(oxnas_dma_sg_entry_t* entries)
{
	while (entries) {
		oxnas_dma_sg_entry_t* next = entries->next_;
		free_sg_entry(entries);
		entries = next;
	}
}

/**
 * This implementation is not the most efficient, as it could result in alot
 * of alloc's only to decide to free them all as not sufficient available, but
 * in practice we would hope there will not be much contention for entries
 */
int oxnas_dma_alloc_sg_entries(
    oxnas_dma_sg_entry_t **entries,
    unsigned               required,
	int                    in_atomic)
{
	if (likely(required)) {
		oxnas_dma_sg_entry_t* prev;
		oxnas_dma_sg_entry_t* entry;
		unsigned acquired = 0;

		*entries = alloc_sg_entry(in_atomic);
		if (!*entries) {
			return 1;
		}

		(*entries)->next_ = 0;
		prev = *entries;

		while (++acquired < required) {
			entry = alloc_sg_entry(in_atomic);
			if (!entry) {
				// Did not acquire the entry
				oxnas_dma_free_sg_entries(*entries);
				return 1;
			}
			entry->next_ = 0;
			prev->next_ = entry;
			prev = entry;
		}
	}

    return 0;
}

/**
 * Optionally blocking acquisition of a DMA channel
 * May be invoked either at task or softirq level
 */
oxnas_dma_channel_t* oxnas_dma_request(int block)
{
    oxnas_dma_channel_t* channel = OXNAS_DMA_CHANNEL_NUL;
    while (channel == OXNAS_DMA_CHANNEL_NUL) {
        if (block) {
            // Wait for a channel to be available
            if (down_interruptible(&dmadev->channel_sem_)) {
                // Awoken by signal
                continue;
            }
        } else {
            // Non-blocking test of whether a channel is available
            if (down_trylock(&dmadev->channel_sem_)) {
                // No channel available so return to user immediately
                break;
            }
        }

        // Serialise while manipulating free list
        spin_lock_bh(&dmadev->channel_alloc_spinlock_);

        // It's an error if there isn't a channel available at this point
        BUG_ON(!dmadev->channel_head_);

        // Unlink the head entry on the free list and return it to caller
        channel = dmadev->channel_head_;
        dmadev->channel_head_ = dmadev->channel_head_->next_;

        // Finished manipulating free list
        spin_unlock_bh(&dmadev->channel_alloc_spinlock_);
    }
    return channel;
}

/**
 * May be invoked either at task or softirq level
 */
void oxnas_dma_free(oxnas_dma_channel_t* channel)
{
    if (oxnas_dma_is_active(channel)) {
        printk(KERN_WARNING "oxnas_dma_free() Freeing channel %u while active\n", channel->channel_number_);
    }

    // Serialise while manipulating free list
    spin_lock_bh(&dmadev->channel_alloc_spinlock_);

    // Insert the freed buffer at the head of the free list
    channel->next_ = dmadev->channel_head_;
    dmadev->channel_head_ = channel;

    // Finished manipulating free list
    spin_unlock_bh(&dmadev->channel_alloc_spinlock_);

    // Make freed buffer available for allocation
    up(&dmadev->channel_sem_);
}

/** Shared between all DMA interrupts and run with interrupts enabled, thus any
 *  access to shared data structures must be sync'ed
 */
static irqreturn_t oxnas_dma_interrupt(int irq, void *dev_id)
{
    oxnas_dma_channel_t *channel = 0;
    unsigned channel_number = 0;
	int need_bh = 0;

DBG("oxnas_dma_interrupt() from interrupt line %u\n", irq);

    // Only acknowledge interrupts from the channel directly responsible for the
    // RPS interrupt line which caused the ISR to be entered, to get around the
    // problem that the SG-DMA controller can only filter DMA interrupts exter-
    // nally to the DMA controller, i.e. the DMA controller interrupt status
    // register always shows all active interrupts for all channels, regardless
    // of whether the SG-DMA controller is filtering them

    // Find the DMA channel that can generate interrupts on the RPS interrupt
    // line which caused the ISR to be invoked.
	if (likely(irq == DMA_INTERRUPT_4)) {
		channel = &dmadev->channels_[4];
	} else {
		channel = &dmadev->channels_[irq - DMA_INTERRUPT_0];
	}
	channel_number = channel->channel_number_;
DBG("RPS interrupt %u from channel %u\n", irq, channel_number);

    // Non-SG transfers have no completion status, so initialise
    // channel's error code to no-error. If transfer turns out to
    // have been SG, this status will be overwritten
    channel->error_code_ = OXNAS_DMA_ERROR_CODE_NONE;

	// Must finish in bottom half if checksumming or need to invoke callback
	need_bh = 
			  (channel->notification_callback_ != OXNAS_DMA_CALLBACK_NUL);

    // Cope with the DMA controller's ability to have a pair of chained
    // transfers which have both completed, which causes the interrupt request
    // to stay active until both have been acknowledged, which is causing the SG
    // controller problems
    while (readl(DMA_CALC_REG_ADR(0, DMA_INTR_ID)) & (1 << channel_number)) {
DBG("Ack'ing interrupt for channel %u\n", channel_number);
        // Write to the interrupt clear register to clear interrupt
        writel(0, DMA_CALC_REG_ADR(channel_number, DMA_INTR_CLEAR_REG));

        // Record how many interrupts are awaiting service
        atomic_inc(&channel->interrupt_count_);
    }
DBG("Left int ack'ing loop\n");

	// If was a SG transfer, record the completion status
	if (channel->v_sg_info_->v_srcEntries_) {
		// Record the SG transfer completion status
		u32 error_code = readl(DMA_SG_CALC_REG_ADR(channel_number, DMA_SG_STATUS));
		channel->error_code_ =
			((error_code >> DMA_SG_STATUS_ERROR_CODE_BIT) &
			 ((1UL << DMA_SG_STATUS_ERROR_CODE_NUM_BITS) - 1));

		 if (channel->auto_sg_entries_) {
			 // Must finish in bottom half if we are to manage the SG entries
DBG("ISR channel %d is auto SG\n", channel->channel_number_);
			 need_bh = 1;
		 } else {
DBG("ISR channel %d not auto SG\n", channel->channel_number_);
			// Zeroise SG DMA descriptor info
			channel->v_sg_info_->p_srcEntries_ = 0;
			channel->v_sg_info_->v_srcEntries_ = 0;
			channel->v_sg_info_->p_dstEntries_ = 0;
			channel->v_sg_info_->v_dstEntries_ = 0;
		 }

DBG("Return SG controller to idle, error_code = 0x%08x\n", error_code);
		// Return the SG DMA controller to the IDLE state and clear any SG
		// controller error interrupt
		writel(1, DMA_SG_CALC_REG_ADR(channel_number, DMA_SG_STATUS));
	}

	// Can we finish w/o invoking bottom half?
	if (likely(!need_bh)) {
DBG("ISR channel %d do not call bh\n", channel->channel_number_);
		atomic_set(&channel->interrupt_count_, 0);
		atomic_set(&channel->active_count_, 0);
	} else {
DBG("Marking channel %d as requiring its bottom half to run\n", channel_number);
		// Set a flag for the channel to cause its bottom half to be run
		set_bit(channel_number, (void*)&dmadev->run_bh_);

DBG("Scheduling tasklet\n");
		// Signal the bottom half to perform the notifications
		tasklet_schedule(&dmadev->tasklet_);
	}

DBG("Returning\n");
    return IRQ_HANDLED;
}

static void fake_interrupt(int channel)
{
    // Set a flag to cause the bottom half handler to be run for the channel
    set_bit(channel, (void*)&dmadev->run_bh_);

    // Signal the bottom half to perform the notifications
    tasklet_schedule(&dmadev->tasklet_);
}

static void dma_bh(unsigned long data)
{
    // Check for any bottom halves having become ready to run
    u32 run_bh = atomic_read(&dmadev->run_bh_);
    while (run_bh) {
        unsigned i;

		// Free any checksumming or SG resources
		u32 temp_run_bh = run_bh;
        for (i = 0; i < dmadev->numberOfChannels_; i++, temp_run_bh >>= 1) {
            if (temp_run_bh & 1) {
                oxnas_dma_channel_t* channel = &dmadev->channels_[i];
DBG("Bottom halve for channel %u\n", channel->channel_number_);

				if (channel->auto_sg_entries_) {
					// Free SG DMA source descriptor resources
					oxnas_dma_sg_entry_t* sg_entry = channel->v_sg_info_->v_srcEntries_;
DBG("Freeing SG resources for channel %d\n", channel->channel_number_);
					while (sg_entry) {
						oxnas_dma_sg_entry_t* next = sg_entry->v_next_;
						free_sg_entry(sg_entry);
						sg_entry = next;
					}

					// Free SG DMA destination descriptor resources
					sg_entry = channel->v_sg_info_->v_dstEntries_;
					while (sg_entry) {
						oxnas_dma_sg_entry_t* next = sg_entry->v_next_;
						free_sg_entry(sg_entry);
						sg_entry = next;
					}

					// Zeroise SG DMA source descriptor info
					channel->v_sg_info_->p_srcEntries_ = 0;
					channel->v_sg_info_->v_srcEntries_ = 0;
					channel->v_sg_info_->p_dstEntries_ = 0;
					channel->v_sg_info_->v_dstEntries_ = 0;
				}
            }
        }

        // Mark that we have serviced the bottom halves. None of the channels
        // we have just serviced can interrupt again until their active flags
        // are cleared below
        atomic_sub(run_bh, &dmadev->run_bh_);

        // Notify all listeners of transfer completion
        for (i = 0; i < dmadev->numberOfChannels_; i++, run_bh >>= 1) {
            if (run_bh & 1) {
                int interrupt_count;
                oxnas_dma_channel_t* channel = &dmadev->channels_[i];

                // Clear the count of received interrupts for the channel now
                // that we have serviced them all
                interrupt_count = atomic_read(&channel->interrupt_count_);
                atomic_sub(interrupt_count, &channel->interrupt_count_);

                // Decrement the count of active transfers, by the number of
                // interrupts we've seen. This must occur before we inform any
                // listeners who are awaiting completion notification. Should
                // only decrement if greater than zero, in case we see spurious
                // interrupt events - we can't be fully safe against this sort
                // of broken h/w, but we can at least stop the count underflowing
                // active_count_ is only shared with thread level code, so read
                // and decrement don't need to be atomic
                if (atomic_read(&channel->active_count_)) {
                    atomic_dec(&channel->active_count_);
                }

                // If there is a callback registered, notify the user that the
                // transfer is complete
                if (channel->notification_callback_ != OXNAS_DMA_CALLBACK_NUL) {
DBG("Notifying channel %u, %d outstanding interrupts\n", channel->channel_number_, interrupt_count);
                    (*channel->notification_callback_)(
                        &dmadev->channels_[i],
                        channel->notification_arg_,
                        channel->error_code_,
						 0,
                        interrupt_count);
                }
            }
        }

        // Check for any more bottom halves having become ready to run
        run_bh = atomic_read(&dmadev->run_bh_);
    }
}
#endif

#if 0
void __init oxnas_dma_init()
{
    unsigned i;
    unsigned long intId;
    oxnas_dma_sg_info_t *v_info;
    dma_addr_t           p_info;

    // Ensure the DMA block is properly reset
    writel(1UL << SYS_CTRL_RSTEN_DMA_BIT, SYS_CTRL_RSTEN_SET_CTRL);
    writel(1UL << SYS_CTRL_RSTEN_DMA_BIT, SYS_CTRL_RSTEN_CLR_CTRL);

    // Ensure the SG-DMA block is properly reset
    writel(1UL << SYS_CTRL_RSTEN_SGDMA_BIT, SYS_CTRL_RSTEN_SET_CTRL);
    writel(1UL << SYS_CTRL_RSTEN_SGDMA_BIT, SYS_CTRL_RSTEN_CLR_CTRL);

    // Enable the clock to the DMA block
    writel(1UL << SYS_CTRL_CKEN_DMA_BIT, SYS_CTRL_CKEN_SET_CTRL);

    // Initialise the DMA controller
    atomic_set(&dmadev->run_bh_, 0);
    spin_lock_init(&dmadev->spinlock_);
    spin_lock_init(&dmadev->alloc_spinlock_);
    spin_lock_init(&dmadev->channel_alloc_spinlock_);
    sema_init(&dmadev->csum_engine_sem_, 1);

    // Initialise channel allocation management
    dmadev->channel_head_ = 0;
    sema_init(&dmadev->channel_sem_, 0);
    // Initialise SRAM buffer management
    dmadev->sg_entry_head_ = 0;
    sema_init(&dmadev->sg_entry_sem_, 0);
    dmadev->sg_entry_available_ = 0;

    tasklet_init(&dmadev->tasklet_, dma_bh, 0);

    // Discover the number of channels available
    intId = readl(DMA_CALC_REG_ADR(0, DMA_INTR_ID));
    dmadev->numberOfChannels_ = DMA_INTR_ID_GET_NUM_CHANNELS(intId);
    if (dmadev->numberOfChannels_ > MAX_OXNAS_DMA_CHANNELS) {
        printk(KERN_WARNING "DMA: Too many DMA channels");
        dmadev->numberOfChannels_ = MAX_OXNAS_DMA_CHANNELS;
    }

    dmadev->version_ = DMA_INTR_ID_GET_VERSION(intId);
    printk(KERN_INFO "Number of DMA channels = %u, version = %u\n",
        dmadev->numberOfChannels_, dmadev->version_);

    if (!DMA_HAS_V4_INTR_CLEAR(dmadev->version_)) {
        panic("DMA: Trying to use v4+ interrupt clearing on DMAC version without support\n");
    }

///////////////// TODO //////////////////
    // Allocate coherent memory for array sg_info structs
    dmadev->v_sg_infos_ = (oxnas_dma_sg_info_t*)DMA_DESC_ALLOC_START;
    dmadev->p_sg_infos_ = DMA_DESC_ALLOC_START_PA;

    if (!dmadev->v_sg_infos_) {
        panic("DMA: Coherent alloc of SG info struct array");
    }

    {
		// Initialise list of DMA descriptors
        unsigned long sg_info_alloc_size = (dmadev->numberOfChannels_ * sizeof(oxnas_dma_sg_info_t));
        unsigned num_sg_entries = (DMA_DESC_ALLOC_SIZE - sg_info_alloc_size) / sizeof(oxnas_dma_sg_entry_t);
        oxnas_dma_sg_entry_t* entry_v = (oxnas_dma_sg_entry_t*)(DMA_DESC_ALLOC_START + sg_info_alloc_size);
        oxnas_dma_sg_entry_t* entry_p = (oxnas_dma_sg_entry_t*)(DMA_DESC_ALLOC_START_PA + sg_info_alloc_size);
printk("Allocating %u SRAM generic DMA descriptors\n", num_sg_entries);
        for (i=0; i < num_sg_entries; ++i, ++entry_v, ++entry_p) {
            entry_v->paddr_ = (dma_addr_t)entry_p;
            free_sg_entry(entry_v);
        }
    }
///////////////// TODO //////////////////

    // Initialise all available DMA channels
    v_info = dmadev->v_sg_infos_;
    p_info = dmadev->p_sg_infos_;
    for (i=0; i < dmadev->numberOfChannels_; i++) {
        oxnas_dma_channel_t *channel = &dmadev->channels_[i];

        channel->channel_number_ = i;
        channel->notification_callback_ = OXNAS_DMA_CALLBACK_NUL;
        channel->notification_arg_ = OXNAS_DMA_CALLBACK_ARG_NUL;

        // Setup physical and virtual addresses of the SG info struct for this
        // channel
        channel->v_sg_info_ = v_info++;
        channel->p_sg_info_ = p_info;
        p_info += sizeof(oxnas_dma_sg_info_t);

        // Initialise heads of src and dst SG lists to null
        channel->v_sg_info_->p_srcEntries_ = 0;
        channel->v_sg_info_->p_dstEntries_ = 0;
        channel->v_sg_info_->v_srcEntries_ = 0;
        channel->v_sg_info_->v_dstEntries_ = 0;

        channel->error_code_ = 0;

        // Initialise the atomic variable that records the number of interrupts
        // for the channel that are awaiting service
        atomic_set(&channel->interrupt_count_, 0);

        // Initialise the atomic variable maintaining the count of in-progress
        // transfers for the channel. Currently can be a maximum of two, as
        // the hardware can only queue details for a pair of transfers
        atomic_set(&channel->active_count_, 0);

        // The binary semaphore for the default callback used when abort
        // requested without a user-registered callback being available
        sema_init(&channel->default_semaphore_, 0);

        // Add channel to free list
        oxnas_dma_free(channel);
    }

    // Connect the dma interrupt handler
    dmadev->channels_[0].rps_interrupt_ = DMA_INTERRUPT_0;
    if (request_irq(DMA_INTERRUPT_0, &oxnas_dma_interrupt, 0, "DMA 0", 0)) {
        panic("DMA: Failed to allocate interrupt %u\n", DMA_INTERRUPT_0);
    }
    dmadev->channels_[1].rps_interrupt_ = DMA_INTERRUPT_1;
    if (request_irq(DMA_INTERRUPT_1, &oxnas_dma_interrupt, 0, "DMA 1", 0)) {
        panic("DMA: Failed to allocate interrupt %u\n", DMA_INTERRUPT_1);
    }
    dmadev->channels_[2].rps_interrupt_ = DMA_INTERRUPT_2;
    if (request_irq(DMA_INTERRUPT_2, &oxnas_dma_interrupt, 0, "DMA 2", 0)) {
        panic("DMA: Failed to allocate interrupt %u\n", DMA_INTERRUPT_2);
    }
    dmadev->channels_[3].rps_interrupt_ = DMA_INTERRUPT_3;
    if (request_irq(DMA_INTERRUPT_3, &oxnas_dma_interrupt, 0, "DMA 3", 0)) {
        panic("DMA: Failed to allocate interrupt %u\n", DMA_INTERRUPT_3);
    }
    dmadev->channels_[4].rps_interrupt_ = DMA_INTERRUPT_4;
    if (request_irq(DMA_INTERRUPT_4, &oxnas_dma_interrupt, 0, "DMA 4", 0)) {
        panic("DMA: Failed to allocate interrupt %u\n", DMA_INTERRUPT_4);
    }
}

void oxnas_dma_shutdown()
{
    dmadev->sg_entry_head_ = 0;
}

int oxnas_dma_is_active(oxnas_dma_channel_t* channel)
{
    return atomic_read(&channel->active_count_);
}

/**
 * Get the transfer status directly from the hardware, so for instance the
 * end of a transfer can be polled for within interrupt context.
 *
 * NB If this function indicates the channel is inactive, it does NOT imply that
 * it can be reused. Reuse is only possible when oxnas_dma_is_active() returns
 * the inactive state 
 */
int oxnas_dma_raw_isactive(oxnas_dma_channel_t* channel)
{
    unsigned long ctrl_status = readl(DMA_CALC_REG_ADR(channel->channel_number_, DMA_CTRL_STATUS));
    return ctrl_status & DMA_CTRL_STATUS_IN_PROGRESS;
}

/**
 * Get the SG transfer status directly from the hardware, so for instance the
 * end of a SG transfer can be polled for within interrupt context.
 *
 * NB If this function indicates the channel is inactive, it does NOT imply that
 * it can be reused. Reuse is only possible when oxnas_dma_is_active() returns
 * the inactive state 
 */
int oxnas_dma_raw_sg_isactive(oxnas_dma_channel_t* channel)
{
    // Record the SG channel status
    u32 status = readl(DMA_SG_CALC_REG_ADR(channel->channel_number_, DMA_SG_STATUS));
    return status & (1UL << DMA_SG_STATUS_BUSY_BIT);
}

int oxnas_dma_get_raw_direction(oxnas_dma_channel_t* channel)
{
    unsigned long ctrl_status = readl(DMA_CALC_REG_ADR(channel->channel_number_, DMA_CTRL_STATUS));
    return (ctrl_status & DMA_CTRL_STATUS_DIR_MASK) >> DMA_CTRL_STATUS_DIR_SHIFT;
}

static unsigned long encode_control_status(
    oxnas_dma_device_settings_t *src_settings,
    oxnas_dma_device_settings_t *dst_settings,
    int                          paused)
{
    unsigned long ctrl_status;
    oxnas_dma_transfer_direction_t direction;

    ctrl_status  = paused ? DMA_CTRL_STATUS_PAUSE : 0;							// Paused if requested
    ctrl_status |= (DMA_CTRL_STATUS_INTERRUPT_ENABLE |							// Interrupts enabled
				    DMA_CTRL_STATUS_FAIR_SHARE_ARB   |							// High priority
					DMA_CTRL_STATUS_INTR_CLEAR_ENABLE);						// Use new interrupt clearing register
    ctrl_status |= (src_settings->dreq_ << DMA_CTRL_STATUS_SRC_DREQ_SHIFT);	// Source dreq
    ctrl_status |= (dst_settings->dreq_ << DMA_CTRL_STATUS_DEST_DREQ_SHIFT);	// Destination dreq

    // Setup the transfer direction and burst/single mode for the two DMA busses
    if (src_settings->bus_ == OXNAS_DMA_SIDE_A) {
        // Set the burst/single mode for bus A based on src device's settings
        if (src_settings->transfer_mode_ == OXNAS_DMA_TRANSFER_MODE_BURST) {
            ctrl_status |= DMA_CTRL_STATUS_TRANSFER_MODE_A;
        } else {
            ctrl_status &= ~DMA_CTRL_STATUS_TRANSFER_MODE_A;
        }

        if (dst_settings->bus_ == OXNAS_DMA_SIDE_A) {
            direction = OXNAS_DMA_A_TO_A;
        } else {
            direction = OXNAS_DMA_A_TO_B;

            // Set the burst/single mode for bus B based on dst device's settings
            if (dst_settings->transfer_mode_ == OXNAS_DMA_TRANSFER_MODE_BURST) {
                ctrl_status |= DMA_CTRL_STATUS_TRANSFER_MODE_B;
            } else {
                ctrl_status &= ~DMA_CTRL_STATUS_TRANSFER_MODE_B;
            }
        }
    } else {
        // Set the burst/single mode for bus B based on src device's settings
        if (src_settings->transfer_mode_ == OXNAS_DMA_TRANSFER_MODE_BURST) {
            ctrl_status |= DMA_CTRL_STATUS_TRANSFER_MODE_B;
        } else {
            ctrl_status &= ~DMA_CTRL_STATUS_TRANSFER_MODE_B;
        }

        if (dst_settings->bus_ == OXNAS_DMA_SIDE_A) {
            direction = OXNAS_DMA_B_TO_A;

            // Set the burst/single mode for bus A based on dst device's settings
            if (dst_settings->transfer_mode_ == OXNAS_DMA_TRANSFER_MODE_BURST) {
                ctrl_status |= DMA_CTRL_STATUS_TRANSFER_MODE_A;
            } else {
                ctrl_status &= ~DMA_CTRL_STATUS_TRANSFER_MODE_A;
            }
        } else {
            direction = OXNAS_DMA_B_TO_B;
        }
    }
    ctrl_status |= (direction << DMA_CTRL_STATUS_DIR_SHIFT);

    // Setup source address mode fixed or increment
    if (src_settings->address_mode_ == OXNAS_DMA_MODE_FIXED) {
        // Fixed address
        ctrl_status &= ~(DMA_CTRL_STATUS_SRC_ADR_MODE);

        // Set up whether fixed address is _really_ fixed
        if (src_settings->address_really_fixed_) {
            ctrl_status |= DMA_CTRL_STATUS_SOURCE_ADDRESS_FIXED;
        } else {
            ctrl_status &= ~DMA_CTRL_STATUS_SOURCE_ADDRESS_FIXED;
        }
    } else {
        // Incrementing address
        ctrl_status |= DMA_CTRL_STATUS_SRC_ADR_MODE;
        ctrl_status &= ~DMA_CTRL_STATUS_SOURCE_ADDRESS_FIXED;
    }

    // Setup destination address mode fixed or increment
    if (dst_settings->address_mode_ == OXNAS_DMA_MODE_FIXED) {
        // Fixed address
        ctrl_status &= ~(DMA_CTRL_STATUS_DEST_ADR_MODE);
        
        // Set up whether fixed address is _really_ fixed
        if (dst_settings->address_really_fixed_) {
            ctrl_status |= DMA_CTRL_STATUS_DESTINATION_ADDRESS_FIXED;
        } else {
            ctrl_status &= ~DMA_CTRL_STATUS_DESTINATION_ADDRESS_FIXED;
        }
    } else {
        // Incrementing address
        ctrl_status |= DMA_CTRL_STATUS_DEST_ADR_MODE;
        ctrl_status &= ~DMA_CTRL_STATUS_DESTINATION_ADDRESS_FIXED;
    }

    // Set up the width of the transfers on the DMA buses
    ctrl_status |= (src_settings->width_ << DMA_CTRL_STATUS_SRC_WIDTH_SHIFT);
    ctrl_status |= (dst_settings->width_ << DMA_CTRL_STATUS_DEST_WIDTH_SHIFT);

    // Setup the priority arbitration scheme
    ctrl_status &= ~DMA_CTRL_STATUS_STARVE_LOW_PRIORITY;    // !Starve low priority

    return ctrl_status;
}

static unsigned long encode_eot(
    oxnas_dma_device_settings_t* src_settings,
    oxnas_dma_device_settings_t* dst_settings,
    unsigned long length,
    int isFinalTransfer)
{
    // Write the length, with EOT configuration and enable INC4 tranfers and
    // HPROT. HPROT will delay data reaching memory for a few clock cycles, but
    // most unlikely to cause a problem for the CPU.
    unsigned long encoded = length |
                            DMA_BYTE_CNT_INC4_SET_MASK |    // Always enable INC4 transfers
                            DMA_BYTE_CNT_HPROT_MASK;        // Always enable HPROT assertion

    // Encode the EOT setting for the src device based on its policy
    encoded &= ~DMA_BYTE_CNT_RD_EOT_MASK;
    switch (src_settings->read_eot_policy_) {
        case OXNAS_DMA_EOT_FINAL:
            if (!isFinalTransfer) {
                break;
            }
            // Fall through in case of final transfer and EOT required for final
            // transfer
        case OXNAS_DMA_EOT_ALL:
            encoded |= DMA_BYTE_CNT_RD_EOT_MASK;
            break;
        default:
            break;
    }

    // Encode the EOT setting for the dst device based on its policy
    encoded &= ~DMA_BYTE_CNT_WR_EOT_MASK;
    switch (dst_settings->write_eot_policy_) {
        case OXNAS_DMA_EOT_FINAL:
            if (!isFinalTransfer) {
                break;
            }
            // Fall through in case of final transfer and EOT required for final
            // transfer
        case OXNAS_DMA_EOT_ALL:
            encoded |= DMA_BYTE_CNT_WR_EOT_MASK;
            break;
        default:
            break;
    }

    return encoded;
}

static unsigned long encode_start(unsigned long ctrl_status)
{
    ctrl_status &= ~DMA_CTRL_STATUS_PAUSE;
    return ctrl_status;
}

static void oxnas_dma_set_common_lowlevel(
    oxnas_dma_channel_t *channel,
    unsigned long        ctrl_status,
    dma_addr_t           src_address,
    dma_addr_t           dst_address,
    unsigned long        lengthAndEOT)
{
    unsigned channel_number = channel->channel_number_;

    spin_lock(&dmadev->spinlock_);

    // Write the control/status value to the DMAC
    writel(ctrl_status, DMA_CALC_REG_ADR(channel_number, DMA_CTRL_STATUS));

    // Ensure control/status word makes it to the DMAC before we write address/length info
    wmb();

    // Write the source addresses to the DMAC
    writel(src_address, DMA_CALC_REG_ADR(channel_number, DMA_BASE_SRC_ADR));

    // Write the destination addresses to the DMAC
    writel(dst_address, DMA_CALC_REG_ADR(channel_number, DMA_BASE_DST_ADR));

    // Write the length, with EOT configuration for the single transfer
    writel(lengthAndEOT, DMA_CALC_REG_ADR(channel_number, DMA_BYTE_CNT));

    // Ensure adr/len info makes it to DMAC before later modifications to
    // control/status register due to starting the transfer, which happens in
    // oxnas_dma_start()
    wmb();

    spin_unlock(&dmadev->spinlock_);

    // Increase count of in-progress transfers on this channel
    atomic_inc(&channel->active_count_);
}

static int oxnas_dma_set_common(
    oxnas_dma_channel_t*         channel,
    unsigned long                length,
    oxnas_dma_device_settings_t *src_settings,
    oxnas_dma_device_settings_t *dst_settings,
    int                          isFinalTransfer,
    int                          paused)
{
    int status = 0;

    if (length > MAX_OXNAS_DMA_TRANSFER_LENGTH) {
        printk(KERN_WARNING "oxnas_dma_set_common() length exceeds hardware allowed maximum\n");
        status = 1;
    } else {
        oxnas_dma_set_common_lowlevel(
            channel,
            encode_control_status(src_settings, dst_settings, paused),
            (dma_addr_t)src_settings->address_,
            (dma_addr_t)dst_settings->address_,
            encode_eot(src_settings, dst_settings, length, isFinalTransfer));
    }
    return status;
}

int oxnas_dma_set(
    oxnas_dma_channel_t *channel,
    unsigned char       *src_adr,   // Physical address
    unsigned long        length,
    unsigned char       *dst_adr,   // Physical address
    oxnas_dma_mode_t     src_mode,
    oxnas_dma_mode_t     dst_mode,
    int                  do_checksum,
    int                  paused)
{
    if (oxnas_dma_is_active(channel)) {
        printk(KERN_WARNING "oxnas_dma_set() Trying to use channel %u while active\n", channel->channel_number_);
    }

	BUG_ON(do_checksum);

    {
        // Assemble complete memory settings, accounting for csum generation if
        // required
        oxnas_dma_device_settings_t src_settings = oxnas_ram_only_src_dma_settings;

        oxnas_dma_device_settings_t dst_settings = oxnas_ram_generic_dma_settings;

        // Assemble the source address
        src_settings.address_ = (unsigned long)src_adr;

        // Ensure only use the valid src address bits are used
        src_settings.address_ &= OXNAS_DMA_CSUM_ADR_MASK;
        src_settings.address_mode_ = src_mode;

        // Ensure only use the valid dst address bits are used
        dst_settings.address_ = ((unsigned long)dst_adr) & OXNAS_DMA_ADR_MASK;
        dst_settings.address_mode_ = dst_mode;

        return oxnas_dma_set_common(channel, length, &src_settings, &dst_settings, 1, paused);
    }
}

int oxnas_dma_device_set(
    oxnas_dma_channel_t         *channel,
    oxnas_dma_direction_t        direction,
    unsigned char               *mem_adr,   // Physical address
    unsigned long                length,
    oxnas_dma_device_settings_t *device_settings,
    oxnas_dma_mode_t             mem_mode,
    int                          paused)
{
    oxnas_dma_device_settings_t mem_settings;

    if (oxnas_dma_is_active(channel)) {
        printk(KERN_WARNING "oxnas_dma_device_set() Trying to use channel %u while active\n", channel->channel_number_);
    }

    // Assemble complete memory settings, ensuring addresses do not affect the
    // checksum enabling high order adr bit
    mem_settings = oxnas_ram_generic_dma_settings;
    mem_settings.address_ = ((unsigned long)mem_adr) & OXNAS_DMA_ADR_MASK;
    mem_settings.address_mode_ = mem_mode;

    device_settings->address_ &= OXNAS_DMA_ADR_MASK;

    return oxnas_dma_set_common(
        channel,
        length,
        (direction == OXNAS_DMA_TO_DEVICE)   ? &mem_settings : device_settings,
        (direction == OXNAS_DMA_FROM_DEVICE) ? &mem_settings : device_settings,
        1,
        paused);
}

int oxnas_dma_device_pair_set(
    oxnas_dma_channel_t*         channel,
    unsigned long                length,
    oxnas_dma_device_settings_t *src_device_settings,
    oxnas_dma_device_settings_t *dst_device_settings,
    int                          paused)
{
    if (oxnas_dma_is_active(channel)) {
        printk(KERN_WARNING "oxnas_dma_device_pair_set() Trying to use channel %u while active\n", channel->channel_number_);
    }

    // Ensure addresses do not affect the checksum enabling high order adr bit
    src_device_settings->address_ &= OXNAS_DMA_ADR_MASK;
    dst_device_settings->address_ &= OXNAS_DMA_ADR_MASK;
    return oxnas_dma_set_common(channel, length, src_device_settings, dst_device_settings, 1, paused);
}

static int oxnas_dma_set_sg_common(
    oxnas_dma_channel_t*         channel,
    struct scatterlist*          src_sg,
    unsigned                     src_sg_count,
    struct scatterlist*          dst_sg,
    unsigned                     dst_sg_count,
    oxnas_dma_device_settings_t* src_settings,
    oxnas_dma_device_settings_t* dst_settings,
	int                          in_atomic)
{
    int i;
    int failed = 0;
    oxnas_dma_sg_entry_t *sg_entry;
    oxnas_dma_sg_entry_t *previous_entry;

    // Get reference to this channel's top level SG DMA descriptor structure
    oxnas_dma_sg_info_t *sg_info = channel->v_sg_info_;

	// SG entries have not been provided
	channel->auto_sg_entries_ = 1;

    // Initialise list pointers to zero
    sg_info->v_srcEntries_ = 0;
    sg_info->p_srcEntries_ = 0;
    sg_info->v_dstEntries_ = 0;
    sg_info->p_dstEntries_ = 0;

    sg_entry = 0;
    previous_entry = 0;
    for (i=0; i < src_sg_count; i++) {
        // Is this entry contiguous with the previous one and would the combined
        // lengths not exceed the maximum that the hardware is capable of
        {
            // Allocate space for SG list entry from coherent DMA pool
            oxnas_dma_sg_entry_t *new_sg_entry = alloc_sg_entry(in_atomic);
            if (!new_sg_entry) {
                failed = 1;
                break;
            }
            sg_entry = new_sg_entry;

            if (previous_entry) {
                // Link the previous SG list entry forward to this one        
                previous_entry->v_next_ = sg_entry;
                previous_entry->p_next_ = sg_entry->paddr_;
            } else {
                // Create a link from the SG info structure to the first SG list entry
                sg_info->v_srcEntries_ = sg_entry;
                sg_info->p_srcEntries_ = sg_entry->paddr_;
            }
            previous_entry = sg_entry;

            // Fill in the SG list entry with start address, ensuring only valid
            // address bits are used, preserving the checksum enabling flag
            sg_entry->addr_ = src_sg[i].dma_address & OXNAS_DMA_CSUM_ADR_MASK;

            // Fill in the length, checking that it does not exceed the hardware
            // allowed maximum
            sg_entry->length_ = (src_sg[i].length <= MAX_OXNAS_DMA_TRANSFER_LENGTH) ? src_sg[i].length : 0;
            if (!sg_entry->length_) {
                printk(KERN_WARNING "oxnas_dma_set_sg_common() Source entry too long, zeroing\n");
            }
        }
    }
    if (sg_entry) {
        // Mark the end of the source SG list with nulls
        sg_entry->p_next_ = 0;
        sg_entry->v_next_ = 0;
    }

    if (failed) {
        // Failed to allocate all SG src entries, so free those we did get
        oxnas_dma_sg_entry_t* sg_entry = sg_info->v_srcEntries_;
        while (sg_entry) {
            oxnas_dma_sg_entry_t* next = sg_entry->v_next_;
            free_sg_entry(sg_entry);
            sg_entry = next;
        }
        channel->v_sg_info_->p_srcEntries_ = 0;
        channel->v_sg_info_->v_srcEntries_ = 0;
        return 1;
    }

    // Assemble destination descriptors
    sg_entry = 0;
    previous_entry = 0;
    for (i=0; i < dst_sg_count; i++) {
        // Is this entry contiguous with the previous one?
        {
            // Allocate space for SG list entry from coherent DMA pool
            oxnas_dma_sg_entry_t *new_sg_entry = alloc_sg_entry(in_atomic);
            if (!new_sg_entry) {
                failed = 1;
                break;
            }
            sg_entry = new_sg_entry;

            if (previous_entry) {
                // Link the previous SG list entry forward to this one        
                previous_entry->v_next_ = sg_entry;
                previous_entry->p_next_ = sg_entry->paddr_;
            } else {
                // Create a link from the SG info structure to the first SG list entry
                sg_info->v_dstEntries_ = sg_entry;
                sg_info->p_dstEntries_ = sg_entry->paddr_;
            }
            previous_entry = sg_entry;

            // Fill in the SG list entry with start address, ensuring address
            // does not affect the checksum enabling high order adr bit
            sg_entry->addr_   = dst_sg[i].dma_address & OXNAS_DMA_ADR_MASK;

            // Fill in the length, checking that it does not exceed the hardware
            // allowed maximum
            sg_entry->length_ = (dst_sg[i].length <= MAX_OXNAS_DMA_TRANSFER_LENGTH) ? dst_sg[i].length : 0;
            if (!sg_entry->length_) {
                printk(KERN_WARNING "oxnas_dma_set_sg_common() Destination entry too long, zeroing\n");
            }
        }
    }
    if (sg_entry) {
        // Mark the end of the destination SG list with nulls
        sg_entry->p_next_ = 0;
        sg_entry->v_next_ = 0;
    }

    if (failed) {
        // Failed to allocate all SG dst entries, so free those we did obtain
        oxnas_dma_sg_entry_t* sg_entry = sg_info->v_dstEntries_;
        while (sg_entry) {
            oxnas_dma_sg_entry_t* next = sg_entry->v_next_;
            free_sg_entry(sg_entry);
            sg_entry = next;
        }
        sg_info->p_dstEntries_ = 0;
        sg_info->v_dstEntries_ = 0;

        // Free all the SG src entries which we did sucessfully obtain
        sg_entry = sg_info->v_srcEntries_;
        while (sg_entry) {
            oxnas_dma_sg_entry_t* next = sg_entry->v_next_;
            free_sg_entry(sg_entry);
            sg_entry = next;
        }
        sg_info->p_srcEntries_ = 0;
        sg_info->v_srcEntries_ = 0;
        return 1;
    }

    sg_info->qualifer_ = ((channel->channel_number_ << OXNAS_DMA_SG_CHANNEL_BIT) |
                          (src_settings->read_eot_policy_ << OXNAS_DMA_SG_SRC_EOT_BIT) |
                          (dst_settings->write_eot_policy_ << OXNAS_DMA_SG_DST_EOT_BIT) |
                          (1 << OXNAS_DMA_SG_QUALIFIER_BIT));

    // Flags are the same for source and destination for each SG transfer component
    sg_info->control_ = encode_control_status(src_settings, dst_settings, 0);

    // Increase count of in-progress transfers on this channel
    atomic_inc(&channel->active_count_);

    return 0;
}

int oxnas_dma_set_sg(
    oxnas_dma_channel_t* channel,
    struct scatterlist*  src_sg,
    unsigned             src_sg_count,
    struct scatterlist*  dst_sg,
    unsigned             dst_sg_count,
    oxnas_dma_mode_t     src_mode,
    oxnas_dma_mode_t     dst_mode,
    int                  do_checksum,
	int                  in_atomic)
{
    if (oxnas_dma_is_active(channel)) {
        printk(KERN_WARNING "oxnas_dma_set_sg() Trying to use channel %u while active\n", channel->channel_number_);
    }

	BUG_ON(do_checksum);
    {
        // Assemble complete memory settings, accounting for csum generation if
        // required
        oxnas_dma_device_settings_t src_settings = oxnas_ram_only_src_dma_settings;

        oxnas_dma_device_settings_t dst_settings = oxnas_ram_generic_dma_settings;

        // Normal adr bits not used for SG transfers
        src_settings.address_ = 0;
        src_settings.address_mode_ = src_mode;

        // Normal adr bits not used for SG transfers
        dst_settings.address_ = 0;
        dst_settings.address_mode_ = dst_mode;

        return oxnas_dma_set_sg_common(
            channel,
            src_sg,
            src_sg_count,
            dst_sg,
            dst_sg_count,
            &src_settings,
            &dst_settings,
			in_atomic);
    }
}

int oxnas_dma_device_set_sg(
    oxnas_dma_channel_t*         channel,
    oxnas_dma_direction_t        direction,
    struct scatterlist*          mem_sg,
    unsigned                     mem_sg_count,
    oxnas_dma_device_settings_t* device_settings,
    oxnas_dma_mode_t             mem_mode,
	int                          in_atomic)
{
    int i;
    struct scatterlist *sg;
    struct scatterlist  dev_sg;

    oxnas_dma_device_settings_t mem_settings;

    if (oxnas_dma_is_active(channel)) {
        printk(KERN_WARNING "oxnas_dma_device_set_sg() Trying to use channel %u while active\n", channel->channel_number_);
    }

    // Assemble complete memory settings
    mem_settings = oxnas_ram_generic_dma_settings;
    mem_settings.address_ = 0;  // Not used for SG transfers
    mem_settings.address_mode_ = mem_mode;

    // Need to total all memory transfer lengths and assign as device single transfer length
    dev_sg.dma_address = device_settings->address_;
    for (i=0, sg=mem_sg, dev_sg.length = 0; i < mem_sg_count; i++, sg++) {
        dev_sg.length += sg->length;
    }

    return oxnas_dma_set_sg_common(
        channel,
        (direction == OXNAS_DMA_TO_DEVICE)   ? mem_sg        : &dev_sg,
        (direction == OXNAS_DMA_TO_DEVICE)   ? mem_sg_count  : 1,
        (direction == OXNAS_DMA_FROM_DEVICE) ? mem_sg        : &dev_sg,
        (direction == OXNAS_DMA_FROM_DEVICE) ? mem_sg_count  : 1,
        (direction == OXNAS_DMA_TO_DEVICE)   ? &mem_settings : device_settings,
        (direction == OXNAS_DMA_FROM_DEVICE) ? &mem_settings : device_settings,
		in_atomic);
}
#endif

#if 0
static int oxnas_dma_set_prd_common(
    oxnas_dma_channel_t         *channel,
    struct ata_prd              *src_prd,
    struct ata_prd              *dst_prd,
    oxnas_dma_device_settings_t *src_settings,
    oxnas_dma_device_settings_t *dst_settings,
	oxnas_dma_sg_entry_t		 *sg_entries)
{
    int i;
    int failed = 0;
    oxnas_dma_sg_entry_t *sg_entry, *previous_entry, *next_entry;
    u32 eot;
	u32 tot_src_len = 0, tot_dst_len = 0;

    // Get reference to this channel's top level SG DMA descriptor structure
    oxnas_dma_sg_info_t *sg_info = channel->v_sg_info_;

	// SG entries have been provided
	channel->auto_sg_entries_ = 0;

    // Initialise list pointers to zero
    sg_info->v_srcEntries_ = 0;
    sg_info->p_srcEntries_ = 0;
    sg_info->v_dstEntries_ = 0;
    sg_info->p_dstEntries_ = 0;

	// Get pointer to first available SG entry
    sg_entry = previous_entry = 0;
    next_entry = sg_entries;
    i=0;
    do {
        u32 addr;
        u32 length;
        u32 flags_len;

        addr = src_prd[i].addr;
        flags_len = le32_to_cpu(src_prd[i++].flags_len);
        length = flags_len & ~ATA_PRD_EOT;
        eot = flags_len & ATA_PRD_EOT;

		// Zero length field means 64KB
        if (!length) length = 0x10000;

		// Accumulate the total length of all source elements
		tot_src_len += length;

        // Is this entry contiguous with the previous one and would the combined
        // lengths not exceed the maximum that the hardware is capable of
        {
			// Get the next available SG entry
			if (!next_entry) {
				failed = 1;
				break;
			}
			sg_entry = next_entry;

            if (previous_entry) {
                // Link the previous SG list entry forward to this one
                previous_entry->v_next_ = sg_entry;
                previous_entry->p_next_ = sg_entry->paddr_;
            } else {
                // Create a link from the SG info structure to the first SG list entry
                sg_info->v_srcEntries_ = sg_entry;
                sg_info->p_srcEntries_ = sg_entry->paddr_;
            }
            previous_entry = sg_entry;

            // Fill in the SG list entry with start address, ensuring only valid
            // address bits are used, preserving the checksum enabling flag
            sg_entry->addr_ = addr & OXNAS_DMA_CSUM_ADR_MASK;

            // Fill in the length, checking that it does not exceed the hardware
            // allowed maximum
            if (length > MAX_OXNAS_DMA_TRANSFER_LENGTH) {
                printk(KERN_WARNING "oxnas_dma_set_prd_common() Source entry too long (0x%x), zeroing\n", length);
                sg_entry->length_ = 0;
            } else {
                sg_entry->length_ = length;
            }

			// Get pointer to next available SG entry
			next_entry = sg_entry->next_;
        }
    } while (!eot);
    if (sg_entry) {
        // Mark the end of the source SG list with nulls
        sg_entry->p_next_ = 0;
        sg_entry->v_next_ = 0;
    }

    if (failed) {
        // Failed to allocate all SG src entries
        channel->v_sg_info_->p_srcEntries_ = 0;
        channel->v_sg_info_->v_srcEntries_ = 0;
		printk(KERN_WARNING "Too few SG entries to satisfy source requirements\n");
        return 1;
    }

    // Assemble destination descriptors
    sg_entry = previous_entry = 0;
    i=0;
    do {
        u32 addr;
        u32 length;
        u32 flags_len;

        addr = dst_prd[i].addr;
        flags_len = le32_to_cpu(dst_prd[i++].flags_len);
        length = flags_len & ~ATA_PRD_EOT;
        eot = flags_len & ATA_PRD_EOT;

		// Zero length field means 64KB
        if (!length) length = 0x10000;

		// Accumulate the total length of all destination elements
		tot_dst_len += length;

        // Is this entry contiguous with the previous one?
        {
			// Get the next available SG entry
			if (!next_entry) {
				failed = 1;
				break;
			}
			sg_entry = next_entry;

            if (previous_entry) {
                // Link the previous SG list entry forward to this one        
                previous_entry->v_next_ = sg_entry;
                previous_entry->p_next_ = sg_entry->paddr_;
            } else {
                // Create a link from the SG info structure to the first SG list entry
                sg_info->v_dstEntries_ = sg_entry;
                sg_info->p_dstEntries_ = sg_entry->paddr_;
            }
            previous_entry = sg_entry;

            // Fill in the SG list entry with start address, ensuring address
            // does not affect the checksum enabling high order adr bit
            sg_entry->addr_ = addr & OXNAS_DMA_ADR_MASK;

            // Fill in the length, checking that it does not exceed the hardware
            // allowed maximum
            if (length > MAX_OXNAS_DMA_TRANSFER_LENGTH) {
                printk(KERN_WARNING "oxnas_dma_set_prd_common() Destination entry too long (0x%x), zeroing\n", length);
                sg_entry->length_ = 0;
            } else {
                sg_entry->length_ = length;
            }

			// Get pointer to next available SG entry
			next_entry = sg_entry->next_;
        }
    } while (!eot);
    if (sg_entry) {
        // Mark the end of the destination SG list with nulls
        sg_entry->p_next_ = 0;
        sg_entry->v_next_ = 0;
    }

    if (failed) {
        // Failed to allocate all SG dst entries
        sg_info->p_dstEntries_ = 0;
        sg_info->v_dstEntries_ = 0;
        sg_info->p_srcEntries_ = 0;
        sg_info->v_srcEntries_ = 0;
		printk(KERN_WARNING "Too few SG entries to satisfy destination requirements\n");
        return 1;
    }

	// Fill in length of single device SG entry from the total length of all the
	// memory SG entries
	if ((sg_entry = sg_info->v_srcEntries_) && !sg_entry->v_next_) {
		sg_entry->length_ = tot_dst_len;
	} else if ((sg_entry = sg_info->v_dstEntries_) && !sg_entry->v_next_) {
		sg_entry->length_ = tot_src_len;
	}

    sg_info->qualifer_ = ((channel->channel_number_ << OXNAS_DMA_SG_CHANNEL_BIT) |
                          (src_settings->read_eot_policy_ << OXNAS_DMA_SG_SRC_EOT_BIT) |
                          (dst_settings->write_eot_policy_ << OXNAS_DMA_SG_DST_EOT_BIT) |
                          (1 << OXNAS_DMA_SG_QUALIFIER_BIT));

    // Flags are the same for source and destination for each SG transfer component
    sg_info->control_ = encode_control_status(src_settings, dst_settings, 0);

    // Increase count of in-progress transfers on this channel
    atomic_inc(&channel->active_count_);

    return 0;
}

int oxnas_dma_device_set_prd(
    oxnas_dma_channel_t         *channel,
    oxnas_dma_direction_t        direction,
    struct ata_prd              *mem_prd,
    oxnas_dma_device_settings_t *device_settings,
    oxnas_dma_mode_t             mem_mode,
	oxnas_dma_sg_entry_t		 *sg_entries)
{
    struct ata_prd dev_prd;
    oxnas_dma_device_settings_t mem_settings;

    if (unlikely(oxnas_dma_is_active(channel))) {
        printk(KERN_WARNING "oxnas_dma_device_set_prd() Trying to use channel %u while active\n", channel->channel_number_);
    }

    // Assemble complete memory settings
    mem_settings = oxnas_ram_generic_dma_settings;
    mem_settings.address_ = 0;  // Not used for SG transfers
    mem_settings.address_mode_ = mem_mode;

    // Device has only a single SG entry whose length will be assigned once
	// all the memory transfer lengths have been accumulated
    dev_prd.addr = device_settings->address_;
    dev_prd.flags_len = ATA_PRD_EOT;

    return oxnas_dma_set_prd_common(
        channel,
        (direction == OXNAS_DMA_TO_DEVICE)   ? mem_prd       : &dev_prd,
        (direction == OXNAS_DMA_FROM_DEVICE) ? mem_prd       : &dev_prd,
        (direction == OXNAS_DMA_TO_DEVICE)   ? &mem_settings : device_settings,
        (direction == OXNAS_DMA_FROM_DEVICE) ? &mem_settings : device_settings,
		sg_entries);
}
#endif

#if 0
void oxnas_dma_abort(
	oxnas_dma_channel_t *channel,
	int                  in_atomic)
{
    u32 ctrl_status;
    unsigned channel_number = channel->channel_number_;
    int must_wait = 0;
    int callback_registered = 0;

    // Assert reset for the channel
    spin_lock(&dmadev->spinlock_);
    ctrl_status = readl(DMA_CALC_REG_ADR(channel_number, DMA_CTRL_STATUS));
    ctrl_status |= DMA_CTRL_STATUS_RESET;
    writel(ctrl_status, DMA_CALC_REG_ADR(channel_number, DMA_CTRL_STATUS));
    spin_unlock(&dmadev->spinlock_);

    // Wait for the channel to become idle - should be quick as should finish
    // after the next AHB single or burst transfer
    while (readl(DMA_CALC_REG_ADR(channel_number, DMA_CTRL_STATUS)) & DMA_CTRL_STATUS_IN_PROGRESS);

    // Deassert reset for the channel
    spin_lock(&dmadev->spinlock_);
    ctrl_status = readl(DMA_CALC_REG_ADR(channel_number, DMA_CTRL_STATUS));
    ctrl_status &= ~DMA_CTRL_STATUS_RESET;
    writel(ctrl_status, DMA_CALC_REG_ADR(channel_number, DMA_CTRL_STATUS));
    spin_unlock(&dmadev->spinlock_);

    // If no user callback is registered, we need to wait here for the DMA
    // channel to become inactive, i.e. for the ISR to be called and the
    // channel software returned to the idle state
    if (channel->notification_callback_ == OXNAS_DMA_CALLBACK_NUL) {
        must_wait = 1;
        if (!in_atomic) {
            // If the callers is not calling us from atomic context we can
            // register our own callback and sleep until it is invoked
            oxnas_dma_set_callback(channel, default_callback, OXNAS_DMA_CALLBACK_ARG_NUL);
            callback_registered = 1;
        }
    }

    // Fake an interrupt to cause the channel to be cleaned up by running the
    // DMA bottom half tasklet
    fake_interrupt(channel_number);

    if (must_wait) {
        if (callback_registered) {
            // Sleep until the channel becomes inactive
            down_interruptible(&channel->default_semaphore_);

            // Deregister the callback
            oxnas_dma_set_callback(channel, OXNAS_DMA_CALLBACK_NUL, OXNAS_DMA_CALLBACK_ARG_NUL);
        } else {
            // If we reach here we are in an atomic context and thus must not do
            // anything that might cause us to sleep
            // NB. Possible problem here if we're atomic because someone has
            // called spin_lock_bh(); I'm concerned that calling do_softirq()
            // under these circumstances might cause issues, althought the net-
            // working code calls do_softirq() and doesn't appear to worry
            if (local_softirq_pending()) {
                // If an interrupt has not arrived and caused the tasklet to
                // have been run already, cause it to run now.
                do_softirq();
            }

            // The tasklet should have run by this point and cleaned up the channel
            BUG_ON(oxnas_dma_is_active(channel));
        }
    }
}
#endif

#if 0
void oxnas_dma_start(oxnas_dma_channel_t* channel)
{
    // Are there SG lists setup for this channel?
    if (channel->v_sg_info_->v_srcEntries_) {
		// Write to the SG-DMA channel's reset register to reset the control
		// in case the previous SG-DMA transfer failed in some way, thus
		// leaving the SG-DMA controller hung up part way through processing
		// its SG list. The reset bits are self-clearing
		writel(1UL << DMA_SG_RESETS_CONTROL_BIT, DMA_SG_CALC_REG_ADR(channel->channel_number_, DMA_SG_RESETS));

        // Write the pointer to the SG info struct into the Request Pointer reg.
        writel(channel->p_sg_info_, DMA_SG_CALC_REG_ADR(channel->channel_number_, DMA_SG_REQ_PTR));

        // Start the transfer
        writel((1UL << DMA_SG_CONTROL_START_BIT) |
               (1UL << DMA_SG_CONTROL_QUEUING_ENABLE_BIT) |
               (1UL << DMA_SG_CONTROL_HBURST_ENABLE_BIT),
               DMA_SG_CALC_REG_ADR(channel->channel_number_, DMA_SG_CONTROL));
    }
}
#endif

static void oxnas_dma_start_next(oxnas_dma_channel_t *channel);

static irqreturn_t oxnas_dma_interrupt(int irq, void *dev_id)
{
	oxnas_dma_channel_t *channel = dev_id;
	oxnas_dma_device_t *dmadev = channel->dmadev;
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

	spin_lock_irqsave(&channel->vc.lock, flags);

	if (atomic_read(&channel->active)) {
		oxnas_adma_desc_t *cur = channel->cur;
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
	oxnas_adma_desc_t *desc;
	unsigned long ctrl_status;
	
	if (!vd) {
		channel->cur = NULL;
		return;
	}

	list_del(&vd->node);

	channel->cur = desc = container_of(&vd->tx, oxnas_adma_desc_t, vd.tx);

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

static struct dma_async_tx_descriptor *oxnas_adma_prep_slave_sg(
	struct dma_chan *chan, struct scatterlist *sgl, unsigned sglen,
	enum dma_transfer_direction dir, unsigned long tx_flags, void *context)
{
	oxnas_dma_channel_t *channel = container_of(chan, oxnas_dma_channel_t, vc.chan);
	oxnas_dma_device_t *dmadev = channel->dmadev;
	oxnas_adma_desc_t *desc;

	desc = kzalloc(sizeof(oxnas_adma_desc_t), GFP_KERNEL);
	if (unlikely(!desc))
		return NULL;

	return NULL;
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

/** Allocate descriptors capable of mapping the requested length of memory */
static struct dma_async_tx_descriptor 
		*oxnas_adma_prep_dma_memcpy(struct dma_chan *chan, 
					    dma_addr_t dst, dma_addr_t src,
					    size_t len, unsigned long flags)
{
	oxnas_dma_channel_t *channel = container_of(chan, oxnas_dma_channel_t, vc.chan);
	oxnas_dma_device_t *dmadev = channel->dmadev;
	oxnas_adma_desc_t *desc;
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
	
	desc = kzalloc(sizeof(oxnas_adma_desc_t), GFP_KERNEL);
	if (unlikely(!desc))
		return NULL;

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

static void oxnas_dma_desc_free(struct virt_dma_desc *vd)
{
	kfree(container_of(vd, oxnas_adma_desc_t, vd));
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
		oxnas_adma_desc_t *desc = container_of(&vd->tx, oxnas_adma_desc_t, vd.tx);
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
static void oxnas_adma_issue_pending(struct dma_chan *chan)
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

static int oxnas_adma_probe(struct platform_device *pdev)
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
#if 0
    	spin_lock_init(&dmadev->alloc_spinlock);
    	spin_lock_init(&dmadev->channel_alloc_spinlock);
    	
	sema_init(&dmadev->csum_engine_sem, 1);

	/* Initialise channel allocation management */
	dmadev->channel_head = 0;
	sema_init(&dmadev->channel_sem, 0);

	/* Initialise SRAM buffer management */
	dmadev->sg_entry_head = 0;
	sema_init(&dmadev->sg_entry_sem, 0);
	dmadev->sg_entry_available = 0;
#endif

	tasklet_init(&dmadev->tasklet, oxnas_dma_sched, (unsigned long)dmadev);
	INIT_LIST_HEAD(&dmadev->common.channels);
	INIT_LIST_HEAD(&dmadev->pending);

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

#if 0
	/* Allocate coherent memory for sg descriptors */
	dmadev->sg_data = dma_alloc_coherent(&pdev->dev, sizeof(oxnas_dma_sg_data_t),
					     &dmadev->p_sg_data, GFP_KERNEL);
	if (!dmadev->sg_data) {
		dev_err(&pdev->dev, "unable to allocate coherent\n");
		return -ENOMEM;
	}

	/* Reset SG descritors */
	memset(dmadev->sg_data, 0, sizeof(oxnas_dma_sg_data_t));
#endif

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

		ret = devm_request_irq(&pdev->dev, ch->irq, oxnas_dma_interrupt, 0, "DMA", ch);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to request irq%d\n", i);
			goto probe_err;
		}
		
#if 0
		ch->p_sg_info = (dma_addr_t)&((oxnas_dma_sg_data_t *)dmadev->p_sg_data)->infos[i];
		ch->sg_info = &dmadev->sg_data->infos[i];
		memset(ch->sg_info, 0, sizeof(oxnas_dma_sg_info_t));
		wmb();
#endif

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
	dmadev->common.device_issue_pending = oxnas_adma_issue_pending;
	dmadev->common.device_prep_dma_memcpy = oxnas_adma_prep_dma_memcpy;
	dmadev->common.device_prep_slave_sg = oxnas_adma_prep_slave_sg;
	dmadev->common.copy_align = DMAENGINE_ALIGN_4_BYTES;
	dmadev->common.src_addr_widths = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dmadev->common.dst_addr_widths = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dmadev->common.directions = BIT(DMA_MEM_TO_MEM);
	dmadev->common.residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;
	dmadev->common.dev = &pdev->dev;

	ret = dma_async_device_register(&dmadev->common);
	if (ret)
		goto probe_err;
	
	dev_info(&pdev->dev, "OXNAS DMA Registered\n");

	return 0;

probe_err:
	/*dma_free_coherent(&pdev->dev, sizeof(oxnas_dma_sg_data_t),
			  &dmadev->p_sg_data, GFP_KERNEL);*/

	return ret;
}

static int oxnas_adma_remove(struct platform_device *dev)
{
	oxnas_dma_device_t *dmadev = platform_get_drvdata(dev);
	//oxnas_dma_channel_t *ch;

	dma_async_device_unregister(&dmadev->common);

	/*
	   TODO
	list_for_each_entry_safe(ch, _channel, &dmadev->common.channels, device_node) {
		list_del(&ch->device_node);
	}
	kfree(oxnas_adma_device);
	*/

	return 0;
}

static const struct of_device_id oxnas_adma_of_dev_id[] = {
	{ .compatible = "plxtech,nas782x-dma", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_dma_of_dev_id);

static struct platform_driver oxnas_adma_driver = {
	.probe		= oxnas_adma_probe,
	.remove		= oxnas_adma_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "oxnas-adma",
		.of_match_table = oxnas_adma_of_dev_id,
	},
};

static int __init oxnas_adma_init_module(void)
{
	return platform_driver_register(&oxnas_adma_driver);
}
subsys_initcall(oxnas_adma_init_module);

static void __exit oxnas_adma_exit_module(void)
{
	platform_driver_unregister(&oxnas_adma_driver);
	return;
}
module_exit(oxnas_adma_exit_module);

MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oxford Semiconductor Ltd.");
