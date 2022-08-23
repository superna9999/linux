// SPDX-License-Identifier: GPL-2.0
/*
 * USB serial driver for USB to Octal UARTs chip ch348.
 *
 * Copyright (C) 2022 Corentin Labbe <clabbe@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#define DEFAULT_BAUD_RATE 9600
#define DEFAULT_TIMEOUT   2000

#define CH348_CTO_D		0x01
#define CH348_CTO_R		0x02
#define CH348_CTI_C		0x01
#define CH348_CTI_DSR		0x02
#define CH348_CTI_R		0x04
#define CH348_CTI_DCD		0x08

#define CH348_LO			0x10
#define CH348_LP			0x20
#define CH348_LF			0x40
#define CH348_LB			0x80

#define CMD_W_R				0xC0
#define CMD_W_BR			0x80

#define CMD_WB_E		0x90
#define CMD_RB_E		0xC0

#define M_NOR		0x00
#define M_HF		0x03

#define R_MOD		0x97
#define R_INIT		0xa1

#define R_C1		0x01
#define R_C2		0x02
#define R_C4		0x04
#define R_C5		0x06

#define CH348_NW  16
#define CH348_NR  32

#define MAXPORT 8

struct ch348_ttyport {
	u8 uartmode;
	struct usb_serial_port *port;
};

struct ch348 {
	struct usb_device *dev;
	struct ch348_ttyport ttyport[MAXPORT];

	struct urb              *read_urbs[CH348_NR];
	unsigned char           *bulk_in_buffers[CH348_NR];

	struct urb              *write_urbs[CH348_NW];
	unsigned char           *bulk_out_buffers[CH348_NW];
	unsigned long write_urbs_free;
	int write_port[CH348_NW];

	int tx_endpoint;
	int cmdtx_endpoint;

	spinlock_t write_lock;
	int writesize;
};

static int do_magic(struct ch348 *ch348, int portnum, u8 action, u8 reg, u8 control)
{
	int ret = 0, len;
	u8 *buffer;

	buffer = kzalloc(3, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	if (portnum < 4)
		reg += 0x10 * portnum;
	else
		reg += 0x10 * (portnum - 4) + 0x08;

	buffer[0] = action;
	buffer[1] = reg;
	buffer[2] = control;

	ret = usb_bulk_msg(ch348->dev, ch348->cmdtx_endpoint, buffer, 3, &len,
			   DEFAULT_TIMEOUT);
	if (ret)
		dev_err(&ch348->dev->dev, "%s usb_bulk_msg err=%d\n", __func__, ret);

	kfree(buffer);
	return ret;
}

static int ch348_configure(struct ch348 *ch348, int portnum)
{
	int ret;

	ret = do_magic(ch348, portnum, CMD_W_R, R_C2, 0x87);
	if (ret)
		return ret;
	ret = do_magic(ch348, portnum, CMD_W_R, R_C4, 0x08);
	return ret;
}

static void ch348_process_read_urb(struct urb *urb)
{
	struct ch348 *ch348 = urb->context;
	int size;
	int i = 0;
	int portnum;
	u8 usblen;
	char *buf = urb->transfer_buffer;

	if (!urb->actual_length)
		return;
	size = urb->actual_length;

	for (i = 0; i < size; i += 32) {
		portnum = *(buf + i);
		if (portnum < 0 || portnum >= 8)
			break;
		usblen = *(buf + i + 1);
		if (usblen > 30)
			break;
		tty_insert_flip_string(&ch348->ttyport[portnum].port->port,
				       buf + i + 2, usblen);
		tty_flip_buffer_push(&ch348->ttyport[portnum].port->port);
	}
}

static void ch348_read_bulk_callback(struct urb *urb)
{
	struct ch348 *ch348 = urb->context;
	int status = urb->status;
	int ret;

	if (status == -ESHUTDOWN)
		return;

	if (status) {
		dev_err(&ch348->dev->dev, "%s - non-zero urb status: %d\n",
			__func__, status);
		return;
	}

	ch348_process_read_urb(urb);
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret)
		dev_err(&ch348->dev->dev, "%s fail to submit URB err=%d\n", __func__, ret);
}

static void ch348_write_bulk_callback(struct urb *urb)
{
	struct ch348 *ch348 = urb->context;
	unsigned long flags;
	int i, portnum;

	for (i = 0; i < ARRAY_SIZE(ch348->write_urbs); ++i) {
		if (urb == ch348->write_urbs[i])
			break;
	}
	portnum = ch348->write_port[i];

	spin_lock_irqsave(&ch348->write_lock, flags);
	set_bit(i, &ch348->write_urbs_free);
	tty_port_tty_wakeup(&ch348->ttyport[portnum].port->port);
	spin_unlock_irqrestore(&ch348->write_lock, flags);
}

static int ch348_write(struct tty_struct *tty, struct usb_serial_port *port,
		       const unsigned char *buf, int count)
{
	struct ch348 *ch348 = usb_get_serial_data(port->serial);
	int portnum = port->port_number;
	unsigned long flags;
	int i;
	unsigned char *ubuf;
	int ret;
	struct urb *urb;

	if (!count)
		return 0;

	spin_lock_irqsave(&ch348->write_lock, flags);
	i = find_first_bit(&ch348->write_urbs_free,
			   ARRAY_SIZE(ch348->write_urbs));
	clear_bit(i, &ch348->write_urbs_free);
	if (i < 0) {
		spin_unlock_irqrestore(&ch348->write_lock, flags);
		return -ENOSPC;
	}
	spin_unlock_irqrestore(&ch348->write_lock, flags);
	ubuf = ch348->bulk_out_buffers[i];

	*ubuf = portnum;
	*(ubuf + 1) = count;
	*(ubuf + 2) = count >> 8;
	memcpy(ubuf + 3, buf, count);
	ch348->write_port[i] = portnum;

	urb = ch348->write_urbs[i];
	urb->transfer_buffer = ubuf;
	urb->transfer_buffer_length = count + 3;
	ret = usb_submit_urb(ch348->write_urbs[i], GFP_ATOMIC);
	if (ret < 0) {
		dev_err(&ch348->dev->dev, "%s - usb_submit_urb err=%d\n",
			__func__, ret);
		return ret;
	}

	return count;
}

static unsigned int ch348_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct ch348 *ch348 = usb_get_serial_data(port->serial);
	unsigned int n = CH348_NW;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&ch348->write_lock, flags);
	for (i = 0; i < CH348_NW; i++)
		if (!test_bit(i, &ch348->write_urbs_free))
			n--;
	spin_unlock_irqrestore(&ch348->write_lock, flags);
	return n;
}

static int ch348_set_uartmode(struct ch348 *ch348, int portnum, u8 index, u8 mode)
{
	int ret = 0;

	if (ch348->ttyport[portnum].uartmode == M_NOR && mode == M_HF) {
		ret = do_magic(ch348, portnum, CMD_W_BR, R_C4, 0x51);
		if (ret)
			return ret;
		ch348->ttyport[portnum].uartmode = M_HF;
	}

	if (ch348->ttyport[portnum].uartmode == M_HF && mode == M_NOR) {
		ret = do_magic(ch348, portnum, CMD_W_BR, R_C4, 0x50);
		if (ret)
			return ret;
		ch348->ttyport[portnum].uartmode = M_NOR;
	}
	return 0;
}

static u8 cal_recv_tmt(__le32 bd)
{
	int dly = 1000000 * 15 / bd;

	if (bd >= 921600)
		return 5;

	return (dly / 100 + 1);
}

static void ch348_set_termios(struct tty_struct *tty, struct usb_serial_port *port,
			      struct ktermios *termios_old)
{
	struct ch348 *ch348 = usb_get_serial_data(port->serial);
	int portnum = port->port_number;
	struct ktermios *termios = &tty->termios;
	int ret, sent;
	char *buffer;
	__le32	dwDTERate;
	u8	bCharFormat;
	u8	bParityType;
	u8	bDataBits;

	if (termios_old && !tty_termios_hw_change(&tty->termios, termios_old)) {
		return;
	}
	buffer = kzalloc(12, GFP_KERNEL);
	if (!buffer) {
		if (termios_old)
			tty->termios = *termios_old;
		return;
	}

	dwDTERate = tty_get_baud_rate(tty);
	if (dwDTERate == 0)
		dwDTERate = DEFAULT_BAUD_RATE;

	bCharFormat = termios->c_cflag & CSTOPB ? 2 : 1;

	bParityType = termios->c_cflag & PARENB ?
			     (termios->c_cflag & PARODD ? 1 : 2) +
			     (termios->c_cflag & CMSPAR ? 2 : 0) : 0;

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		bDataBits = 5;
		break;
	case CS6:
		bDataBits = 6;
		break;
	case CS7:
		bDataBits = 7;
		break;
	case CS8:
	default:
		bDataBits = 8;
		break;
	}

	buffer[0] = CMD_WB_E | (portnum & 0x0F);
	buffer[1] = R_INIT;
	buffer[2] = portnum;
	buffer[3] = dwDTERate >> 24;
	buffer[4] = dwDTERate >> 16;
	buffer[5] = dwDTERate >> 8;
	buffer[6] = dwDTERate;
	if (bCharFormat == 2)
		buffer[7] = 0x02;
	else if (bCharFormat == 1)
		buffer[7] = 0x00;
	buffer[8] = bParityType;
	buffer[9] = bDataBits;
	buffer[10] = cal_recv_tmt(dwDTERate);
	buffer[11] = 0;
	ret = usb_bulk_msg(ch348->dev, ch348->cmdtx_endpoint, buffer,
			   12, &sent, DEFAULT_TIMEOUT);
	if (ret < 0) {
		dev_err(&ch348->dev->dev, "%s usb_bulk_msg err=%d\n",
			__func__, ret);
		goto out;
	}

	ret = do_magic(ch348, portnum, CMD_W_R, R_C1, 0x0F);
	if (ret < 0)
		goto out;

	if (C_CRTSCTS(tty)) {
		ret = ch348_set_uartmode(ch348, portnum, portnum, M_HF);
	} else {
		ret = ch348_set_uartmode(ch348, portnum, portnum, M_NOR);
	}

out:
	kfree(buffer);
}

static int ch348_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct ch348 *ch348 = usb_get_serial_data(port->serial);
	int rv;

	if (tty)
		ch348_set_termios(tty, port, NULL);

	rv = ch348_configure(ch348, port->port_number);
	if (rv)
		pr_err("%s configure err\n", __func__);

	rv = usb_serial_generic_open(tty, port);

	return rv;
}

static int ch348_allocate_write(struct ch348 *ch348, struct usb_endpoint_descriptor *epd)
{
	int buffer_size = usb_endpoint_maxp(epd);
	int i;

	for (i = 0; i < CH348_NW; i++) {
		set_bit(i, &ch348->write_urbs_free);
		ch348->write_urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!ch348->write_urbs[i])
			return -ENOMEM;
		ch348->bulk_out_buffers[i] = devm_kmalloc(&ch348->dev->dev,
							  buffer_size,
							  GFP_KERNEL);
		if (!ch348->bulk_in_buffers[i])
			return -ENOMEM;
		usb_fill_bulk_urb(ch348->write_urbs[i], ch348->dev,
				  ch348->tx_endpoint,
				  NULL, buffer_size,
				  ch348_write_bulk_callback, ch348);
	}
	return 0;
}

static int ch348_free_write(struct ch348 *ch348)
{
	int i;

	for (i = 0; i < CH348_NW; i++) {
		if (ch348->write_urbs[i]) {
			usb_kill_urb(ch348->write_urbs[i]);
			usb_free_urb(ch348->write_urbs[i]);
		}
	}
	return 0;
}

static int ch348_allocate_read(struct ch348 *ch348, struct usb_endpoint_descriptor *epd)
{
	int buffer_size = usb_endpoint_maxp(epd);
	int i, ret;

	for (i = 0; i < CH348_NR; i++) {
		ch348->read_urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!ch348->read_urbs[i])
			return -ENOMEM;
		ch348->bulk_in_buffers[i] = devm_kmalloc(&ch348->dev->dev,
							 buffer_size, GFP_KERNEL);
		if (!ch348->bulk_in_buffers[i])
			return -ENOMEM;
		usb_fill_bulk_urb(ch348->read_urbs[i], ch348->dev,
				  usb_rcvbulkpipe(ch348->dev, epd->bEndpointAddress),
				  ch348->bulk_in_buffers[i], buffer_size,
				  ch348_read_bulk_callback, ch348);
		ret = usb_submit_urb(ch348->read_urbs[i], GFP_KERNEL);
		if (ret)
			return ret;
	}
	return 0;
}

static int ch348_free_read(struct ch348 *ch348)
{
	int i;

	for (i = 0; i < CH348_NR; i++) {
		if (ch348->read_urbs[i]) {
			usb_kill_urb(ch348->read_urbs[i]);
			usb_free_urb(ch348->read_urbs[i]);
		}
	}
	return 0;
}

static int ch348_probe(struct usb_serial *serial, const struct usb_device_id *id)
{
	struct usb_interface *data_interface;
	struct usb_endpoint_descriptor *epcmdwrite = NULL;
	struct usb_endpoint_descriptor *epread = NULL;
	struct usb_endpoint_descriptor *epwrite = NULL;
	struct usb_device *usb_dev = serial->dev;
	struct ch348 *ch348;
	int ret = -ENOMEM;

	data_interface = usb_ifnum_to_if(usb_dev, 0);

	epread = &data_interface->cur_altsetting->endpoint[0].desc;
	epwrite = &data_interface->cur_altsetting->endpoint[1].desc;
	epcmdwrite = &data_interface->cur_altsetting->endpoint[3].desc;

	ch348 = devm_kzalloc(&serial->dev->dev, sizeof(*ch348), GFP_KERNEL);
	if (!ch348)
		goto alloc_fail;

	usb_set_serial_data(serial, ch348);

	ch348->writesize = usb_endpoint_maxp(epwrite);
	ch348->dev = serial->dev;

	spin_lock_init(&ch348->write_lock);

	ch348->tx_endpoint = usb_sndbulkpipe(usb_dev, epwrite->bEndpointAddress);
	ch348->cmdtx_endpoint = usb_sndbulkpipe(usb_dev, epcmdwrite->bEndpointAddress);

	ret = ch348_allocate_read(ch348, epread);
	if (ret)
		goto err_read;
	ret = ch348_allocate_write(ch348, epwrite);
	if (ret)
		goto err_write;

	dev_info(&serial->dev->dev, "ch348 device attached. Vr0.7\n");

	return 0;

err_write:
	ch348_free_write(ch348);
err_read:
	ch348_free_read(ch348);
alloc_fail:
	return ret;
}

static void ch348_release(struct usb_serial *serial)
{
	struct ch348 *ch348 = usb_get_serial_data(serial);

	ch348_free_write(ch348);
	ch348_free_read(ch348);
}

static int ch348_port_probe(struct usb_serial_port *port)
{
	struct ch348 *ch348 = usb_get_serial_data(port->serial);

	ch348->ttyport[port->port_number].port = port;

	return 0;
}

static void ch348_process_read_urbnew(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct ch348 *ch348 = usb_get_serial_data(port->serial);
	int size;
	char buffer[512];
	int i = 0;
	int portnum;
	u8 usblen;

	if (!urb->actual_length)
		return;
	size = urb->actual_length;
/*	pr_info("%s size=%d\n", __func__, size);*/

	if (size > 512)
		return;

	memcpy(buffer, urb->transfer_buffer, urb->actual_length);

	for (i = 0; i < size; i += 32) {
		portnum = *(buffer + i);
		if (portnum < 0 || portnum >= 8)
			break;
		usblen = *(buffer + i + 1);
		if (usblen > 30)
			break;
		port = ch348->ttyport[portnum].port;
		tty_insert_flip_string(&port->port, buffer + i + 2, usblen);
		tty_flip_buffer_push(&port->port);
		port->icount.rx += usblen;
		usb_serial_debug_data(&port->dev, __func__, usblen, buffer + i + 2);
	}
}

static const struct usb_device_id ch348_ids[] = {
	{ USB_DEVICE(0x1a86, 0x55d9), },
	{ }
};

MODULE_DEVICE_TABLE(usb, ch348_ids);

static struct usb_serial_driver ch348_device = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "ch348",
	},
	.id_table =		ch348_ids,
	.num_ports =		8,
	.open =			ch348_open,
	.write =		ch348_write,
	.write_room =		ch348_write_room,
	.set_termios =		ch348_set_termios,
	.process_read_urb =	ch348_process_read_urbnew,
	.probe =		ch348_probe,
	.release =		ch348_release,
	.port_probe =		ch348_port_probe,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&ch348_device, NULL
};

module_usb_serial_driver(serial_drivers, ch348_ids);

MODULE_AUTHOR("Corentin Labbe <clabbe@baylibre.com>");
MODULE_DESCRIPTION("USB CH348 Octo port serial converter driver");
MODULE_LICENSE("GPL");
