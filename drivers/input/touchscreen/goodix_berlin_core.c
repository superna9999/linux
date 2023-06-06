// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Goodix Touchscreen Driver
 * Copyright (C) 2020 - 2021 Goodix, Inc.
 * Copyright (C) 2023 Linaro Ltd.
 *
 * Based on goodix_ts_berlin driver.
 */
#include <linux/debugfs.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/regmap.h>

#include "goodix_berlin.h"

/*
 * Goodix "Berlin" Touchscreen ID driver
 *
 * Currently only handles Multitouch events with already
 * programmed firmware and "config" for "Revision D" Berlin IC.
 *
 * Support is missing for:
 * - ESD Management
 * - Firmware update/flashing
 * - "Config" update/flashing
 * - Pen/Stylus Events
 * - Gesture Events
 * - Support for older revisions (A, B & C)
 */

static bool goodix_berlin_check_checksum(const u8 *data, int size)
{
	u32 cal_checksum = 0;
	u32 r_checksum = 0;
	u32 i;

	if (size < COOR_DATA_CHECKSUM_SIZE)
		return false;

	for (i = 0; i < size - COOR_DATA_CHECKSUM_SIZE; i++)
		cal_checksum += data[i];

	r_checksum += data[i++];
	r_checksum += (data[i] << 8);

	return (cal_checksum & 0xFFFF) == r_checksum;
}

static bool goodix_berlin_is_risk_data(struct goodix_berlin_core *cd,
				       const u8 *data, int size)
{
	int zero_count = 0;
	int ff_count = 0;
	int i;

	for (i = 0; i < size; i++) {
		if (data[i] == 0)
			zero_count++;
		else if (data[i] == 0xff)
			ff_count++;
	}
	if (zero_count == size || ff_count == size) {
		dev_warn(cd->dev, "warning data is all %s\n",
			 zero_count == size ? "zero" : "0xff");
		return true;
	}

	return false;
}

static int goodix_berlin_dev_confirm(struct goodix_berlin_core *cd)
{
	u8 tx_buf[8], rx_buf[8];
	int retry = 3;
	int error;

	memset(tx_buf, DEV_CONFIRM_VAL, sizeof(tx_buf));
	while (retry--) {
		error = regmap_raw_write(cd->regmap, BOOTOPTION_ADDR, tx_buf,
					 sizeof(tx_buf));
		if (error < 0)
			return error;

		error = regmap_raw_read(cd->regmap, BOOTOPTION_ADDR, rx_buf,
					sizeof(rx_buf));
		if (error < 0)
			return error;

		if (!memcmp(tx_buf, rx_buf, sizeof(tx_buf)))
			return 0;

		usleep_range(5000, 5100);
	}

	dev_err(cd->dev, "device confirm failed, rx_buf: %*ph\n", 8, rx_buf);

	return -EINVAL;
}

static int goodix_berlin_power_on(struct goodix_berlin_core *cd, bool on)
{
	int error;

	if (on) {
		error = regulator_enable(cd->iovdd);
		if (error < 0) {
			dev_err(cd->dev, "Failed to enable iovdd: %d\n", error);
			return error;
		}

		/* Vendor waits 3ms for IOVDD to settle */
		usleep_range(3000, 3100);

		error = regulator_enable(cd->avdd);
		if (error < 0) {
			dev_err(cd->dev, "Failed to enable avdd: %d\n", error);
			goto power_off_iovdd;
		}

		/* Vendor waits 15ms for IOVDD to settle */
		usleep_range(15000, 15100);

		gpiod_set_value(cd->reset_gpio, 0);

		/* Vendor waits 4ms for Firmware to initialize */
		usleep_range(4000, 4100);

		error = goodix_berlin_dev_confirm(cd);
		if (error < 0)
			goto power_off_gpio;

		/* Vendor waits 100ms for Firmware to fully boot */
		msleep(GOODIX_NORMAL_RESET_DELAY_MS);
	}

	return 0;

power_off_gpio:
	gpiod_set_value(cd->reset_gpio, 1);

	regulator_disable(cd->avdd);

power_off_iovdd:
	regulator_disable(cd->iovdd);

	return error;
}

static int goodix_berlin_read_version(struct goodix_berlin_core *cd,
				      struct goodix_berlin_fw_version *version)
{
	u8 buf[sizeof(struct goodix_berlin_fw_version)];
	int retry = 2;
	int error;

	while (retry--) {
		error = regmap_raw_read(cd->regmap, FW_VERSION_INFO_ADDR, buf, sizeof(buf));
		if (error) {
			dev_dbg(cd->dev, "read fw version: %d, retry %d\n", error, retry);
			usleep_range(5000, 5100);
			continue;
		}

		if (goodix_berlin_check_checksum(buf, sizeof(buf)))
			break;

		dev_dbg(cd->dev, "invalid fw version: checksum error\n");

		error = -EINVAL;

		/* Do not sleep on the last try */
		if (retry)
			usleep_range(10000, 11000);
	}

	if (error) {
		dev_err(cd->dev, "failed to get fw version");
		return error;
	}

	memcpy(version, buf, sizeof(*version));

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int goodix_berlin_fw_version_show(struct seq_file *s, void *unused)
{
	struct goodix_berlin_core *cd = s->private;
	struct goodix_berlin_fw_version *version = &cd->fw_version;
	u8 temp_pid[9];

	memcpy(temp_pid, version->rom_pid, sizeof(version->rom_pid));
	temp_pid[sizeof(version->rom_pid)] = '\0';

	seq_printf(s, "rom_pid: %s\n", temp_pid);
	seq_printf(s, "rom_vid: %*ph\n",
		   (int)sizeof(version->rom_vid),
		   version->rom_vid);

	memcpy(temp_pid, version->patch_pid, sizeof(version->patch_pid));
	temp_pid[sizeof(version->patch_pid)] = '\0';

	seq_printf(s, "patch_pid: %s\n", temp_pid);
	seq_printf(s, "patch_vid: %*ph\n",
		   (int)sizeof(version->patch_vid),
		   version->patch_vid);
	seq_printf(s, "sensor_id: %d\n",
		   version->sensor_id);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(goodix_berlin_fw_version);
#endif

static int goodix_berlin_convert_ic_info(struct goodix_berlin_core *cd,
					 struct goodix_berlin_ic_info *info,
					 const u8 *data)
{
	int i;
	struct goodix_berlin_ic_info_version *version = &info->version;
	struct goodix_berlin_ic_info_feature *feature = &info->feature;
	struct goodix_berlin_ic_info_param *parm = &info->parm;
	struct goodix_berlin_ic_info_misc *misc = &info->misc;

	info->length = le16_to_cpup((__le16 *)data);

	data += 2;
	memcpy(version, data, sizeof(*version));
	version->config_id = le32_to_cpu(version->config_id);

	data += sizeof(struct goodix_berlin_ic_info_version);
	memcpy(feature, data, sizeof(*feature));
	feature->freqhop_feature = le16_to_cpu(feature->freqhop_feature);
	feature->calibration_feature =
		le16_to_cpu(feature->calibration_feature);
	feature->gesture_feature = le16_to_cpu(feature->gesture_feature);
	feature->side_touch_feature = le16_to_cpu(feature->side_touch_feature);
	feature->stylus_feature = le16_to_cpu(feature->stylus_feature);

	data += sizeof(struct goodix_berlin_ic_info_feature);
	parm->drv_num = *(data++);
	parm->sen_num = *(data++);
	parm->button_num = *(data++);
	parm->force_num = *(data++);
	parm->active_scan_rate_num = *(data++);

	if (parm->active_scan_rate_num > MAX_SCAN_RATE_NUM) {
		dev_err(cd->dev, "invalid scan rate num %d > %d\n",
			parm->active_scan_rate_num, MAX_SCAN_RATE_NUM);
		return -EINVAL;
	}

	for (i = 0; i < parm->active_scan_rate_num; i++)
		parm->active_scan_rate[i] =
			le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->active_scan_rate_num * 2;
	parm->mutual_freq_num = *(data++);

	if (parm->mutual_freq_num > MAX_SCAN_FREQ_NUM) {
		dev_err(cd->dev, "invalid mntual freq num %d > %d\n",
			parm->mutual_freq_num, MAX_SCAN_FREQ_NUM);
		return -EINVAL;
	}

	for (i = 0; i < parm->mutual_freq_num; i++)
		parm->mutual_freq[i] = le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->mutual_freq_num * 2;
	parm->self_tx_freq_num = *(data++);

	if (parm->self_tx_freq_num > MAX_SCAN_FREQ_NUM) {
		dev_err(cd->dev, "invalid tx freq num %d > %d\n",
			parm->self_tx_freq_num, MAX_SCAN_FREQ_NUM);
		return -EINVAL;
	}

	for (i = 0; i < parm->self_tx_freq_num; i++)
		parm->self_tx_freq[i] = le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->self_tx_freq_num * 2;
	parm->self_rx_freq_num = *(data++);

	if (parm->self_rx_freq_num > MAX_SCAN_FREQ_NUM) {
		dev_err(cd->dev, "invalid rx freq num %d > %d\n",
			parm->self_rx_freq_num, MAX_SCAN_FREQ_NUM);
		return -EINVAL;
	}

	for (i = 0; i < parm->self_rx_freq_num; i++)
		parm->self_rx_freq[i] = le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->self_rx_freq_num * 2;
	parm->stylus_freq_num = *(data++);

	if (parm->stylus_freq_num > MAX_FREQ_NUM_STYLUS) {
		dev_err(cd->dev, "invalid stylus freq num %d > %d\n",
			parm->stylus_freq_num, MAX_FREQ_NUM_STYLUS);
		return -EINVAL;
	}

	for (i = 0; i < parm->stylus_freq_num; i++)
		parm->stylus_freq[i] = le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->stylus_freq_num * 2;
	memcpy(misc, data, sizeof(*misc));
	misc->cmd_addr = le32_to_cpu(misc->cmd_addr);
	misc->cmd_max_len = le16_to_cpu(misc->cmd_max_len);
	misc->cmd_reply_addr = le32_to_cpu(misc->cmd_reply_addr);
	misc->cmd_reply_len = le16_to_cpu(misc->cmd_reply_len);
	misc->fw_state_addr = le32_to_cpu(misc->fw_state_addr);
	misc->fw_state_len = le16_to_cpu(misc->fw_state_len);
	misc->fw_buffer_addr = le32_to_cpu(misc->fw_buffer_addr);
	misc->fw_buffer_max_len = le16_to_cpu(misc->fw_buffer_max_len);
	misc->frame_data_addr = le32_to_cpu(misc->frame_data_addr);
	misc->frame_data_head_len = le16_to_cpu(misc->frame_data_head_len);

	misc->fw_attr_len = le16_to_cpu(misc->fw_attr_len);
	misc->fw_log_len = le16_to_cpu(misc->fw_log_len);
	misc->stylus_struct_len = le16_to_cpu(misc->stylus_struct_len);
	misc->mutual_struct_len = le16_to_cpu(misc->mutual_struct_len);
	misc->self_struct_len = le16_to_cpu(misc->self_struct_len);
	misc->noise_struct_len = le16_to_cpu(misc->noise_struct_len);
	misc->touch_data_addr = le32_to_cpu(misc->touch_data_addr);
	misc->touch_data_head_len = le16_to_cpu(misc->touch_data_head_len);
	misc->point_struct_len = le16_to_cpu(misc->point_struct_len);
	misc->mutual_rawdata_addr = le32_to_cpu(misc->mutual_rawdata_addr);
	misc->mutual_diffdata_addr = le32_to_cpu(misc->mutual_diffdata_addr);
	misc->mutual_refdata_addr = le32_to_cpu(misc->mutual_refdata_addr);
	misc->self_rawdata_addr = le32_to_cpu(misc->self_rawdata_addr);
	misc->self_diffdata_addr = le32_to_cpu(misc->self_diffdata_addr);
	misc->self_refdata_addr = le32_to_cpu(misc->self_refdata_addr);
	misc->iq_rawdata_addr = le32_to_cpu(misc->iq_rawdata_addr);
	misc->iq_refdata_addr = le32_to_cpu(misc->iq_refdata_addr);
	misc->im_rawdata_addr = le32_to_cpu(misc->im_rawdata_addr);
	misc->im_readata_len = le16_to_cpu(misc->im_readata_len);
	misc->noise_rawdata_addr = le32_to_cpu(misc->noise_rawdata_addr);
	misc->noise_rawdata_len = le16_to_cpu(misc->noise_rawdata_len);
	misc->stylus_rawdata_addr = le32_to_cpu(misc->stylus_rawdata_addr);
	misc->stylus_rawdata_len = le16_to_cpu(misc->stylus_rawdata_len);
	misc->noise_data_addr = le32_to_cpu(misc->noise_data_addr);
	misc->esd_addr = le32_to_cpu(misc->esd_addr);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int goodix_berlin_ic_info_show(struct seq_file *s, void *unused)
{
	struct goodix_berlin_core *cd = s->private;
	struct goodix_berlin_ic_info_version *version = &cd->ic_info.version;
	struct goodix_berlin_ic_info_feature *feature = &cd->ic_info.feature;
	struct goodix_berlin_ic_info_param *parm = &cd->ic_info.parm;
	struct goodix_berlin_ic_info_misc *misc = &cd->ic_info.misc;

	seq_printf(s, "ic_info_length:                %d\n", cd->ic_info.length);
	seq_printf(s, "info_customer_id:              0x%01X\n",
		   version->info_customer_id);
	seq_printf(s, "info_version_id:               0x%01X\n",
		   version->info_version_id);
	seq_printf(s, "ic_die_id:                     0x%01X\n",
		   version->ic_die_id);
	seq_printf(s, "ic_version_id:                 0x%01X\n",
		   version->ic_version_id);
	seq_printf(s, "config_id:                     0x%4X\n",
		   version->config_id);
	seq_printf(s, "config_version:                0x%01X\n",
		   version->config_version);
	seq_printf(s, "frame_data_customer_id:        0x%01X\n",
		   version->frame_data_customer_id);
	seq_printf(s, "frame_data_version_id:         0x%01X\n",
		   version->frame_data_version_id);
	seq_printf(s, "touch_data_customer_id:        0x%01X\n",
		   version->touch_data_customer_id);
	seq_printf(s, "touch_data_version_id:         0x%01X\n",
		   version->touch_data_version_id);
	seq_printf(s, "freqhop_feature:               0x%04X\n",
		   feature->freqhop_feature);
	seq_printf(s, "calibration_feature:           0x%04X\n",
		   feature->calibration_feature);
	seq_printf(s, "gesture_feature:               0x%04X\n",
		   feature->gesture_feature);
	seq_printf(s, "side_touch_feature:            0x%04X\n",
		   feature->side_touch_feature);
	seq_printf(s, "stylus_feature:                0x%04X\n",
		   feature->stylus_feature);
	seq_printf(s, "Drv*Sen,Button,Force num:      %d x %d, %d, %d\n",
		   parm->drv_num, parm->sen_num, parm->button_num, parm->force_num);
	seq_printf(s, "Cmd:                           0x%04X, %d\n",
		   misc->cmd_addr, misc->cmd_max_len);
	seq_printf(s, "Cmd-Reply:                     0x%04X, %d\n",
		   misc->cmd_reply_addr, misc->cmd_reply_len);
	seq_printf(s, "FW-State:                      0x%04X, %d\n",
		   misc->fw_state_addr, misc->fw_state_len);
	seq_printf(s, "FW-Buffer:                     0x%04X, %d\n",
		   misc->fw_buffer_addr, misc->fw_buffer_max_len);
	seq_printf(s, "Touch-Data:                    0x%04X, %d\n",
		   misc->touch_data_addr, misc->touch_data_head_len);
	seq_printf(s, "point_struct_len:              %d\n",
		   misc->point_struct_len);
	seq_printf(s, "mutual_rawdata_addr:           0x%04X\n",
		   misc->mutual_rawdata_addr);
	seq_printf(s, "mutual_diffdata_addr:          0x%04X\n",
		   misc->mutual_diffdata_addr);
	seq_printf(s, "self_rawdata_addr:             0x%04X\n",
		   misc->self_rawdata_addr);
	seq_printf(s, "self_diffdata_addr:            0x%04X\n",
		   misc->self_diffdata_addr);
	seq_printf(s, "stylus_rawdata_addr:           0x%04X, %d\n",
		   misc->stylus_rawdata_addr, misc->stylus_rawdata_len);
	seq_printf(s, "esd_addr:                      0x%04X\n",
		   misc->esd_addr);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(goodix_berlin_ic_info);
#endif

static int goodix_berlin_get_ic_info(struct goodix_berlin_core *cd,
				     struct goodix_berlin_ic_info *ic_info)
{
	int error, i;
	u16 length = 0;
	u32 ic_addr;
	u8 afe_data[GOODIX_IC_INFO_MAX_LEN] = { 0 };

	ic_addr = GOODIX_IC_INFO_ADDR;

	for (i = 0; i < 3; i++) {
		error = regmap_raw_read(cd->regmap, ic_addr, (u8 *)&length, sizeof(length));
		if (error) {
			dev_info(cd->dev, "failed get ic info length, %d\n", error);
			usleep_range(5000, 5100);
			continue;
		}

		length = le16_to_cpu(length);
		if (length >= GOODIX_IC_INFO_MAX_LEN) {
			dev_info(cd->dev, "invalid ic info length %d, errorry %d\n", length, i);
			continue;
		}

		error = regmap_raw_read(cd->regmap, ic_addr, afe_data, length);
		if (error) {
			dev_info(cd->dev, "failed get ic info data, %d\n", error);
			usleep_range(5000, 5100);
			continue;
		}

		/* judge whether the data is valid */
		if (goodix_berlin_is_risk_data(cd, (const uint8_t *)afe_data,
					       length)) {
			dev_info(cd->dev, "fw info data invalid\n");
			usleep_range(5000, 5100);
			continue;
		}

		if (!goodix_berlin_check_checksum((const uint8_t *)afe_data, length)) {
			dev_info(cd->dev, "fw info checksum error\n");
			usleep_range(5000, 5100);
			continue;
		}

		break;
	}
	if (i == 3) {
		dev_err(cd->dev, "failed get ic info\n");
		return -EINVAL;
	}

	error = goodix_berlin_convert_ic_info(cd, ic_info, afe_data);
	if (error) {
		dev_err(cd->dev, "error converting ic info\n");
		return error;
	}

	/* check some key info */
	if (!ic_info->misc.cmd_addr || !ic_info->misc.fw_buffer_addr ||
	    !ic_info->misc.touch_data_addr) {
		dev_err(cd->dev, "cmd_addr fw_buf_addr and touch_data_addr is null\n");
		return -EINVAL;
	}

	return 0;
}

static int goodix_berlin_after_event_handler(struct goodix_berlin_core *cd)
{
	struct goodix_berlin_ic_info_misc *misc = &cd->ic_info.misc;
	u8 sync_clean = 0;

	return regmap_raw_write(cd->regmap, misc->touch_data_addr, &sync_clean, 1);
}

static void goodix_berlin_parse_finger(struct goodix_berlin_core *cd,
				       struct goodix_berlin_touch_data *touch_data,
				       u8 *buf, int touch_num)
{
	unsigned int id = 0, x = 0, y = 0, w = 0;
	u8 *coor_data;
	int i;

	coor_data = &buf[IRQ_EVENT_HEAD_LEN];

	for (i = 0; i < touch_num; i++) {
		id = (coor_data[0] >> 4) & 0x0F;

		if (id >= GOODIX_MAX_TOUCH) {
			dev_warn(cd->dev, "invalid finger id %d\n", id);

			touch_data->touch_num = 0;
			return;
		}

		x = le16_to_cpup((__le16 *)(coor_data + 2));
		y = le16_to_cpup((__le16 *)(coor_data + 4));
		w = le16_to_cpup((__le16 *)(coor_data + 6));

		touch_data->coords[id].status = GOODIX_BERLIN_TS_TOUCH;
		touch_data->coords[id].x = x;
		touch_data->coords[id].y = y;
		touch_data->coords[id].w = w;

		coor_data += BYTES_PER_POINT;
	}

	touch_data->touch_num = touch_num;
}

static int goodix_berlin_touch_handler(struct goodix_berlin_core *cd,
				       u8 *pre_buf, u32 pre_buf_len)
{
	static u8 buffer[IRQ_EVENT_HEAD_LEN + BYTES_PER_POINT * GOODIX_MAX_TOUCH + 2];
	struct goodix_berlin_touch_data *touch_data = &cd->ts_event.touch_data;
	struct goodix_berlin_ic_info_misc *misc = &cd->ic_info.misc;
	u8 point_type = 0;
	u8 touch_num = 0;
	int error = 0;

	memset(&cd->ts_event, 0, sizeof(cd->ts_event));

	/* copy pre-data to buffer */
	memcpy(buffer, pre_buf, pre_buf_len);

	touch_num = buffer[2] & 0x0F;

	if (touch_num > GOODIX_MAX_TOUCH) {
		dev_warn(cd->dev, "invalid touch num %d\n", touch_num);
		return -EINVAL;
	}

	/* read more data if more than 2 touch events */
	if (unlikely(touch_num > 2)) {
		error = regmap_raw_read(cd->regmap,
					misc->touch_data_addr + pre_buf_len,
					&buffer[pre_buf_len],
					(touch_num - 2) * BYTES_PER_POINT);
		if (error) {
			dev_err(cd->dev, "failed get touch data\n");
			return error;
		}
	}

	goodix_berlin_after_event_handler(cd);

	if (touch_num == 0)
		return 0;

	point_type = buffer[IRQ_EVENT_HEAD_LEN] & 0x0F;

	if (point_type == POINT_TYPE_STYLUS || point_type == POINT_TYPE_STYLUS_HOVER) {
		dev_warn_once(cd->dev, "Stylus event type not handled\n");
		return 0;
	}

	if (!goodix_berlin_check_checksum(&buffer[IRQ_EVENT_HEAD_LEN],
					  touch_num * BYTES_PER_POINT + 2)) {
		dev_dbg(cd->dev, "touch data checksum error\n");
		dev_dbg(cd->dev, "data: %*ph\n",
			touch_num * BYTES_PER_POINT + 2, &buffer[IRQ_EVENT_HEAD_LEN]);
		return -EINVAL;
	}

	cd->ts_event.event_type = GOODIX_BERLIN_EVENT_TOUCH;
	goodix_berlin_parse_finger(cd, touch_data, buffer, touch_num);

	return 0;
}

static int goodix_berlin_event_handler(struct goodix_berlin_core *cd)
{
	struct goodix_berlin_ic_info_misc *misc = &cd->ic_info.misc;
	int pre_read_len;
	u8 pre_buf[32];
	u8 event_status;
	int error;

	pre_read_len = IRQ_EVENT_HEAD_LEN + BYTES_PER_POINT * 2 +
		       COOR_DATA_CHECKSUM_SIZE;

	error = regmap_raw_read(cd->regmap, misc->touch_data_addr, pre_buf,
				pre_read_len);
	if (error) {
		dev_err(cd->dev, "failed get event head data\n");
		return error;
	}

	if (pre_buf[0] == 0x00)
		return -EINVAL;

	if (!goodix_berlin_check_checksum(pre_buf, IRQ_EVENT_HEAD_LEN)) {
		dev_warn(cd->dev, "touch head checksum err : %*ph\n",
			 IRQ_EVENT_HEAD_LEN, pre_buf);
		return -EINVAL;
	}

	event_status = pre_buf[0];
	if (event_status & GOODIX_TOUCH_EVENT)
		return goodix_berlin_touch_handler(cd, pre_buf, pre_read_len);

	if (event_status & GOODIX_REQUEST_EVENT) {
		cd->ts_event.event_type = GOODIX_BERLIN_EVENT_REQUEST;
		if (pre_buf[2] == GOODIX_REQUEST_CODE_RESET)
			cd->ts_event.request_code = GOODIX_BERLIN_REQUEST_TYPE_RESET;
		else
			dev_warn(cd->dev, "unsupported request code 0x%x\n", pre_buf[2]);
	}

	goodix_berlin_after_event_handler(cd);

	return 0;
}

static void goodix_berlin_report_finger(struct goodix_berlin_core *cd)
{
	struct goodix_berlin_touch_data *touch_data = &cd->ts_event.touch_data;
	int i;

	mutex_lock(&cd->input_dev->mutex);

	for (i = 0; i < GOODIX_MAX_TOUCH; i++) {
		if (touch_data->coords[i].status == GOODIX_BERLIN_TS_TOUCH) {
			dev_dbg(cd->dev, "report: id[%d], x %d, y %d, w %d\n", i,
				touch_data->coords[i].x,
				touch_data->coords[i].y,
				touch_data->coords[i].w);

			input_mt_slot(cd->input_dev, i);
			input_mt_report_slot_state(cd->input_dev,
						   MT_TOOL_FINGER, true);
			touchscreen_report_pos(cd->input_dev, &cd->props,
					       touch_data->coords[i].x,
					       touch_data->coords[i].y, true);
			input_report_abs(cd->input_dev, ABS_MT_TOUCH_MAJOR,
					 touch_data->coords[i].w);
		} else {
			input_mt_slot(cd->input_dev, i);
			input_mt_report_slot_state(cd->input_dev,
						   MT_TOOL_FINGER, false);
		}
	}

	input_mt_sync_frame(cd->input_dev);
	input_sync(cd->input_dev);

	mutex_unlock(&cd->input_dev->mutex);
}

static int goodix_berlin_request_handle(struct goodix_berlin_core *cd)
{
	/* TOFIX: Handle more request codes */
	if (cd->ts_event.request_code != GOODIX_BERLIN_REQUEST_TYPE_RESET) {
		dev_info(cd->dev, "can't handle request type 0x%x\n",
			 cd->ts_event.request_code);
		return -EINVAL;
	}

	gpiod_set_value(cd->reset_gpio, 1);
	usleep_range(2000, 2100);
	gpiod_set_value(cd->reset_gpio, 0);

	msleep(GOODIX_NORMAL_RESET_DELAY_MS);

	return 0;
}

static irqreturn_t goodix_berlin_threadirq_func(int irq, void *data)
{
	struct goodix_berlin_core *cd = data;
	int error;

	error = goodix_berlin_event_handler(cd);
	if (likely(!error)) {
		switch (cd->ts_event.event_type) {
		case GOODIX_BERLIN_EVENT_TOUCH:
			goodix_berlin_report_finger(cd);
			break;

		case GOODIX_BERLIN_EVENT_REQUEST:
			goodix_berlin_request_handle(cd);
			break;

		/* TOFIX: Handle more request types */
		case GOODIX_BERLIN_EVENT_INVALID:
			break;
		}
	}

	return IRQ_HANDLED;
}

static int goodix_berlin_input_dev_config(struct goodix_berlin_core *cd,
					  const struct input_id *id)
{
	struct input_dev *input_dev;
	int error;

	input_dev = devm_input_allocate_device(cd->dev);
	if (!input_dev)
		return -ENOMEM;

	cd->input_dev = input_dev;
	input_set_drvdata(input_dev, cd);

	input_dev->name = "Goodix Berlin Capacitive TouchScreen";
	input_dev->phys = "input/ts";
	input_dev->dev.parent = cd->dev;

	memcpy(&input_dev->id, id, sizeof(*id));

	/* Set input parameters */
	input_set_abs_params(cd->input_dev, ABS_MT_POSITION_X, 0, SZ_64K - 1, 0, 0);
	input_set_abs_params(cd->input_dev, ABS_MT_POSITION_Y, 0, SZ_64K - 1, 0, 0);
	input_set_abs_params(cd->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);

	touchscreen_parse_properties(cd->input_dev, true, &cd->props);

	error = input_mt_init_slots(cd->input_dev, GOODIX_MAX_TOUCH, INPUT_MT_DIRECT);
	if (error)
		return error;

	return input_register_device(cd->input_dev);
}

static int goodix_berlin_pm_suspend(struct device *dev)
{
	struct goodix_berlin_core *cd = dev_get_drvdata(dev);

	disable_irq(cd->irq);

	return goodix_berlin_power_on(cd, false);
}

static int goodix_berlin_pm_resume(struct device *dev)
{
	struct goodix_berlin_core *cd = dev_get_drvdata(dev);
	int error;

	error = goodix_berlin_power_on(cd, true);
	if (error)
		return error;

	enable_irq(cd->irq);

	return 0;
}

EXPORT_GPL_SIMPLE_DEV_PM_OPS(goodix_berlin_pm_ops,
			     goodix_berlin_pm_suspend,
			     goodix_berlin_pm_resume);

static void goodix_berlin_power_off(void *data)
{
	struct goodix_berlin_core *cd = data;

	goodix_berlin_power_on(cd, false);
}

static void goodix_berlin_debug_remove(void *data)
{
	struct goodix_berlin_core *cd = data;

	debugfs_remove(cd->debugfs_root);
}

int goodix_berlin_probe(struct device *dev, int irq, const struct input_id *id,
			struct regmap *regmap)
{
	struct goodix_berlin_core *cd;
	int error;

	if (irq <= 0)
		return -EINVAL;

	cd = devm_kzalloc(dev, sizeof(*cd), GFP_KERNEL);
	if (!cd)
		return -ENOMEM;

	cd->dev = dev;
	cd->regmap = regmap;
	cd->irq = irq;

	cd->reset_gpio = devm_gpiod_get_optional(cd->dev, "reset", GPIOF_OUT_INIT_HIGH);
	if (IS_ERR(cd->reset_gpio))
		return dev_err_probe(cd->dev, PTR_ERR(cd->reset_gpio),
				     "Failed to request reset gpio\n");

	cd->avdd = devm_regulator_get(cd->dev, "avdd");
	if (IS_ERR(cd->avdd))
		return dev_err_probe(cd->dev, PTR_ERR(cd->avdd),
				     "Failed to request avdd regulator\n");

	cd->iovdd = devm_regulator_get_optional(cd->dev, "iovdd");
	if (IS_ERR(cd->iovdd))
		return dev_err_probe(cd->dev, PTR_ERR(cd->iovdd),
				     "Failed to request iovdd regulator\n");

	error = goodix_berlin_input_dev_config(cd, id);
	if (error < 0) {
		dev_err(cd->dev, "failed set input device");
		return error;
	}

	error = devm_request_threaded_irq(dev, irq, NULL,
					  goodix_berlin_threadirq_func,
					  IRQF_ONESHOT, "goodix-berlin", cd);
	if (error) {
		dev_err(dev, "request threaded irq failed: %d\n", error);
		return error;
	}

	dev_set_drvdata(dev, cd);

	error = goodix_berlin_power_on(cd, true);
	if (error) {
		dev_err(cd->dev, "failed power on");
		return error;
	}

	error = devm_add_action_or_reset(dev, goodix_berlin_power_off, cd);
	if (error)
		return error;

	error = goodix_berlin_read_version(cd, &cd->fw_version);
	if (error < 0) {
		dev_err(cd->dev, "failed to get version info");
		return error;
	}

	error = goodix_berlin_get_ic_info(cd, &cd->ic_info);
	if (error) {
		dev_err(cd->dev, "invalid ic info, abort");
		return error;
	}

#ifdef CONFIG_DEBUG_FS
	cd->debugfs_root = debugfs_create_dir(dev_name(dev), NULL);
	if (!IS_ERR_OR_NULL(cd->debugfs_root)) {
		debugfs_create_file("fw_version", 0444, cd->debugfs_root,
				    cd, &goodix_berlin_fw_version_fops);
		debugfs_create_file("ic_info", 0444, cd->debugfs_root,
				    cd, &goodix_berlin_ic_info_fops);

		error = devm_add_action_or_reset(dev, goodix_berlin_debug_remove, cd);
		if (error)
			return error;
	}
#endif

	dev_dbg(cd->dev, "Goodix Berlin %s Touchscreen Controller", cd->fw_version.patch_pid);

	return 0;
}
EXPORT_SYMBOL_GPL(goodix_berlin_probe);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Goodix Berlin Core Touchscreen driver");
MODULE_AUTHOR("Neil Armstrong <neil.armstrong@linaro.org>");
