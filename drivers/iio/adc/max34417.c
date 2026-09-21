// SPDX-License-Identifier: GPL-2.0
/*
 * IIO driver for Maxim MAX34417 ADC, 4-Channels High Dynamic Range Power Accumulator
 *
 * Datasheet: https://www.analog.com/en/products/max34417.html
 *
 * TODO: Slow Mode, Continuous Accumulate Mode, Park Feature, Bulk Update, Perr_Verr Correction
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/units.h>

#include <linux/iio/iio.h>
#include <linux/iio/types.h>

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

#define MAX34417_DEFAULT_CMM_WIDE	(MAX34417_CONTROL_MODE | MAX34417_CONTROL_SMM)

#define MAX34417_DEFAULT_RSENSE		1000

#define MAX34417_PWR_CORRECTION_SCALE		24
#define MAX34417_PWR_AVG_FULL_SCALE_BITS	30

#define MAX34417_VOLTAGE_CORRECTION_SCALE	24
#define MAX34417_VOLTAGE_FULL_SCALE_BITS	14

#define MAX34417_CHANNEL_COUNT			4

/**
 * struct max34417_data - max34417 specific data.
 * @regmap:	device register map.
 * @dev:	max34417 device.
 * @lock:	lock for protecting access to device hardware registers, mostly
 *		for reading common accumulator count and control register.
 * @input_correction:	Correction based on the Rsense value from channel nodes.
 * @input_label:	Channel label from channel nodes.
 */
struct max34417_data {
	struct regmap *regmap;
	struct device *dev;
	struct mutex lock;
	u32 input_correction[MAX34417_CHANNEL_COUNT];
	const char *input_label[MAX34417_CHANNEL_COUNT];
};

static const struct regmap_config max34417_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= MAX34417_DID_REG,
};

#define MAX34417_CHANNEL(_index, _v_address, _power_address)	\
	{							\
		.type = IIO_VOLTAGE,				\
		.info_mask_separate	= BIT(IIO_CHAN_INFO_RAW) | \
					  BIT(IIO_CHAN_INFO_SCALE), \
		.channel = (_index),				\
		.address = (_v_address),			\
		.indexed = 1,					\
	},							\
	{							\
		.type = IIO_POWER,				\
		.info_mask_separate	= BIT(IIO_CHAN_INFO_AVERAGE_RAW) | \
					  BIT(IIO_CHAN_INFO_SCALE), \
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

static int max34417_accumulator_update(struct max34417_data *max34417)
{
	int rc;

	rc = regmap_write(max34417->regmap, MAX34417_UPDATE_REG, 1);
	if (rc) {
		dev_err(max34417->dev, "Error (%d) writing update register\n", rc);
		return rc;
	}

	/* Wait for accumulator update */
	usleep_range(1000, 2000);

	return 0;
}

static int max34417_read_voltage(struct max34417_data *max34417,
				 const struct iio_chan_spec *chan, int *val)
{
	uint16_t voltage;
	uint8_t buf[3];
	int rc;

	guard(mutex)(&max34417->lock);

	rc = max34417_accumulator_update(max34417);
	if (rc)
		return rc;

	rc = regmap_noinc_read(max34417->regmap, chan->address, &buf, 3);
	if (rc)
		return rc;

	voltage = buf[2] | ((uint64_t)buf[1] << 8);
	voltage >>= 2;

	*val = voltage;

	return IIO_VAL_INT;
}

static int max34417_read_power(struct max34417_data *max34417,
				 const struct iio_chan_spec *chan, int *val, int *val2)
{
	uint32_t acc_count;
	uint64_t power;
	uint8_t buf[8];
	int rc;

	guard(mutex)(&max34417->lock);

	rc = max34417_accumulator_update(max34417);
	if (rc)
		return rc;

	rc = regmap_noinc_read(max34417->regmap, MAX34417_ACC_COUNT_REG,
			       &buf, 4);
	if (rc)
		return rc;

	acc_count = buf[3] | ((uint64_t)buf[2] << 8) | ((uint64_t)buf[1] << 16);

	rc = regmap_noinc_read(max34417->regmap, chan->address, &buf, 8);
	if (rc)
		return rc;

	power = buf[7];
	power |= ((uint64_t)buf[6] << 8UL);
	power |= ((uint64_t)buf[5] << 16UL);
	power |= ((uint64_t)buf[4] << 24UL);
	power |= ((uint64_t)buf[3] << 32UL);
	power |= ((uint64_t)buf[2] << 40UL);
	power |= ((uint64_t)buf[1] << 48UL);

	power = div_u64(power, acc_count);

	*val = FIELD_GET(GENMASK(31, 0), power);
	*val2 = FIELD_GET(GENMASK(55, 32), power);

	return IIO_VAL_INT_64;
}

static int max34417_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct max34417_data *max34417 = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type == IIO_VOLTAGE)
			return max34417_read_voltage(max34417, chan, val);
		return -EINVAL;
	case IIO_CHAN_INFO_AVERAGE_RAW:
		if (chan->type == IIO_POWER)
			return max34417_read_power(max34417, chan, val, val2);
		return -EINVAL;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_VOLTAGE) {
			/* Scale to mA */
			*val = MAX34417_VOLTAGE_CORRECTION_SCALE * MILLI;
			*val2 = MAX34417_VOLTAGE_FULL_SCALE_BITS;

			return IIO_VAL_FRACTIONAL_LOG2;
		} else if (chan->type == IIO_POWER) {
			/* Scale to mW */
			*val = max34417->input_correction[chan->channel] * MILLI;
			*val2 = MAX34417_PWR_AVG_FULL_SCALE_BITS;

			return IIO_VAL_FRACTIONAL_LOG2;
		}
		return -EINVAL;
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

	if (chan->type == IIO_VOLTAGE) {
		if (input_label)
			return sysfs_emit(label, "%s-voltage\n", input_label);
		return sysfs_emit(label, "channel%d-voltage\n", chan->channel);
	}

	if (chan->type == IIO_POWER) {
		if (input_label)
			return sysfs_emit(label, "%s-power\n", input_label);
		return sysfs_emit(label, "channel%d-power\n", chan->channel);
	}

	return 0;
}

static const struct iio_info max34417_info = {
	.read_raw	= max34417_read_raw,
	.read_label	= max34417_read_label,
};

static int max34417_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct max34417_data *max34417;
	struct fwnode_handle *node;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int rc;

	regmap = devm_regmap_init_i2c(client, &max34417_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err_probe(dev, PTR_ERR(regmap),
			      "regmap_init failed\n");
		return PTR_ERR(regmap);
	}

	indio_dev = devm_iio_device_alloc(dev, sizeof(*max34417));
	if (!indio_dev)
		return -ENOMEM;

	/* Get and enable regulators */
	rc = devm_regulator_get_enable(dev, "vdd");
	if (rc)
		return dev_err_probe(dev, rc, "failed to get vdd regulator\n");

	rc = devm_regulator_get_enable(dev, "vio");
	if (rc)
		return dev_err_probe(dev, rc, "failed to get vio regulator\n");

	max34417 = iio_priv(indio_dev);
	max34417->regmap = regmap;
	max34417->dev = dev;
	mutex_init(&max34417->lock);

	device_for_each_child_node(dev, node) {
		unsigned int correction;
		u32 rsense, index;

		if (fwnode_property_read_u32(node, "reg", &index)) {
			dev_err(dev, "missing reg property of %pfwP\n", node);
			return -EINVAL;
		} else if (index >= MAX34417_CHANNEL_COUNT) {
			dev_err(dev, "invalid reg %d of %pfwP\n", index, node);
			return -EINVAL;
		}

		fwnode_property_read_string(node, "label", &max34417->input_label[index]);
		if (fwnode_property_read_u32(node, "maxim,rsense-val-micro-ohms", &rsense))
			rsense = MAX34417_DEFAULT_RSENSE;

		/* (100 milliOhm / rsense) * MAX34417_PWR_CORRECTION_SCALE */
		correction = (100 * MILLI * MAX34417_PWR_CORRECTION_SCALE) / rsense;

		max34417->input_correction[index] = correction;
	}

	indio_dev->channels = max34417_channels;
	indio_dev->num_channels = ARRAY_SIZE(max34417_channels);
	indio_dev->name = "max34417";
	indio_dev->info = &max34417_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	/* Set as default Manual Mode & Wide ADC */
	rc = regmap_write(max34417->regmap, MAX34417_CONTROL_REG, MAX34417_DEFAULT_CMM_WIDE);
	if (rc) {
		dev_err(max34417->dev, "Error (%d) writing control register\n", rc);
		return rc;
	}

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id max34417_of_match[] = {
	{ .compatible = "maxim,max34417" },
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
MODULE_DESCRIPTION("Maxim MAX34417 ADC driver");
MODULE_LICENSE("GPL");
