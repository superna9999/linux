// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Amlogic Meson IR remote transmitter
 *
 * Copyright (C) 2019 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/reset.h>

#include <media/rc-core.h>

#define DRIVER_NAME		"meson-ir-blaster"
#define DEVICE_NAME		"Amlogic IR Blaster"

#define IR_BLASTER_OFFSET			0xc

#define IR_BLASTER_ADDR0			(IR_BLASTER_OFFSET + 0x0)
#define 	IR_BLASTER_ADDR0_BUSY		BIT(26)
#define 	IR_BLASTER_ADDR0_FULL		BIT(25)
#define 	IR_BLASTER_ADDR0_EMPTY		BIT(24)
#define 	IR_BLASTER_ADDR0_FIFO_LEVEL	GENMASK(23, 16)
#define 	IR_BLASTER_ADDR0_MOD_CLOCK	GENMASK(13, 12)	
#define 	IR_BLASTER_ADDR0_SLOW_CLK_DIV	GENMASK(11, 4)
#define 	IR_BLASTER_ADDR0_SLOW_CLK_MODE	BIT(3)
#define 	IR_BLASTER_ADDR0_INIT_HIGH	BIT(2)
#define 	IR_BLASTER_ADDR0_INIT_LOW	BIT(1)
#define 	IR_BLASTER_ADDR0_ENABLE		BIT(0)

enum {
	IR_BLASTER_MOD_CLOCK_SYS_CLK	= 0,
	IR_BLASTER_MOD_CLOCK_MPEG_XTAL3	= 1,
	IR_BLASTER_MOD_CLOCK_MPEG_1US	= 2,
	IR_BLASTER_MOD_CLOCK_MPEG_10US	= 3,
};

#define IR_BLASTER_ADDR1			(IR_BLASTER_OFFSET + 0x4)
#define 	IR_BLASTER_ADDR1_COUNT_HIGH	GENMASK(31, 16)
#define 	IR_BLASTER_ADDR1_COUNT_LOW	GENMASK(15, 0)

#define IR_BLASTER_ADDR2			(IR_BLASTER_OFFSET + 0x8)
#define 	IR_BLASTER_ADDR2_WRITE_FIFO	BIT(16)
#define 	IR_BLASTER_ADDR2_MOD_ENABLE	BIT(12)
#define 	IR_BLASTER_ADDR2_TIMEBASE	GENMASK(11, 10)
#define 	IR_BLASTER_ADDR2_COUNT		GENMASK(9, 0)

#define IR_BLASTER_ADDR3			(IR_BLASTER_OFFSET + 0xc)
#define		IR_BLASTER_ADDR3_THRESHOLD_PEND	BIT(16)
#define		IR_BLASTER_ADDR3_IRQ_ENABLE	BIT(8)
#define		IR_BLASTER_ADDR3_IRQ_THRESHOLD	GENMASK(7, 0)

enum {
	IR_BLASTER_TIMEBASE_1US		= 0,
	IR_BLASTER_TIMEBASE_10US	= 1,
	IR_BLASTER_TIMEBASE_100US	= 2,
	IR_BLASTER_TIMEBASE_MOD_CLOCK	= 3,
};

struct meson_ir_blaster {
	struct regmap		*regmap;
	struct rc_dev		*rc;
	struct reset_control	*reset;
	unsigned int		carrier;
	unsigned int		duty_cycle;
	struct clk		*clk;
	struct regulator	*supply;
};

static int meson_ir_blaster_set_duty_cycle(struct rc_dev *dev, u32 duty_cycle)
{
	struct meson_ir_blaster *ir = dev->priv;

	ir->duty_cycle = duty_cycle;

	return 0;
}

static int meson_ir_blaster_set_carrier(struct rc_dev *dev, u32 carrier)
{
	struct meson_ir_blaster *ir = dev->priv;

	if (!carrier)
		return -EINVAL;
	
	if (carrier < 32000 || carrier > 56000)
		return -EINVAL;

	ir->carrier = carrier;

	return 0;
}

static int meson_ir_blaster_tx(struct rc_dev *dev, unsigned int *txbuf,
			       unsigned int count)
{
	struct meson_ir_blaster *ir = dev->priv;
	int i, duty, period;
	unsigned int reg;
	int ret = 0;

	period = DIV_ROUND_CLOSEST(USEC_PER_SEC, ir->carrier);
	duty = DIV_ROUND_CLOSEST(ir->duty_cycle * period, 100);

	reset_control_reset(ir->reset);

	regmap_write(ir->regmap, IR_BLASTER_ADDR0,
		     IR_BLASTER_ADDR0_INIT_HIGH |
		     FIELD_PREP(IR_BLASTER_ADDR0_MOD_CLOCK,
			        IR_BLASTER_MOD_CLOCK_MPEG_1US));

	regmap_write(ir->regmap, IR_BLASTER_ADDR1,
		     FIELD_PREP(IR_BLASTER_ADDR1_COUNT_HIGH, (duty - 1)) |
		     FIELD_PREP(IR_BLASTER_ADDR1_COUNT_LOW,
			        ((period - duty) - 1)));

	regmap_update_bits(ir->regmap, IR_BLASTER_ADDR0,
			   IR_BLASTER_ADDR0_INIT_HIGH, 0);

	/* TOFIX add IRQ FIFO support */

	regmap_update_bits(ir->regmap, IR_BLASTER_ADDR0,
			   IR_BLASTER_ADDR0_ENABLE, IR_BLASTER_ADDR0_ENABLE);

	for (i = 0 ; i < count ; i++) {
		unsigned long timebase;
		unsigned long delay;

		if (!(i % 2))
			reg = IR_BLASTER_ADDR2_MOD_ENABLE;
		else
			reg = 0;
		
		if (txbuf[i] <= 1024) {
			timebase = IR_BLASTER_TIMEBASE_1US;
			delay = txbuf[i] - 1;
		} else if (txbuf[i] <= 10240) {
			timebase = IR_BLASTER_TIMEBASE_10US;
			delay = DIV_ROUND_CLOSEST(txbuf[i], 10) - 1;
		} else if (txbuf[i] <= 102400) {
			timebase = IR_BLASTER_TIMEBASE_100US;
			delay = DIV_ROUND_CLOSEST(txbuf[i], 100) - 1;
		} else {
			timebase = IR_BLASTER_TIMEBASE_MOD_CLOCK;
			delay = DIV_ROUND_CLOSEST(txbuf[i], period) - 1;
		}

		reg |= IR_BLASTER_ADDR2_WRITE_FIFO;
		reg |= FIELD_PREP(IR_BLASTER_ADDR2_TIMEBASE, timebase);
		reg |= FIELD_PREP(IR_BLASTER_ADDR2_COUNT, delay);

		regmap_write(ir->regmap, IR_BLASTER_ADDR2, reg);

		/* Continue while FIFO is not full */
		regmap_read(ir->regmap, IR_BLASTER_ADDR0, &reg);
		if (!(reg & IR_BLASTER_ADDR0_FULL))
			continue;

		ret = regmap_read_poll_timeout(ir->regmap,
					       IR_BLASTER_ADDR0, reg,
					       !(reg & IR_BLASTER_ADDR0_FULL),
					       5, 1000);
		if (ret)
			break;
	}

	ret = regmap_read_poll_timeout(ir->regmap,
				       IR_BLASTER_ADDR0, reg,
			               !(reg & IR_BLASTER_ADDR0_BUSY),
			               5, 1000);

	/* Reset */
	reset_control_reset(ir->reset);

	return ret;
}

static int meson_ir_blaster_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct meson_ir_blaster *ir;
	int ret;

	ir = devm_kzalloc(dev, sizeof(struct meson_ir_blaster), GFP_KERNEL);
	if (!ir)
		return -ENOMEM;

	ir->regmap = syscon_node_to_regmap(of_get_parent(pdev->dev.of_node));
	if (IS_ERR(ir->regmap)) {
		dev_err(&pdev->dev, "failed to get regmap\n");
		return PTR_ERR(ir->regmap);
	}

	ir->reset = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(ir->reset)) {
		dev_err(dev, "failed to get reset\n");
		return PTR_ERR(ir->reset);
	}

	ir->supply = devm_regulator_get_optional(dev, "vcc");
	if (IS_ERR(ir->supply)) {
		if (PTR_ERR(ir->supply) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		ir->supply = NULL;
	}

	ir->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ir->clk)) {
		if (PTR_ERR(ir->clk) == -EPROBE_DEFER)
			return PTR_ERR(ir->clk);
		ir->clk = NULL;
	}

	ir->rc = devm_rc_allocate_device(dev, RC_DRIVER_IR_RAW_TX);
	if (!ir->rc)
		return -ENOMEM;

	ir->rc->priv = ir;
	ir->rc->driver_name = DRIVER_NAME;
	ir->rc->device_name = DEVICE_NAME;
	ir->rc->tx_ir = meson_ir_blaster_tx;
	ir->rc->s_tx_duty_cycle = meson_ir_blaster_set_duty_cycle;
	ir->rc->s_tx_carrier = meson_ir_blaster_set_carrier;

	platform_set_drvdata(pdev, ir);

	ret = devm_rc_register_device(dev, ir->rc);
	if (ret) {
		dev_err(dev, "failed to register rc device\n");
		return ret;
	}

	if (ir->supply) {
		ret = regulator_enable(ir->supply);
		if (ret)
			return ret;
	}
	
	clk_prepare_enable(ir->clk);

	reset_control_deassert(ir->reset);

	return 0;
}

static int meson_ir_blaster_remove(struct platform_device *pdev)
{
	struct meson_ir_blaster *ir = platform_get_drvdata(pdev);

	reset_control_assert(ir->reset);

	clk_disable_unprepare(ir->clk);

	if (ir->supply)
		regulator_disable(ir->supply);

	return 0;
}

static const struct of_device_id meson_ir_blaster_match[] = {
	{ .compatible = "amlogic,g12a-ir-blaster" },
	{ },
};
MODULE_DEVICE_TABLE(of, meson_ir_blaster_match);

static struct platform_driver meson_ir_blaster_driver = {
	.probe		= meson_ir_blaster_probe,
	.remove		= meson_ir_blaster_remove,
	.driver = {
		.name		= DRIVER_NAME,
		.of_match_table	= meson_ir_blaster_match,
	},
};

module_platform_driver(meson_ir_blaster_driver);

MODULE_DESCRIPTION("Amlogic Meson IR remote transmitter driver");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("GPL v2");
