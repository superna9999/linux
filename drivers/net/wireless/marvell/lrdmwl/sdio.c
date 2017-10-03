/*
 * Marvell Wireless LAN device driver: SDIO specific handling
 *
 * Copyright (C) 2011-2014, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/gpio.h>
#include "sysadpt.h"
#include "dev.h"
#include "main.h"
#include "fwcmd.h"
#include "rx.h"
#include "tx.h"
#include "sdio.h"
#include "hostcmd.h"

#define MWL_SDIODRV_VERSION  "10.3.0.16-20160105"
#define LRD_SDIO_VERSION     LRD_BLD_VERSION "-" MWL_SDIODRV_VERSION
#define LRD_SDIO_DESC        "Laird 60 Series Wireless SDIO Network Driver"

#define INTF_HEADER_LEN         4

#ifdef CONFIG_ARCH_BERLIN
#define MWL_FW_ROOT     "mrvl"
#else
#define MWL_FW_ROOT     "lrdmwl"
#endif
static struct mwl_chip_info mwl_chip_tbl[] = {
	[MWL8864] = {
		.part_name	= "88W8864",
		.fw_image	= MWL_FW_ROOT"/88W8864_sdio.bin",
		.antenna_tx	= ANTENNA_TX_4_AUTO,
		.antenna_rx	= ANTENNA_RX_4_AUTO,
	},
	[MWL8897] = {
		.part_name	= "88W8897",
		.fw_image	= MWL_FW_ROOT"/88W8897_sdio.bin",
		.antenna_tx	= ANTENNA_TX_2,
		.antenna_rx	= ANTENNA_RX_2,
	},
	[MWL8997] = {
		.part_name	= "88W8997",
		.fw_image	= MWL_FW_ROOT"/88W8997_sdio.bin",
		.antenna_tx	= ANTENNA_TX_2,
		.antenna_rx	= ANTENNA_RX_2,
	},
};


static int
mwl_write_data_sync(struct mwl_priv *priv,
			u8 *buffer, u32 pkt_len, u32 port);
static int mwl_sdio_enable_int(struct mwl_priv *priv);
static int mwl_sdio_complete_cmd(struct mwl_priv *priv);
static int mwl_sdio_event(struct mwl_priv *priv);
static void mwl_sdio_tx_workq(struct work_struct *work);
static void mwl_sdio_rx_recv(unsigned long data);
static void mwl_sdio_flush_amsdu(unsigned long data);
static void mwl_sdio_flush_amsdu_no_lock(unsigned long data);
static int mwl_sdio_read_fw_status(struct mwl_priv *priv, u16 *dat);

static int mwl_sdio_init_gpio(void);
static int mwl_sdio_release_gpio(void);
static int mwl_sdio_reset_gpio(struct mwl_priv *priv);

/* Device ID for SD8897 */
#define SDIO_DEVICE_ID_MARVELL_8897   (0x912d)
#define SDIO_DEVICE_ID_MARVELL_8997   (0x9141)

static void
mwl_sdio_interrupt(struct sdio_func *func);

/* WLAN IDs */
static const struct sdio_device_id mwl_sdio_id_tbl[] = {

	{SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8897),
		.driver_data = MWL8897},
	{SDIO_DEVICE(SDIO_VENDOR_ID_MARVELL, SDIO_DEVICE_ID_MARVELL_8997),
		.driver_data = MWL8997},
	{ },
};

/*
 * This function reads data from SDIO card register.
 */
static int
mwl_read_reg(struct mwl_priv *priv, u32 reg, u8 *data)
{
	struct mwl_sdio_card *card = priv->intf;
	int ret = -1;
	u8 val;

	sdio_claim_host(card->func);
	val = sdio_readb(card->func, reg, &ret);
	sdio_release_host(card->func);

	*data = val;

	return ret;
}

/*
 * This function writes data into SDIO card register.
 */
static int
mwl_write_reg(struct mwl_priv *priv, u32 reg, u8 data)
{
	struct mwl_sdio_card *card = priv->intf;
	int rc = 0;

	sdio_claim_host(card->func);
	sdio_writeb(card->func, data, reg, &rc);
	sdio_release_host(card->func);

	return rc;
}

/*
 * This function allocates the MPA Tx and Rx buffers.
 */
static int mwl_alloc_sdio_mpa_buffers(struct mwl_priv *priv,
				   u32 mpa_tx_buf_size, u32 mpa_rx_buf_size)
{
	struct mwl_sdio_card *card = priv->intf;
	u32 rx_buf_size;
	int ret = 0;

	card->mpa_tx.buf = kzalloc(mpa_tx_buf_size, GFP_KERNEL);
	if (!card->mpa_tx.buf) {
		ret = -1;
		goto error;
	}

	card->mpa_tx.buf_size = mpa_tx_buf_size;

	rx_buf_size = max_t(u32, mpa_rx_buf_size,
			    (u32)SDIO_MAX_AGGR_BUF_SIZE);
	card->mpa_rx.buf = kzalloc(rx_buf_size, GFP_KERNEL);
	if (!card->mpa_rx.buf) {
		ret = -1;
		goto error;
	}

	card->mpa_rx.buf_size = rx_buf_size;

error:
	if (ret) {
		kfree(card->mpa_tx.buf);
		kfree(card->mpa_rx.buf);
		card->mpa_tx.buf_size = 0;
		card->mpa_rx.buf_size = 0;
	}

	return ret;
}

static void *mwl_alloc_dma_align_buf(int rx_len, gfp_t flags)
{
	struct sk_buff *skb;
	int buf_len, pad;

	buf_len = rx_len + MWL_RX_HEADROOM + MWL_DMA_ALIGN_SZ;

	skb = __dev_alloc_skb(buf_len, flags);

	if (!skb)
		return NULL;

	skb_reserve(skb, MWL_RX_HEADROOM);

	pad = MWL_ALIGN_ADDR(skb->data, MWL_DMA_ALIGN_SZ) -
	      (long)skb->data;

	skb_reserve(skb, pad);

	return skb;
}



/*
 * This function polls the card status.
 */
static int
mwl_sdio_poll_card_status(struct mwl_priv *priv, u8 bits)
{
	struct mwl_sdio_card *card = priv->intf;
	u32 tries;
	u8 cs;

	for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
		if (mwl_read_reg(priv, card->reg->poll_reg, &cs))
			break;
		else if ((cs & bits) == bits)
			return 0;

		usleep_range(10, 20);
	}

	wiphy_err(priv->hw->wiphy,
		    "poll card status failed, tries = %d\n", tries);

	return -1;
}



/*
 * This function is used to initialize IO ports for the
 * chipsets supporting SDIO new mode eg SD8897.
 */
static int mwl_init_sdio_new_mode(struct mwl_priv *priv)
{
	struct mwl_sdio_card *card = priv->intf;
	int rc = 0;
	u8 reg;

	card->ioport = MEM_PORT;

	/* enable sdio new mode */
	if (mwl_read_reg(priv, card->reg->card_cfg_2_1_reg, &reg)) {
		rc = -EIO;
		goto err_new_mode;
	}

	if (mwl_write_reg(priv, card->reg->card_cfg_2_1_reg,
			      reg | CMD53_NEW_MODE)) {
		rc = -EIO;
		goto err_new_mode;
	}

	/* Configure cmd port and enable reading rx length from the register */
	if (mwl_read_reg(priv, card->reg->cmd_cfg_0, &reg)) {
		rc = -EIO;
		goto err_new_mode;
	}

	if (mwl_write_reg(priv, card->reg->cmd_cfg_0,
			      reg | CMD_PORT_RD_LEN_EN)) {
		rc = -EIO;
		goto err_new_mode;
	}

	/* Enable Dnld/Upld ready auto reset for cmd port after cmd53 is
	 * completed
	 */
	if (mwl_read_reg(priv, card->reg->cmd_cfg_1, &reg)) {
		rc = -EIO;
		goto err_new_mode;
	}

	if (mwl_write_reg(priv, card->reg->cmd_cfg_1,
			      reg | CMD_PORT_AUTO_EN)) {
		rc = -EIO;
		goto err_new_mode;
	}

err_new_mode:

	return rc;
}



/* This function initializes the IO ports.
 *
 * The following operations are performed -
 *      - Read the IO ports (0, 1 and 2)
 *      - Set host interrupt Reset-To-Read to clear
 *      - Set auto re-enable interrupt
 */
static int mwl_init_sdio_ioport(struct mwl_priv *priv)
{
	struct mwl_sdio_card *card = priv->intf;
	int rc;
	u8 reg;

	card->ioport = 0;

	rc = mwl_init_sdio_new_mode(priv);
	if (rc)
		goto cont;

	/* Read the IO port */
	rc = mwl_read_reg(priv, card->reg->io_port_0_reg, &reg);
	if (rc)
		goto err_init_ioport;

	card->ioport |= (reg & 0xff);

	rc = mwl_read_reg(priv, card->reg->io_port_1_reg, &reg);
	if (rc)
		goto err_init_ioport;

	card->ioport |= ((reg & 0xff) << 8);

	rc = mwl_read_reg(priv, card->reg->io_port_2_reg, &reg);
	if (rc)
		goto err_init_ioport;

	card->ioport |= ((reg & 0xff) << 16);

cont:
	wiphy_err(priv->hw->wiphy, "%s: SDIO FUNC1 IO port: %#x\n",
		MWL_DRV_NAME, card->ioport);

	/* Set Host interrupt reset to read to clear */
	rc = mwl_read_reg(priv, card->reg->host_int_rsr_reg, &reg);
	if (rc)
		goto err_init_ioport;

	mwl_write_reg(priv, card->reg->host_int_rsr_reg,
				  reg | card->reg->sdio_int_mask);

	/* Dnld/Upld ready set to auto reset */
	rc = mwl_read_reg(priv, card->reg->card_misc_cfg_reg, &reg);
	if (rc)
		goto err_init_ioport;

	mwl_write_reg(priv, card->reg->card_misc_cfg_reg,
				  reg | AUTO_RE_ENABLE_INT);

err_init_ioport:

	return rc;
}

static int mwl_sdio_enable_int(struct mwl_priv *priv)
{
	struct mwl_sdio_card *card = priv->intf;
	struct sdio_func *func = card->func;
	int ret;

	sdio_claim_host(func);
	sdio_writeb(func, card->reg->host_int_enable,
				       card->reg->host_int_mask_reg, &ret);
	if (ret) {
		wiphy_err(priv->hw->wiphy,
			    "=>%s(): enable host interrupt failed\n", __func__);
	} else {
		wiphy_info(priv->hw->wiphy,
				"=>%s(): enable host interrupt ok\n", __func__);
	}
	sdio_release_host(func);
	return ret;
}

static int mwl_sdio_init_irq(struct mwl_priv *priv)
{
	struct mwl_sdio_card *card = priv->intf;
	struct sdio_func *func = card->func;
	int ret;

	wiphy_info(priv->hw->wiphy,
			    "%s, register IRQ\n", __func__);

	sdio_claim_host(func);
	/* Request the SDIO IRQ */
	ret = sdio_claim_irq(func, mwl_sdio_interrupt);
	if (ret) {
		wiphy_err(priv->hw->wiphy,
			    "claim irq failed: ret=%d\n", ret);
		goto out;
	}

out:
	sdio_release_host(func);
	return ret;
}

static int mwl_sdio_init(struct mwl_priv *priv)
{
	struct mwl_sdio_card *card = priv->intf;
	struct sdio_func *func = card->func;
	const struct mwl_sdio_card_reg *reg = card->reg;
	int rc;
	u8 sdio_ireg;
	int num;

	priv->host_if = MWL_IF_SDIO;
	card->priv = priv;
	sdio_set_drvdata(card->func, card);
	priv->dev = &func->dev;

	/*
	 * Read the host_int_status_reg for ACK the first interrupt got
	 * from the bootloader. If we don't do this we get a interrupt
	 * as soon as we register the irq.
	 */
	mwl_read_reg(priv, card->reg->host_int_status_reg, &sdio_ireg);

	/* Get SDIO ioport */
	mwl_init_sdio_ioport(priv);

	/* Initialize SDIO variables in card */
	card->mp_rd_bitmap = 0;
	card->mp_wr_bitmap = 0;
	card->curr_rd_port = reg->start_rd_port;
	card->curr_wr_port = reg->start_wr_port;

	card->mp_data_port_mask = reg->data_port_mask;

	card->mpa_tx.buf_len = 0;
	card->mpa_tx.pkt_cnt = 0;
	card->mpa_tx.start_port = 0;

	card->mpa_tx.enabled = 1;
	card->mpa_tx.pkt_aggr_limit = card->mp_agg_pkt_limit;

	card->mpa_rx.buf_len = 0;
	card->mpa_rx.pkt_cnt = 0;
	card->mpa_rx.start_port = 0;

	card->mpa_rx.enabled = 1;
	card->mpa_rx.pkt_aggr_limit = card->mp_agg_pkt_limit;

	/* Allocate buffers for SDIO MP-A */
	card->mp_regs = kzalloc(reg->max_mp_regs, GFP_KERNEL);
	if (!card->mp_regs)
		return -ENOMEM;


	/* Allocate skb pointer buffers */
	card->mpa_rx.skb_arr = kzalloc((sizeof(void *)) *
				       card->mp_agg_pkt_limit, GFP_KERNEL);
	card->mpa_rx.len_arr = kzalloc(sizeof(*card->mpa_rx.len_arr) *
				       card->mp_agg_pkt_limit, GFP_KERNEL);

	rc = mwl_alloc_sdio_mpa_buffers(priv,
					     card->mp_tx_agg_buf_size,
					     card->mp_rx_agg_buf_size);

	/* Allocate 32k MPA Tx/Rx buffers if 64k memory allocation fails */
	if (rc && (card->mp_tx_agg_buf_size == MWL_MP_AGGR_BUF_SIZE_MAX ||
		    card->mp_rx_agg_buf_size == MWL_MP_AGGR_BUF_SIZE_MAX)) {
		/* Disable rx single port aggregation */
		card->host_disable_sdio_rx_aggr = true;

		rc = mwl_alloc_sdio_mpa_buffers
			(priv, MWL_MP_AGGR_BUF_SIZE_32K,
			 MWL_MP_AGGR_BUF_SIZE_32K);
		if (rc) {
			/* Disable multi port aggregation */
			card->mpa_tx.enabled = 0;
			card->mpa_rx.enabled = 0;
		}
	}

	spin_lock_init(&card->int_lock);
	spin_lock_init(&card->rx_proc_lock);
	init_waitqueue_head(&card->cmd_wait_q.wait);
	skb_queue_head_init(&card->rx_data_q);
	card->cmd_wait_q.status = 0;
	card->cmd_sent = false;
	card->data_sent = false;

	sdio_claim_host(card->func);

	/* Set block size */
	rc = sdio_set_block_size(card->func, MWL_SDIO_BLOCK_SIZE);
	sdio_release_host(card->func);
	if (rc) {
		wiphy_err(priv->hw->wiphy,
			"cannot set SDIO block size rc 0x%04x\n", rc);
		return rc;
	}

	priv->chip_type = card->chip_type;
	priv->pcmd_buf = kzalloc(CMD_BUF_SIZE, GFP_KERNEL);
	if (!priv->pcmd_buf) {
		wiphy_err(priv->hw->wiphy,
			  "%s: cannot alloc memory for command buffer\n",
			  MWL_DRV_NAME);
		return -ENOMEM;
	}

	memset(priv->pcmd_buf, 0x00, CMD_BUF_SIZE);

	/* Init the tasklet first in case there are tx/rx interrupts */
	tasklet_init(&priv->rx_task, (void *)mwl_sdio_rx_recv,
		(unsigned long)priv->hw);
	tasklet_disable(&priv->rx_task);

	for (num = 0; num < SYSADPT_NUM_OF_DESC_DATA; num++)
		skb_queue_head_init(&priv->txq[num]);

	mwl_sdio_init_irq(priv);

	return 0;
}

/*
 * This function sends data to the card.
 */
static int mwl_write_data_to_card(struct mwl_priv *priv,
				      u8 *payload, u32 pkt_len, u32 port)
{
	u32 i = 0;
	int ret;

	do {
		ret = mwl_write_data_sync(priv, payload, pkt_len, port);
		if (ret) {
			i++;
			wiphy_err(priv->hw->wiphy,
				    "host_to_card, write iomem\t"
				    "(%d) failed: %d\n", i, ret);
			if (mwl_write_reg(priv, CONFIGURATION_REG, 0x04))
				wiphy_err(priv->hw->wiphy,
					    "write CFG reg failed\n");

			ret = -1;
			if (i > MAX_WRITE_IOMEM_RETRY)
				return ret;
		}
	} while (ret == -1);
	return ret;
}

static void mwl_sdio_send_command(struct mwl_priv *priv)
{
	struct mwl_sdio_card *card = priv->intf;
	struct cmd_header *cmd_hdr = (struct cmd_header *)&priv->pcmd_buf[
		INTF_CMDHEADER_LEN(INTF_HEADER_LEN)];
	u32 buf_block_len;
	u32 blk_size;
	u16 len;
	u32 pkt_len;
	u32 port;
	int rc;
	__le16 *pbuf = (__le16 *)priv->pcmd_buf;
    int status;

    /* Wait till the card informs CMD_DNLD_RDY interrupt except
     * for get HW spec command */
    if (cmd_hdr->command != HOSTCMD_CMD_GET_HW_SPEC) {
        status = wait_event_interruptible_timeout(card->cmd_wait_q.wait,
                        (card->int_status & DN_LD_CMD_PORT_HOST_INT_STATUS),
                        (12 * HZ));
        if(status <= 0) {
            wiphy_err(priv->hw->wiphy, "CMD_DNLD failure\n");
            priv->in_send_cmd = false;
            priv->cmd_timeout = true;
            return;
        }
        else {
            card->int_status &= ~DN_LD_CMD_PORT_HOST_INT_STATUS;
        }
    }

	len = le16_to_cpu(cmd_hdr->len) +
		INTF_CMDHEADER_LEN(INTF_HEADER_LEN)*sizeof(unsigned short);
	port = CMD_PORT_SLCT;
	blk_size = MWL_SDIO_BLOCK_SIZE;
	buf_block_len = (len + blk_size - 1) / blk_size;
	pkt_len = buf_block_len * blk_size;
	card->cmd_sent = true;
	card->cmd_cond = false;
	card->cmd_id = (u16)(le16_to_cpu(cmd_hdr->command) & ~HOSTCMD_RESP_BIT);

	pbuf[0] = cpu_to_le16(pkt_len);
	pbuf[1] = cpu_to_le16(MWL_TYPE_CMD);

	rc = mwl_write_data_to_card(priv, (u8 *)&priv->pcmd_buf[0],
		pkt_len, (card->ioport + port));
	return;
}


static void mwl_sdio_cleanup(struct mwl_priv *priv)
{
	int num;
	struct mwl_sdio_card *card = priv->intf;

	/* Disable Interrupt before tx/rx cleanup */
	sdio_claim_host(card->func);
	sdio_release_irq(card->func);
	sdio_release_host(card->func);

	/* Free Tx bufs */
	for (num = 0; num < SYSADPT_NUM_OF_DESC_DATA; num++) {
		skb_queue_purge(&priv->txq[num]);
		priv->fw_desc_cnt[num] = 0;
	}

	/* Free Rx bufs */
	skb_queue_purge(&card->rx_data_q);
	return;
}

static bool mwl_sdio_check_card_status(struct mwl_priv *priv)
{
	return true;
}

/*
 * This function writes multiple data into SDIO card memory.
 *
 * This does not work in suspended mode.
 */
static int
mwl_write_data_sync(struct mwl_priv *priv,
			u8 *buffer, u32 pkt_len, u32 port)
{
	struct mwl_sdio_card *card = priv->intf;
	int ret;
	u8 blk_mode =
		(port & MWL_SDIO_BYTE_MODE_MASK) ? BYTE_MODE : BLOCK_MODE;
	u32 blk_size = (blk_mode == BLOCK_MODE) ? MWL_SDIO_BLOCK_SIZE : 1;
	u32 blk_cnt =
		(blk_mode ==
		 BLOCK_MODE) ? (pkt_len /
				MWL_SDIO_BLOCK_SIZE) : pkt_len;
	u32 ioport = (port & MWL_SDIO_IO_PORT_MASK);

	if (card->is_suspended) {
		wiphy_err(priv->hw->wiphy,
			    "%s: not allowed while suspended\n", __func__);
		return -1;
	}

	sdio_claim_host(card->func);
	ret = sdio_writesb(card->func, ioport, buffer, blk_cnt * blk_size);
	sdio_release_host(card->func);

	return ret;
}

/*
 * This function reads the firmware status.
 */
static int
mwl_sdio_read_fw_status(struct mwl_priv *priv, u16 *dat)
{
	struct mwl_sdio_card *card = priv->intf;
	const struct mwl_sdio_card_reg *reg = card->reg;
	u8 fws0, fws1;

	if (mwl_read_reg(priv, reg->status_reg_0, &fws0))
		return -1;

	if (mwl_read_reg(priv, reg->status_reg_1, &fws1))
		return -1;

	*dat = (u16) ((fws1 << 8) | fws0);

	return 0;
}


/*
 * This function checks the firmware status in card.
 *
 * The winner interface is also determined by this function.
 */
static int mwl_check_fw_status(struct mwl_priv *priv,
				   u32 poll_num)
{
	int ret = 0;
	u16 firmware_stat = 0;
	u32 tries;

	wiphy_info(priv->hw->wiphy,"Checking fw status %d\n", poll_num);

	/* Wait for firmware initialization event */
	for (tries = 1; tries <= poll_num; tries++) {
		ret = mwl_sdio_read_fw_status(priv, &firmware_stat);
		if (ret)
		{
			if(!(tries % 10) ){
				wiphy_err(priv->hw->wiphy,"fw read failed %d\n", ret);
			}
			continue;
		}

		if (firmware_stat == FIRMWARE_READY_SDIO) {
			ret = 0;
			wiphy_info(priv->hw->wiphy,
			    "firmware is ready %d\n", tries);
			break;
		} else {
			
			if(!(tries % 10) ){
				wiphy_info(priv->hw->wiphy,
				"Waiting on fw status %d 0x%x\n", tries, firmware_stat);
			}
			msleep(100);
			ret = -1;
		}
	}

	return ret;
}



/*
 * This function downloads the firmware to the card.
 *
 * Firmware is downloaded to the card in blocks. Every block download
 * is tested for CRC errors, and retried a number of times before
 * returning failure.
 */
static int mwl_sdio_program_firmware(struct mwl_priv *priv)
{
	struct mwl_sdio_card *card = priv->intf;
	const struct mwl_sdio_card_reg *reg = card->reg;
	const struct firmware *fw;
	u8 *fw_data;
	u32 fw_len;
	int ret;
	u32 offset = 0;
	u8 base0, base1;
	u8 *fwbuf;
	u16 len = 0;
	u32 txlen, tx_blocks = 0, tries;
	u32 i = 0;
	u16 firmware_status = 0;

	fw = priv->fw_ucode;
	fw_len = fw->size;
	fw_data = (u8 *)fw->data;

	if (!fw_len) {
		wiphy_err(priv->hw->wiphy,
			    "firmware image not found! Terminating download\n");
		return -1;
	}

	wiphy_info(priv->hw->wiphy,
		    "downloading FW image (%d bytes)\n", fw_len);

	/* Assume that the allocated buffer is 8-byte aligned */
	fwbuf = kzalloc(MWL_UPLD_SIZE, GFP_KERNEL);
	if (!fwbuf)
		return -ENOMEM;

	sdio_claim_host(card->func);

	ret = mwl_sdio_read_fw_status(priv, &firmware_status);

	if (!ret) {
		if (firmware_status == FIRMWARE_READY_SDIO) {
			wiphy_debug(priv->hw->wiphy, "Resetting Firmware\n");
			ret = mwl_sdio_reset_gpio(priv);
			if (ret) {
				goto err_dnld;
			}
		}
	}

	/* Perform firmware data transfer */
	do {
		/* The host polls for the DN_LD_CARD_RDY and CARD_IO_READY
		   bits */
		ret = mwl_sdio_poll_card_status(priv, CARD_IO_READY |
						    DN_LD_CARD_RDY);
		if (ret) {
			wiphy_err(priv->hw->wiphy,
				    "FW downloading \t"
				    "poll status timeout @ %d\n", offset);
			goto err_dnld;
		}

		/* More data? */
		if (offset >= fw_len)
			break;

		for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
			ret = mwl_read_reg(priv, reg->base_0_reg,
					       &base0);
			if (ret) {
				wiphy_err(priv->hw->wiphy,
					    "dev BASE0 register read failed:\t"
					    "base0=%#04X(%d). Terminating dnld\n",
					    base0, base0);
				goto err_dnld;
			}
			ret = mwl_read_reg(priv, reg->base_1_reg,
					       &base1);
			if (ret) {
				wiphy_err(priv->hw->wiphy,
					    "dev BASE1 register read failed:\t"
					    "base1=%#04X(%d). Terminating dnld\n",
					    base1, base1);
				goto err_dnld;
			}
			len = (u16) (((base1 & 0xff) << 8) | (base0 & 0xff));

			if (len)
				break;

			usleep_range(10, 20);
		}

		if (!len) {
			break;
		} else if (len > MWL_UPLD_SIZE) {
			wiphy_err(priv->hw->wiphy,
				    "FW dnld failed @ %d, invalid length %d\n",
				    offset, len);
			ret = -1;
			goto err_dnld;
		}

		txlen = len;

		if (len & BIT(0)) {
			i++;
			if (i > MAX_WRITE_IOMEM_RETRY) {
				wiphy_err(priv->hw->wiphy,
					    "FW dnld failed @ %d, over max retry\n",
					    offset);
				ret = -1;
				goto err_dnld;
			}
			wiphy_err(priv->hw->wiphy,
				    "CRC indicated by the helper:\t"
				    "len = 0x%04X, txlen = %d\n", len, txlen);
			len &= ~BIT(0);
			/* Setting this to 0 to resend from same offset */
			txlen = 0;
		} else {
			i = 0;

			/* Set blocksize to transfer - checking for last
			   block */
			if (fw_len - offset < txlen)
				txlen = fw_len - offset;

			tx_blocks = (txlen + MWL_SDIO_BLOCK_SIZE - 1)
				    / MWL_SDIO_BLOCK_SIZE;

			/* Copy payload to buffer */
			memmove(fwbuf, &fw_data[offset], txlen);
		}

		ret = mwl_write_data_sync(priv, fwbuf, tx_blocks *
					      MWL_SDIO_BLOCK_SIZE,
					      card->ioport);
		if (ret) {
			wiphy_err(priv->hw->wiphy,
				    "FW download, write iomem (%d) failed @ %d\n",
				    i, offset);
			if (mwl_write_reg(priv, CONFIGURATION_REG, 0x04))
				wiphy_err(priv->hw->wiphy,
					    "write CFG reg failed\n");

			ret = -1;
			goto err_dnld;
		}

		offset += txlen;
	} while (true);

	sdio_release_host(card->func);

	wiphy_info(priv->hw->wiphy,
		    "FW download over, size %d bytes\n", offset);

	ret = mwl_check_fw_status(priv, MAX_FIRMWARE_POLL_TRIES);
	if (ret) {
		wiphy_err(priv->hw->wiphy,
			"FW status is not ready\n");
	}
	/* Enabling interrupt after firmware is ready.
	 * Otherwise there may be abnormal interrupt DN_LD_HOST_INT_MASK
	 */
	mwl_sdio_enable_int(priv);
	kfree(fwbuf);

	return ret;


err_dnld:
	sdio_release_host(card->func);
	kfree(fwbuf);
	return ret;
}

/*
 * This function reads multiple data from SDIO card memory.
 */
static int mwl_read_data_sync(struct mwl_priv *priv, u8 *buffer,
				  u32 len, u32 port, u8 claim)
{
	struct mwl_sdio_card *card = priv->intf;
	int ret;
	u8 blk_mode = (port & MWL_SDIO_BYTE_MODE_MASK) ? BYTE_MODE
		       : BLOCK_MODE;
	u32 blk_size = (blk_mode == BLOCK_MODE) ? MWL_SDIO_BLOCK_SIZE : 1;
	u32 blk_cnt = (blk_mode == BLOCK_MODE) ? (len / MWL_SDIO_BLOCK_SIZE)
			: len;
	u32 ioport = (port & MWL_SDIO_IO_PORT_MASK);

	if (claim)
		sdio_claim_host(card->func);

	ret = sdio_readsb(card->func, buffer, ioport, blk_cnt * blk_size);

	if (claim)
		sdio_release_host(card->func);

	return ret;
}

static char *mwl_sdio_event_strn(u16 event_id)
{
	int max_entries = 0;
	int curr_id = 0;

	static const struct {
		u16 id;
		char *id_string;
	} events[] = {
		{ SDEVENT_RADAR_DETECT, "SDEVENT_RADAR_DETECT" },
		{ SDEVENT_CHNL_SWITCH, "SDEVENT_CHNL_SWITCH" },
		{ SDEVENT_BA_WATCHDOG, "SDEVENT_BA_WATCHDOG" },
	};

	max_entries = ARRAY_SIZE(events);

	for (curr_id = 0; curr_id < max_entries; curr_id++)
		if ((event_id & 0x7fff) == events[curr_id].id)
			return events[curr_id].id_string;

	return "unknown";
}

static int mwl_sdio_event(struct mwl_priv *priv)
{
	struct ieee80211_hw *hw = priv->hw;
	struct mwl_hostevent *host_event = (struct mwl_hostevent *)(
		&priv->pcmd_buf[INTF_CMDHEADER_LEN(INTF_HEADER_LEN)]);
	u16	event_id = host_event->mac_event.event_id;	

	wiphy_info(hw->wiphy,
		"=> sd_event: %s\n", mwl_sdio_event_strn(event_id));

	mwl_hex_dump((u8 *)host_event, host_event->length);
	switch (event_id) {
	case SDEVENT_RADAR_DETECT:
		ieee80211_radar_detected(hw);
		break;
	case SDEVENT_CHNL_SWITCH:
		ieee80211_queue_work(hw,
			&priv->chnl_switch_handle);
		break;
	case SDEVENT_BA_WATCHDOG:
		ieee80211_queue_work(hw,
			&priv->watchdog_ba_handle);
		break;
	default:
		wiphy_info(hw->wiphy,"Unknown event, id=%04xh\n", event_id);
	}
	

	wiphy_info(hw->wiphy,
		"<= %s()\n", __func__);
	return 0;
}

static int mwl_sdio_complete_cmd(struct mwl_priv *priv)
{
	struct mwl_sdio_card *card = priv->intf;
	struct cmd_header *cmd_hdr;

	cmd_hdr = (struct cmd_header *)&priv->pcmd_buf[
			INTF_CMDHEADER_LEN(INTF_HEADER_LEN)];

	card->cmd_wait_q.status = 0;
	card->cmd_cond = true;
	wake_up_interruptible(&card->cmd_wait_q.wait);

	return 0;
}

/*
 * This function reads the interrupt status from card.
 */
static void mwl_sdio_interrupt_status(struct mwl_priv *priv)
{
	struct mwl_sdio_card *card = priv->intf;
	u8 sdio_ireg;
	unsigned long flags;

	if (mwl_read_data_sync(priv, card->mp_regs,
				   card->reg->max_mp_regs,
				   REG_PORT | MWL_SDIO_BYTE_MODE_MASK, 0)) {
		wiphy_err(priv->hw->wiphy, "read mp_regs failed\n");
		return;
	}

	sdio_ireg = card->mp_regs[card->reg->host_int_status_reg];
	if (sdio_ireg) {
		/*
		 * DN_LD_HOST_INT_STATUS and/or UP_LD_HOST_INT_STATUS
		 * For SDIO new mode CMD port interrupts
		 *	DN_LD_CMD_PORT_HOST_INT_STATUS and/or
		 *	UP_LD_CMD_PORT_HOST_INT_STATUS
		 * Clear the interrupt status register
		 */
		spin_lock_irqsave(&card->int_lock, flags);
		card->int_status |= sdio_ireg;
		spin_unlock_irqrestore(&card->int_lock, flags);
	}
	return;
}

/*
 * This function sends a data buffer to the card.
 */
static int mwl_sdio_card_to_host(struct mwl_priv *priv, u32 *type,
				     u8 *buffer, u32 npayload, u32 ioport)
{
	int ret;

	if (!buffer) {
		wiphy_err(priv->hw->wiphy,
			    "%s: buffer is NULL\n", __func__);
		return -1;
	}

	ret = mwl_read_data_sync(priv, buffer, npayload, ioport, 1);
	if (type != NULL)
		*type = le16_to_cpu(*(__le16 *)(buffer + 2));

	return ret;
}


/*
 * This function gets the read port.
 *
 * If control port bit is set in MP read bitmap, the control port
 * is returned, otherwise the current read port is returned and
 * the value is increased (provided it does not reach the maximum
 * limit, in which case it is reset to 1)
 */
static int mwl_get_rd_port(struct mwl_priv *priv, u8 *port)
{
	struct mwl_sdio_card *card = priv->intf;
	const struct mwl_sdio_card_reg *reg = card->reg;
	u32 rd_bitmap = card->mp_rd_bitmap;

	if (!(rd_bitmap & reg->data_port_mask))
		return -1;


	if (!(card->mp_rd_bitmap & (1 << card->curr_rd_port)))
		return -1;

	/* We are now handling the SDIO data ports */
	card->mp_rd_bitmap &= (u32)(~(1 << card->curr_rd_port));
	*port = card->curr_rd_port;

	/*
	 * card->curr_rd_port is 0 ~ 31 (= start_rd_port ~ card->max_ports-1)
	 */
	if (++card->curr_rd_port == card->max_ports)
		card->curr_rd_port = reg->start_rd_port;

	return 0;
}

/*
 * This function decode sdio aggreation pkt.
 *
 * Based on the the data block size and pkt_len,
 * skb data will be decoded to few packets.
 */
static void mwl_deaggr_sdio_pkt(struct mwl_priv *priv,
				    struct sk_buff *skb)
{
	struct mwl_sdio_card *card = priv->intf;
	u32 total_pkt_len, pkt_len;
	struct sk_buff *skb_deaggr;
	u32 pkt_type;
	u16 blk_size;
	u8 blk_num;
	u8 *data;

	data = skb->data;
	total_pkt_len = skb->len;

	while (total_pkt_len >= (SDIO_HEADER_OFFSET + INTF_HEADER_LEN)) {
		if (total_pkt_len < card->sdio_rx_block_size)
			break;
		blk_num = *(data + BLOCK_NUMBER_OFFSET);
		blk_size = card->sdio_rx_block_size * blk_num;
		if (blk_size > total_pkt_len) {
			wiphy_err(priv->hw->wiphy,
				"%s: error in blk_size,\t"
				"blk_num=%d, blk_size=%d, total_pkt_len=%d\n",
				__func__, blk_num, blk_size, total_pkt_len);
			break;
		}
		pkt_len = le16_to_cpu(*(__le16 *)(data + SDIO_HEADER_OFFSET));
		pkt_type = le16_to_cpu(*(__le16 *)(data + SDIO_HEADER_OFFSET +
					 2));
		if ((pkt_len + SDIO_HEADER_OFFSET) > blk_size) {
			wiphy_err(priv->hw->wiphy,
				"%s: error in pkt_len,\t"
				"pkt_len=%d, blk_size=%d\n",
				__func__, pkt_len, blk_size);
			break;
		}
		skb_deaggr = mwl_alloc_dma_align_buf(pkt_len,
							 GFP_KERNEL | GFP_DMA);
		if (!skb_deaggr)
			break;
		skb_put(skb_deaggr, pkt_len);
		memcpy(skb_deaggr->data, data + SDIO_HEADER_OFFSET, pkt_len);
		skb_pull(skb_deaggr, INTF_HEADER_LEN);

		mwl_handle_rx_packet(priv, skb_deaggr);
		data += blk_size;
		total_pkt_len -= blk_size;
	}
}


/*
 * This function decodes a received packet.
 *
 * Based on the type, the packet is treated as either a data, or
 * a command response, or an event, and the correct handler
 * function is invoked.
 */
static int mwl_decode_rx_packet(struct mwl_priv *priv,
				    struct sk_buff *skb, u32 upld_typ)
{
	struct mwl_sdio_card *card = priv->intf;
	__le16 *curr_ptr = (__le16 *)skb->data;
	u16 pkt_len = le16_to_cpu(*curr_ptr);
	struct mwl_rxinfo *rx_info;

	switch (upld_typ) {
	case MWL_TYPE_AGGR_DATA:
		rx_info = MWL_SKB_RXCB(skb);
		rx_info->buf_type = MWL_TYPE_AGGR_DATA;
		break;
	case MWL_TYPE_DATA:
		skb_trim(skb, pkt_len);
		/* Remove the header (len:2 + type:2) */
		skb_pull(skb, INTF_HEADER_LEN);

		break;
	case MWL_TYPE_MGMT:
		skb_trim(skb, pkt_len);
		/* Remove the header (len:2 + type:2) */
		skb_pull(skb, INTF_HEADER_LEN);
		rx_info = MWL_SKB_RXCB(skb);
		rx_info->buf_type = MWL_TYPE_MGMT;
		break;
	case MWL_TYPE_BEACON:
		skb_trim(skb, pkt_len);
		/* Remove the header (len:2 + type:2) */
		skb_pull(skb, INTF_HEADER_LEN);
		rx_info = MWL_SKB_RXCB(skb);
		rx_info->buf_type = MWL_TYPE_BEACON;
		break;
	default:
		wiphy_err(priv->hw->wiphy,
			    "unknown upload type %#x\n", upld_typ);
		goto error;
		break;
	}
	skb_queue_tail(&card->rx_data_q, skb);
	atomic_inc(&card->rx_pending);
	card->data_received = true;

error:
	return 0;
}


/*
 * This function transfers received packets from card to driver, performing
 * aggregation if required.
 *
 * For data received on control port, or if aggregation is disabled, the
 * received buffers are uploaded as separate packets. However, if aggregation
 * is enabled and required, the buffers are copied onto an aggregation buffer,
 * provided there is space left, processed and finally uploaded.
 */
static int mwl_sdio_card_to_host_mp_aggr(struct mwl_priv *priv,
					     u16 rx_len, u8 port)
{
	struct mwl_sdio_card *card = priv->intf;
	s32 f_do_rx_aggr = 0;
	s32 f_do_rx_cur = 0;
	s32 f_aggr_cur = 0;
	s32 f_post_aggr_cur = 0;
	struct sk_buff *skb_deaggr;
	struct sk_buff *skb = NULL;
	u32 pkt_len, pkt_type, mport, pind;
	u8 *curr_ptr;
	int i;
	u32 port_count;

	if (!card->mpa_rx.enabled) {
		wiphy_err(priv->hw->wiphy,
			    "info: %s: rx aggregation disabled\n",
			    __func__);

		f_do_rx_cur = 1;
		goto rx_curr_single;
	}

	if (card->mp_rd_bitmap &
		card->reg->data_port_mask) {
		/* Some more data RX pending */

		if (MP_RX_AGGR_IN_PROGRESS(card)) {
			if (MP_RX_AGGR_BUF_HAS_ROOM(card, rx_len)) {
				f_aggr_cur = 1;
			} else {
				/* No room in Aggr buf, do rx aggr now */
				f_do_rx_aggr = 1;
				f_post_aggr_cur = 1;
			}
		} else {
			/* Rx aggr not in progress */
			f_aggr_cur = 1;
		}

	} else {
		/* No more data RX pending */
		if (MP_RX_AGGR_IN_PROGRESS(card)) {
			f_do_rx_aggr = 1;
			if (MP_RX_AGGR_BUF_HAS_ROOM(card, rx_len))
				f_aggr_cur = 1;
			else
				/* No room in Aggr buf, do rx aggr now */
				f_do_rx_cur = 1;
		} else {
			f_do_rx_cur = 1;
		}
	}

	if (f_aggr_cur != 0) {
		/* Curr pkt can be aggregated */
		mp_rx_aggr_setup(card, rx_len, port);

		if (MP_RX_AGGR_PKT_LIMIT_REACHED(card) ||
		    mp_rx_aggr_port_limit_reached(card)) {
			/* wiphy_err(priv->hw->wiphy,
				    "info: %s: aggregated packet\t"
				    "limit reached\n", __func__);*/
			/* No more pkts allowed in Aggr buf, rx it */
			f_do_rx_aggr = 1;
		}
	}

	if (f_do_rx_aggr) {
		/* do aggr RX now */
		for (i = 0, port_count = 0; i < card->max_ports; i++)
			if (card->mpa_rx.ports & BIT(i))
				port_count++;
			/* Reading data from "start_port + 0" to "start_port +
			 * port_count -1", so decrease the count by 1
			 */
		port_count--;
		mport = (card->ioport | SDIO_MPA_ADDR_BASE |
				 (port_count << 8)) + card->mpa_rx.start_port;

		if (mwl_read_data_sync(priv, card->mpa_rx.buf,
					   card->mpa_rx.buf_len, mport, 1))
			goto error;


		/*
		* Get the data from bus (in mpa_rx.buf)
		* => put to the buffer array packet by packet
		*/
		curr_ptr = card->mpa_rx.buf;
		for (pind = 0; pind < card->mpa_rx.pkt_cnt; pind++) {
			u32 *len_arr = card->mpa_rx.len_arr;

			/* get curr PKT len & type */
			pkt_len = le16_to_cpu(*(__le16 *) &curr_ptr[0]);
			pkt_type = le16_to_cpu(*(__le16 *) &curr_ptr[2]);

			/* copy pkt to deaggr buf */
			skb_deaggr = mwl_alloc_dma_align_buf(len_arr[pind],
								 GFP_KERNEL |
								 GFP_DMA);
			if (!skb_deaggr) {
				wiphy_err(priv->hw->wiphy,
					"skb allocation failure\t"\
					"drop pkt len=%d type=%d\n",
					pkt_len, pkt_type);
				curr_ptr += len_arr[pind];
				continue;
			}

			skb_put(skb_deaggr, len_arr[pind]);

			if (((pkt_type == MWL_TYPE_DATA) ||
			     (pkt_type == MWL_TYPE_AGGR_DATA &&
				card->sdio_rx_aggr_enable) ||
			     (pkt_type == MWL_TYPE_MGMT) ||
			     (pkt_type == MWL_TYPE_BEACON)
			     ) &&
			    (pkt_len <= len_arr[pind])) {

				memcpy(skb_deaggr->data, curr_ptr, pkt_len);

				skb_trim(skb_deaggr, pkt_len);
				/* Process de-aggr packet */
				mwl_decode_rx_packet(priv, skb_deaggr,
							 pkt_type);
			} else {
				wiphy_err(priv->hw->wiphy,
					    "drop wrong aggr pkt:\t"
					    "sdio_single_port_rx_aggr=%d\t"
					    "type=%d len=%d max_len=%d\n",
					    card->sdio_rx_aggr_enable,
					    pkt_type, pkt_len, len_arr[pind]);
				dev_kfree_skb_any(skb_deaggr);
			}
			curr_ptr += len_arr[pind];
		}
		MP_RX_AGGR_BUF_RESET(card);
	}

rx_curr_single:
	if (f_do_rx_cur) {
		skb = mwl_alloc_dma_align_buf(rx_len, GFP_KERNEL | GFP_DMA);
		if (!skb) {
			wiphy_err(priv->hw->wiphy,
				    "single skb allocated fail,\t"
				    "drop pkt port=%d len=%d\n", port, rx_len);
			if (mwl_sdio_card_to_host(priv, &pkt_type,
				card->mpa_rx.buf, rx_len, card->ioport + port))
				goto error;

			return 0;
		}
		skb_put(skb, rx_len);

		if (mwl_sdio_card_to_host(priv, &pkt_type, skb->data, skb->len,
					      card->ioport + port))
			goto error;
		mwl_decode_rx_packet(priv, skb, pkt_type);
	}
	if (f_post_aggr_cur) {
		wiphy_err(priv->hw->wiphy,
			    "info: current packet aggregation\n");
		/* Curr pkt can be aggregated */
		mp_rx_aggr_setup(card, rx_len, port);
	}

	return 0;

error:
	if (MP_RX_AGGR_IN_PROGRESS(card))
		MP_RX_AGGR_BUF_RESET(card);

	if (f_do_rx_cur && skb)
		/* Single transfer pending. Free curr buff also */
		dev_kfree_skb_any(skb);

	return -1;
}




/*
 * This function checks the current interrupt status.
 *
 * The following interrupts are checked and handled by this function -
 *      - Data sent
 *      - Command sent
 *      - Packets received
 *
 * Since the firmware does not generate download ready interrupt if the
 * port updated is command port only, command sent interrupt checking
 * should be done manually, and for every SDIO interrupt.
 *
 * In case of Rx packets received, the packets are uploaded from card to
 * host and processed accordingly.
 */
static int mwl_sdio_process_int_status(struct mwl_priv *priv)
{
	struct mwl_sdio_card *card = priv->intf;
	const struct mwl_sdio_card_reg *reg = card->reg;
	struct ieee80211_hw *hw = priv->hw;
	int ret = 0;
	u8 sdio_ireg;
	u8 port = CTRL_PORT;
	u32 len_reg_l, len_reg_u;
	u32 rx_blocks;
	u16 rx_len;
	unsigned long flags;
	u32 bitmap;
	u8 cr;
	__le16 *pBuf = (__le16 *)priv->pcmd_buf;

	spin_lock_irqsave(&card->int_lock, flags);
	sdio_ireg = card->int_status;
	//card->int_status = 0;
	spin_unlock_irqrestore(&card->int_lock, flags);

	if (!sdio_ireg)
		return ret;
	/* Following interrupt is only for SDIO new mode */
	if (sdio_ireg & DN_LD_CMD_PORT_HOST_INT_STATUS) {
		//card->cmd_sent = false;
        card->cmd_wait_q.status = 0;
	    wake_up_interruptible(&card->cmd_wait_q.wait);
    }

	/* Command Response / Event is back */
	if (sdio_ireg & UP_LD_CMD_PORT_HOST_INT_STATUS) {
		struct cmd_header *cmd_hdr = (struct cmd_header *)
			&priv->pcmd_buf[INTF_CMDHEADER_LEN(INTF_HEADER_LEN)];
	    card->int_status &= ~UP_LD_CMD_PORT_HOST_INT_STATUS;
		/* read the len of control packet */
		rx_len = card->mp_regs[reg->cmd_rd_len_1] << 8;
		rx_len |= (u16)card->mp_regs[reg->cmd_rd_len_0];
		rx_blocks = DIV_ROUND_UP(rx_len, MWL_SDIO_BLOCK_SIZE);
		if ((rx_blocks * MWL_SDIO_BLOCK_SIZE) >
		     CMD_BUF_SIZE)
			return -1;
		rx_len = (u16) (rx_blocks * MWL_SDIO_BLOCK_SIZE);

		ret = mwl_sdio_card_to_host(priv, NULL, (u8 *)priv->pcmd_buf,
			rx_len, card->ioport | CMD_PORT_SLCT);
		if (ret != 0) {
			wiphy_err(hw->wiphy,
			    "%s: failed to card_to_host, (%d)", __func__, ret);
			goto term_cmd;
		}

		/*
		* If command has been sent & cmd_code = 0x8xxx => It's cmd_resp
		* Otherwise, it's event (new added)
		*/
		if ((card->cmd_cond == false) &&
		    ((le16_to_cpu(cmd_hdr->command) & ~HOSTCMD_RESP_BIT) == card->cmd_id) &&
		    (pBuf[1] ==  cpu_to_le16(MWL_TYPE_CMD))) {
			card->cmd_id = 0;
			mwl_sdio_complete_cmd(priv);
		} else if (pBuf[1] ==  cpu_to_le16(MWL_TYPE_EVENT))
			mwl_sdio_event(priv);
	}

	/* Tx-Done interrut */
	if (sdio_ireg & DN_LD_HOST_INT_STATUS) {
		bitmap = (u32) card->mp_regs[reg->wr_bitmap_l];
		bitmap |= ((u32) card->mp_regs[reg->wr_bitmap_u]) << 8;
		bitmap |= ((u32) card->mp_regs[reg->wr_bitmap_1l]) << 16;
		bitmap |= ((u32) card->mp_regs[reg->wr_bitmap_1u]) << 24;

	    card->int_status &= ~DN_LD_HOST_INT_STATUS;
		card->mp_wr_bitmap = bitmap;

		if (card->data_sent &&
		    (card->mp_wr_bitmap & card->mp_data_port_mask)) {
#if 0
			wiphy_err(hw->wiphy,
				    "error:  <--- Tx DONE Interrupt bmp=0x%x --->\n", card->mp_wr_bitmap);
#endif
			card->data_sent = false;
		}


		if (!priv->is_tx_done_schedule) {
			priv->is_tx_done_schedule = true;
			queue_work(card->tx_workq, &card->tx_work);
		}
	}

	/* Rx process */
	if (sdio_ireg & UP_LD_HOST_INT_STATUS) {
		bitmap = (u32) card->mp_regs[reg->rd_bitmap_l];
		bitmap |= ((u32) card->mp_regs[reg->rd_bitmap_u]) << 8;
		bitmap |= ((u32) card->mp_regs[reg->rd_bitmap_1l]) << 16;
		bitmap |= ((u32) card->mp_regs[reg->rd_bitmap_1u]) << 24;
		card->mp_rd_bitmap = bitmap;

	    card->int_status &= ~UP_LD_HOST_INT_STATUS;
		while (true) {
			ret = mwl_get_rd_port(priv, &port);
			if (ret)
				break;

			len_reg_l = reg->rd_len_p0_l + (port << 1);
			len_reg_u = reg->rd_len_p0_u + (port << 1);
			rx_len = ((u16) card->mp_regs[len_reg_u]) << 8;
			rx_len |= (u16) card->mp_regs[len_reg_l];

			rx_blocks =
				(rx_len + MWL_SDIO_BLOCK_SIZE -
				 1) / MWL_SDIO_BLOCK_SIZE;

			if (card->mpa_rx.enabled &&
			     ((rx_blocks * MWL_SDIO_BLOCK_SIZE) >
			      card->mpa_rx.buf_size)) {
				wiphy_err(hw->wiphy,
					    "invalid rx_len=%d\n",
					    rx_len);
				return -1;
			}

			rx_len = (u16) (rx_blocks * MWL_SDIO_BLOCK_SIZE);
			if (mwl_sdio_card_to_host_mp_aggr(priv, rx_len,
							      port)) {
				wiphy_err(hw->wiphy,
					    "card_to_host_mpa failed: int status=%#x\n",
					    sdio_ireg);
				goto term_cmd;
			}
		}

		/* Indicate the received packets (card->rx_data_q)to MAC80211 */
		if (!priv->is_rx_schedule) {
			priv->is_rx_schedule = true;
			tasklet_schedule(&priv->rx_task);
		}
	}
	return 0;

term_cmd:
	/* terminate cmd */
	if (mwl_read_reg(priv, CONFIGURATION_REG, &cr))
		wiphy_err(hw->wiphy, "read CFG reg failed\n");
	else
		wiphy_err(hw->wiphy,
			    "info: CFG reg val = %d\n", cr);

	if (mwl_write_reg(priv, CONFIGURATION_REG, (cr | 0x04)))
		wiphy_err(hw->wiphy,
			    "write CFG reg failed\n");
	else
		wiphy_err(hw->wiphy, "info: write success\n");

	if (mwl_read_reg(priv, CONFIGURATION_REG, &cr))
		wiphy_err(hw->wiphy,
			    "read CFG reg failed\n");
	else
		wiphy_err(hw->wiphy,
			    "info: CFG reg val =%x\n", cr);


	return -1;
}


/*
static char *mwl_pktstrn(char* pkt)
{
	static char msg[80];
	char pkthd = pkt[0];
	char pkt_type = (pkthd&0x0c)>>2;
	char pkt_subtype = (pkthd&0xf0) >> 4;

	memset(msg, 0, sizeof(msg));
	if (pkt_type == 0) {    //mgmt pkt
		switch (pkt_subtype) {
		case 0x0:
			strcpy(msg, "assoc_req");
			break;
		case 0x01:
			strcpy(msg, "assoc_resp");
			break;
		case 0x2:
			strcpy(msg, "reassoc_req");
			break;
		case 0x3:
			strcpy(msg, "reassoc_resp");
			break;
		case 0x4:
			strcpy(msg, "probe_req");
			break;
		case 0x5:
			strcpy(msg, "prob_resp");
			break;
		case 0x8:
			strcpy(msg, "beacon");
			break;
		case 0xa:
			strcpy(msg, "disassoc");
			break;
		case 0xb:
			strcpy(msg, "auth");
			break;
		case 0xc:
			strcpy(msg, "deauth");
			break;
		case 0xd:
			strcpy(msg, "action");
			break;
		default:
			break;
		}
	}
	
	if (pkt_type == 1) {    //ctrl pkt
		switch (pkt_subtype) {
		case 0xb:
			strcpy(msg, "rts");
			break;
		case 0xc:
			strcpy(msg, "cts");
			break;
		case 0xd:
			strcpy(msg, "ack");
			break;
		case 0x8:
			strcpy(msg, "BAR");
			break;
		case 0x9:
			strcpy(msg, "BA");
			break;
		default:
			break;
		}
	}
	
	if (pkt_type == 2) {    //data pkt
		char* pllc;
		switch (pkt_subtype) {
		case 0x8:
			strcpy(msg, "QOS");
			break;
		case 0x00:
			strcpy(msg, "data");
			break;
		case 0x4:
			strcpy(msg, "null_data");
			return msg;
		default:
			strcpy(msg, "data");
			return msg;
		}
		if (pkt_subtype == 0x00) {
			pllc = &pkt[24];
		} else if (pkt_subtype == 0x8) {
			pllc = &pkt[26];
		}
		
		if ((pllc[0] == '\xaa') && (pllc[1]='\xaa') && (pllc[2]=='\x03')) {
			if ((pllc[6]=='\x08')&&(pllc[7]=='\x06')) {
				strcat(msg, " - ARP");
			}
			if ((pllc[6]=='\x08') && (pllc[7] == '\x00')) {
				char *pip = &pllc[8];
				strcat(msg, " - IP");
				if (pip[9] == '\x01') {
					char *picmp = &pip[20];
					strcat(msg, " - ICMP");
					if (picmp[0] == '\x00') {
						strcat(msg, " - echo_rply");
					}
					if (picmp[0] == '\x08') {
						strcat(msg, " - echo_req");
					}
				}
				if (pip[9] == '\x11') {
					//char *pudp = &pip[20];
					strcat(msg, " - UDP");
				}
			}
		}
	}

	return msg;
}
*/


/*
  Packet format (sdio interface):
  [len:2][type:2][mwl_rx_desc:44][mwl_dma_data:32][payload wo 802.11 header]
*/
void mwl_handle_rx_packet(struct mwl_priv *priv, struct sk_buff *skb)
{
	struct ieee80211_hw *hw = priv->hw;
	struct mwl_rx_desc *pdesc;
	struct mwl_dma_data *dma;
	struct sk_buff *prx_skb = skb;
	int pkt_len;
	struct ieee80211_rx_status status;
	struct mwl_vif *mwl_vif = NULL;
	struct ieee80211_hdr *wh;
	struct mwl_rx_event_data *rx_evnt;

	pdesc = (struct mwl_rx_desc *)prx_skb->data;
	pkt_len = le16_to_cpu(pdesc->pkt_len);

	/* => todo:
	// Save the rate info back to card
	//card->rate_info = pdesc->rate;
	//=> rateinfo--
	*/
	if (pdesc->payldType == RX_PAYLOAD_TYPE_EVENT_INFO) {
		skb_pull(prx_skb, sizeof(struct mwl_rx_desc));
		rx_evnt = (struct mwl_rx_event_data *)prx_skb->data;
		mwl_handle_rx_event(hw, rx_evnt);
		dev_kfree_skb_any(prx_skb);
		return;
	}

	if ((pdesc->channel != hw->conf.chandef.chan->hw_value) &&
		!(priv->roc.tmr_running && priv->roc.in_progress && 
			(pdesc->channel == priv->roc.chan))) {
		dev_kfree_skb_any(prx_skb);
//		wiphy_debug(priv->hw->wiphy,
//			"<= %s(), not accepted channel (%d, %d)\n", __func__,
//			pdesc->channel, hw->conf.chandef.chan->hw_value);
		return;
	}

	mwl_rx_prepare_status(pdesc, &status);
	priv->noise = -pdesc->noise_floor;

	skb_pull(prx_skb, sizeof(struct mwl_rx_desc));
	dma = (struct mwl_dma_data *)prx_skb->data;
	wh = &dma->wh;

	if (ieee80211_has_protected(wh->frame_control)) {
		/* Check if hw crypto has been enabled for
		 * this bss. If yes, set the status flags
		 * accordingly
		 */
		if (ieee80211_has_tods(wh->frame_control))
			mwl_vif = mwl_rx_find_vif_bss(priv, wh->addr1);
		else
			mwl_vif = mwl_rx_find_vif_bss(priv, wh->addr2);

		if  ((mwl_vif && mwl_vif->is_hw_crypto_enabled) ||
		     is_multicast_ether_addr(wh->addr1) ||
		     (ieee80211_is_mgmt(wh->frame_control) &&
		     ieee80211_has_protected(wh->frame_control) &&
		     !is_multicast_ether_addr(wh->addr1))) {
			/* When MMIC ERROR is encountered
			 * by the firmware, payload is
			 * dropped and only 32 bytes of
			 * mwlwifi Firmware header is sent
			 * to the host.
			 *
			 * We need to add four bytes of
			 * key information.  In it
			 * MAC80211 expects keyidx set to
			 * 0 for triggering Counter
			 * Measure of MMIC failure.
			 */
			if (status.flag & RX_FLAG_MMIC_ERROR) {
				memset((void *)&dma->data, 0, 4);
				pkt_len += 4;
			}

			if (!ieee80211_is_auth(wh->frame_control))
				status.flag |= RX_FLAG_IV_STRIPPED |
					       RX_FLAG_DECRYPTED |
					       RX_FLAG_MMIC_STRIPPED;
		}
	}

	/*
	    Remove the DMA header (dma->fwlen)
	*/
	mwl_rx_remove_dma_header(prx_skb, pdesc->qos_ctrl);

	/* Update the pointer of wifi header,
		which may be different after mwl_rx_remove_dma_header()
	*/
	wh = (struct ieee80211_hdr *)prx_skb->data;
	if (ieee80211_is_mgmt(wh->frame_control)) {
		struct ieee80211_mgmt *mgmt;
		__le16 capab;

		mgmt = (struct ieee80211_mgmt *)prx_skb->data;

		if (unlikely(ieee80211_is_action(wh->frame_control) &&
			mgmt->u.action.category == WLAN_CATEGORY_BACK &&
			mgmt->u.action.u.addba_resp.action_code ==
				WLAN_ACTION_ADDBA_RESP)) {
			capab = mgmt->u.action.u.addba_resp.capab;
			if (le16_to_cpu(capab) & 1)
				mwl_rx_enable_sta_amsdu(priv, mgmt->sa);
		}
	}
#if 0 //def CONFIG_MAC80211_MESH
		if (ieee80211_is_data_qos(wh->frame_control) &&
		    ieee80211_has_a4(wh->frame_control)) {
			u8 *qc = ieee80211_get_qos_ctl(wh);

			if (*qc & IEEE80211_QOS_CTL_A_MSDU_PRESENT)
				if (mwl_rx_process_mesh_amsdu(priv, prx_skb,
							      &status))
					return;
		}
#endif
	memcpy(IEEE80211_SKB_RXCB(prx_skb), &status, sizeof(status));

	/* Packet to indicate => Will indicate AMPDU/AMSDU packets */
	mwl_rx_upload_pkt(hw, prx_skb);

	return;
}


static void mwl_sdio_rx_recv(unsigned long data)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)data;
	struct mwl_priv *priv = hw->priv;
	struct mwl_sdio_card *card = priv->intf;
	struct mwl_rxinfo *rx_info;
	struct sk_buff *prx_skb = NULL;
	int work_done = 0;

	while (work_done < priv->recv_limit) {
		prx_skb = skb_dequeue(&card->rx_data_q);
		if (prx_skb == NULL) {
			break;
		}
		rx_info = MWL_SKB_RXCB(prx_skb);

		if (rx_info->buf_type == MWL_TYPE_AGGR_DATA)
			mwl_deaggr_sdio_pkt(priv, prx_skb);
		else
			mwl_handle_rx_packet(priv, prx_skb);
		work_done++;
	}

	priv->is_rx_schedule = false;
	return;
}

/*
 * Packet send completion callback handler.
 *
 * It either frees the buffer directly or forwards it to another
 * completion callback which checks conditions, updates statistics,
 * wakes up stalled traffic queue if required, and then frees the buffer.
 */
static int mwl_write_data_complete(struct mwl_priv *priv,
				struct sk_buff *skb)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)priv->hw;
	struct mwl_tx_ctrl *tx_ctrl;
	struct ieee80211_tx_info *info;
	struct sk_buff_head *amsdu_pkts;
	struct mwl_dma_data *dma_data;
	struct ieee80211_hdr *wh;
	u8 *data = skb->data;
	u32 rate;

	if (skb == NULL)
		return 0;
	dma_data = (struct mwl_dma_data *)
		&data[INTF_HEADER_LEN + sizeof(struct mwl_tx_desc)];
	wh = &dma_data->wh;
	info = IEEE80211_SKB_CB(skb);

	tx_ctrl = (struct mwl_tx_ctrl *)&info->status;

	if (ieee80211_is_data(wh->frame_control) ||
		ieee80211_is_data_qos(wh->frame_control)) {
		rate = TX_COMP_RATE_FOR_DATA;
		tx_ctrl = (struct mwl_tx_ctrl *)&info->status;
		amsdu_pkts = (struct sk_buff_head *)
					tx_ctrl->amsdu_pkts;
		if (amsdu_pkts) {
			mwl_tx_ack_amsdu_pkts(hw, rate, amsdu_pkts);
			dev_kfree_skb_any(skb);
			skb = NULL;
		} else
			mwl_tx_prepare_info(hw, rate, info);
	 } else
			mwl_tx_prepare_info(hw, 0, info);

	if (skb != NULL) {
		info->flags &= ~IEEE80211_TX_CTL_AMPDU;
		info->flags |= IEEE80211_TX_STAT_ACK;

		if (ieee80211_is_data(wh->frame_control) ||
			ieee80211_is_data_qos(wh->frame_control)) {
//			wiphy_err(hw->wiphy, "fr_data_skb=%p\n", skb);
		}

		ieee80211_tx_status(hw, skb);
	}

	return 0;
}

static void mwl_sdio_tx_workq(struct work_struct *work)
{
	struct mwl_sdio_card *card = container_of(work,
			struct mwl_sdio_card, tx_work);
	struct mwl_priv *priv = card->priv;
	struct ieee80211_hw *hw = (struct ieee80211_hw *)priv->hw;

	priv->is_tx_done_schedule = false;
	mwl_tx_skbs((unsigned long)hw);
	mwl_sdio_flush_amsdu_no_lock((unsigned long)hw);

}


/*
 * This function aggregates transmission buffers in driver and downloads
 * the aggregated packet to card.
 *
 * The individual packets are aggregated by copying into an aggregation
 * buffer and then downloaded to the card. Previous unsent packets in the
 * aggregation buffer are pre-copied first before new packets are added.
 * Aggregation is done till there is space left in the aggregation buffer,
 * or till new packets are available.
 *
 * The function will only download the packet to the card when aggregation
 * stops, otherwise it will just aggregate the packet in aggregation buffer
 * and return.
 */
static int mwl_host_to_card_mp_aggr(struct mwl_priv *priv,
					u8 *payload, u32 pkt_len, u32 port,
					u32 next_pkt_len)
{
	struct mwl_sdio_card *card = priv->intf;
	int ret = 0;
	s32 f_send_aggr_buf = 0;
	s32 f_send_cur_buf = 0;
	s32 f_precopy_cur_buf = 0;
	s32 f_postcopy_cur_buf = 0;
	u32 mport;
	u32 port_count;
	int i;

//	wiphy_err(priv->hw->wiphy, "%s() called\n", __FUNCTION__);

	if (next_pkt_len) {
		/* More pkt in TX queue */
		if (MP_TX_AGGR_IN_PROGRESS(card)) {
			if (MP_TX_AGGR_BUF_HAS_ROOM(card, pkt_len)) {
				f_precopy_cur_buf = 1;

				if (((card->mp_wr_bitmap &
					(1 << card->curr_wr_port)) == 0) ||
				    !MP_TX_AGGR_BUF_HAS_ROOM(card,
					pkt_len + next_pkt_len)) {
					f_send_aggr_buf = 1;
				}
			} else {
				/* No room in Aggr buf, send it */
				f_send_aggr_buf = 1;

				if ((card->mp_wr_bitmap &
					(1 << card->curr_wr_port)) == 0)
					f_send_cur_buf = 1;
				else
					f_postcopy_cur_buf = 1;
			}
		} else {
			if (MP_TX_AGGR_BUF_HAS_ROOM(card, pkt_len) &&
			    (card->mp_wr_bitmap & (1 << card->curr_wr_port))) {
				f_precopy_cur_buf = 1;
			} else {
				f_send_cur_buf = 1;
			}
		}
	} else {
		/* Last pkt in TX queue */
		if (MP_TX_AGGR_IN_PROGRESS(card)) {
			/* some packs in Aggr buf already */
			f_send_aggr_buf = 1;

			if (MP_TX_AGGR_BUF_HAS_ROOM(card, pkt_len)) {
				f_precopy_cur_buf = 1;
			} else {
				/* No room in Aggr buf, send it */
				f_send_cur_buf = 1;
			}
		} else {
			f_send_cur_buf = 1;
		}
	}

	if (f_precopy_cur_buf) {
		MP_TX_AGGR_BUF_PUT(card, payload, pkt_len, port);

		if (MP_TX_AGGR_PKT_LIMIT_REACHED(card) ||
		    mp_tx_aggr_port_limit_reached(card))
			/* No more pkts allowed in Aggr buf, send it */
			f_send_aggr_buf = 1;
	}

	if (f_send_aggr_buf) {
		for (i = 0, port_count = 0; i < card->max_ports; i++)
				if (card->mpa_tx.ports & BIT(i))
					port_count++;

		/* Writing data from "start_port + 0" to "start_port +
		 * port_count -1", so decrease the count by 1
		 */
		port_count--;
		mport = (card->ioport | SDIO_MPA_ADDR_BASE | (port_count << 8))
			+ card->mpa_tx.start_port;
		ret = mwl_write_data_to_card(priv, card->mpa_tx.buf,
						 card->mpa_tx.buf_len, mport);

		MP_TX_AGGR_BUF_RESET(card);
	}

	if (f_send_cur_buf != 0) {
		ret = mwl_write_data_to_card(priv, payload, pkt_len,
						 card->ioport + port);
	}

	if (f_postcopy_cur_buf != 0)
		MP_TX_AGGR_BUF_PUT(card, payload, pkt_len, port);

	return ret;
}

/*
 * This function gets the write port for data.
 *
 * The current write port is returned if available and the value is
 * increased (provided it does not reach the maximum limit, in which
 * case it is reset to 1)
 */
static int mwl_get_wr_port_data(struct mwl_priv *priv, u32 *port)
{
	struct mwl_sdio_card *card = priv->intf;
	const struct mwl_sdio_card_reg *reg = card->reg;
	u32 wr_bitmap = card->mp_wr_bitmap;

	if (!(wr_bitmap & card->mp_data_port_mask)) {
		card->data_sent = true;
		return -EBUSY;
	}

	if (card->mp_wr_bitmap & (1 << card->curr_wr_port)) {
		card->mp_wr_bitmap &= (u32) (~(1 << card->curr_wr_port));
		*port = card->curr_wr_port;
		if (++card->curr_wr_port == card->mp_end_port)
			card->curr_wr_port = reg->start_wr_port;
	} else {
		card->data_sent = true;
		return -EBUSY;
	}

	return 0;
}


static bool mwl_sdio_is_tx_available(struct mwl_priv *priv, int desc_num)
{
	struct mwl_sdio_card *card = priv->intf;
	u32 wr_bitmap = card->mp_wr_bitmap;

	if ((wr_bitmap & card->mp_data_port_mask) == 0)
		return false;

	if ((card->mp_wr_bitmap & (1 << card->curr_wr_port)) == 0)
		return false;

	return true;
}

/*
 * Adds TxPD to AMSDU header.
 *
 * Each AMSDU packet will contain one TxPD at the beginning,
 * followed by multiple AMSDU subframes.
 */
static void
mwl_process_txdesc(struct mwl_priv *priv,
			    struct sk_buff *skb)
{
	struct mwl_tx_desc *tx_desc;
	struct mwl_tx_ctrl *tx_ctrl;
	struct ieee80211_tx_info *tx_info;
	u8 *ptr;
	int headroom = INTF_HEADER_LEN;

	tx_info = IEEE80211_SKB_CB(skb);
	tx_ctrl = (struct mwl_tx_ctrl *)&IEEE80211_SKB_CB(skb)->status;
	ptr = (u8 *)skb->data;

	skb_push(skb, sizeof(struct mwl_tx_desc));
	tx_desc = (struct mwl_tx_desc *) skb->data;
	memset(tx_desc, 0, sizeof(struct mwl_tx_desc));

	skb_push(skb, headroom);
	tx_desc->tx_priority = tx_ctrl->tx_priority;
	tx_desc->qos_ctrl = cpu_to_le16(tx_ctrl->qos_ctrl);
	tx_desc->pkt_len = cpu_to_le16(skb->len);

	if (tx_info->flags & IEEE80211_TX_INTFL_DONT_ENCRYPT) {
		tx_desc->flags |= MWL_TX_WCB_FLAGS_DONT_ENCRYPT;
	}

	if (tx_info->flags & IEEE80211_TX_CTL_NO_CCK_RATE) {
		tx_desc->flags |= MWL_TX_WCB_FLAGS_NO_CCK_RATE;
	}

	tx_desc->packet_info = 0;
	tx_desc->data_rate = 0;
	tx_desc->type = tx_ctrl->type;
	tx_desc->xmit_control = tx_ctrl->xmit_control;
	tx_desc->sap_pkt_info = 0;
	tx_desc->pkt_ptr = cpu_to_le32((u8 *)skb->data - ptr);
	tx_desc->status = 0;
	return;
}

/*

*/
static int mwl_sdio_host_to_card(struct mwl_priv *priv,
	int next_pkt_len, struct sk_buff *tx_skb)
{
	struct mwl_sdio_card *card = priv->intf;
	int ret;
	u32 buf_block_len;
	u32 blk_size;
	u32 port;
	u8 *payload;
	u32 pkt_len;
	struct mwl_dma_data *dma_data = (struct mwl_dma_data *)tx_skb->data;
	struct ieee80211_hdr *wh = &dma_data->wh;

	/* get port number. */
	ret = mwl_get_wr_port_data(priv, &port);
	if (ret) {
//			wiphy_err(priv->hw->wiphy, "%s: no wr_port available\n", __func__);
			return ret;
	}


//	wiphy_err(priv->hw->wiphy, "curr wr_port = %d\n", port);

	/* hard code rate_info here, will get this information from FW later. */
	card->rate_info = 0x0F4F8762;  /* VHT, SGI-80M, MCS7, 3SS.*/

	/* Push INTF_HEADER_LEN & mwl_tx_desc */
	mwl_process_txdesc(priv, tx_skb);

	payload = (u8 *)tx_skb->data;
	pkt_len = tx_skb->len;

	/* Transfer data to card */
	blk_size = MWL_SDIO_BLOCK_SIZE;
	buf_block_len = (pkt_len + blk_size - 1) / blk_size;
	*(__le16 *)&payload[0] = cpu_to_le16((u16)pkt_len);
	if (ieee80211_is_data(wh->frame_control))
		*(__le16 *)&payload[2] = cpu_to_le16(MWL_TYPE_DATA);
	else
		*(__le16 *)&payload[2] = cpu_to_le16(MWL_TYPE_MGMT);

	pkt_len = buf_block_len * blk_size;
	ret = mwl_host_to_card_mp_aggr(priv, payload, pkt_len,
						   port, next_pkt_len);

	if (ret != 0) {
		card->curr_wr_port = port;
		card->mp_wr_bitmap |= (u32)(1<<card->curr_wr_port);
	}
	mwl_write_data_complete(priv, tx_skb);
	return ret;
}

/*
 * SDIO interrupt handler.
 *
 * This function reads the interrupt status from firmware and handles
 * the interrupt in current thread (ksdioirqd) right away.
 */
static void
mwl_sdio_interrupt(struct sdio_func *func)
{
	struct mwl_priv *priv;
	struct mwl_sdio_card *card;

	card = sdio_get_drvdata(func);
	priv = card->priv;
	if (!card || !card->priv) {
		pr_err("int: func=%p card=%p priv=%p\n",
			 func, card, card ? card->priv : NULL);
		return;
	}
	mwl_sdio_interrupt_status(priv);
	mwl_sdio_process_int_status(priv);
	return;
}

/* Check command response back or not */
static int mwl_sdio_cmd_resp_wait_completed(struct mwl_priv *priv,
	unsigned short cmd)
{
	struct mwl_sdio_card *card = priv->intf;
	int status;

	/* Wait for completion */
	status = wait_event_interruptible_timeout(card->cmd_wait_q.wait,
						  (card->cmd_cond == true),
						  (12 * HZ));
	if (status <= 0) {
		if (status == 0)
			status = -ETIMEDOUT;
		wiphy_err(priv->hw->wiphy, "timeout, cmd_wait_q terminated: %d\n",
			    status);
		card->cmd_wait_q.status = status;
		return status;
	}

	status = card->cmd_wait_q.status;
	card->cmd_wait_q.status = 0;

  /* status is command response value */
	return status;
}

/*
 * This function enables the host interrupt.
 *
 * The host interrupt enable mask is written to the card
 * host interrupt mask register.
 */
static int mwl_sdio_register_dev(struct mwl_priv *priv)
{
	int rc  = 0;

	return rc;
}

/*
 * This function unregisters the SDIO device.
 *
 * The SDIO IRQ is released, the function is disabled and driver
 * data is set to null.
 */
static void
mwl_sdio_unregister_dev(struct mwl_priv *priv)
{
	struct mwl_sdio_card *card = priv->intf;

	destroy_workqueue(card->tx_workq);

	tasklet_enable(&priv->rx_task);
	tasklet_kill(&priv->rx_task);

	if (card) {
		sdio_claim_host(card->func);
		sdio_disable_func(card->func);
		sdio_release_host(card->func);
	}
}

static struct mwl_if_ops sdio_ops = {
	.inttf_head_len = INTF_HEADER_LEN,
	.init_if =            mwl_sdio_init,
	.cleanup_if =         mwl_sdio_cleanup,
	.check_card_status =  mwl_sdio_check_card_status,
	.prog_fw =            mwl_sdio_program_firmware,
	.register_dev =       mwl_sdio_register_dev,
	.unregister_dev =     mwl_sdio_unregister_dev,
	.send_cmd =           mwl_sdio_send_command,
	.cmd_resp_wait_completed = mwl_sdio_cmd_resp_wait_completed,
	.host_to_card       = mwl_sdio_host_to_card,
	.is_tx_available    = mwl_sdio_is_tx_available,
	.flush_amsdu		  = mwl_sdio_flush_amsdu,
};

static int mwl_sdio_probe(struct sdio_func *func,
	const struct sdio_device_id *id)
{
	static bool printed_version;
	struct mwl_sdio_card *card;
	int rc = 0;

	if (id->driver_data >= MWLUNKNOWN)
		return -ENODEV;

	if (!printed_version) {
		pr_info("<<%s version %s>>",
			LRD_DESC, LRD_DRV_VERSION);
		printed_version = true;
	}

	card = kzalloc(sizeof(struct mwl_sdio_card), GFP_KERNEL);
	if (!card) {
		pr_err("%s: allocate mwl_sdio_card structure failed",
			MWL_DRV_NAME);
		return -ENOMEM;
	}

	card->func = func;
	card->dev_id = id;

	if ((id->driver_data == MWL8897) || (id->driver_data == MWL8997)){
	if (id->driver_data == MWL8897) {
		card->reg = &mwl_reg_sd8897;
		card->chip_type = MWL8897;
	} else {
		card->reg = &mwl_reg_sd8997;
		card->chip_type = MWL8997;
        }
		card->max_ports = 32;
		card->mp_agg_pkt_limit = 16;
		card->tx_buf_size = MWL_TX_DATA_BUF_SIZE_4K;
		card->mp_tx_agg_buf_size = MWL_MP_AGGR_BUF_SIZE_MAX;
		card->mp_rx_agg_buf_size = MWL_MP_AGGR_BUF_SIZE_MAX;
		card->mp_end_port = 0x0020;
	}

	/* not sure this patch is needed or not?? */
	func->card->quirks |= MMC_QUIRK_BLKSZ_FOR_BYTE_MODE;
	sdio_claim_host(func);
	rc = sdio_enable_func(func);
	sdio_release_host(func);

	if (rc != 0) {
		pr_err("%s: failed to enable sdio function\n", __func__);
		goto err_sdio_enable;
	}

	card->tx_workq = alloc_workqueue("mwlwifi-tx_workq",
		WQ_HIGHPRI | WQ_MEM_RECLAIM | WQ_UNBOUND, 1);
	INIT_WORK(&card->tx_work, mwl_sdio_tx_workq);
	sdio_ops.ptx_work = &card->tx_work;
	sdio_ops.ptx_workq = card->tx_workq;

	memcpy(&sdio_ops.mwl_chip_tbl, &mwl_chip_tbl[card->chip_type],
		sizeof(struct mwl_chip_info));

	rc = mwl_add_card((void *)card, &sdio_ops);
	if (rc != 0) {
		pr_err("%s: failed to add_card\n", __func__);
		goto err_add_card;
	}

	return rc;
err_sdio_enable:
	kfree(card);

err_add_card:
	sdio_claim_host(func);
	sdio_disable_func(func);
	sdio_release_host(func);
	return rc;
}

static void mwl_sdio_remove(struct sdio_func *func)
{
	struct mwl_priv *priv;
	struct mwl_sdio_card *card;
	struct ieee80211_hw *hw;

	card = sdio_get_drvdata(func);
	if (!card || !card->priv) {
		pr_err("int: func=%p card=%p priv=%p\n",
			 func, card, card ? card->priv : NULL);
		return;
	}
	priv = card->priv;
	hw = priv->hw;

	mwl_wl_deinit(priv);

	mwl_sdio_cleanup(priv);
	mwl_sdio_unregister_dev(priv);

	ieee80211_free_hw(hw);

	return;
}

static int mwl_sdio_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct mwl_priv *priv;
	struct mwl_sdio_card *card;
	mmc_pm_flag_t pm_flag = 0;
	int ret = 0;

	if (func) {
		pm_flag = sdio_get_host_pm_caps(func);
		pr_info("cmd: %s: suspend: PM flag = 0x%x\n",
			 sdio_func_id(func), pm_flag);
		if (!(pm_flag & MMC_PM_KEEP_POWER)) {
			pr_err("%s: cannot remain alive while host is"\
				" suspended\n", sdio_func_id(func));
			return -ENOSYS;
		}

		card = sdio_get_drvdata(func);
		if (!card || !card->priv) {
			pr_err("suspend: invalid card or priv\n");
			return 0;
		}
	} else {
		pr_err("suspend: sdio_func is not specified\n");
		return 0;
	}

	priv = card->priv;

	/* Enable the Host Sleep */
	/*if (!mwifiex_enable_hs(adapter)) {
		//mwifiex_dbg(adapter, ERROR,
		//	    "cmd: failed to suspend\n");
		wiphy_err(priv->hw->wiphy, "cmd: failed to suspend\n");
		card->hs_enabling = false;
		return -EFAULT;
	}*/

	wiphy_debug(priv->hw->wiphy, "cmd: suspend with MMC_PM_KEEP_POWER\n");

	ret = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);

	/* Indicate device suspended */
	card->is_suspended = true;
	card->hs_enabling = false;

	return ret;
}

static int mwl_sdio_resume(struct device *dev)
{

	struct sdio_func *func = dev_to_sdio_func(dev);
	struct mwl_priv *priv;
	struct mwl_sdio_card *card;
	mmc_pm_flag_t pm_flag = 0;

	if (func) {
		pm_flag = sdio_get_host_pm_caps(func);
		card = sdio_get_drvdata(func);
		if (!card || !card->priv) {
			pr_err("resume: invalid card or priv\n");
			return 0;
		}
	} else {
		pr_err("resume: sdio_func is not specified\n");
		return 0;
	}

	priv = card->priv;

	if (!card->is_suspended) {
		wiphy_debug(priv->hw->wiphy,
			    "device already resumed\n");
		return 0;
	}

	card->is_suspended = false;

	/* Disable Host Sleep */
	/*mwifiex_cancel_hs(mwifiex_get_priv(adapter, MWIFIEX_BSS_ROLE_STA),
			MWIFIEX_SYNC_CMD);
	*/

	return 0;
}
MODULE_DEVICE_TABLE(sdio, mwl_sdio_id_tbl);

static void mwl_sdio_flush_amsdu(unsigned long data)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)data;
	struct mwl_priv *priv = hw->priv;
	struct mwl_amsdu_frag *amsdu_frag;

	struct mwl_sta *sta_info;
	int i;

	list_for_each_entry(sta_info, &priv->sta_list, list) {
		for (i = 0; i < SYSADPT_TX_WMM_QUEUES; i++) {
			amsdu_frag = &sta_info->amsdu_ctrl.frag[i];
			if (amsdu_frag->num) {
				spin_unlock_bh(&sta_info->amsdu_lock);
				mwl_tx_skb(priv, i,
					   amsdu_frag->skb);
				spin_lock_bh(&sta_info->amsdu_lock);
				amsdu_frag->num = 0;
				amsdu_frag->cur_pos = NULL;
			}
		}
	}

	return;
}

static void mwl_sdio_flush_amsdu_no_lock(unsigned long data)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)data;
	struct mwl_priv *priv = hw->priv;
	struct mwl_amsdu_frag *amsdu_frag;

	struct mwl_sta *sta_info;
	int i;

	list_for_each_entry(sta_info, &priv->sta_list, list) {
		if (sta_info == NULL) {
			return;
		}
		for (i = 0; i < SYSADPT_TX_WMM_QUEUES; i++) {
			amsdu_frag = &sta_info->amsdu_ctrl.frag[i];
			if (amsdu_frag->num) {
				mwl_tx_skb(priv, i,
					   amsdu_frag->skb);
				amsdu_frag->num = 0;
				amsdu_frag->cur_pos = NULL;
			}
		}
	}

	return;
}



#define module_sdio_driver(__sdio_driver) \
		module_driver(__sdio_driver, sdio_register_driver, \
			sdio_unregister_driver)

static const struct dev_pm_ops mwl_sdio_pm_ops = {
	.suspend  = mwl_sdio_suspend,
	.resume   = mwl_sdio_resume,
};

static struct sdio_driver mwl_sdio_driver = {
	.name     = MWL_DRV_NAME,
	.id_table = mwl_sdio_id_tbl,
	.probe    = mwl_sdio_probe,
	.remove   = mwl_sdio_remove,
	.drv = {
		   .owner = THIS_MODULE,
		   .pm    = &mwl_sdio_pm_ops,
	}
};


#define LRDMWL_MMC_PROBE_DELAY	150
static unsigned int reset_pwd_gpio = ARCH_NR_GPIOS;

#ifdef CONFIG_GPIOLIB
module_param(reset_pwd_gpio, uint, 0644);
MODULE_PARM_DESC(reset_pwd_gpio, "WIFI CHIP_PWD reset pin GPIO");
#endif

static int mwl_sdio_init_gpio(void)
{
	int ret = 0;

	if (gpio_is_valid(reset_pwd_gpio)) {
		/* Request the reset GPIO, and assert it to make sure we get a
		 * clean boot in-case we had a floating input or other issue.
		 */
		ret = gpio_request_one(reset_pwd_gpio,
				       GPIOF_INIT_LOW |
				       GPIOF_EXPORT_DIR_FIXED,
				       "WIFI_RESET");

		if (!ret) {
			msleep(50);  /* Pin must be asserted a min of 50ms  */

			gpio_set_value(reset_pwd_gpio, 1); /* De-assert the pin */

			msleep(LRDMWL_MMC_PROBE_DELAY);
		}
		else {
			pr_info("lrdmwl: Unable to obtain WIFI power gpio. %d\n", ret);
		}
	}

	return ret;
}

static int mwl_sdio_reset_gpio(struct mwl_priv *priv)
{
	int ret = -2;

	if (gpio_is_valid(reset_pwd_gpio)) {
		gpio_set_value(reset_pwd_gpio, 0);

		msleep(50);

		gpio_set_value(reset_pwd_gpio, 1);
	}

	return ret;
}

static int mwl_sdio_release_gpio(void)
{
	if (gpio_is_valid(reset_pwd_gpio)) {
		/* Be sure we release GPIO and leave the chip in reset
		 * when we unload
		 */
		gpio_set_value(reset_pwd_gpio, 0);
		gpio_free(reset_pwd_gpio);
	}

	return 0;
}

static int __init mwl_sdio_driver_init(void)
{
	int ret = 0;

	ret = mwl_sdio_init_gpio();

	if (!ret) {
		ret = sdio_register_driver(&mwl_sdio_driver);

		if (ret) {
			mwl_sdio_release_gpio();
		}
	}

	return ret;
}

static void __exit mwl_sdio_driver_exit(void)
{
	sdio_unregister_driver(&mwl_sdio_driver);

	mwl_sdio_release_gpio();
}

module_init(mwl_sdio_driver_init);
module_exit(mwl_sdio_driver_exit);
MODULE_DESCRIPTION(LRD_SDIO_DESC);
MODULE_VERSION(LRD_SDIO_VERSION);
MODULE_AUTHOR(LRD_AUTHOR);
MODULE_LICENSE("GPL v2");



