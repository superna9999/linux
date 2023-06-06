// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Goodix Berlin Touchscreen Driver
 *
 * Copyright (C) 2020 - 2021 Goodix, Inc.
 * Copyright (C) 2023 Linaro Ltd.
 *
 * Based on goodix_ts_berlin driver.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <asm/unaligned.h>

#include "goodix_berlin.h"

#define SPI_TRANS_PREFIX_LEN	1
#define REGISTER_WIDTH		4
#define SPI_READ_DUMMY_LEN	3
#define SPI_READ_PREFIX_LEN	(SPI_TRANS_PREFIX_LEN + REGISTER_WIDTH + SPI_READ_DUMMY_LEN)
#define SPI_WRITE_PREFIX_LEN	(SPI_TRANS_PREFIX_LEN + REGISTER_WIDTH)

#define SPI_WRITE_FLAG		0xF0
#define SPI_READ_FLAG		0xF1

static int goodix_berlin_spi_read(void *context, const void *reg_buf,
				  size_t reg_size, void *val_buf,
				  size_t val_size)
{
	struct spi_device *spi = context;
	struct spi_transfer xfers;
	struct spi_message spi_msg;
	const u32 *reg = reg_buf; /* reg is stored as native u32 at start of buffer */
	u8 *buf = NULL;
	int ret = 0;

	if (reg_size != REGISTER_WIDTH)
		return -EINVAL;

	buf = kzalloc(SPI_READ_PREFIX_LEN + val_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	spi_message_init(&spi_msg);
	memset(&xfers, 0, sizeof(xfers));

	/* buffer format: 0xF1 + addr(4bytes) + dummy(3bytes) + data */
	buf[0] = SPI_READ_FLAG;
	put_unaligned_be32(*reg, buf + SPI_TRANS_PREFIX_LEN);
	memset(buf + SPI_TRANS_PREFIX_LEN + REGISTER_WIDTH, 0xff,
	       SPI_READ_DUMMY_LEN);

	xfers.tx_buf = buf;
	xfers.rx_buf = buf;
	xfers.len = SPI_READ_PREFIX_LEN + val_size;
	xfers.cs_change = 0;
	spi_message_add_tail(&xfers, &spi_msg);

	ret = spi_sync(spi, &spi_msg);
	if (ret < 0) {
		dev_err(&spi->dev, "transfer error:%d", ret);
		goto exit;
	}

	memcpy(val_buf, buf + SPI_READ_PREFIX_LEN, val_size);
exit:
	kfree(buf);
	return ret;
}

static int goodix_berlin_spi_write(void *context, const void *data,
				   size_t count)
{
	unsigned int len = count - REGISTER_WIDTH;
	struct spi_device *spi = context;
	struct spi_transfer xfers;
	struct spi_message spi_msg;
	const u32 *reg = data; /* reg is stored as native u32 at start of buffer */
	u8 *buf = NULL;
	int ret = 0;

	buf = kzalloc(SPI_WRITE_PREFIX_LEN + len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	spi_message_init(&spi_msg);
	memset(&xfers, 0, sizeof(xfers));

	buf[0] = SPI_WRITE_FLAG;
	put_unaligned_be32(*reg, buf + SPI_TRANS_PREFIX_LEN);
	memcpy(buf + SPI_WRITE_PREFIX_LEN, data + REGISTER_WIDTH, len);

	xfers.tx_buf = buf;
	xfers.len = SPI_WRITE_PREFIX_LEN + len;
	xfers.cs_change = 0;
	spi_message_add_tail(&xfers, &spi_msg);

	ret = spi_sync(spi, &spi_msg);
	if (ret < 0)
		dev_err(&spi->dev, "transfer error:%d", ret);

	kfree(buf);
	return ret;
}

static const struct regmap_config goodix_berlin_spi_regmap_conf = {
	.reg_bits = 32,
	.val_bits = 8,
	.read = goodix_berlin_spi_read,
	.write = goodix_berlin_spi_write,
};

static const struct input_id goodix_berlin_spi_input_id = {
	.bustype = BUS_SPI,
	.vendor = 0x0416,
	.product = 0x1001,
};

static int goodix_berlin_spi_probe(struct spi_device *spi)
{
	struct regmap_config *cfg;
	struct regmap *map;
	size_t max_size;
	int ret = 0;

	cfg = devm_kmemdup(&spi->dev, &goodix_berlin_spi_regmap_conf,
			   sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret)
		return ret;

	max_size = spi_max_transfer_size(spi);
	cfg->max_raw_read = max_size - SPI_READ_PREFIX_LEN;
	cfg->max_raw_write = max_size - SPI_WRITE_PREFIX_LEN;

	map = devm_regmap_init(&spi->dev, NULL, spi, cfg);
	if (IS_ERR(map))
		return PTR_ERR(map);

	return goodix_berlin_probe(&spi->dev, spi->irq,
				   &goodix_berlin_spi_input_id, map);
}

static void goodix_berlin_spi_remove(struct spi_device *spi)
{
	goodix_berlin_remove(&spi->dev);
}

static const struct of_device_id goodix_berlin_spi_of_match[] = {
	{
		.compatible = "goodix,gt9916",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, goodix_berlin_spi_of_match);

static const struct spi_device_id goodix_berlin_spi_ids[] = {
	{ "gt9916" },
	{ },
};
MODULE_DEVICE_TABLE(spi, goodix_berlin_spi_ids);

static struct spi_driver goodix_berlin_spi_driver = {
	.driver = {
		.name = "goodix-berlin-spi",
		.of_match_table = goodix_berlin_spi_of_match,
		.pm = pm_sleep_ptr(&goodix_berlin_pm_ops),
	},
	.id_table = goodix_berlin_spi_ids,
	.probe = goodix_berlin_spi_probe,
	.remove = goodix_berlin_spi_remove,
};
module_spi_driver(goodix_berlin_spi_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Goodix Berlin SPI Touchscreen driver");
MODULE_AUTHOR("Neil Armstrong <neil.armstrong@linaro.org>");
