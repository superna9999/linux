/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef QCOM_PHY_QMP_PCS_V10_H_
#define QCOM_PHY_QMP_PCS_V10_H_

/* Only for QMP V10 PHY - PCIe PCS registers */
#define QPHY_V10_PCS_SW_RESET				0x000
#define QPHY_V10_PCS_PCS_STATUS1			0x014
#define QPHY_V10_PCS_POWER_DOWN_CONTROL			0x040
#define QPHY_V10_PCS_START_CONTROL			0x044
#define QPHY_V10_PCS_REFGEN_REQ_CONFIG1			0x0dc
#define QPHY_V10_PCS_G12S1_TXDEEMPH_M6DB		0x168
#define QPHY_V10_PCS_G3S2_PRE_GAIN			0x170
#define QPHY_V10_PCS_RX_SIGDET_LVL			0x188
#define QPHY_V10_PCS_RATE_SLEW_CNTRL1			0x198
#define QPHY_V10_PCS_PCS_TX_RX_CONFIG			0x1d0
#define QPHY_V10_PCS_EQ_CONFIG2				0x1e4

#endif
