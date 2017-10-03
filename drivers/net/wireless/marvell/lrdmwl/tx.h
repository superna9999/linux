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

/* Description:  This file defines transmit related functions. */

#ifndef _TX_H_
#define _TX_H_

/* Tx Rate to be indicated to mac80211 - For KF2 PCIe & SDIO,
** driver has no way of knowing the rate at which the pkt was Tx'ed.
** Use hardcoded max value for this
*/

/* VHT/2SS/BW80/MCS7/SGI */
#define TX_COMP_RATE_FOR_DATA ((7 << MWL_TX_RATE_RATEIDMCS_SHIFT) |\
	(TX_RATE_INFO_SHORT_GI << MWL_TX_RATE_SHORTGI_SHIFT) |\
	(TX_RATE_BANDWIDTH_80 << MWL_TX_RATE_BANDWIDTH_SHIFT) |\
	(2 << MWL_TX_RATE_NSS_SHIFT) |\
	TX_RATE_FORMAT_11AC);


int mwl_tx_init(struct ieee80211_hw *hw);
void mwl_tx_deinit(struct ieee80211_hw *hw);
void mwl_tx_xmit(struct ieee80211_hw *hw,
		 struct ieee80211_tx_control *control,
		 struct sk_buff *skb);
void mwl_tx_del_pkts_via_vif(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif);
void mwl_tx_del_pkts_via_sta(struct ieee80211_hw *hw,
			     struct ieee80211_sta *sta);
void mwl_tx_del_ampdu_pkts(struct ieee80211_hw *hw,
			   struct ieee80211_sta *sta, u8 tid);

void mwl_tx_del_sta_amsdu_pkts(struct ieee80211_sta *sta);
void mwl_tx_skbs(unsigned long data);
void mwl_tx_skb(struct mwl_priv *priv, int desc_num,
			      struct sk_buff *tx_skb);
void mwl_tx_done(unsigned long data);
void mwl_pfu_tx_done(unsigned long data);
void mwl_tx_flush_amsdu(unsigned long data);
void mwl_tx_ack_amsdu_pkts(struct ieee80211_hw *hw, u32 rate,
					 struct sk_buff_head *amsdu_pkts);
void mwl_tx_prepare_info(struct ieee80211_hw *hw, u32 rate,
				       struct ieee80211_tx_info *info);

#endif /* _TX_H_ */
