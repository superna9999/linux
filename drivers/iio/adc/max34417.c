// SPDX-License-Identifier: GPL-2.0
/*
 * IIO driver for Analog Devices MAX34417 ADC, 4-Channels High Dynamic Range Power Accumulator
 *
 * Datasheet: https://www.analog.com/en/products/max34417.html
 *
 * TODO: Slow Mode, Park Feature, Bulk Update, Perr_Verr Correction
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/init.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/unaligned.h>
#include <linux/units.h>

#define MAX34417_UPDATE_REG		0x0
#define MAX34417_CONTROL_REG		0x1
#define MAX34417_ACC_COUNT_REG		0x2

#define MAX34417_PWR_ACC_1_REG		0x3
#define MAX34417_PWR_ACC_2_REG		0x4
#define MAX34417_PWR_ACC_3_REG		0x5
#define MAX34417_PWR_ACC_4_REG		0x6

#define MAX34417_V_CH1_REG		0x7
#define MAX34417_V_CH2_REG		0x8
#define MAX34417_V_CH3_REG		0x9
#define MAX34417_V_CH4_REG		0xa

#define MAX34417_DID_REG		0xf

#define MAX34417_BULK_POWER_READOUT_REG		0x10
#define MAX34417_BULK_VOLTAGE_READOUT_REG	0x11

#define MAX34417_BULK_UPDATE_ADDRESS	0x2c
#define MAX34417_BULK_UPDATE_REG	0x0

/* Bit masks for control register */
#define MAX34417_CONTROL_OVF		BIT(0)
#define MAX34417_CONTROL_SLOW		BIT(1)
#define MAX34417_CONTROL_PARK0		BIT(2)
#define MAX34417_CONTROL_PARK1		BIT(3)
#define MAX34417_CONTROL_PARK_EN	BIT(4)
#define MAX34417_CONTROL_SMM		BIT(5)
#define MAX34417_CONTROL_CAM		BIT(6)
#define MAX34417_CONTROL_MODE		BIT(7)

#define MAX34417_ACC_COUNT_READ_LENGTH	4
#define MAX34417_ACC_COUNT_READ_OFFSET	1

#define MAX34417_POWER_READ_LENGTH	8
#define MAX34417_POWER_READ_OFFSET	1

#define MAX34417_VOLTAGE_READ_LENGTH	3
#define MAX34417_VOLTAGE_READ_OFFSET	1

#define MAX34417_MIN_RSENSE		(1 * MILLI) /* 1000 microOhm */
#define MAX34417_MAX_RSENSE		(100 * MILLI) /* 100000 microOhm */
#define MAX34417_DEFAULT_RSENSE		(1 * MILLI) /* 1000 microOhm */

#define MAX34417_PWR_CORRECTION_SCALE		24
#define MAX34417_PWR_AVG_FULL_SCALE_BITS	30

#define MAX34417_VOLTAGE_CORRECTION_SCALE	24
#define MAX34417_VOLTAGE_FULL_SCALE_BITS	14

/* TODO implement IIO_CHAN_INFO_SAMP_FREQ for slow mode */
#define MAX34417_SLOW_SPS_BIT			3 /* 8 SPS */
#define MAX34417_FAST_SPS_BIT			10 /* 1024 SPS */
#define MAX34417_FAST_SPS_CAM			1536 /* 1.5 * 1024 SPS */

#define MAX34417_CHANNEL_COUNT			4

/**
 * struct max34417_data - MAX34417 specific data.
 * @indio_dev:  IIO Device.
 * @regmap:	Device register map.
 * @lock:	Lock for protecting access to device hardware registers, mostly
 *		for reading common accumulator count and control register.
 * @input_correction:	Correction based on the Rsense value from channel nodes.
 * @input_label:	Channel label from channel nodes.
 * @acc_power:		Saved power accumulator for each channel when in CAM mode.
 * @acc_count:		Saved counter for each channel when in CAM mode.
 * @overflow:		Indicates if a channel saved data has overflowed.
 * @last_update:	Timestamp of the last UPDATE call in CAM mode.
 * @cam_mode:		State of the Continuous Accumulate Mode feature.
 */
struct max34417_data {
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	struct mutex lock;
	u32 input_correction[MAX34417_CHANNEL_COUNT];
	const char *input_label[MAX34417_CHANNEL_COUNT];
	s64 acc_power[MAX34417_CHANNEL_COUNT];
	s64 acc_count[MAX34417_CHANNEL_COUNT];
	bool overflow[MAX34417_CHANNEL_COUNT];
	ktime_t last_update;
	bool cam_mode;
};

static bool max34417_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX34417_UPDATE_REG:
	case MAX34417_CONTROL_REG:
		return true;
	default:
		return false;
	}
}

static bool max34417_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX34417_UPDATE_REG:
		return false;
	default:
		return true;
	}
}

static bool max34417_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX34417_DID_REG:
		return false;
	/* MAX34417_CONTROL_REG field OVF is volatile */
	case MAX34417_CONTROL_REG:
	default:
		return true;
	}
}

static bool max34417_readable_noinc_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX34417_UPDATE_REG:
	case MAX34417_CONTROL_REG:
	case MAX34417_DID_REG:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config max34417_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= MAX34417_BULK_VOLTAGE_READOUT_REG,
	.writeable_reg	= max34417_writeable_reg,
	.readable_reg	= max34417_readable_reg,
	.volatile_reg	= max34417_volatile_reg,
	/*
	 * While the DATA registers are not FIFOs, each register
	 * type has a different byte count and requires a
	 * Block Read transaction to read the value
	 */
	.readable_noinc_reg = max34417_readable_noinc_reg,
	/* No cache since the only non-volatile register is read-only */
};

#define MAX34417_CHANNEL(_index, _v_address, _power_address)	\
	{							\
		.type = IIO_VOLTAGE,				\
		.info_mask_separate =				\
			BIT(IIO_CHAN_INFO_RAW) |		\
			BIT(IIO_CHAN_INFO_SCALE),		\
		.channel = (_index),				\
		.address = (_v_address),			\
		.indexed = 1,					\
	},							\
	{							\
		.type = IIO_POWER,				\
		.info_mask_separate =				\
			BIT(IIO_CHAN_INFO_RAW) |		\
			BIT(IIO_CHAN_INFO_AVERAGE_RAW) |	\
			BIT(IIO_CHAN_INFO_SCALE),		\
		.channel = (_index),				\
		.address = (_power_address),			\
		.indexed = 1,					\
	},							\
	{							\
		.type = IIO_ENERGY,				\
		.info_mask_separate =				\
			BIT(IIO_CHAN_INFO_RAW) |		\
			BIT(IIO_CHAN_INFO_SCALE),		\
		.info_mask_shared_by_type =			\
			BIT(IIO_CHAN_INFO_ENABLE),		\
		.channel = (_index),				\
		.address = (_power_address),			\
		.indexed = 1,					\
	}

static const struct iio_chan_spec max34417_channels[] = {
	MAX34417_CHANNEL(0, MAX34417_V_CH1_REG, MAX34417_PWR_ACC_1_REG),
	MAX34417_CHANNEL(1, MAX34417_V_CH2_REG, MAX34417_PWR_ACC_2_REG),
	MAX34417_CHANNEL(2, MAX34417_V_CH3_REG, MAX34417_PWR_ACC_3_REG),
	MAX34417_CHANNEL(3, MAX34417_V_CH4_REG, MAX34417_PWR_ACC_4_REG),
};

/* TODO Implement trigger to update accumulator once and get all channels at once */

static int max34417_read_acc_count(struct max34417_data *max34417, u32 *acc_count)
{
	u8 buf[MAX34417_ACC_COUNT_READ_LENGTH];
	struct regmap *map = max34417->regmap;
	int rc;

	rc = regmap_noinc_read(map, MAX34417_ACC_COUNT_REG, &buf, sizeof(buf));
	if (rc)
		return rc;

	/* Ignore first byte which contains the Byte Count */
	*acc_count = get_unaligned_be24(&buf[MAX34417_ACC_COUNT_READ_OFFSET]);

	return 0;
}

static int max34417_read_acc_power(struct max34417_data *max34417, unsigned int address,
				   s64 *acc_power)
{
	struct regmap *map = max34417->regmap;
	u8 buf[MAX34417_POWER_READ_LENGTH];
	int rc;

	rc = regmap_noinc_read(map, address, &buf, sizeof(buf));
	if (rc)
		return rc;

	/* Ignore first byte which contains the Byte Count */
	*acc_power = get_unaligned_be56(&buf[MAX34417_POWER_READ_OFFSET]);

	return 0;
}

static int max34417_read_voltage_raw(struct max34417_data *max34417, unsigned int address,
				     u32 *voltage_raw)
{
	struct regmap *map = max34417->regmap;
	u8 buf[MAX34417_VOLTAGE_READ_LENGTH];
	int rc;

	rc = regmap_noinc_read(map, address, &buf, sizeof(buf));
	if (rc)
		return rc;

	/* Ignore first byte which contains the Byte Count & data starts at Bit 2 */
	*voltage_raw = get_unaligned_be16(&buf[MAX34417_VOLTAGE_READ_OFFSET]) >> 2;

	return 0;
}

/*
 * Check the OVF status bit indicating if one of the channels
 * has reached overflow, meaning one of the accumulators has
 * saturated and the MAX34417 stopped measuring.
 * The registers contains valid measurements, not wrapped,
 * and were already added to the local context accumulators.
 * The OVF bit is required to be cleared followed by an UPDATE
 * command to resume the accumulation mode sequence.
 */
static int max34417_check_ovf(struct max34417_data *max34417)
{
	struct regmap *map = max34417->regmap;
	u32 control;
	int rc;

	rc = regmap_read(map, MAX34417_CONTROL_REG, &control);
	if (rc)
		return rc;

	if (!(control & MAX34417_CONTROL_OVF))
		return 0;

	rc = regmap_clear_bits(map, MAX34417_CONTROL_REG,
			       MAX34417_CONTROL_OVF);
	if (rc)
		return rc;

	rc = regmap_write(map, MAX34417_UPDATE_REG, 1);
	if (rc)
		return rc;

	/* Save the timestamp for the next read */
	max34417->last_update = ktime_get();

	return 0;
}

/*
 * Save all power accumulators and counter after each UPDATE command
 * since it will reset the internal registers for ALL channels,
 * so in order to not loose any captured data we retrieve all registers
 * and accumulate them in the context variables.
 * When user gets either power or energy, those variables content
 * will be returned and zero-ed to return the power accumulated
 * since last read for each separate channel.
 */
static int max34417_save_accumulators(struct max34417_data *max34417)
{
	s64 acc_power;
	s32 acc_count;
	int rc;

	rc = max34417_read_acc_count(max34417, &acc_count);
	if (rc)
		return rc;

	for (int i = 0; i < MAX34417_CHANNEL_COUNT; i++) {
		rc = max34417_read_acc_power(max34417, MAX34417_PWR_ACC_1_REG + i,
					     &acc_power);
		if (rc)
			return rc;

		/* Checked for overflow on local accumulators */
		if (check_add_overflow(max34417->acc_power[i], acc_power,
				       &max34417->acc_power[i]) ||
		    check_add_overflow(max34417->acc_count[i], acc_count,
				       &max34417->acc_count[i])) {
			max34417->overflow[i] = true;
		}
	}

	/* Check for overflow */
	return max34417_check_ovf(max34417);
}

static int max34417_read_voltage(struct max34417_data *max34417,
				 const struct iio_chan_spec *chan, int *val)
{
	s64 delta_us = ktime_us_delta(ktime_get(), max34417->last_update);
	struct regmap *map = max34417->regmap;
	u32 voltage_raw;
	int rc;

	/* Sleep up to 1msec to make sure we have a measurement cycle done */
	if (delta_us >= 0 && delta_us < (1 * USEC_PER_MSEC))
		fsleep((1 * USEC_PER_MSEC) - delta_us);

	/*
	 * When not in CAM mode, we use the default mode where each
	 * UPDATE command will move the current accumulated data to the
	 * SMbus registers, reset those accumulators and run a new
	 * measurement cycle.
	 */
	if (!max34417->cam_mode) {
		/* Reset the accumulators and perform a measurement cycle */
		rc = regmap_write(map, MAX34417_UPDATE_REG, 1);
		if (rc)
			return rc;
		/*
		 * UPDATE commands should be no less than 1msec apart for reliable
		 * measurements, since in Manual mode the SPS is 1kHz, we need wait
		 * for the measurement to finish before moving the data in the SMbus
		 * registers.
		 */
		fsleep(1 * USEC_PER_MSEC);
	}

	/*
	 * Move the new UPDATE data into the SMBus read registers.
	 * This will also reset the accumulators, returning the
	 * power accumulated since the last UPDATE.
	 */
	rc = regmap_write(map, MAX34417_UPDATE_REG, 1);
	if (rc)
		return rc;

	/* Save the timestamp for the next read */
	max34417->last_update = ktime_get();

	if (max34417->cam_mode) {

		/* Retrieve all channels to avoid missing data for other channels */
		rc = max34417_save_accumulators(max34417);
		if (rc)
			return rc;
	}

	rc = max34417_read_voltage_raw(max34417, chan->address, &voltage_raw);
	if (rc)
		return rc;

	*val = voltage_raw;

	return IIO_VAL_INT;
}

static int max34417_read_power(struct max34417_data *max34417,
			       const struct iio_chan_spec *chan,
			       int *val, int *val2)
{
	s64 delta_us = ktime_us_delta(ktime_get(), max34417->last_update);
	struct regmap *map = max34417->regmap;
	s64 power;
	int rc;

	/* Sleep up to 1msec to make sure we have a measurement cycle done */
	if (delta_us >= 0 && delta_us < (1 * USEC_PER_MSEC))
		fsleep((1 * USEC_PER_MSEC) - delta_us);

	/*
	 * When not in CAM mode, we use the default mode where each
	 * UPDATE command will move the current accumulated data to the
	 * SMbus registers, reset those accumulators and run a new
	 * measurement cycle.
	 */
	if (!max34417->cam_mode) {
		/* Reset the accumulators and perform a measurement cycle */
		rc = regmap_write(map, MAX34417_UPDATE_REG, 1);
		if (rc)
			return rc;
		/*
		 * UPDATE commands should be no less than 1msec apart for reliable
		 * measurements, since in Manual mode the SPS is 1kHz, we need wait
		 * for the measurement to finish before moving the data in the SMbus
		 * registers.
		 */
		fsleep(1 * USEC_PER_MSEC);
	}

	/*
	 * Move the new UPDATE data into the SMBus read registers.
	 * This will also reset the accumulators, returning the
	 * power accumulated since the last UPDATE.
	 */
	rc = regmap_write(map, MAX34417_UPDATE_REG, 1);
	if (rc)
		return rc;

	/* Save the timestamp for the next read */
	max34417->last_update = ktime_get();

	/*
	 * Only get the accumulator counter in CAM mode, in normal mode
	 * we only accumulate once, so the counter is 1
	 */
	if (max34417->cam_mode) {
		s64 acc_count;
		s64 acc_power;

		/* Retrieve all channels to avoid missing data for other channels */
		rc = max34417_save_accumulators(max34417);
		if (rc)
			return rc;

		if (max34417->overflow[chan->channel]) {
			max34417->overflow[chan->channel] = false;
			max34417->acc_power[chan->channel] = 0;
			max34417->acc_count[chan->channel] = 0;
			return -EOVERFLOW;
		}

		/* Get current channel power, counter and reset them */
		acc_power = max34417->acc_power[chan->channel];
		acc_count = max34417->acc_count[chan->channel];
		max34417->acc_power[chan->channel] = 0;
		max34417->acc_count[chan->channel] = 0;

		if (acc_count == 0) {
			dev_warn_ratelimited(&max34417->indio_dev->dev,
					     "unexpected zero accumulator count\n");
			power = 0;
		} else {
			power = div64_s64(acc_power, acc_count);
		}
	} else {
		rc = max34417_read_acc_power(max34417, chan->address, &power);
		if (rc)
			return rc;
	}

	iio_val_s64_decompose(power, val, val2);

	return IIO_VAL_INT_64;
}

static int max34417_read_energy(struct max34417_data *max34417,
			       const struct iio_chan_spec *chan,
			       int *val, int *val2)
{
	struct regmap *map = max34417->regmap;
	s64 delta_us = ktime_us_delta(ktime_get(), max34417->last_update);
	s64 acc_power;
	int rc;

	/* Sleep up to 1msec to make sure we have a measurement cycle done */
	if (delta_us >= 0 && delta_us < (1 * USEC_PER_MSEC))
		fsleep((1 * USEC_PER_MSEC) - delta_us);

	/* Move the data to the SMBus registers */
	rc = regmap_write(map, MAX34417_UPDATE_REG, 1);
	if (rc)
		return rc;

	/* Save the timestamp for the next read */
	max34417->last_update = ktime_get();

	/* Retrieve all channels to avoid missing data for other channels */
	rc = max34417_save_accumulators(max34417);
	if (rc)
		return rc;

	if (max34417->overflow[chan->channel]) {
		max34417->overflow[chan->channel] = false;
		max34417->acc_power[chan->channel] = 0;
		max34417->acc_count[chan->channel] = 0;
		return -EOVERFLOW;
	}

	/* Get current channel power and reset them */
	acc_power = max34417->acc_power[chan->channel];
	max34417->acc_power[chan->channel] = 0;
	max34417->acc_count[chan->channel] = 0;

	iio_val_s64_decompose(acc_power, val, val2);

	return IIO_VAL_INT_64;
}

/*
 * The Continuous Accumulation Mode (CAM) will automatically
 * run measurement cycles 1536 times per second and update
 * the ACC_COUNT register, while in normal mode the measument
 * cycle is only done after an UPDATE command.
 * The Single Measurement Mode (SMM) is like CAM, but each
 * measurement cycle is triggered with an UPDATE command
 * but the accumulators registers are not cleared out on the
 * UPDATE command.
 */
static int max34417_setup_cam_mode(struct max34417_data *max34417, int val)
{
	struct regmap *map = max34417->regmap;
	int rc;

	if (max34417->cam_mode && !val) {
		rc = regmap_clear_bits(map, MAX34417_CONTROL_REG,
				       MAX34417_CONTROL_CAM);
		if (rc)
			return rc;

		rc = regmap_write(map, MAX34417_UPDATE_REG, 1);
		if (rc)
			return rc;
	} else if (!max34417->cam_mode && val) {
		/*
		 * If CAM mode must be set, the Control byte must be set to
		 * desired values with CAM = 0, then followed by an UPDATE
		 */
		rc = regmap_clear_bits(map, MAX34417_CONTROL_REG,
				       MAX34417_CONTROL_CAM);
		if (rc)
			return rc;

		rc = regmap_write(map, MAX34417_UPDATE_REG, 1);
		if (rc)
			return rc;

		rc = regmap_set_bits(map, MAX34417_CONTROL_REG, MAX34417_CONTROL_CAM);
		if (rc)
			return rc;

		rc = regmap_write(map, MAX34417_UPDATE_REG, 1);
		if (rc)
			return rc;

		/* Reset counters and accumulators */
		for (int i = 0; i < MAX34417_CHANNEL_COUNT; i++) {
			max34417->acc_power[i] = 0;
			max34417->acc_count[i] = 0;
			max34417->overflow[i] = false;
		}
	}

	/* Save the timestamp for the next read */
	max34417->last_update = ktime_get();

	max34417->cam_mode = val;

	return 0;
}

static int max34417_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct max34417_data *max34417 = iio_priv(indio_dev);
	u64 num, den, scale;

	guard(mutex)(&max34417->lock);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_VOLTAGE:
			return max34417_read_voltage(max34417, chan, val);
		case IIO_POWER:
			/* In default mode, we return a single measurement */
			if (max34417->cam_mode)
				return -EINVAL;

			return max34417_read_power(max34417, chan, val, val2);
		case IIO_ENERGY:
			if (!max34417->cam_mode)
				return -EINVAL;

			return max34417_read_energy(max34417, chan, val, val2);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_AVERAGE_RAW:
		if (chan->type != IIO_POWER)
			return -EINVAL;

		/* In CAM mode, we return an average measurement */
		if (!max34417->cam_mode)
			return -EINVAL;

		return max34417_read_power(max34417, chan, val, val2);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			/* Scale to mV */
			*val = MAX34417_VOLTAGE_CORRECTION_SCALE * MILLI;
			*val2 = MAX34417_VOLTAGE_FULL_SCALE_BITS;

			return IIO_VAL_FRACTIONAL_LOG2;
		case IIO_POWER:
			/*
			 * Scale to mW
			 * (1000 * (Power Accumulation / Counter) * Correction) / 2^30
			 */
			*val = max34417->input_correction[chan->channel] * MILLIWATT_PER_WATT;
			*val2 = MAX34417_PWR_AVG_FULL_SCALE_BITS;

			return IIO_VAL_FRACTIONAL_LOG2;
		case IIO_ENERGY:
			/*
			 * Scale to Joules
			 * P=((Power Accumulation / Counter) * Correction) / 2^30
			 * t=(Counter / Samples per Second)
			 * E=(P * t)
			 * Can be simplified to:
			 * (Power Accumulation * Correction) / (2^30 * SPS)
			 * And scale expressed in Femto (10^-15 precision):
			 * S=(Correction * 10^15) / (2^30 * SPS)
			 * With the maximum input_correction of 2400, it fits in 64bits
			 */
			num = (u64)max34417->input_correction[chan->channel] * FEMTO;
			den = (u64)MAX34417_FAST_SPS_CAM << MAX34417_PWR_AVG_FULL_SCALE_BITS;
			scale = div64_u64(num, den);
			iio_val_s64_decompose(scale, val, val2);

			return IIO_VAL_DECIMAL64_FEMTO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_ENABLE:
		*val = max34417->cam_mode;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int max34417_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct max34417_data *max34417 = iio_priv(indio_dev);

	guard(mutex)(&max34417->lock);

	switch (mask) {
	case IIO_CHAN_INFO_ENABLE:
		/* Setup the CAM mode for the 4 channels */
		return max34417_setup_cam_mode(max34417, val);
	default:
		return -EINVAL;
	}
}

static int max34417_read_label(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       char *label)
{
	struct max34417_data *max34417 = iio_priv(indio_dev);
	const char *input_label = max34417->input_label[chan->channel];

	if (input_label)
		return sysfs_emit(label, "%s\n", input_label);

	return sysfs_emit(label, "channel%d\n", chan->channel);
}

static const struct iio_info max34417_info = {
	.read_raw	= max34417_read_raw,
	.write_raw	= max34417_write_raw,
	.read_label	= max34417_read_label,
};

static u32 max34417_calc_input_correction(u32 rsense_uOhm)
{
	/* Derived from Table 14. Correction Factors for Various Current-Sense Resistor Values */
	return (MAX34417_MAX_RSENSE * MAX34417_PWR_CORRECTION_SCALE) / rsense_uOhm;
}

static int max34417_probe_channel_node(struct device *dev,
				       struct max34417_data *max34417,
				       struct fwnode_handle *node)
{
	u32 rsense_uOhm, index;
	int rc;

	rc = fwnode_property_read_u32(node, "reg", &index);
	if (rc)
		return dev_err_probe(dev, rc, "failure reading reg property of %pfwP\n",
				     node);

	if (index >= MAX34417_CHANNEL_COUNT)
		return dev_err_probe(dev, -ECHRNG, "invalid reg %d of %pfwP\n",
				     index, node);

	if (fwnode_property_present(node, "label")) {
		rc = fwnode_property_read_string(node, "label",
						 &max34417->input_label[index]);
		if (rc)
			return dev_err_probe(dev, rc,
					     "failure reading label property of %pfwP\n",
					     node);
	}

	if (fwnode_property_present(node, "shunt-resistor-micro-ohms")) {
		rc = fwnode_property_read_u32(node, "shunt-resistor-micro-ohms",
					      &rsense_uOhm);
		if (rc)
			return dev_err_probe(dev, rc,
					     "failure reading shunt property of %pfwP\n",
					     node);

		if (rsense_uOhm < MAX34417_MIN_RSENSE ||
		    rsense_uOhm > MAX34417_MAX_RSENSE)
			return dev_err_probe(dev, -EINVAL,
					     "invalid shunt value %d of %pfwP\n",
					     rsense_uOhm, node);

		max34417->input_correction[index] =
			max34417_calc_input_correction(rsense_uOhm);
	}

	return 0;
}

static int max34417_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct max34417_data *max34417;
	struct iio_dev *indio_dev;
	struct regmap *map;
	int rc;

	map = devm_regmap_init_i2c(client, &max34417_regmap_config);
	if (IS_ERR(map))
		return dev_err_probe(dev, PTR_ERR(map), "regmap_init failed\n");

	indio_dev = devm_iio_device_alloc(dev, sizeof(*max34417));
	if (!indio_dev)
		return -ENOMEM;

	rc = devm_regulator_get_enable(dev, "vdd");
	if (rc)
		return dev_err_probe(dev, rc, "failed to get vdd regulator\n");

	rc = devm_regulator_get_enable(dev, "vio");
	if (rc)
		return dev_err_probe(dev, rc, "failed to get vio regulator\n");

	max34417 = iio_priv(indio_dev);
	max34417->regmap = map;
	max34417->indio_dev = indio_dev;

	rc = devm_mutex_init(dev, &max34417->lock);
	if (rc)
		return rc;

	/* Set default input correction for all channels */
	for (int i = 0; i < MAX34417_CHANNEL_COUNT; i++)
		max34417->input_correction[i] =
			max34417_calc_input_correction(MAX34417_DEFAULT_RSENSE);

	device_for_each_child_node_scoped(dev, node) {
		rc = max34417_probe_channel_node(dev, max34417, node);
		if (rc)
			return rc;
	}

	indio_dev->channels = max34417_channels;
	indio_dev->num_channels = ARRAY_SIZE(max34417_channels);
	indio_dev->name = "max34417";
	indio_dev->info = &max34417_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	/*
	 * Reset mode and set accumulator width to Wide
	 *
	 * The MAX34407 variant could be supported by setting the
	 * MAX34417_CONTROL_MODE field to 0 and handling a smaller
	 * accumulator width.
	 */
	rc = regmap_write(map, MAX34417_CONTROL_REG, MAX34417_CONTROL_MODE);
	if (rc)
		return dev_err_probe(dev, rc, "Error writing control register\n");

	/* Reset the accumulators */
	rc = regmap_write(map, MAX34417_UPDATE_REG, 1);
	if (rc)
		return rc;

	/* Save the timestamp for the next read */
	max34417->last_update = ktime_get();

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id max34417_of_match[] = {
	{ .compatible = "adi,max34417" },
	{ }
};
MODULE_DEVICE_TABLE(of, max34417_of_match);

static const struct i2c_device_id max34417_id[] = {
	{ .name = "max34417" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max34417_id);

static struct i2c_driver max34417_driver = {
	.driver = {
		.name   = "max34417",
		.of_match_table = max34417_of_match,
	},
	.probe = max34417_probe,
	.id_table = max34417_id,
};
module_i2c_driver(max34417_driver);

MODULE_AUTHOR("Neil Armstrong <neil.armstrong@linaro.org>");
MODULE_DESCRIPTION("Analog Devices MAX34417 ADC driver");
MODULE_LICENSE("GPL");
