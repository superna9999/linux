/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Goodix Touchscreen Driver
 * Copyright (C) 2020 - 2021 Goodix, Inc.
 * Copyright (C) 2023 Linaro Ltd.
 *
 * Based on goodix_berlin_berlin driver.
 */

#ifndef __GOODIX_BERLIN_H_
#define __GOODIX_BERLIN_H_

#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/input/touchscreen.h>
#include <linux/regulator/consumer.h>
#include <linux/sizes.h>

#define GOODIX_BERLIN_MAX_TOUCH			10

#define GOODIX_BERLIN_NORMAL_RESET_DELAY_MS	100

#define GOODIX_BERLIN_IRQ_EVENT_HEAD_LEN	8
#define GOODIX_BERLIN_STATUS_OFFSET		0
#define GOODIX_BERLIN_REQUEST_TYPE_OFFSET	2

#define GOODIX_BERLIN_BYTES_PER_POINT		8
#define GOODIX_BERLIN_COOR_DATA_CHECKSUM_SIZE	2
#define GOODIX_BERLIN_COOR_DATA_CHECKSUM_MASK	GENMASK(15, 0)

/* Read n finger events */
#define GOODIX_BERLIN_IRQ_READ_LEN(n)		(GOODIX_BERLIN_IRQ_EVENT_HEAD_LEN + \
						 (GOODIX_BERLIN_BYTES_PER_POINT * (n)) + \
						 GOODIX_BERLIN_COOR_DATA_CHECKSUM_SIZE)

#define GOODIX_BERLIN_TOUCH_EVENT		BIT(7)
#define GOODIX_BERLIN_REQUEST_EVENT		BIT(6)
#define GOODIX_BERLIN_TOUCH_COUNT_MASK		GENMASK(3, 0)

#define GOODIX_BERLIN_REQUEST_CODE_RESET	3

#define GOODIX_BERLIN_POINT_TYPE_MASK		GENMASK(3, 0)
#define GOODIX_BERLIN_POINT_TYPE_STYLUS_HOVER	1
#define GOODIX_BERLIN_POINT_TYPE_STYLUS		3

#define GOODIX_BERLIN_TOUCH_ID_MASK		GENMASK(7, 4)

#define GOODIX_BERLIN_DEV_CONFIRM_VAL		0xAA
#define GOODIX_BERLIN_BOOTOPTION_ADDR		0x10000
#define GOODIX_BERLIN_FW_VERSION_INFO_ADDR	0x10014

#define GOODIX_BERLIN_IC_INFO_MAX_LEN		SZ_1K
#define GOODIX_BERLIN_IC_INFO_ADDR		0x10070

struct goodix_berlin_fw_version {
	u8 rom_pid[6];
	u8 rom_vid[3];
	u8 rom_vid_reserved;
	u8 patch_pid[8];
	u8 patch_vid[4];
	u8 patch_vid_reserved;
	u8 sensor_id;
	u8 reserved[2];
	__le16 checksum;
} __packed;

struct goodix_berlin_ic_info_version {
	u8 info_customer_id;
	u8 info_version_id;
	u8 ic_die_id;
	u8 ic_version_id;
	__le32 config_id;
	u8 config_version;
	u8 frame_data_customer_id;
	u8 frame_data_version_id;
	u8 touch_data_customer_id;
	u8 touch_data_version_id;
	u8 reserved[3];
} __packed;

struct goodix_berlin_ic_info_feature {
	__le16 freqhop_feature;
	__le16 calibration_feature;
	__le16 gesture_feature;
	__le16 side_touch_feature;
	__le16 stylus_feature;
} __packed;

struct goodix_berlin_ic_info_misc {
	__le32 cmd_addr;
	__le16 cmd_max_len;
	__le32 cmd_reply_addr;
	__le16 cmd_reply_len;
	__le32 fw_state_addr;
	__le16 fw_state_len;
	__le32 fw_buffer_addr;
	__le16 fw_buffer_max_len;
	__le32 frame_data_addr;
	__le16 frame_data_head_len;
	__le16 fw_attr_len;
	__le16 fw_log_len;
	u8 pack_max_num;
	u8 pack_compress_version;
	__le16 stylus_struct_len;
	__le16 mutual_struct_len;
	__le16 self_struct_len;
	__le16 noise_struct_len;
	__le32 touch_data_addr;
	__le16 touch_data_head_len;
	__le16 point_struct_len;
	__le16 reserved1;
	__le16 reserved2;
	__le32 mutual_rawdata_addr;
	__le32 mutual_diffdata_addr;
	__le32 mutual_refdata_addr;
	__le32 self_rawdata_addr;
	__le32 self_diffdata_addr;
	__le32 self_refdata_addr;
	__le32 iq_rawdata_addr;
	__le32 iq_refdata_addr;
	__le32 im_rawdata_addr;
	__le16 im_readata_len;
	__le32 noise_rawdata_addr;
	__le16 noise_rawdata_len;
	__le32 stylus_rawdata_addr;
	__le16 stylus_rawdata_len;
	__le32 noise_data_addr;
	__le32 esd_addr;
} __packed;

struct goodix_berlin_touch_data {
	u8 id;
	u8 unused;
	__le16 x;
	__le16 y;
	__le16 w;
} __packed;

struct goodix_berlin_core {
	struct device *dev;
	struct regmap *regmap;
	struct regulator *avdd;
	struct regulator *iovdd;
	struct gpio_desc *reset_gpio;
	struct touchscreen_properties props;
	struct goodix_berlin_fw_version fw_version;
	struct input_dev *input_dev;
	int irq;

	/* Runtime parameters extracted from IC_INFO buffer  */
	u32 touch_data_addr;
};

int goodix_berlin_probe(struct device *dev, int irq, const struct input_id *id,
			struct regmap *regmap);

extern const struct dev_pm_ops goodix_berlin_pm_ops;

#endif
