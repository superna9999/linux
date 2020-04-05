// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Khadas MCU User programmable Memory
 *
 * Copyright (C) 2020 BayLibre SAS
 * Author(s): Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/mfd/khadas-mcu.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/platform_device.h>

static int khadas_mcu_user_mem_read(void *context, unsigned int offset,
			    void *val, size_t bytes)
{
	struct khadas_mcu *khadas_mcu = context;

	return regmap_bulk_read(khadas_mcu->map,
				KHADAS_MCU_USER_DATA_0_REG + offset,
				val, bytes);
}

static int khadas_mcu_user_mem_write(void *context, unsigned int offset,
			     void *val, size_t bytes)
{
	struct khadas_mcu *khadas_mcu = context;

	return regmap_bulk_write(khadas_mcu->map,
				KHADAS_MCU_USER_DATA_0_REG + offset,
				val, bytes);
}

static ssize_t password_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct khadas_mcu *khadas_mcu = dev_get_drvdata(dev);
	int i, ret;

	if (count < 6)
		return -EINVAL;

	ret = regmap_write(khadas_mcu->map, KHADAS_MCU_PASSWD_START_REG, 1);
	if (ret)
		return ret;

	for (i = 0 ; i < 6 ; ++i) {
		ret = regmap_write(khadas_mcu->map,
				   KHADAS_MCU_CHECK_USER_PASSWD_REG,
				   buf[i]);
		if (ret)
			goto out;
	}

	ret = regmap_write(khadas_mcu->map, KHADAS_MCU_PASSWD_START_REG, 0);
	if (ret)
		return ret;

	return count;
out:
	regmap_write(khadas_mcu->map, KHADAS_MCU_PASSWD_START_REG, 0);

	return ret;
}

static DEVICE_ATTR_WO(password);

static struct attribute *khadas_mcu_user_mem_sysfs_attributes[] = {
	&dev_attr_password.attr,
	NULL,
};

static const struct attribute_group khadas_mcu_user_mem_sysfs_attr_group = {
	.attrs = khadas_mcu_user_mem_sysfs_attributes,
};

static int khadas_mcu_user_mem_probe(struct platform_device *pdev)
{
	struct khadas_mcu *khadas_mcu = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct nvmem_device *nvmem;
	struct nvmem_config *econfig;

	econfig = devm_kzalloc(dev, sizeof(*econfig), GFP_KERNEL);
	if (!econfig)
		return -ENOMEM;

	econfig->dev = pdev->dev.parent;
	econfig->name = dev_name(pdev->dev.parent);
	econfig->stride = 1;
	econfig->word_size = 1;
	econfig->reg_read = khadas_mcu_user_mem_read;
	econfig->reg_write = khadas_mcu_user_mem_write;
	econfig->size = 56;
	econfig->priv = khadas_mcu;

	platform_set_drvdata(pdev, khadas_mcu);

	nvmem = devm_nvmem_register(&pdev->dev, econfig);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	return sysfs_create_group(&pdev->dev.kobj,
				  &khadas_mcu_user_mem_sysfs_attr_group);
}

static const struct platform_device_id khadas_mcu_user_mem_id_table[] = {
	{ .name = "khadas-mcu-user-mem", },
	{},
};
MODULE_DEVICE_TABLE(platform, khadas_mcu_user_mem_id_table);

static struct platform_driver khadas_mcu_user_mem_driver = {
	.probe = khadas_mcu_user_mem_probe,
	.driver = {
		.name = "khadas-mcu-user-mem",
	},
	.id_table = khadas_mcu_user_mem_id_table,
};

module_platform_driver(khadas_mcu_user_mem_driver);

MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_DESCRIPTION("Khadas MCU User MEM driver");
MODULE_LICENSE("GPL v2");
