#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/module.h>
#include <linux/sysfs.h>

#define MAX34417_BULK_UPDATE_REG 0x0

struct max34417_bulk {
	struct iio_trigger *trig;
	struct i2c_client *client;
};

/* Writing 1 to this sysfs node fires the broadcast command and wakes consumers */
static ssize_t trigger_now_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct iio_trigger *trig = to_iio_trigger(dev);
	struct max34417_bulk *bulk = iio_trigger_get_drvdata(trig);
	bool trigger;
	int rc;

	rc = kstrtobool(buf, &trigger);
	if (rc || !trigger)
		return rc ? rc : count;

	/* 1. Broadcast the UPDATE command to 0x2C */
	i2c_smbus_write_byte_data(bulk->client, MAX34417_BULK_UPDATE_REG, 1);

	/* 2. Notify all attached IIO consumers to read their bulk registers */
	iio_trigger_poll(trig);

	return count;
}
static DEVICE_ATTR_WO(trigger_now);

static struct attribute *max34417_bulk_attrs[] = {
	&dev_attr_trigger_now.attr,
	NULL,
};
ATTRIBUTE_GROUPS(max34417_bulk);

static int max34417_bulk_probe(struct i2c_client *client)
{
	struct max34417_bulk *bulk;
	int rc;

	bulk = devm_kzalloc(&client->dev, sizeof(*bulk), GFP_KERNEL);
	if (!bulk)
		return -ENOMEM;

	bulk->client = client;
	bulk->trig = devm_iio_trigger_alloc(&client->dev, "max34417-bulk-%s",
					    dev_name(&client->adapter->dev));
	if (!bulk->trig)
		return -ENOMEM;

	bulk->trig->dev.groups = max34417_bulk_groups;
	iio_trigger_set_drvdata(bulk->trig, bulk);

	rc = devm_iio_trigger_register(&client->dev, bulk->trig);
	if (rc)
		return rc;

	return 0;
}

static const struct i2c_device_id max34417_bulk_id[] = {
	{ "max34417-bulk", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max34417_bulk_id);

static struct i2c_driver max34417_bulk_driver = {
	.driver = {
		.name = "max34417-bulk",
	},
	.probe = max34417_bulk_probe,
	.id_table = max34417_bulk_id,
};
module_i2c_driver(max34417_bulk_driver);

MODULE_AUTHOR("Neil Armstrong <neil.armstrong@linaro.org>");
MODULE_DESCRIPTION("Analog Devices MAX34417 Bulk I2C IIO Trigger");
MODULE_LICENSE("GPL");
