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

#include <linux/input.h>
#include <linux/of_gpio.h>
#include <linux/input/touchscreen.h>
#include <linux/regulator/consumer.h>

#define GOODIX_MAX_TOUCH 10

#define GOODIX_NORMAL_RESET_DELAY_MS 100

#define MAX_SCAN_FREQ_NUM	8
#define MAX_SCAN_RATE_NUM	8
#define MAX_FREQ_NUM_STYLUS	8

#define IRQ_EVENT_HEAD_LEN	8
#define BYTES_PER_POINT		8
#define COOR_DATA_CHECKSUM_SIZE 2

#define GOODIX_TOUCH_EVENT	BIT(7)
#define GOODIX_REQUEST_EVENT	BIT(6)

#define GOODIX_REQUEST_CODE_RESET	3

#define POINT_TYPE_STYLUS_HOVER	0x01
#define POINT_TYPE_STYLUS	0x03

#define DEV_CONFIRM_VAL		0xAA
#define BOOTOPTION_ADDR		0x10000
#define FW_VERSION_INFO_ADDR	0x10014

#define GOODIX_IC_INFO_MAX_LEN	1024
#define GOODIX_IC_INFO_ADDR	0x10070

struct goodix_berlin_fw_version {
	u8 rom_pid[6];
	u8 rom_vid[3];
	u8 rom_vid_reserved;
	u8 patch_pid[8];
	u8 patch_vid[4];
	u8 patch_vid_reserved;
	u8 sensor_id;
	u8 reserved[2];
	u16 checksum;
} __packed;

struct goodix_berlin_ic_info_version {
	u8 info_customer_id;
	u8 info_version_id;
	u8 ic_die_id;
	u8 ic_version_id;
	u32 config_id;
	u8 config_version;
	u8 frame_data_customer_id;
	u8 frame_data_version_id;
	u8 touch_data_customer_id;
	u8 touch_data_version_id;
	u8 reserved[3];
} __packed;

struct goodix_berlin_ic_info_feature {
	u16 freqhop_feature;
	u16 calibration_feature;
	u16 gesture_feature;
	u16 side_touch_feature;
	u16 stylus_feature;
} __packed;

struct goodix_berlin_ic_info_param {
	u8 drv_num;
	u8 sen_num;
	u8 button_num;
	u8 force_num;
	u8 active_scan_rate_num;
	u16 active_scan_rate[MAX_SCAN_RATE_NUM];
	u8 mutual_freq_num;
	u16 mutual_freq[MAX_SCAN_FREQ_NUM];
	u8 self_tx_freq_num;
	u16 self_tx_freq[MAX_SCAN_FREQ_NUM];
	u8 self_rx_freq_num;
	u16 self_rx_freq[MAX_SCAN_FREQ_NUM];
	u8 stylus_freq_num;
	u16 stylus_freq[MAX_FREQ_NUM_STYLUS];
} __packed;

struct goodix_berlin_ic_info_misc {
	u32 cmd_addr;
	u16 cmd_max_len;
	u32 cmd_reply_addr;
	u16 cmd_reply_len;
	u32 fw_state_addr;
	u16 fw_state_len;
	u32 fw_buffer_addr;
	u16 fw_buffer_max_len;
	u32 frame_data_addr;
	u16 frame_data_head_len;
	u16 fw_attr_len;
	u16 fw_log_len;
	u8 pack_max_num;
	u8 pack_compress_version;
	u16 stylus_struct_len;
	u16 mutual_struct_len;
	u16 self_struct_len;
	u16 noise_struct_len;
	u32 touch_data_addr;
	u16 touch_data_head_len;
	u16 point_struct_len;
	u16 reserved1;
	u16 reserved2;
	u32 mutual_rawdata_addr;
	u32 mutual_diffdata_addr;
	u32 mutual_refdata_addr;
	u32 self_rawdata_addr;
	u32 self_diffdata_addr;
	u32 self_refdata_addr;
	u32 iq_rawdata_addr;
	u32 iq_refdata_addr;
	u32 im_rawdata_addr;
	u16 im_readata_len;
	u32 noise_rawdata_addr;
	u16 noise_rawdata_len;
	u32 stylus_rawdata_addr;
	u16 stylus_rawdata_len;
	u32 noise_data_addr;
	u32 esd_addr;
} __packed;

struct goodix_berlin_ic_info {
	u16 length;
	struct goodix_berlin_ic_info_version version;
	struct goodix_berlin_ic_info_feature feature;
	struct goodix_berlin_ic_info_param parm;
	struct goodix_berlin_ic_info_misc misc;
} __packed;

enum goodix_berlin_ts_event_type {
	GOODIX_BERLIN_EVENT_INVALID,
	GOODIX_BERLIN_EVENT_TOUCH,
	GOODIX_BERLIN_EVENT_REQUEST,
};

enum goodix_berlin_ts_request_type {
	GOODIX_BERLIN_REQUEST_TYPE_RESET,
};

enum goodix_berlin_touch_point_status {
	GOODIX_BERLIN_TS_TOUCH,
};

struct goodix_berlin_coords {
	enum goodix_berlin_touch_point_status status;
	unsigned int x, y, w, p;
};

struct goodix_berlin_touch_data {
	int touch_num;
	struct goodix_berlin_coords coords[GOODIX_MAX_TOUCH];
};

struct goodix_berlin_event {
	enum goodix_berlin_ts_event_type event_type;
	enum goodix_berlin_ts_request_type request_code;
	struct goodix_berlin_touch_data touch_data;
};

struct goodix_berlin_core {
	struct device *dev;
	struct regmap *regmap;
	struct regulator *avdd;
	struct regulator *iovdd;
	struct gpio_desc *reset_gpio;
	struct touchscreen_properties props;
	struct goodix_berlin_fw_version fw_version;
	struct goodix_berlin_ic_info ic_info;
	struct input_dev *input_dev;
	struct goodix_berlin_event ts_event;
	int irq;
	struct dentry *debugfs_root;
};

int goodix_berlin_probe(struct device *dev, int irq, const struct input_id *id,
			struct regmap *regmap);

extern const struct dev_pm_ops goodix_berlin_pm_ops;

#endif
