/*
 * Copyright (C) 2017, Laird, PLC.
 *
 * This software file (the "File") is distributed by Laird, PLC.
 * under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include <net/mac80211.h>
#include <linux/netlink.h>
#include <net/genetlink.h>

#include "sysadpt.h"
#include "dev.h"
#include "fwcmd.h"
#include "vendor_cmd.h"

void mwl_hex_dump(const void *buf, size_t len);

static int
lrd_vendor_cmd_mfg_start(struct wiphy *wiphy, struct wireless_dev *wdev,
			       const void *data, int data_len)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct sk_buff  *msg = NULL;
	int rc  = -ENOSYS;
	u32 rsp = 0;

	//Send
	rc = lrd_fwcmd_mfg_start(hw, &rsp);

	//Respond
	msg = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, sizeof(u32) * 2);

	if (msg) {
		nla_put_u32(msg, LRD_ATTR_CMD_RSP, rc);
		nla_put(msg, LRD_ATTR_DATA, sizeof(rsp), &rsp);
		rc = cfg80211_vendor_cmd_reply(msg);
	}
	else {
		rc = -ENOMEM;
	}

	return rc;
}

static int
lrd_vendor_cmd_mfg_write(struct wiphy *wiphy, struct wireless_dev *wdev,
			       const void *data, int data_len)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct sk_buff  *msg = NULL;
	int rc = -ENOSYS;

	//Send
	rc = lrd_fwcmd_mfg_write(hw, (void*)data, data_len);

	//Respond
	msg = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, sizeof(uint32_t));

	if (msg) {
		nla_put_u32(msg, LRD_ATTR_CMD_RSP, rc);
		rc = cfg80211_vendor_cmd_reply(msg);
	}
	else {
		rc = -ENOMEM;
	}

	return rc;
}

static int
lrd_vendor_cmd_mfg_stop(struct wiphy *wiphy, struct wireless_dev *wdev,
			       const void *data, int data_len)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct sk_buff  *msg = NULL;
	int rc = -ENOSYS;

	//Send
	rc = lrd_fwcmd_mfg_end(hw);

	//Respond
	msg = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, sizeof(uint32_t));

	if (msg) {
		nla_put_u32(msg, LRD_ATTR_CMD_RSP, rc);
		rc = cfg80211_vendor_cmd_reply(msg);
	}
	else {
		rc = -ENOMEM;
	}

	return rc;
}

static const struct wiphy_vendor_command lrd_vendor_commands[] = {
	{
		.info = {
			.vendor_id = LRD_OUI,
			.subcmd    = LRD_VENDOR_CMD_MFG_START,
		},
		.flags = 0,
		.doit  = lrd_vendor_cmd_mfg_start,
	},
	{
		.info = {
			.vendor_id = LRD_OUI,
			.subcmd    = LRD_VENDOR_CMD_MFG_WRITE,
		},
		.flags = 0,
		.doit  = lrd_vendor_cmd_mfg_write,
	},
	{
		.info = {
			.vendor_id = LRD_OUI,
			.subcmd    = LRD_VENDOR_CMD_MFG_STOP,
		},
		.flags = 0,
		.doit  = lrd_vendor_cmd_mfg_stop,
	},
};


void lrd_set_vendor_commands(struct wiphy *wiphy)
{
	wiphy->vendor_commands   = lrd_vendor_commands;
	wiphy->n_vendor_commands = ARRAY_SIZE(lrd_vendor_commands);
}
