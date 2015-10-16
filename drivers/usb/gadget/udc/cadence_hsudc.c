/*
 *  linux/drivers/usb/gadget/udc/cadence_hsudc.c
 *  - Cadence USB2.0 Device Controller Driver
 *
 *  Copyright (C) 2015 Neotion
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/sysfs.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#include "cadence_hsudc_regs.h"

/* Driver Status
 *
 * Managed :
 *  - EP0 Endpoint Status, Clear/Set Feature and Gadget stack pass-through
 *  - EP IN/OUT 1 to 15 with hardware caps
 *  - EP Bulk and Interrupt tranfer
 *  - DMA in normal mode, with auto-arm
 *  - Endpoint Halting (from Gadget stack or EP0 Setup)
 *  - HW config via device tree
 *
 *  TODOs :
 *  - LPM
 *  - USB Suspend/Wakeup
 *  - IP config like AHB master configuration
 *
 *  Not (Never?) Supported :
 *  - Isochronous (No Hardware available)
 *  - OTG/OTG2 (No Hardware available)
 *  - Host Mode (No Hardware available)
 *  - Configuration FSM (No Hardware available)
 *
 */

#define DMA_ADDR_INVALID        (~(dma_addr_t)0)

struct cadence_hsudc_request {
	struct usb_request req;
	struct list_head queue;
};

struct cadence_hsudc;
struct cadence_hsudc_ep;

struct hsudc_dma_channel {
	struct cadence_hsudc_ep *cur_ep;

	int num;
	int is_available;

	int in_use;
};

struct cadence_hsudc_ep {
	struct cadence_hsudc *hsudc_dev;
	const struct usb_endpoint_descriptor *desc;
	struct usb_ep ep;
	int num;
	int is_in;
	int is_ep0;
	int is_available;

	struct list_head queue;
	struct cadence_hsudc_request *cur;
	spinlock_t s;

	struct work_struct ws;
	struct work_struct comp;

	int maxpacket;

	struct hsudc_dma_channel *dma_channel;
	int use_dma;
};

struct hsudc_hw_config {
	unsigned ep_in_exist[HSUDC_EP_COUNT];
	unsigned ep_out_exist[HSUDC_EP_COUNT];
	unsigned ep_in_size[HSUDC_EP_COUNT];
	unsigned ep_out_size[HSUDC_EP_COUNT];
	unsigned ep_in_buffering[HSUDC_EP_COUNT];
	unsigned ep_out_buffering[HSUDC_EP_COUNT];
	unsigned ep_in_startbuff[HSUDC_EP_COUNT];
	unsigned ep_out_startbuff[HSUDC_EP_COUNT];
	unsigned dma_enabled;
	unsigned dma_channels;
};

struct cadence_hsudc {
	struct platform_device *pdev;
	void __iomem *io_base;
	const struct hsudc_hw_config *hw_config;
	int irq;

	struct usb_gadget_driver *driver;
	struct usb_gadget gadget;

	struct cadence_hsudc_ep ep_in[HSUDC_EP_COUNT];	/* 0 is not available */
	struct cadence_hsudc_ep ep_out[HSUDC_EP_COUNT];	/* 0 is not available */
	struct cadence_hsudc_ep ep0;
	struct work_struct ep0_setup;

	struct workqueue_struct *wq_ep;

	struct hsudc_dma_channel dma_channels[HSUDC_DMA_CHANNELS];
	struct semaphore dma_sem;
	spinlock_t dma_s;
};

/* Register Access */
#define hsudc_write8(value, reg) \
		writeb((value)&0xFF, hsudc_dev->io_base + (reg))
#define hsudc_write16(value, reg) \
		writew((value)&0xFFFF, hsudc_dev->io_base + (reg))
#define hsudc_write32(value, reg) \
		writel((value), hsudc_dev->io_base + (reg))

#define hsudc_read8(reg) readb(hsudc_dev->io_base + (reg))
#define hsudc_read16(reg) readw(hsudc_dev->io_base + (reg))
#define hsudc_read32(reg) readl(hsudc_dev->io_base + (reg))

static inline void cadence_hsudc_dma_irq(struct cadence_hsudc *hsudc_dev,
					 unsigned dma_channel,
					 unsigned dmairq, dmashortirq)
{
	struct hsudc_dma_channel *channel =
		&hsudc_dev->dma_channels[dma_channel];

	if ((dmairq & (1 << i))) {
		/* Clear and disable DMAIRQ */
		hsudc_write32(1 << i, HSUDC_DMA_IRQ_REG32);
		hsudc_write32(hsudc_read32(HSUDC_DMA_IEN_REG32) &
				~(1 << dma_channel),
				HSUDC_DMA_IEN_REG32);
	}
	if ((dmashortirq & (1 << dma_channel))) {
		/* Clear and disable DMASHORTIRQ */
		hsudc_write32(1 << dma_channel, HSUDC_DMA_SHORTIRQ_REG32);
		hsudc_write32(hsudc_read32(HSUDC_DMA_SHORTIEN_REG32) &
				~(1 << dma_channel),
				HSUDC_DMA_SHORTIEN_REG32);
	}
	if (channel->is_available &
			channel->in_use &&
			channel->cur_ep->cur) {
		struct cadence_hsudc_request *hsudc_req =
			channel->cur_ep->cur;
		unsigned remain =
			hsudc_read32(HSUDC_DMA_CNT_REG32(dma_channel));

		hsudc_req->req.actual = hsudc_req->req.length - remain;

		queue_work(hsudc_dev->wq_ep,
				&channel->cur_ep->comp);

		channel->cur_ep->dma_channel = NULL;
		channel->cur_ep = NULL;
		channel->in_use = 0;

		/* Free DMA channel */
		up(&hsudc_dev->dma_sem);
	}
}

static irqreturn_t cadence_hsudc_irq(int irq, void *data)
{
	struct cadence_hsudc *hsudc_dev = data;

	unsigned in_packet_irq;
	unsigned out_packet_irq;
	unsigned usbirq;
	unsigned dmairq, dmashortirq;

	(void)irq;

	in_packet_irq = hsudc_read16(HSUDC_INIRQ_REG16) &
			hsudc_read16(HSUDC_INIEN_REG16);
	out_packet_irq = hsudc_read16(HSUDC_OUTIRQ_REG16) &
			 hsudc_read16(HSUDC_OUTIEN_REG16);
	usbirq = hsudc_read8(HSUDC_USBIRQ_REG8) &
		 hsudc_read8(HSUDC_USBIEN_REG8);

	dmairq = hsudc_read32(HSUDC_DMA_IRQ_REG32) &
		 hsudc_read32(HSUDC_DMA_IEN_REG32);
	dmashortirq = hsudc_read32(HSUDC_DMA_SHORTIRQ_REG32) &
		      hsudc_read32(HSUDC_DMA_SHORTIEN_REG32);

	dev_vdbg(&hsudc_dev->pdev->dev, "irq: in %04X out %04X usb %04X dma %x/%x\n",
			in_packet_irq, out_packet_irq,
			usbirq, dmairq, dmashortirq);

	if (dmairq || dmashortirq) {
		unsigned i;

		for (i = 0; i < hsudc_dev->hw_config->dma_channels; ++i)
			if ((dmairq & (1 << i)) || (dmashortirq & (1 << i)))
				cadence_hsudc_dma_irq(hsudc_dev, i,
						      dmairq, dmashortirq);
	}

	if (in_packet_irq || out_packet_irq) {
		unsigned i;

		/* Handle EP0 */
		if ((out_packet_irq & 1) || (in_packet_irq & 1)) {
			/* Clear IRQ */
			if (out_packet_irq & 1)
				hsudc_write16(1, HSUDC_OUTIRQ_REG16);
			else
				hsudc_write16(1, HSUDC_INIRQ_REG16);

			queue_work(hsudc_dev->wq_ep, &hsudc_dev->ep0.comp);
		}

		for (i = 1; i < HSUDC_EP_COUNT; ++i) {
			if (out_packet_irq & (1 << i)) {
				/* Clear IRQ */
				hsudc_write16(1 << i, HSUDC_OUTIRQ_REG16);

				if (hsudc_dev->ep_out[i].cur)
					queue_work(hsudc_dev->wq_ep,
						   &hsudc_dev->ep_out[i].comp);
			}

			if (in_packet_irq & (1 << i)) {
				/* Clear IRQ */
				hsudc_write16(1 << i, HSUDC_INIRQ_REG16);

				if (hsudc_dev->ep_in[i].cur)
					queue_work(hsudc_dev->wq_ep,
						   &hsudc_dev->ep_in[i].comp);
			}
		}
	}

	/* Clear All USB IRQs */
	hsudc_write8(usbirq, HSUDC_USBIRQ_REG8);

	if (usbirq & HSUDC_USBIRQ_URES_MSK) {
		dev_dbg(&hsudc_dev->pdev->dev, "irq: RESET\n");
		hsudc_dev->gadget.speed = USB_SPEED_FULL;
	}

	if (usbirq & HSUDC_USBIRQ_HSPPED_MSK) {
		/* High Speed indicator */
		dev_dbg(&hsudc_dev->pdev->dev, "irq: HSPPED\n");
		hsudc_dev->gadget.speed = USB_SPEED_HIGH;
	}

	if (usbirq & HSUDC_USBIRQ_SUDAV_MSK)
		/* Queue SETUP work */
		dev_vdbg(&hsudc_dev->pdev->dev, "irq: SUDAV\n");

	if (usbirq & HSUDC_USBIRQ_SUTOK_MSK) {
		dev_vdbg(&hsudc_dev->pdev->dev, "irq: SUTOK\n");
		queue_work(hsudc_dev->wq_ep, &hsudc_dev->ep0_setup);
	}

	if (usbirq & HSUDC_USBIRQ_SOF_MSK)
		dev_vdbg(&hsudc_dev->pdev->dev, "irq: SOF\n");

	if (usbirq & HSUDC_USBIRQ_SUSP_MSK)
		/* TODO handle suspended */
		dev_vdbg(&hsudc_dev->pdev->dev, "irq: SUSP\n");

	return IRQ_HANDLED;
}

static int hsudc_dma_get_channel(struct cadence_hsudc *hsudc_dev,
				 struct cadence_hsudc_ep *hsudc_ep)
{
	unsigned i;

	spin_lock(&hsudc_dev->dma_s);

	/* Get DMA */
	down(&hsudc_dev->dma_sem);

	for (i = 0; i < hsudc_dev->hw_config->dma_channels; ++i) {
		if (hsudc_dev->dma_channels[i].is_available
		    && !hsudc_dev->dma_channels[i].in_use) {
			hsudc_dev->dma_channels[i].in_use = 1;
			hsudc_dev->dma_channels[i].cur_ep = hsudc_ep;
			hsudc_ep->dma_channel = &hsudc_dev->dma_channels[i];
			hsudc_ep->use_dma = 1;

			dev_vdbg(&hsudc_dev->pdev->dev, "%s(): ep%d%s got dma channel %d for req %p\n",
					__func__,
					hsudc_ep->num,
					(hsudc_ep->is_in?"in":"out"),
					i, hsudc_ep->cur);

			spin_unlock(&hsudc_dev->dma_s);

			return 0;
		}
	}

	dev_err(&hsudc_dev->pdev->dev, "%s(): error failed to get dma channel\n",
		__func__);

	up(&hsudc_dev->dma_sem);

	spin_unlock(&hsudc_dev->dma_s);

	return -1;
}

static int hsudc_dma_init(struct cadence_hsudc *hsudc_dev,
			   struct cadence_hsudc_ep *hsudc_ep,
			   struct cadence_hsudc_request *hsudc_req)
{
	int ret;

	/* Map buffer as DMA address */
	hsudc_req->req.dma = dma_map_single(hsudc_dev->gadget.dev.parent,
					    hsudc_req->req.buf,
					    hsudc_req->req.length,
					    hsudc_ep->is_in ?
						DMA_TO_DEVICE :
						DMA_FROM_DEVICE);

	ret = dma_mapping_error(hsudc_dev->gadget.dev.parent,
				hsudc_req->req.dma);
	if (ret) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): dma mapping error %d\n",
				__func__, ret);

		hsudc_ep->use_dma = 0;
		return ret;
	}

	/* Configure DMA direction, EP, address and mode */
	hsudc_write32(hsudc_req->req.dma,
		      HSUDC_DMA_ADDR_REG32(hsudc_ep->dma_channel->num));
	hsudc_write32(hsudc_req->req.length,
		      HSUDC_DMA_CNT_REG32(hsudc_ep->dma_channel->num));

	/* Mode normal, incremental address */
	if (hsudc_ep->is_in)
		hsudc_write8(HSUDC_DMA_MODE_DIRECTION_IN |
			     HSUDC_DMA_MODE_ADDRESS_INC,
			     HSUDC_DMA_MODE_REG8(hsudc_ep->dma_channel->num));
	else
		hsudc_write8(HSUDC_DMA_MODE_ADDRESS_INC,
			     HSUDC_DMA_MODE_REG8(hsudc_ep->dma_channel->num));

	hsudc_write8(hsudc_ep->num << HSUDC_DMA_ENDP_SHIFT,
		     HSUDC_DMA_ENDP_REG8(hsudc_ep->dma_channel->num));

	/* TODO HSUDC_DMA_BUSCTRL_REG8 */

	/* Enable DMAIRQ, DMASHORTIRQ */
	hsudc_write32(hsudc_read32(HSUDC_DMA_IEN_REG32) |
		      (1 << hsudc_ep->dma_channel->num), HSUDC_DMA_IEN_REG32);
	hsudc_write32(hsudc_read32(HSUDC_DMA_SHORTIEN_REG32) |
		      (1 << hsudc_ep->dma_channel->num),
		      HSUDC_DMA_SHORTIEN_REG32);

	return 0;
}

/*
 * Enable, Configure and Reset endpoint
 */
static int cadence_hsudc_ep_enable(struct usb_ep *ep,
			       const struct usb_endpoint_descriptor *desc)
{
	struct cadence_hsudc *hsudc_dev;
	struct cadence_hsudc_ep *hsudc_ep;
	uint16_t maxpacket;
	uint32_t tmp;

	hsudc_ep = container_of(ep, struct cadence_hsudc_ep, ep);
	hsudc_dev = hsudc_ep->hsudc_dev;

	maxpacket = le16_to_cpu(desc->wMaxPacketSize);

	if (!ep || !hsudc_ep) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error bad ep\n",
			__func__);
		return -EINVAL;
	}

	if (!desc || hsudc_ep->desc) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error bad descriptor\n",
			__func__);
		return -EINVAL;
	}

	if (!hsudc_ep->num) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error ep[0]\n",
			__func__);
		return -EINVAL;
	}

	if (((desc->bEndpointAddress & USB_DIR_IN) == USB_DIR_IN &&
	     hsudc_ep->is_in == 0) ||
	    ((desc->bEndpointAddress & USB_DIR_IN) == 0 &&
	     hsudc_ep->is_in == 1)) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error invalid direction\n",
			__func__);
		return -EINVAL;
	}

	if (desc->bDescriptorType != USB_DT_ENDPOINT) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error not USB_DT_ENDPOINT\n",
			__func__);
		return -EINVAL;
	}

	if (!maxpacket || maxpacket > hsudc_ep->maxpacket) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error maxpacket %d\n",
			__func__,
			maxpacket);
		return -EINVAL;
	}

	if (!hsudc_dev->driver ||
		hsudc_dev->gadget.speed == USB_SPEED_UNKNOWN) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error bogus device state\n",
			__func__);
		return -ESHUTDOWN;
	}

	tmp = desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
	switch (tmp) {
	case USB_ENDPOINT_XFER_CONTROL:
		dev_err(&hsudc_dev->pdev->dev,
			"%s(): error only one control endpoint\n", __func__);
		return -EINVAL;
	case USB_ENDPOINT_XFER_INT:
		if (maxpacket > hsudc_ep->maxpacket) {
			dev_err(&hsudc_dev->pdev->dev,
				"%s(): error '%s', bogus maxpacket %d for XFER_INT\n",
				__func__, hsudc_ep->ep.name, maxpacket);
			return -EINVAL;
		}
		break;
	case USB_ENDPOINT_XFER_BULK:
		if (maxpacket > hsudc_ep->maxpacket) {
			dev_err(&hsudc_dev->pdev->dev,
				"%s(): error '%s', bogus maxpacket %d for XFER_BULK\n",
				__func__, hsudc_ep->ep.name, maxpacket);
			return -EINVAL;
		}
		break;
	case USB_ENDPOINT_XFER_ISOC:
		dev_err(&hsudc_dev->pdev->dev,
			"%s(): error USB_ENDPOINT_XFER_ISOC not supported yet.\n",
			__func__);
		return -EINVAL;
	}

	/* initialize endpoint to match this descriptor */
	hsudc_ep->desc = desc;
	hsudc_ep->ep.maxpacket = maxpacket;
	spin_lock_init(&hsudc_ep->s);

	dev_dbg(&hsudc_dev->pdev->dev, "%s(): '%s', is_in %d, maxpacket %d\n",
		__func__, hsudc_ep->ep.name, hsudc_ep->is_in, maxpacket);

	if (hsudc_ep->is_in) {
		unsigned val =
		    hsudc_dev->hw_config->ep_in_buffering[hsudc_ep->num] &
		    HSUDC_EP_CON_BUF_MSK;

		/* Set EP type */
		if ((desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
		    USB_ENDPOINT_XFER_INT)
			val |= HSUDC_EP_CON_TYPE_INTERRUPT;
		else
			val |= HSUDC_EP_CON_TYPE_BULK;

		/* Enable EP */
		val |= HSUDC_EP_CON_VAL_MSK;

		hsudc_write8(val, HSUDC_EP_INCON_REG8(hsudc_ep->num));

		hsudc_write16(hsudc_ep->ep.maxpacket,
			      HSUDC_EP_IN_MAXPCK_REG16(hsudc_ep->num));

		/* Select endpoint */
		hsudc_write8(hsudc_ep->num | HSUDC_ENDPRST_IO_MSK,
			     HSUDC_ENDPRST_REG8);

		/* Reset endpoint */
		hsudc_write8(hsudc_ep->num |
			     HSUDC_ENDPRST_IO_MSK |
			     HSUDC_ENDPRST_TOGRST_MSK |
			     HSUDC_ENDPRST_FIFORST_MSK, HSUDC_ENDPRST_REG8);
	} else {
		unsigned val =
		    hsudc_dev->hw_config->ep_out_buffering[hsudc_ep->
							 num] &
		    HSUDC_EP_CON_BUF_MSK;

		/* Set EP type */
		if ((desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
		    USB_ENDPOINT_XFER_INT)
			val |= HSUDC_EP_CON_TYPE_INTERRUPT;
		else
			val |= HSUDC_EP_CON_TYPE_BULK;

		/* Enable EP */
		val |= HSUDC_EP_CON_VAL_MSK;

		hsudc_write8(val, HSUDC_EP_OUTCON_REG8(hsudc_ep->num));

		hsudc_write16(hsudc_ep->ep.maxpacket,
			      HSUDC_EP_OUT_MAXPCK_REG16(hsudc_ep->num));

		/* Select endpoint */
		hsudc_write8(hsudc_ep->num, HSUDC_ENDPRST_REG8);

		/* Reset endpoint */
		hsudc_write8(hsudc_ep->num | HSUDC_ENDPRST_TOGRST_MSK |
			     HSUDC_ENDPRST_FIFORST_MSK, HSUDC_ENDPRST_REG8);
	}

	return 0;
}

/*
 * Disable and Reset endpoint
 */
static int cadence_hsudc_ep_disable(struct usb_ep *ep)
{
	struct cadence_hsudc *hsudc_dev;
	struct cadence_hsudc_ep *hsudc_ep;
	struct cadence_hsudc_request *req;

	hsudc_ep = container_of(ep, struct cadence_hsudc_ep, ep);
	hsudc_dev = hsudc_ep->hsudc_dev;

	spin_lock(&hsudc_ep->s);

	hsudc_ep->desc = NULL;
	if (hsudc_ep->cur) {
		hsudc_ep->cur->req.status = -ESHUTDOWN;
		dev_dbg(&hsudc_dev->pdev->dev, "%s(): nuked cur %p\n",
			__func__,
			hsudc_ep->cur);
		queue_work(hsudc_dev->wq_ep, &hsudc_ep->comp);
	}

	while (!list_empty(&hsudc_ep->queue)) {
		req = list_entry(hsudc_ep->queue.next,
				 struct cadence_hsudc_request,
				 queue);
		list_del_init(&req->queue);

		if (req == hsudc_ep->cur)
			continue;

		req->req.status = -ESHUTDOWN;
		req->req.complete(&hsudc_ep->ep, &req->req);
		dev_dbg(&hsudc_dev->pdev->dev, "%s(): nuked %p\n",
			__func__,
			req);
	}

	dev_dbg(&hsudc_dev->pdev->dev, "%s(): '%s'\n", __func__,
		hsudc_ep->ep.name);
	hsudc_ep->ep.maxpacket = hsudc_ep->maxpacket;
	INIT_LIST_HEAD(&hsudc_ep->queue);

	if (hsudc_ep->is_in) {
		hsudc_write8(0, HSUDC_EP_INCON_REG8(hsudc_ep->num));

		/* Select endpoint */
		hsudc_write8(hsudc_ep->num | HSUDC_ENDPRST_IO_MSK,
			     HSUDC_ENDPRST_REG8);

		/* Reset endpoint */
		hsudc_write8(hsudc_ep->num | HSUDC_ENDPRST_IO_MSK |
			     HSUDC_ENDPRST_TOGRST_MSK |
			     HSUDC_ENDPRST_FIFORST_MSK, HSUDC_ENDPRST_REG8);
	} else {
		hsudc_write8(0, HSUDC_EP_OUTCON_REG8(hsudc_ep->num));

		/* Select endpoint */
		hsudc_write8(hsudc_ep->num, HSUDC_ENDPRST_REG8);

		/* Reset endpoint */
		hsudc_write8(hsudc_ep->num | HSUDC_ENDPRST_TOGRST_MSK |
			     HSUDC_ENDPRST_FIFORST_MSK, HSUDC_ENDPRST_REG8);
	}

	spin_unlock(&hsudc_ep->s);
	return 0;

}

/*
 * Allocate request internal structure
 */
static struct usb_request *cadence_hsudc_ep_alloc_request(struct usb_ep *ep,
						      unsigned int gfp_flags)
{
	struct cadence_hsudc *hsudc_dev;
	struct cadence_hsudc_ep *hsudc_ep;
	struct cadence_hsudc_request *req;

	hsudc_ep = container_of(ep, struct cadence_hsudc_ep, ep);
	hsudc_dev = hsudc_ep->hsudc_dev;

	req = kzalloc(sizeof(struct cadence_hsudc_request), gfp_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);
	req->req.dma = 0xFFFFFFFF;

	dev_vdbg(&hsudc_dev->pdev->dev, "%s(): %p @ '%s'\n", __func__,
		&req->req,
		hsudc_ep->ep.name);

	return &req->req;
}

/*
 * Free request internal structure
 */
static void cadence_hsudc_ep_free_request(struct usb_ep *ep,
				      struct usb_request *req)
{
	struct cadence_hsudc *hsudc_dev;
	struct cadence_hsudc_ep *hsudc_ep;
	struct cadence_hsudc_request *hsudc_req;

	hsudc_ep = container_of(ep, struct cadence_hsudc_ep, ep);
	hsudc_dev = hsudc_ep->hsudc_dev;

	hsudc_req = container_of(req, struct cadence_hsudc_request, req);

	dev_vdbg(&hsudc_dev->pdev->dev, "%s(): %p @ '%s'\n", __func__,
		&hsudc_req->req,
		hsudc_ep->ep.name);

	kfree(hsudc_req);
}

/*
 * Continue/Completion work for ep0
 * If some more data must be read/pushed, restart ep or complete
 * At end of request, ACK status request and STALL data requests
 */
static void hsudc_ep0_completion(struct work_struct *work)
{
	struct cadence_hsudc *hsudc_dev;
	struct cadence_hsudc_ep *hsudc_ep;
	struct cadence_hsudc_request *hsudc_req;

	hsudc_ep = container_of(work, struct cadence_hsudc_ep, comp);
	hsudc_dev = hsudc_ep->hsudc_dev;
	hsudc_req = hsudc_ep->cur;

	/* Should be a get status implicit request */
	if (hsudc_req == NULL) {
		/* Disable IRQ */
		hsudc_write16(hsudc_read16(HSUDC_OUTIEN_REG16) & ~1,
			      HSUDC_OUTIEN_REG16);
		hsudc_write16(hsudc_read16(HSUDC_INIEN_REG16) & ~1,
			      HSUDC_INIEN_REG16);

		/* Finish control transaction */
		hsudc_write8(HSUDC_EP0_CS_HSNAK_MSK,
				HSUDC_EP0_CS_REG8);

		return;
	}

	dev_vdbg(&hsudc_dev->pdev->dev, "%s(): %p @ '%s'\n", __func__,
		&hsudc_req->req,
		hsudc_ep->ep.name);

	if (!hsudc_ep->is_in) {
		/* Retrieve data from FIFO */
		uint8_t *buf = hsudc_req->req.buf + hsudc_req->req.actual;
		unsigned length = hsudc_read8(HSUDC_EP0_OUTBC_REG8);
		unsigned i;

		/* copy data in ep fifo */
		for (i = 0; i < length; ++i)
			buf[i] = hsudc_read8(HSUDC_EP0_OUTBUF_BASE_REG + i);

		hsudc_req->req.actual += length;

		if (hsudc_req->req.actual < hsudc_req->req.length) {
			length = hsudc_req->req.length - hsudc_req->req.actual;

			if (length > hsudc_ep->maxpacket)
				length = hsudc_ep->maxpacket;

			hsudc_write8(length, HSUDC_EP0_OUTBC_REG8);

			return;
		}

		/* Disable IRQ */
		hsudc_write16(hsudc_read16(HSUDC_OUTIEN_REG16) & ~1,
			      HSUDC_OUTIEN_REG16);
	} else {
		if (hsudc_req->req.actual < hsudc_req->req.length) {
			uint8_t *buf =
			    hsudc_req->req.buf + hsudc_req->req.actual;
			unsigned length =
			    hsudc_req->req.length - hsudc_req->req.actual;
			unsigned i;

			if (length > hsudc_ep->maxpacket)
				length = hsudc_ep->maxpacket;

			/* copy data in ep0 fifo */
			for (i = 0; i < length; ++i)
				hsudc_write8(buf[i],
					     HSUDC_EP0_INBUF_BASE_REG + i);

			hsudc_req->req.actual += length;

			/* Load byte size */
			hsudc_write8(length, HSUDC_EP0_INBC_REG8);

			return;
		}

		/* Disable IRQ */
		hsudc_write16(hsudc_read16(HSUDC_INIEN_REG16) & ~1,
			      HSUDC_INIEN_REG16);
	}

	/* Finish control transaction */
	hsudc_write8(HSUDC_EP0_CS_HSNAK_MSK,
		     HSUDC_EP0_CS_REG8);

	spin_lock(&hsudc_ep->s);

	if (hsudc_ep->cur) {
		hsudc_req = hsudc_ep->cur;
		hsudc_ep->cur = NULL;
		spin_unlock(&hsudc_ep->s);

		hsudc_req->req.status = 0;
		hsudc_req->req.complete(&hsudc_ep->ep, &hsudc_req->req);
	} else
		spin_unlock(&hsudc_ep->s);
}

static void hsudc_ep0_work(struct work_struct *work)
{
	BUG();
}

static int hsudc_ep0_queue(struct cadence_hsudc *hsudc_dev,
			   struct cadence_hsudc_request *req)
{
	dev_vdbg(&hsudc_dev->pdev->dev, "%s(): %s length %d actual %d\n",
			__func__,
			(hsudc_dev->ep0.is_in?"IN":"OUT"),
			req->req.length, req->req.actual);

	spin_lock(&hsudc_dev->ep0.s);

	hsudc_dev->ep0.cur = req;

	if (!req->req.length) {
		/* Finish control transaction */
		hsudc_write8(HSUDC_EP0_CS_HSNAK_MSK,
			     HSUDC_EP0_CS_REG8);

		spin_unlock(&hsudc_dev->ep0.s);
		return 0;
	}

	spin_unlock(&hsudc_dev->ep0.s);

	if (hsudc_dev->ep0.is_in) {
		uint8_t *buf = req->req.buf + req->req.actual;
		unsigned length = req->req.length - req->req.actual;
		unsigned i;

		if (length > hsudc_dev->ep0.maxpacket)
			length = hsudc_dev->ep0.maxpacket;

		/* copy data in ep0 fifo */
		for (i = 0; i < length; ++i)
			hsudc_write8(buf[i], HSUDC_EP0_INBUF_BASE_REG + i);

		req->req.actual += length;

		/* Clear and enable ep0 in irq */
		hsudc_write16(1, HSUDC_INIRQ_REG16);
		hsudc_write16(hsudc_read16(HSUDC_INIEN_REG16) | 1,
			      HSUDC_INIEN_REG16);

		/* Load byte size */
		hsudc_write8(length, HSUDC_EP0_INBC_REG8);
	} else {
		unsigned length = req->req.length - req->req.actual;

		if (length > hsudc_dev->ep0.maxpacket)
			length = hsudc_dev->ep0.maxpacket;

		/* Clear and enable ep0 out irq */
		hsudc_write16(1, HSUDC_OUTIRQ_REG16);
		hsudc_write16(hsudc_read16(HSUDC_OUTIEN_REG16) | 1,
			      HSUDC_OUTIEN_REG16);

		/* ARM out ep0, set size */
		hsudc_write8(length, HSUDC_EP0_OUTBC_REG8);
	}

	return 0;
}

static inline void hsudc_copy_to_fifo(struct cadence_hsudc_ep *hsudc_ep,
				      size_t length, void *buf)
{
	int i;
	unsigned reg = HSUDC_FIFODAT_REG32(hsudc_ep->num);
	struct cadence_hsudc *hsudc_dev = hsudc_ep->hsudc_dev;

	/* copy data in ep fifo, with optimized accesses */
	for (i = 0; i < length;) {
		if ((i % 4) == 0 && (length - i) >= 4) {
			hsudc_write32(*(uint32_t *)(&buf[i]), reg);
			i += 4;
		} else if ((i % 2) == 0
				&& (length - i) >= 2) {
			hsudc_write16(*(uint16_t *)(&buf[i]), reg);
			i += 2;
		} else {
			hsudc_write8(*(uint8_t *)(&buf[i]), reg);
			i += 1;
		}
	}
}

static inline void hsudc_copy_from_fifo(struct cadence_hsudc_ep *hsudc_ep,
					size_t length, void *buf)
{
	int i;
	unsigned reg = HSUDC_FIFODAT_REG32(hsudc_ep->num);
	struct cadence_hsudc *hsudc_dev = hsudc_ep->hsudc_dev;

	/* copy data from ep fifo, with optimized accesses */
	for (i = 0; i < length;) {
		if ((i % 4) == 0 && (length - i) >= 4) {
			*((uint32_t *) &buf[i]) = hsudc_read32(reg);
			i += 4;
		} else if ((i % 2) == 0 && (length - i) >= 2) {
			*((uint16_t *) &buf[i]) = hsudc_read16(reg);
			i += 2;
		} else {
			buf[i] = hsudc_read8(reg);
			i += 1;
		}
	}
}

/*
 * Continue/Completion work
 * If some more data must be read/pushed, restart ep or complete
 * If another request is available, run ep_work to start it
 */
static void hsudc_ep_completion(struct work_struct *work)
{
	struct cadence_hsudc *hsudc_dev;
	struct cadence_hsudc_ep *hsudc_ep;
	struct cadence_hsudc_request *hsudc_req;

	hsudc_ep = container_of(work, struct cadence_hsudc_ep, comp);
	hsudc_dev = hsudc_ep->hsudc_dev;

	spin_lock(&hsudc_ep->s);

	hsudc_req = hsudc_ep->cur;

	if (hsudc_req == NULL) {
		spin_unlock(&hsudc_ep->s);
		return;
	}

	if (hsudc_req->req.status != -EINPROGRESS)
		/* Request was unqueued */
		goto req_complete;

	dev_vdbg(&hsudc_dev->pdev->dev, "%s(): ep%d%s req %p/%d:%d\n", __func__,
			hsudc_ep->num, (hsudc_ep->is_in?"in":"out"),
			&hsudc_req->req, hsudc_req->req.length,
			hsudc_req->req.actual);

	if (!hsudc_ep->use_dma && hsudc_ep->is_in) {
		if (hsudc_req->req.actual < hsudc_req->req.length) {
			void *buf =
				hsudc_req->req.buf + hsudc_req->req.actual;
			unsigned length =
				hsudc_req->req.length -
				hsudc_req->req.actual;
			unsigned i;

			if (length > hsudc_ep->ep.maxpacket)
				length = hsudc_ep->ep.maxpacket;

			hsudc_copy_to_fifo(hsudc_ep, length, buf);

			hsudc_req->req.actual += length;

			dev_dbg(&hsudc_dev->pdev->dev,
					"%s(): ep%d%s req %p/%d:%d len %d max %d\n",
					__func__,
					hsudc_ep->num,
					(hsudc_ep->is_in?"in":"out"),
					&hsudc_req->req, hsudc_req->req.length,
					hsudc_req->req.actual,
					length, hsudc_ep->ep.maxpacket);

			/* ARM out ep,
			 * set busy bit to enable sending to the host
			 */
			hsudc_write8(0x00,
					HSUDC_EP_INCS_REG8(hsudc_ep->num));

			spin_unlock(&hsudc_ep->s);
			return;
		}
	} else if (!hsudc_ep->use_dma && !hsudc_ep->is_in) {
		/* Retrieve data from FIFO */
		void *buf =
			hsudc_req->req.buf + hsudc_req->req.actual;
		unsigned length =
			hsudc_read16(HSUDC_EP_OUTBC_REG16(hsudc_ep->num));

		hsudc_copy_from_fifo(hsudc_ep, length, buf);

		hsudc_req->req.actual += length;

		dev_vdbg(&hsudc_dev->pdev->dev,
				"%s(): ep%d%s req %p/%d:%d len %d max %d\n",
				__func__,
				hsudc_ep->num,
				(hsudc_ep->is_in?"in":"out"),
				&hsudc_req->req, hsudc_req->req.length,
				hsudc_req->req.actual,
				length, hsudc_ep->ep.maxpacket);

		if (length == hsudc_ep->ep.maxpacket
		    && hsudc_req->req.actual < hsudc_req->req.length) {
			/* ARM out ep,
			 * set busy bit to enable acking from the host
			 */
			hsudc_write8(0x00,
					HSUDC_EP_OUTCS_REG8(hsudc_ep->num));

			spin_unlock(&hsudc_ep->s);
			return;
		}
	} else {
		dma_unmap_single(hsudc_dev->gadget.dev.parent,
				 hsudc_req->req.dma, hsudc_req->req.length,
				 hsudc_ep->is_in ? DMA_TO_DEVICE :
						   DMA_FROM_DEVICE);
		hsudc_req->req.dma = DMA_ADDR_INVALID;
		hsudc_ep->use_dma = 0;

		dev_dbg(&hsudc_dev->pdev->dev, "%s(): ep%d%s req %p/%d:%d dma end\n",
					__func__,
					hsudc_ep->num,
					(hsudc_ep->is_in?"in":"out"),
					&hsudc_req->req, hsudc_req->req.length,
					hsudc_req->req.actual);
	}

	/* Explicit ZLP handling :
	 * IN, non zero, multiple of maxpacket, ZLP required
	 */
	if (hsudc_ep->is_in && hsudc_req->req.actual &&
			(!(hsudc_req->req.actual % hsudc_ep->ep.maxpacket))
			&& hsudc_req->req.zero) {
		/* Send explicit ZLP */
		hsudc_req->req.zero = 0;

		dev_vdbg(&hsudc_dev->pdev->dev, "%s(): ep%d%s explicit ZLP\n",
				__func__,
				hsudc_ep->num, (hsudc_ep->is_in?"in":"out"));

		/* ARM out ep, set busy bit to enable sending to the host */
		hsudc_write8(0x00, HSUDC_EP_INCS_REG8(hsudc_ep->num));

		spin_unlock(&hsudc_ep->s);
		return;
	}

	if (hsudc_req->req.status == -EINPROGRESS)
		hsudc_req->req.status = 0;

req_complete:
	/* Remove request from list */
	list_del_init(&hsudc_req->queue);

	dev_dbg(&hsudc_dev->pdev->dev, "%s(): ep%d%s req %p/%d:%d complete status %d\n",
			__func__,
			hsudc_ep->num, (hsudc_ep->is_in?"in":"out"),
			&hsudc_req->req, hsudc_req->req.length,
			hsudc_req->req.actual,
			hsudc_req->req.status);

	hsudc_ep->cur = NULL;

	spin_unlock(&hsudc_ep->s);

	/* Complete request, unlock so the complete can
	 * also queue another request and we handle it immediately
	 * without disabling the irqs
	 */
	hsudc_req->req.complete(&hsudc_ep->ep, &hsudc_req->req);

	spin_lock(&hsudc_ep->s);

	/* If queue is not empty, continue work */
	if (!list_empty(&hsudc_ep->queue))
		queue_work(hsudc_dev->wq_ep, &hsudc_ep->ws);
	else {
		dev_dbg(&hsudc_dev->pdev->dev, "%s(): ep%d%s queue empty\n",
				__func__,
				hsudc_ep->num,
				(hsudc_ep->is_in?"in":"out"));

		/* Disable EP IRQ */
		if (hsudc_ep->is_in) {
			hsudc_write16(1 << hsudc_ep->num, HSUDC_INIRQ_REG16);
			hsudc_write16(hsudc_read16(HSUDC_INIEN_REG16) &
					~(1 << hsudc_ep->num),
					HSUDC_INIEN_REG16);
		} else {
			hsudc_write16(hsudc_read16(HSUDC_OUTIEN_REG16) &
					~(1 << hsudc_ep->num),
					HSUDC_OUTIEN_REG16);
		}
	}

	spin_unlock(&hsudc_ep->s);
}

static int hsudc_ep0_clear_feature(struct cadence_hsudc *hsudc_dev,
				   unsigned type,
				   unsigned w_value, int w_index)
{
	if (type == USB_RECIP_ENDPOINT && w_value == USB_ENDPOINT_HALT) {
		unsigned num = w_index & 0xf;
		unsigned is_in = w_index & USB_DIR_IN;

		if (is_in && hsudc_dev->ep_in[num].is_available) {
			/* Select endpoint */
			hsudc_write8(num | HSUDC_ENDPRST_IO_MSK,
				     HSUDC_ENDPRST_REG8);

			/* Reset endpoint */
			hsudc_write8(num | HSUDC_ENDPRST_IO_MSK |
				     HSUDC_ENDPRST_TOGRST_MSK,
				     HSUDC_ENDPRST_REG8);

			/* UnHalt */
			hsudc_write8(hsudc_read8(HSUDC_EP_INCON_REG8(num)) &
				     ~HSUDC_EP_CON_STALL_MSK,
				     HSUDC_EP_INCON_REG8(num));

			return 0;
		} else if (!is_in && hsudc_dev->ep_out[num].is_available) {
			/* Select endpoint */
			hsudc_write8(num, HSUDC_ENDPRST_REG8);

			/* Reset endpoint */
			hsudc_write8(num | HSUDC_ENDPRST_TOGRST_MSK,
				     HSUDC_ENDPRST_REG8);

			/* UnHalt */
			hsudc_write8(hsudc_read8(HSUDC_EP_OUTCON_REG8(num)) &
				     ~HSUDC_EP_CON_STALL_MSK,
				     HSUDC_EP_OUTCON_REG8(num));

			return 0;
		} else
			return -1;	/* Invalid Endpoint, STALL */
	} else
		return -1;	/* STALL */
}

static int hsudc_ep0_set_feature(struct cadence_hsudc *hsudc_dev, unsigned type,
				 unsigned w_value, int w_index)
{
	if (type == USB_RECIP_ENDPOINT && w_value == USB_ENDPOINT_HALT) {
		unsigned num = w_index & 0xf;
		unsigned is_in = w_index & USB_DIR_IN;

		if (is_in && hsudc_dev->ep_in[num].is_available) {
			/* endpoint in stall */
			hsudc_write8(hsudc_read8(HSUDC_EP_INCON_REG8(num)) |
				     HSUDC_EP_CON_STALL_MSK,
				     HSUDC_EP_INCON_REG8(num));

			return 0;
		} else if (!is_in && hsudc_dev->ep_out[num].is_available) {
			/* endpoint out stall */
			hsudc_write8(hsudc_read8(HSUDC_EP_OUTCON_REG8(num)) |
				     HSUDC_EP_CON_STALL_MSK,
				     HSUDC_EP_OUTCON_REG8(num));

			return 0;
		} else
			return -1;	/* Invalid Endpoint, STALL */
	} else
		return -1;	/* STALL */
}

static int hsudc_ep0_get_status(struct cadence_hsudc *hsudc_dev, unsigned type,
				int w_index)
{
	uint8_t status[2] = { 0, 0 };

	if (type == USB_RECIP_ENDPOINT) {
		unsigned num = w_index & 0xf;
		unsigned is_in = w_index & USB_DIR_IN;

		if (is_in && hsudc_dev->ep_in[num].is_available) {
			if ((hsudc_read8(HSUDC_EP_INCON_REG8(num)) &
			     HSUDC_EP_CON_STALL_MSK) == HSUDC_EP_CON_STALL_MSK)
				status[0] = 1;
		} else if (!is_in && hsudc_dev->ep_out[num].is_available) {
			if ((hsudc_read8(HSUDC_EP_OUTCON_REG8(num)) &
			     HSUDC_EP_CON_STALL_MSK) == HSUDC_EP_CON_STALL_MSK)
				status[0] = 1;
		} else
			return -1;	/* Invalid EP */
	}
	/* Copy into fifo */
	hsudc_write8(status[0], HSUDC_EP0_INBUF_BASE_REG + 0);
	hsudc_write8(status[1], HSUDC_EP0_INBUF_BASE_REG + 1);

	/* Clear and enable ep0 in irq */
	hsudc_write16(1, HSUDC_INIRQ_REG16);
	hsudc_write16(hsudc_read16(HSUDC_INIEN_REG16) | 1, HSUDC_INIEN_REG16);

	/* Load byte size */
	hsudc_write8(2, HSUDC_EP0_INBC_REG8);

	return 0;
}

static void hsudc_ep0_setup(struct work_struct *work)
{
	struct cadence_hsudc *hsudc_dev;
	union setup {
		uint8_t raw[8];
		struct usb_ctrlrequest r;
	} ctrlrequest;
	unsigned i;
	int ret;

#define w_index		le16_to_cpu(ctrlrequest.r.wIndex)
#define w_value		le16_to_cpu(ctrlrequest.r.wValue)
#define w_length	le16_to_cpu(ctrlrequest.r.wLength)

	hsudc_dev = container_of(work, struct cadence_hsudc, ep0_setup);

	for (i = 0; i < 8; ++i)
		ctrlrequest.raw[i] =
		    hsudc_read8(HSUDC_EP0_SETUPDAT_BASE_REG + i);

	dev_vdbg(&hsudc_dev->pdev->dev,
			"SETUP bRequest 0x%x bRequestType 0x%x w_index 0x%x w_value 0x%x w_length %d\n",
			ctrlrequest.r.bRequest, ctrlrequest.r.bRequestType,
			w_index, w_value, w_length);

	if (ctrlrequest.r.bRequestType & USB_DIR_IN)
		hsudc_dev->ep0.is_in = 1;
	else
		hsudc_dev->ep0.is_in = 0;

	switch (ctrlrequest.r.bRequest) {
	case USB_REQ_SET_ADDRESS:
		return;		/* Supported by Hardware */
	case USB_REQ_CLEAR_FEATURE:
		dev_dbg(&hsudc_dev->pdev->dev, "USB_REQ_CLEAR_FEATURE\n");
		if (hsudc_ep0_clear_feature(hsudc_dev,
					ctrlrequest.r.bRequestType & 0xf,
					w_index, w_value) < 0)
			goto hsudc_ep0_setup_stall;
		/* Finish control transaction */
		hsudc_write8(HSUDC_EP0_CS_HSNAK_MSK, HSUDC_EP0_CS_REG8);
		return;
	case USB_REQ_SET_FEATURE:
		dev_dbg(&hsudc_dev->pdev->dev, "USB_REQ_SET_FEATURE\n");
		if (hsudc_ep0_set_feature(hsudc_dev,
					ctrlrequest.r.bRequestType & 0xf,
					w_index, w_value) < 0)
			goto hsudc_ep0_setup_stall;
		/* Finish control transaction */
		hsudc_write8(HSUDC_EP0_CS_HSNAK_MSK, HSUDC_EP0_CS_REG8);
		return;
	case USB_REQ_GET_STATUS:
		dev_dbg(&hsudc_dev->pdev->dev, "USB_REQ_GET_STATUS\n");
		if (hsudc_ep0_get_status(hsudc_dev,
					ctrlrequest.r.bRequestType & 0xf,
					w_index) < 0)
			goto hsudc_ep0_setup_stall;
		return;
	default:
		ret =
		    hsudc_dev->driver->setup(&hsudc_dev->gadget,
					     &ctrlrequest.r);
		dev_vdbg(&hsudc_dev->pdev->dev, "Driver SETUP ret %d\n", ret);
		if (ret < 0) {
			dev_dbg(&hsudc_dev->pdev->dev,
				"req %02x.%02x protocol STALL; ret %d\n",
				ctrlrequest.r.bRequestType,
				ctrlrequest.r.bRequest, ret);
			goto hsudc_ep0_setup_stall;
		}
	}

	if (!w_length) {
		/* Finish control transaction */
		hsudc_write8(HSUDC_EP0_CS_HSNAK_MSK, HSUDC_EP0_CS_REG8);
	}
#undef w_value
#undef w_index
#undef w_length

	return;

hsudc_ep0_setup_stall:
	hsudc_write8(HSUDC_EP0_CS_STALL_MSK, HSUDC_EP0_CS_REG8);
}

static void hsudc_ep_work(struct work_struct *work)
{
	struct cadence_hsudc *hsudc_dev;
	struct cadence_hsudc_ep *hsudc_ep;
	struct cadence_hsudc_request *hsudc_req;

	hsudc_ep = container_of(work, struct cadence_hsudc_ep, ws);
	hsudc_dev = hsudc_ep->hsudc_dev;

	spin_lock(&hsudc_ep->s);

	if (list_empty(&hsudc_ep->queue)) {
		dev_dbg(&hsudc_dev->pdev->dev, "%s(): ep%d%s queue empty\n",
				__func__,
				hsudc_ep->num,
				(hsudc_ep->is_in?"in":"out"));

		/* Disable EP IRQ */
		if (hsudc_ep->is_in) {
			hsudc_write16(hsudc_read16(HSUDC_INIEN_REG16) &
					~(1 << hsudc_ep->num),
					HSUDC_INIEN_REG16);
			hsudc_write16(1 << hsudc_ep->num, HSUDC_INIRQ_REG16);
		} else {
			hsudc_write16(hsudc_read16(HSUDC_OUTIEN_REG16) &
					~(1 << hsudc_ep->num),
					HSUDC_OUTIEN_REG16);
			hsudc_write16(1 << hsudc_ep->num, HSUDC_OUTIRQ_REG16);
		}
		spin_unlock(&hsudc_ep->s);
		return;
	}

	hsudc_req = list_entry(hsudc_ep->queue.next,
			       struct cadence_hsudc_request, queue);

	hsudc_ep->cur = hsudc_req;

	if (!hsudc_req)
		BUG();

	spin_unlock(&hsudc_ep->s);

	dev_vdbg(&hsudc_dev->pdev->dev, "%s(): ep%d%s req %p/%d:%d\n",
			__func__,
			hsudc_ep->num, (hsudc_ep->is_in?"in":"out"),
			&hsudc_req->req,
			hsudc_req->req.length,
			hsudc_req->req.actual);

	if (hsudc_req->req.length > 0 &&
			((unsigned long)hsudc_req->req.buf & 0x3) == 0 &&
			hsudc_dev->hw_config->dma_enabled &&
			hsudc_dma_get_channel(hsudc_dev, hsudc_ep) == 0 &&
			hsudc_dma_init(hsudc_dev, hsudc_ep, hsudc_req) == 0) {

		/* Start DMA Channel */
		hsudc_write8(HSUDC_DMA_WORK_START,
			     HSUDC_DMA_WORK_REG8(hsudc_ep->dma_channel->num));

	} else if (hsudc_ep->is_in) {
		uint8_t *buf =
			hsudc_req->req.buf + hsudc_req->req.actual;
		unsigned length =
			hsudc_req->req.length - hsudc_req->req.actual;
		unsigned i;

		if (length > hsudc_ep->ep.maxpacket)
			length = hsudc_ep->ep.maxpacket;

		/* copy data in ep fifo, with optimized accesses */
		for (i = 0; i < length;) {
			if ((i % 4) == 0 && (length - i) >= 4) {
				hsudc_write32(*(uint32_t *) (&buf[i]),
						HSUDC_FIFODAT_REG32
						(hsudc_ep->num));
				i += 4;
			} else if ((i % 2) == 0 && (length - i) >= 2) {
				hsudc_write16(*(uint16_t *) (&buf[i]),
						HSUDC_FIFODAT_REG32
						(hsudc_ep->num));
				i += 2;
			} else {
				hsudc_write8(*(uint8_t *) (&buf[i]),
						HSUDC_FIFODAT_REG32
						(hsudc_ep->num));
				i += 1;
			}
		}

		hsudc_req->req.actual += length;

		dev_dbg(&hsudc_dev->pdev->dev, "%s(): ep%d%s req %p/%d:%d start\n",
				__func__,
				hsudc_ep->num, (hsudc_ep->is_in?"in":"out"),
				&hsudc_req->req,
				hsudc_req->req.length,
				hsudc_req->req.actual);

		/* Enable IRQ */
		hsudc_write16(1 << hsudc_ep->num, HSUDC_INIRQ_REG16);
		hsudc_write16(hsudc_read16(HSUDC_INIEN_REG16) |
				(1 << hsudc_ep->num), HSUDC_INIEN_REG16);

		/* ARM out ep, set busy bit to enable sending to the host */
		hsudc_write8(0x00, HSUDC_EP_INCS_REG8(hsudc_ep->num));
	} else {
		/* Enable IRQ */
		hsudc_write16(hsudc_read16(HSUDC_OUTIEN_REG16) |
				(1 << hsudc_ep->num), HSUDC_OUTIEN_REG16);

		/* ARM out ep, set busy bit to enable acking from the host */
		hsudc_write8(0x00, HSUDC_EP_OUTCS_REG8(hsudc_ep->num));
	}
}

static int cadence_hsudc_ep_queue(struct usb_ep *ep, struct usb_request *req,
			      gfp_t gfp_flags)
{
	struct cadence_hsudc *hsudc_dev;
	struct cadence_hsudc_ep *hsudc_ep;
	struct cadence_hsudc_request *hsudc_req;
	int running = 0;

	hsudc_req = container_of(req, struct cadence_hsudc_request, req);
	hsudc_ep = container_of(ep, struct cadence_hsudc_ep, ep);

	hsudc_dev = hsudc_ep->hsudc_dev;

	if (!req || !req->complete || !req->buf) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error invalid request %p\n",
			__func__, req);
		return -EINVAL;
	}

	if (!hsudc_dev->driver ||
		hsudc_dev->gadget.speed == USB_SPEED_UNKNOWN) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error invalid device\n",
			__func__);
		return -EINVAL;
	}

	if (!hsudc_ep->desc && hsudc_ep->num) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error invalid ep\n",
			__func__);
		return -EINVAL;
	}

	req->status = -EINPROGRESS;
	req->actual = 0;

	dev_dbg(&hsudc_dev->pdev->dev, "%s(): '%s', req %p, empty %d\n",
		__func__, hsudc_ep->ep.name, req,
		list_empty(&hsudc_ep->queue));

	if (hsudc_ep->is_ep0)
		return hsudc_ep0_queue(hsudc_dev, hsudc_req);

	spin_lock(&hsudc_ep->s);

	if (list_empty(&hsudc_ep->queue))
		running = 0;
	else
		running = 1;

	list_add_tail(&hsudc_req->queue, &hsudc_ep->queue);

	if (!running)
		queue_work(hsudc_dev->wq_ep, &hsudc_ep->ws);

	spin_unlock(&hsudc_ep->s);

	return 0;
}

static int cadence_hsudc_ep_dequeue(struct usb_ep *ep, struct usb_request *req)
{
	struct cadence_hsudc *hsudc_dev;
	struct cadence_hsudc_ep *hsudc_ep;
	struct cadence_hsudc_request *hsudc_req;

	hsudc_req = container_of(req, struct cadence_hsudc_request, req);
	hsudc_ep = container_of(ep, struct cadence_hsudc_ep, ep);

	hsudc_dev = hsudc_ep->hsudc_dev;

	if (!req || !req->complete || !req->buf) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error invalid request %p\n",
			__func__, req);
		return -EINVAL;
	}

	if (!hsudc_dev->driver ||
		hsudc_dev->gadget.speed == USB_SPEED_UNKNOWN) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error invalid device\n",
			__func__);
		return -EINVAL;
	}

	if (!hsudc_ep->desc && hsudc_ep->num) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error invalid ep\n",
				__func__);
		return -EINVAL;
	}

	spin_lock(&hsudc_ep->s);

	if (hsudc_ep->cur == hsudc_req) {
		dev_dbg(&hsudc_dev->pdev->dev, "%s(): ep%d%s req %p unqueue cur req\n",
				__func__,
				hsudc_ep->num,
				(hsudc_ep->is_in?"in":"out"), hsudc_req);
		req->status = -ECONNRESET;
		queue_work(hsudc_dev->wq_ep, &hsudc_ep->comp);
		spin_unlock(&hsudc_ep->s);
		return 0;
	}

	dev_dbg(&hsudc_dev->pdev->dev, "%s(): ep%d%s req %p unqueue\n",
			__func__,
			hsudc_ep->num,
			(hsudc_ep->is_in?"in":"out"), hsudc_req);

	/* Remove request from list */
	list_del_init(&hsudc_req->queue);

	spin_unlock(&hsudc_ep->s);

	req->status = -ECONNRESET;
	req->complete(ep, req);

	return 0;
}

static int cadence_hsudc_ep_set_halt(struct usb_ep *ep, int value)
{
	struct cadence_hsudc *hsudc_dev;
	struct cadence_hsudc_ep *hsudc_ep;

	hsudc_ep = container_of(ep, struct cadence_hsudc_ep, ep);
	hsudc_dev = hsudc_ep->hsudc_dev;

	spin_lock(&hsudc_ep->s);

	if (hsudc_ep->is_in) {
		if (value) {
			dev_vdbg(&hsudc_dev->pdev->dev, "%s(): ep%d%s stall\n",
					__func__,
					hsudc_ep->num,
					(hsudc_ep->is_in?"in":"out"));

			/* endpoint in stall */
			hsudc_write8(hsudc_read8
					(HSUDC_EP_INCON_REG8(hsudc_ep->num)) |
					HSUDC_EP_CON_STALL_MSK,
					HSUDC_EP_INCON_REG8(hsudc_ep->num));
		} else {
			dev_vdbg(&hsudc_dev->pdev->dev, "%s(): ep%d%s unhalt\n",
					__func__,
					hsudc_ep->num,
					(hsudc_ep->is_in?"in":"out"));

			/* Select endpoint */
			hsudc_write8(hsudc_ep->num | HSUDC_ENDPRST_IO_MSK,
				     HSUDC_ENDPRST_REG8);

			/* Reset endpoint */
			hsudc_write8(hsudc_ep->num | HSUDC_ENDPRST_IO_MSK |
				     HSUDC_ENDPRST_TOGRST_MSK,
				     HSUDC_ENDPRST_REG8);

			/* UnHalt */
			hsudc_write8(
				hsudc_read8(HSUDC_EP_INCON_REG8(hsudc_ep->num))
				& ~HSUDC_EP_CON_STALL_MSK,
				HSUDC_EP_INCON_REG8(hsudc_ep->num));
		}
	} else {
		if (value) {
			dev_vdbg(&hsudc_dev->pdev->dev, "%s(): ep%d%s stall\n",
					__func__,
					hsudc_ep->num,
					(hsudc_ep->is_in?"in":"out"));

			/* endpoint out stall */
			hsudc_write8(
				hsudc_read8(HSUDC_EP_OUTCON_REG8(hsudc_ep->num))
				| HSUDC_EP_CON_STALL_MSK,
				HSUDC_EP_OUTCON_REG8(hsudc_ep->num));
		} else {
			dev_vdbg(&hsudc_dev->pdev->dev, "%s(): ep%d%s unhalt\n",
					__func__,
					hsudc_ep->num,
					(hsudc_ep->is_in?"in":"out"));

			/* Select endpoint */
			hsudc_write8(hsudc_ep->num, HSUDC_ENDPRST_REG8);

			/* Reset endpoint */
			hsudc_write8(hsudc_ep->num | HSUDC_ENDPRST_TOGRST_MSK,
				     HSUDC_ENDPRST_REG8);

			/* UnHalt */
			hsudc_write8(
				hsudc_read8(HSUDC_EP_OUTCON_REG8(hsudc_ep->num))
				& ~HSUDC_EP_CON_STALL_MSK,
				HSUDC_EP_OUTCON_REG8(hsudc_ep->num));
		}
	}

	spin_unlock(&hsudc_ep->s);

	return 0;
}

static const struct usb_ep_ops cadence_hsudc_ep_ops = {
	.enable = cadence_hsudc_ep_enable,
	.disable = cadence_hsudc_ep_disable,
	.alloc_request = cadence_hsudc_ep_alloc_request,
	.free_request = cadence_hsudc_ep_free_request,
	.queue = cadence_hsudc_ep_queue,
	.dequeue = cadence_hsudc_ep_dequeue,
	.set_halt = cadence_hsudc_ep_set_halt,
};

int cadence_hsudc_udc_start(struct usb_gadget *gadget,
			   struct usb_gadget_driver *driver)
{
	struct cadence_hsudc *hsudc_dev = container_of(gadget,
			struct cadence_hsudc, gadget);

	dev_dbg(&hsudc_dev->pdev->dev, "%s():\n", __func__);

	if (!driver
	    || !driver->setup) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error invalid arguments\n",
			__func__);
		return -EINVAL;
	}

	if (hsudc_dev->driver) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error already in use\n",
			__func__);
		return -EINVAL;
	}

	driver->driver.bus = NULL;
	hsudc_dev->driver = driver;
	hsudc_dev->gadget.dev.of_node = hsudc_dev->pdev->dev.of_node;
	hsudc_dev->gadget.speed = USB_SPEED_UNKNOWN;

	/* Setup USB Speed */
	hsudc_write8(HSUDC_SPEEDCTRL_HS_MSK,
			HSUDC_SPEEDCTRL_REG8);

	/* Configure EP0 maxpacket (EVCI writes 8 here) */
	hsudc_write8(0xFF, HSUDC_EP0_OUTBC_REG8);
	hsudc_write8(hsudc_dev->ep0.maxpacket, HSUDC_EP0_MAXPCK_REG8);
	hsudc_write16(hsudc_read16(HSUDC_OUTIEN_REG16) | 1,
			HSUDC_OUTIEN_REG16);

	/* Connect */
	hsudc_write8(hsudc_read8(HSUDC_USBCS_REG8) & ~HSUDC_USBCS_DISCON_MSK,
		     HSUDC_USBCS_REG8);

	/* Enable : */
	/* - High Speed mode interrupt */
	/* - Start reset interrupt */
	/* - SETUP data interrupt */
	/* - Suspend interrupt */
	hsudc_write8(HSUDC_USBIEN_SUTOKIE_MSK |
			HSUDC_USBIEN_URESIE_MSK |
			HSUDC_USBIEN_HSPIE_MSK,
			HSUDC_USBIEN_REG8);

	dev_dbg(&hsudc_dev->pdev->dev, "%s(): bound to %s\n", __func__,
		driver->driver.name);

	return 0;
}

int cadence_hsudc_udc_stop(struct usb_gadget *gadget)
{
	struct cadence_hsudc *hsudc_dev = container_of(gadget,
				struct cadence_hsudc, gadget);
	unsigned i;

	if (!hsudc_dev->driver) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error invalid arguments\n",
			__func__);
		return -EINVAL;
	}

	disable_irq(hsudc_dev->irq);

	for (i = 1; i < HSUDC_EP_COUNT; i++) {
		if (hsudc_dev->ep_in[i].is_available) {
			cancel_work_sync(&hsudc_dev->ep_in[i].ws);
			cancel_work_sync(&hsudc_dev->ep_in[i].comp);
		}
		if (hsudc_dev->ep_out[i].is_available) {
			cancel_work_sync(&hsudc_dev->ep_out[i].ws);
			cancel_work_sync(&hsudc_dev->ep_out[i].comp);
		}
	}
	cancel_work_sync(&hsudc_dev->ep0.ws);
	cancel_work_sync(&hsudc_dev->ep0.comp);
	cancel_work_sync(&hsudc_dev->ep0_setup);
	flush_workqueue(hsudc_dev->wq_ep);

	/* Disconnect */
	hsudc_write8(hsudc_read8(HSUDC_USBCS_REG8) | HSUDC_USBCS_DISCON_MSK,
		     HSUDC_USBCS_REG8);

	hsudc_dev->driver = NULL;
	hsudc_dev->gadget.speed = USB_SPEED_UNKNOWN;

	dev_dbg(&hsudc_dev->pdev->dev, "%s(): unbound\n", __func__);

	return 0;
}

static const struct usb_gadget_ops cadence_hsudc_gadget_ops = {
	.udc_start		= cadence_hsudc_udc_start,
	.udc_stop		= cadence_hsudc_udc_stop,
};

/* Match table for of_platform binding */
static const struct of_device_id cadence_hsudc_of_match[] = {
	{ .compatible = "cdns,usbhs-udc" },
	{ /* Sentinel */ }
};

MODULE_DEVICE_TABLE(of, cadence_hsudc_of_match);

static int cadence_hsudc_of_probe(struct cadence_hsudc *hsudc_dev)
{
	int ret, i;
	int ep_in_count = 0;
	int ep_out_count = 0;
	struct device_node *np;
	struct hsudc_hw_config *hw_config;
	u32 val;

	np = hsudc_dev->pdev->dev.of_node;
	if (!np)
		return -EINVAL;

	hw_config = devm_kzalloc(&hsudc_dev->pdev->dev,
				 sizeof(struct hsudc_hw_config),
				 GFP_KERNEL);
	if (!hw_config)
		return -ENOMEM;

	ep_in_count = of_property_count_u32_elems(np, "cdns,ep-in");
	if (ep_in_count < 1) {
		dev_err(&hsudc_dev->pdev->dev, "cdns,ep-in should have 1+ ep\n");
		return -EINVAL;
	}
	if (ep_in_count >= HSUDC_EP_COUNT)
		ep_in_count = HSUDC_EP_COUNT;
	dev_info(&hsudc_dev->pdev->dev, "max %d in EPs\n", ep_in_count);

	ep_out_count = of_property_count_u32_elems(np, "cdns,ep-out");
	if (ep_out_count < 1) {
		dev_err(&hsudc_dev->pdev->dev, "cdns,ep-out should have 1+ ep\n");
		return -EINVAL;
	}
	if (ep_out_count >= HSUDC_EP_COUNT)
		ep_out_count = HSUDC_EP_COUNT;
	dev_info(&hsudc_dev->pdev->dev, "max %d out EPs\n", ep_out_count);

	ret = of_property_count_u32_elems(np, "cdns,ep-in-size");
	if (ret < ep_in_count) {
		dev_err(&hsudc_dev->pdev->dev, "cdns,ep-in-size size differs (%d < %d)\n",
				ret, ep_in_count);
		return -EINVAL;
	}

	ret = of_property_count_u32_elems(np, "cdns,ep-in-buffers");
	if (ret < ep_in_count) {
		dev_err(&hsudc_dev->pdev->dev, "cdns,ep-in-buffers size differs (%d < %d)\n",
				ret, ep_in_count);
		return -EINVAL;
	}

	ret = of_property_count_u32_elems(np, "cdns,ep-in-buffstart");
	if (ret < ep_in_count) {
		dev_err(&hsudc_dev->pdev->dev, "cdns,ep-in-buffstart size differs (%d < %d)\n",
				ret, ep_in_count);
		return -EINVAL;
	}

	ret = of_property_count_u32_elems(np, "cdns,ep-out-size");
	if (ret < ep_out_count) {
		dev_err(&hsudc_dev->pdev->dev, "cdns,ep-out-size size differs (%d < %d)\n",
				ret, ep_out_count);
		return -EINVAL;
	}

	ret = of_property_count_u32_elems(np, "cdns,ep-out-buffers");
	if (ret < ep_out_count) {
		dev_err(&hsudc_dev->pdev->dev, "cdns,ep-out-buffers size differs (%d < %d)\n",
				ret, ep_out_count);
		return -EINVAL;
	}

	ret = of_property_count_u32_elems(np, "cdns,ep-out-buffstart");
	if (ret < ep_out_count) {
		dev_err(&hsudc_dev->pdev->dev, "cdns,ep-out-buffstart size differs (%d < %d)\n",
				ret, ep_out_count);
		return -EINVAL;
	}

	for (i = 0 ; i < ep_in_count ; ++i) {
		of_property_read_u32_index(np, "cdns,ep-in", i, &val);
		hw_config->ep_in_exist[i] = !!val;
		if (!hw_config->ep_in_exist[i])
			continue;
		of_property_read_u32_index(np, "cdns,ep-in-size", i, &val);
		hw_config->ep_in_size[i] = val;
		of_property_read_u32_index(np, "cdns,ep-in-buffers", i, &val);
		hw_config->ep_in_buffering[i] = val;
		of_property_read_u32_index(np, "cdns,ep-in-buffstart", i, &val);
		hw_config->ep_in_startbuff[i] = val;
	}

	for (i = 0 ; i < ep_out_count ; ++i) {
		of_property_read_u32_index(np, "cdns,ep-out", i, &val);
		hw_config->ep_out_exist[i] = !!val;
		if (!hw_config->ep_out_exist[i])
			continue;
		of_property_read_u32_index(np, "cdns,ep-out-size", i, &val);
		hw_config->ep_out_size[i] = val;
		of_property_read_u32_index(np, "cdns,ep-out-buffers", i, &val);
		hw_config->ep_out_buffering[i] = val;
		of_property_read_u32_index(np,
					   "cdns,ep-out-buffstart", i, &val);
		hw_config->ep_out_startbuff[i] = val;
	}

	if (of_property_read_bool(np, "cdns,dma-enable")) {
		ret = of_property_read_u32(np, "cdns,dma-channels", &val);
		if (ret < 0 || val < 1)
			dev_warn(&hsudc_dev->pdev->dev,
				 "cdns,dma-enable exists without valid cdns,dma-channels, disabling DMA\n");
		else {
			hw_config->dma_enabled = 1;
			hw_config->dma_channels = val;
		}
	}

	hsudc_dev->hw_config = hw_config;

	return 0;
}

static int cadence_hsudc_probe(struct platform_device *pdev)
{
	struct cadence_hsudc *hsudc_dev;
	struct resource *res;
	struct reset_control *reset;
	struct clk *pclk;
	int i;
	int ret = -EIO;

	reset = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(reset) && PTR_ERR(reset) == -EPROBE_DEFER)
		return PTR_ERR(reset);

	dev_info(&pdev->dev, "Cadence USB2.0 Device Controller");

	hsudc_dev = devm_kzalloc(&pdev->dev, sizeof(struct cadence_hsudc),
				 GFP_KERNEL);
	if (hsudc_dev == NULL)
		return -ENOMEM;

	hsudc_dev->pdev = pdev;

	ret = cadence_hsudc_of_probe(hsudc_dev);
	if (ret)
		return ret;

	/* TODO : Add non-dt pdata initialization */
	if (!hsudc_dev->hw_config) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error hw_config missing\n",
				__func__);
		return -EINVAL;
	}

	pclk = devm_clk_get(&pdev->dev, NULL);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hsudc_dev->io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hsudc_dev->io_base)) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error ioremap() failed\n",
				__func__);
		return PTR_ERR(hsudc_dev->io_base);
	}

	hsudc_dev->irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, hsudc_dev->irq,
			cadence_hsudc_irq, 0, "hsudc_dev_irq", hsudc_dev);
	if (ret) {
		dev_err(&hsudc_dev->pdev->dev, "%s(): error request_irq() failed\n",
				__func__);
		return ret;
	}

	hsudc_dev->wq_ep = create_workqueue("hsudc_wq_ep");
	if (!hsudc_dev->wq_ep) {
		dev_err(&hsudc_dev->pdev->dev,
			"%s(): error create_workqueue() failed\n", __func__);
		return -EBUSY;
	}

	/* init software state */
	hsudc_dev->gadget.max_speed = USB_SPEED_HIGH;
	hsudc_dev->gadget.ops = &cadence_hsudc_gadget_ops;
	hsudc_dev->gadget.name = dev_name(&pdev->dev);
	hsudc_dev->gadget.ep0 = &hsudc_dev->ep0.ep;

	/* ep0 init handling */
	spin_lock_init(&hsudc_dev->ep0.s);
	INIT_LIST_HEAD(&hsudc_dev->ep0.queue);
	hsudc_dev->ep0.maxpacket = hsudc_dev->hw_config->ep_in_size[0];
	usb_ep_set_maxpacket_limit(&hsudc_dev->ep0.ep,
				   hsudc_dev->ep0.maxpacket);
	hsudc_dev->ep0.ep.ops = &cadence_hsudc_ep_ops;
	hsudc_dev->ep0.ep.name = "ep0-inout";
	hsudc_dev->ep0.is_available = 1;
	hsudc_dev->ep0.is_ep0 = 1;
	hsudc_dev->ep0.num = 0;
	hsudc_dev->ep0.hsudc_dev = hsudc_dev;
	INIT_WORK(&hsudc_dev->ep0.ws, hsudc_ep0_work);
	INIT_WORK(&hsudc_dev->ep0.comp, hsudc_ep0_completion);
	INIT_WORK(&hsudc_dev->ep0_setup, hsudc_ep0_setup);

	/* other ep init handling */
	INIT_LIST_HEAD(&hsudc_dev->gadget.ep_list);
	INIT_LIST_HEAD(&hsudc_dev->gadget.ep0->ep_list);
	/* IN Endpoints */
	for (i = 1; i < HSUDC_EP_COUNT; i++) {
		struct cadence_hsudc_ep *ep = &hsudc_dev->ep_in[i];

		if (!hsudc_dev->hw_config->ep_in_exist[i])
			continue;

		hsudc_dev->ep_in[i].num = i;
		hsudc_dev->ep_in[i].hsudc_dev = hsudc_dev;
		hsudc_dev->ep_in[i].is_available = 1;
		hsudc_dev->ep_in[i].is_in = 1;
		hsudc_dev->ep_in[i].maxpacket =
		    hsudc_dev->hw_config->ep_in_size[i];
		hsudc_dev->ep_in[i].ep.name =
			kasprintf(GFP_KERNEL, "ep%din-bulk", i);
		usb_ep_set_maxpacket_limit(&hsudc_dev->ep_in[i].ep,
				hsudc_dev->ep_in[i].maxpacket);
		hsudc_dev->ep_in[i].ep.ops = &cadence_hsudc_ep_ops;
		INIT_LIST_HEAD(&ep->queue);
		list_add_tail(&ep->ep.ep_list, &hsudc_dev->gadget.ep_list);
		INIT_WORK(&hsudc_dev->ep_in[i].ws, hsudc_ep_work);
		INIT_WORK(&hsudc_dev->ep_in[i].comp, hsudc_ep_completion);
	}
	/* OUT Endpoints */
	for (i = 1; i < HSUDC_EP_COUNT; i++) {
		struct cadence_hsudc_ep *ep = &hsudc_dev->ep_out[i];

		if (!hsudc_dev->hw_config->ep_out_exist[i])
			continue;

		hsudc_dev->ep_out[i].num = i;
		hsudc_dev->ep_out[i].hsudc_dev = hsudc_dev;
		hsudc_dev->ep_out[i].is_available = 1;
		hsudc_dev->ep_out[i].maxpacket =
		    hsudc_dev->hw_config->ep_out_size[i];
		hsudc_dev->ep_out[i].ep.name =
			kasprintf(GFP_KERNEL, "ep%dout-bulk", i);
		usb_ep_set_maxpacket_limit(&hsudc_dev->ep_out[i].ep,
				hsudc_dev->ep_out[i].maxpacket);
		hsudc_dev->ep_out[i].ep.ops = &cadence_hsudc_ep_ops;
		INIT_LIST_HEAD(&ep->queue);
		list_add_tail(&ep->ep.ep_list, &hsudc_dev->gadget.ep_list);
		INIT_WORK(&hsudc_dev->ep_out[i].ws, hsudc_ep_work);
		INIT_WORK(&hsudc_dev->ep_out[i].comp, hsudc_ep_completion);
	}

	/* DMA Channels */
	if (hsudc_dev->hw_config->dma_enabled) {
		for (i = 0; i < hsudc_dev->hw_config->dma_channels; ++i) {
			hsudc_dev->dma_channels[i].num = i;
			hsudc_dev->dma_channels[i].is_available = 1;
			hsudc_dev->dma_channels[i].in_use = 0;
			hsudc_dev->dma_channels[i].cur_ep = NULL;
		}
		sema_init(&hsudc_dev->dma_sem,
			  hsudc_dev->hw_config->dma_channels);
		spin_lock_init(&hsudc_dev->dma_s);
	}

	/* Try to enable pclk */
	if (!IS_ERR(pclk))
		clk_prepare_enable(pclk);

	if (!IS_ERR(reset))
		reset_control_deassert(reset);

	/* init hardware */
	/* Configure each endpoints */
	for (i = 1; i < HSUDC_EP_COUNT; i++) {
		/* Clear irqs */
		hsudc_write16(1 << i, HSUDC_ERRIRQ_OUT_REG16);
		hsudc_write16(1 << i, HSUDC_ERRIRQ_IN_REG16);
		hsudc_write16(1 << i, HSUDC_OUTIRQ_REG16);
		hsudc_write16(1 << i, HSUDC_INIRQ_REG16);

		/* OUT endpoint */
		if (hsudc_dev->hw_config->ep_out_exist[i]) {
			/* Configure buffer */
			hsudc_write16(hsudc_dev->hw_config->ep_out_startbuff[i],
				      HSUDC_EP_OUT_STARTADDR_REG16(i));
			/* Configure endpoint with maximum buffering,
			 * bulk, non stall and disabled
			 */
			hsudc_write8(hsudc_dev->hw_config->ep_out_buffering[i] &
					HSUDC_EP_CON_BUF_MSK,
				     HSUDC_EP_OUTCON_REG8(i));
		} else
			hsudc_write8(0x00, HSUDC_EP_OUTCON_REG8(i));

		/* IN endpoint */
		if (hsudc_dev->hw_config->ep_in_exist[i]) {
			/* Configure buffer */
			hsudc_write16(hsudc_dev->hw_config->ep_in_startbuff[i],
				      HSUDC_EP_IN_STARTADDR_REG16(i));
			/* Configure endpoint with maximum buffering,
			 * bulk, non stall and disabled
			 */
			hsudc_write8(hsudc_dev->hw_config->ep_in_buffering[i] &
					HSUDC_EP_CON_BUF_MSK,
				     HSUDC_EP_INCON_REG8(i));
		} else
			hsudc_write8(0x00, HSUDC_EP_INCON_REG8(i));
	}

	/* Set FIFO access by the CPU */
	hsudc_write8(HSUDC_FIFOCTRL_FIFOACC_MSK, HSUDC_FIFOCTRL_REG8);
	hsudc_write8(HSUDC_FIFOCTRL_IO_MSK | HSUDC_FIFOCTRL_FIFOACC_MSK,
		     HSUDC_FIFOCTRL_REG8);

	/* Clear USB start reset interrupt */
	hsudc_write8(HSUDC_USBIRQ_URES_MSK, HSUDC_USBIRQ_REG8);

	/* DMA Channels Init */
	if (hsudc_dev->hw_config->dma_enabled) {
		hsudc_write32(0xFFFFFFFF, HSUDC_DMA_IRQ_REG32);
		hsudc_write32(0, HSUDC_DMA_IEN_REG32);
		hsudc_write32(0xFFFFFFFF, HSUDC_DMA_SHORTIRQ_REG32);
		hsudc_write32(0, HSUDC_DMA_SHORTIEN_REG32);
		hsudc_write32(0xFFFFFFFF, HSUDC_DMA_ERRORIRQ_REG32);
		hsudc_write32(0, HSUDC_DMA_ERRORIEN_REG32);
		for (i = 0; i < hsudc_dev->hw_config->dma_channels; ++i) {
			hsudc_write8(HSUDC_DMA_WORK_RESET,
				     HSUDC_DMA_WORK_REG8(i));
		}

		/* Set FIFO access by the DMA, CPU can still access FIFO */
		hsudc_write8(HSUDC_FIFOCTRL_FIFOAUTO_MSK, HSUDC_FIFOCTRL_REG8);
		hsudc_write8(HSUDC_FIFOCTRL_FIFOAUTO_MSK |
			     HSUDC_FIFOCTRL_IO_MSK, HSUDC_FIFOCTRL_REG8);
	}

	ret = usb_add_gadget_udc(&pdev->dev, &hsudc_dev->gadget);
	if (ret < 0) {
		dev_err(&hsudc_dev->pdev->dev,
				"%s(): error device_register() failed\n",
				__func__);

		destroy_workqueue(hsudc_dev->wq_ep);

		return ret;
	}

	platform_set_drvdata(pdev, hsudc_dev);

	dev_info(&hsudc_dev->pdev->dev, "%s %dx%dbytes FIFO\n",
			hsudc_dev->ep0.ep.name,
			hsudc_dev->hw_config->ep_in_buffering[0],
			hsudc_dev->hw_config->ep_in_size[0]);
	dev_info(&hsudc_dev->pdev->dev, "1 IN/OUT Control EP\n");

	ret = 0;
	for (i = 1 ; i < HSUDC_EP_COUNT ; ++i)
		if (hsudc_dev->hw_config->ep_in_exist[i]) {
			dev_info(&hsudc_dev->pdev->dev, "%s %dx%dbytes FIFO\n",
				hsudc_dev->ep_in[i].ep.name,
				hsudc_dev->hw_config->ep_in_buffering[i],
				hsudc_dev->hw_config->ep_in_size[i]);
			ret++;
		}
	dev_info(&hsudc_dev->pdev->dev, "%d IN EPs\n", ret);

	ret = 0;
	for (i = 1 ; i < HSUDC_EP_COUNT ; ++i)
		if (hsudc_dev->hw_config->ep_out_exist[i]) {
			dev_info(&hsudc_dev->pdev->dev, "%s %dx%dbytes FIFO\n",
				hsudc_dev->ep_out[i].ep.name,
				hsudc_dev->hw_config->ep_out_buffering[i],
				hsudc_dev->hw_config->ep_out_size[i]);
			ret++;
		}
	dev_info(&hsudc_dev->pdev->dev, "%d OUT EPs\n", ret);
	if (hsudc_dev->hw_config->dma_enabled)
		dev_info(&hsudc_dev->pdev->dev, "DMA Enabled with %d channels\n",
				hsudc_dev->hw_config->dma_channels);
	else
		dev_info(&hsudc_dev->pdev->dev, "DMA Support is Disabled\n");

	dev_info(&hsudc_dev->pdev->dev, "ready\n");

	return 0;
}

static int cadence_hsudc_remove(struct platform_device *pdev)
{
	struct cadence_hsudc *hsudc_dev = platform_get_drvdata(pdev);
	unsigned i;

	if (hsudc_dev->driver) {
		hsudc_dev->driver->disconnect(&hsudc_dev->gadget);
		hsudc_dev->driver->unbind(&hsudc_dev->gadget);
	}

	hsudc_write8(hsudc_read8(HSUDC_USBCS_REG8) | HSUDC_USBCS_DISCON_MSK,
		     HSUDC_USBCS_REG8);

	for (i = 1; i < HSUDC_EP_COUNT; i++) {
		if (hsudc_dev->ep_in[i].is_available) {
			cancel_work_sync(&hsudc_dev->ep_in[i].ws);
			cancel_work_sync(&hsudc_dev->ep_in[i].comp);
			kfree(hsudc_dev->ep_in[i].ep.name);
		}
		if (hsudc_dev->ep_out[i].is_available) {
			cancel_work_sync(&hsudc_dev->ep_out[i].ws);
			cancel_work_sync(&hsudc_dev->ep_out[i].comp);
			kfree(hsudc_dev->ep_out[i].ep.name);
		}
	}

	flush_workqueue(hsudc_dev->wq_ep);

	device_unregister(&hsudc_dev->gadget.dev);

	destroy_workqueue(hsudc_dev->wq_ep);

	return 0;
}

static struct platform_driver cadence_hsudc_driver = {
	.probe = cadence_hsudc_probe,
	.remove = cadence_hsudc_remove,
	.driver = {
		   .name = "cadence_hsudc",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(cadence_hsudc_of_match),
		   },
};

module_platform_driver(cadence_hsudc_driver);

MODULE_DESCRIPTION("Cadence USB2.0 Device Controller driver");
MODULE_AUTHOR("Neil Armstrong <narmstrong@neotion.com>");
MODULE_LICENSE("GPL");
