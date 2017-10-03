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

/* Description:  This file implements firmware download related
 * functions.
 */

#include <linux/io.h>

#include "sysadpt.h"
#include "dev.h"
#include "fwcmd.h"
#include "fwdl.h"

#define FW_DOWNLOAD_BLOCK_SIZE          256
#define FW_CHECK_MSECS                  3

#define FW_MAX_NUM_CHECKS               0xffff

int mwl_fwdl_download_firmware(struct ieee80211_hw *hw)
{
	struct mwl_priv *priv = hw->priv;
	int rc;

	mwl_fwcmd_reset(hw);

	/* FW before jumping to boot rom, it will enable PCIe transaction retry,
	 * wait for boot code to stop it.
	 */
	mdelay(FW_CHECK_MSECS);

	rc = priv->if_ops.prog_fw(priv);
	if (rc)
		goto err_download;

	return 0;

err_download:

	mwl_fwcmd_reset(hw);

	return -EIO;
}
