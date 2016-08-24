/* 
 * Copyright (c) 2010, BayLibre, SAS. All rights reserved.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * Driver for Semtech SX150X I2C GPIO Expanders
 *
 * Author: Gregory Bean <gbean@codeaurora.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf-generic.h>

#include "core.h"
#include "pinconf.h"
#include "pinctrl-utils.h"

/* The chip models of sx150x */
enum {
	SX150X_123 = 0,
	SX150X_456,
	SX150X_789,
};

struct sx150x_123_pri {
	u8 reg_pld_mode;
	u8 reg_pld_table0;
	u8 reg_pld_table1;
	u8 reg_pld_table2;
	u8 reg_pld_table3;
	u8 reg_pld_table4;
	u8 reg_advance;
};

struct sx150x_456_pri {
	u8 reg_pld_mode;
	u8 reg_pld_table0;
	u8 reg_pld_table1;
	u8 reg_pld_table2;
	u8 reg_pld_table3;
	u8 reg_pld_table4;
	u8 reg_advance;
};

struct sx150x_789_pri {
	u8 reg_drain;
	u8 reg_polarity;
	u8 reg_clock;
	u8 reg_misc;
	u8 reg_reset;
	u8 ngpios;
};

struct sx150x_device_data {
	u8 model;
	u8 reg_pullup;
	u8 reg_pulldn;
	u8 reg_dir;
	u8 reg_data;
	u8 reg_irq_mask;
	u8 reg_irq_src;
	u8 reg_sense;
	u8 ngpios;
	union {
		struct sx150x_123_pri x123;
		struct sx150x_456_pri x456;
		struct sx150x_789_pri x789;
	} pri;
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	const struct sx150x_pin_group *groups;
	unsigned int ngroups;
};

struct sx150x_pin_group {
	const char *name;
	unsigned int pin;
	struct sx150x_desc_function *functions;
};

struct sx150x_pinctrl {
	struct device *dev;
	struct i2c_client *client;
	struct pinctrl_dev *pctldev;
	struct pinctrl_desc pinctrl_desc;
	struct gpio_chip gpio;
	struct pinctrl_gpio_range range;
	struct mutex lock;
	bool oscio_is_gpo;
	const struct sx150x_device_data *data;
};

static const struct pinctrl_pin_desc sx150x_8_pins[] = {
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),
	PINCTRL_PIN(8, "oscio"),
};

static const struct pinctrl_pin_desc sx150x_16_pins[] = {
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),
	PINCTRL_PIN(8, "gpio8"),
	PINCTRL_PIN(9, "gpio9"),
	PINCTRL_PIN(10, "gpio10"),
	PINCTRL_PIN(11, "gpio11"),
	PINCTRL_PIN(12, "gpio12"),
	PINCTRL_PIN(13, "gpio13"),
	PINCTRL_PIN(14, "gpio14"),
	PINCTRL_PIN(15, "gpio15"),
	PINCTRL_PIN(16, "oscio"),
};

#define SX150X_PINCTRL_GROUP(_pin, _name, ...)				\
	{								\
		.name = #_name,						\
		.pin = _pin,						\
	}

static const struct sx150x_pin_group sx150x_16_groups[] = {
	SX150X_PINCTRL_GROUP(0, gpio0),
	SX150X_PINCTRL_GROUP(1, gpio1),
	SX150X_PINCTRL_GROUP(2, gpio2),
	SX150X_PINCTRL_GROUP(3, gpio3),
	SX150X_PINCTRL_GROUP(4, gpio4),
	SX150X_PINCTRL_GROUP(5, gpio5),
	SX150X_PINCTRL_GROUP(6, gpio6),
	SX150X_PINCTRL_GROUP(7, gpio7),
	SX150X_PINCTRL_GROUP(8, gpio8),
	SX150X_PINCTRL_GROUP(9, gpio9),
	SX150X_PINCTRL_GROUP(10, gpio10),
	SX150X_PINCTRL_GROUP(11, gpio11),
	SX150X_PINCTRL_GROUP(12, gpio12),
	SX150X_PINCTRL_GROUP(13, gpio13),
	SX150X_PINCTRL_GROUP(14, gpio14),
	SX150X_PINCTRL_GROUP(15, gpio15),
	SX150X_PINCTRL_GROUP(16, oscio),
};

static const struct sx150x_pin_group sx150x_8_groups[] = {
	SX150X_PINCTRL_GROUP(0, gpio0),
	SX150X_PINCTRL_GROUP(1, gpio1),
	SX150X_PINCTRL_GROUP(2, gpio2),
	SX150X_PINCTRL_GROUP(3, gpio3),
	SX150X_PINCTRL_GROUP(4, gpio4),
	SX150X_PINCTRL_GROUP(5, gpio5),
	SX150X_PINCTRL_GROUP(6, gpio6),
	SX150X_PINCTRL_GROUP(7, gpio7),
	SX150X_PINCTRL_GROUP(8, oscio),
};

static const struct sx150x_device_data sx1508q_device_data = {
	.model = SX150X_789,
	.reg_pullup	= 0x03,
	.reg_pulldn	= 0x04,
	.reg_dir	= 0x07,
	.reg_data	= 0x08,
	.reg_irq_mask	= 0x09,
	.reg_irq_src	= 0x0c,
	.reg_sense	= 0x0b,
	.pri.x789 = {
		.reg_drain	= 0x05,
		.reg_polarity	= 0x06,
		.reg_clock	= 0x0f,
		.reg_misc	= 0x10,
		.reg_reset	= 0x7d,
	},
	.ngpios = 8,
	.pins = sx150x_8_pins,
	.npins = ARRAY_SIZE(sx150x_8_pins),
	.groups = sx150x_8_groups,
	.ngroups = ARRAY_SIZE(sx150x_8_groups),
};

static const struct sx150x_device_data sx1509q_device_data = {
	.model = SX150X_789,
	.reg_pullup	= 0x07,
	.reg_pulldn	= 0x09,
	.reg_dir	= 0x0f,
	.reg_data	= 0x11,
	.reg_irq_mask	= 0x13,
	.reg_irq_src	= 0x19,
	.reg_sense	= 0x17,
	.pri.x789 = {
		.reg_drain	= 0x0b,
		.reg_polarity	= 0x0d,
		.reg_clock	= 0x1e,
		.reg_misc	= 0x1f,
		.reg_reset	= 0x7d,
	},
	.ngpios	= 16,
	.pins = sx150x_16_pins,
	.npins = ARRAY_SIZE(sx150x_16_pins),
	.groups = sx150x_16_groups,
	.ngroups = ARRAY_SIZE(sx150x_16_groups),
};

static const struct sx150x_device_data sx1506q_device_data = {
	.model = SX150X_456,
	.reg_pullup	= 0x05,
	.reg_pulldn	= 0x07,
	.reg_dir	= 0x03,
	.reg_data	= 0x01,
	.reg_irq_mask	= 0x09,
	.reg_irq_src	= 0x0f,
	.reg_sense	= 0x0d,
	.pri.x456 = {
		.reg_pld_mode	= 0x21,
		.reg_pld_table0	= 0x23,
		.reg_pld_table1	= 0x25,
		.reg_pld_table2	= 0x27,
		.reg_pld_table3	= 0x29,
		.reg_pld_table4	= 0x2b,
		.reg_advance	= 0xad,
	},
	.ngpios	= 16,
	.pins = sx150x_16_pins,
	.npins = ARRAY_SIZE(sx150x_16_pins),
	.groups = sx150x_16_groups,
	.ngroups = ARRAY_SIZE(sx150x_16_groups),
};

static const struct sx150x_device_data sx1502q_device_data = {
	.model = SX150X_123,
	.reg_pullup	= 0x02,
	.reg_pulldn	= 0x03,
	.reg_dir	= 0x01,
	.reg_data	= 0x00,
	.reg_irq_mask	= 0x05,
	.reg_irq_src	= 0x08,
	.reg_sense	= 0x07,
	.pri.x123 = {
		.reg_pld_mode	= 0x10,
		.reg_pld_table0	= 0x11,
		.reg_pld_table1	= 0x12,
		.reg_pld_table2	= 0x13,
		.reg_pld_table3	= 0x14,
		.reg_pld_table4	= 0x15,
		.reg_advance	= 0xad,
	},
	.ngpios	= 8,
	.pins = sx150x_8_pins,
	.npins = ARRAY_SIZE(sx150x_8_pins),
	.groups = sx150x_8_groups,
	.ngroups = ARRAY_SIZE(sx150x_8_groups),
};

static s32 sx150x_i2c_write(struct i2c_client *client, u8 reg, u8 val)
{
	s32 err = i2c_smbus_write_byte_data(client, reg, val);

	if (err < 0)
		dev_warn(&client->dev,
			"i2c write fail: can't write %02x to %02x: %d\n",
			val, reg, err);
	return err;
}

static s32 sx150x_i2c_read(struct i2c_client *client, u8 reg, u8 *val)
{
	s32 err = i2c_smbus_read_byte_data(client, reg);

	if (err >= 0)
		*val = err;
	else
		dev_warn(&client->dev,
			"i2c read fail: can't read from %02x: %d\n",
			reg, err);
	return err;
}

/*
 * These utility functions solve the common problem of locating and setting
 * configuration bits.  Configuration bits are grouped into registers
 * whose indexes increase downwards.  For example, with eight-bit registers,
 * sixteen gpios would have their config bits grouped in the following order:
 * REGISTER N-1 [ f e d c b a 9 8 ]
 *          N   [ 7 6 5 4 3 2 1 0 ]
 *
 * For multi-bit configurations, the pattern gets wider:
 * REGISTER N-3 [ f f e e d d c c ]
 *          N-2 [ b b a a 9 9 8 8 ]
 *          N-1 [ 7 7 6 6 5 5 4 4 ]
 *          N   [ 3 3 2 2 1 1 0 0 ]
 *
 * Given the address of the starting register 'N', the index of the gpio
 * whose configuration we seek to change, and the width in bits of that
 * configuration, these functions allow us to locate the correct
 * register and mask the correct bits.
 */
static inline void sx150x_find_cfg(u8 offset, u8 width,
				   u8 *reg, u8 *mask, u8 *shift)
{
	*reg   -= offset * width / 8;
	*mask   = (1 << width) - 1;
	*shift  = (offset * width) % 8;
	*mask <<= *shift;
}

static int sx150x_write_cfg(struct i2c_client *client,
			    u8 offset, u8 width, u8 reg, u8 val)
{
	u8  mask;
	u8  data;
	u8  shift;
	int err;

	sx150x_find_cfg(offset, width, &reg, &mask, &shift);
	err = sx150x_i2c_read(client, reg, &data);
	if (err < 0)
		return err;

	data &= ~mask;
	data |= (val << shift) & mask;
	return sx150x_i2c_write(client, reg, data);
}

static int sx150x_read_cfg(struct i2c_client *client,
			   u8 offset, u8 width, u8 reg)
{
	u8  mask;
	u8  data;
	u8  shift;
	int err;

	sx150x_find_cfg(offset, width, &reg, &mask, &shift);
	err = sx150x_i2c_read(client, reg, &data);
	if (err < 0)
		return err;

	return (data & mask);
}

static int sx150x_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct sx150x_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->data->ngroups;
}

static const char *sx150x_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						unsigned int group)
{
	struct sx150x_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->data->groups[group].name;
}

static int sx150x_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					unsigned int group,
					const unsigned int **pins,
					unsigned int *num_pins)
{
	struct sx150x_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*pins = &pctl->data->groups[group].pin;
	*num_pins = 1;

	return 0;
}

static const struct pinctrl_ops sx150x_pinctrl_ops = {
	.get_groups_count = sx150x_pinctrl_get_groups_count,
	.get_group_name = sx150x_pinctrl_get_group_name,
	.get_group_pins = sx150x_pinctrl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_free_map,
};

static bool sx150x_pin_is_oscio(struct sx150x_pinctrl *pctl, unsigned int pin)
{
	if (pin >= pctl->data->ngroups)
		return false;

	return !strcmp(pctl->data->groups[pin].name, "oscio");
}

static int sx150x_gpio_get_direction(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct sx150x_pinctrl *pctl = gpiochip_get_data(chip);
	int status;

	if (sx150x_pin_is_oscio(pctl, offset))
		return false;

	mutex_lock(&pctl->lock);

	status = sx150x_read_cfg(pctl->client, offset, 1, pctl->data->reg_dir);
	if (status >= 0)
		status = !!status;

	mutex_unlock(&pctl->lock);

	return status;
}

static int sx150x_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct sx150x_pinctrl *pctl = gpiochip_get_data(chip);
	int status;

	if (sx150x_pin_is_oscio(pctl, offset))
		return -EINVAL;

	mutex_lock(&pctl->lock);

	status = sx150x_read_cfg(pctl->client, offset, 1, pctl->data->reg_data);
	if (status >= 0)
		status = !!status;

	mutex_unlock(&pctl->lock);

	return status;
}

static void sx150x_gpio_set(struct gpio_chip *chip, unsigned int offset,
			       int value)
{
	struct sx150x_pinctrl *pctl = gpiochip_get_data(chip);

	if (sx150x_pin_is_oscio(pctl, offset)) {
		
		mutex_lock(&pctl->lock);
		sx150x_i2c_write(pctl->client,
				       pctl->data->pri.x789.reg_clock,
				       (value ? 0x1f : 0x10));
		mutex_unlock(&pctl->lock);
	} else {
		mutex_lock(&pctl->lock);
		sx150x_write_cfg(pctl->client, offset, 1,
				       pctl->data->reg_data,
				       (value ? 1 : 0));
		mutex_unlock(&pctl->lock);
	}
}

static int sx150x_gpio_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct sx150x_pinctrl *pctl = gpiochip_get_data(chip);
	int ret;

	if (sx150x_pin_is_oscio(pctl, offset))
		return -EINVAL;

	mutex_lock(&pctl->lock);
	ret = sx150x_write_cfg(pctl->client, offset, 1,
				pctl->data->reg_dir, 1);
	mutex_unlock(&pctl->lock);

	return ret;
}

static int sx150x_gpio_direction_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	struct sx150x_pinctrl *pctl = gpiochip_get_data(chip);
	int status;

	if (sx150x_pin_is_oscio(pctl, offset))
		return sx150x_gpio_set(chip, offset, value);

	mutex_lock(&pctl->lock);
	status = sx150x_write_cfg(pctl->client, offset, 1,
				  pctl->data->reg_data,
				  (value ? 1 : 0));
	if (status >= 0)
		status = sx150x_write_cfg(pctl->client, offset, 1,
					  pctl->data->reg_dir, 0);
	mutex_unlock(&pctl->lock);

	return status;
}

static int sx150x_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *config)
{
	struct sx150x_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	unsigned int param = pinconf_to_config_param(*config);
	int ret;
	u32 arg;

	if (sx150x_pin_is_oscio(pctl, pin)) {
		switch (param) {
		case PIN_CONFIG_OUTPUT:
			arg = pctl->oscio_is_gpo;
			break;
		default:
			return -ENOTSUPP;
		}
		
		goto out;
	}

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_DOWN:
		mutex_lock(&pctl->lock);
		ret = sx150x_read_cfg(pctl->client, pin, 1,
				      pctl->data->reg_pulldn);
		mutex_unlock(&pctl->lock);

		if (ret < 0)
			return ret;

		arg = ret ? 1 : 0;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		mutex_lock(&pctl->lock);
		ret = sx150x_read_cfg(pctl->client, pin, 1,
				      pctl->data->reg_pullup);
		mutex_unlock(&pctl->lock);

		if (ret < 0)
			return ret;

		arg = ret ? 1 : 0;
		break;

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (pctl->data->model != SX150X_789)
			return -ENOTSUPP;

		mutex_lock(&pctl->lock);
		ret = sx150x_read_cfg(pctl->client, pin, 1,
				      pctl->data->pri.x789.reg_drain);
		mutex_unlock(&pctl->lock);

		if (ret < 0)
			return ret;

		arg = ret ? 1 : 0;
		break;

	case PIN_CONFIG_DRIVE_PUSH_PULL:
		if (pctl->data->model != SX150X_789)
			arg = true;
		else {
			mutex_lock(&pctl->lock);
			ret = sx150x_read_cfg(pctl->client, pin, 1,
					      pctl->data->pri.x789.reg_drain);
			mutex_unlock(&pctl->lock);

			if (ret < 0)
				return ret;

			arg = ret ? 0 : 1;
		}
		break;

	case PIN_CONFIG_OUTPUT:
		mutex_lock(&pctl->lock);
		ret = sx150x_read_cfg(pctl->client, pin, 1,
				      pctl->data->reg_dir);
		mutex_unlock(&pctl->lock);

		if (ret < 0)
			return ret;

		arg = ret ? 0 : 1;
		break;

	default:
		return -ENOTSUPP;
	}

out:
	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int sx150x_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *configs, unsigned int num_configs)
{
	struct sx150x_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	u32 arg;
	int i;
	int ret;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		if (sx150x_pin_is_oscio(pctl, pin)) {
			if (param == PIN_CONFIG_OUTPUT && arg) {
				mutex_lock(&pctl->lock);
				ret = sx150x_i2c_write(pctl->client,
						pctl->data->pri.x789.reg_clock,
						0);
				mutex_unlock(&pctl->lock);
				if (ret < 0)
					return ret;

				continue;
			}
			else
				return -ENOTSUPP;
		}

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
		case PIN_CONFIG_BIAS_DISABLE:
			mutex_lock(&pctl->lock);
			ret = sx150x_write_cfg(pctl->client, pin, 1,
					       pctl->data->reg_pulldn, 0);
			mutex_unlock(&pctl->lock);
			if (ret < 0)
				return ret;

			mutex_lock(&pctl->lock);
			ret = sx150x_write_cfg(pctl->client, pin, 1,
					       pctl->data->reg_pullup, 0);
			mutex_unlock(&pctl->lock);
			if (ret < 0)
				return ret;

			break;
			
		case PIN_CONFIG_BIAS_PULL_UP:
			mutex_lock(&pctl->lock);
			ret = sx150x_write_cfg(pctl->client, pin, 1,
					       pctl->data->reg_pullup,
					       (arg ? 1 : 0));
			mutex_unlock(&pctl->lock);
			if (ret < 0)
				return ret;

			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			mutex_lock(&pctl->lock);
			ret = sx150x_write_cfg(pctl->client, pin, 1,
					       pctl->data->reg_pulldn,
					       (arg ? 1 : 0));
			mutex_unlock(&pctl->lock);
			if (ret < 0)
				return ret;

			break;

		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			if (pctl->data->model != SX150X_789)
				return -ENOTSUPP;

			mutex_lock(&pctl->lock);
			ret = sx150x_write_cfg(pctl->client, pin, 1,
					       pctl->data->pri.x789.reg_drain,
					       (arg ? 1 : 0));
			mutex_unlock(&pctl->lock);
			if (ret < 0)
				return ret;

			break;

		case PIN_CONFIG_DRIVE_PUSH_PULL:
			if (pctl->data->model != SX150X_789)
				return -ENOTSUPP;

			mutex_lock(&pctl->lock);
			ret = sx150x_write_cfg(pctl->client, pin, 1,
					       pctl->data->pri.x789.reg_drain,
					       (arg ? 0 : 1));
			mutex_unlock(&pctl->lock);
			if (ret < 0)
				return ret;

			break;

		case PIN_CONFIG_OUTPUT:
			mutex_lock(&pctl->lock);
			ret = sx150x_write_cfg(pctl->client, pin, 1,
					       pctl->data->reg_dir,
					       (arg ? 0 : 1));
			mutex_unlock(&pctl->lock);
			if (ret < 0)
				return ret;

			break;

		default:
			return -ENOTSUPP;
			break;
		}
	} /* for each config */

	return 0;
}

static const struct pinconf_ops sx150x_pinconf_ops = {
	.pin_config_get = sx150x_pinconf_get,
	.pin_config_set = sx150x_pinconf_set,
	.is_generic = true,
};

static const struct i2c_device_id sx150x_id[] = {
	{"sx1508q", (kernel_ulong_t) &sx1508q_device_data },
	{"sx1509q", (kernel_ulong_t) &sx1509q_device_data },
	{"sx1506q", (kernel_ulong_t) &sx1506q_device_data },
	{"sx1502q", (kernel_ulong_t) &sx1502q_device_data },
	{}
};
MODULE_DEVICE_TABLE(i2c, sx150x_id);

static const struct of_device_id sx150x_of_match[] = {
	{ .compatible = "semtech,sx1508q" },
	{ .compatible = "semtech,sx1509q" },
	{ .compatible = "semtech,sx1506q" },
	{ .compatible = "semtech,sx1502q" },
	{},
};
MODULE_DEVICE_TABLE(of, sx150x_of_match);


static int sx150x_init_io(struct sx150x_pinctrl *pctl, u8 base, u16 cfg)
{
	int err = 0;
	unsigned n;

	for (n = 0; err >= 0 && n < (pctl->data->ngpios / 8); ++n)
		err = sx150x_i2c_write(pctl->client, base - n, cfg >> (n * 8));
	return err;
}

static int sx150x_reset(struct sx150x_pinctrl *pctl)
{
	int err;

	err = i2c_smbus_write_byte_data(pctl->client,
					pctl->data->pri.x789.reg_reset,
					0x12);
	if (err < 0)
		return err;

	err = i2c_smbus_write_byte_data(pctl->client,
					pctl->data->pri.x789.reg_reset,
					0x34);
	return err;
}

static int sx150x_init_hw(struct sx150x_pinctrl *pctl)
{
	int err;

	if (of_property_read_bool(pctl->dev->of_node, "semtech,probe-reset")) {
		err = sx150x_reset(pctl);
		if (err < 0)
			return err;
	}

	if (pctl->data->model == SX150X_789)
		err = sx150x_i2c_write(pctl->client,
				pctl->data->pri.x789.reg_misc,
				0x01);
	else if (pctl->data->model == SX150X_456)
		err = sx150x_i2c_write(pctl->client,
				pctl->data->pri.x456.reg_advance,
				0x04);
	else
		err = sx150x_i2c_write(pctl->client,
				pctl->data->pri.x123.reg_advance,
				0x00);
	if (err < 0)
		return err;

	/* Set all pins to work in normal mode */
	if (pctl->data->model == SX150X_789) {
		err = sx150x_init_io(pctl,
				pctl->data->pri.x789.reg_polarity,
				0);
		if (err < 0)
			return err;
	} else if (pctl->data->model == SX150X_456) {
		/* Set all pins to work in normal mode */
		err = sx150x_init_io(pctl,
				pctl->data->pri.x456.reg_pld_mode,
				0);
		if (err < 0)
			return err;
	} else {
		/* Set all pins to work in normal mode */
		err = sx150x_init_io(pctl,
				pctl->data->pri.x123.reg_pld_mode,
				0);
		if (err < 0)
			return err;
	}

	return 0;
}

static int sx150x_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	static const u32 i2c_funcs = I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WRITE_WORD_DATA;
	struct device *dev = &client->dev;
	struct sx150x_pinctrl *pctl;
	int ret;

	if (!id->driver_data)
		return -EINVAL;

	if (!i2c_check_functionality(client->adapter, i2c_funcs))
		return -ENOSYS;

	pctl = devm_kzalloc(dev, sizeof(*pctl), GFP_KERNEL);
	if (!pctl)
		return -ENOMEM;

	pctl->dev = dev;
	pctl->client = client;
	pctl->data = (void*)id->driver_data;

	mutex_init(&pctl->lock);

	ret = sx150x_init_hw(pctl);
	if (ret)
		return ret;

	/* Pinctrl_desc */
	pctl->pinctrl_desc.name = "sx150x-pinctrl";
	pctl->pinctrl_desc.pctlops = &sx150x_pinctrl_ops;
	pctl->pinctrl_desc.confops = &sx150x_pinconf_ops;
	pctl->pinctrl_desc.pins = pctl->data->pins;
	pctl->pinctrl_desc.npins = pctl->data->npins;
	pctl->pinctrl_desc.owner = THIS_MODULE;

	pctl->pctldev = pinctrl_register(&pctl->pinctrl_desc, dev, pctl);
	if (IS_ERR(pctl->pctldev)) {
		dev_err(dev, "Failed to register pinctrl device\n");
		return PTR_ERR(pctl->pctldev);
	}

	/* Register GPIO controller */
	pctl->gpio.label = devm_kstrdup(dev, client->name, GFP_KERNEL);
	pctl->gpio.base = -1;
	pctl->gpio.ngpio = pctl->data->npins;
	pctl->gpio.get_direction = sx150x_gpio_get_direction;
	pctl->gpio.direction_input = sx150x_gpio_direction_input;
	pctl->gpio.direction_output = sx150x_gpio_direction_output;
	pctl->gpio.get = sx150x_gpio_get;
	pctl->gpio.set = sx150x_gpio_set;
	pctl->gpio.of_node = dev->of_node;
	pctl->gpio.can_sleep = true;

	return devm_gpiochip_add_data(dev, &pctl->gpio, pctl);
}

static struct i2c_driver sx150x_driver = {
	.driver = {
		.name = "sx150x-pinctrl",
		.of_match_table = of_match_ptr(sx150x_of_match),
	},
	.probe    = sx150x_probe,
	.id_table = sx150x_id,
};

static int __init sx150x_init(void)
{
	return i2c_add_driver(&sx150x_driver);
}
subsys_initcall(sx150x_init);
