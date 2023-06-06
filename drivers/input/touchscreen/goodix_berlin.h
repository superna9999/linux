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
#define GOODIX_MAX_FRAMEDATA_LEN 1700

#define GOODIX_NORMAL_RESET_DELAY_MS 100
#define GOODIX_HOLD_CPU_RESET_DELAY_MS 5

enum CHECKSUM_MODE {
	CHECKSUM_MODE_U8_LE,
	CHECKSUM_MODE_U16_LE,
};

#define MAX_SCAN_FREQ_NUM	8
#define MAX_SCAN_RATE_NUM	8
#define MAX_FREQ_NUM_STYLUS	8

#define IRQ_EVENT_HEAD_LEN	8
#define BYTES_PER_POINT		8
#define COOR_DATA_CHECKSUM_SIZE 2

#define GOODIX_TOUCH_EVENT	BIT(7)
#define GOODIX_REQUEST_EVENT	BIT(6)
#define GOODIX_GESTURE_EVENT	BIT(5)

#define POINT_TYPE_STYLUS_HOVER	0x01
#define POINT_TYPE_STYLUS	0x03

#define DEV_CONFIRM_VAL		0xAA
#define BOOTOPTION_ADDR		0x10000
#define FW_VERSION_INFO_ADDR	0x10014

#define GOODIX_IC_INFO_MAX_LEN	1024
#define GOODIX_IC_INFO_ADDR	0x10070

enum brl_request_code {
	BRL_REQUEST_CODE_CONFIG = 1,
	BRL_REQUEST_CODE_REF_ERR = 2,
	BRL_REQUEST_CODE_RESET = 3,
	BRL_REQUEST_CODE_CLOCK = 4,
};

struct goodix_fw_version {
	u8 rom_pid[6]; /* rom PID */
	u8 rom_vid[3]; /* Mask VID */
	u8 rom_vid_reserved;
	u8 patch_pid[8]; /* Patch PID */
	u8 patch_vid[4]; /* Patch VID */
	u8 patch_vid_reserved;
	u8 sensor_id;
	u8 reserved[2];
	u16 checksum;
} __packed;

struct goodix_ic_info_version {
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

struct goodix_ic_info_feature { /* feature info*/
	u16 freqhop_feature;
	u16 calibration_feature;
	u16 gesture_feature;
	u16 side_touch_feature;
	u16 stylus_feature;
} __packed;

struct goodix_ic_info_param { /* param */
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

struct goodix_ic_info_misc { /* other data */
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

struct goodix_ic_info {
	u16 length;
	struct goodix_ic_info_version version;
	struct goodix_ic_info_feature feature;
	struct goodix_ic_info_param parm;
	struct goodix_ic_info_misc misc;
} __packed;

/* interrupt event type */
enum ts_event_type {
	EVENT_INVALID,
	EVENT_TOUCH, /* finger touch event */
	EVENT_REQUEST,
};

enum ts_request_type {
	REQUEST_TYPE_RESET,
};

enum touch_point_status {
	TS_NONE,
	TS_RELEASE,
	TS_TOUCH,
};

/* coordinate package */
struct goodix_berlin_coords {
	int status; /* NONE, RELEASE, TOUCH */
	unsigned int x, y, w, p;
};

/* touch event data */
struct goodix_touch_data {
	int touch_num;
	struct goodix_berlin_coords coords[GOODIX_MAX_TOUCH];
};

/* touch event struct */
struct goodix_berlin_event {
	enum ts_event_type event_type;
	u8 request_code; /* represent the request type */
	struct goodix_touch_data touch_data;
};

struct goodix_berlin_core {
	struct device *dev;
	struct regmap *regmap;

	struct regulator *avdd;
	struct regulator *iovdd;
	struct gpio_desc *reset_gpio;

	struct touchscreen_properties props;

	struct goodix_fw_version fw_version;
	struct goodix_ic_info ic_info;

	struct input_dev *input_dev;
	struct goodix_berlin_event ts_event;

	int irq;

	struct dentry *debugfs_root;
};

u32 goodix_append_checksum(u8 *data, int len, int mode);
int checksum_cmp(const u8 *data, int size, int mode);
int is_risk_data(const u8 *data, int size);
u32 goodix_get_file_config_id(u8 *ic_config);
void goodix_rotate_abcd2cbad(int tx, int rx, s16 *data);

int goodix_berlin_probe(struct device *dev, int irq, const struct input_id *id,
			struct regmap *regmap);
int goodix_berlin_remove(struct device *dev);

extern const struct dev_pm_ops goodix_berlin_pm_ops;

#endif
