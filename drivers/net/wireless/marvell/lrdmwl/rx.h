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

/* Description:  This file defines receive related functions. */

#ifndef _RX_H_
#define _RX_H_

int mwl_rx_init(struct ieee80211_hw *hw);
void mwl_rx_deinit(struct ieee80211_hw *hw);
void mwl_rx_prepare_status(struct mwl_rx_desc *pdesc,
					 struct ieee80211_rx_status *status);
struct mwl_vif *mwl_rx_find_vif_bss(struct mwl_priv *priv,
						  u8 *bssid);
void mwl_rx_remove_dma_header(struct sk_buff *skb, __le16 qos);
void mwl_rx_enable_sta_amsdu(struct mwl_priv *priv,
					   u8 *sta_addr);

extern void mwl_rx_upload_pkt(struct ieee80211_hw *hw,
		struct sk_buff *rx_skb);
extern void mwl_rx_defered_handler(struct work_struct *work);
extern void mwl_handle_rx_event(struct ieee80211_hw *hw,
                    struct mwl_rx_event_data *rx_evnt);

#endif /* _RX_H_ */
