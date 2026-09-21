// SPDX-License-Identifier: GPL-2.0
/*
 * IIO driver for Maxim MAX34417 ADC, 4-Channels High Dynamic Range Power Accumulator
 *
 * Datasheet: https://www.analog.com/en/products/max34417.html
 *
 * TODO: Slow Mode, Continuous Accumulate Mode, Park Feature, Bulk Update
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>

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

static int max34417_calc_correction(u32 rsense, u32 * correction)
{
	switch (rsense) {
	case 100:
		*correction = 1 * MAX34417_PWR_CORRECTION_SCALE;
		break;
	case 50:
		*correction = 2 * MAX34417_PWR_CORRECTION_SCALE;
		break;
	case 40:
		*correction = 2.5 * MAX34417_PWR_CORRECTION_SCALE;
		break;
	case 25:
		*correction = 4 * MAX34417_PWR_CORRECTION_SCALE;
		break;
	case 20:
		*correction = 5 * MAX34417_PWR_CORRECTION_SCALE;
		break;
	case 15:
		*correction = 6.667f * MAX34417_PWR_CORRECTION_SCALE;
		break;
	case 10:
		*correction = 10 * MAX34417_PWR_CORRECTION_SCALE;
		break;
	case 5:
		*correction = 20 * MAX34417_PWR_CORRECTION_SCALE;
		break;
	case 4:
		*correction = 25 * MAX34417_PWR_CORRECTION_SCALE;
		break;
	case 2:
		*correction = 50 * MAX34417_PWR_CORRECTION_SCALE;
		break;
	case 1:
		*correction = 100 * MAX34417_PWR_CORRECTION_SCALE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#define MAX34417_CHANNEL(_index, _v_address, _curr_address)	\
	{							\
		.type = IIO_VOLTAGE,				\
		.info_mask_separate	= BIT(IIO_CHAN_INFO_RAW) | \
					  BIT(IIO_CHAN_INFO_SCALE) | \
					  BIT(IIO_CHAN_INFO_OFFSET), \
		.channel = (_index) * 2,			\
		.address = (_v_address),			\
		.indexed = 1,					\
	},							\
	{							\
		.type = IIO_CURRENT,				\
		.info_mask_separate	= BIT(IIO_CHAN_INFO_AVERAGE_RAW) | \
					  BIT(IIO_CHAN_INFO_SCALE) | \
					  BIT(IIO_CHAN_INFO_OFFSET), \
		.channel = ((_index) * 2) + 1,			\
		.address = (_curr_address),			\
		.indexed = 1,					\
	}

static const struct iio_chan_spec max34417_channels[] = {
	MAX34417_CHANNEL(0, MAX34417_V_CH1_REG, MAX34417_PWR_ACC_1_REG),
	MAX34417_CHANNEL(1, MAX34417_V_CH2_REG, MAX34417_PWR_ACC_2_REG),
	MAX34417_CHANNEL(2, MAX34417_V_CH3_REG, MAX34417_PWR_ACC_3_REG),
	MAX34417_CHANNEL(3, MAX34417_V_CH4_REG, MAX34417_PWR_ACC_4_REG),
};

static int max34417_accumulator_update(struct max34417_data *max34417)
{
	int rc;

	rc = regmap_write(max34417->regmap, MAX34417_UPDATE_REG, 1);
	if (rc) {
		dev_err(max34417->dev, "Error (%d) writing updateregister\n", rc);
		return rc;
	}

	// Wait for accumulator update
	usleep_range(1000, 2000);

	return 0;
}

static int max34417_read_voltage(struct max34417_data *max34417,
				 const struct iio_chan_spec *chan, int *val)
{
	uint16_t val16;
	int rc;

	guard(mutex)(&max34417->lock);

	rc = max34417_accumulator_update(max34417);
	if (rc)
		return rc;

	rc = regmap_noinc_read(max34417->regmap, chan->address, &val16, sizeof(val16));
	if (rc)
		return rc;

	*val = val16;

	return IIO_VAL_INT;
}

static int max34417_read_current(struct max34417_data *max34417,
				 const struct iio_chan_spec *chan, int *val, int *val2)
{
	uint64_t val56 = 0, scaled_val;
	uint32_t acc_count;
	int rc;

	guard(mutex)(&max34417->lock);

	rc = max34417_accumulator_update(max34417);
	if (rc)
		return rc;

	rc = regmap_noinc_read(max34417->regmap, MAX34417_ACC_COUNT_REG,
			       &acc_count, sizeof(acc_count));
	if (rc)
		return rc;

	rc = regmap_noinc_read(max34417->regmap, chan->address, &val56, 7);
	if (rc)
		return rc;

	scaled_val = div_u64(val56, acc_count);

	*val = FIELD_GET(GENMASK(31, 0), scaled_val);
	*val2 = FIELD_GET(GENMASK(55, 32), scaled_val);

	return IIO_VAL_INT_64;
}

static int max34417_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct max34417_data *max34417 = iio_priv(indio_dev);
	unsigned channel_number = chan->channel / 2;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type == IIO_VOLTAGE)
			return max34417_read_voltage(max34417, chan, val);
		return -EINVAL;
	case IIO_CHAN_INFO_AVERAGE_RAW:
		if (chan->type == IIO_CURRENT)
			return max34417_read_current(max34417, chan, val, val2);
		return -EINVAL;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_VOLTAGE) {
			*val = MAX34417_VOLTAGE_CORRECTION_SCALE * 1000;
			*val2 = 4 * BIT(MAX34417_VOLTAGE_FULL_SCALE_BITS);

			return IIO_VAL_FRACTIONAL;
		} else if (chan->type == IIO_CURRENT) {
			*val = max34417->input_correction[channel_number] * 1000000;
			*val2 = BIT(MAX34417_PWR_AVG_FULL_SCALE_BITS);

			return IIO_VAL_FRACTIONAL;
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
	unsigned channel_number = chan->channel / 2;
	const char *input_label = max34417->input_label[channel_number];

	if (chan->type == IIO_VOLTAGE) {
		if (input_label)
			return sysfs_emit(label, "%s-voltage\n", input_label);
		return sysfs_emit(label, "channel%d-voltage\n", channel_number);
	}

	if (chan->type == IIO_CURRENT) {
		if (input_label)
			return sysfs_emit(label, "%s-current\n", input_label);
		return sysfs_emit(label, "channel%d-current\n", channel_number);
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

	max34417 = iio_priv(indio_dev);
	max34417->regmap = regmap;
	max34417->dev = dev;
	mutex_init(&max34417->lock);

	device_for_each_child_node(dev, node) {
		u32 val, index;
		
		rc = fwnode_property_read_u32(node, "reg", &val);
		if (rc) {
			dev_err(dev, "missing reg property of %pfwP\n", node);
			return rc;
		} else if (val > MAX34417_CHANNEL_COUNT) {
			dev_err(dev, "invalid reg %d of %pfwP\n", val, node);
			return -EINVAL;
		}

		index = val;

		rc = fwnode_property_read_string(node, "label", &max34417->input_label[index]);
		if (rc) {
			dev_err(dev, "missing label property of %pfwP\n", node);
			return rc;
		}

		rc = fwnode_property_read_u32(node, "maxim,rsense-val-micro-ohms", &val);
		if (rc) {
			dev_err(dev, "missing rsense property of %pfwP\n", node);
			return rc;
		}

		rc = max34417_calc_correction(val, &max34417->input_correction[index]);
		if (rc) {
			dev_err(dev, "invalid rsense property %d of %pfwP\n", val, node);
			return rc;
		}
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
