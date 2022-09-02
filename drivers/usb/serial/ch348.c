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

#define CH348_CTO_D	0x01
#define CH348_CTO_R	0x02

#define CH348_CTI_C	0x10
#define CH348_CTI_DSR	0x20
#define CH348_CTI_R	0x40
#define CH348_CTI_DCD	0x80

#define CH348_LO	0x02
#define CH348_LP	0x04
#define CH348_LF	0x08
#define CH348_LB	0x10

#define CMD_W_R		0xC0
#define CMD_W_BR	0x80

#define CMD_WB_E	0x90
#define CMD_RB_E	0xC0

#define M_NOR		0x00
#define M_HF		0x03

#define R_MOD		0x97
#define R_IO_D  	0x98
#define R_IO_O  	0x99
#define R_IO_I   	0x9b
#define R_TM_O		0x9c
#define R_INIT		0xa1

#define R_C1		0x01
#define R_C2		0x02
#define R_C4		0x04
#define R_C5		0x06

#define R_II_B1 	0x06
#define R_II_B2 	0x02
#define R_II_B3		0x00

#define CH348_RX_PORTNUM_OFF		0
#define CH348_RX_LENGTH_OFF		1
#define CH348_RX_DATA_OFF		2

#define CH348_RX_PORT_CHUNK_LENGTH	32
#define CH348_RX_PORT_MAX_LENGTH	30

#define CH348_TX_PORTNUM_OFF		0
#define CH348_TX_LENGTH0_OFF		1
#define CH348_TX_LENGTH1_OFF		2
#define CH348_TX_DATA_OFF		3

#define MAXPORT 8

/*
 * The Ch348 multiplexes rx & tx into a pair of Bulk USB endpoints for
 * the 8 serial ports, and anothe pair of Bulk USB endpoints to
 * set port settings and receive port status events.
 *
 * The USB serial cores ties every Bulk endpoints pair to each port,
 * but in our case it will set port 0 with the rx/tx endpoints
 * and port 1 with the settup/status endpoints.
 *
 * To still take advantage of the generic code, we (re-)initialize
 * the USb serial port structure with the correct USB endpoint
 * for read and write, and write proper process_read_urb()
 * and prepare_write_buffer() to correctly (de-)multiplex data.
 */

struct ch348_ttyport {
	u8 uartmode;
	struct usb_serial_port *port;
	unsigned int io_status;
	unsigned int modem_status;
};

struct ch348 {
	struct usb_device *dev;
	struct ch348_ttyport ttyport[MAXPORT];

	int rx_endpoint;
	int tx_endpoint;
	int statusrx_endpoint;
	int cmdtx_endpoint;

	struct urb *status_read_urb;
	u8 *status_read_buffer;

	spinlock_t status_lock;
	int readsize;
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
	struct usb_serial_port *port = urb->context;
	struct ch348 *ch348 = usb_get_serial_data(port->serial);
	u8 *buffer = urb->transfer_buffer, *end;
	unsigned int portnum, usblen;

	if (!urb->actual_length) {
		dev_warn(&port->dev, "%s:%d empty rx buffer\n", __func__, __LINE__);
		return;
	}

	end = buffer + urb->actual_length;

	for (; buffer < end; buffer += CH348_RX_PORT_CHUNK_LENGTH) {
		portnum = buffer[CH348_RX_PORTNUM_OFF];
		if (portnum < 0 || portnum >= 8) {
			dev_warn(&port->dev, "%s:%d invalid port %d\n",
				 __func__, __LINE__, portnum);
			break;
		}

		usblen = buffer[CH348_RX_LENGTH_OFF];
		if (usblen > 30) {
			dev_warn(&port->dev, "%s:%d invalid length %d for port %d\n",
				 __func__, __LINE__, usblen, portnum);
			break;
		}

		port = ch348->ttyport[portnum].port;
		tty_insert_flip_string(&port->port, &buffer[CH348_RX_DATA_OFF], usblen);
		tty_flip_buffer_push(&port->port);
		port->icount.rx += usblen;
		usb_serial_debug_data(&port->dev, __func__, usblen, &buffer[CH348_RX_DATA_OFF]);
	}
}

static int ch348_prepare_write_buffer(struct usb_serial_port *port, void *dest, size_t size)
{
	u8 *buf = dest;
	int count;

	count = kfifo_out_locked(&port->write_fifo, buf + CH348_TX_DATA_OFF,
				size - CH348_TX_DATA_OFF,
				&port->lock);

	buf[CH348_TX_PORTNUM_OFF] = port->port_number;
	buf[CH348_TX_LENGTH0_OFF] = count;
	buf[CH348_TX_LENGTH1_OFF] = count >> 8;

	return count + CH348_TX_DATA_OFF;
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

static int ch348_fixup_port_bulk_in(struct ch348 *ch348, struct usb_serial_port *port)
{
	int i;

	/* Already Initialized */
	if (port->bulk_in_size) {
		port->bulk_in_endpointAddress = usb_pipeendpoint(ch348->rx_endpoint);

		for (i = 0; i < ARRAY_SIZE(port->read_urbs); ++i)
			usb_fill_bulk_urb(port->read_urbs[i], port->serial->dev,
					  ch348->rx_endpoint,
					  port->bulk_in_buffers[i], port->bulk_in_size,
					  usb_serial_generic_read_bulk_callback, port);
	} else {
		port->bulk_in_size = ch348->readsize;
		port->bulk_in_endpointAddress = usb_pipeendpoint(ch348->rx_endpoint);

		for (i = 0; i < ARRAY_SIZE(port->read_urbs); ++i) {
			set_bit(i, &port->read_urbs_free);
			port->read_urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
			if (!port->read_urbs[i])
				return -ENOMEM;
			port->bulk_in_buffers[i] = kmalloc(port->bulk_in_size, GFP_KERNEL);
			if (!port->bulk_in_buffers[i])
				return -ENOMEM;
			usb_fill_bulk_urb(port->read_urbs[i], port->serial->dev,
					  ch348->rx_endpoint,
					  port->bulk_in_buffers[i], port->bulk_in_size,
					  usb_serial_generic_read_bulk_callback, port);
		}

		port->read_urb = port->read_urbs[0];
		port->bulk_in_buffer = port->bulk_in_buffers[0];
	}

	return 0;
}

static int ch348_fixup_port_bulk_out(struct ch348 *ch348, struct usb_serial_port *port)
{
	int i;

	/* Already Initialized */
	if (port->bulk_out_size) {
		port->bulk_out_endpointAddress = usb_pipeendpoint(ch348->tx_endpoint);

		for (i = 0; i < ARRAY_SIZE(port->write_urbs); ++i)
			usb_fill_bulk_urb(port->write_urbs[i], port->serial->dev,
					  ch348->tx_endpoint,
					  port->bulk_out_buffers[i], port->bulk_out_size,
					  usb_serial_generic_write_bulk_callback, port);
	} else {
		if (kfifo_alloc(&port->write_fifo, PAGE_SIZE, GFP_KERNEL))
			return -ENOMEM;

		port->bulk_out_size = ch348->writesize;
		port->bulk_out_endpointAddress = usb_pipeendpoint(ch348->tx_endpoint);

		for (i = 0; i < ARRAY_SIZE(port->write_urbs); ++i) {
			set_bit(i, &port->write_urbs_free);
			port->write_urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
			if (!port->write_urbs[i])
				return -ENOMEM;
			port->bulk_out_buffers[i] = kmalloc(port->bulk_out_size, GFP_KERNEL);
			if (!port->bulk_out_buffers[i])
				return -ENOMEM;
			usb_fill_bulk_urb(port->write_urbs[i], port->serial->dev,
					  ch348->tx_endpoint,
					  port->bulk_out_buffers[i], port->bulk_out_size,
					  usb_serial_generic_write_bulk_callback, port);
		}

		port->write_urb = port->write_urbs[0];
		port->bulk_out_buffer = port->bulk_out_buffers[0];
	}

	return 0;
}

static int ch348_attach(struct usb_serial *serial)
{
	struct ch348 *ch348 = usb_get_serial_data(serial);
	int i, ret;

	/* Fixup Bulk OUT and IN endpoints */
	for (i = 0; i < serial->num_port_pointers; ++i) {
		ret = ch348_fixup_port_bulk_out(ch348, serial->port[i]);
		if (ret)
			return ret;
		ret = ch348_fixup_port_bulk_in(ch348, serial->port[i]);
		if (ret)
			return ret;
	}

	return usb_submit_urb(ch348->status_read_urb, GFP_KERNEL);
}

static void ch348_update_io_status(struct ch348 *ch348, unsigned int portnum, u8 data)
{
	u8 diff;

	if (portnum > 8)
		return;

	data &= (CH348_LO | CH348_LP | CH348_LF | CH348_LB);

	spin_lock(&ch348->status_lock);
	diff = data ^ ch348->ttyport[portnum].io_status;
	ch348->ttyport[portnum].io_status = data;
	spin_unlock(&ch348->status_lock);

	if (!diff)
		return;

	if (diff & CH348_LO)
		ch348->ttyport[portnum].port->icount.overrun++;
	if (diff & CH348_LP)
		ch348->ttyport[portnum].port->icount.parity++;
	if (diff & CH348_LF)
		ch348->ttyport[portnum].port->icount.frame++;
	if (diff & CH348_LB)
		ch348->ttyport[portnum].port->icount.brk++;

	wake_up_interruptible(&ch348->ttyport[portnum].port->port.delta_msr_wait);
}

static void ch348_update_modem_status(struct ch348 *ch348, unsigned int portnum, u8 data)
{
	struct tty_struct *tty;
	u8 diff;

	if (portnum > 8)
		return;

	data &= (CH348_CTI_C | CH348_CTI_DSR | CH348_CTI_R | CH348_CTI_DCD);

	spin_lock(&ch348->status_lock);
	diff = data ^ ch348->ttyport[portnum].modem_status;
	ch348->ttyport[portnum].modem_status = data;
	spin_unlock(&ch348->status_lock);

	if (!diff)
		return;

	if (diff & CH348_CTI_C)
		ch348->ttyport[portnum].port->icount.cts++;
	if (diff & CH348_CTI_DSR)
		ch348->ttyport[portnum].port->icount.dsr++;
	if (diff & CH348_CTI_R)
		ch348->ttyport[portnum].port->icount.rng++;
	if (diff & CH348_CTI_DCD) {
		ch348->ttyport[portnum].port->icount.dcd++;

		tty = tty_port_tty_get(&ch348->ttyport[portnum].port->port);
		if (tty) {
			usb_serial_handle_dcd_change(ch348->ttyport[portnum].port, tty,
						     data & CH348_CTI_DCD);
			tty_kref_put(tty);
		}
	}

	wake_up_interruptible(&ch348->ttyport[portnum].port->port.delta_msr_wait);
}

static void ch348_update_status(struct ch348 *ch348, u8 *data, unsigned int len)
{
	u8 *end = data + len;
	unsigned int portnum, reg;

	for (; data < end; ) {
		portnum = data[0] & 0xf;
		reg = data[1];

		if (reg == R_INIT) {
			data += 12;
			continue;
		}

		if (reg >= R_MOD && reg <= R_IO_I) {
			/* This signal is used by vendor driver to handle GPIO Interrupts */
			dev_dbg(&ch348->dev->dev, "port%d: unhandled status %02x%02x\n",
				portnum, data[2], data[3]);
			data += 4;
			continue;
		}

		if ((reg & 0xf) == R_II_B1) {
			dev_dbg(&ch348->dev->dev, "port%d: uart io state %02x\n", portnum, data[2]);
			ch348_update_io_status(ch348, portnum, data[2]);
			data += 3;
			continue;
		}

		if ((reg & 0xf) == R_II_B2) {
			/* This signal is used by vendor driver to aggregate multiple port TX */
			dev_dbg(&ch348->dev->dev, "port%d: unhandled Write Empty status\n", portnum);
			data += 3;
			continue;
		}

		if ((reg & 0xf) == R_II_B3) {
			dev_dbg(&ch348->dev->dev, "port%d: modem status %02x\n", portnum, data[2]);
			ch348_update_modem_status(ch348, portnum, data[2]);
			data += 3;
			continue;
		}

		dev_dbg(&ch348->dev->dev, "port%d: unknown status %02x\n", portnum, reg);
		data += 3;
	}
}

static void ch348_status_read_bulk_callback(struct urb *urb)
{
	struct ch348 *ch348 = urb->context;
	u8 *data = urb->transfer_buffer;
	unsigned int len = urb->actual_length;
	int ret;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&ch348->dev->dev, "%s - urb shutting down: %d\n",
			__func__, urb->status);
		return;
	default:
		dev_dbg(&ch348->dev->dev, "%s - nonzero urb status: %d\n",
			__func__, urb->status);
		goto exit;
	}

	usb_serial_debug_data(&ch348->dev->dev, __func__, len, data);
	ch348_update_status(ch348, data, len);

exit:
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret && urb->status == 0) {
		dev_err(&ch348->dev->dev, "%s - usb_submit_urb failed: %d\n",
			__func__, ret);
	}
}

static int ch348_allocate_status_read(struct ch348 *ch348, struct usb_endpoint_descriptor *epd)
{
	int buffer_size = usb_endpoint_maxp(epd);

	ch348->status_read_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ch348->status_read_urb)
		return -ENOMEM;
	ch348->status_read_buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!ch348->status_read_buffer)
		return -ENOMEM;

	usb_fill_bulk_urb(ch348->status_read_urb, ch348->dev,
			  ch348->statusrx_endpoint, ch348->status_read_buffer,
			  buffer_size, ch348_status_read_bulk_callback, ch348);

	return 0;
}

static void ch348_release(struct usb_serial *serial)
{
	struct ch348 *ch348 = usb_get_serial_data(serial);

	usb_kill_urb(ch348->status_read_urb);
	usb_free_urb(ch348->status_read_urb);
}

static int ch348_probe(struct usb_serial *serial, const struct usb_device_id *id)
{
	struct usb_interface *data_interface;
	struct usb_endpoint_descriptor *epcmdwrite = NULL;
	struct usb_endpoint_descriptor *epstatusread = NULL;
	struct usb_endpoint_descriptor *epread = NULL;
	struct usb_endpoint_descriptor *epwrite = NULL;
	struct usb_device *usb_dev = serial->dev;
	struct ch348 *ch348;
	int ret;

	data_interface = usb_ifnum_to_if(usb_dev, 0);

	epread = &data_interface->cur_altsetting->endpoint[0].desc;
	epwrite = &data_interface->cur_altsetting->endpoint[1].desc;
	epstatusread = &data_interface->cur_altsetting->endpoint[2].desc;
	epcmdwrite = &data_interface->cur_altsetting->endpoint[3].desc;

	ch348 = devm_kzalloc(&serial->dev->dev, sizeof(*ch348), GFP_KERNEL);
	if (!ch348)
		return -ENOMEM;

	usb_set_serial_data(serial, ch348);

	ch348->readsize = usb_endpoint_maxp(epread);
	ch348->writesize = usb_endpoint_maxp(epwrite);
	ch348->dev = serial->dev;

	spin_lock_init(&ch348->status_lock);

	ch348->rx_endpoint = usb_rcvbulkpipe(usb_dev, epread->bEndpointAddress);
	ch348->tx_endpoint = usb_sndbulkpipe(usb_dev, epwrite->bEndpointAddress);
	ch348->cmdtx_endpoint = usb_sndbulkpipe(usb_dev, epcmdwrite->bEndpointAddress);
	ch348->statusrx_endpoint = usb_rcvbulkpipe(usb_dev, epstatusread->bEndpointAddress);

	ret = ch348_allocate_status_read(ch348, epstatusread);
	if (ret)
		return ret;

	dev_info(&serial->interface->dev, "ch348 device attached. Vr0.7\n");

	return 0;
}

static int ch348_port_probe(struct usb_serial_port *port)
{
	struct ch348 *ch348 = usb_get_serial_data(port->serial);

	ch348->ttyport[port->port_number].port = port;

	return 0;
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
	.set_termios =		ch348_set_termios,
	.process_read_urb =	ch348_process_read_urb,
	.prepare_write_buffer =	ch348_prepare_write_buffer,
	.probe =		ch348_probe,
	.attach =		ch348_attach,
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
