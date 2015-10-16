/*
 *  linux/drivers/usb/gadget/udc/cadence_hsudc_regs.h
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

#ifndef HSUDC_UDC_REGS
#define HSUDC_UDC_REGS

/* General Defines */
/* 0 is control endpoint, 1 to 15 are configurable endpoints */
#define HSUDC_EP_COUNT  16
#define HSUDC_FIFODAT_COUNT 15
#define HSUDC_EP0_INBUF_LEN 64
#define HSUDC_EP0_OUTBUF_LEN 64
#define HSUDC_EP0_SETUPDAT_LEN 8
#define HSUDC_DMA_CHANNELS  32

/* Base Registers Addresses */
#define HSUDC_EP_CTRL_BASE_REG(n)    (0x0000 + 0x8*(n))
#define HSUDC_FIFODAT_BASE_REG       (0x0080)
#define HSUDC_EP0_INBUF_BASE_REG     (0x0100)
#define HSUDC_EP0_OUTBUF_BASE_REG    (0x0140)
#define HSUDC_EP0_SETUPDAT_BASE_REG  (0x0180)
#define HSUDC_IRQ_REQ_BASE_REG       (0x0188)
#define HSUDC_IRQ_EN_BASE_REG        (0x0194)
#define HSUDC_IRQ_VEC_BASE_REG       (0x01A0)
#define HSUDC_CTRL_STAT_BASE_REG     (0x01A2)
#define HSUDC_IRQ_ERR_REG_BASE_REG   (0x01B0)
#define HSUDC_IRQ_ERR_EN_BASE_REG    (0x01B8)
#define HSUDC_OUT_MAXPACKET_BASE_REG (0x01E0)
#define HSUDC_OUT_STARTADDR_BASE_REG (0x0300)
#define HSUDC_IN_STARTADDR_BASE_REG  (0x0340)
#define HSUDC_IN_MAXPACKET_BASE_REG  (0x03E0)

/* DMA Base Registers Addresses */
#define HSUDC_DMA_IRQ_BASE_REG          (0x0400)
#define HSUDC_DMA_CHANNEL_BASE_REG(n)   (0x0420 + ((n)*0x10))

/* Endpoints Registers */
#define HSUDC_EP0_OUTBC_REG8        (HSUDC_EP_CTRL_BASE_REG(0) + 0x0)
#define HSUDC_EP0_INBC_REG8         (HSUDC_EP_CTRL_BASE_REG(0) + 0x1)
#define HSUDC_EP0_CS_REG8           (HSUDC_EP_CTRL_BASE_REG(0) + 0x2)
#define HSUDC_EP0_LPMCTRL_REG16     (HSUDC_EP_CTRL_BASE_REG(0) + 0x4)

/* chgsetup bit indicates change of the contents of the setup data buffer */
#define HSUDC_EP0_CS_CHGSETUP_MSK	0x80
#define HSUDC_EP0_CS_DSTALL_MSK	0x10
#define HSUDC_EP0_CS_OUTBSY_MSK	0x08    /**< read only bit */
#define HSUDC_EP0_CS_INBSY_MSK	0x04    /**< read only bit */
#define HSUDC_EP0_CS_HSNAK_MSK	0x02	/* read/write bit, device mode */
#define HSUDC_EP0_CS_STALL_MSK	0x01	/* endpoint 0 stall bit, device mode */

/* Host Initiated Resume Duration mask */
#define HSUDC_EP0_LPMCTRL_HIRD_MSK	0x00F0
/* Host Initiated Resume Duration offset */
#define HSUDC_EP0_LPMCTRL_HIRD_OFFSET	0x0004
/* LPM bRemoteWakeup register */
#define HSUDC_EP0_LPMCTRL_BREMOTEWAKEUP_MSK	0x0100
/* It reflects value of the lpmnyet bit located in the usbcs(1) register. */
#define HSUDC_EP0_LPMCTRL_LPMNYET_MSK	0x8000

/* Use special HSUDC_EP0_XXXX for EP0 */
#define HSUDC_EP_OUTBC_REG16(n)	(HSUDC_EP_CTRL_BASE_REG(n) + 0x0)
#define HSUDC_EP_OUTCON_REG8(n)	(HSUDC_EP_CTRL_BASE_REG(n) + 0x2)
#define HSUDC_EP_OUTCS_REG8(n)	(HSUDC_EP_CTRL_BASE_REG(n) + 0x3)
#define HSUDC_EP_INBC_REG16(n)	(HSUDC_EP_CTRL_BASE_REG(n) + 0x4)
#define HSUDC_EP_INCON_REG8(n)	(HSUDC_EP_CTRL_BASE_REG(n) + 0x6)
#define HSUDC_EP_INCS_REG8(n)	(HSUDC_EP_CTRL_BASE_REG(n) + 0x7)

#define HSUDC_EP_CON_BUF_SINGLE		0x00
#define HSUDC_EP_CON_BUF_DOUBLE		0x01
#define HSUDC_EP_CON_BUF_TRIPLE		0x02
#define HSUDC_EP_CON_BUF_QUAD		0x03
#define HSUDC_EP_CON_BUF_MSK		0x03
#define HSUDC_EP_CON_TYPE_ISOCHRONOUS	0x04	/* "01" isochronous endpoint */
#define HSUDC_EP_CON_TYPE_BULK		0x08	/* "10" bulk endpoint */
#define HSUDC_EP_CON_TYPE_INTERRUPT	0x0C	/* "11" interrupt endpoint */
#define HSUDC_EP_CON_TYPE_MSK		0x0C
#define HSUDC_EP_CON_STALL_MSK		0x40	/* OUT x endpoint stall bit */
#define HSUDC_EP_CON_VAL_MSK		0x80	/* OUT x endpoint valid bit */

/* read only bit, Data sequence error for ISO endpoints */
#define HSUDC_EP_CS_ERR_MSK	0x01
#define HSUDC_EP_CS_BUSY_MSK	0x02	/* OUT x endpoint busy bit */
/* Number of received data packets that are stored in the OUT x buffer memory */
#define HSUDC_EP_CS_NPAK0_MSK	0x12
#define HSUDC_EP_CS_NPAK0_OFS	0x03
#define HSUDC_EP_CS_AUTOOUT_MSK 0x10	/* Auto-OUT bit, device mode */

/* FIFODAT Endpoints Registers */
/* FIFODAT0 is not available */
#define HSUDC_FIFODAT_REG32(n)	(HSUDC_FIFODAT_BASE_REG + (0x4*(n)))

/* Interrupts Request Registers */
#define HSUDC_INIRQ_REG16	(HSUDC_IRQ_REQ_BASE_REG + 0x0)
#define HSUDC_OUTIRQ_REG16	(HSUDC_IRQ_REQ_BASE_REG + 0x2)
#define HSUDC_USBIRQ_REG8	(HSUDC_IRQ_REQ_BASE_REG + 0x4)
#define HSUDC_OUT_PNGIRQ_REG16	(HSUDC_IRQ_REQ_BASE_REG + 0x6)
#define HSUDC_IN_FULLIRQ_REG16	(HSUDC_IRQ_REQ_BASE_REG + 0x8)
#define HSUDC_OUT_EMPTIRQ_REG16	(HSUDC_IRQ_REQ_BASE_REG + 0xA)

/* SETUP data valid interrupt request, write 1 to clear */
#define HSUDC_USBIRQ_SUDAV_MSK	0x01
/* Start-of-frame interrupt request, write 1 to clear */
#define HSUDC_USBIRQ_SOF_MSK	0x02
/* SETUP token interrupt request, write 1 to clear */
#define HSUDC_USBIRQ_SUTOK_MSK	0x04
/* USB suspend interrupt request, write 1 to clear */
#define HSUDC_USBIRQ_SUSP_MSK	0x08
/* USB reset interrupt request, write 1 to clear */
#define HSUDC_USBIRQ_URES_MSK	0x10
/* USB high-speed mode interrupt request, write 1 to clear */
#define HSUDC_USBIRQ_HSPPED_MSK	0x20
/* Link Power Management interrupt request, write 1 to clear */
#define HSUDC_USBIRQ_LPMIR_MSK	0x80

/* Interrupts Enable Registers */
#define HSUDC_INIEN_REG16	(HSUDC_IRQ_EN_BASE_REG + 0x0)
#define HSUDC_OUTIEN_REG16	(HSUDC_IRQ_EN_BASE_REG + 0x2)
#define HSUDC_USBIEN_REG8	(HSUDC_IRQ_EN_BASE_REG + 0x4)
#define HSUDC_OUT_PNGIEN_REG16	(HSUDC_IRQ_EN_BASE_REG + 0x6)
#define HSUDC_IN_FULLIEN_REG16	(HSUDC_IRQ_EN_BASE_REG + 0x8)
#define HSUDC_OUT_EMPTIEN_REG16	(HSUDC_IRQ_EN_BASE_REG + 0xA)

/* SETUP data valid interrupt enable, set this bit to enable interrupt */
#define HSUDC_USBIEN_SUDAVIE_MSK    0x01
/* Start-of-frame interrupt enable, set this bit to enable interrupt */
#define HSUDC_USBIEN_SOFIE_MSK      0x02
/* SETUP token interrupt enable, set this bit to enable interrupt */
#define HSUDC_USBIEN_SUTOKIE_MSK    0x04
/* USB suspend interrupt enable, set this bit to enable interrupt */
#define HSUDC_USBIEN_SUSPIE_MSK     0x08
/* USB reset interrupt enable, set this bit to enable interrupt */
#define HSUDC_USBIEN_URESIE_MSK     0x10
/* USB high speed mode interrupt enable, set this bit to enable interrupt */
#define HSUDC_USBIEN_HSPIE_MSK      0x20
/* Link Power Management interrupt request, set this bit to enable interrupt */
#define HSUDC_USBIEN_LPMIE_MSK      0x80

/* Interrupt Vector Registers */
#define HSUDC_IVECT_REG8            (HSUDC_IRQ_VEC_BASE_REG + 0x0)
#define HSUDC_FIFOIVECT_REG8        (HSUDC_IRQ_VEC_BASE_REG + 0x1)

#define HSUDC_IVECT_SUDAV	0x00	/* usbirq(0)*/
#define HSUDC_IVECT_SOF		0x04	/* usbirq(1)*/
#define HSUDC_IVECT_SUTOK	0x08	/* usbirq(2)*/
#define HSUDC_IVECT_SUSPEND	0x0C	/* usbirq(3)*/
#define HSUDC_IVECT_RESET	0x10	/* usbirq(4)*/
#define HSUDC_IVECT_HSPEED	0x14	/* usbirq(6)*/
#define HSUDC_IVECT_OVERFLOWIR	0x16	/* usbirq(6)*/
#define HSUDC_IVECT_OTGIRQ	0xD8	/* OTG interrupt*/
#define HSUDC_IVECT_LPMIRQ	0xDC	/* LPM interrupt*/

/* Error Interrupts Registers */
#define HSUDC_ERRIRQ_IN_REG16	(HSUDC_IRQ_ERR_REG_BASE_REG + 0x4)
#define HSUDC_ERRIRQ_OUT_REG16	(HSUDC_IRQ_ERR_REG_BASE_REG + 0x6)
#define HSUDC_ERRIEN_IN_REG16	(HSUDC_IRQ_ERR_EN_BASE_REG + 0x0)
#define HSUDC_ERRIEN_OUT_REG16	(HSUDC_IRQ_ERR_EN_BASE_REG + 0x2)

/* Control and Status Registers */
#define HSUDC_ENDPRST_REG8	(HSUDC_CTRL_STAT_BASE_REG + 0x0)
#define HSUDC_USBCS_REG8	(HSUDC_CTRL_STAT_BASE_REG + 0x1)
#define HSUDC_FRMNR_REG16	(HSUDC_CTRL_STAT_BASE_REG + 0x2)
#define HSUDC_FNADDR_REG8	(HSUDC_CTRL_STAT_BASE_REG + 0x4)
#define HSUDC_CLKGATE_REG8	(HSUDC_CTRL_STAT_BASE_REG + 0x5)
#define HSUDC_FIFOCTRL_REG8	(HSUDC_CTRL_STAT_BASE_REG + 0x6)
#define HSUDC_SPEEDCTRL_REG8	(HSUDC_CTRL_STAT_BASE_REG + 0x7)

/* Direction bit '1' - IN endpoint selected, '0' - OUT endpoint selected */
#define HSUDC_ENDPRST_IO_MSK		0x10
#define HSUDC_ENDPRST_TOGRST_MSK	0x20	/**<  Toggle reset bit */
#define HSUDC_ENDPRST_FIFORST_MSK	0x40	/**<  Fifo reset bit */
/**<  Read access: Data toggle value, Write access: Toggle set bit */
#define HSUDC_ENDPRST_TOGSETQ_MSK	0x80

/* Send NYET handshake for the LPM transaction */
#define HSUDC_USBCS_LPMNYET_MSK	0x02
/* Set Self Powered status bit */
#define HSUDC_USBCS_SELFPWR_MSK	0x04
/* read only bit, report remote wakeup enable from host */
#define HSUDC_USBCS_RWAKEN_MSK	0x08
/* Set enumeration from FSM */
#define HSUDC_USBCS_ENUM_MSK	0x10

/* Remote wakeup bit, device mode */
#define HSUDC_USBCS_SIGRSUME_MSK	0x20
/* Software disconnect bit, device mode */
#define HSUDC_USBCS_DISCON_MSK		0x40
/* Wakeup source. Wakesrc=1 indicates that a wakeup pin resumed the HC.
 * The microprocessor resets this bit by writing a '1' to it
 */
#define HSUDC_USBCS_WAKESRC_MSK		0x80

/* Direction bit '1' - IN endpoint selected, '0' - OUT endpoint selected */
#define HSUDC_FIFOCTRL_IO_MSK		0x10
#define HSUDC_FIFOCTRL_FIFOAUTO_MSK	0x20    /* FIFO auto bit */
#define HSUDC_FIFOCTRL_FIFOCMIT_MSK	0x40    /* FIFO commit bit */
#define HSUDC_FIFOCTRL_FIFOACC_MSK	0x80    /* FIFO access bit */

#define HSUDC_SPEEDCTRL_FS_MSK		0x02      /* Enable Full-Speed */
#define HSUDC_SPEEDCTRL_HS_MSK		0x04      /* Enable High-Speed */
#define HSUDC_SPEEDCTRL_HSDIS_MSK	0x80      /* Disable High-Speed */

/* EPs Maxpacket Registers */
#define HSUDC_EP0_MAXPCK_REG8	(HSUDC_OUT_MAXPACKET_BASE_REG + 0x00)

/* Use special HSUDC_EP0_MAXPCK for EP0 */
#define HSUDC_EP_OUT_MAXPCK_REG16(n) (HSUDC_OUT_MAXPACKET_BASE_REG + 0x2*(n))
#define HSUDC_EP_IN_MAXPCK_REG16(n) (HSUDC_IN_MAXPACKET_BASE_REG + 0x2*(n))

/* EPs Start Address Registers */
/* EP0 STARTADDR is not available */
#define HSUDC_EP_OUT_STARTADDR_REG16(n) (HSUDC_OUT_STARTADDR_BASE_REG + 0x4*(n))
#define HSUDC_EP_IN_STARTADDR_REG16(n) (HSUDC_IN_STARTADDR_BASE_REG + 0x4*(n))

/* DMA Interrupt Registers */
#define HSUDC_DMA_IEN_REG32		(HSUDC_DMA_IRQ_BASE_REG + 0x00)
#define HSUDC_DMA_IRQ_REG32		(HSUDC_DMA_IRQ_BASE_REG + 0x04)
#define HSUDC_DMA_SHORTIEN_REG32	(HSUDC_DMA_IRQ_BASE_REG + 0x08)
#define HSUDC_DMA_SHORTIRQ_REG32	(HSUDC_DMA_IRQ_BASE_REG + 0x0C)
#define HSUDC_DMA_IVECT_REG8		(HSUDC_DMA_IRQ_BASE_REG + 0x10)
#define HSUDC_DMA_ERRORIEN_REG32	(HSUDC_DMA_IRQ_BASE_REG + 0x18)
#define HSUDC_DMA_ERRORIRQ_REG32	(HSUDC_DMA_IRQ_BASE_REG + 0x1C)

#define HSUDC_DMA_IVECT_CHANNEL_MASK	0x1F
#define HSUDC_DMA_IVECT_SHORTPI_MASK	0x20
#define HSUDC_DMA_IVECT_ERROR_MASK	0x80

/* DMA Channels Registers */
#define HSUDC_DMA_ADDR_REG32(n)	(HSUDC_DMA_CHANNEL_BASE_REG(n) + 0x0)
#define HSUDC_DMA_CNT_REG32(n)	(HSUDC_DMA_CHANNEL_BASE_REG(n) + 0x4)
#define HSUDC_DMA_MODE_REG8(n)	(HSUDC_DMA_CHANNEL_BASE_REG(n) + 0x8)
#define HSUDC_DMA_ECTRL_REG8(n)	(HSUDC_DMA_CHANNEL_BASE_REG(n) + 0x9)
#define HSUDC_DMA_WORK_REG8(n)	(HSUDC_DMA_CHANNEL_BASE_REG(n) + 0xA)
#define HSUDC_DMA_LINK_REG8(n)	(HSUDC_DMA_CHANNEL_BASE_REG(n) + 0xB)
#define HSUDC_DMA_ENDP_REG8(n)	(HSUDC_DMA_CHANNEL_BASE_REG(n) + 0xC)
#define HSUDC_DMA_BUSCTRL_REG8(n) (HSUDC_DMA_CHANNEL_BASE_REG(n) + 0xD)

#define HSUDC_DMA_MODE_DIRECTION_IN	0x01	/* 1 for IN, 0 for OUT */
#define HSUDC_DMA_MODE_ADDRESS_CONST	0x00	/* Constant address mode */
#define HSUDC_DMA_MODE_ADDRESS_INC	0x02	/* Incremental address mode */
#define HSUDC_DMA_MODE_ADDRESS_DEC	0x04	/* Decremental address mode */
#define HSUDC_DMA_MODE_ADDRESS_MASK	0x06
#define HSUDC_DMA_MODE_UNLIMITED	0x10	/* Unlimited transfer mode */
#define HSUDC_DMA_MODE_SINGLE		0x20	/* Single packet tx mode */

/**** Not Implemented External Transfer Request ****/

#define HSUDC_DMA_WORK_START	1
#define HSUDC_DMA_WORK_STOP	2
#define HSUDC_DMA_WORK_RESET	4

#define HSUDC_DMA_LINK_CHANNEL_MASK	0x1F
#define HSUDC_DMA_LINK_EN		0x80

#define HSUDC_DMA_ENDP_SHIFT	4
#define HSUDC_DMA_ENDP_MASK	0xF0

#define HSUDC_DMA_BUSCTRL_BIG_ENDIAN	1 /* 1 for big endian, 0 for little */
#define HSUDC_DMA_BUSCTRL_HSIZE_8BIT	(0 << 1) /* 8-bit data tranfer */
#define HSUDC_DMA_BUSCTRL_HSIZE_16BIT	(1 << 1) /* 16-bit data tranfer */
#define HSUDC_DMA_BUSCTRL_HSIZE_32BIT	(2 << 1) /* 32-bit data tranfer */
#define HSUDC_DMA_BUSCTRL_HSIZE_MASK	(3 << 1)
#define HSUDC_DMA_BUSCTRL_BURST_SINGLE	(0 << 4) /* Single tranfer */
#define HSUDC_DMA_BUSCTRL_BURST_INC	(1 << 4) /* Incrementing burst */
#define HSUDC_DMA_BUSCTRL_BURST_4BEAT	(2 << 4) /* 4beat incrementing burst */
#define HSUDC_DMA_BUSCTRL_BURST_8BEAT	(4 << 4) /* 8beat incrementing burst */
#define HSUDC_DMA_BUSCTRL_BURST_16BEAT	(6 << 4) /* 16beat incrementing burst */
#define HSUDC_DMA_BUSCTRL_BURST_MASK	(7 << 4)

#endif /* HSUDC_UDC_REGS */
