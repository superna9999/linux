/*
 * Amlogic Meson GXL Internal PHY Driver
 *
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 * Copyright (C) 2016 BayLibre, SAS. All rights reserved.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/bitfield.h>

#define TSTCNTL		0x14
#define TSTREAD1	0x15
#define TSTREAD2	0x16
#define TSTWRITE	0x17

#define TSTCNTL_READ		BIT(15)
#define TSTCNTL_WRITE		BIT(14)
#define TSTCNTL_REG_BANK_SEL	GENMASK(12, 11)
#define TSTCNTL_TEST_MODE	BIT(10)
#define TSTCNTL_READ_ADDRESS	GENMASK(9, 5)
#define TSTCNTL_WRITE_ADDRESS	GENMASK(4, 0)

#define BANK_ANALOG_DSP		0
#define BANK_BIST		3

/* Analog/DSP Registers */
#define A6_CONFIG_REG		0x17

/* BIST Registers */
#define FR_PLL_CONTROL		0x1b
#define FR_PLL_DIV0		0x1c
#define FR_PLL_DIV1		0x1d

#define A6_CONFIG_PLLMULX4ICH		BIT(15)
#define A6_CONFIG_PLLBIASSEL		BIT(14)
#define A6_CONFIG_PLLINTRATIO		GENMASK(13, 12)
#define A6_CONFIG_PLLBUFITRIM		GENMASK(11, 9)
#define A6_CONFIG_PLLCHTRIM		GENMASK(8, 5)
#define A6_CONFIG_PLLCHBIASSEL		BIT(4)
#define A6_CONFIG_PLLRSTVCOPD		BIT(3)
#define A6_CONFIG_PLLCPOFF		BIT(2)
#define A6_CONFIG_PLLPD			BIT(1)
#define A6_CONFIG_PLL_SRC		BIT(0)

static inline int meson_gxl_write_reg(struct phy_device *phydev,
				      unsigned int bank, unsigned reg,
				      uint16_t value)
{
	int ret = phy_write(phydev, TSTWRITE, value);

	if (ret)
		return ret;

	ret = phy_write(phydev, TSTCNTL, TSTCNTL_WRITE |
				FIELD_PREP(TSTCNTL_REG_BANK_SEL, bank) |
				TSTCNTL_TEST_MODE |
				FIELD_PREP(TSTCNTL_WRITE_ADDRESS, reg));

	return ret;
}

static int meson_gxl_config_init(struct phy_device *phydev)
{
	int ret;

	/*
	 * Enable Analog and DSP register Bank access by
	 * toggling TSTCNTL_TEST_MODE bit in the TSTCNTL register
	 */
	ret = phy_write(phydev, TSTCNTL, 0);
	if (ret)
		return ret;
	ret = phy_write(phydev, TSTCNTL, TSTCNTL_TEST_MODE);
	if (ret)
		return ret;
	ret = phy_write(phydev, TSTCNTL, 0);
	if (ret)
		return ret;
	ret = phy_write(phydev, TSTCNTL, TSTCNTL_TEST_MODE);
	if (ret)
		return ret;

	/* Write PLL Configuration 1 */
	ret = meson_gxl_write_reg(phydev, BANK_ANALOG_DSP, A6_CONFIG_REG,
				  A6_CONFIG_PLLMULX4ICH |
				  FIELD_PREP(A6_CONFIG_PLLBUFITRIM, 7) |
				  A6_CONFIG_PLLRSTVCOPD |
				  A6_CONFIG_PLLCPOFF |
				  A6_CONFIG_PLL_SRC);
	if (ret)
		return ret;

	/* Enable fractional PLL configuration */
	ret = meson_gxl_write_reg(phydev, BANK_BIST, FR_PLL_CONTROL, 0x5);
	if (ret)
		return ret;

	/* Program fraction FR_PLL_DIV1 */
	ret = meson_gxl_write_reg(phydev, BANK_BIST, FR_PLL_DIV1, 0x029a);
	if (ret)
		return ret;

	/* Program fraction FR_PLL_DIV0 */
	ret = meson_gxl_write_reg(phydev, BANK_BIST, FR_PLL_DIV0, 0xaaaa);

	return ret;
}

static struct phy_driver meson_gxl_phy[] = {
	{
		.phy_id		= 0x01814400,
		.phy_id_mask	= 0xfffffff0,
		.name		= "Meson GXL Internal PHY",
		.features	= PHY_BASIC_FEATURES,
		.flags		= PHY_IS_INTERNAL,
		.config_init	= meson_gxl_config_init,
		.config_aneg	= genphy_config_aneg,
		.aneg_done      = genphy_aneg_done,
		.read_status	= genphy_read_status,
		.suspend        = genphy_suspend,
		.resume         = genphy_resume,
	},
};

static struct mdio_device_id __maybe_unused meson_gxl_tbl[] = {
	{ 0x01814400, 0xfffffff0 },
	{ }
};

module_phy_driver(meson_gxl_phy);

MODULE_DEVICE_TABLE(mdio, meson_gxl_tbl);

MODULE_DESCRIPTION("Amlogic Meson GXL Internal PHY driver");
MODULE_AUTHOR("Baoqi wang");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("GPL");
