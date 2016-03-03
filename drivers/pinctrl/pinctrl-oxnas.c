/*
 * PLX Technology OXNAS Pinctrl driver based on at91 pinctrl driver
 *
 * Copyright (C) 2016 Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2013 Ma Hajun <mahaijuns@gmail.com>
 * Copyright (C) 2011 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
/* Since we request GPIOs from ourself */
#include <linux/pinctrl/consumer.h>
#include <linux/version.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "core.h"

#define MAX_NB_GPIO_PER_BANK	32
#define MAX_GPIO_BANKS		2

struct oxnas_gpio_chip {
	struct gpio_chip	chip;
	struct pinctrl_gpio_range range;
	void __iomem		*regbase;  /* GPIOA/B virtual address */
	struct irq_domain	*domain;   /* associated irq domain */
	struct regmap		*regmap;
};

#define to_oxnas_gpio_chip(c) container_of(c, struct oxnas_gpio_chip, chip)

static struct oxnas_gpio_chip *gpio_chips[MAX_GPIO_BANKS];

static int gpio_banks;

/**
 * struct oxnas_pmx_func - describes pinmux functions
 * @name: the name of this specific function
 * @groups: corresponding pin groups
 * @ngroups: the number of groups
 */
struct oxnas_pmx_func {
	const char	*name;
	const char	**groups;
	unsigned	ngroups;
};

enum oxnas_mux {
	OXNAS_PINMUX_GPIO,
	OXNAS_PINMUX_FUNC1,
	OXNAS_PINMUX_FUNC2,
	OXNAS_PINMUX_FUNC3,
};

enum {
	INPUT_VALUE = 0,
	OUTPUT_ENABLE = 4,
	IRQ_PENDING = 0xC,
	OUTPUT_VALUE = 0x10,
	OUTPUT_SET = 0x14,
	OUTPUT_CLEAR = 0x18,
	OUTPUT_EN_SET = 0x1C,
	OUTPUT_EN_CLEAR = 0x20,
	RE_IRQ_ENABLE = 0x28, /* rising edge */
	FE_IRQ_ENABLE = 0x2C, /* falling edge */
	RE_IRQ_PENDING = 0x30, /* rising edge */
	FE_IRQ_PENDING = 0x34, /* falling edge */
};

enum {
	PINMUX_PRIMARY_SEL0 = 0x0c,
	PINMUX_PRIMARY_SEL1 = 0x10,
	PINMUX_SECONDARY_SEL0 = 0x14,
	PINMUX_SECONDARY_SEL1 = 0x18,
	PINMUX_TERTIARY_SEL0 = 0x8c,
	PINMUX_TERTIARY_SEL1 = 0x90,
};

/**
 * struct oxnas_pmx_pin - describes an pin mux
 * @bank: the bank of the pin
 * @pin: the pin number in the @bank
 * @mux: the mux mode : gpio or periph_x of the pin i.e. alternate function.
 * @conf: the configuration of the pin: PULL_UP, MULTIDRIVE etc...
 */
struct oxnas_pmx_pin {
	uint32_t	bank;
	uint32_t	pin;
	enum oxnas_mux	mux;
	unsigned long	conf;
};

/**
 * struct oxnas_pin_group - describes an pin group
 * @name: the name of this specific pin group
 * @pins_conf: the mux mode for each pin in this group. The size of this
 *	array is the same as pins.
 * @pins: an array of discrete physical pins used in this group, taken
 *	from the driver-local pin enumeration space
 * @npins: the number of pins in this group array, i.e. the number of
 *	elements in .pins so we can iterate over that array
 */
struct oxnas_pin_group {
	const char		*name;
	struct oxnas_pmx_pin	*pins_conf;
	unsigned int		*pins;
	unsigned		npins;
};

struct oxnas_pinctrl {
	struct device		*dev;
	struct pinctrl_dev	*pctl;
	struct regmap		*regmap;

	int			nbanks;

	uint32_t		*mux_mask;
	int			nmux;

	struct oxnas_pmx_func	*functions;
	int			nfunctions;

	struct oxnas_pin_group	*groups;
	int			ngroups;
};

static const inline struct oxnas_pin_group *oxnas_pinctrl_find_group_by_name(
				const struct oxnas_pinctrl *info,
				const char *name)
{
	const struct oxnas_pin_group *grp = NULL;
	int i;

	for (i = 0; i < info->ngroups; i++) {
		if (strcmp(info->groups[i].name, name))
			continue;

		grp = &info->groups[i];
		dev_dbg(info->dev, "%s: %d 0:%d\n", name, grp->npins,
			grp->pins[0]);
		break;
	}

	return grp;
}

static int oxnas_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct oxnas_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->ngroups;
}

static const char *oxnas_get_group_name(struct pinctrl_dev *pctldev,
				       unsigned selector)
{
	struct oxnas_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->groups[selector].name;
}

static int oxnas_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector,
			       const unsigned **pins,
			       unsigned *npins)
{
	struct oxnas_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	if (selector >= info->ngroups)
		return -EINVAL;

	*pins = info->groups[selector].pins;
	*npins = info->groups[selector].npins;

	return 0;
}

static void oxnas_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
		   unsigned offset)
{
	seq_printf(s, "%s", dev_name(pctldev->dev));
}

static int oxnas_dt_node_to_map(struct pinctrl_dev *pctldev,
			struct device_node *np,
			struct pinctrl_map **map, unsigned *num_maps)
{
	struct oxnas_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	const struct oxnas_pin_group *grp;
	struct pinctrl_map *new_map;
	struct device_node *parent;
	int map_num = 1;
	int i;

	/*
	 * first find the group of this node and check if we need create
	 * config maps for pins
	 */
	grp = oxnas_pinctrl_find_group_by_name(info, np->name);
	if (!grp) {
		dev_err(info->dev, "unable to find group for node %s\n",
			np->name);
		return -EINVAL;
	}

	map_num += grp->npins;
	new_map = devm_kzalloc(pctldev->dev, sizeof(*new_map) * map_num,
			       GFP_KERNEL);
	if (!new_map)
		return -ENOMEM;

	*map = new_map;
	*num_maps = map_num;

	/* create mux map */
	parent = of_get_parent(np);
	if (!parent) {
		devm_kfree(pctldev->dev, new_map);
		return -EINVAL;
	}
	new_map[0].type = PIN_MAP_TYPE_MUX_GROUP;
	new_map[0].data.mux.function = parent->name;
	new_map[0].data.mux.group = np->name;
	of_node_put(parent);

	/* create config map */
	new_map++;
	for (i = 0; i < grp->npins; i++) {
		new_map[i].type = PIN_MAP_TYPE_CONFIGS_PIN;
		new_map[i].data.configs.group_or_pin =
				pin_get_name(pctldev, grp->pins[i]);
		new_map[i].data.configs.configs = &grp->pins_conf[i].conf;
		new_map[i].data.configs.num_configs = 1;
	}

	dev_dbg(pctldev->dev, "maps: function %s group %s num %d\n",
		(*map)->data.mux.function, (*map)->data.mux.group, map_num);

	return 0;
}

static void oxnas_dt_free_map(struct pinctrl_dev *pctldev,
				struct pinctrl_map *map, unsigned num_maps)
{
}

static const struct pinctrl_ops oxnas_pctrl_ops = {
	.get_groups_count	= oxnas_get_groups_count,
	.get_group_name		= oxnas_get_group_name,
	.get_group_pins		= oxnas_get_group_pins,
	.pin_dbg_show		= oxnas_pin_dbg_show,
	.dt_node_to_map		= oxnas_dt_node_to_map,
	.dt_free_map		= oxnas_dt_free_map,
};

static void __iomem *pin_to_gpioctrl(struct oxnas_pinctrl *info,
				 unsigned int bank)
{
	return gpio_chips[bank]->regbase;
}

static inline int pin_to_bank(unsigned pin)
{
	return pin / MAX_NB_GPIO_PER_BANK;
}

static unsigned pin_to_mask(unsigned int pin)
{
	return 1 << pin;
}

static void oxnas_mux_disable_interrupt(void __iomem *pio, unsigned mask)
{
	writel(readl(pio + RE_IRQ_ENABLE) & ~mask, pio + RE_IRQ_ENABLE);
	writel(readl(pio + FE_IRQ_ENABLE) & ~mask, pio + FE_IRQ_ENABLE);
}

static void oxnas_mux_set_func1(struct oxnas_pinctrl *ctrl,
				unsigned bank, unsigned mask)
{
	if (!bank) {
		regmap_write_bits(ctrl->regmap,
				PINMUX_PRIMARY_SEL0, mask, mask);
		regmap_write_bits(ctrl->regmap,
				PINMUX_SECONDARY_SEL0, mask, 0);
		regmap_write_bits(ctrl->regmap,
				PINMUX_TERTIARY_SEL1, mask, 0);
	} else {
		regmap_write_bits(ctrl->regmap,
				PINMUX_PRIMARY_SEL1, mask, mask);
		regmap_write_bits(ctrl->regmap,
				PINMUX_SECONDARY_SEL1, mask, 0);
		regmap_write_bits(ctrl->regmap,
				PINMUX_TERTIARY_SEL1, mask, 0);
	}
}

static void oxnas_mux_set_func2(struct oxnas_pinctrl *ctrl,
				unsigned bank, unsigned mask)
{
	if (!bank) {
		regmap_write_bits(ctrl->regmap,
				PINMUX_PRIMARY_SEL0, mask, 0);
		regmap_write_bits(ctrl->regmap,
				PINMUX_SECONDARY_SEL0, mask, mask);
		regmap_write_bits(ctrl->regmap,
				PINMUX_TERTIARY_SEL0, mask, 0);
	} else {
		regmap_write_bits(ctrl->regmap,
				PINMUX_PRIMARY_SEL1, mask, 0);
		regmap_write_bits(ctrl->regmap,
				PINMUX_SECONDARY_SEL1, mask, mask);
		regmap_write_bits(ctrl->regmap,
				PINMUX_TERTIARY_SEL1, mask, 0);
	}
}

static void oxnas_mux_set_func3(struct oxnas_pinctrl *ctrl,
				unsigned bank, unsigned mask)
{
	if (!bank) {
		regmap_write_bits(ctrl->regmap,
				PINMUX_PRIMARY_SEL0, mask, 0);
		regmap_write_bits(ctrl->regmap,
				PINMUX_SECONDARY_SEL0, mask, 0);
		regmap_write_bits(ctrl->regmap,
				PINMUX_TERTIARY_SEL0, mask, mask);
	} else {
		regmap_write_bits(ctrl->regmap,
				PINMUX_PRIMARY_SEL1, mask, 0);
		regmap_write_bits(ctrl->regmap,
				PINMUX_SECONDARY_SEL1, mask, 0);
		regmap_write_bits(ctrl->regmap,
				PINMUX_TERTIARY_SEL1, mask, mask);
	}
}

static void oxnas_mux_set_gpio(struct oxnas_pinctrl *ctrl,
			       unsigned bank, unsigned mask)
{
	if (!bank) {
		regmap_write_bits(ctrl->regmap,
				PINMUX_PRIMARY_SEL0, mask, 0);
		regmap_write_bits(ctrl->regmap,
				PINMUX_SECONDARY_SEL0, mask, 0);
		regmap_write_bits(ctrl->regmap,
				PINMUX_TERTIARY_SEL0, mask, 0);
	} else {
		regmap_write_bits(ctrl->regmap,
				PINMUX_PRIMARY_SEL1, mask, 0);
		regmap_write_bits(ctrl->regmap,
				PINMUX_SECONDARY_SEL1, mask, 0);
		regmap_write_bits(ctrl->regmap,
				PINMUX_TERTIARY_SEL1, mask, 0);
	}
}

static enum oxnas_mux oxnas_mux_get_func(struct regmap *regmap,
					 unsigned bank, unsigned mask)
{
	unsigned int val;

	if (!bank) {
		if (!regmap_read(regmap, PINMUX_PRIMARY_SEL0, &val) &&
		    (val & mask))
			return OXNAS_PINMUX_FUNC1;
		if (!regmap_read(regmap, PINMUX_SECONDARY_SEL0, &val) &&
		    (val & mask))
			return OXNAS_PINMUX_FUNC2;
		if (!regmap_read(regmap, PINMUX_TERTIARY_SEL0, &val) &&
		    (val & mask))
			return OXNAS_PINMUX_FUNC3;
	} else {
		if (!regmap_read(regmap, PINMUX_PRIMARY_SEL1, &val) &&
		    (val & mask))
			return OXNAS_PINMUX_FUNC1;
		if (!regmap_read(regmap, PINMUX_SECONDARY_SEL1, &val) &&
		    (val & mask))
			return OXNAS_PINMUX_FUNC2;
		if (!regmap_read(regmap, PINMUX_TERTIARY_SEL1, &val) &&
		    (val & mask))
			return OXNAS_PINMUX_FUNC3;
	}

	return OXNAS_PINMUX_GPIO;
}

static void oxnas_pin_dbg(const struct device *dev,
			  const struct oxnas_pmx_pin *pin)
{
	if (pin->mux) {
		dev_dbg(dev,
			"MF_%c%d configured as periph%c with conf = %lu\n",
			pin->bank + 'A', pin->pin, pin->mux - 1 + 'A',
			pin->conf);
	} else {
		dev_dbg(dev, "MF_%c%d configured as gpio with conf = %lu\n",
			pin->bank + 'A', pin->pin, pin->conf);
	}
}

static int pin_check_config(struct oxnas_pinctrl *info, const char *name,
			    int index, const struct oxnas_pmx_pin *pin)
{
	int mux;

	/* check if it's a valid config */
	if (pin->bank >= info->nbanks) {
		dev_err(info->dev, "%s: pin conf %d bank_id %d >= nbanks %d\n",
			name, index, pin->bank, info->nbanks);
		return -EINVAL;
	}

	if (pin->pin >= MAX_NB_GPIO_PER_BANK) {
		dev_err(info->dev, "%s: pin conf %d pin_bank_id %d >= %d\n",
			name, index, pin->pin, MAX_NB_GPIO_PER_BANK);
		return -EINVAL;
	}
	/* gpio always allowed */
	if (!pin->mux)
		return 0;

	mux = pin->mux - 1;

	if (mux >= info->nmux) {
		dev_err(info->dev, "%s: pin conf %d mux_id %d >= nmux %d\n",
			name, index, mux, info->nmux);
		return -EINVAL;
	}

	if (!(info->mux_mask[pin->bank * info->nmux + mux] & 1 << pin->pin)) {
		dev_err(info->dev, "%s: pin conf %d mux_id %d not supported for MF_%c%d\n",
			name, index, mux, pin->bank + 'A', pin->pin);
		return -EINVAL;
	}

	return 0;
}

static void oxnas_mux_gpio_enable(struct oxnas_pinctrl *ctrl,
				  int bank,
				  void __iomem *pio,
				  unsigned mask, bool input)
{
	oxnas_mux_set_gpio(ctrl, bank, mask);

	if (input)
		writel_relaxed(mask, pio + OUTPUT_EN_CLEAR);
	else
		writel_relaxed(mask, pio + OUTPUT_EN_SET);
}

static void oxnas_mux_gpio_disable(struct oxnas_pinctrl *ctrl,
				   int bank,
				   unsigned mask)
{
	/* when switch to other function, gpio is disabled automatically */
}

static int oxnas_pmx_set_mux(struct pinctrl_dev *pctldev, unsigned selector,
			    unsigned group)
{
	struct oxnas_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	const struct oxnas_pmx_pin *pins_conf = info->groups[group].pins_conf;
	const struct oxnas_pmx_pin *pin;
	uint32_t npins = info->groups[group].npins;
	int i, ret;
	unsigned mask;
	void __iomem *pio;

	dev_dbg(info->dev, "enable function %s group %s\n",
		info->functions[selector].name, info->groups[group].name);

	/* first check that all the pins of the group are valid with a valid
	 * parameter
	 */
	for (i = 0; i < npins; i++) {
		pin = &pins_conf[i];
		ret = pin_check_config(info, info->groups[group].name, i, pin);
		if (ret)
			return ret;
	}

	for (i = 0; i < npins; i++) {
		pin = &pins_conf[i];
		oxnas_pin_dbg(info->dev, pin);

		pio = pin_to_gpioctrl(info, pin->bank);

		mask = pin_to_mask(pin->pin);
		oxnas_mux_disable_interrupt(pio, mask);

		switch (pin->mux) {
		case OXNAS_PINMUX_GPIO:
			oxnas_mux_gpio_enable(info, pin->bank, pio, mask, 1);
			break;
		case OXNAS_PINMUX_FUNC1:
			oxnas_mux_set_func1(info, pin->bank, mask);
			break;
		case OXNAS_PINMUX_FUNC2:
			oxnas_mux_set_func2(info, pin->bank, mask);
			break;
		case OXNAS_PINMUX_FUNC3:
			oxnas_mux_set_func3(info, pin->bank, mask);
			break;
		}
		if (pin->mux)
			oxnas_mux_gpio_disable(info, pin->bank, mask);
	}

	return 0;
}

static int oxnas_pmx_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct oxnas_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->nfunctions;
}

static const char *oxnas_pmx_get_func_name(struct pinctrl_dev *pctldev,
					   unsigned selector)
{
	struct oxnas_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->functions[selector].name;
}

static int oxnas_pmx_get_groups(struct pinctrl_dev *pctldev, unsigned selector,
				const char * const **groups,
				unsigned * const num_groups)
{
	struct oxnas_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	*groups = info->functions[selector].groups;
	*num_groups = info->functions[selector].ngroups;

	return 0;
}

static int oxnas_gpio_request_enable(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned offset)
{
	struct oxnas_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);
	struct oxnas_gpio_chip *oxnas_chip;
	struct gpio_chip *chip;
	unsigned mask;

	if (!range) {
		dev_err(npct->dev, "invalid range\n");
		return -EINVAL;
	}
	if (!range->gc) {
		dev_err(npct->dev, "missing GPIO chip in range\n");
		return -EINVAL;
	}
	chip = range->gc;
	oxnas_chip = container_of(chip, struct oxnas_gpio_chip, chip);

	dev_dbg(npct->dev, "enable pin %u as GPIO\n", offset);

	mask = 1 << (offset - chip->base);

	dev_dbg(npct->dev, "enable pin %u as MF_%c%d 0x%x\n",
		offset, 'A' + range->id, offset - chip->base, mask);

	oxnas_mux_set_gpio(npct, range->id, mask);

	return 0;
}

static void oxnas_gpio_disable_free(struct pinctrl_dev *pctldev,
				    struct pinctrl_gpio_range *range,
				    unsigned offset)
{
	struct oxnas_pinctrl *npct = pinctrl_dev_get_drvdata(pctldev);

	dev_dbg(npct->dev, "disable pin %u as GPIO\n", offset);

	/* Set the pin to some default state, GPIO is usually default */
}

static const struct pinmux_ops oxnas_pmx_ops = {
	.get_functions_count	= oxnas_pmx_get_funcs_count,
	.get_function_name	= oxnas_pmx_get_func_name,
	.get_function_groups	= oxnas_pmx_get_groups,
	.set_mux		= oxnas_pmx_set_mux,
	.gpio_request_enable	= oxnas_gpio_request_enable,
	.gpio_disable_free	= oxnas_gpio_disable_free,
};

static int oxnas_pinconf_get(struct pinctrl_dev *pctldev,
			     unsigned pin_id, unsigned long *config)
{
	/* Nothing yet */

	return 0;
}

static int oxnas_pinconf_set(struct pinctrl_dev *pctldev,
			     unsigned pin_id, unsigned long *configs,
			     unsigned num_configs)
{
	/* Nothing yet */

	return 0;
}

static void oxnas_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				   struct seq_file *s, unsigned pin_id)
{

}

static void oxnas_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
					 struct seq_file *s, unsigned group)
{
}

static const struct pinconf_ops oxnas_pinconf_ops = {
	.pin_config_get			= oxnas_pinconf_get,
	.pin_config_set			= oxnas_pinconf_set,
	.pin_config_dbg_show		= oxnas_pinconf_dbg_show,
	.pin_config_group_dbg_show	= oxnas_pinconf_group_dbg_show,
};

static struct pinctrl_desc oxnas_pinctrl_desc = {
	.pctlops	= &oxnas_pctrl_ops,
	.pmxops		= &oxnas_pmx_ops,
	.confops	= &oxnas_pinconf_ops,
	.owner		= THIS_MODULE,
};

static const char *gpio_compat = "oxsemi,ox810se-gpio";

static void oxnas_pinctrl_child_count(struct oxnas_pinctrl *info,
				      struct device_node *np)
{
	struct device_node *child;

	for_each_child_of_node(np, child) {
		if (of_device_is_compatible(child, gpio_compat)) {
			info->nbanks++;
		} else {
			info->nfunctions++;
			info->ngroups += of_get_child_count(child);
		}
	}
}

static int oxnas_pinctrl_mux_mask(struct oxnas_pinctrl *info,
				  struct device_node *np)
{
	int ret = 0;
	int size;
	const __be32 *list;

	list = of_get_property(np, "plxtech,mux-mask", &size);
	if (!list) {
		dev_err(info->dev, "can not read the mux-mask of %d\n", size);
		return -EINVAL;
	}

	size /= sizeof(*list);
	if (!size || size % info->nbanks) {
		dev_err(info->dev, "wrong mux mask array should be by %d\n",
			info->nbanks);
		return -EINVAL;
	}
	info->nmux = size / info->nbanks;

	info->mux_mask = devm_kzalloc(info->dev, sizeof(u32) * size,
				      GFP_KERNEL);
	if (!info->mux_mask)
		return -ENOMEM;

	ret = of_property_read_u32_array(np, "plxtech,mux-mask",
					  info->mux_mask, size);
	if (ret)
		dev_err(info->dev, "can not read the mux-mask of %d\n", size);
	return ret;
}

static int oxnas_pinctrl_parse_groups(struct device_node *np,
				      struct oxnas_pin_group *grp,
				      struct oxnas_pinctrl *info, u32 index)
{
	struct oxnas_pmx_pin *pin;
	int size;
	const __be32 *list;
	int i, j;

	dev_dbg(info->dev, "group(%d): %s\n", index, np->name);

	/* Initialise group */
	grp->name = np->name;

	/*
	 * the binding format is plxtech,pins = <bank pin mux CONFIG ...>,
	 * do sanity check and calculate pins number
	 */
	list = of_get_property(np, "plxtech,pins", &size);
	/* we do not check return since it's safe node passed down */
	size /= sizeof(*list);
	if (!size || size % 4) {
		dev_err(info->dev, "wrong pins number or pins and configs should be divisible by 4\n");
		return -EINVAL;
	}

	grp->npins = size / 4;
	pin = grp->pins_conf = devm_kzalloc(info->dev,
				grp->npins * sizeof(struct oxnas_pmx_pin),
				GFP_KERNEL);
	grp->pins = devm_kzalloc(info->dev, grp->npins * sizeof(unsigned int),
				GFP_KERNEL);
	if (!grp->pins_conf || !grp->pins)
		return -ENOMEM;

	for (i = 0, j = 0; i < size; i += 4, j++) {
		pin->bank = be32_to_cpu(*list++);
		pin->pin = be32_to_cpu(*list++);
		grp->pins[j] = pin->bank * MAX_NB_GPIO_PER_BANK + pin->pin;
		pin->mux = be32_to_cpu(*list++);
		pin->conf = be32_to_cpu(*list++);

		oxnas_pin_dbg(info->dev, pin);
		pin++;
	}

	return 0;
}

static int oxnas_pinctrl_parse_functions(struct device_node *np,
					struct oxnas_pinctrl *info, u32 index)
{
	struct device_node *child;
	struct oxnas_pmx_func *func;
	struct oxnas_pin_group *grp;
	int ret;
	static u32 grp_index;
	u32 i = 0;

	dev_dbg(info->dev, "parse function(%d): %s\n", index, np->name);

	func = &info->functions[index];

	/* Initialise function */
	func->name = np->name;
	func->ngroups = of_get_child_count(np);
	if (func->ngroups <= 0) {
		dev_err(info->dev, "no groups defined\n");
		return -EINVAL;
	}
	func->groups = devm_kzalloc(info->dev,
			func->ngroups * sizeof(char *), GFP_KERNEL);
	if (!func->groups)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		func->groups[i] = child->name;
		grp = &info->groups[grp_index++];
		ret = oxnas_pinctrl_parse_groups(child, grp, info, i++);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct of_device_id oxnas_pinctrl_of_match[] = {
	{ .compatible = "oxsemi,ox810se-pinctrl"},
	{ /* sentinel */ }
};

static int oxnas_pinctrl_probe_dt(struct platform_device *pdev,
				 struct oxnas_pinctrl *info)
{
	int ret = 0;
	int i, j;
	uint32_t *tmp;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;

	if (!np)
		return -ENODEV;

	info->dev = &pdev->dev;

	oxnas_pinctrl_child_count(info, np);

	if (info->nbanks < 1) {
		dev_err(&pdev->dev, "you need to specify atleast one gpio-controller\n");
		return -EINVAL;
	}

	ret = oxnas_pinctrl_mux_mask(info, np);
	if (ret)
		return ret;

	dev_dbg(&pdev->dev, "nmux = %d\n", info->nmux);

	dev_dbg(&pdev->dev, "mux-mask\n");
	tmp = info->mux_mask;
	for (i = 0; i < info->nbanks; i++)
		for (j = 0; j < info->nmux; j++, tmp++)
			dev_dbg(&pdev->dev, "%d:%d\t0x%x\n", i, j, tmp[0]);

	dev_dbg(&pdev->dev, "nfunctions = %d\n", info->nfunctions);
	dev_dbg(&pdev->dev, "ngroups = %d\n", info->ngroups);
	info->functions = devm_kzalloc(&pdev->dev, info->nfunctions *
						sizeof(struct oxnas_pmx_func),
					GFP_KERNEL);
	if (!info->functions)
		return -ENOMEM;

	info->groups = devm_kzalloc(&pdev->dev, info->ngroups *
					sizeof(struct oxnas_pin_group),
				    GFP_KERNEL);
	if (!info->groups)
		return -ENOMEM;

	dev_dbg(&pdev->dev, "nbanks = %d\n", info->nbanks);
	dev_dbg(&pdev->dev, "nfunctions = %d\n", info->nfunctions);
	dev_dbg(&pdev->dev, "ngroups = %d\n", info->ngroups);

	i = 0;

	for_each_child_of_node(np, child) {
		if (of_device_is_compatible(child, gpio_compat))
			continue;
		ret = oxnas_pinctrl_parse_functions(child, info, i++);
		if (ret) {
			dev_err(&pdev->dev, "failed to parse function\n");
			return ret;
		}
	}

	return 0;
}

static int oxnas_pinctrl_probe(struct platform_device *pdev)
{
	struct oxnas_pinctrl *info;
	struct pinctrl_pin_desc *pdesc;
	int ret, i, j, k;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						       "plxtech,sys-ctrl");
	if (IS_ERR(info->regmap)) {
		dev_err(&pdev->dev, "failed to get sys ctrl regmap\n");
		return -ENODEV;
	}

	ret = oxnas_pinctrl_probe_dt(pdev, info);
	if (ret)
		return ret;

	/*
	 * We need all the GPIO drivers to probe FIRST, or we will not be able
	 * to obtain references to the struct gpio_chip * for them, and we
	 * need this to proceed.
	 */
	for (i = 0; i < info->nbanks; i++) {
		if (!gpio_chips[i]) {
			dev_warn(&pdev->dev,
				 "GPIO chip %d not registered yet\n", i);
			devm_kfree(&pdev->dev, info);
			return -EPROBE_DEFER;
		}
	}

	oxnas_pinctrl_desc.name = dev_name(&pdev->dev);
	oxnas_pinctrl_desc.npins = info->nbanks * MAX_NB_GPIO_PER_BANK;
	oxnas_pinctrl_desc.pins = pdesc =
		devm_kzalloc(&pdev->dev, sizeof(*pdesc) *
				oxnas_pinctrl_desc.npins, GFP_KERNEL);

	if (!oxnas_pinctrl_desc.pins)
		return -ENOMEM;

	for (i = 0, k = 0; i < info->nbanks; i++) {
		for (j = 0; j < MAX_NB_GPIO_PER_BANK; j++, k++) {
			pdesc->number = k;
			pdesc->name = kasprintf(GFP_KERNEL, "MF_%c%d", i + 'A',
						j);
			pdesc++;
		}
	}

	platform_set_drvdata(pdev, info);
	info->pctl = pinctrl_register(&oxnas_pinctrl_desc, &pdev->dev, info);

	if (!info->pctl) {
		dev_err(&pdev->dev, "could not register OXNAS pinctrl driver\n");
		ret = -EINVAL;
		goto err;
	}

	/* We will handle a range of GPIO pins */
	for (i = 0; i < info->nbanks; i++)
		pinctrl_add_gpio_range(info->pctl, &gpio_chips[i]->range);

	dev_info(&pdev->dev, "initialized OXNAS pinctrl driver\n");

	return 0;

err:
	return ret;
}

static int oxnas_pinctrl_remove(struct platform_device *pdev)
{
	struct oxnas_pinctrl *info = platform_get_drvdata(pdev);

	pinctrl_unregister(info->pctl);

	return 0;
}

static int oxnas_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	/*
	 * Map back to global GPIO space and request muxing, the direction
	 * parameter does not matter for this controller.
	 */
	int gpio = chip->base + offset;
	int bank = chip->base / chip->ngpio;

	dev_dbg(chip->parent, "%s:%d MF_%c%d(%d)\n", __func__, __LINE__,
		'A' + bank, offset, gpio);

	return pinctrl_request_gpio(gpio);
}

static void oxnas_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	int gpio = chip->base + offset;

	pinctrl_free_gpio(gpio);
}

static int oxnas_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct oxnas_gpio_chip *oxnas_gpio = to_oxnas_gpio_chip(chip);
	void __iomem *pio = oxnas_gpio->regbase;

	writel_relaxed(BIT(offset), pio + OUTPUT_EN_CLEAR);
	return 0;
}

static int oxnas_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct oxnas_gpio_chip *oxnas_gpio = to_oxnas_gpio_chip(chip);
	void __iomem *pio = oxnas_gpio->regbase;
	unsigned mask = 1 << offset;
	u32 pdsr = 0;

	pdsr = readl_relaxed(pio + INPUT_VALUE);
	return (pdsr & mask) != 0;
}

static void oxnas_gpio_set(struct gpio_chip *chip, unsigned offset,
				int val)
{
	struct oxnas_gpio_chip *oxnas_gpio = to_oxnas_gpio_chip(chip);
	void __iomem *pio = oxnas_gpio->regbase;

	if (val)
		writel_relaxed(BIT(offset), pio + OUTPUT_SET);
	else
		writel_relaxed(BIT(offset), pio + OUTPUT_CLEAR);
}

static int oxnas_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
				int val)
{
	struct oxnas_gpio_chip *oxnas_gpio = to_oxnas_gpio_chip(chip);
	void __iomem *pio = oxnas_gpio->regbase;

	if (val)
		writel_relaxed(BIT(offset), pio + OUTPUT_SET);
	else
		writel_relaxed(BIT(offset), pio + OUTPUT_CLEAR);

	writel_relaxed(BIT(offset), pio + OUTPUT_EN_SET);

	return 0;
}

static int oxnas_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct oxnas_gpio_chip *oxnas_gpio = to_oxnas_gpio_chip(chip);
	int virq;

	if (offset < chip->ngpio)
		virq = irq_create_mapping(oxnas_gpio->domain, offset);
	else
		virq = -ENXIO;

	dev_dbg(chip->parent, "%s: request IRQ for GPIO %d, return %d\n",
				chip->label, offset + chip->base, virq);
	return virq;
}

#ifdef CONFIG_DEBUG_FS
static void oxnas_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	int i;
	struct oxnas_gpio_chip *oxnas_gpio = to_oxnas_gpio_chip(chip);
	void __iomem *pio = oxnas_gpio->regbase;
	enum oxnas_mux mux;

	for (i = 0; i < chip->ngpio; i++) {
		unsigned pin = chip->base + i;
		unsigned mask = pin_to_mask(pin);
		unsigned bank = pin_to_bank(pin);
		const char *gpio_label;
		u32 pdsr;

		gpio_label = gpiochip_is_requested(chip, i);
		if (gpio_label) {
			seq_printf(s, "[%s]\tGPIO%s%d: ",
				   gpio_label, chip->label, i);
			pdsr = readl_relaxed(pio + INPUT_VALUE);

			seq_printf(s, "[gpio] %s\n",
					pdsr & mask ?
					"set" : "clear");
		} else {
			mux = oxnas_mux_get_func(oxnas_gpio->regmap,
						 bank,
						 mask);
			seq_printf(s, "\tGPIO%s%d: [func%d]\n",
				   chip->label, i, mux);
		}

	}
}
#else
#define oxnas_gpio_dbg_show	NULL
#endif

/* Several AIC controller irqs are dispatched through this GPIO handler.
 * To use any AT91_PIN_* as an externally triggered IRQ, first call
 * oxnas_set_gpio_input() then maybe enable its glitch filter.
 * Then just request_irq() with the pin ID; it works like any ARM IRQ
 * handler.
 */

static void gpio_irq_mask(struct irq_data *d)
{
	struct oxnas_gpio_chip *oxnas_gpio = irq_data_get_irq_chip_data(d);
	void __iomem	*pio = oxnas_gpio->regbase;
	unsigned	mask = 1 << d->hwirq;
	unsigned	type = irqd_get_trigger_type(d);

	if (type & IRQ_TYPE_EDGE_RISING)
		writel(readl(pio + RE_IRQ_ENABLE) & ~mask, pio + RE_IRQ_ENABLE);

	if (type & IRQ_TYPE_EDGE_FALLING)
		writel(readl(pio + FE_IRQ_ENABLE) & ~mask, pio + FE_IRQ_ENABLE);
}

static void gpio_irq_unmask(struct irq_data *d)
{
	struct oxnas_gpio_chip *oxnas_gpio = irq_data_get_irq_chip_data(d);
	void __iomem	*pio = oxnas_gpio->regbase;
	unsigned	mask = 1 << d->hwirq;
	unsigned	type = irqd_get_trigger_type(d);

	if (type & IRQ_TYPE_EDGE_RISING)
		writel(readl(pio + RE_IRQ_ENABLE) | mask, pio + RE_IRQ_ENABLE);

	if (type & IRQ_TYPE_EDGE_FALLING)
		writel(readl(pio + FE_IRQ_ENABLE) | mask, pio + FE_IRQ_ENABLE);
}


static int gpio_irq_type(struct irq_data *d, unsigned type)
{
	if ((type & IRQ_TYPE_EDGE_BOTH) == 0) {
		pr_warn("oxnas: Unsupported type for irq %d\n",
			gpio_to_irq(d->irq));
		return -EINVAL;
	}
	/* seems no way to set trigger type without enable irq,
	 * so leave it to unmask time
	 */

	return 0;
}

static struct irq_chip gpio_irqchip = {
	.name		= "GPIO",
	.irq_disable	= gpio_irq_mask,
	.irq_mask	= gpio_irq_mask,
	.irq_unmask	= gpio_irq_unmask,
	.irq_set_type	= gpio_irq_type,
};

static void gpio_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irq_data *idata = irq_desc_get_irq_data(desc);
	struct oxnas_gpio_chip *oxnas_gpio = irq_data_get_irq_chip_data(idata);
	void __iomem *pio = oxnas_gpio->regbase;
	unsigned long isr;
	int n;

	chained_irq_enter(chip, desc);
	for (;;) {
		isr = readl_relaxed(pio + IRQ_PENDING);
		if (!isr)
			break;

		/* acks pending interrupts */
		writel_relaxed(isr, pio + IRQ_PENDING);

		for_each_set_bit(n, &isr, BITS_PER_LONG) {
			generic_handle_irq(irq_find_mapping(oxnas_gpio->domain,
							    n));
		}
	}
	chained_irq_exit(chip, desc);
	/* now it may re-trigger */
}

/*
 * This lock class tells lockdep that GPIO irqs are in a different
 * category than their parents, so it won't report false recursion.
 */
static struct lock_class_key gpio_lock_class;

static int oxnas_gpio_irq_map(struct irq_domain *h, unsigned int virq,
			      irq_hw_number_t hw)
{
	struct oxnas_gpio_chip *oxnas_gpio = h->host_data;

	irq_set_lockdep_class(virq, &gpio_lock_class);

	irq_set_chip_and_handler(virq, &gpio_irqchip, handle_edge_irq);

	irq_set_chip_data(virq, oxnas_gpio);

	return 0;
}

static int oxnas_gpio_irq_domain_xlate(struct irq_domain *d,
				       struct device_node *ctrlr,
				       const u32 *intspec,
				       unsigned int intsize,
				       irq_hw_number_t *out_hwirq,
				       unsigned int *out_type)
{
	struct oxnas_gpio_chip *oxnas_gpio = d->host_data;
	int ret;
	int pin = oxnas_gpio->chip.base + intspec[0];

	if (WARN_ON(intsize < 2))
		return -EINVAL;
	*out_hwirq = intspec[0];
	*out_type = intspec[1] & IRQ_TYPE_SENSE_MASK;

	ret = gpio_request(pin, ctrlr->full_name);
	if (ret)
		return ret;

	ret = gpio_direction_input(pin);
	if (ret)
		return ret;

	return 0;
}

static struct irq_domain_ops oxnas_gpio_ops = {
	.map	= oxnas_gpio_irq_map,
	.xlate	= oxnas_gpio_irq_domain_xlate,
};

static int oxnas_gpio_of_irq_setup(struct device_node *node,
				   struct oxnas_gpio_chip *oxnas_gpio,
				   unsigned int irq)
{
	/* Disable irqs of this controller */
	writel_relaxed(0, oxnas_gpio->regbase + RE_IRQ_ENABLE);
	writel_relaxed(0, oxnas_gpio->regbase + FE_IRQ_ENABLE);

	/* Setup irq domain */
	oxnas_gpio->domain = irq_domain_add_linear(node, oxnas_gpio->chip.ngpio,
						   &oxnas_gpio_ops, oxnas_gpio);
	if (!oxnas_gpio->domain)
		panic("oxnas_gpio: couldn't allocate irq domain (DT).\n");

	irq_set_chip_data(irq, oxnas_gpio);
	irq_set_chained_handler(irq, gpio_irq_handler);

	return 0;
}

/* This structure is replicated for each GPIO block allocated at probe time */
static struct gpio_chip oxnas_gpio_template = {
	.request		= oxnas_gpio_request,
	.free			= oxnas_gpio_free,
	.direction_input	= oxnas_gpio_direction_input,
	.get			= oxnas_gpio_get,
	.direction_output	= oxnas_gpio_direction_output,
	.set			= oxnas_gpio_set,
	.to_irq			= oxnas_gpio_to_irq,
	.dbg_show		= oxnas_gpio_dbg_show,
	.can_sleep		= 0,
	.ngpio			= MAX_NB_GPIO_PER_BANK,
};

static const struct of_device_id oxnas_gpio_of_match[] = {
	{ .compatible = "oxsemi,ox810se-gpio"},
	{ /* sentinel */ }
};

static int oxnas_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct oxnas_gpio_chip *oxnas_chip = NULL;
	struct gpio_chip *chip;
	struct pinctrl_gpio_range *range;
	struct device_node *node = pdev->dev.of_node;
	int ret = 0;
	int irq, i;
	int alias_idx = of_alias_get_id(np, "gpio");
	uint32_t ngpio;
	char **names;

	if (WARN_ON(alias_idx >= ARRAY_SIZE(gpio_chips)))
		return -EINVAL;

	if (gpio_chips[alias_idx]) {
		ret = -EBUSY;
		goto err;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err;
	}

	oxnas_chip = devm_kzalloc(&pdev->dev, sizeof(*oxnas_chip), GFP_KERNEL);
	if (!oxnas_chip) {
		ret = -ENOMEM;
		goto err;
	}

	/* Get pinctrl sys control regmap */
	oxnas_chip->regmap =
		syscon_regmap_lookup_by_phandle(of_get_parent(node),
						"plxtech,sys-ctrl");
	if (IS_ERR(oxnas_chip->regmap)) {
		dev_err(&pdev->dev, "failed to get sys ctrl regmap\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	oxnas_chip->regbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(oxnas_chip->regbase)) {
		ret = PTR_ERR(oxnas_chip->regbase);
		goto err;
	}

	oxnas_chip->chip = oxnas_gpio_template;

	chip = &oxnas_chip->chip;
	chip->of_node = np;
	chip->label = dev_name(&pdev->dev);
	chip->parent = &pdev->dev;
	chip->owner = THIS_MODULE;
	chip->base = alias_idx * MAX_NB_GPIO_PER_BANK;

	if (!of_property_read_u32(np, "#gpio-lines", &ngpio)) {
		if (ngpio > MAX_NB_GPIO_PER_BANK)
			pr_err("oxnas_gpio.%d, gpio-nb >= %d failback to %d\n",
			       alias_idx, MAX_NB_GPIO_PER_BANK,
			       MAX_NB_GPIO_PER_BANK);
		else
			chip->ngpio = ngpio;
	}

	names = devm_kzalloc(&pdev->dev, sizeof(char *) * chip->ngpio,
			     GFP_KERNEL);

	if (!names) {
		ret = -ENOMEM;
		goto err;
	}

	for (i = 0; i < chip->ngpio; i++)
		names[i] = kasprintf(GFP_KERNEL, "MF_%c%d", alias_idx + 'A', i);

	chip->names = (const char *const *)names;

	range = &oxnas_chip->range;
	range->name = chip->label;
	range->id = alias_idx;
	range->pin_base = range->base = range->id * MAX_NB_GPIO_PER_BANK;

	range->npins = chip->ngpio;
	range->gc = chip;

	ret = gpiochip_add(chip);
	if (ret)
		goto err;

	gpio_chips[alias_idx] = oxnas_chip;
	gpio_banks = max(gpio_banks, alias_idx + 1);

	oxnas_gpio_of_irq_setup(np, oxnas_chip, irq);

	dev_info(&pdev->dev, "at address %p\n", oxnas_chip->regbase);

	return 0;
err:
	dev_err(&pdev->dev, "Failure %i for GPIO %i\n", ret, alias_idx);

	return ret;
}

static struct platform_driver oxnas_gpio_driver = {
	.driver = {
		.name = "gpio-oxnas",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(oxnas_gpio_of_match),
	},
	.probe = oxnas_gpio_probe,
};

static struct platform_driver oxnas_pinctrl_driver = {
	.driver = {
		.name = "pinctrl-oxnas",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(oxnas_pinctrl_of_match),
	},
	.probe = oxnas_pinctrl_probe,
	.remove = oxnas_pinctrl_remove,
};

static int __init oxnas_pinctrl_init(void)
{
	int ret;

	ret = platform_driver_register(&oxnas_gpio_driver);
	if (ret)
		return ret;

	return platform_driver_register(&oxnas_pinctrl_driver);
}
arch_initcall(oxnas_pinctrl_init);

static void __exit oxnas_pinctrl_exit(void)
{
	platform_driver_unregister(&oxnas_pinctrl_driver);
}

module_exit(oxnas_pinctrl_exit);
