// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Realtek DHC RTD1625 gpio driver
 *
 * Copyright (c) 2023-2026 Realtek Semiconductor Corp.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define RTD1625_GPIO_DIR BIT(0)
#define RTD1625_GPIO_OUT BIT(2)
#define RTD1625_GPIO_IN BIT(4)
#define RTD1625_GPIO_EDGE_INT_DP BIT(6)
#define RTD1625_GPIO_EDGE_INT_EN BIT(8)
#define RTD1625_GPIO_LEVEL_INT_EN BIT(16)
#define RTD1625_GPIO_LEVEL_INT_DP BIT(18)
#define RTD1625_GPIO_DEBOUNCE GENMASK(30, 28)
#define RTD1625_GPIO_DEBOUNCE_WREN BIT(31)

#define RTD1625_GPIO_WREN(x) ((x) << 1)

/* Write-enable masks for all GPIO configs and reserved hardware bits */
#define RTD1625_ISO_GPIO_WREN_ALL 0x8000aa8a
#define RTD1625_ISOM_GPIO_WREN_ALL 0x800aaa8a

#define RTD1625_GPIO_DEBOUNCE_1US 0
#define RTD1625_GPIO_DEBOUNCE_10US 1
#define RTD1625_GPIO_DEBOUNCE_100US 2
#define RTD1625_GPIO_DEBOUNCE_1MS 3
#define RTD1625_GPIO_DEBOUNCE_10MS 4
#define RTD1625_GPIO_DEBOUNCE_20MS 5
#define RTD1625_GPIO_DEBOUNCE_30MS 6
#define RTD1625_GPIO_DEBOUNCE_50MS 7

#define GPIO_CONTROL(gpio) ((gpio) * 4)

enum rtd1625_irq_index {
	RTD1625_IRQ_ASSERT,
	RTD1625_IRQ_DEASSERT,
	RTD1625_IRQ_LEVEL,
	RTD1625_MAX_IRQS
};

/**
 * struct rtd1625_gpio_info - Specific GPIO register information
 * @num_gpios: The number of GPIOs
 * @irq_type_support: Supported IRQ types
 * @gpa_offset: Offset for GPIO assert interrupt status registers
 * @gpda_offset: Offset for GPIO deassert interrupt status registers
 * @level_offset: Offset of level interrupt status register
 * @write_en_all: Write-enable mask for all configurable bits
 */
struct rtd1625_gpio_info {
	unsigned int	num_gpios;
	unsigned int	irq_type_support;
	unsigned int	base_offset;
	unsigned int	gpa_offset;
	unsigned int	gpda_offset;
	unsigned int	level_offset;
	unsigned int	write_en_all;
};

struct rtd1625_gpio {
	struct gpio_chip		gpio_chip;
	const struct rtd1625_gpio_info	*info;
	void __iomem			*base;
	void __iomem			*irq_base;
	unsigned int			irqs[RTD1625_MAX_IRQS];
	raw_spinlock_t			lock;
	unsigned int			*save_regs;
};

static unsigned int rtd1625_gpio_gpa_offset(struct rtd1625_gpio *data, unsigned int offset)
{
	return data->info->gpa_offset + ((offset / 32) * 4);
}

static unsigned int rtd1625_gpio_gpda_offset(struct rtd1625_gpio *data, unsigned int offset)
{
	return data->info->gpda_offset + ((offset / 32) * 4);
}

static unsigned int rtd1625_gpio_level_offset(struct rtd1625_gpio *data, unsigned int offset)
{
	return data->info->level_offset + ((offset / 32) * 4);
}

static int rtd1625_gpio_set_debounce(struct gpio_chip *chip, unsigned int offset,
				     unsigned int debounce)
{
	struct rtd1625_gpio *data = gpiochip_get_data(chip);
	u8 deb_val;
	u32 val;

	switch (debounce) {
	case 1:
		deb_val = RTD1625_GPIO_DEBOUNCE_1US;
		break;
	case 10:
		deb_val = RTD1625_GPIO_DEBOUNCE_10US;
		break;
	case 100:
		deb_val = RTD1625_GPIO_DEBOUNCE_100US;
		break;
	case 1000:
		deb_val = RTD1625_GPIO_DEBOUNCE_1MS;
		break;
	case 10000:
		deb_val = RTD1625_GPIO_DEBOUNCE_10MS;
		break;
	case 20000:
		deb_val = RTD1625_GPIO_DEBOUNCE_20MS;
		break;
	case 30000:
		deb_val = RTD1625_GPIO_DEBOUNCE_30MS;
		break;
	case 50000:
		deb_val = RTD1625_GPIO_DEBOUNCE_50MS;
		break;
	default:
		return -ENOTSUPP;
	}

	val = FIELD_PREP(RTD1625_GPIO_DEBOUNCE, deb_val) | RTD1625_GPIO_DEBOUNCE_WREN;

	guard(raw_spinlock_irqsave)(&data->lock);

	writel_relaxed(val, data->base + GPIO_CONTROL(offset));

	return 0;
}

static int rtd1625_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
				   unsigned long config)
{
	u32 debounce;

	if (pinconf_to_config_param(config) == PIN_CONFIG_INPUT_DEBOUNCE) {
		debounce = pinconf_to_config_argument(config);
		return rtd1625_gpio_set_debounce(chip, offset, debounce);
	}

	return gpiochip_generic_config(chip, offset, config);
}

static int rtd1625_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct rtd1625_gpio *data = gpiochip_get_data(chip);
	u32 val = RTD1625_GPIO_WREN(RTD1625_GPIO_OUT);

	if (value)
		val |= RTD1625_GPIO_OUT;

	guard(raw_spinlock_irqsave)(&data->lock);

	writel_relaxed(val, data->base + GPIO_CONTROL(offset));

	return 0;
}

static int rtd1625_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct rtd1625_gpio *data = gpiochip_get_data(chip);
	u32 val;

	guard(raw_spinlock_irqsave)(&data->lock);

	val = readl_relaxed(data->base + GPIO_CONTROL(offset));

	if (val & RTD1625_GPIO_DIR)
		return !!(val & RTD1625_GPIO_OUT);
	else
		return !!(val & RTD1625_GPIO_IN);
}

static int rtd1625_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct rtd1625_gpio *data = gpiochip_get_data(chip);
	u32 val;

	guard(raw_spinlock_irqsave)(&data->lock);

	val = readl_relaxed(data->base + GPIO_CONTROL(offset));

	if (val & RTD1625_GPIO_DIR)
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int rtd1625_gpio_set_direction(struct gpio_chip *chip, unsigned int offset, bool out)
{
	struct rtd1625_gpio *data = gpiochip_get_data(chip);
	u32 val = RTD1625_GPIO_WREN(RTD1625_GPIO_DIR);

	if (out)
		val |= RTD1625_GPIO_DIR;

	guard(raw_spinlock_irqsave)(&data->lock);

	writel_relaxed(val, data->base + GPIO_CONTROL(offset));

	return 0;
}

static int rtd1625_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	return rtd1625_gpio_set_direction(chip, offset, false);
}

static int rtd1625_gpio_direction_output(struct gpio_chip *chip, unsigned int offset, int value)
{
	rtd1625_gpio_set(chip, offset, value);

	return rtd1625_gpio_set_direction(chip, offset, true);
}

static void rtd1625_gpio_irq_handle(struct irq_desc *desc)
{
	unsigned int (*get_reg_offset)(struct rtd1625_gpio *gpio, unsigned int offset);
	struct rtd1625_gpio *data = irq_desc_get_handler_data(desc);
	struct irq_domain *domain = data->gpio_chip.irq.domain;
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int irq = irq_desc_get_irq(desc);
	unsigned long status;
	unsigned int reg_offset, i, j;
	unsigned int girq;
	irq_hw_number_t hwirq;
	u32 irq_type;

	if (irq == data->irqs[RTD1625_IRQ_ASSERT])
		get_reg_offset = &rtd1625_gpio_gpa_offset;
	else if (irq == data->irqs[RTD1625_IRQ_DEASSERT])
		get_reg_offset = &rtd1625_gpio_gpda_offset;
	else if (irq == data->irqs[2])
		get_reg_offset = &rtd1625_gpio_level_offset;
	else
		return;

	chained_irq_enter(chip, desc);

	for (i = 0; i < data->info->num_gpios; i += 32) {
		reg_offset = get_reg_offset(data, i);
		status = readl_relaxed(data->irq_base + reg_offset);

		/*
		 * Hardware quirk: The controller fires both "assert" and "de-assert"
		 * interrupts simultaneously on any edge toggle.
		 * We must pre-clear edge interrupts here. If we drop an unwanted
		 * de-assert interrupt below, it will never reach the IRQ core
		 * (generic_handle_domain_irq), meaning ->irq_ack() won't be called.
		 * Failing to clear it here leads to an interrupt storm.
		 */
		if (irq != data->irqs[RTD1625_IRQ_LEVEL])
			writel_relaxed(status, data->irq_base + reg_offset);

		for_each_set_bit(j, &status, 32) {
			hwirq = i + j;
			girq = irq_find_mapping(domain, hwirq);
			irq_type = irq_get_trigger_type(girq);

			/*
			 * Filter out the hardware-forced de-assert interrupt unless
			 * the user explicitly requested IRQ_TYPE_EDGE_BOTH.
			 */
			if (irq == data->irqs[RTD1625_IRQ_DEASSERT] &&
			    irq_type != IRQ_TYPE_EDGE_BOTH)
				continue;

			generic_handle_domain_irq(domain, hwirq);
		}
	}

	chained_irq_exit(chip, desc);
}

static void rtd1625_gpio_ack_irq(struct irq_data *d)
{
	struct rtd1625_gpio *data = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	u32 irq_type = irqd_get_trigger_type(d);
	u32 bit_mask = BIT(hwirq % 32);
	int reg_offset;

	if (irq_type & IRQ_TYPE_LEVEL_MASK) {
		reg_offset = rtd1625_gpio_level_offset(data, hwirq);
		writel_relaxed(bit_mask, data->irq_base + reg_offset);
	}
}

static void rtd1625_gpio_enable_edge_irq(struct rtd1625_gpio *data, irq_hw_number_t hwirq)
{
	int gpda_reg_offset = rtd1625_gpio_gpda_offset(data, hwirq);
	int gpa_reg_offset = rtd1625_gpio_gpa_offset(data, hwirq);
	u32 clr_mask = BIT(hwirq % 32);
	u32 val;

	guard(raw_spinlock_irqsave)(&data->lock);

	writel_relaxed(clr_mask, data->irq_base + gpa_reg_offset);
	writel_relaxed(clr_mask, data->irq_base + gpda_reg_offset);
	val = RTD1625_GPIO_EDGE_INT_EN | RTD1625_GPIO_WREN(RTD1625_GPIO_EDGE_INT_EN);
	writel_relaxed(val, data->base + GPIO_CONTROL(hwirq));
}

static void rtd1625_gpio_disable_edge_irq(struct rtd1625_gpio *data, irq_hw_number_t hwirq)
{
	u32 val;

	guard(raw_spinlock_irqsave)(&data->lock);

	val = RTD1625_GPIO_WREN(RTD1625_GPIO_EDGE_INT_EN);
	writel_relaxed(val, data->base + GPIO_CONTROL(hwirq));
}

static void rtd1625_gpio_enable_level_irq(struct rtd1625_gpio *data, irq_hw_number_t hwirq)
{
	int level_reg_offset = rtd1625_gpio_level_offset(data, hwirq);
	u32 clr_mask = BIT(hwirq % 32);
	u32 val;

	guard(raw_spinlock_irqsave)(&data->lock);

	writel_relaxed(clr_mask, data->irq_base + level_reg_offset);
	val = RTD1625_GPIO_LEVEL_INT_EN | RTD1625_GPIO_WREN(RTD1625_GPIO_LEVEL_INT_EN);
	writel_relaxed(val, data->base + GPIO_CONTROL(hwirq));
}

static void rtd1625_gpio_disable_level_irq(struct rtd1625_gpio *data, irq_hw_number_t hwirq)
{
	u32 val;

	guard(raw_spinlock_irqsave)(&data->lock);

	val = RTD1625_GPIO_WREN(RTD1625_GPIO_LEVEL_INT_EN);
	writel_relaxed(val, data->base + GPIO_CONTROL(hwirq));
}

static void rtd1625_gpio_enable_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct rtd1625_gpio *data = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	u32 irq_type = irqd_get_trigger_type(d);

	gpiochip_enable_irq(gc, hwirq);

	if (irq_type & IRQ_TYPE_EDGE_BOTH)
		rtd1625_gpio_enable_edge_irq(data, hwirq);
	else if (irq_type & IRQ_TYPE_LEVEL_MASK)
		rtd1625_gpio_enable_level_irq(data, hwirq);
}

static void rtd1625_gpio_disable_irq(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct rtd1625_gpio *data = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	u32 irq_type = irqd_get_trigger_type(d);

	if (irq_type & IRQ_TYPE_EDGE_BOTH)
		rtd1625_gpio_disable_edge_irq(data, hwirq);
	else if (irq_type & IRQ_TYPE_LEVEL_MASK)
		rtd1625_gpio_disable_level_irq(data, hwirq);

	gpiochip_disable_irq(gc, hwirq);
}

static int rtd1625_gpio_irq_set_level_type(struct irq_data *d, bool level)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct rtd1625_gpio *data = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	u32 val = RTD1625_GPIO_WREN(RTD1625_GPIO_LEVEL_INT_DP);

	if (!(data->info->irq_type_support & IRQ_TYPE_LEVEL_MASK))
		return -EINVAL;

	if (level)
		val |= RTD1625_GPIO_LEVEL_INT_DP;

	scoped_guard(raw_spinlock_irqsave, &data->lock)
		writel_relaxed(val, data->base + GPIO_CONTROL(hwirq));

	irq_set_handler_locked(d, handle_level_irq);

	return 0;
}

static int rtd1625_gpio_irq_set_edge_type(struct irq_data *d, bool polarity)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct rtd1625_gpio *data = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	u32 val = RTD1625_GPIO_WREN(RTD1625_GPIO_EDGE_INT_DP);

	if (!(data->info->irq_type_support & IRQ_TYPE_EDGE_BOTH))
		return -EINVAL;

	if (polarity)
		val |= RTD1625_GPIO_EDGE_INT_DP;

	scoped_guard(raw_spinlock_irqsave, &data->lock)
		writel_relaxed(val, data->base + GPIO_CONTROL(hwirq));

	irq_set_handler_locked(d, handle_edge_irq);

	return 0;
}

static int rtd1625_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		return rtd1625_gpio_irq_set_edge_type(d, 1);

	case IRQ_TYPE_EDGE_FALLING:
		return rtd1625_gpio_irq_set_edge_type(d, 0);

	case IRQ_TYPE_EDGE_BOTH:
		return rtd1625_gpio_irq_set_edge_type(d, 1);

	case IRQ_TYPE_LEVEL_HIGH:
		return rtd1625_gpio_irq_set_level_type(d, 0);

	case IRQ_TYPE_LEVEL_LOW:
		return rtd1625_gpio_irq_set_level_type(d, 1);

	default:
		return -EINVAL;
	}
}

static struct irq_chip rtd1625_iso_gpio_irq_chip = {
	.name = "rtd1625-gpio",
	.irq_ack = rtd1625_gpio_ack_irq,
	.irq_mask = rtd1625_gpio_disable_irq,
	.irq_unmask = rtd1625_gpio_enable_irq,
	.irq_set_type = rtd1625_gpio_irq_set_type,
	.flags = IRQCHIP_IMMUTABLE | IRQCHIP_SKIP_SET_WAKE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int rtd1625_gpio_setup_irq(struct platform_device *pdev, struct rtd1625_gpio *data)
{
	struct gpio_irq_chip *irq_chip;
	unsigned int num_irqs;
	int irq;

	/*
	 * Interrupt support is optional. All IRQs must be provided together.
	 * If index 0 is missing, we assume no interrupts are configured in DT
	 * and fall back to basic GPIO operation.
	 */
	irq = platform_get_irq_optional(pdev, 0);
	if (irq == -ENXIO)
		return 0;
	if (irq < 0)
		return irq;

	num_irqs = (data->info->irq_type_support & IRQ_TYPE_LEVEL_MASK) ? 3 : 2;
	data->irqs[RTD1625_IRQ_ASSERT] = irq;

	for (unsigned int i = 1; i < num_irqs; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0)
			return irq;
		data->irqs[i] = irq;
	}

	irq_chip = &data->gpio_chip.irq;
	irq_chip->handler = handle_bad_irq;
	irq_chip->default_type = IRQ_TYPE_NONE;
	irq_chip->parent_handler = rtd1625_gpio_irq_handle;
	irq_chip->parent_handler_data = data;
	irq_chip->num_parents = num_irqs;
	irq_chip->parents = data->irqs;

	gpio_irq_chip_set_chip(irq_chip, &rtd1625_iso_gpio_irq_chip);

	return 0;
}

static int rtd1625_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtd1625_gpio *data;
	void __iomem *irq_base;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->info = device_get_match_data(dev);
	if (!data->info)
		return -EINVAL;

	raw_spin_lock_init(&data->lock);

	irq_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(irq_base))
		return PTR_ERR(irq_base);

	data->irq_base = irq_base;
	data->base = irq_base + data->info->base_offset;

	data->save_regs = devm_kcalloc(dev, data->info->num_gpios, sizeof(*data->save_regs),
				       GFP_KERNEL);
	if (!data->save_regs)
		return -ENOMEM;

	data->gpio_chip.label = dev_name(dev);
	data->gpio_chip.base = -1;
	data->gpio_chip.ngpio = data->info->num_gpios;
	data->gpio_chip.request = gpiochip_generic_request;
	data->gpio_chip.free = gpiochip_generic_free;
	data->gpio_chip.get_direction = rtd1625_gpio_get_direction;
	data->gpio_chip.direction_input = rtd1625_gpio_direction_input;
	data->gpio_chip.direction_output = rtd1625_gpio_direction_output;
	data->gpio_chip.set = rtd1625_gpio_set;
	data->gpio_chip.get = rtd1625_gpio_get;
	data->gpio_chip.set_config = rtd1625_gpio_set_config;
	data->gpio_chip.parent = dev;

	ret = rtd1625_gpio_setup_irq(pdev, data);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, data);

	return devm_gpiochip_add_data(dev, &data->gpio_chip, data);
}

static const struct rtd1625_gpio_info rtd1625_iso_gpio_info = {
	.num_gpios        = 166,
	.irq_type_support = IRQ_TYPE_EDGE_BOTH,
	.base_offset      = 0x100,
	.gpa_offset       = 0x000,
	.gpda_offset      = 0x020,
	.write_en_all     = RTD1625_ISO_GPIO_WREN_ALL,
};

static const struct rtd1625_gpio_info rtd1625_isom_gpio_info = {
	.num_gpios        = 4,
	.irq_type_support = IRQ_TYPE_EDGE_BOTH | IRQ_TYPE_LEVEL_LOW |
			    IRQ_TYPE_LEVEL_HIGH,
	.base_offset      = 0x20,
	.gpa_offset       = 0x00,
	.gpda_offset      = 0x04,
	.level_offset     = 0x18,
	.write_en_all     = RTD1625_ISOM_GPIO_WREN_ALL,
};

static int rtd1625_gpio_suspend(struct device *dev)
{
	struct rtd1625_gpio *data = dev_get_drvdata(dev);
	const struct rtd1625_gpio_info *info = data->info;

	for (unsigned int i = 0; i < info->num_gpios; i++)
		data->save_regs[i] = readl_relaxed(data->base + GPIO_CONTROL(i));

	return 0;
}

static int rtd1625_gpio_resume(struct device *dev)
{
	struct rtd1625_gpio *data = dev_get_drvdata(dev);
	const struct rtd1625_gpio_info *info = data->info;

	for (unsigned int i = 0; i < info->num_gpios; i++)
		writel_relaxed(data->save_regs[i] | info->write_en_all,
			       data->base + GPIO_CONTROL(i));

	return 0;
}

static DEFINE_NOIRQ_DEV_PM_OPS(rtd1625_gpio_pm_ops, rtd1625_gpio_suspend, rtd1625_gpio_resume);

static const struct of_device_id rtd1625_gpio_of_matches[] = {
	{ .compatible = "realtek,rtd1625-iso-gpio", .data = &rtd1625_iso_gpio_info },
	{ .compatible = "realtek,rtd1625-isom-gpio", .data = &rtd1625_isom_gpio_info },
	{ }
};
MODULE_DEVICE_TABLE(of, rtd1625_gpio_of_matches);

static struct platform_driver rtd1625_gpio_platform_driver = {
	.driver = {
		.name = "gpio-rtd1625",
		.of_match_table = rtd1625_gpio_of_matches,
		.pm = pm_sleep_ptr(&rtd1625_gpio_pm_ops),
	},
	.probe = rtd1625_gpio_probe,
};
module_platform_driver(rtd1625_gpio_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Realtek Semiconductor Corporation");
MODULE_DESCRIPTION("Realtek DHC SoC RTD1625 gpio driver");
