#include <linux/module.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#endif
#include <linux/pci.h>
#include <linux/pcieport_if.h>
#include <linux/interrupt.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include "sysadpt.h"
#include "dev.h"
#include "fwdl.h"
#include "fwcmd.h"
#include "hostcmd.h"
#include "main.h"
#include "pcie.h"
#include "isr.h"
#include "tx.h"
#include "rx.h"

/*Note PCIEDRV_VERSION define was taken from SDIO since there
 *     was no PCI version defined in reference driver */
#define MWL_PCIEDRV_VERSION  "10.3.0.16-20160105"
#define LRD_PCIE_VERSION     LRD_BLD_VERSION "-" MWL_PCIEDRV_VERSION
#define LRD_PCIE_DESC        "Laird 60 Series Wireless PCIE Network Driver"

#define INTF_HEADER_LEN      0

#ifdef CONFIG_ARCH_BERLIN
#define MWL_FW_ROOT     "mrvl"
#else
#define MWL_FW_ROOT     "lrdmwl"
#endif

static struct mwl_chip_info mwl_chip_tbl[] = {
	[MWL8864] = {
		.part_name	= "88W8864",
		.fw_image	= MWL_FW_ROOT"/88W8864_pcie.bin",
		.antenna_tx	= ANTENNA_TX_4_AUTO,
		.antenna_rx	= ANTENNA_RX_4_AUTO,
	},
	[MWL8897] = {
		.part_name	= "88W8897",
		.fw_image	= MWL_FW_ROOT"/88W8897_pcie.bin",
		.antenna_tx	= ANTENNA_TX_2,
		.antenna_rx	= ANTENNA_RX_2,
	},
	[MWL8964] = {
		.part_name	= "88W8964",
		.fw_image	= MWL_FW_ROOT"/88W8964_pcie.bin",
		.antenna_tx	= ANTENNA_TX_4_AUTO,
		.antenna_rx	= ANTENNA_RX_4_AUTO,
	},
	[MWL8997] = {
		.part_name	= "88W8997",
		.fw_image	= MWL_FW_ROOT"/88W8997_pcie.bin",
		.antenna_tx	= ANTENNA_TX_2,
		.antenna_rx	= ANTENNA_RX_2,
	},
};

static void mwl_pcie_tx_flush_amsdu(unsigned long data);
static int mwl_tx_ring_alloc(struct mwl_priv *priv);
static int mwl_tx_ring_init(struct mwl_priv *priv);
static void mwl_tx_ring_cleanup(struct mwl_priv *priv);
static void mwl_tx_ring_free(struct mwl_priv *priv);

#define MAX_WAIT_FW_COMPLETE_ITERATIONS         8000

static irqreturn_t mwl_pcie_isr(int irq, void *dev_id);
static struct pci_device_id mwl_pci_id_tbl[] = {
	{ PCI_VDEVICE(MARVELL, 0x2a55), .driver_data = MWL8864, },
	{ PCI_VDEVICE(MARVELL, 0x2b38), .driver_data = MWL8897, },
	{ PCI_VDEVICE(MARVELL, 0x2b40), .driver_data = MWL8964, },
	{ PCI_VDEVICE(MARVELL_EXT, 0x2b42), .driver_data = MWL8997, },
	{ },
};

static void mwl_free_pci_resource(struct mwl_priv *priv)
{
	struct mwl_pcie_card *card = priv->intf;
	struct pci_dev *pdev = card->pdev;

	/* priv->pcmd_buf will be automatically freed on driver unload */
#if 0
	if (priv->pcmd_buf)
		dma_free_coherent(priv->dev,
			CMD_BUF_SIZE,
			priv->pcmd_buf,
			priv->pphys_cmd_buf);
#endif

	if (pdev) {
		iounmap((volatile void __iomem *)&pdev->resource[0]);
		iounmap((volatile void __iomem *)&pdev->resource[card->next_bar_num]);
	}
}

static int mwl_alloc_pci_resource(struct mwl_priv *priv)
{
	struct mwl_pcie_card *card = priv->intf;
	struct pci_dev *pdev = card->pdev;
	void __iomem *addr;

	card->next_bar_num = 1;	/* 32-bit */
	if (pci_resource_flags(pdev, 0) & 0x04)
		card->next_bar_num = 2;	/* 64-bit */

	addr = devm_ioremap_resource(priv->dev, &pdev->resource[0]);
	if (IS_ERR(addr)) {
		wiphy_err(priv->hw->wiphy,
			  "%s: cannot reserve PCI memory region 0\n",
			  MWL_DRV_NAME);
		goto err;
	}
	card->iobase0 = addr;
	wiphy_debug(priv->hw->wiphy, "card->iobase0 = %p\n", card->iobase0);

	addr = devm_ioremap_resource(priv->dev,
				     &pdev->resource[card->next_bar_num]);
	if (IS_ERR(addr)) {
		wiphy_err(priv->hw->wiphy,
			  "%s: cannot reserve PCI memory region 1\n",
			  MWL_DRV_NAME);
		goto err;
	}
	card->iobase1 = addr;
	wiphy_debug(priv->hw->wiphy, "card->iobase1 = %p\n", card->iobase1);

	priv->pcmd_buf =
		(unsigned short *)dmam_alloc_coherent(priv->dev,
						      CMD_BUF_SIZE,
						      &priv->pphys_cmd_buf,
						      GFP_KERNEL);
	if (!priv->pcmd_buf) {
		wiphy_err(priv->hw->wiphy,
			  "%s: cannot alloc memory for command buffer\n",
			  MWL_DRV_NAME);
		goto err;
	}
	wiphy_debug(priv->hw->wiphy,
		    "priv->pcmd_buf = %p  priv->pphys_cmd_buf = %p\n",
		    priv->pcmd_buf,
		    (void *)priv->pphys_cmd_buf);
	memset(priv->pcmd_buf, 0x00, CMD_BUF_SIZE);

	return 0;

err:
	wiphy_err(priv->hw->wiphy, "pci alloc fail\n");

	return -EIO;
}

int mwl_tx_init(struct ieee80211_hw *hw)
{
	struct mwl_priv *priv = hw->priv;
	int rc;

	wiphy_err(hw->wiphy, "%s() called: ctype=%d\n", __FUNCTION__, priv->chip_type);

	skb_queue_head_init(&priv->delay_q);

	if (IS_PFU_ENABLED(priv->chip_type)) {
		rc = wlan_pcie_create_txbd_ring(hw);
		if (rc) {
			wiphy_err(hw->wiphy, "wlan_pcie_create_txbd_ring() failed\n");
			return rc;
		}
	} else {
		rc = mwl_tx_ring_alloc(priv);
		if (rc) {
			wiphy_err(hw->wiphy, "allocating TX ring failed\n");
			return rc;
		}
	}

	rc = mwl_tx_ring_init(priv);

	if (rc) {
		if (!IS_PFU_ENABLED(priv->chip_type)) {

			mwl_tx_ring_free(priv);
			wiphy_err(hw->wiphy, "initializing TX ring failed\n");
			return rc;
		}
	}

	return 0;
}

/* rx */
#define MAX_NUM_RX_RING_BYTES  (SYSADPT_MAX_NUM_RX_DESC * \
				sizeof(struct mwl_rx_desc))

#define MAX_NUM_RX_HNDL_BYTES  (SYSADPT_MAX_NUM_RX_DESC * \
				sizeof(struct mwl_rx_hndl))

static int mwl_rx_ring_alloc(struct mwl_priv *priv)
{
	struct mwl_desc_data *desc;

	desc = &priv->desc_data[0];
	desc->prx_ring = (struct mwl_rx_desc *)
		dma_alloc_coherent(priv->dev,
				   MAX_NUM_RX_RING_BYTES,
				   &desc->pphys_rx_ring,
				   GFP_KERNEL);
	if (!desc->prx_ring) {
		wiphy_err(priv->hw->wiphy, "cannot alloc mem\n");
		return -ENOMEM;
	}

	memset(desc->prx_ring, 0x00, MAX_NUM_RX_RING_BYTES);

	desc->rx_hndl = kmalloc(MAX_NUM_RX_HNDL_BYTES, GFP_KERNEL);

	if (!desc->rx_hndl) {
		dma_free_coherent(priv->dev,
				  MAX_NUM_RX_RING_BYTES,
				  desc->prx_ring,
				  desc->pphys_rx_ring);
		return -ENOMEM;
	}

	memset(desc->rx_hndl, 0x00, MAX_NUM_RX_HNDL_BYTES);

	return 0;
}

static int mwl_rx_ring_init(struct mwl_priv *priv)
{
	struct mwl_pcie_card *card = priv->intf;
	struct mwl_desc_data *desc;
	int i;
	struct mwl_rx_hndl *rx_hndl;
	dma_addr_t dma;
	u32 val;

	desc = &priv->desc_data[0];

	if (desc->prx_ring) {
		desc->rx_buf_size = SYSADPT_MAX_AGGR_SIZE;

		for (i = 0; i < SYSADPT_MAX_NUM_RX_DESC; i++) {
			rx_hndl = &desc->rx_hndl[i];
			rx_hndl->psk_buff =
				dev_alloc_skb(desc->rx_buf_size);

			if (!rx_hndl->psk_buff) {
				wiphy_err(priv->hw->wiphy,
					  "rxdesc %i: no skbuff available\n",
					  i);
				return -ENOMEM;
			}

			skb_reserve(rx_hndl->psk_buff,
				    SYSADPT_MIN_BYTES_HEADROOM);
			desc->prx_ring[i].rx_control =
				EAGLE_RXD_CTRL_DRIVER_OWN;
			desc->prx_ring[i].status = EAGLE_RXD_STATUS_OK;
			desc->prx_ring[i].qos_ctrl = 0x0000;
			desc->prx_ring[i].channel = 0x00;
			desc->prx_ring[i].rssi = 0x00;
			desc->prx_ring[i].pkt_len =
				cpu_to_le16(SYSADPT_MAX_AGGR_SIZE);
			dma = pci_map_single(card->pdev,
					     rx_hndl->psk_buff->data,
					     desc->rx_buf_size,
					     PCI_DMA_FROMDEVICE);
			if (pci_dma_mapping_error(card->pdev, dma)) {
				wiphy_err(priv->hw->wiphy,
					  "failed to map pci memory!\n");
				return -ENOMEM;
			}
			desc->prx_ring[i].pphys_buff_data = cpu_to_le32(dma);
			val = (u32)desc->pphys_rx_ring +
			      ((i + 1) * sizeof(struct mwl_rx_desc));
			desc->prx_ring[i].pphys_next = cpu_to_le32(val);
			rx_hndl->pdesc = &desc->prx_ring[i];
			if (i < (SYSADPT_MAX_NUM_RX_DESC - 1))
				rx_hndl->pnext = &desc->rx_hndl[i + 1];
		}
		desc->prx_ring[SYSADPT_MAX_NUM_RX_DESC - 1].pphys_next =
			cpu_to_le32((u32)desc->pphys_rx_ring);
		desc->rx_hndl[SYSADPT_MAX_NUM_RX_DESC - 1].pnext =
			&desc->rx_hndl[0];
		desc->pnext_rx_hndl = &desc->rx_hndl[0];

		return 0;
	}

	wiphy_err(priv->hw->wiphy, "no valid RX mem\n");

	return -ENOMEM;
}

static void mwl_rx_ring_cleanup(struct mwl_priv *priv)
{
	struct mwl_pcie_card *card = priv->intf;
	struct mwl_desc_data *desc;
	int i;
	struct mwl_rx_hndl *rx_hndl;

	desc = &priv->desc_data[0];

	if (desc->prx_ring) {
		for (i = 0; i < SYSADPT_MAX_NUM_RX_DESC; i++) {
			rx_hndl = &desc->rx_hndl[i];
			if (!rx_hndl->psk_buff)
				continue;

			pci_unmap_single(card->pdev,
					 le32_to_cpu
					 (rx_hndl->pdesc->pphys_buff_data),
					 desc->rx_buf_size,
					 PCI_DMA_FROMDEVICE);

			wiphy_info(priv->hw->wiphy,
				   "Rx: unmapped+free'd %i 0x%p 0x%x %i\n",
				   i, rx_hndl->psk_buff->data,
				   le32_to_cpu(rx_hndl->pdesc->pphys_buff_data),
				   desc->rx_buf_size);

			dev_kfree_skb_any(rx_hndl->psk_buff);
			rx_hndl->psk_buff = NULL;
		}
	}
}

static void mwl_rx_ring_free(struct mwl_priv *priv)
{
	struct mwl_desc_data *desc;

	desc = &priv->desc_data[0];

	if (desc->prx_ring) {
		mwl_rx_ring_cleanup(priv);

		dma_free_coherent(priv->dev,
				  MAX_NUM_RX_RING_BYTES,
				  desc->prx_ring,
				  desc->pphys_rx_ring);

		desc->prx_ring = NULL;
	}

	kfree(desc->rx_hndl);

	desc->pnext_rx_hndl = NULL;
}

static int mwl_rx_refill(struct mwl_priv *priv, struct mwl_rx_hndl *rx_hndl)
{
	struct mwl_pcie_card *card = priv->intf;
	struct mwl_desc_data *desc;
	dma_addr_t dma;

	desc = &priv->desc_data[0];

	rx_hndl->psk_buff = dev_alloc_skb(desc->rx_buf_size);

	if (!rx_hndl->psk_buff)
		return -ENOMEM;

	skb_reserve(rx_hndl->psk_buff, SYSADPT_MIN_BYTES_HEADROOM);

	rx_hndl->pdesc->status = EAGLE_RXD_STATUS_OK;
	rx_hndl->pdesc->qos_ctrl = 0x0000;
	rx_hndl->pdesc->channel = 0x00;
	rx_hndl->pdesc->rssi = 0x00;
	rx_hndl->pdesc->pkt_len = cpu_to_le16(desc->rx_buf_size);

	dma = pci_map_single(card->pdev,
			     rx_hndl->psk_buff->data,
			     desc->rx_buf_size,
			     PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(card->pdev, dma)) {
		dev_kfree_skb_any(rx_hndl->psk_buff);
		wiphy_err(priv->hw->wiphy,
			  "failed to map pci memory!\n");
		return -ENOMEM;
	}

	rx_hndl->pdesc->pphys_buff_data = cpu_to_le32(dma);

	return 0;
}

void mwl_pcie_rx_recv(unsigned long data)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)data;
	struct mwl_priv *priv = hw->priv;
	struct mwl_pcie_card *card = priv->intf;
	struct mwl_desc_data *desc;
	struct mwl_rx_hndl *curr_hndl;
	int work_done = 0;
	struct sk_buff *prx_skb = NULL;
	int pkt_len;
	struct ieee80211_rx_status status;
	struct mwl_vif *mwl_vif = NULL;
	struct ieee80211_hdr *wh;
	struct mwl_rx_event_data *rx_evnt;

	desc = &priv->desc_data[0];
	curr_hndl = desc->pnext_rx_hndl;

	if (!curr_hndl) {
		set_bit(MACREG_A2HRIC_BIT_RX_RDY,
		(card->iobase1 + MACREG_REG_A2H_INTERRUPT_STATUS_MASK));

		priv->is_rx_schedule = false;
		wiphy_warn(hw->wiphy, "busy or no receiving packets\n");
		return;
	}

	while ((curr_hndl->pdesc->rx_control == EAGLE_RXD_CTRL_DMA_OWN) &&
	       (work_done < priv->recv_limit)) {
		prx_skb = curr_hndl->psk_buff;
		if (!prx_skb)
			goto out;
		pci_unmap_single(card->pdev,
				 le32_to_cpu(curr_hndl->pdesc->pphys_buff_data),
				 desc->rx_buf_size,
				 PCI_DMA_FROMDEVICE);
		pkt_len = le16_to_cpu(curr_hndl->pdesc->pkt_len);

		if (skb_tailroom(prx_skb) < pkt_len) {
			dev_kfree_skb_any(prx_skb);
			goto out;
		}

		if (curr_hndl->pdesc->payldType == RX_PAYLOAD_TYPE_EVENT_INFO) {
			rx_evnt = (struct mwl_rx_event_data *)prx_skb->data;
			mwl_handle_rx_event(hw, rx_evnt);
			dev_kfree_skb_any(prx_skb);
			goto out;
		}

		if ((curr_hndl->pdesc->channel != hw->conf.chandef.chan->hw_value) &&
			!(priv->roc.tmr_running && priv->roc.in_progress &&
				(curr_hndl->pdesc->channel == priv->roc.chan))) {
			dev_kfree_skb_any(prx_skb);
			goto out;
		}

		mwl_rx_prepare_status(curr_hndl->pdesc, &status);

		priv->noise = -curr_hndl->pdesc->noise_floor;

		wh = &((struct mwl_dma_data *)prx_skb->data)->wh;

		if (ieee80211_has_protected(wh->frame_control)) {
			/* Check if hw crypto has been enabled for
			 * this bss. If yes, set the status flags
			 * accordingly
			 */
			if (ieee80211_has_tods(wh->frame_control)) {
				mwl_vif = mwl_rx_find_vif_bss(priv, wh->addr1);
				if (mwl_vif != NULL &&
				    ieee80211_has_a4(wh->frame_control))
					mwl_vif = mwl_rx_find_vif_bss(priv,
							wh->addr2);
			} else {
				mwl_vif = mwl_rx_find_vif_bss(priv, wh->addr2);
			}

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
					struct mwl_dma_data *tr;

					tr = (struct mwl_dma_data *)
					     prx_skb->data;
					memset((void *)&tr->data, 0, 4);
					pkt_len += 4;
				}

				if (!ieee80211_is_auth(wh->frame_control))
					status.flag |= RX_FLAG_IV_STRIPPED |
						       RX_FLAG_DECRYPTED |
						       RX_FLAG_MMIC_STRIPPED;
			}
		}

		skb_put(prx_skb, pkt_len);
		mwl_rx_remove_dma_header(prx_skb, curr_hndl->pdesc->qos_ctrl);

		wh = (struct ieee80211_hdr *)prx_skb->data;

		if (ieee80211_is_mgmt(wh->frame_control)) {
			struct ieee80211_mgmt *mgmt;
			__le16 capab;

			mgmt = (struct ieee80211_mgmt *)prx_skb->data;

			if (unlikely(ieee80211_is_action(wh->frame_control) &&
				     mgmt->u.action.category ==
				     WLAN_CATEGORY_BACK &&
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
					goto out;
		}
#endif
		memcpy(IEEE80211_SKB_RXCB(prx_skb), &status, sizeof(status));
		mwl_rx_upload_pkt(hw, prx_skb);
out:
		mwl_rx_refill(priv, curr_hndl);
		curr_hndl->pdesc->rx_control = EAGLE_RXD_CTRL_DRIVER_OWN;
		curr_hndl->pdesc->qos_ctrl = 0;
		curr_hndl = curr_hndl->pnext;
		work_done++;
	}

	desc->pnext_rx_hndl = curr_hndl;

	set_bit(MACREG_A2HRIC_BIT_RX_RDY,
	(card->iobase1 + MACREG_REG_A2H_INTERRUPT_STATUS_MASK));

	priv->is_rx_schedule = false;
	return;
}


int mwl_rx_init(struct ieee80211_hw *hw)
{
	struct mwl_priv *priv = hw->priv;
	int rc;

	rc = mwl_rx_ring_alloc(priv);
	if (rc) {
		wiphy_err(hw->wiphy, "allocating RX ring failed\n");
		return rc;
	}

	rc = mwl_rx_ring_init(priv);
	if (rc) {
		mwl_rx_ring_free(priv);
		wiphy_err(hw->wiphy,
			  "initializing RX ring failed\n");
		return rc;
	}

	return 0;
}

void mwl_rx_deinit(struct ieee80211_hw *hw)
{
	struct mwl_priv *priv = hw->priv;

	mwl_rx_ring_cleanup(priv);
	mwl_rx_ring_free(priv);
}

/* tx */
#define MAX_NUM_TX_RING_BYTES  (SYSADPT_MAX_NUM_TX_DESC * \
				sizeof(struct mwl_tx_desc))

#define MAX_NUM_TX_HNDL_BYTES   (SYSADPT_MAX_NUM_TX_DESC * \
				sizeof(struct mwl_tx_hndl))
static int mwl_tx_ring_alloc(struct mwl_priv *priv)
{
	struct mwl_desc_data *desc;
	int num;
	u8 *mem;

	desc = &priv->desc_data[0];

	mem = dma_alloc_coherent(priv->dev,
				 MAX_NUM_TX_RING_BYTES *
				 SYSADPT_NUM_OF_DESC_DATA,
				 &desc->pphys_tx_ring,
				 GFP_KERNEL);

	if (!mem) {
		wiphy_err(priv->hw->wiphy, "cannot alloc mem\n");
		return -ENOMEM;
	}

	for (num = 0; num < SYSADPT_NUM_OF_DESC_DATA; num++) {
		desc = &priv->desc_data[num];

		desc->ptx_ring = (struct mwl_tx_desc *)
			(mem + num * MAX_NUM_TX_RING_BYTES);

		desc->pphys_tx_ring = (dma_addr_t)
			((u32)priv->desc_data[0].pphys_tx_ring +
			num * MAX_NUM_TX_RING_BYTES);

		memset(desc->ptx_ring, 0x00,
		       MAX_NUM_TX_RING_BYTES);
	}

	mem = kmalloc(MAX_NUM_TX_HNDL_BYTES * SYSADPT_NUM_OF_DESC_DATA,
		      GFP_KERNEL);

	if (!mem) {
		wiphy_err(priv->hw->wiphy, "cannot alloc mem\n");
		dma_free_coherent(priv->dev,
				  MAX_NUM_TX_RING_BYTES *
				  SYSADPT_NUM_OF_DESC_DATA,
				  priv->desc_data[0].ptx_ring,
				  priv->desc_data[0].pphys_tx_ring);
		return -ENOMEM;
	}

	for (num = 0; num < SYSADPT_NUM_OF_DESC_DATA; num++) {
		desc = &priv->desc_data[num];

		desc->tx_hndl = (struct mwl_tx_hndl *)
			(mem + num * MAX_NUM_TX_HNDL_BYTES);

		memset(desc->tx_hndl, 0x00,
		       MAX_NUM_TX_HNDL_BYTES);
	}

	return 0;
}

static int mwl_tx_ring_init(struct mwl_priv *priv)
{
	int num;
	int i;
	struct mwl_desc_data *desc;

	for (num = 0; num < SYSADPT_NUM_OF_DESC_DATA; num++) {
		skb_queue_head_init(&priv->txq[num]);
		priv->fw_desc_cnt[num] = 0;

		if (!IS_PFU_ENABLED(priv->chip_type)) {
			desc = &priv->desc_data[num];

			if (desc->ptx_ring) {
				for (i = 0; i < SYSADPT_MAX_NUM_TX_DESC; i++) {
				desc->ptx_ring[i].status =
					cpu_to_le32(EAGLE_TXD_STATUS_IDLE);
					desc->ptx_ring[i].pphys_next =
					cpu_to_le32((u32)desc->pphys_tx_ring +
					((i + 1) * sizeof(struct mwl_tx_desc)));
					desc->tx_hndl[i].pdesc =
						&desc->ptx_ring[i];
					if (i < SYSADPT_MAX_NUM_TX_DESC - 1)
						desc->tx_hndl[i].pnext =
						&desc->tx_hndl[i + 1];
				}
				desc->ptx_ring[SYSADPT_MAX_NUM_TX_DESC - 1].pphys_next =
					cpu_to_le32((u32)desc->pphys_tx_ring);
				desc->tx_hndl[SYSADPT_MAX_NUM_TX_DESC - 1].pnext =
					&desc->tx_hndl[0];

				desc->pstale_tx_hndl = &desc->tx_hndl[0];
				desc->pnext_tx_hndl  = &desc->tx_hndl[0];
			} else {
				wiphy_err(priv->hw->wiphy, "no valid TX mem\n");
				return -ENOMEM;
			}
		}
	}

	return 0;
}

static void mwl_tx_ring_cleanup(struct mwl_priv *priv)
{
	struct mwl_pcie_card *card = priv->intf;
	int cleaned_tx_desc = 0;
	int num, i;
	struct mwl_desc_data *desc;

	for (num = 0; num < SYSADPT_NUM_OF_DESC_DATA; num++) {
		skb_queue_purge(&priv->txq[num]);
		priv->fw_desc_cnt[num] = 0;

		if (!IS_PFU_ENABLED(priv->chip_type)) {
			desc = &priv->desc_data[num];

			if (desc->ptx_ring) {
				for (i = 0; i < SYSADPT_MAX_NUM_TX_DESC; i++) {
					if (!desc->tx_hndl[i].psk_buff)
						continue;

					wiphy_info(priv->hw->wiphy,
							"Tx: unmapped and free'd %i 0x%p 0x%x\n",
							i,
							desc->tx_hndl[i].psk_buff->data,
							le32_to_cpu(
								desc->ptx_ring[i].pkt_ptr));
					pci_unmap_single(card->pdev,
							le32_to_cpu(
								desc->ptx_ring[i].pkt_ptr),
							desc->tx_hndl[i].psk_buff->len,
							PCI_DMA_TODEVICE);
					dev_kfree_skb_any(desc->tx_hndl[i].psk_buff);
					desc->ptx_ring[i].status =
						cpu_to_le32(EAGLE_TXD_STATUS_IDLE);
					desc->ptx_ring[i].pkt_ptr = 0;
					desc->ptx_ring[i].pkt_len = 0;
					desc->tx_hndl[i].psk_buff = NULL;
					cleaned_tx_desc++;
				}
			}
		}
	}

	wiphy_info(priv->hw->wiphy, "cleaned %i TX descr\n", cleaned_tx_desc);
}

static void mwl_tx_ring_free(struct mwl_priv *priv)
{
	int num;

	if (priv->desc_data[0].ptx_ring) {
		dma_free_coherent(priv->dev,
				  MAX_NUM_TX_RING_BYTES *
				  SYSADPT_NUM_OF_DESC_DATA,
				  priv->desc_data[0].ptx_ring,
				  priv->desc_data[0].pphys_tx_ring);
	}

	for (num = 0; num < SYSADPT_NUM_OF_DESC_DATA; num++) {
		if (priv->desc_data[num].ptx_ring)
			priv->desc_data[num].ptx_ring = NULL;
		priv->desc_data[num].pstale_tx_hndl = NULL;
		priv->desc_data[num].pnext_tx_hndl = NULL;
	}

	kfree(priv->desc_data[0].tx_hndl);
}
void mwl_tx_deinit(struct ieee80211_hw *hw)
{
	struct mwl_priv *priv = hw->priv;

	skb_queue_purge(&priv->delay_q);

	mwl_tx_ring_cleanup(priv);

	if (IS_PFU_ENABLED(priv->chip_type))
		wlan_pcie_delete_txbd_ring(hw);
	else
		mwl_tx_ring_free(priv);
}

static bool mwl_pcie_is_tx_available(struct mwl_priv *priv, int desc_num)
{
	struct mwl_pcie_card *card = priv->intf;
	struct mwl_tx_hndl *tx_hndl;

	if (IS_PFU_ENABLED(priv->chip_type))
		return PCIE_TXBD_NOT_FULL(priv->txbd_wrptr, priv->txbd_rdptr);

	tx_hndl = priv->desc_data[desc_num].pnext_tx_hndl;

	if (!tx_hndl->pdesc)
		return false;

	if (tx_hndl->pdesc->status != EAGLE_TXD_STATUS_IDLE) {
		/* Interrupt F/W anyway */
		if (tx_hndl->pdesc->status &
		    cpu_to_le32(EAGLE_TXD_STATUS_FW_OWNED))
			writel(MACREG_H2ARIC_BIT_PPA_READY,
			       card->iobase1 +
			       MACREG_REG_H2A_INTERRUPT_EVENTS);
		return false;
	}

	return true;
}

/*
 * This function initializes the PCI-E host memory space, WCB rings, etc.
 *
 * The following initializations steps are followed -
 *      - Allocate TXBD ring buffers
 *      - Allocate RXBD ring buffers
 *      - Allocate event BD ring buffers
 *      - Allocate command response ring buffer
 *      - Allocate sleep cookie buffer
 */
static int mwl_pcie_init(struct mwl_priv *priv)
{
	struct mwl_pcie_card *card = priv->intf;
	struct pci_dev *pdev = card->pdev;
	struct ieee80211_hw *hw;
	int rc = 0;

	priv->chip_type = card->chip_type;
	priv->host_if = MWL_IF_PCIE;

	hw = priv->hw;
	rc = pci_enable_device(pdev);
	if (rc) {
		wiphy_err(hw->wiphy, "%s: cannot enable new PCI device.\n",
			MWL_DRV_NAME);
		goto err_enable_dev;
	}

	pci_set_master(pdev);

	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (rc) {
		wiphy_err(hw->wiphy, "%s: 32-bit PCI DMA not supported",
			MWL_DRV_NAME);
		goto err_set_dma;
	}

	pci_set_drvdata(pdev, hw);
	priv->dev = &pdev->dev;
	rc = mwl_alloc_pci_resource(priv);
	if (rc) {
		wiphy_err(hw->wiphy, "%s: fail to allocate pci resource.\n",
			MWL_DRV_NAME);
		goto err_alloc_resource;
	}

	rc = mwl_tx_init(hw);
	if (rc) {
		wiphy_err(hw->wiphy, "%s: fail to initialize TX\n",
			  MWL_DRV_NAME);
		goto err_mwl_tx_init;
	}

	rc = mwl_rx_init(hw);
	if (rc) {
		wiphy_err(hw->wiphy, "%s: fail to initialize RX\n",
			  MWL_DRV_NAME);
		goto err_mwl_rx_init;
	}

	rc = request_irq(card->pdev->irq, mwl_pcie_isr,
			 IRQF_SHARED, MWL_DRV_NAME, priv->hw);
	if (rc) {
		priv->irq = -1;
		wiphy_err(priv->hw->wiphy, "%s: fail to register IRQ handler\n",
			  MWL_DRV_NAME);
		goto err_mwl_rx_init;
	}
	priv->irq = card->pdev->irq;
	return rc;
err_mwl_rx_init:
	mwl_tx_deinit(hw);

err_mwl_tx_init:
	mwl_free_pci_resource(priv);

err_alloc_resource:
	pci_set_drvdata(pdev, NULL);

err_set_dma:
	pci_disable_device(pdev);

err_enable_dev:
	return rc;
}

static void mwl_pcie_cleanup(struct mwl_priv *priv)
{
	struct mwl_pcie_card *card = priv->intf;
	struct pci_dev *pdev = card->pdev;

	mwl_rx_deinit(priv->hw);
	mwl_tx_deinit(priv->hw);

	mwl_free_pci_resource(priv);
	if (pdev) {
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
	}
}

static void mwl_fwdl_trig_pcicmd(struct mwl_priv *priv)
{
	struct mwl_pcie_card *card = (struct mwl_pcie_card *)priv->intf;

	writel(priv->pphys_cmd_buf, card->iobase1 + MACREG_REG_GEN_PTR);

	writel(0x00, card->iobase1 + MACREG_REG_INT_CODE);

	writel(MACREG_H2ARIC_BIT_DOOR_BELL,
	       card->iobase1 + MACREG_REG_H2A_INTERRUPT_EVENTS);
}

static void mwl_fwdl_trig_pcicmd_bootcode(struct mwl_priv *priv)
{
	struct mwl_pcie_card *card = (struct mwl_pcie_card *) priv->intf;

	writel(priv->pphys_cmd_buf, card->iobase1 + MACREG_REG_GEN_PTR);

	writel(0x00, card->iobase1 + MACREG_REG_INT_CODE);

	writel(MACREG_H2ARIC_BIT_DOOR_BELL,
	       card->iobase1 + MACREG_REG_H2A_INTERRUPT_EVENTS);
}
static int mwl_pcie_program_firmware(struct mwl_priv *priv)
{
	const struct firmware *fw;
	struct ieee80211_hw *hw;
	struct mwl_pcie_card *card;
	u32 curr_iteration = 0;
	u32 size_fw_downloaded = 0;
	u32 int_code = 0;
	u32 len = 0;
#ifdef SUPPORT_MFG
	u32 fwreadysignature = priv->mfg_mode ?
		MFG_FW_READY_SIGNATURE : HOSTCMD_SOFTAP_FWRDY_SIGNATURE;
#else
	u32 fwreadysignature = HOSTCMD_SOFTAP_FWRDY_SIGNATURE;
#endif

	fw = priv->fw_ucode;
	card = (struct mwl_pcie_card *)priv->intf;
	hw = priv->hw;

	/* FW before jumping to boot rom, it will enable PCIe transaction retry,
	 * wait for boot code to stop it.
	 */
	usleep_range(FW_CHECK_MSECS * 1000, FW_CHECK_MSECS * 2000);

	writel(MACREG_A2HRIC_BIT_MASK,
	       card->iobase1 + MACREG_REG_A2H_INTERRUPT_CLEAR_SEL);
	writel(0x00, card->iobase1 + MACREG_REG_A2H_INTERRUPT_CAUSE);
	writel(0x00, card->iobase1 + MACREG_REG_A2H_INTERRUPT_MASK);
	writel(MACREG_A2HRIC_BIT_MASK,
	       card->iobase1 + MACREG_REG_A2H_INTERRUPT_STATUS_MASK);

	/* this routine interacts with SC2 bootrom to download firmware binary
	 * to the device. After DMA'd to SC2, the firmware could be deflated to
	 * reside on its respective blocks such as ITCM, DTCM, SQRAM,
	 * (or even DDR, AFTER DDR is init'd before fw download
	 */
	wiphy_debug(hw->wiphy, "fw download start\n");

	if (priv->chip_type != MWL8997) {
		/* Disable PFU before FWDL */
		writel(0x100, card->iobase1 + 0xE0E4);
	}

	/* make sure SCRATCH2 C40 is clear, in case we are too quick */
	while (readl(card->iobase1 + 0xc40) == 0)
		cond_resched();

	while (size_fw_downloaded < fw->size) {
		len = readl(card->iobase1 + 0xc40);

		if (!len)
			break;

		/* this copies the next chunk of fw binary to be delivered */
		memcpy((char *)&priv->pcmd_buf[
			INTF_CMDHEADER_LEN(INTF_HEADER_LEN)],
		       (fw->data + size_fw_downloaded), len);

		/* this function writes pdata to c10, then write 2 to c18 */
		mwl_fwdl_trig_pcicmd_bootcode(priv);

		/* this is arbitrary per your platform; we use 0xffff */
		curr_iteration = FW_MAX_NUM_CHECKS;

		/* NOTE: the following back to back checks on C1C is time
		 * sensitive, hence may need to be tweaked dependent on host
		 * processor. Time for SC2 to go from the write of event 2 to
		 * C1C == 2 is ~1300 nSec. Hence the checkings on host has to
		 * consider how efficient your code can be to meet this timing,
		 * or you can alternatively tweak this routines to fit your
		 * platform
		 */
	if (priv->chip_type != MWL8997) {
		do {
			int_code = readl(card->iobase1 + 0xc1c);
			if (int_code != 0)
				break;
			cond_resched();
			curr_iteration--;
		} while (curr_iteration);
	}

		do {
			int_code = readl(card->iobase1 + 0xc1c);
			if ((int_code & MACREG_H2ARIC_BIT_DOOR_BELL) !=
			    MACREG_H2ARIC_BIT_DOOR_BELL)
				break;
			cond_resched();
			curr_iteration--;
		} while (curr_iteration);

		if (curr_iteration == 0) {
			/* This limited loop check allows you to exit gracefully
			 * without locking up your entire system just because fw
			 * download failed
			 */
			wiphy_err(hw->wiphy,
				  "Exhausted curr_iteration for fw download\n");
			goto err_download;
		}

		size_fw_downloaded += len;
	}

	wiphy_debug(hw->wiphy,
		    "FwSize = %d downloaded Size = %d curr_iteration %d\n",
		    (int)fw->size, size_fw_downloaded, curr_iteration);
	/* Now firware is downloaded successfully, so this part is to check
	 * whether fw can properly execute to an extent that write back
	 * signature to indicate its readiness to the host. NOTE: if your
	 * downloaded fw crashes, this signature checking will fail. This
	 * part is similar as SC1
	 */
	*((u32 *)&priv->pcmd_buf[INTF_CMDHEADER_LEN(INTF_HEADER_LEN)+1]) = 0;
	mwl_fwdl_trig_pcicmd(priv);
	curr_iteration = FW_MAX_NUM_CHECKS;

	do {
		curr_iteration--;
		writel(HOSTCMD_SOFTAP_MODE,
			       card->iobase1 + MACREG_REG_GEN_PTR);
			usleep_range(FW_CHECK_MSECS * 1000,
				     FW_CHECK_MSECS * 2000);
			int_code = readl(card->iobase1 + MACREG_REG_INT_CODE);
		if (!(curr_iteration % 0xff) && (int_code != 0))
			wiphy_err(hw->wiphy, "%x;", int_code);
	} while ((curr_iteration) &&
		 (int_code != fwreadysignature));

	if (curr_iteration == 0) {
		wiphy_err(hw->wiphy,
			  "Exhausted curr_iteration for fw signature\n");
		goto err_download;
	}

	wiphy_debug(hw->wiphy, "fw download complete\n");
	writel(0x00, card->iobase1 + MACREG_REG_INT_CODE);

	return 0;

err_download:

	mwl_fwcmd_reset(hw);

	return -EIO;
}

static bool mwl_pcie_check_card_status(struct mwl_priv *priv)
{
        struct mwl_pcie_card *card = (struct mwl_pcie_card *)priv->intf;
		u32 regval;

        regval = readl(card->iobase1 + MACREG_REG_INT_CODE);
	if (regval == 0xffffffff) {
		wiphy_err(priv->hw->wiphy, "adapter does not exist\n");
		return false;
	}

	return true;
}

static void mwl_pcie_enable_int(struct mwl_priv *priv)
{
	struct mwl_pcie_card *card = (struct mwl_pcie_card *)priv->intf;

	if (mwl_pcie_check_card_status(priv)) {
		writel(0x00,
		       card->iobase1 + MACREG_REG_A2H_INTERRUPT_MASK);
		writel(MACREG_A2HRIC_BIT_MASK,
		       card->iobase1 + MACREG_REG_A2H_INTERRUPT_MASK);
	}
}

static void mwl_pcie_disable_int(struct mwl_priv *priv)
{
	struct mwl_pcie_card *card = (struct mwl_pcie_card *)priv->intf;

	if (mwl_pcie_check_card_status(priv))
		writel(0x00,
		       card->iobase1 + MACREG_REG_A2H_INTERRUPT_MASK);
}

static void mwl_pcie_send_command(struct mwl_priv *priv)
{
	struct mwl_pcie_card *card = (struct mwl_pcie_card *)priv->intf;

	writel(priv->pphys_cmd_buf, card->iobase1 + MACREG_REG_GEN_PTR);
	writel(MACREG_H2ARIC_BIT_DOOR_BELL,
	       card->iobase1 + MACREG_REG_H2A_INTERRUPT_EVENTS);
}

/* Check command response back or not */
static int mwl_pcie_cmd_resp_wait_completed(struct mwl_priv *priv,
		unsigned short cmd)
{
	unsigned int curr_iteration = MAX_WAIT_FW_COMPLETE_ITERATIONS;
	unsigned short int_code = 0;

	do {
        usleep_range(250, 500);        
		int_code = le16_to_cpu(*((__le16 *)&priv->pcmd_buf[
				INTF_CMDHEADER_LEN(INTF_HEADER_LEN)+0]));
	} while ((int_code != cmd) && (--curr_iteration));

	if (curr_iteration == 0) {
		wiphy_err(priv->hw->wiphy, "cmd 0x%04x=%s timed out\n",
			  cmd, mwl_fwcmd_get_cmd_string(cmd));
		wiphy_err(priv->hw->wiphy, "return code: 0x%04x\n", int_code);
		return -EIO;
	}

    if (priv->chip_type != MWL8997)
        usleep_range(3000, 5000);

	return 0;
}

static void mwl_pcie_card_reset(struct mwl_priv *priv)
{
	struct mwl_pcie_card *card = (struct mwl_pcie_card *)priv->intf;

	if (mwl_pcie_check_card_status(priv))
		writel(ISR_RESET,
		       card->iobase1 + MACREG_REG_H2A_INTERRUPT_EVENTS);
}
static int mwl_pcie_host_to_card(struct mwl_priv *priv, int desc_num,
		struct sk_buff *tx_skb)
{
	struct mwl_pcie_card *card = priv->intf;
	struct mwl_tx_hndl *tx_hndl = NULL;
	struct mwl_tx_desc *tx_desc;
	struct mwl_tx_ctrl *tx_ctrl;
	struct ieee80211_tx_info *tx_info;

	dma_addr_t dma;
        unsigned int wrindx;
        const unsigned int num_tx_buffs = MLAN_MAX_TXRX_BD << PCIE_TX_START_PTR;
	tx_info = IEEE80211_SKB_CB(tx_skb);
	tx_ctrl = (struct mwl_tx_ctrl *)&IEEE80211_SKB_CB(tx_skb)->status;

	if (!IS_PFU_ENABLED(priv->chip_type)) {
		tx_hndl = priv->desc_data[desc_num].pnext_tx_hndl;
		tx_hndl->psk_buff = tx_skb;
		tx_desc = tx_hndl->pdesc;
	} else {
		struct mwl_tx_pfu_dma_data *dma_data =
			(struct mwl_tx_pfu_dma_data *)tx_skb->data;
		tx_desc = &dma_data->tx_desc;
	}

	if (tx_info->flags & IEEE80211_TX_INTFL_DONT_ENCRYPT) {
		tx_desc->flags |= MWL_TX_WCB_FLAGS_DONT_ENCRYPT;
	}

	if (tx_info->flags & IEEE80211_TX_CTL_NO_CCK_RATE) {
		tx_desc->flags |= MWL_TX_WCB_FLAGS_NO_CCK_RATE;
	}

	tx_desc->tx_priority = tx_ctrl->tx_priority;
	tx_desc->qos_ctrl = cpu_to_le16(tx_ctrl->qos_ctrl);
	tx_desc->pkt_len = cpu_to_le16(tx_skb->len);
	tx_desc->packet_info = 0;
	tx_desc->data_rate = 0;
	tx_desc->type = tx_ctrl->type;
	tx_desc->xmit_control = tx_ctrl->xmit_control;
	tx_desc->sap_pkt_info = 0;
	dma = pci_map_single(card->pdev, tx_skb->data,
			     tx_skb->len, PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(card->pdev, dma)) {
		dev_kfree_skb_any(tx_skb);
		wiphy_err(priv->hw->wiphy,
			  "failed to map pci memory!\n");
		return -ENOMEM;
	}
	
	if (IS_PFU_ENABLED(priv->chip_type))
		tx_desc->pkt_ptr = cpu_to_le32(sizeof(struct mwl_tx_desc));
	else
		tx_desc->pkt_ptr = cpu_to_le32(dma);
	tx_desc->status = cpu_to_le32(EAGLE_TXD_STATUS_FW_OWNED);

	/* make sure all the memory transactions done by cpu were completed */
	wmb();	/*Data Memory Barrier*/
	if (IS_PFU_ENABLED(priv->chip_type)) {
		wrindx = (priv->txbd_wrptr & MLAN_TXBD_MASK) >>
			PCIE_TX_START_PTR;
#if 0
	wiphy_err(priv->hw->wiphy,
	"SEND DATA: Attach pmbuf %p at txbd_wridx=%d\n", tx_skb, wrindx);
#endif
		priv->tx_buf_list[wrindx] = tx_skb;
		priv->txbd_ring[wrindx]->paddr = dma;
	priv->txbd_ring[wrindx]->len = (unsigned short)tx_skb->len;
		priv->txbd_ring[wrindx]->flags = MLAN_BD_FLAG_FIRST_DESC |
			MLAN_BD_FLAG_LAST_DESC;

	priv->txbd_ring[wrindx]->frag_len = (unsigned short)tx_skb->len;
		priv->txbd_ring[wrindx]->offset = 0;
		priv->txbd_wrptr += MLAN_BD_FLAG_TX_START_PTR;

		if ((priv->txbd_wrptr & MLAN_TXBD_MASK) == num_tx_buffs)
			priv->txbd_wrptr = ((priv->txbd_wrptr &
						MLAN_BD_FLAG_TX_ROLLOVER_IND) ^
					MLAN_BD_FLAG_TX_ROLLOVER_IND);

		/* Write the TX ring write pointer in to REG_TXBD_WRPTR */
		writel(priv->txbd_wrptr, card->iobase1 + REG_TXBD_WRPTR);

#if 0
		wiphy_err(priv->hw->wiphy,
		"SEND DATA: Updated <Rd: %#x, Wr: %#x>\n",
				priv->txbd_rdptr, priv->txbd_wrptr);
#endif

#if 0
		if (pcb->moal_read_reg(pmadapter->pmoal_handle,
			REG_TXBD_WRPTR, &txbd_wrptr) != MLAN_STATUS_SUCCESS) {
			wiphy_err(hw->wiphy,
			"SEND DATA: failed to read back REG_TXBD_WRPTR\n");
			ret = MLAN_STATUS_FAILURE;
			goto done_unmap;
		}
		wiphy_err(hw->wiphy,
			"SEND DATA: read back REG_TXBD_WRPTR (0x%x) = 0x%x\n",
				REG_TXBD_WRPTR, txbd_wrptr);

		if (PCIE_TXBD_NOT_FULL(pmadapter->txbd_wrptr,
						pmadapter->txbd_rdptr))
			pmadapter->data_sent = MFALSE;
#endif

	} else {
		writel(MACREG_H2ARIC_BIT_PPA_READY,
	       card->iobase1 + MACREG_REG_H2A_INTERRUPT_EVENTS);
		priv->desc_data[desc_num].pnext_tx_hndl = tx_hndl->pnext;
		priv->fw_desc_cnt[desc_num]++;
	}

	return 0;
}

void mwl_non_pfu_tx_done(unsigned long data)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)data;
	struct mwl_priv *priv = hw->priv;
	struct mwl_pcie_card *card = priv->intf;
	int num;
	struct mwl_desc_data *desc;
	struct mwl_tx_hndl *tx_hndl;
	struct mwl_tx_desc *tx_desc;
	struct sk_buff *done_skb;
	u32 rate;
	struct mwl_dma_data *tr;
	struct ieee80211_tx_info *info;
	struct mwl_tx_ctrl *tx_ctrl;
	struct sk_buff_head *amsdu_pkts;
	int hdrlen;

	spin_lock_bh(&priv->tx_desc_lock);
	for (num = 0; num < SYSADPT_TX_WMM_QUEUES; num++) {
		desc = &priv->desc_data[num];
		tx_hndl = desc->pstale_tx_hndl;
		tx_desc = tx_hndl->pdesc;

		if ((tx_desc->status &
				cpu_to_le32(EAGLE_TXD_STATUS_FW_OWNED)) &&
		    (tx_hndl->pnext->pdesc->status &
		    cpu_to_le32(EAGLE_TXD_STATUS_OK)))
			tx_desc->status = cpu_to_le32(EAGLE_TXD_STATUS_OK);

		while (tx_hndl &&
		       (tx_desc->status & cpu_to_le32(EAGLE_TXD_STATUS_OK)) &&
		       (!(tx_desc->status &
		       cpu_to_le32(EAGLE_TXD_STATUS_FW_OWNED)))) {
			pci_unmap_single(card->pdev,
					 le32_to_cpu(tx_desc->pkt_ptr),
					 le16_to_cpu(tx_desc->pkt_len),
					 PCI_DMA_TODEVICE);
			done_skb = tx_hndl->psk_buff;
			rate = le32_to_cpu(tx_desc->rate_info);
			tx_desc->pkt_ptr = 0;
			tx_desc->pkt_len = 0;
			tx_desc->status =
				cpu_to_le32(EAGLE_TXD_STATUS_IDLE);
			tx_hndl->psk_buff = NULL;
			wmb(); /* memory barrier */

			skb_get(done_skb);
			skb_queue_tail(&priv->delay_q, done_skb);
			if (skb_queue_len(&priv->delay_q) >
			    SYSADPT_DELAY_FREE_Q_LIMIT)
				dev_kfree_skb_any(skb_dequeue(&priv->delay_q));

			tr = (struct mwl_dma_data *)done_skb->data;
			info = IEEE80211_SKB_CB(done_skb);

			if (ieee80211_is_data(tr->wh.frame_control) ||
			    ieee80211_is_data_qos(tr->wh.frame_control)) {
				tx_ctrl = (struct mwl_tx_ctrl *)&info->status;
				amsdu_pkts = (struct sk_buff_head *)
					tx_ctrl->amsdu_pkts;
				if (amsdu_pkts) {
					mwl_tx_ack_amsdu_pkts(hw, rate,
							      amsdu_pkts);
					dev_kfree_skb_any(done_skb);
					done_skb = NULL;
				} else {
					mwl_tx_prepare_info(hw, rate, info);
				}
			} else {
				mwl_tx_prepare_info(hw, 0, info);
			}

			if (done_skb) {
				/* Remove H/W dma header */
				hdrlen = ieee80211_hdrlen(tr->wh.frame_control);
				memmove(tr->data - hdrlen, &tr->wh, hdrlen);
				skb_pull(done_skb, sizeof(*tr) - hdrlen);
				info->flags &= ~IEEE80211_TX_CTL_AMPDU;
				info->flags |= IEEE80211_TX_STAT_ACK;
				ieee80211_tx_status(hw, done_skb);
			}

			tx_hndl = tx_hndl->pnext;
			tx_desc = tx_hndl->pdesc;
			priv->fw_desc_cnt[num]--;
		}

		desc->pstale_tx_hndl = tx_hndl;
	}
	spin_unlock_bh(&priv->tx_desc_lock);

	if (priv->is_tx_done_schedule) {

		set_bit(MACREG_A2HRIC_BIT_TX_DONE,
		(card->iobase1 + MACREG_REG_A2H_INTERRUPT_STATUS_MASK));

		tasklet_schedule(priv->if_ops.ptx_task);
		priv->is_tx_done_schedule = false;
	}

	return;
}

/* XXX: Tx Rate to be indicated to mac80211 - For KF2 PCIe & SDIO,
** driver has no way of knowing the rate at which the pkt was Tx'ed.
** Use hardcoded max value for this
*/
static void mwl_tx_complete_skb(struct sk_buff *done_skb,
		struct _mlan_pcie_data_buf *tx_ring_entry,
		struct ieee80211_hw *hw)
{
	struct mwl_tx_desc *tx_desc;
	struct mwl_priv *priv = hw->priv;
	struct mwl_pcie_card *card = priv->intf;
	struct ieee80211_tx_info *info;
	struct mwl_tx_ctrl *tx_ctrl;
	struct sk_buff_head *amsdu_pkts;
	u32 rate;
	struct mwl_tx_pfu_dma_data *tr =
		(struct mwl_tx_pfu_dma_data *)done_skb->data;
	int hdrlen;

	tx_desc = &tr->tx_desc;

#if 0
wiphy_err(priv->hw->wiphy, "unmap: skb=%p vdata=%p pdata=%p plen=%d!\n",
			done_skb,
			done_skb->data,
			tx_ring_entry->paddr,
			tx_ring_entry->len);
#endif

	pci_unmap_single(card->pdev,
			tx_ring_entry->paddr,
			tx_ring_entry->len,
			PCI_DMA_TODEVICE);

#if 0
	rate = le32_to_cpu(tx_desc->rate_info);
#endif

	tx_desc->pkt_ptr = 0;
	tx_desc->pkt_len = 0;
	tx_desc->status = cpu_to_le32(EAGLE_TXD_STATUS_IDLE);
	wmb(); /* memory barrier */

	skb_get(done_skb);
	skb_queue_tail(&priv->delay_q, done_skb);
	if (skb_queue_len(&priv->delay_q) > SYSADPT_DELAY_FREE_Q_LIMIT)
		dev_kfree_skb_any(skb_dequeue(&priv->delay_q));

	info = IEEE80211_SKB_CB(done_skb);

	if (ieee80211_is_data(tr->wh.frame_control) ||
			ieee80211_is_data_qos(tr->wh.frame_control)) {
		rate = TX_COMP_RATE_FOR_DATA;
		tx_ctrl = (struct mwl_tx_ctrl *)&info->status;
		amsdu_pkts = (struct sk_buff_head *)
			tx_ctrl->amsdu_pkts;
		if (amsdu_pkts) {
			mwl_tx_ack_amsdu_pkts(hw, rate,
					amsdu_pkts);
			dev_kfree_skb_any(done_skb);
			done_skb = NULL;
		} else {
			mwl_tx_prepare_info(hw, rate, info);
		}
	} else {
		mwl_tx_prepare_info(hw, 0, info);
	}

	if (done_skb) {
		/* Remove H/W dma header */
		hdrlen = ieee80211_hdrlen(tr->wh.frame_control);
		memmove(tr->data - hdrlen, &tr->wh, hdrlen);
		skb_pull(done_skb, sizeof(*tr) - hdrlen);
		info->flags &= ~IEEE80211_TX_CTL_AMPDU;
		info->flags |= IEEE80211_TX_STAT_ACK;
		ieee80211_tx_status(hw, done_skb);
	}
}

void mwl_pfu_tx_done(unsigned long data)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)data;
	struct mwl_priv *priv = hw->priv;
	struct mwl_pcie_card *card = priv->intf;
	struct sk_buff *done_skb;
	u32 wrdoneidx, rdptr;
	const unsigned int num_tx_buffs = MLAN_MAX_TXRX_BD << PCIE_TX_START_PTR;

	spin_lock_bh(&priv->tx_desc_lock);

	/* Read the TX ring read pointer set by firmware */
	rdptr = readl(card->iobase1 + REG_TXBD_RDPTR);

#if 0
	wiphy_err(hw->wiphy,  "SEND DATA COMP:  rdptr_prev=0x%x, rdptr=0x%x\n",
		priv->txbd_rdptr, rdptr);
#endif

	/* free from previous txbd_rdptr to current txbd_rdptr */
	while (((priv->txbd_rdptr & MLAN_TXBD_MASK)
			   != (rdptr & MLAN_TXBD_MASK))
		 || ((priv->txbd_rdptr & MLAN_BD_FLAG_TX_ROLLOVER_IND)
			   != (rdptr & MLAN_BD_FLAG_TX_ROLLOVER_IND))) {
		wrdoneidx = priv->txbd_rdptr & MLAN_TXBD_MASK;
		wrdoneidx >>= PCIE_TX_START_PTR;

		done_skb = priv->tx_buf_list[wrdoneidx];
		if (done_skb)
			mwl_tx_complete_skb(done_skb, priv->txbd_ring[wrdoneidx], hw);

		priv->tx_buf_list[wrdoneidx] = MNULL;
		priv->txbd_ring[wrdoneidx]->paddr = 0;
		priv->txbd_ring[wrdoneidx]->len = 0;
		priv->txbd_ring[wrdoneidx]->flags = 0;
		priv->txbd_ring[wrdoneidx]->frag_len = 0;
		priv->txbd_ring[wrdoneidx]->offset = 0;
		priv->txbd_rdptr += MLAN_BD_FLAG_TX_START_PTR;
		if ((priv->txbd_rdptr & MLAN_TXBD_MASK) == num_tx_buffs)
			priv->txbd_rdptr = ((priv->txbd_rdptr &
				  MLAN_BD_FLAG_TX_ROLLOVER_IND) ^
				  MLAN_BD_FLAG_TX_ROLLOVER_IND);
	}

	spin_unlock_bh(&priv->tx_desc_lock);
	 if (priv->is_tx_done_schedule) {

		set_bit(MACREG_A2HRIC_BIT_TX_DONE,
		(card->iobase1 + MACREG_REG_A2H_INTERRUPT_STATUS_MASK));

		tasklet_schedule(priv->if_ops.ptx_task);
		priv->is_tx_done_schedule = false;
	}	
}

void mwl_pcie_tx_done(unsigned long data)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)data;
	struct mwl_priv *priv = hw->priv;

	/* Return all skbs to mac80211 */
	if (IS_PFU_ENABLED(priv->chip_type))
		mwl_pfu_tx_done((unsigned long)hw);
	else
		mwl_non_pfu_tx_done((unsigned long)hw);
}


#ifdef BG4CT_A0_WORKAROUND
#define MAX_ISR_ITERATION 2
#endif
static
irqreturn_t mwl_pcie_isr(int irq, void *dev_id)
{
	struct ieee80211_hw *hw = dev_id;
	struct mwl_priv *priv = hw->priv;
	void __iomem *int_status_mask;
	struct mwl_pcie_card *card = (struct mwl_pcie_card *)priv->intf;
	unsigned int int_status;

	int_status_mask = card->iobase1 +
		MACREG_REG_A2H_INTERRUPT_STATUS_MASK;

	int_status = readl(card->iobase1 +
		MACREG_REG_A2H_INTERRUPT_CAUSE);

	if (int_status == 0x00000000)
		return IRQ_NONE;

	if (int_status == 0xffffffff) {
		wiphy_warn(hw->wiphy, "card unplugged?\n");
	} else {
		writel(~int_status,
		       card->iobase1 + MACREG_REG_A2H_INTERRUPT_CAUSE);

		priv->valid_interrupt_cnt++;

		if (int_status & MACREG_A2HRIC_BIT_TX_DONE) {
			if (!priv->is_tx_done_schedule) {
				clear_bit(MACREG_A2HRIC_BIT_TX_DONE,
				(card->iobase1 + MACREG_REG_A2H_INTERRUPT_STATUS_MASK));


					tasklet_schedule(
						priv->if_ops.ptx_done_task);
				priv->is_tx_done_schedule = true;
			}
		}

		if (int_status & MACREG_A2HRIC_BIT_RX_RDY) {
			if (!priv->is_rx_schedule) {
				clear_bit(MACREG_A2HRIC_BIT_RX_RDY,
					(card->iobase1 + MACREG_REG_A2H_INTERRUPT_STATUS_MASK));
				tasklet_schedule(&priv->rx_task);
				priv->is_rx_schedule = true;
			}
		}

		if (int_status & MACREG_A2HRIC_BIT_RADAR_DETECT) {
			wiphy_info(hw->wiphy, "radar detected by firmware\n");
			ieee80211_radar_detected(hw);
		}

		if (int_status & MACREG_A2HRIC_BIT_QUE_EMPTY) {
			if (!priv->is_qe_schedule) {
				if (time_after(jiffies,
					       (priv->qe_trigger_time + 1))) {
					clear_bit(MACREG_A2HRIC_BIT_QUE_EMPTY,
		(card->iobase1 + MACREG_REG_A2H_INTERRUPT_STATUS_MASK));
						tasklet_schedule(
						    priv->if_ops.pqe_task);
					priv->qe_trigger_num++;
					priv->is_qe_schedule = true;
					priv->qe_trigger_time = jiffies;
				}
			}
		}

		if (int_status & MACREG_A2HRIC_BIT_CHAN_SWITCH)
				ieee80211_queue_work(hw,
					&priv->chnl_switch_handle);

		if (int_status & MACREG_A2HRIC_BA_WATCHDOG)
				ieee80211_queue_work(hw,
					&priv->watchdog_ba_handle);
	}

	return IRQ_HANDLED;
}

static void mwl_pcie_read_register(struct mwl_priv *priv,
		int index, int reg, u32 *data)
{
	struct mwl_pcie_card *card = priv->intf;

	if (index == 0)
		*data = readl(card->iobase0 + reg);
	else
		*data = readl(card->iobase1 + reg);
}

static void mwl_pcie_write_register(struct mwl_priv *priv,
		int index, int reg, u32 data)
{
	struct mwl_pcie_card *card = (struct mwl_pcie_card *)priv->intf;

	if (index == 0)
		writel(data, card->iobase0 + reg);
	else
		writel(data, card->iobase1 + reg);
}

static int mwl_pcie_register_dev(struct mwl_priv *priv)
{
	struct mwl_pcie_card *card;
	int rc = 0;
	int i;

	card = (struct mwl_pcie_card *)priv->intf;

#ifndef NEW_DP
	tasklet_init(priv->if_ops.ptx_task, (void *)mwl_tx_skbs,
		(unsigned long)priv->hw);
	tasklet_disable(priv->if_ops.ptx_task);

	tasklet_init(priv->if_ops.ptx_done_task,
		(void *)mwl_pcie_tx_done, (unsigned long)priv->hw);
	tasklet_disable(priv->if_ops.ptx_done_task);

	tasklet_init(priv->if_ops.pqe_task, (void *)mwl_pcie_tx_flush_amsdu,
		(unsigned long)priv->hw);
	tasklet_disable(priv->if_ops.pqe_task);
#endif
	tasklet_init(&priv->rx_task, (void *)mwl_pcie_rx_recv,
		(unsigned long)priv->hw);
	tasklet_disable(&priv->rx_task);

	if (!IS_PFU_ENABLED(priv->chip_type)) {
		writel(priv->desc_data[0].pphys_tx_ring,
			card->iobase0 + priv->desc_data[0].wcb_base);
		for (i = 1; i < SYSADPT_TOTAL_TX_QUEUES; i++)
			writel(priv->desc_data[i].pphys_tx_ring,
				card->iobase0 + priv->desc_data[i].wcb_base);
	}

	writel(priv->desc_data[0].pphys_rx_ring,
	       card->iobase0 + priv->desc_data[0].rx_desc_read);
	writel(priv->desc_data[0].pphys_rx_ring,
	       card->iobase0 + priv->desc_data[0].rx_desc_write);

	return rc;
}

static void mwl_pcie_unregister_dev(struct mwl_priv *priv)
{
	if (priv->irq != -1) {
		free_irq(priv->irq, priv->hw);
		priv->irq = -1;
	}
#ifndef NEW_DP
	if (priv->if_ops.ptx_task != NULL)
		tasklet_kill(priv->if_ops.ptx_task);

	if (priv->if_ops.ptx_done_task != NULL)
		tasklet_kill(priv->if_ops.ptx_done_task);

	if (priv->if_ops.pqe_task != NULL)
		tasklet_kill(priv->if_ops.pqe_task);
#endif /* NEW_DP */
}

static void mwl_pcie_tx_flush_amsdu(unsigned long data)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)data;
	struct mwl_priv *priv = hw->priv;
	struct mwl_pcie_card *card = (struct mwl_pcie_card *)priv->intf;

	struct mwl_sta *sta_info;
	int i;
	struct mwl_amsdu_frag *amsdu_frag;

// TODO: RR: Take spin_lock_bh() here ??
	spin_lock(&priv->tx_desc_lock);
	spin_lock(&priv->sta_lock);
	list_for_each_entry(sta_info, &priv->sta_list, list) {
		spin_lock(&sta_info->amsdu_lock);
		for (i = 0; i < SYSADPT_TX_WMM_QUEUES; i++) {
			amsdu_frag = &sta_info->amsdu_ctrl.frag[i];
			if (amsdu_frag->num) {
				if (time_after(jiffies,
					       (amsdu_frag->jiffies + 1))) {
					if (mwl_pcie_is_tx_available(priv, i)) {
						/* wiphy_err(priv->hw->wiphy,
						* "%s()\n", __FUNCTION__);
						*/
						mwl_tx_skb(priv, i,
							   amsdu_frag->skb);
						amsdu_frag->num = 0;
						amsdu_frag->cur_pos = NULL;
					}
				}
			}
		}
		spin_unlock(&sta_info->amsdu_lock);
	}
	spin_unlock(&priv->sta_lock);
	spin_unlock(&priv->tx_desc_lock);

	set_bit(MACREG_A2HRIC_BIT_QUE_EMPTY,
		(card->iobase1 + MACREG_REG_A2H_INTERRUPT_STATUS_MASK));

	priv->is_qe_schedule = false;
}

static int mwl_pcie_dbg_info(struct mwl_priv *priv, char *p, int size, int len)
{
	struct mwl_pcie_card *card = (struct mwl_pcie_card *)priv->intf;
	len += scnprintf(p + len, size - len, "irq number: %d\n", priv->irq);
	len += scnprintf(p + len, size - len, "iobase0: %p\n", card->iobase0);
	len += scnprintf(p + len, size - len, "iobase1: %p\n", card->iobase1);

	return len;
}

static int mwl_pcie_debugfs_reg_access(struct mwl_priv *priv, bool write)
{
	struct ieee80211_hw *hw = priv->hw;
	struct mwl_pcie_card *card = (struct mwl_pcie_card *)priv->intf;
	u8 set;
	u32 *addr_val;
	int ret = 0;

	set = write ? WL_SET : WL_GET;

	switch (priv->reg_type) {
	case MWL_ACCESS_ADDR0:
		if (set == WL_GET)
			priv->reg_value =
				readl(card->iobase0 + priv->reg_offset);
		else
			writel(priv->reg_value,
			       card->iobase0 + priv->reg_offset);
		break;
	case MWL_ACCESS_ADDR1:
		if (set == WL_GET)
			priv->reg_value =
				readl(card->iobase1 + priv->reg_offset);
		else
			writel(priv->reg_value,
			       card->iobase1 + priv->reg_offset);
		break;
	case MWL_ACCESS_ADDR:
		addr_val = kmalloc(64 * sizeof(u32), GFP_KERNEL);
		if (addr_val) {
			memset(addr_val, 0, 64 * sizeof(u32));
			addr_val[0] = priv->reg_value;
			ret = mwl_fwcmd_get_addr_value(hw, priv->reg_offset,
						       4, addr_val, set);
			if ((!ret) && (set == WL_GET))
				priv->reg_value = addr_val[0];
			kfree(addr_val);
		} else {
			ret = -ENOMEM;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static struct tasklet_struct tx_task;
static struct tasklet_struct tx_done_task;
static struct tasklet_struct qe_task;

static struct mwl_if_ops pcie_ops = {
	.inttf_head_len = INTF_HEADER_LEN,
	.ptx_task = &tx_task,
	.ptx_done_task = &tx_done_task,
	.pqe_task = &qe_task,
	.init_if =			mwl_pcie_init,
	.cleanup_if =		mwl_pcie_cleanup,
	.check_card_status =		mwl_pcie_check_card_status,
	.prog_fw =			mwl_pcie_program_firmware,
	.enable_int =		mwl_pcie_enable_int,
	.disable_int =  mwl_pcie_disable_int,
	.send_cmd =     mwl_pcie_send_command,
	.cmd_resp_wait_completed = mwl_pcie_cmd_resp_wait_completed,
	.card_reset =   mwl_pcie_card_reset,
	.register_dev =			mwl_pcie_register_dev,
	.unregister_dev =		mwl_pcie_unregister_dev,
	.is_tx_available = mwl_pcie_is_tx_available,
	.host_to_card =			mwl_pcie_host_to_card,
	.read_reg = mwl_pcie_read_register,
	.write_reg = mwl_pcie_write_register,
	.tx_done = mwl_pcie_tx_done,
	.dbg_info = mwl_pcie_dbg_info,
	.dbg_reg_access = mwl_pcie_debugfs_reg_access,
};


static int mwl_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static bool printed_version;
	struct mwl_pcie_card *card;
	int rc = 0;

	if (id->driver_data >= MWLUNKNOWN)
		return -ENODEV;

	if (!printed_version) {
		pr_info("<<%s version %s>>",
			LRD_DESC, LRD_DRV_VERSION);
		printed_version = true;
	}

	card = kzalloc(sizeof(struct mwl_pcie_card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->chip_type = id->driver_data;
	card->pdev = pdev;
	memcpy(&pcie_ops.mwl_chip_tbl, &mwl_chip_tbl[card->chip_type],
		sizeof(struct mwl_chip_info));
	rc = mwl_add_card(card, &pcie_ops);
	if (rc) {
		pr_err("%s: add card failed", MWL_DRV_NAME);
		rc = -EIO;
		goto err_add_card;
	}
	return rc;

err_add_card:
	kfree(card);
	return rc;

}

static void mwl_remove(struct pci_dev *pdev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct mwl_priv *priv;

	if (!hw)
		return;

	priv = hw->priv;

	mwl_wl_deinit(priv);

	mwl_pcie_cleanup(priv);
	mwl_pcie_unregister_dev(priv);

	ieee80211_free_hw(hw);

#if 0
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
#endif

}

#if 0
static u32 pci_read_mac_reg(struct mwl_priv *priv, u32 offset)
{
	if (priv->chip_type == MWL8964) {
		u32 *addr_val = kmalloc(64 * sizeof(u32), GFP_ATOMIC);
		u32 val;

		if (addr_val) {
			mwl_fwcmd_get_addr_value(priv->hw,
						 0x8000a000 + offset, 4,
						 addr_val, 0);
			val = addr_val[0];
			kfree(addr_val);
			return val;
		}
		return 0;
	} else
		return le32_to_cpu(*(__le32 * __force)
		       (MAC_REG_ADDR_PCI(offset)));
}
#endif


static struct pci_driver mwl_pci_driver = {
	.name     = MWL_DRV_NAME,
	.id_table = mwl_pci_id_tbl,
	.probe    = mwl_probe,
	.remove   = mwl_remove,
};

module_pci_driver(mwl_pci_driver);
MODULE_DEVICE_TABLE(pci, mwl_pci_id_tbl);


MODULE_DESCRIPTION(LRD_PCIE_DESC);
MODULE_VERSION(LRD_PCIE_VERSION);
MODULE_AUTHOR(LRD_AUTHOR);
MODULE_LICENSE("GPL v2");


