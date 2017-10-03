/*
 * Copyright (C) 2006-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/* Description:  This file implements transmit related functions. */

#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "sysadpt.h"
#include "dev.h"
#include "fwcmd.h"
#include "tx.h"


#define EAGLE_TXD_XMITCTRL_USE_MC_RATE     0x8     /* Use multicast data rate */

#define MWL_QOS_ACK_POLICY_MASK	           0x0060
#define MWL_QOS_ACK_POLICY_NORMAL          0x0000
#define MWL_QOS_ACK_POLICY_BLOCKACK        0x0060

#define EXT_IV                             0x20
#define INCREASE_IV(iv16, iv32) \
{ \
	(iv16)++; \
	if ((iv16) == 0) \
		(iv32)++; \
}

enum {
	IEEE_TYPE_MANAGEMENT = 0,
	IEEE_TYPE_CONTROL,
	IEEE_TYPE_DATA
};

struct ccmp_hdr {
	__le16 iv16;
	u8 rsvd;
	u8 key_id;
	__le32 iv32;
} __packed;


static inline void mwl_tx_add_dma_header(struct mwl_priv *priv,
					 struct sk_buff *skb,
					 int head_pad,
					 int tail_pad)
{
	struct ieee80211_hdr *wh, *tr_wh_ptr;
	int hdrlen;
	int reqd_hdrlen, dma_hdr_len;
	__le16 *tr_fwlen_ptr;

 	dma_hdr_len = (((priv->host_if == MWL_IF_PCIE) && 
		IS_PFU_ENABLED(priv->chip_type)) ?
		sizeof(struct mwl_tx_pfu_dma_data) :
		sizeof(struct mwl_dma_data));

	/* Add a firmware DMA header; the firmware requires that we
	 * present a 2-byte payload length followed by a 4-address
	 * header (without QoS field), followed (optionally) by any
	 * WEP/ExtIV header (but only filled in for CCMP).
	 */
	wh = (struct ieee80211_hdr *)skb->data;

	hdrlen = ieee80211_hdrlen(wh->frame_control);
	/*wiphy_err(priv->hw->wiphy,"%s() hdrlen=%d\n",__FUNCTION__,hdrlen);*/

	reqd_hdrlen = dma_hdr_len + head_pad;
	/* wiphy_err(priv->hw->wiphy, "reqd_hdrlen=%d\n", reqd_hdrlen); */

	if (hdrlen != reqd_hdrlen)
		skb_push(skb, reqd_hdrlen - hdrlen);

	if (ieee80211_is_data_qos(wh->frame_control)) {
		hdrlen -= IEEE80211_QOS_CTL_LEN;

		if (ieee80211_has_order(wh->frame_control))
			hdrlen -= IEEE80211_HT_CTL_LEN;
	}

	if (((priv->host_if == MWL_IF_PCIE) && 
		IS_PFU_ENABLED(priv->chip_type))) {
		struct mwl_tx_pfu_dma_data *tr =
			(struct mwl_tx_pfu_dma_data *)skb->data;

		tr_wh_ptr = &tr->wh;
		tr_fwlen_ptr = &tr->fwlen;
	} else {
		struct mwl_dma_data *tr = (struct mwl_dma_data *)skb->data;

		tr_wh_ptr = &tr->wh;
		tr_fwlen_ptr = &tr->fwlen;
	}

	if (wh != tr_wh_ptr)
		memmove(tr_wh_ptr, wh, hdrlen);

	if (hdrlen != sizeof(struct ieee80211_hdr))
		memset(((void *)tr_wh_ptr) + hdrlen, 0,
			sizeof(struct ieee80211_hdr) - hdrlen);

	/* Firmware length is the length of the fully formed "802.11
	 * payload".  That is, everything except for the 802.11 header.
	 * This includes all crypto material including the MIC.
	 */
	*tr_fwlen_ptr = cpu_to_le16(skb->len - dma_hdr_len + tail_pad);
}

static inline void mwl_tx_encapsulate_frame(struct mwl_priv *priv,
					    struct sk_buff *skb,
					    struct ieee80211_key_conf *k_conf,
					    bool *ccmp)
{
	int head_pad = 0;
	int data_pad = 0;

	/* Make sure the packet header is in the DMA header format (4-address
	 * without QoS), and add head & tail padding when HW crypto is enabled.
	 *
	 * We have the following trailer padding requirements:
	 * - WEP: 4 trailer bytes (ICV)
	 * - TKIP: 12 trailer bytes (8 MIC + 4 ICV)
	 * - CCMP: 8 trailer bytes (MIC)
	 */

	if (k_conf) {
		head_pad = k_conf->iv_len;

		switch (k_conf->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			data_pad = 4;
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			data_pad = 12;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			data_pad = 8;
			*ccmp = true;
			break;
		}
	}

	mwl_tx_add_dma_header(priv, skb, head_pad, data_pad);
}

static inline void mwl_tx_insert_ccmp_hdr(u8 *pccmp_hdr,
				  u8 key_id, u16 iv16, u32 iv32)
{
	struct ccmp_hdr *ccmp_h = (struct ccmp_hdr *)pccmp_hdr;

	ccmp_h->iv16 = cpu_to_le16(iv16);
	ccmp_h->rsvd = 0;
	ccmp_h->key_id = EXT_IV | (key_id << 6);
	ccmp_h->iv32 = cpu_to_le32(iv32);
}

static inline int mwl_tx_tid_queue_mapping(u8 tid)
{
	switch (tid) {
	case 0:
	case 3:
		return IEEE80211_AC_BE;
	case 1:
	case 2:
		return IEEE80211_AC_BK;
	case 4:
	case 5:
		return IEEE80211_AC_VI;
	case 6:
	case 7:
		return IEEE80211_AC_VO;
	default:
		break;
	}

	return -1;
}

static inline void mwl_tx_add_basic_rates(int band, struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt;
	int len;
	u8 *pos;

	mgmt = (struct ieee80211_mgmt *)skb->data;
	len = skb->len - ieee80211_hdrlen(mgmt->frame_control);
	len -= 4;
	pos = (u8 *)cfg80211_find_ie(WLAN_EID_SUPP_RATES,
				     mgmt->u.assoc_req.variable,
				     len);
	if (pos) {
		pos++;
		len = *pos++;
		while (len) {
			if (band == NL80211_BAND_2GHZ) {
				if ((*pos == 2) || (*pos == 4) ||
				    (*pos == 11) || (*pos == 22))
					*pos |= 0x80;
			} else {
				if ((*pos == 12) || (*pos == 24) ||
				    (*pos == 48))
					*pos |= 0x80;
			}
			pos++;
			len--;
		}
	}
}

static inline void mwl_tx_set_cap_2040_coex(__le16 fc, struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt;
	int len;
	u8 *pos, *ie_start;

	mgmt = (struct ieee80211_mgmt *)skb->data;
	len = skb->len - ieee80211_hdrlen(fc);
	len -= 4;

	if (ieee80211_is_reassoc_req(fc)) {
		len -= 6; /* Current AP addr */
		ie_start = mgmt->u.reassoc_req.variable;
	} else
		ie_start = mgmt->u.assoc_req.variable;

	pos = (u8 *)cfg80211_find_ie(WLAN_EID_EXT_CAPABILITY, ie_start, len);

	if (pos) {
		pos++;
		len = *pos++;
		if(len)
			pos[0] |= BIT(0);	/* Set 20/40 coex support */
	}

}

static inline void mwl_tx_count_packet(struct ieee80211_sta *sta, u8 tid)
{
	struct mwl_sta *sta_info;
	struct mwl_tx_info *tx_stats;

	if (WARN_ON(tid >= SYSADPT_MAX_TID))
		return;

	sta_info = mwl_dev_get_sta(sta);

	tx_stats = &sta_info->tx_stats[tid];

	if (tx_stats->start_time == 0)
		tx_stats->start_time = jiffies;

	/* reset the packet count after each second elapses.  If the number of
	 * packets ever exceeds the ampdu_min_traffic threshold, we will allow
	 * an ampdu stream to be started.
	 */
	if (jiffies - tx_stats->start_time > HZ) {
		tx_stats->pkts = 0;
		tx_stats->start_time = jiffies;
	} else {
		tx_stats->pkts++;
	}
}

static inline bool mwl_tx_available(struct mwl_priv *priv, int desc_num)
{
	  return priv->if_ops.is_tx_available(priv, desc_num);
}

inline void mwl_tx_skb(struct mwl_priv *priv, int desc_num,
			      struct sk_buff *tx_skb)
{
	struct ieee80211_tx_info *tx_info;
	struct mwl_tx_ctrl *tx_ctrl;
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif;
	struct mwl_vif *mwl_vif;
	struct ieee80211_key_conf *k_conf;
	bool ccmp = false;
	struct ieee80211_hdr *wh;
	char *dma_data_pload;

	if (WARN_ON(!tx_skb))
		return;

//	wiphy_err(priv->hw->wiphy, "%s() called\n", __FUNCTION__);

	tx_info = IEEE80211_SKB_CB(tx_skb);
	tx_ctrl = (struct mwl_tx_ctrl *)&tx_info->status;
	sta = (struct ieee80211_sta *)tx_ctrl->sta;
	vif = (struct ieee80211_vif *)tx_ctrl->vif;
	mwl_vif = mwl_dev_get_vif(vif);
	k_conf = (struct ieee80211_key_conf *)tx_ctrl->k_conf;

	mwl_tx_encapsulate_frame(priv, tx_skb, k_conf, &ccmp);


	if (((priv->host_if == MWL_IF_PCIE) && 
		IS_PFU_ENABLED(priv->chip_type))) {
		struct mwl_tx_pfu_dma_data *dma_data =
			(struct mwl_tx_pfu_dma_data *)tx_skb->data;

		wh = &dma_data->wh;
		dma_data_pload = dma_data->data;
	} else {
		struct mwl_dma_data *dma_data =
			(struct mwl_dma_data *)tx_skb->data;

		wh = &dma_data->wh;
		dma_data_pload = dma_data->data;
	}

	if (ieee80211_is_data(wh->frame_control) ||
	    (ieee80211_is_mgmt(wh->frame_control) &&
	    ieee80211_has_protected(wh->frame_control) &&
	    !is_multicast_ether_addr(wh->addr1))) {
		if (is_multicast_ether_addr(wh->addr1)) {
			if (ccmp) {
				mwl_tx_insert_ccmp_hdr(dma_data_pload,
						       mwl_vif->keyidx,
						       mwl_vif->iv16,
						       mwl_vif->iv32);
				INCREASE_IV(mwl_vif->iv16, mwl_vif->iv32);
			}
		} else {
			if (ccmp) {
				if ((vif->type == NL80211_IFTYPE_STATION) ||
					(vif->type == NL80211_IFTYPE_P2P_CLIENT)) {
					mwl_tx_insert_ccmp_hdr(dma_data_pload,
							       mwl_vif->keyidx,
							       mwl_vif->iv16,
							       mwl_vif->iv32);
					INCREASE_IV(mwl_vif->iv16,
						    mwl_vif->iv32);
				} else {
					struct mwl_sta *sta_info;

					sta_info = mwl_dev_get_sta(sta);

					mwl_tx_insert_ccmp_hdr(dma_data_pload,
							       0,
							       sta_info->iv16,
							       sta_info->iv32);
					INCREASE_IV(sta_info->iv16,
						    sta_info->iv32);
				}
			}
		}
	}
	priv->if_ops.host_to_card(priv, desc_num, tx_skb);
}
EXPORT_SYMBOL_GPL(mwl_tx_skb);

static inline struct sk_buff *mwl_tx_do_amsdu(struct mwl_priv *priv,
					int desc_num,
					struct sk_buff *tx_skb,
					struct ieee80211_tx_info *tx_info)
{
	struct ieee80211_sta *sta;
	struct mwl_sta *sta_info;
	struct mwl_tx_ctrl *tx_ctrl = (struct mwl_tx_ctrl *)&tx_info->status;
	struct ieee80211_tx_info *amsdu_info;
	struct sk_buff_head *amsdu_pkts;
	struct mwl_amsdu_frag *amsdu;
	int amsdu_allow_size;
	struct ieee80211_hdr *wh;
	int wh_len;
	u16 len;
	u8 *data;

	sta = (struct ieee80211_sta *)tx_ctrl->sta;
	sta_info = mwl_dev_get_sta(sta);

	if (!sta_info->is_amsdu_allowed)
		return tx_skb;

	wh = (struct ieee80211_hdr *)tx_skb->data;
	if (sta_info->is_mesh_node && is_multicast_ether_addr(wh->addr3))
		return tx_skb;

	if (sta_info->amsdu_ctrl.cap == MWL_AMSDU_SIZE_4K)
		amsdu_allow_size = SYSADPT_AMSDU_4K_MAX_SIZE;
	else if (sta_info->amsdu_ctrl.cap == MWL_AMSDU_SIZE_8K)
		amsdu_allow_size = SYSADPT_AMSDU_8K_MAX_SIZE;
	else
		return tx_skb;

	spin_lock_bh(&sta_info->amsdu_lock);
	amsdu = &sta_info->amsdu_ctrl.frag[desc_num];

//	wiphy_err(priv->hw->wiphy, "[1]\n");
	if (tx_skb->len > SYSADPT_AMSDU_ALLOW_SIZE) {
		if (amsdu->num) {
			spin_unlock_bh(&sta_info->amsdu_lock);
			mwl_tx_skb(priv, desc_num, amsdu->skb);
			spin_lock_bh(&sta_info->amsdu_lock);
			amsdu->num = 0;
			amsdu->cur_pos = NULL;
		}
		spin_unlock_bh(&sta_info->amsdu_lock);
		return tx_skb;
	}

	/* potential amsdu size, should add amsdu header 14 bytes +
	 * maximum padding 3.
	 */
	wh_len = ieee80211_hdrlen(wh->frame_control);
	len = tx_skb->len - wh_len + 17;

	/*pr_alert("(2)len=%d a->num=%d\n",tx_skb->len,amsdu->num);*/
	if (amsdu->num) {
		if ((amsdu->skb->len + len) > amsdu_allow_size) {
			spin_unlock_bh(&sta_info->amsdu_lock);
			mwl_tx_skb(priv, desc_num, amsdu->skb);
			spin_lock_bh(&sta_info->amsdu_lock);
			amsdu->num = 0;
			amsdu->cur_pos = NULL;
		}
	}

	amsdu->jiffies = jiffies;
	len = tx_skb->len - wh_len;

	if (amsdu->num == 0) {
		struct sk_buff *newskb;
		int headroom;

		amsdu_pkts = (struct sk_buff_head *)
			kmalloc(sizeof(*amsdu_pkts), GFP_ATOMIC);
		if (!amsdu_pkts) {
			spin_unlock_bh(&sta_info->amsdu_lock);
			return tx_skb;
		}
		newskb = dev_alloc_skb(amsdu_allow_size +
				       SYSADPT_TX_MIN_BYTES_HEADROOM);
		if (!newskb) {
			spin_unlock_bh(&sta_info->amsdu_lock);
			kfree(amsdu_pkts);
			wiphy_err(priv->hw->wiphy, "Failed to allocate skb for amsdu pkt\n");
			return tx_skb;
		}

		headroom = skb_headroom(newskb);
		if (headroom < SYSADPT_TX_MIN_BYTES_HEADROOM) {
		/* wiphy_err(priv->hw->wiphy,
		* "INCR reserve hroom %d -> %d\n",
		* headroom, SYSADPT_TX_MIN_BYTES_HEADROOM);
		*/
			skb_reserve(newskb,
				(SYSADPT_TX_MIN_BYTES_HEADROOM - headroom));
		}

		data = newskb->data;
		memcpy(data, tx_skb->data, wh_len);
		if (sta_info->is_mesh_node) {
			ether_addr_copy(data + wh_len, wh->addr3);
			ether_addr_copy(data + wh_len + ETH_ALEN, wh->addr4);
		} else {
			ether_addr_copy(data + wh_len,
					ieee80211_get_DA(wh));
			ether_addr_copy(data + wh_len + ETH_ALEN,
					ieee80211_get_SA(wh));
		}
		*(u8 *)(data + wh_len + ETH_HLEN - 1) = len & 0xff;
		*(u8 *)(data + wh_len + ETH_HLEN - 2) = (len >> 8) & 0xff;
		memcpy(data + wh_len + ETH_HLEN, tx_skb->data + wh_len, len);

		skb_put(newskb, tx_skb->len + ETH_HLEN);
		tx_ctrl->qos_ctrl |= IEEE80211_QOS_CTL_A_MSDU_PRESENT;
		amsdu_info = IEEE80211_SKB_CB(newskb);
		memcpy(amsdu_info, tx_info, sizeof(*tx_info));
		skb_queue_head_init(amsdu_pkts);
		((struct mwl_tx_ctrl *)&amsdu_info->status)->amsdu_pkts =
			(void *)amsdu_pkts;
		amsdu->skb = newskb;
	} else {
		amsdu->cur_pos += amsdu->pad;
		data = amsdu->cur_pos;

		if (sta_info->is_mesh_node) {
			ether_addr_copy(data, wh->addr3);
			ether_addr_copy(data + ETH_ALEN, wh->addr4);
		} else {
			ether_addr_copy(data, ieee80211_get_DA(wh));
			ether_addr_copy(data + ETH_ALEN, ieee80211_get_SA(wh));
		}
		*(u8 *)(data + ETH_HLEN - 1) = len & 0xff;
		*(u8 *)(data + ETH_HLEN - 2) = (len >> 8) & 0xff;
		memcpy(data + ETH_HLEN, tx_skb->data + wh_len, len);

		skb_put(amsdu->skb, len + ETH_HLEN + amsdu->pad);
		amsdu_info = IEEE80211_SKB_CB(amsdu->skb);
		amsdu_pkts = (struct sk_buff_head *)
			((struct mwl_tx_ctrl *)&amsdu_info->status)->amsdu_pkts;
	}

	amsdu->num++;
	amsdu->pad = ((len + ETH_HLEN) % 4) ? (4 - (len + ETH_HLEN) % 4) : 0;
	amsdu->cur_pos = amsdu->skb->data + amsdu->skb->len;
	skb_queue_tail(amsdu_pkts, tx_skb);

	if (amsdu->num > SYSADPT_AMSDU_PACKET_THRESHOLD) {
		amsdu->num = 0;
		amsdu->cur_pos = NULL;
		spin_unlock_bh(&sta_info->amsdu_lock);
		return amsdu->skb;
	}
	/* amsdu_tcp */
	if ((priv->if_ops.flush_amsdu != NULL)
		&& (amsdu->num > 1)) {
		priv->if_ops.flush_amsdu((unsigned long)priv->hw);
	}

	spin_unlock_bh(&sta_info->amsdu_lock);
	return NULL;
}


inline void mwl_tx_prepare_info(struct ieee80211_hw *hw, u32 rate,
				       struct ieee80211_tx_info *info)
{
	u32 format, bandwidth, short_gi, rate_id, nss;

	ieee80211_tx_info_clear_status(info);

	info->status.rates[0].idx = -1;
	info->status.rates[0].count = 0;
	info->status.rates[0].flags = 0;

	if (rate) {
		/* Prepare rate information */
		format = rate & MWL_TX_RATE_FORMAT_MASK;
		bandwidth =
			(rate & MWL_TX_RATE_BANDWIDTH_MASK) >>
			MWL_TX_RATE_BANDWIDTH_SHIFT;
		short_gi = (rate & MWL_TX_RATE_SHORTGI_MASK) >>
			MWL_TX_RATE_SHORTGI_SHIFT;
		rate_id = (rate & MWL_TX_RATE_RATEIDMCS_MASK) >>
			MWL_TX_RATE_RATEIDMCS_SHIFT;
		nss = (rate & MWL_TX_RATE_NSS_MASK) >>
			MWL_TX_RATE_NSS_SHIFT;

		info->status.rates[0].idx = rate_id;
		if (format == TX_RATE_FORMAT_LEGACY) {
			if (hw->conf.chandef.chan->hw_value >
			    BAND_24_CHANNEL_NUM) {
				info->status.rates[0].idx -= 5;
			}
		} else if (format == TX_RATE_FORMAT_11AC) {
			ieee80211_rate_set_vht(
				&info->status.rates[0], rate_id, nss);
		}

		if (format == TX_RATE_FORMAT_11N)
			info->status.rates[0].flags |=
				IEEE80211_TX_RC_MCS;
		if (format == TX_RATE_FORMAT_11AC)
			info->status.rates[0].flags |=
				IEEE80211_TX_RC_VHT_MCS;
		if (bandwidth == TX_RATE_BANDWIDTH_40)
			info->status.rates[0].flags |=
				IEEE80211_TX_RC_40_MHZ_WIDTH;
		if (bandwidth == TX_RATE_BANDWIDTH_80)
			info->status.rates[0].flags |=
				IEEE80211_TX_RC_80_MHZ_WIDTH;
		if (bandwidth == TX_RATE_BANDWIDTH_160)
			info->status.rates[0].flags |=
				IEEE80211_TX_RC_160_MHZ_WIDTH;
		if (short_gi == TX_RATE_INFO_SHORT_GI)
			info->status.rates[0].flags |=
				IEEE80211_TX_RC_SHORT_GI;
		info->status.rates[0].count = 1;
		info->status.rates[1].idx = -1;
	}
}
EXPORT_SYMBOL_GPL(mwl_tx_prepare_info);

inline void mwl_tx_ack_amsdu_pkts(struct ieee80211_hw *hw, u32 rate,
					 struct sk_buff_head *amsdu_pkts)
{
	struct sk_buff *amsdu_pkt;
	struct ieee80211_tx_info *info;

//	wiphy_err(hw->wiphy, "Free amsdu #%d\n", skb_queue_len(amsdu_pkts));
	while (skb_queue_len(amsdu_pkts) > 0) {
		amsdu_pkt = skb_dequeue(amsdu_pkts);
		info = IEEE80211_SKB_CB(amsdu_pkt);
		mwl_tx_prepare_info(hw, rate, info);
		info->flags &= ~IEEE80211_TX_CTL_AMPDU;
		info->flags |= IEEE80211_TX_STAT_ACK;
 //wiphy_err(hw->wiphy, "fr_data_skb(a)=%p\n", amsdu_pkt);
		ieee80211_tx_status(hw, amsdu_pkt);
	}

	kfree(amsdu_pkts);
}
EXPORT_SYMBOL_GPL(mwl_tx_ack_amsdu_pkts);

void mwl_tx_xmit(struct ieee80211_hw *hw,
		 struct ieee80211_tx_control *control,
		 struct sk_buff *skb)
{
	struct mwl_priv *priv = hw->priv;
	int index;
	struct ieee80211_sta *sta;
	struct ieee80211_tx_info *tx_info;
	struct mwl_vif *mwl_vif;
	struct ieee80211_hdr *wh;
	u8 xmitcontrol;
	u16 qos;
	int txpriority;
	u8 tid = 0;
	struct mwl_ampdu_stream *stream = NULL;
	bool start_ba_session = false;
	bool mgmtframe = false;
	struct ieee80211_mgmt *mgmt;
	bool eapol_frame = false;
	struct mwl_tx_ctrl *tx_ctrl;
	struct ieee80211_key_conf *k_conf = NULL;

	index = skb_get_queue_mapping(skb);
	sta = control->sta;

	wh = (struct ieee80211_hdr *)skb->data;

	if (ieee80211_is_data_qos(wh->frame_control))
		qos = *((u16 *)ieee80211_get_qos_ctl(wh));
	else
		qos = 0;

	if (skb->protocol == cpu_to_be16(ETH_P_PAE)) {
		index = IEEE80211_AC_VO;
		eapol_frame = true;
	}

	if (ieee80211_is_mgmt(wh->frame_control)) {
		mgmtframe = true;
		mgmt = (struct ieee80211_mgmt *)skb->data;
	}

	if (ieee80211_is_mgmt(wh->frame_control))
		priv->tx_mgmt_cnt++;
	else
		priv->tx_data_cnt++;

	tx_info = IEEE80211_SKB_CB(skb);
	mwl_vif = mwl_dev_get_vif(tx_info->control.vif);

	if (tx_info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ) {
		if (tx_info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT)
			mwl_vif->seqno += 0x10;

		wh->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
		wh->seq_ctrl |= cpu_to_le16(mwl_vif->seqno);
	}

	/* Setup firmware control bit fields for each frame type. */
	xmitcontrol = 0;

	if (mgmtframe || ieee80211_is_ctl(wh->frame_control)) {
		qos = 0;
	} else if (ieee80211_is_data(wh->frame_control)) {
		qos &= ~MWL_QOS_ACK_POLICY_MASK;

		if (tx_info->flags & IEEE80211_TX_CTL_AMPDU) {
			xmitcontrol &= 0xfb;
			qos |= MWL_QOS_ACK_POLICY_BLOCKACK;
		} else {
			xmitcontrol |= 0x4;
			qos |= MWL_QOS_ACK_POLICY_NORMAL;
		}

		if (is_multicast_ether_addr(wh->addr1) || eapol_frame)
			xmitcontrol |= EAGLE_TXD_XMITCTRL_USE_MC_RATE;
	}

	k_conf = tx_info->control.hw_key;

	/* Queue ADDBA request in the respective data queue.  While setting up
	 * the ampdu stream, mac80211 queues further packets for that
	 * particular ra/tid pair.  However, packets piled up in the hardware
	 * for that ra/tid pair will still go out. ADDBA request and the
	 * related data packets going out from different queues asynchronously
	 * will cause a shift in the receiver window which might result in
	 * ampdu packets getting dropped at the receiver after the stream has
	 * been setup.
	 */
	if (mgmtframe) {
		u16 capab;

		if (unlikely(ieee80211_is_action(wh->frame_control) &&
			     mgmt->u.action.category == WLAN_CATEGORY_BACK &&
			     mgmt->u.action.u.addba_req.action_code ==
			     WLAN_ACTION_ADDBA_REQ)) {
			capab = le16_to_cpu(mgmt->u.action.u.addba_req.capab);
			tid = (capab & IEEE80211_ADDBA_PARAM_TID_MASK) >> 2;
			index = mwl_tx_tid_queue_mapping(tid);
		}

		if (unlikely(ieee80211_is_assoc_req(wh->frame_control)))
			mwl_tx_add_basic_rates(hw->conf.chandef.chan->band,
					       skb);

		/* XXX: WAR - Currently it's not clear where/how to inform to
		** to upper layer to set 20/40 coex capability. Set it in 
		** driver for now
		*/
		if (unlikely((ieee80211_is_assoc_req(wh->frame_control) ||
			ieee80211_is_reassoc_req(wh->frame_control)) &&
			(hw->conf.chandef.chan->band == NL80211_BAND_2GHZ))) {
			wiphy_err(hw->wiphy, "Setting 20/40 coex cap\n");
			mwl_tx_set_cap_2040_coex(wh->frame_control, skb);
		}
	}

	index = SYSADPT_TX_WMM_QUEUES - index - 1;
	txpriority = index;

	if (sta && sta->ht_cap.ht_supported && !eapol_frame &&
	    ieee80211_is_data_qos(wh->frame_control)) {
		tid = qos & 0xf;
		mwl_tx_count_packet(sta, tid);

		spin_lock_bh(&priv->stream_lock);
		stream = mwl_fwcmd_lookup_stream(hw, sta->addr, tid);

		if (stream) {
			if (stream->state == AMPDU_STREAM_ACTIVE) {
				if (WARN_ON(!(qos &
					    MWL_QOS_ACK_POLICY_BLOCKACK))) {
					spin_unlock_bh(&priv->stream_lock);
					dev_kfree_skb_any(skb);
					return;
				}

				txpriority =
					(SYSADPT_TX_WMM_QUEUES + stream->idx) %
					SYSADPT_TOTAL_HW_QUEUES;
			} else if (stream->state == AMPDU_STREAM_NEW) {
				/* We get here if the driver sends us packets
				 * after we've initiated a stream, but before
				 * our ampdu_action routine has been called
				 * with IEEE80211_AMPDU_TX_START to get the SSN
				 * for the ADDBA request.  So this packet can
				 * go out with no risk of sequence number
				 * mismatch.  No special handling is required.
				 */
			} else {
				/* Drop packets that would go out after the
				 * ADDBA request was sent but before the ADDBA
				 * response is received.  If we don't do this,
				 * the recipient would probably receive it
				 * after the ADDBA request with SSN 0.  This
				 * will cause the recipient's BA receive window
				 * to shift, which would cause the subsequent
				 * packets in the BA stream to be discarded.
				 * mac80211 queues our packets for us in this
				 * case, so this is really just a safety check.
				 */
				wiphy_warn(hw->wiphy,
					   "can't send packet during ADDBA\n");
				spin_unlock_bh(&priv->stream_lock);
				dev_kfree_skb_any(skb);
				return;
			}
		} else {
			if (mwl_fwcmd_ampdu_allowed(sta, tid)) {
				stream = mwl_fwcmd_add_stream(hw, sta, tid);

				if (stream)
					start_ba_session = true;
			}
		}

		spin_unlock_bh(&priv->stream_lock);
	} else {
		qos &= ~MWL_QOS_ACK_POLICY_MASK;
		qos |= MWL_QOS_ACK_POLICY_NORMAL;
	}

	tx_ctrl = (struct mwl_tx_ctrl *)&tx_info->status;
	tx_ctrl->vif = (void *)tx_info->control.vif;
	tx_ctrl->sta = (void *)sta;
	tx_ctrl->k_conf = (void *)k_conf;
	tx_ctrl->amsdu_pkts = NULL;
	tx_ctrl->tx_priority = txpriority;
	tx_ctrl->type = (mgmtframe ? IEEE_TYPE_MANAGEMENT : IEEE_TYPE_DATA);
	tx_ctrl->qos_ctrl = qos;
	tx_ctrl->xmit_control = xmitcontrol;

	if (skb_queue_len(&priv->txq[index]) > priv->txq_limit){
		wiphy_err(priv->hw->wiphy, "[SQ%d]\n", SYSADPT_TX_WMM_QUEUES - index - 1);
		ieee80211_stop_queue(hw, SYSADPT_TX_WMM_QUEUES - index - 1);
	}

	skb_queue_tail(&priv->txq[index], skb);

	if (priv->if_ops.ptx_work != NULL) {
		/* SDIO interface is using this path */
		if (!priv->is_tx_done_schedule) {
			priv->is_tx_done_schedule = true;
			queue_work(priv->if_ops.ptx_workq,
				priv->if_ops.ptx_work);
		}
	} else {
		/* PCIE interface is using this path */
		tasklet_schedule(priv->if_ops.ptx_task);
	}

	/* Initiate the ampdu session here */
	if (start_ba_session) {
		spin_lock_bh(&priv->stream_lock);
		if (mwl_fwcmd_start_stream(hw, stream))
			mwl_fwcmd_remove_stream(hw, stream);
		spin_unlock_bh(&priv->stream_lock);
	}
}

void mwl_tx_del_pkts_via_vif(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif)
{
	struct mwl_priv *priv = hw->priv;
	int num;
	struct sk_buff *skb, *tmp;
	struct ieee80211_tx_info *tx_info;
	struct mwl_tx_ctrl *tx_ctrl;
	struct sk_buff_head *amsdu_pkts;

	for (num = 1; num < SYSADPT_NUM_OF_DESC_DATA; num++) {
		spin_lock_bh(&priv->txq[num].lock);
		skb_queue_walk_safe(&priv->txq[num], skb, tmp) {
			tx_info = IEEE80211_SKB_CB(skb);
			tx_ctrl = (struct mwl_tx_ctrl *)&tx_info->status;
			if (tx_ctrl->vif == vif) {
				amsdu_pkts = (struct sk_buff_head *)
					tx_ctrl->amsdu_pkts;
				if (amsdu_pkts) {
					skb_queue_purge(amsdu_pkts);
					kfree(amsdu_pkts);
				}
				__skb_unlink(skb, &priv->txq[num]);
				dev_kfree_skb_any(skb);
			}
		}
		spin_unlock_bh(&priv->txq[num].lock);
	}
}

void mwl_tx_del_pkts_via_sta(struct ieee80211_hw *hw,
			     struct ieee80211_sta *sta)
{
	struct mwl_priv *priv = hw->priv;
	int num;
	struct sk_buff *skb, *tmp;
	struct ieee80211_tx_info *tx_info;
	struct mwl_tx_ctrl *tx_ctrl;
	struct sk_buff_head *amsdu_pkts;

	for (num = 1; num < SYSADPT_NUM_OF_DESC_DATA; num++) {
		spin_lock_bh(&priv->txq[num].lock);
		skb_queue_walk_safe(&priv->txq[num], skb, tmp) {
			tx_info = IEEE80211_SKB_CB(skb);
			tx_ctrl = (struct mwl_tx_ctrl *)&tx_info->status;
			if (tx_ctrl->sta == sta) {
				amsdu_pkts = (struct sk_buff_head *)
					tx_ctrl->amsdu_pkts;
				if (amsdu_pkts) {
					skb_queue_purge(amsdu_pkts);
					kfree(amsdu_pkts);
				}
				__skb_unlink(skb, &priv->txq[num]);
				dev_kfree_skb_any(skb);
			}
		}
		spin_unlock_bh(&priv->txq[num].lock);
	}
}

void mwl_tx_del_ampdu_pkts(struct ieee80211_hw *hw,
			   struct ieee80211_sta *sta, u8 tid)
{
	struct mwl_priv *priv = hw->priv;
	struct mwl_sta *sta_info = mwl_dev_get_sta(sta);
	int ac, desc_num;
	struct mwl_amsdu_frag *amsdu_frag;
	struct sk_buff *skb, *tmp;
	struct ieee80211_tx_info *tx_info;
	struct mwl_tx_ctrl *tx_ctrl;
	struct sk_buff_head *amsdu_pkts;

	ac = mwl_tx_tid_queue_mapping(tid);
	desc_num = SYSADPT_TX_WMM_QUEUES - ac - 1;
	spin_lock_bh(&priv->txq[desc_num].lock);
	skb_queue_walk_safe(&priv->txq[desc_num], skb, tmp) {
		tx_info = IEEE80211_SKB_CB(skb);
		tx_ctrl = (struct mwl_tx_ctrl *)&tx_info->status;
		if (tx_ctrl->sta == sta) {
			amsdu_pkts = (struct sk_buff_head *)
				tx_ctrl->amsdu_pkts;
			if (amsdu_pkts) {
				skb_queue_purge(amsdu_pkts);
				kfree(amsdu_pkts);
			}
			__skb_unlink(skb, &priv->txq[desc_num]);
			dev_kfree_skb_any(skb);
		}
	}
	spin_unlock_bh(&priv->txq[desc_num].lock);

	spin_lock_bh(&sta_info->amsdu_lock);
	amsdu_frag = &sta_info->amsdu_ctrl.frag[desc_num];
	if (amsdu_frag->num) {
		amsdu_frag->num = 0;
		amsdu_frag->cur_pos = NULL;
		if (amsdu_frag->skb) {
			tx_info = IEEE80211_SKB_CB(amsdu_frag->skb);
			tx_ctrl = (struct mwl_tx_ctrl *)&tx_info->status;
			amsdu_pkts = (struct sk_buff_head *)
				tx_ctrl->amsdu_pkts;
			if (amsdu_pkts) {
				skb_queue_purge(amsdu_pkts);
				kfree(amsdu_pkts);
			}
			dev_kfree_skb_any(amsdu_frag->skb);
		}
	}
	spin_unlock_bh(&sta_info->amsdu_lock);
}

int mwl_tx_get_highest_priority_qnum(struct ieee80211_hw *hw)
{
	struct mwl_priv *priv = hw->priv;
	int num = -1;

	for (num = SYSADPT_TX_WMM_QUEUES - 1; num >= 0; num--) {
		if ((skb_queue_len(&priv->txq[num]) > 0) &&
			mwl_tx_available(priv, num))
			return num;
	}
	return num;
}


void mwl_tx_skbs(unsigned long data)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)data;
	struct mwl_priv *priv = hw->priv;
	int num;
	int next_pkt_len = 0;
	struct sk_buff *tx_skb, *next_skb;
	struct mwl_sta *sta_info;

/* pr_alert( "%s() called\n", __FUNCTION__); */

#if 0
pr_alert("wrptr=0x%x, rdptr=0x%x not_full=%d\n",
		priv->txbd_wrptr, priv->txbd_rdptr,
		PCIE_TXBD_NOT_FULL(priv->txbd_wrptr, priv->txbd_rdptr));
#endif

	spin_lock_bh(&priv->tx_desc_lock);
	while (1) {
		struct ieee80211_tx_info *tx_info;
		struct mwl_tx_ctrl *tx_ctrl;

		num = mwl_tx_get_highest_priority_qnum(hw);
		if (num < 0)
			break;

		tx_skb = skb_dequeue(&priv->txq[num]);
		tx_info = IEEE80211_SKB_CB(tx_skb);
		tx_ctrl = (struct mwl_tx_ctrl *)&tx_info->status;

		if ((tx_skb->protocol != cpu_to_be16(ETH_P_PAE)) &&
		(tx_ctrl->tx_priority >= SYSADPT_TX_WMM_QUEUES)) {
			/* Need to leave spin_lock since bus driver may
			   sleep while transferring data
			 */
			spin_unlock_bh(&priv->tx_desc_lock);
			//wiphy_err(priv->hw->wiphy, "[call mwl_tx_do_amsdu]\n");
			tx_skb = mwl_tx_do_amsdu(priv, num,
					tx_skb, tx_info);
			spin_lock_bh(&priv->tx_desc_lock);
		}

		/* get next buffer length */
		next_pkt_len = 0;
		if (priv->host_if == MWL_IF_SDIO) {
			next_skb = skb_peek(&priv->txq[num]);
			if (next_skb != NULL) {
				next_pkt_len = next_skb->len +
					sizeof(struct mwl_tx_desc);
			}
		} else
			next_pkt_len = num;

		if (tx_skb) {
			/* Need to leave spin_lock since bus driver
			 * may sleep while transferring data
			 */
			spin_unlock_bh(&priv->tx_desc_lock);
			if (mwl_tx_available(priv, next_pkt_len))
			{
				//	wiphy_err(priv->hw->wiphy, "[call mwl_tx_skb1]\n");
				mwl_tx_skb(priv, next_pkt_len, tx_skb);
			}
			else {
				/* skb consumed. Marking as NULL. This code is not
				 * intended to be executed.
				 * If it gets executed, following will
				 * happen - tx_skb can contain an amsdu, which will get
				 * added to queue head updating the queue length and the
				 * while () loop will never terminate...
				 */
				skb_queue_head(&priv->txq[num], tx_skb);
			}
			spin_lock_bh(&priv->tx_desc_lock);
		}

		if (skb_queue_len(&priv->txq[num]) == 0) {
			spin_lock(&priv->sta_lock);

			list_for_each_entry(sta_info, &priv->sta_list, list) {

				spin_lock_bh(&sta_info->amsdu_lock);

				if (sta_info->amsdu_ctrl.frag[num].num) {
					if (mwl_tx_available(priv, num)) {
						sta_info->amsdu_ctrl.frag[num].num = 0;
						sta_info->amsdu_ctrl.frag[num].cur_pos = NULL;
						spin_unlock_bh(&sta_info->amsdu_lock);
						spin_unlock(&priv->sta_lock);
						spin_unlock_bh(&priv->tx_desc_lock);
						//	wiphy_err(priv->hw->wiphy, "[call mwl_tx_skb2]\n");
                        /* Passing 0 or num as argument is HID specific since
                         * its usage or interpretation varies for different
                         * HIDs.
                         */
						mwl_tx_skb(priv, 
								((priv->host_if == MWL_IF_SDIO)? 0: num),
								sta_info->amsdu_ctrl.frag[num].skb);
						spin_lock_bh(&priv->tx_desc_lock);
						spin_lock(&priv->sta_lock);
						spin_lock_bh(&sta_info->amsdu_lock);
					}
				}
				spin_unlock_bh(&sta_info->amsdu_lock);
			}
			spin_unlock(&priv->sta_lock);
		}

		if (skb_queue_len(&priv->txq[num]) <
				SYSADPT_TX_WAKE_Q_THRESHOLD) {
			int queue;

			queue = SYSADPT_TX_WMM_QUEUES - num - 1;
			if (ieee80211_queue_stopped(hw, queue))
			{
				wiphy_err(priv->hw->wiphy, "[WQ%d]\n", queue);
				ieee80211_wake_queue(hw, queue);
			}
		}
	}
	spin_unlock_bh(&priv->tx_desc_lock);
}
EXPORT_SYMBOL_GPL(mwl_tx_skbs);

void mwl_tx_del_sta_amsdu_pkts(struct ieee80211_sta *sta)
{
	struct mwl_sta *sta_info = mwl_dev_get_sta(sta);
	int num;
	struct mwl_amsdu_frag *amsdu_frag;
	struct ieee80211_tx_info *tx_info;
	struct mwl_tx_ctrl *tx_ctrl;
	struct sk_buff_head *amsdu_pkts;

	spin_lock_bh(&sta_info->amsdu_lock);
	for (num = 0; num < SYSADPT_TX_WMM_QUEUES; num++) {
		amsdu_frag = &sta_info->amsdu_ctrl.frag[num];
		if (amsdu_frag->num) {
			amsdu_frag->num = 0;
			amsdu_frag->cur_pos = NULL;
			if (amsdu_frag->skb) {
				tx_info = IEEE80211_SKB_CB(amsdu_frag->skb);
				tx_ctrl = (struct mwl_tx_ctrl *)
					&tx_info->status;
				amsdu_pkts = (struct sk_buff_head *)
					tx_ctrl->amsdu_pkts;
				if (amsdu_pkts) {
					skb_queue_purge(amsdu_pkts);
					kfree(amsdu_pkts);
				}
				dev_kfree_skb_any(amsdu_frag->skb);
			}
		}
	}
	spin_unlock_bh(&sta_info->amsdu_lock);
}
