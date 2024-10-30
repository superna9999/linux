/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Code shared between the main and auxiliary Qualcomm PMIC voltage ADCs
 * of type ADC5 Gen3.
 */

#ifndef QCOM_ADC5_GEN3_COMMON_H
#define QCOM_ADC5_GEN3_COMMON_H

#include <linux/auxiliary_bus.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/iio/adc/qcom-vadc-common.h>

#define ADC5_GEN3_HS				0x45
#define ADC5_GEN3_HS_BUSY			BIT(7)
#define ADC5_GEN3_HS_READY			BIT(0)

#define ADC5_GEN3_STATUS1			0x46
#define ADC5_GEN3_STATUS1_CONV_FAULT		BIT(7)
#define ADC5_GEN3_STATUS1_THR_CROSS		BIT(6)
#define ADC5_GEN3_STATUS1_EOC			BIT(0)

#define ADC5_GEN3_TM_EN_STS			0x47
#define ADC5_GEN3_TM_HIGH_STS			0x48
#define ADC5_GEN3_TM_LOW_STS			0x49

#define ADC5_GEN3_EOC_STS			0x4a
#define ADC5_GEN3_EOC_CHAN_0			BIT(0)

#define ADC5_GEN3_EOC_CLR			0x4b
#define ADC5_GEN3_TM_HIGH_STS_CLR		0x4c
#define ADC5_GEN3_TM_LOW_STS_CLR		0x4d
#define ADC5_GEN3_CONV_ERR_CLR			0x4e
#define ADC5_GEN3_CONV_ERR_CLR_REQ		BIT(0)

#define ADC5_GEN3_SID				0x4f
#define ADC5_GEN3_SID_MASK			GENMASK(3, 0)

#define ADC5_GEN3_PERPH_CH			0x50
#define ADC5_GEN3_CHAN_CONV_REQ			BIT(7)

#define ADC5_GEN3_TIMER_SEL			0x51
#define ADC5_GEN3_TIME_IMMEDIATE		0x1

#define ADC5_GEN3_DIG_PARAM			0x52
#define ADC5_GEN3_DIG_PARAM_CAL_SEL_MASK	GENMASK(5, 4)
#define ADC5_GEN3_DIG_PARAM_DEC_RATIO_SEL_MASK	GENMASK(3, 2)

#define ADC5_GEN3_FAST_AVG			0x53
#define ADC5_GEN3_FAST_AVG_CTL_EN		BIT(7)
#define ADC5_GEN3_FAST_AVG_CTL_SAMPLES_MASK	GENMASK(2, 0)

#define ADC5_GEN3_ADC_CH_SEL_CTL		0x54
#define ADC5_GEN3_DELAY_CTL			0x55
#define ADC5_GEN3_HW_SETTLE_DELAY_MASK		GENMASK(3, 0)

#define ADC5_GEN3_CH_EN				0x56
#define ADC5_GEN3_HIGH_THR_INT_EN		BIT(1)
#define ADC5_GEN3_LOW_THR_INT_EN		BIT(0)

#define ADC5_GEN3_LOW_THR0			0x57
#define ADC5_GEN3_LOW_THR1			0x58
#define ADC5_GEN3_HIGH_THR0			0x59
#define ADC5_GEN3_HIGH_THR1			0x5a

#define ADC5_GEN3_CH_DATA0(channel)	(0x5c + (channel) * 2)
#define ADC5_GEN3_CH_DATA1(channel)	(0x5d + (channel) * 2)

#define ADC5_GEN3_CONV_REQ			0xe5
#define ADC5_GEN3_CONV_REQ_REQ			BIT(0)

#define ADC5_GEN3_VIRTUAL_SID_MASK			GENMASK(15, 8)
#define ADC5_GEN3_CHANNEL_MASK			GENMASK(7, 0)
#define V_CHAN(x)		\
	(FIELD_PREP(ADC5_GEN3_VIRTUAL_SID_MASK, (x).sid) | (x).channel)	\

enum adc5_cal_method {
	ADC5_NO_CAL = 0,
	ADC5_RATIOMETRIC_CAL,
	ADC5_ABSOLUTE_CAL
};

enum adc5_time_select {
	MEAS_INT_DISABLE = 0,
	MEAS_INT_IMMEDIATE,
	MEAS_INT_50MS,
	MEAS_INT_100MS,
	MEAS_INT_1S,
	MEAS_INT_NONE,
};

struct adc5_sdam_data {
	u16			base_addr;
	const char		*irq_name;
	int			irq;
};

struct adc5_device_data {
	struct regmap			*regmap;
	struct adc5_sdam_data		*base;
	int				num_sdams;
};

/**
 * struct adc5_channel_prop - ADC channel property.
 * @channel: channel number, refer to the channel list.
 * @cal_method: calibration method.
 * @decimation: sampling rate supported for the channel.
 * @sid: slave id of PMIC owning the channel.
 * @label: Channel name used in device tree.
 * @prescale: channel scaling performed on the input signal.
 * @hw_settle_time: the time between AMUX being configured and the
 *	start of conversion.
 * @avg_samples: ability to provide single result from the ADC
 *	that is an average of multiple measurements.
 * @scale_fn_type: Represents the scaling function to convert voltage
 *	physical units desired by the client for the channel.
 */
struct adc5_channel_common_prop {
	unsigned int			channel;
	enum adc5_cal_method		cal_method;
	unsigned int			decimation;
	unsigned int			sid;
	const char			*label;
	unsigned int			prescale;
	unsigned int			hw_settle_time;
	unsigned int			avg_samples;
	enum vadc_scale_fn_type		scale_fn_type;
};

struct tm5_aux_dev_wrapper {
	struct auxiliary_device aux_dev;
	struct adc5_device_data *dev_data;
	struct adc5_channel_common_prop *tm_props;
	unsigned int			n_tm_channels;
};

struct adc_tm5_auxiliary_drv {
	struct auxiliary_driver adrv;
	void (*tm_event_notify)(struct auxiliary_device *adev);
};

static int adc5_gen3_read(struct adc5_device_data *adc, unsigned int sdam_index,
			  u16 offset, u8 *data, int len)
{
	return regmap_bulk_read(adc->regmap, adc->base[sdam_index].base_addr + offset, data, len);
}

static int adc5_gen3_write(struct adc5_device_data *adc, unsigned int sdam_index,
			   u16 offset, u8 *data, int len)
{
	return regmap_bulk_write(adc->regmap, adc->base[sdam_index].base_addr + offset, data, len);
}

/*
 * Worst case delay from PBS in readying handshake bit
 * can be up to 15ms, when PBS is busy running other
 * simultaneous transactions, while in the best case, it is
 * already ready at this point. Assigning polling delay and
 * retry count accordingly.
 */

#define ADC5_GEN3_HS_DELAY_MIN_US		100
#define ADC5_GEN3_HS_DELAY_MAX_US		110
#define ADC5_GEN3_HS_RETRY_COUNT		150

static int adc5_gen3_poll_wait_hs(struct adc5_device_data *adc,
				  unsigned int sdam_index)
{
	u8 conv_req = ADC5_GEN3_CONV_REQ_REQ;
	int ret, count;
	u8 status = 0;

	for (count = 0; count < ADC5_GEN3_HS_RETRY_COUNT; count++) {
		ret = adc5_gen3_read(adc, sdam_index, ADC5_GEN3_HS, &status, 1);
		if (ret)
			return ret;

		if (status == ADC5_GEN3_HS_READY) {
			ret = adc5_gen3_read(adc, sdam_index, ADC5_GEN3_CONV_REQ,
					     &conv_req, 1);
			if (ret)
				return ret;

			if (!conv_req)
				return 0;
		}

		usleep_range(ADC5_GEN3_HS_DELAY_MIN_US, ADC5_GEN3_HS_DELAY_MAX_US);
	}

	pr_err("Setting HS ready bit timed out, sdam_index:%d, status:%#x\n", sdam_index, status);
	return -ETIMEDOUT;
}

static void adc5_gen3_update_dig_param(struct adc5_channel_common_prop *prop, u8 *data)
{
	/* Update calibration select and decimation ratio select */
	*data &= ~(ADC5_GEN3_DIG_PARAM_CAL_SEL_MASK | ADC5_GEN3_DIG_PARAM_DEC_RATIO_SEL_MASK);
	*data |= FIELD_PREP(ADC5_GEN3_DIG_PARAM_CAL_SEL_MASK, prop->cal_method);
	*data |= FIELD_PREP(ADC5_GEN3_DIG_PARAM_DEC_RATIO_SEL_MASK, prop->decimation);
}

static int adc5_gen3_status_clear(struct adc5_device_data *adc,
				  int sdam_index, u16 offset, u8 *val, int len)
{
	u8 value;
	int ret;

	ret = adc5_gen3_write(adc, sdam_index, offset, val, len);
	if (ret)
		return ret;

	/* To indicate conversion request is only to clear a status */
	value = 0;
	ret = adc5_gen3_write(adc, sdam_index, ADC5_GEN3_PERPH_CH, &value, 1);
	if (ret)
		return ret;

	value = ADC5_GEN3_CONV_REQ_REQ;
	return adc5_gen3_write(adc, sdam_index, ADC5_GEN3_CONV_REQ, &value, 1);
}

void adc5_take_mutex_lock(struct device *dev, bool lock);
int adc5_gen3_get_scaled_reading(struct device *dev, struct adc5_channel_common_prop *common_props,
				 int *val);
int adc5_gen3_therm_code_to_temp(struct device *dev, struct adc5_channel_common_prop *common_props,
				 u16 code, int *val);

#endif /* QCOM_VADC5_GEN3_COMMON_H */
