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
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
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
 * @cam_mode:		State of the Continuous Accumulate Mode feature
 * @acc_power:		Saved power accumulator for each channel when in CAM mode
 * @acc_count:		Saved counter for each channel when in CAM mode
 */
struct max34417_data {
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	struct mutex lock;
	u32 input_correction[MAX34417_CHANNEL_COUNT];
	const char *input_label[MAX34417_CHANNEL_COUNT];
	bool cam_mode;
	u64 acc_power[MAX34417_CHANNEL_COUNT];
	u64 acc_count[MAX34417_CHANNEL_COUNT];
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
	default:
	/* MAX34417_CONTROL_REG field OVF is volatile */
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

static int max34417_reset_smm(struct max34417_data *max34417)
{
	int rc;

	/* Set SMM=0 to re-init the SMM mode */
	rc = regmap_clear_bits(max34417->regmap, MAX34417_CONTROL_REG,
			       MAX34417_CONTROL_SMM);
	if (rc)
		return rc;

	/* Reset accumulators after setting SMM=0 */
	return regmap_write(max34417->regmap, MAX34417_UPDATE_REG, 1);
}

static int max34417_trigger_smm(struct max34417_data *max34417)
{
	u32 control;
	int rc;

	rc = regmap_read(max34417->regmap, MAX34417_CONTROL_REG, &control);
	if (rc)
		return rc;

	if (control & MAX34417_CONTROL_SMM) {
		/* Set SMM=0 to re-init the SMM mode */
		rc = regmap_clear_bits(max34417->regmap, MAX34417_CONTROL_REG,
				       MAX34417_CONTROL_SMM);
		if (rc)
			return rc;

		/* Reset accumulators after setting SMM=0 */
		rc = regmap_write(max34417->regmap, MAX34417_UPDATE_REG, 1);
		if (rc)
			return rc;
	}

	/* Enable SMM to do a single accumulation */
	rc = regmap_set_bits(max34417->regmap, MAX34417_CONTROL_REG,
			     MAX34417_CONTROL_SMM);
	if (rc)
		return rc;

	/*
	 * After the SMM bit is changed, the UPDATE command should be sent
	 * to reset the accumulators and perform the selected scan operation.
	 */
	rc = regmap_write(max34417->regmap, MAX34417_UPDATE_REG, 1);
	if (rc)
		return rc;
	/*
	 * UPDATE commands should be no less than 1msec apart for reliable
	 * measurements.
	 */
	fsleep(1 * USEC_PER_MSEC);

	return 0;
}

static int max34417_read_acc_count(struct max34417_data *max34417, u32 *acc_count)
{
	u8 buf[MAX34417_ACC_COUNT_READ_LENGTH];
	int rc;

	rc = regmap_noinc_read(max34417->regmap, MAX34417_ACC_COUNT_REG,
			       &buf, sizeof(buf));
	if (rc)
		return rc;

	/* Ignore byte0 */
	*acc_count = get_unaligned_be24(&buf[MAX34417_ACC_COUNT_READ_OFFSET]);

	return 0;
}

static int max34417_read_acc_power(struct max34417_data *max34417, unsigned int address,
				   u64 *acc_power)
{
	u8 buf[MAX34417_POWER_READ_LENGTH];
	int rc;

	rc = regmap_noinc_read(max34417->regmap, address, &buf, sizeof(buf));
	if (rc)
		return rc;

	/* Ignore byte0 */
	*acc_power = get_unaligned_be56(&buf[MAX34417_POWER_READ_OFFSET]);

	return 0;
}

static int max34417_check_ovf(struct max34417_data *max34417)
{
	u32 control;
	int rc;

	rc = regmap_read(max34417->regmap, MAX34417_CONTROL_REG, &control);
	if (rc)
		return rc;

	if (control & MAX34417_CONTROL_OVF) {
		rc = regmap_clear_bits(max34417->regmap, MAX34417_CONTROL_REG,
				       MAX34417_CONTROL_OVF);
		if (rc)
			return rc;

		rc = regmap_write(max34417->regmap, MAX34417_UPDATE_REG, 1);
		if (rc)
			return rc;
	}

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
	u64 acc_power;
	u32 acc_count;
	int rc;

	rc = max34417_read_acc_count(max34417, &acc_count);
	if (rc)
		return rc;

	for (int i = 0; i < MAX34417_CHANNEL_COUNT; i++) {
		rc = max34417_read_acc_power(max34417, MAX34417_PWR_ACC_1_REG + i,
					     &acc_power);
		if (rc)
			return rc;

		/* TODO sanitize accumulators in case of overflow */
		max34417->acc_power[i] += acc_power;
		max34417->acc_count[i] += acc_count;
	}

	/* Check for overflow */
	return max34417_check_ovf(max34417);
}

static int max34417_read_voltage(struct max34417_data *max34417,
				 const struct iio_chan_spec *chan, int *val)
{
	u8 buf[MAX34417_VOLTAGE_READ_LENGTH];
	int rc;

	/* In SMM mode, trigger a measurement cycle manually */
	if (!max34417->cam_mode) {
		rc = max34417_trigger_smm(max34417);
		if (rc)
			return rc;
	}

	/* Move the new UPDATE data into the SMBus read registers */
	rc = regmap_write(max34417->regmap, MAX34417_UPDATE_REG, 1);
	if (rc)
		return rc;

	if (max34417->cam_mode) {
		/* Retrieve all channels to avoid missing data for other channels */
		rc = max34417_save_accumulators(max34417);
		if (rc)
			return rc;
	}

	rc = regmap_noinc_read(max34417->regmap, chan->address, &buf, sizeof(buf));
	if (rc)
		return rc;

	if (!max34417->cam_mode) {
		rc = max34417_reset_smm(max34417);
		if (rc)
			return rc;
	}

	*val = get_unaligned_be16(&buf[MAX34417_VOLTAGE_READ_OFFSET]) >> 2;

	return IIO_VAL_INT;
}

static int max34417_read_power(struct max34417_data *max34417,
			       const struct iio_chan_spec *chan,
			       int *val, int *val2)
{
	u64 power;
	int rc;

	/* In SMM mode, trigger a measurement cycle manually */
	if (!max34417->cam_mode) {
		rc = max34417_trigger_smm(max34417);
		if (rc)
			return rc;
	}

	/*
	 * Move the new UPDATE data into the SMBus read registers.
	 * In CAM mode this will also reset the accumulators, returning
	 * the power accumulated since the last UPDATE.
	 */
	rc = regmap_write(max34417->regmap, MAX34417_UPDATE_REG, 1);
	if (rc)
		return rc;

	/*
	 * Only get the accumulator counter in CAM mode, in SMM mode
	 * we only accumulate only once, so the counter is 1
	 */
	if (max34417->cam_mode) {
		u64 acc_count;
		u64 acc_power;

		/* Retrieve all channels to avoid missing data for other channels */
		rc = max34417_save_accumulators(max34417);
		if (rc)
			return rc;

		/* Get current channel power, counter and reset them */
		acc_power = max34417->acc_power[chan->channel];
		acc_count = max34417->acc_count[chan->channel];
		max34417->acc_power[chan->channel] = 0;
		max34417->acc_count[chan->channel] = 0;

		if (acc_count == 0) {
			dev_warn_ratelimited(&max34417->indio_dev->dev,
					     "unexpected zero accumulator count\n");

			*val = 0;
			*val2 = 0;

			return IIO_VAL_INT_64;
		}

		power = div64_u64(acc_power, acc_count);
	} else {
		rc = max34417_read_acc_power(max34417, chan->address, &power);
		if (rc)
			return rc;

		rc = max34417_reset_smm(max34417);
		if (rc)
			return rc;
	}

	*val = lower_32_bits(power);
	*val2 = upper_32_bits(power);

	return IIO_VAL_INT_64;
}

static int max34417_read_energy(struct max34417_data *max34417,
			       const struct iio_chan_spec *chan,
			       int *val, int *val2)
{
	u64 acc_power;
	int rc;

	/* Move the data to the SMBus registers */
	rc = regmap_write(max34417->regmap, MAX34417_UPDATE_REG, 1);
	if (rc)
		return rc;

	/* Retrieve all channels to avoid missing data for other channels */
	rc = max34417_save_accumulators(max34417);
	if (rc)
		return rc;

	/* Get current channel power and reset them */
	acc_power = max34417->acc_power[chan->channel];
	max34417->acc_power[chan->channel] = 0;
	max34417->acc_count[chan->channel] = 0;

	*val = lower_32_bits(acc_power);
	*val2 = upper_32_bits(acc_power);

	return IIO_VAL_INT_64;
}

static int max34417_setup_cam_mode(struct max34417_data *max34417, int val)
{
	int rc;

	if (max34417->cam_mode && !val) {
		rc = regmap_clear_bits(max34417->regmap, MAX34417_CONTROL_REG,
				       MAX34417_CONTROL_CAM | MAX34417_CONTROL_SMM);
		if (rc)
			return rc;

		/*
		 * If CAM mode must be set, the Control byte must be set to
		 * desired values with CAM = 0, then followed by an UPDATE
		 */
		rc = regmap_write(max34417->regmap, MAX34417_UPDATE_REG, 1);
		if (rc)
			return rc;
	} else if (!max34417->cam_mode && val) {
		/*
		 * After this, the CAM mode bit can be set and another
		 * UPDATE must be sent.
		 */
		rc = regmap_clear_bits(max34417->regmap, MAX34417_CONTROL_REG,
				       MAX34417_CONTROL_SMM);
		if (rc)
			return rc;
		rc = regmap_set_bits(max34417->regmap, MAX34417_CONTROL_REG,
				     MAX34417_CONTROL_CAM);
		if (rc)
			return rc;

		rc = regmap_write(max34417->regmap, MAX34417_UPDATE_REG, 1);
		if (rc)
			return rc;

		/* Reset counters and accumulators */
		for (int i = 0; i < MAX34417_CHANNEL_COUNT; i++) {
			max34417->acc_power[i] = 0;
			max34417->acc_count[i] = 0;
		}
	}

	max34417->cam_mode = val;

	return 0;
}

static int max34417_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct max34417_data *max34417 = iio_priv(indio_dev);

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
		if (chan->type == IIO_VOLTAGE) {
			/* Scale to mV */
			*val = MAX34417_VOLTAGE_CORRECTION_SCALE * MILLI;
			*val2 = MAX34417_VOLTAGE_FULL_SCALE_BITS;

			return IIO_VAL_FRACTIONAL_LOG2;
		} else if (chan->type == IIO_POWER) {
			/*
			 * Scale to mW
			 * (1000 * (Power Accumulation / Counter) * Correction) / 2^30
			 */
			*val = max34417->input_correction[chan->channel] * MILLIWATT_PER_WATT;
			*val2 = MAX34417_PWR_AVG_FULL_SCALE_BITS;

			return IIO_VAL_FRACTIONAL_LOG2;
		} else if (chan->type == IIO_ENERGY) {
			/*
			 * Scale to mJ
			 * P=(1000 * (Power Accumulation / Counter) * Correction) / 2^30
			 * t=(Counter / Samples per Second)
			 * E=(P * t)
			 * Can be simplified to:
			 * (1000 * Power Accumulation * Correction) / (2^30 * SPS)
			 * The denominator doesn't fit in 32bit so scale down both terms.
			 */
			*val = max34417->input_correction[chan->channel];
			*val2 = div_u64(BIT_ULL(MAX34417_PWR_AVG_FULL_SCALE_BITS) *
					MAX34417_FAST_SPS_CAM, MILLI);

			return IIO_VAL_FRACTIONAL;
		}

		return -EINVAL;
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

	if (input_label) {
		switch (chan->type) {
		case IIO_VOLTAGE:
			return sysfs_emit(label, "%s-voltage\n", input_label);
		case IIO_POWER:
			return sysfs_emit(label, "%s-power\n", input_label);
		case IIO_ENERGY:
			return sysfs_emit(label, "%s-energy\n", input_label);
		default:
			return 0;
		}
	}

	switch (chan->type) {
	case IIO_VOLTAGE:
		return sysfs_emit(label, "channel%d-voltage\n", chan->channel);
	case IIO_POWER:
		return sysfs_emit(label, "channel%d-power\n", chan->channel);
	case IIO_ENERGY:
		return sysfs_emit(label, "channel%d-energy\n", chan->channel);
	default:
		return 0;
	}
}

static const struct iio_info max34417_info = {
	.read_raw	= max34417_read_raw,
	.write_raw	= max34417_write_raw,
	.read_label	= max34417_read_label,
};

static unsigned int max34417_calc_input_correction(u32 rsense_microOhm)
{
	/* (100 milliOhm / rsense) * MAX34417_PWR_CORRECTION_SCALE */
	return (MAX34417_MAX_RSENSE * MAX34417_PWR_CORRECTION_SCALE) / rsense_microOhm;
}

static int max34417_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct max34417_data *max34417;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int rc;

	regmap = devm_regmap_init_i2c(client, &max34417_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "regmap_init failed\n");

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
	max34417->regmap = regmap;
	max34417->indio_dev = indio_dev;

	rc = devm_mutex_init(dev, &max34417->lock);
	if (rc)
		return rc;

	/* Set default input correction for all channels */
	for (int i = 0; i < MAX34417_CHANNEL_COUNT; i++)
		max34417->input_correction[i] =
			max34417_calc_input_correction(MAX34417_DEFAULT_RSENSE);

	device_for_each_child_node_scoped(dev, node) {
		u32 rsense_microOhm, index;

		rc = fwnode_property_read_u32(node, "reg", &index);
		if (rc)
			return dev_err_probe(dev, rc, "failure reading reg property of %pfwP\n",
					     node);

		if (index >= MAX34417_CHANNEL_COUNT)
			return dev_err_probe(dev, -EINVAL, "invalid reg %d of %pfwP\n",
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
						      &rsense_microOhm);
			if (rc)
				return dev_err_probe(dev, rc,
						"failure reading shunt property of %pfwP\n",
						node);

			if (rsense_microOhm < MAX34417_MIN_RSENSE ||
			    rsense_microOhm > MAX34417_MAX_RSENSE)
				return dev_err_probe(dev, -EINVAL,
						     "invalid shunt value %d of %pfwP\n",
						     rsense_microOhm, node);

			max34417->input_correction[index] =
				max34417_calc_input_correction(rsense_microOhm);
		}
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
	rc = regmap_write(max34417->regmap, MAX34417_CONTROL_REG, MAX34417_CONTROL_MODE);
	if (rc)
		return dev_err_probe(dev, rc, "Error writing control register\n");

	/* Reset the accumulators */
	rc = regmap_write(max34417->regmap, MAX34417_UPDATE_REG, 1);
	if (rc)
		return rc;

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
