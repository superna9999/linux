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

/* Description:  This file implements mac80211 related functions. */

#include <linux/etherdevice.h>

#include "sysadpt.h"
#include "dev.h"
#include "fwcmd.h"
#include "tx.h"
#include "main.h"

#define MWL_DRV_NAME        KBUILD_MODNAME

#define MAX_AMPDU_ATTEMPTS  5

extern struct ieee80211_rate mwl_rates_24[];
extern struct ieee80211_rate mwl_rates_50[];

static void mwl_mac80211_tx(struct ieee80211_hw *hw,
			    struct ieee80211_tx_control *control,
			    struct sk_buff *skb)
{
	struct mwl_priv *priv = hw->priv;

	if (!priv->radio_on) {
		wiphy_warn(hw->wiphy,
			   "dropped TX frame since radio is disabled\n");
		dev_kfree_skb_any(skb);
		return;
	}

	mwl_tx_xmit(hw, control, skb);
}

static int mwl_mac80211_start(struct ieee80211_hw *hw)
{
	struct mwl_priv *priv = hw->priv;
	int rc;

	/* Enable TX and RX tasklets. */
	if (priv->if_ops.ptx_task != NULL)
		tasklet_enable(priv->if_ops.ptx_task);

	tasklet_enable(&priv->rx_task);
	if (priv->if_ops.ptx_done_task != NULL)
		tasklet_enable(priv->if_ops.ptx_done_task);

	if (priv->if_ops.pqe_task != NULL)
		tasklet_enable(priv->if_ops.pqe_task);

	/* Enable interrupts */
	mwl_fwcmd_int_enable(hw);

	rc = mwl_fwcmd_radio_enable(hw);
	if (rc)
		goto fwcmd_fail;
	rc = mwl_fwcmd_set_rate_adapt_mode(hw, 0);
	if (rc)
		goto fwcmd_fail;
	rc = mwl_fwcmd_set_wmm_mode(hw, true);
	if (rc)
		goto fwcmd_fail;
	rc = mwl_fwcmd_ht_guard_interval(hw, GUARD_INTERVAL_AUTO);
	if (rc)
		goto fwcmd_fail;
	rc = mwl_fwcmd_set_dwds_stamode(hw, true);
	if (rc)
		goto fwcmd_fail;
	rc = mwl_fwcmd_set_fw_flush_timer(hw, SYSADPT_AMSDU_FLUSH_TIME);
	if (rc)
		goto fwcmd_fail;
	rc = mwl_fwcmd_set_optimization_level(hw, wmm_turbo);
	if (rc)
		goto fwcmd_fail;

	ieee80211_wake_queues(hw);
	return 0;

fwcmd_fail:
	mwl_fwcmd_int_disable(hw);
	if (priv->if_ops.ptx_task != NULL)
		tasklet_disable(priv->if_ops.ptx_task);

	if (priv->if_ops.ptx_done_task != NULL)
		tasklet_disable(priv->if_ops.ptx_done_task);

	if (priv->if_ops.pqe_task != NULL)
		tasklet_disable(priv->if_ops.pqe_task);
	tasklet_disable(&priv->rx_task);

	return rc;
}

static void mwl_mac80211_stop(struct ieee80211_hw *hw)
{
	struct mwl_priv *priv = hw->priv;

	mwl_fwcmd_radio_disable(hw);

	ieee80211_stop_queues(hw);

	/* Disable interrupts */
	mwl_fwcmd_int_disable(hw);

	/* Disable TX reclaim and RX tasklets. */
	if (priv->if_ops.ptx_task != NULL)
		tasklet_disable(priv->if_ops.ptx_task);

	if (priv->if_ops.ptx_done_task != NULL)
		tasklet_disable(priv->if_ops.ptx_done_task);

	if (priv->if_ops.pqe_task != NULL)
		tasklet_disable(priv->if_ops.pqe_task);
	tasklet_disable(&priv->rx_task);

	/* Return all skbs to mac80211 */
	if (priv->if_ops.tx_done != NULL)
		priv->if_ops.tx_done((unsigned long)hw);
}

static int mwl_mac80211_add_interface(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif)
{
	struct mwl_priv *priv = hw->priv;
	struct mwl_vif *mwl_vif;
	u32 macids_supported;
	int macid;

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		macids_supported = priv->ap_macids_supported;
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		macids_supported = priv->sta_macids_supported;
		break;
	default:
		return -EINVAL;
	}

	macid = ffs(macids_supported & ~priv->macids_used);

	if (!macid) {
		wiphy_warn(hw->wiphy, "no macid can be allocated\n");
		return -EBUSY;
	}
	macid--;

	/* Setup driver private area. */
	mwl_vif = mwl_dev_get_vif(vif);
	memset(mwl_vif, 0, sizeof(*mwl_vif));
	mwl_vif->macid = macid;
	mwl_vif->seqno = 0;
	mwl_vif->is_hw_crypto_enabled = false;
	mwl_vif->beacon_info.valid = false;
	mwl_vif->iv16 = 1;
	mwl_vif->iv32 = 0;
	mwl_vif->keyidx = 0;
	mwl_vif->tx_key_idx = -1;

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		ether_addr_copy(mwl_vif->bssid, vif->addr);
		mwl_fwcmd_set_new_stn_add_self(hw, vif);
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		ether_addr_copy(mwl_vif->sta_mac, vif->addr);
		mwl_fwcmd_bss_start(hw, vif, true);
		mwl_fwcmd_set_infra_mode(hw, vif);
		mwl_fwcmd_set_mac_addr_client(hw, vif, vif->addr);
		break;
	default:
		return -EINVAL;
	}

	priv->macids_used |= 1 << mwl_vif->macid;
	spin_lock_bh(&priv->vif_lock);
	list_add_tail(&mwl_vif->list, &priv->vif_list);
	spin_unlock_bh(&priv->vif_lock);

	return 0;
}

static void mwl_mac80211_remove_vif(struct mwl_priv *priv,
				    struct ieee80211_vif *vif)
{
	struct mwl_vif *mwl_vif = mwl_dev_get_vif(vif);

	if (!priv->macids_used)
		return;

	mwl_tx_del_pkts_via_vif(priv->hw, vif);

	priv->macids_used &= ~(1 << mwl_vif->macid);
	spin_lock_bh(&priv->vif_lock);
	list_del(&mwl_vif->list);
	spin_unlock_bh(&priv->vif_lock);
}

static void mwl_mac80211_remove_interface(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif)
{
	struct mwl_priv *priv = hw->priv;

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		mwl_fwcmd_set_new_stn_del(hw, vif, vif->addr);
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		mwl_fwcmd_remove_mac_addr(hw, vif, vif->addr);
		break;
	default:
		break;
	}

	mwl_mac80211_remove_vif(priv, vif);
}

static int mwl_mac80211_config(struct ieee80211_hw *hw,
			       u32 changed)
{
	struct ieee80211_conf *conf = &hw->conf;
	struct mwl_priv *priv = hw->priv;

	int rc;

	if (conf->flags & IEEE80211_CONF_IDLE)
		rc = mwl_fwcmd_radio_disable(hw);
	else
		rc = mwl_fwcmd_radio_enable(hw);

	if (rc)
		goto out;

	if (changed & IEEE80211_CONF_CHANGE_PS) {
		rc = mwl_fwcmd_powersave_EnblDsbl(hw, conf);
		if (rc)
			goto out;
	}

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		int rate = 0;

//		wiphy_debug(hw->wiphy, "config: c=%x fl=%x pw=%d rd=%d ch=%d\n", 
//				changed, conf->flags, conf->power_level, 
//				conf->radar_enabled, conf->chandef.chan->hw_value);

		if (conf->chandef.chan->band == NL80211_BAND_2GHZ) {
			mwl_fwcmd_set_apmode(hw, AP_MODE_2_4GHZ_11AC_MIXED);
			mwl_fwcmd_set_linkadapt_cs_mode(hw, LINK_CS_STATE_CONSERV);
			rate = mwl_rates_24[0].hw_value;
		} else if (conf->chandef.chan->band == NL80211_BAND_5GHZ) {
			mwl_fwcmd_set_apmode(hw, AP_MODE_11AC);
			mwl_fwcmd_set_linkadapt_cs_mode(hw, LINK_CS_STATE_AUTO);
			rate = mwl_rates_50[0].hw_value;

			if (conf->radar_enabled)
				mwl_fwcmd_set_radar_detect(hw, MONITOR_START);
			else
				mwl_fwcmd_set_radar_detect(hw, STOP_DETECT_RADAR);
		}

		rc = mwl_fwcmd_get_survey(hw, 0);
		if (rc)
			goto out;
		rc = mwl_fwcmd_set_rf_channel(hw, conf);
		if (rc)
			goto out;
		rc = mwl_fwcmd_use_fixed_rate(hw, rate, rate);
		if (rc)
			goto out;
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		priv->target_power = conf->power_level;
	}

	if (changed & (IEEE80211_CONF_CHANGE_POWER | IEEE80211_CONF_CHANGE_CHANNEL)) {
		rc = mwl_fwcmd_tx_power(hw, conf, priv->target_power);
		if (rc)
			goto out;
	}

out:

	return rc;
}

static void mwl_rc_update_work(struct work_struct *work)
{
	struct mwl_sta *sta_info =
		container_of(work, struct mwl_sta, rc_update_work);
	struct ieee80211_sta *sta =
	    container_of((void *)sta_info, struct ieee80211_sta, drv_priv);
	struct mwl_priv *priv = sta_info->mwl_private;
	struct ieee80211_hw *hw = priv->hw;
	u8 smps_mode;

	wiphy_err(hw->wiphy, "%s() new smps_mode=%d\n",
			__FUNCTION__, sta->smps_mode);
	wiphy_err(hw->wiphy, "mac: %x:%x:%x:%x:%x:%x\n",
			sta->addr[0], sta->addr[1],
			sta->addr[2], sta->addr[3],
			sta->addr[4], sta->addr[5]);

	if ((sta->smps_mode == IEEE80211_SMPS_AUTOMATIC) ||
		(sta->smps_mode == IEEE80211_SMPS_OFF)){
		smps_mode = 0;
	} else {
		/* Convert mac80211 enum to 80211 format again */
		smps_mode = 0x1; // Enable
		smps_mode |= ((sta->smps_mode ==
			IEEE80211_SMPS_DYNAMIC)? 0x10 : 0);
	}

	mwl_fwcmd_set_mimops_ht(hw,
			sta->addr, smps_mode);
}

void mwl_mac80211_sta_rc_update(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta,
			      u32 changed)
{
	struct mwl_priv *priv = hw->priv;

	if(changed & IEEE80211_RC_SMPS_CHANGED) {
		struct mwl_sta *sta_info;
		sta_info = mwl_dev_get_sta(sta);

		queue_work(priv->rx_defer_workq,
			&sta_info->rc_update_work);
	}
	/* TODO: VHT OpMode notification related handling here */
}


static void mwl_mac80211_bss_info_changed_sta(struct ieee80211_hw *hw,
					      struct ieee80211_vif *vif,
					      struct ieee80211_bss_conf *info,
					      u32 changed)
{

	if (changed & BSS_CHANGED_ERP_SLOT)
		mwl_fwcmd_set_slot_time(hw, vif->bss_conf.use_short_slot);

	if (changed & BSS_CHANGED_ERP_PREAMBLE)
		mwl_fwcmd_set_radio_preamble(hw,
					     vif->bss_conf.use_short_preamble);

	if ((changed & BSS_CHANGED_ASSOC) && vif->bss_conf.assoc)
		mwl_fwcmd_set_aid(hw, vif, (u8 *)vif->bss_conf.bssid,
				  vif->bss_conf.aid);
}

static void mwl_mac80211_bss_info_changed_ap(struct ieee80211_hw *hw,
					     struct ieee80211_vif *vif,
					     struct ieee80211_bss_conf *info,
					     u32 changed)
{

	if (changed & BSS_CHANGED_ERP_SLOT)
		mwl_fwcmd_set_slot_time(hw, vif->bss_conf.use_short_slot);
	if (changed & BSS_CHANGED_ERP_PREAMBLE)
		mwl_fwcmd_set_radio_preamble(hw,
					     vif->bss_conf.use_short_preamble);

	if (changed & BSS_CHANGED_BASIC_RATES) {
		int idx;
		int rate;

		/* Use lowest supported basic rate for multicasts
		 * and management frames (such as probe responses --
		 * beacons will always go out at 1 Mb/s).
		 */
		idx = ffs(vif->bss_conf.basic_rates);
		if (idx)
			idx--;

		if (hw->conf.chandef.chan->band == NL80211_BAND_2GHZ)
			rate = mwl_rates_24[idx].hw_value;
		else
			rate = mwl_rates_50[idx].hw_value;

		mwl_fwcmd_use_fixed_rate(hw, rate, rate);
	}

	if (changed & (BSS_CHANGED_BEACON_INT | BSS_CHANGED_BEACON)) {
		struct sk_buff *skb;

		skb = ieee80211_beacon_get(hw, vif);
		if (skb) {
			mwl_fwcmd_set_beacon(hw, vif, skb->data, skb->len);
			dev_kfree_skb_any(skb);
		}
	}

	if (changed & BSS_CHANGED_BEACON_ENABLED)
		mwl_fwcmd_bss_start(hw, vif, info->enable_beacon);
}

static void mwl_mac80211_bss_info_changed(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  struct ieee80211_bss_conf *info,
					  u32 changed)
{
	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		mwl_mac80211_bss_info_changed_ap(hw, vif, info, changed);
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		mwl_mac80211_bss_info_changed_sta(hw, vif, info, changed);
		break;
	default:
		break;
	}
}

static void mwl_mac80211_configure_filter(struct ieee80211_hw *hw,
					  unsigned int changed_flags,
					  unsigned int *total_flags,
					  u64 multicast)
{
	/* AP firmware doesn't allow fine-grained control over
	 * the receive filter.
	 */
	*total_flags &= FIF_ALLMULTI | FIF_BCN_PRBRESP_PROMISC;
}

static int mwl_mac80211_set_key(struct ieee80211_hw *hw,
				enum set_key_cmd cmd_param,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta,
				struct ieee80211_key_conf *key)
{
	struct mwl_vif *mwl_vif;
	int rc = 0;
	u8 encr_type;
	u8 *addr;

	mwl_vif = mwl_dev_get_vif(vif);

	if (!sta) {
		addr = vif->addr;
	} else {
		addr = sta->addr;
		if ((vif->type == NL80211_IFTYPE_STATION) ||
			(vif->type == NL80211_IFTYPE_P2P_CLIENT))
			ether_addr_copy(mwl_vif->bssid, addr);
	}

	if (cmd_param == SET_KEY) {
		rc = mwl_fwcmd_encryption_set_key(hw, vif, addr, key);

		if (rc)
			goto out;

		if ((key->cipher == WLAN_CIPHER_SUITE_WEP40) ||
		    (key->cipher == WLAN_CIPHER_SUITE_WEP104)) {
			encr_type = ENCR_TYPE_WEP;
		} else if (key->cipher == WLAN_CIPHER_SUITE_CCMP) {
			encr_type = ENCR_TYPE_AES;
			if ((key->flags & IEEE80211_KEY_FLAG_PAIRWISE) == 0) {
				if ((vif->type != NL80211_IFTYPE_STATION) &&
					(vif->type != NL80211_IFTYPE_P2P_CLIENT))
					mwl_vif->keyidx = key->keyidx;
			}
		} else if (key->cipher == WLAN_CIPHER_SUITE_TKIP) {
			encr_type = ENCR_TYPE_TKIP;
		} else {
			encr_type = ENCR_TYPE_DISABLE;
		}

		rc = mwl_fwcmd_update_encryption_enable(hw, vif, addr,
							encr_type);
		if (rc)
			goto out;

		mwl_vif->is_hw_crypto_enabled = true;
	} else {
		rc = mwl_fwcmd_encryption_remove_key(hw, vif, addr, key);
		if (rc)
			goto out;
	}

out:

	return rc;
}

static int mwl_mac80211_set_rts_threshold(struct ieee80211_hw *hw,
					  u32 value)
{
	return mwl_fwcmd_set_rts_threshold(hw, value);
}

static int mwl_mac80211_sta_add(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta)
{
	struct mwl_priv *priv = hw->priv;
	struct mwl_vif *mwl_vif;
	struct mwl_sta *sta_info;
	struct ieee80211_key_conf *key;
	int rc;
	int i;

	mwl_vif = mwl_dev_get_vif(vif);
	sta_info = mwl_dev_get_sta(sta);

	memset(sta_info, 0, sizeof(*sta_info));

	if (sta->ht_cap.ht_supported) {
		sta_info->is_ampdu_allowed = true;
		sta_info->is_amsdu_allowed = false;
		if (sta->ht_cap.cap & IEEE80211_HT_CAP_MAX_AMSDU)
			sta_info->amsdu_ctrl.cap = MWL_AMSDU_SIZE_8K;
		else
			sta_info->amsdu_ctrl.cap = MWL_AMSDU_SIZE_4K;
		if ((sta->tdls) && (!sta->wme))
			sta->wme = true;
	}
	sta_info->iv16 = 1;
	sta_info->iv32 = 0;
	spin_lock_init(&sta_info->amsdu_lock);
	INIT_WORK(&sta_info->rc_update_work, mwl_rc_update_work);
	sta_info->mwl_private = priv;

	spin_lock_bh(&priv->sta_lock);
	list_add_tail(&sta_info->list, &priv->sta_list);
	spin_unlock_bh(&priv->sta_lock);

	if ((vif->type == NL80211_IFTYPE_STATION) ||
		(vif->type == NL80211_IFTYPE_P2P_CLIENT))
		mwl_fwcmd_set_new_stn_del(hw, vif, sta->addr);

	rc = mwl_fwcmd_set_new_stn_add(hw, vif, sta);

	for (i = 0; i < NUM_WEP_KEYS; i++) {
		key = (struct ieee80211_key_conf *)mwl_vif->wep_key_conf[i].key;

		if (mwl_vif->wep_key_conf[i].enabled)
			mwl_mac80211_set_key(hw, SET_KEY, vif, sta, key);
	}

	return rc;
}

static int mwl_mac80211_sta_remove(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta)
{
	struct mwl_priv *priv = hw->priv;
	int rc;
	struct mwl_sta *sta_info = mwl_dev_get_sta(sta);

	cancel_work_sync(&sta_info->rc_update_work);

	mwl_tx_del_sta_amsdu_pkts(sta);
	mwl_fwcmd_del_sta_streams(hw, sta);
	mwl_tx_del_pkts_via_sta(hw, sta);

	rc = mwl_fwcmd_set_new_stn_del(hw, vif, sta->addr);

	spin_lock_bh(&priv->sta_lock);
	list_del(&sta_info->list);
	spin_unlock_bh(&priv->sta_lock);

	return rc;
}

static int mwl_mac80211_conf_tx(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				u16 queue,
				const struct ieee80211_tx_queue_params *params)
{
	struct mwl_priv *priv = hw->priv;
	int rc = 0;

	if (WARN_ON(queue > SYSADPT_TX_WMM_QUEUES - 1))
		return -EINVAL;

	memcpy(&priv->wmm_params[queue], params, sizeof(*params));

	if (!priv->wmm_enabled) {
		rc = mwl_fwcmd_set_wmm_mode(hw, true);
		priv->wmm_enabled = true;
	}

	if (!rc) {
		int q = SYSADPT_TX_WMM_QUEUES - 1 - queue;


//		wiphy_info(hw->wiphy, "WMM Params[Q %d]: cwmin=%d cwmax=%d aifs=%d txop=%d\n", q, params->cw_min, params->cw_max, params->aifs, params->txop);

		rc = mwl_fwcmd_set_edca_params(hw, q,
					       params->cw_min, params->cw_max,
					       params->aifs, params->txop);
	}

	return rc;
}

static int mwl_mac80211_get_stats(struct ieee80211_hw *hw,
				  struct ieee80211_low_level_stats *stats)
{
	return mwl_fwcmd_get_stat(hw, stats);
}

static int mwl_mac80211_get_survey(struct ieee80211_hw *hw,
				   int idx,
				   struct survey_info *survey)
{
	struct mwl_priv *priv = hw->priv;
	struct mwl_survey_info *survey_info;

	if ((priv->survey_info_idx) && (idx < priv->survey_info_idx)) {
        survey_info = &priv->survey_info[idx];
	} else {
        if (idx > priv->survey_info_idx) {
			priv->survey_info_idx = 0;
            return -ENOENT;
        } else if (idx == priv->survey_info_idx) {
            int i;
            for (i = 0; i < priv->survey_info_idx; i++) {
                if (priv->cur_survey_info.channel.hw_value
                        == priv->survey_info[i].channel.hw_value) {
                    priv->survey_info_idx = 0;
                    return -ENOENT;
                }
            }
        }

		if(mwl_fwcmd_get_survey(hw, 0)) {
			return -EIO;
		}
		survey_info = &priv->cur_survey_info;
		if (!(hw->conf.flags & IEEE80211_CONF_OFFCHANNEL))
			survey->filled |= SURVEY_INFO_IN_USE;
	}

	survey->channel = &survey_info->channel;
	survey->filled |= survey_info->filled;
	survey->time = survey_info->time_period / 1000;
	survey->time_busy = survey_info->time_busy / 1000;
	survey->time_tx = survey_info->time_tx / 1000;
	survey->noise = survey_info->noise;

	return 0;
}

#if KERNEL_VERSION(4, 6, 0) > LINUX_VERSION_CODE
static int mwl_mac80211_ampdu_action(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     enum ieee80211_ampdu_mlme_action action,
				     struct ieee80211_sta *sta,
				     u16 tid, u16 *ssn, u8 buf_size, bool amsdu)
{
	int rc = 0;
	struct mwl_priv *priv = hw->priv;
	struct mwl_ampdu_stream *stream;
	u8 *addr = sta->addr, idx;
	struct mwl_sta *sta_info;

	sta_info = mwl_dev_get_sta(sta);

	spin_lock_bh(&priv->stream_lock);

	stream = mwl_fwcmd_lookup_stream(hw, addr, tid);

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
	case IEEE80211_AMPDU_RX_STOP:
		break;
	case IEEE80211_AMPDU_TX_START:
		if (!sta_info->is_ampdu_allowed) {
			wiphy_warn(hw->wiphy, "ampdu not allowed\n");
			rc = -EPERM;
			break;
		}

		if (!stream) {
			stream = mwl_fwcmd_add_stream(hw, sta, tid);
			if (!stream) {
				wiphy_warn(hw->wiphy, "no stream found\n");
				rc = -EPERM;
				break;
			}
		}

		spin_unlock_bh(&priv->stream_lock);
		rc = mwl_fwcmd_check_ba(hw, stream, vif);
		spin_lock_bh(&priv->stream_lock);
		if (rc) {
			mwl_fwcmd_remove_stream(hw, stream);
			sta_info->check_ba_failed[tid]++;
			rc = -EPERM;
			break;
		}
		stream->state = AMPDU_STREAM_IN_PROGRESS;
		*ssn = 0;
		ieee80211_start_tx_ba_cb_irqsafe(vif, addr, tid);
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:

		wiphy_warn(hw->wiphy, "%s(e) Action=%d stream=%p\n",
			__FUNCTION__, action, stream);

		if (stream) {

			wiphy_warn(hw->wiphy, "stream: state = %d idx=%d\n", stream->state, stream->idx);
			wiphy_warn(hw->wiphy, "Addr = %02x:%02x:%02x:%02x:%02x:%02x\n",
					addr[0], addr[1], addr[2],
					addr[3], addr[4], addr[5]);

			if (stream->state == AMPDU_STREAM_ACTIVE) {
				mwl_tx_del_ampdu_pkts(hw, sta, tid);
				idx = stream->idx;
				spin_unlock_bh(&priv->stream_lock);
				mwl_fwcmd_destroy_ba(hw, idx);
				spin_lock_bh(&priv->stream_lock);
			}

			mwl_fwcmd_remove_stream(hw, stream);
			ieee80211_stop_tx_ba_cb_irqsafe(vif, addr, tid);
		} else {
			rc = -EPERM;
		}

		wiphy_warn(hw->wiphy, "%s(l) Action=%d stream=%p ret=%d\n",
			__FUNCTION__, action, stream, rc);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		if (stream) {
			if (WARN_ON(stream->state !=
				    AMPDU_STREAM_IN_PROGRESS)) {
				rc = -EPERM;
				break;
			}
			spin_unlock_bh(&priv->stream_lock);
			rc = mwl_fwcmd_create_ba(hw, stream, buf_size, vif);
			spin_lock_bh(&priv->stream_lock);

			if (!rc) {
				stream->state = AMPDU_STREAM_ACTIVE;
				sta_info->check_ba_failed[tid] = 0;
				sta_info->is_amsdu_allowed = amsdu;
			} else {
				idx = stream->idx;
				spin_unlock_bh(&priv->stream_lock);
				mwl_fwcmd_destroy_ba(hw, idx);
				spin_lock_bh(&priv->stream_lock);
				mwl_fwcmd_remove_stream(hw, stream);
				wiphy_err(hw->wiphy,
					  "ampdu operation error code: %d\n",
					  rc);
			}
		} else {
			rc = -EPERM;
		}
		break;
	default:
		rc = -ENOTSUPP;
		break;
	}

	spin_unlock_bh(&priv->stream_lock);

	return rc;
}
#else
static int mwl_mac80211_ampdu_action(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_ampdu_params *params)
{
	int rc = 0;
	struct mwl_priv *priv = hw->priv;
	struct mwl_ampdu_stream *stream;
	enum ieee80211_ampdu_mlme_action action = params->action;
	struct ieee80211_sta *sta = params->sta;
	u16 tid = params->tid;
	u8 buf_size = params->buf_size;
	u8 *addr = sta->addr, idx;
	struct mwl_sta *sta_info;

	sta_info = mwl_dev_get_sta(sta);

	spin_lock_bh(&priv->stream_lock);

	stream = mwl_fwcmd_lookup_stream(hw, addr, tid);

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
	case IEEE80211_AMPDU_RX_STOP:
		break;
	case IEEE80211_AMPDU_TX_START:
		if (!sta_info->is_ampdu_allowed) {
			wiphy_warn(hw->wiphy, "ampdu not allowed\n");
			rc = -EPERM;
			break;
		}

		if (!stream) {
			stream = mwl_fwcmd_add_stream(hw, sta, tid);
			if (!stream) {
				wiphy_warn(hw->wiphy, "no stream found\n");
				rc = -EPERM;
				break;
			}
		}

		spin_unlock_bh(&priv->stream_lock);
		rc = mwl_fwcmd_check_ba(hw, stream, vif);
		spin_lock_bh(&priv->stream_lock);
		if (rc) {
			mwl_fwcmd_remove_stream(hw, stream);
			sta_info->check_ba_failed[tid]++;
			rc = -EPERM;
			break;
		}
		stream->state = AMPDU_STREAM_IN_PROGRESS;
		params->ssn = 0;
		ieee80211_start_tx_ba_cb_irqsafe(vif, addr, tid);
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		if (stream) {
			if (stream->state == AMPDU_STREAM_ACTIVE) {
				stream->state = AMPDU_STREAM_IN_PROGRESS;
				mwl_tx_del_ampdu_pkts(hw, sta, tid);
				idx = stream->idx;
				spin_unlock_bh(&priv->stream_lock);
				mwl_fwcmd_destroy_ba(hw, idx);
				spin_lock_bh(&priv->stream_lock);
				sta_info->is_amsdu_allowed = false;
			}

			mwl_fwcmd_remove_stream(hw, stream);
			ieee80211_stop_tx_ba_cb_irqsafe(vif, addr, tid);
		} else {
			rc = -EPERM;
		}
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		if (stream) {
			if (WARN_ON(stream->state !=
				    AMPDU_STREAM_IN_PROGRESS)) {
				rc = -EPERM;
				break;
			}
			spin_unlock_bh(&priv->stream_lock);
			rc = mwl_fwcmd_create_ba(hw, stream, buf_size, vif);
			spin_lock_bh(&priv->stream_lock);

			if (!rc) {
				stream->state = AMPDU_STREAM_ACTIVE;
				sta_info->check_ba_failed[tid] = 0;
				sta_info->is_amsdu_allowed = params->amsdu;
			} else {
				idx = stream->idx;
				spin_unlock_bh(&priv->stream_lock);
				mwl_fwcmd_destroy_ba(hw, idx);
				spin_lock_bh(&priv->stream_lock);
				mwl_fwcmd_remove_stream(hw, stream);
				wiphy_err(hw->wiphy,
					  "ampdu operation error code: %d\n",
					  rc);
			}
		} else {
			rc = -EPERM;
		}
		break;
	default:
		rc = -ENOTSUPP;
		break;
	}

	spin_unlock_bh(&priv->stream_lock);

	return rc;
}
#endif

static int mwl_mac80211_remain_on_channel(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ieee80211_channel *chan,
					int duration, enum ieee80211_roc_type type)
{
	struct mwl_priv *priv = hw->priv;
	int rc = 0;

	rc = mwl_config_remain_on_channel(hw, chan, true, duration, type);
	if (!rc) {
		mod_timer(&priv->roc.roc_timer, jiffies + msecs_to_jiffies(duration));
		priv->roc.tmr_running = true;
		ieee80211_ready_on_channel(hw);
	}

	return rc;
}

static int mwl_mac80211_cancel_remain_on_channel(struct ieee80211_hw *hw)
{
	int rc = 0;
    
	rc = mwl_config_remain_on_channel(hw, 0, false, 0, 0);

	return rc;
}

static int mwl_mac80211_chnl_switch(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_channel_switch *ch_switch)
{
	struct mwl_priv *priv = hw->priv;
	int rc = 0;

	rc = mwl_fwcmd_set_switch_channel(priv, ch_switch);

	return rc;
}

static void mwl_mac80211_sw_scan_start(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       const u8 *mac_addr)
{
	struct mwl_priv *priv = hw->priv;

	/* Stop BA timer again */
	del_timer_sync(&priv->period_timer);

	priv->sw_scanning = true;
	priv->survey_info_idx = 0;
	priv->cur_survey_valid = false;

	mwl_fwcmd_set_pre_scan(hw);
}

static void mwl_mac80211_sw_scan_complete(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif)
{
	struct mwl_priv *priv = hw->priv;

	priv->sw_scanning = false;
	mwl_fwcmd_set_post_scan(hw);

	/* Start BA timer again */
	setup_timer(&priv->period_timer, timer_routine, (unsigned long)priv);
	mod_timer(&priv->period_timer, jiffies +
		  msecs_to_jiffies(SYSADPT_TIMER_WAKEUP_TIME));

}

int mwl_mac80211_set_ant(struct ieee80211_hw *hw, u32 tx_ant, u32 rx_ant)
{
	struct mwl_priv *priv = hw->priv;

	wiphy_err(hw->wiphy, "set ant: tx=0x%x rx=0x%x\n",
			tx_ant, rx_ant);

	if (tx_ant == 0x3)
		priv->antenna_tx = ANTENNA_TX_2;
	else
		priv->antenna_tx = ANTENNA_TX_1;

	if (rx_ant == 0x3)
		priv->antenna_rx = ANTENNA_RX_2;
	else
		priv->antenna_rx = ANTENNA_RX_1;

	wiphy_err(hw->wiphy, "set ant(internal): tx=0x%x rx=0x%x\n",
			priv->antenna_tx,
			priv->antenna_rx);

	mwl_fwcmd_rf_antenna(hw, WL_ANTENNATYPE_TX, priv->antenna_tx);
	mwl_fwcmd_rf_antenna(hw, WL_ANTENNATYPE_RX, priv->antenna_rx);

	mwl_set_caps(priv);

	return 0;
}

int mwl_mac80211_get_ant(struct ieee80211_hw *hw, u32 *tx_ant, u32 *rx_ant)
{
	struct mwl_priv *priv = hw->priv;

	if (priv->antenna_tx == ANTENNA_TX_2)
		*tx_ant = 0x3;
	else
		*tx_ant = 0x1;

	if (priv->antenna_rx == ANTENNA_RX_2)
		*rx_ant = 0x3;
	else
		*rx_ant = 0x1;

//	wiphy_err(hw->wiphy, "get ant: tx=0x%x rx=0x%x\n",
//			*tx_ant, *rx_ant);
	return 0;
}

void
mwl_mac80211_set_default_uni_key (struct ieee80211_hw *hw, 
					struct ieee80211_vif *vif,
					int idx)
{
	struct mwl_vif *mwl_vif;
	struct ieee80211_key_conf  *key;

	mwl_vif = mwl_dev_get_vif(vif);

	if (idx >= 0 && idx < NUM_WEP_KEYS) {

		if (mwl_vif->wep_key_conf[idx].enabled && mwl_vif->tx_key_idx != idx) {
			key = (struct ieee80211_key_conf*)mwl_vif->wep_key_conf[idx].key;

			if (!(key->flags & IEEE80211_KEY_FLAG_PAIRWISE)) {
				mwl_vif->tx_key_idx = idx;

				mwl_fwcmd_encryption_set_tx_key(hw, vif, key);
			}
		}
	}
	else {
		mwl_vif->tx_key_idx = -1;
	}

	return;
}

const struct ieee80211_ops mwl_mac80211_ops = {
	.tx                         = mwl_mac80211_tx,
	.start                      = mwl_mac80211_start,
	.stop                       = mwl_mac80211_stop,
	.add_interface              = mwl_mac80211_add_interface,
	.remove_interface           = mwl_mac80211_remove_interface,
	.config                     = mwl_mac80211_config,
	.sta_rc_update              = mwl_mac80211_sta_rc_update,
	.bss_info_changed           = mwl_mac80211_bss_info_changed,
	.configure_filter           = mwl_mac80211_configure_filter,
	.set_key                    = mwl_mac80211_set_key,
	.set_rts_threshold          = mwl_mac80211_set_rts_threshold,
	.sta_add                    = mwl_mac80211_sta_add,
	.sta_remove                 = mwl_mac80211_sta_remove,
	.conf_tx                    = mwl_mac80211_conf_tx,
	.get_stats                  = mwl_mac80211_get_stats,
	.get_survey                 = mwl_mac80211_get_survey,
	.ampdu_action               = mwl_mac80211_ampdu_action,
	.pre_channel_switch         = mwl_mac80211_chnl_switch,
	.remain_on_channel          = mwl_mac80211_remain_on_channel,
	.cancel_remain_on_channel   = mwl_mac80211_cancel_remain_on_channel,
	.sw_scan_start              = mwl_mac80211_sw_scan_start,
	.sw_scan_complete           = mwl_mac80211_sw_scan_complete,
	.set_default_unicast_key    = mwl_mac80211_set_default_uni_key,

	.set_antenna                = mwl_mac80211_set_ant,
	.get_antenna                = mwl_mac80211_get_ant,

};
