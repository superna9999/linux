/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * S32 pinmux core definitions
 *
 * Copyright 2016-2020, 2022, 2026 NXP
 * Copyright (C) 2022 SUSE LLC
 * Copyright 2015-2016 Freescale Semiconductor, Inc.
 * Copyright (C) 2012 Linaro Ltd.
 */

#ifndef __DRIVERS_PINCTRL_S32_H
#define __DRIVERS_PINCTRL_S32_H

struct platform_device;

/**
 * struct s32_pin_group - describes an S32 pin group
 * @data: generic data describes group name, number of pins, and a pin array in
 *	this group.
 * @pin_sss: an array of source signal select configs paired with pin array.
 */
struct s32_pin_group {
	struct pingroup data;
	unsigned int *pin_sss;
};

/**
 * struct s32_pin_range - pin ID range for each memory region.
 * @start: start pin ID
 * @end: end pin ID
 */
struct s32_pin_range {
	unsigned int start;
	unsigned int end;
};

/**
 * struct s32_gpio_range - contiguous GPIO pin range within a SIUL2 module
 * @gpio_base: first GPIO line offset in the GPIO range
 * @pin_base: first pinctrl pin number mapped by this GPIO range
 * @gpio_num: number of consecutive GPIO pins in the range
 * @sparse: true if the PGPD layout is non-linear (resolved via pad map only);
 *          pins not found in the pad map are invalid for this range
 */
struct s32_gpio_range {
	unsigned int gpio_base;
	unsigned int pin_base;
	unsigned int gpio_num;
	bool sparse;
};

/**
 * struct s32_gpio_pad_map - mapping between GPIO ranges and PGPD pads
 * @gpio_start: first GPIO line offset in the range
 * @gpio_end: last GPIO line offset in the range
 * @pad: PGPD pad number serving the range
 */
struct s32_gpio_pad_map {
	unsigned int gpio_start;
	unsigned int gpio_end;
	unsigned int pad;
};

struct s32_pinctrl_soc_data {
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	const struct s32_pin_range *mem_pin_ranges;
	unsigned int mem_regions;
	const struct s32_gpio_range *gpio_ranges;
	unsigned int num_gpio_ranges;
	const struct s32_gpio_pad_map *gpio_pad_maps;
	unsigned int num_gpio_pad_maps;
};

struct s32_pinctrl_soc_info {
	struct device *dev;
	const struct s32_pinctrl_soc_data *soc_data;
	struct s32_pin_group *groups;
	unsigned int ngroups;
	struct pinfunction *functions;
	unsigned int nfunctions;
	unsigned int grp_index;
};

#define S32_PINCTRL_PIN(pin)	PINCTRL_PIN(pin, #pin)
#define S32_PIN_RANGE(_start, _end) { .start = _start, .end = _end }
#define S32_GPIO_RANGE(gpio, pin, num) \
	{ .gpio_base = gpio, .pin_base = pin, .gpio_num = num }

int s32_pinctrl_probe(struct platform_device *pdev,
		      const struct s32_pinctrl_soc_data *soc_data);
int s32_pinctrl_resume(struct device *dev);
int s32_pinctrl_suspend(struct device *dev);
#endif /* __DRIVERS_PINCTRL_S32_H */
