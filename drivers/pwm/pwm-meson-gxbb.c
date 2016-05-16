/*
 * drivers/pwm/pwm-meson-gxbb.c
 *
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 * Copyright (c) 2016 BayLibre, Inc.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/pwm.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/of.h>

#define REG_PWM_A	0x0
#define REG_PWM_B	0x4
#define PWM_HIGH_SHIFT	16

#define REG_MISC_AB	0x8
#define MISC_B_CLK_EN	BIT(23)
#define MISC_A_CLK_EN	BIT(15)
#define MISC_CLK_DIV_MASK	0x7f
#define MISC_B_CLK_DIV_SHIFT	16
#define MISC_A_CLK_DIV_SHIFT	8
#define MISC_B_EN	BIT(1)
#define MISC_A_EN	BIT(0)

#define DUTY_MAX	1024

#define PWM_NUM		2

enum pwm_channel {
	PWM_A = 0,
	PWM_B,
};

struct meson_gxbb_pwm_channel {
	unsigned int pwm_hi;
	unsigned int pwm_lo;
	u8 pwm_pre_div;
	int period;
	int duty;
};

struct meson_gxbb_pwm_chip {
	struct pwm_chip chip;
	void __iomem *base;
	u8 inverter_mask;
	spinlock_t lock;
	struct clk *clk[PWM_NUM];
};

#define to_meson_gxbb_pwm_chip(chip) \
	container_of(chip, struct meson_gxbb_pwm_chip, chip)

static int meson_gxbb_pwm_calc(struct meson_gxbb_pwm_chip *chip,
			       struct meson_gxbb_pwm_channel *pwm_chan,
			       unsigned int id,
			       int duty_ns, unsigned int period_ns)
{
	unsigned long long pwm_post_div_ns;
	unsigned int pwm_pre_div;
	unsigned int pwm_cnt;
	unsigned int pwm_duty_cnt;
	unsigned int fin_freq = -1;
	unsigned int i = 0;

	if (duty_ns > period_ns)
		return -EINVAL;

	switch (id) {
	case PWM_A:
		fin_freq = clk_get_rate(chip->clk[0]);
		break;
	case PWM_B:
		fin_freq = clk_get_rate(chip->clk[1]);
		break;
	}
	if (fin_freq <= 0) {
		dev_err(chip->chip.dev, "invalid source clock frequency\n");
		return -EINVAL;
	}
	dev_dbg(chip->chip.dev, "fin_freq: %dHz\n", fin_freq);

	/* Calc pre_div with the period */
	for (i = 0; i < MISC_CLK_DIV_MASK; i++) {
		pwm_pre_div = i;
		pwm_post_div_ns = period_ns / (pwm_pre_div + 1);
		pwm_cnt = DIV_ROUND_CLOSEST_ULL(
				(u64)fin_freq * pwm_post_div_ns,
				NSEC_PER_SEC);
		dev_dbg(chip->chip.dev, "pre_div=%d post_div_ns=%llu cnt=%d\n",
				pwm_pre_div, pwm_post_div_ns, pwm_cnt);
		if (pwm_cnt <= 0xffff)
			break;
	}
	if (i == MISC_CLK_DIV_MASK) {
		dev_err(chip->chip.dev, "Unable to get period pre_div");
		return -EINVAL;
	}
	dev_dbg(chip->chip.dev, "period_ns=%d pre_div=%d pwm_cnt=%d\n",
		period_ns, pwm_pre_div, pwm_cnt);

	if (duty_ns == period_ns) {
		pwm_chan->pwm_pre_div = pwm_pre_div;
		pwm_chan->pwm_hi = pwm_cnt;
		pwm_chan->pwm_lo = 0;
	} else if (duty_ns == 0) {
		pwm_chan->pwm_pre_div = pwm_pre_div;
		pwm_chan->pwm_hi = 0;
		pwm_chan->pwm_lo = pwm_cnt;
	} else {
		/* Then check is we can have the duty with the same pre_div */
		pwm_post_div_ns = duty_ns / (pwm_pre_div + 1);
		pwm_duty_cnt = DIV_ROUND_CLOSEST_ULL(
					(u64)fin_freq * pwm_post_div_ns,
					NSEC_PER_SEC);
		if (pwm_cnt > 0xffff) {
			dev_err(chip->chip.dev, "Unable to get duty period, differences are too high");
			return -EINVAL;
		}
		dev_dbg(chip->chip.dev, "duty_ns=%d pre_div=%d pwm_cnt=%d\n",
			duty_ns, pwm_pre_div, pwm_duty_cnt);

		pwm_chan->pwm_pre_div = pwm_pre_div;
		pwm_chan->pwm_hi = pwm_duty_cnt;
		pwm_chan->pwm_lo = pwm_cnt - pwm_chan->pwm_hi;
	}

	return 0;
}

static int meson_gxbb_pwm_request(struct pwm_chip *chip,
				  struct pwm_device *pwm)
{
	struct meson_gxbb_pwm_channel *pwm_chan;

	pwm_chan = devm_kzalloc(chip->dev, sizeof(*pwm_chan), GFP_KERNEL);
	if (!pwm_chan)
		return -ENOMEM;

	pwm_set_chip_data(pwm, pwm_chan);

	return 0;
}

static void meson_gxbb_pwm_free(struct pwm_chip *chip,
				struct pwm_device *pwm)
{
	devm_kfree(chip->dev, pwm_get_chip_data(pwm));
	pwm_set_chip_data(pwm, NULL);
}

static int meson_gxbb_pwm_enable(struct pwm_chip *chip,
				 struct pwm_device *pwm)
{
	struct meson_gxbb_pwm_chip *pwm_data = to_meson_gxbb_pwm_chip(chip);
	unsigned int id = pwm->hwpwm;
	unsigned long flags;

	spin_lock_irqsave(&pwm_data->lock, flags);
	switch (id) {
	case PWM_A:
		writel(readl(pwm_data->base + REG_MISC_AB) | MISC_A_EN,
			pwm_data->base + REG_MISC_AB);
		break;

	case PWM_B:
		writel(readl(pwm_data->base + REG_MISC_AB) | MISC_B_EN,
			pwm_data->base + REG_MISC_AB);
		break;

	default:
		break;
	}
	spin_unlock_irqrestore(&pwm_data->lock, flags);

	return 0;
}

static void meson_gxbb_pwm_disable(struct pwm_chip *chip,
				   struct pwm_device *pwm)
{
	struct meson_gxbb_pwm_chip *pwm_data = to_meson_gxbb_pwm_chip(chip);
	unsigned int id = pwm->hwpwm;
	unsigned long flags;

	spin_lock_irqsave(&pwm_data->lock, flags);
	switch (id) {
	case PWM_A:
		writel(readl(pwm_data->base + REG_MISC_AB) & ~MISC_A_EN,
			pwm_data->base + REG_MISC_AB);
		break;

	case PWM_B:
		writel(readl(pwm_data->base + REG_MISC_AB) & ~MISC_B_EN,
			pwm_data->base + REG_MISC_AB);
		break;

	default:
		break;
	}
	spin_unlock_irqrestore(&pwm_data->lock, flags);
}

static int meson_gxbb_pwm_config(struct pwm_chip *chip,
				 struct pwm_device *pwm,
				 int duty_ns,
				 int period_ns)
{
	struct meson_gxbb_pwm_chip *pwm_data = to_meson_gxbb_pwm_chip(chip);
	struct meson_gxbb_pwm_channel *pwm_chan = pwm_get_chip_data(pwm);
	unsigned int id = pwm->hwpwm;
	int ret;

	if ((~(pwm_data->inverter_mask >> id) & 0x1))
		duty_ns = period_ns - duty_ns;

	if (period_ns == pwm_chan->period && duty_ns == pwm_chan->duty)
		return 0;

	ret = meson_gxbb_pwm_calc(pwm_data, pwm_chan, id, duty_ns, period_ns);
	if (ret) {
		dev_err(chip->dev, "error while calculating pwm parameters\n");
		return ret;
	}

	switch (id) {
	case PWM_A:
		writel((readl(pwm_data->base + REG_MISC_AB) &
			~(MISC_CLK_DIV_MASK << MISC_A_CLK_DIV_SHIFT)) |
			((pwm_chan->pwm_pre_div << MISC_A_CLK_DIV_SHIFT) |
			 MISC_A_CLK_EN),
			pwm_data->base + REG_MISC_AB);

		writel((pwm_chan->pwm_hi << PWM_HIGH_SHIFT) |
		       (pwm_chan->pwm_lo),
		       pwm_data->base + REG_PWM_A);
		break;

	case PWM_B:
		writel((readl(pwm_data->base + REG_MISC_AB) &
			~(MISC_CLK_DIV_MASK << MISC_B_CLK_DIV_SHIFT)) |
			((pwm_chan->pwm_pre_div << MISC_B_CLK_DIV_SHIFT) |
			 MISC_B_CLK_EN),
			pwm_data->base + REG_MISC_AB);

		writel((pwm_chan->pwm_hi << PWM_HIGH_SHIFT) |
		       (pwm_chan->pwm_lo),
		       pwm_data->base + REG_PWM_B);
		break;

	default:
		break;
	}

	pwm_chan->period = period_ns;
	pwm_chan->duty = duty_ns;

	return 0;
}

static int meson_gxbb_pwm_set_polarity(struct pwm_chip *chip,
				    struct pwm_device *pwm,
				    enum pwm_polarity polarity)
{
	struct meson_gxbb_pwm_chip *pwm_data = to_meson_gxbb_pwm_chip(chip);
	struct meson_gxbb_pwm_channel *pwm_chan = pwm_get_chip_data(pwm);
	bool invert = (polarity == PWM_POLARITY_NORMAL);
	unsigned long flags;

	spin_lock_irqsave(&pwm_data->lock, flags);

	if (invert)
		pwm_data->inverter_mask |= BIT(pwm->hwpwm);
	else
		pwm_data->inverter_mask &= ~BIT(pwm->hwpwm);

	meson_gxbb_pwm_config(chip, pwm, pwm_chan->duty, pwm_chan->period);

	spin_unlock_irqrestore(&pwm_data->lock, flags);

	return 0;
}

static const struct pwm_ops meson_gxbb_pwm_ops = {
	.request	= meson_gxbb_pwm_request,
	.free		= meson_gxbb_pwm_free,
	.enable		= meson_gxbb_pwm_enable,
	.disable	= meson_gxbb_pwm_disable,
	.config		= meson_gxbb_pwm_config,
	.set_polarity	= meson_gxbb_pwm_set_polarity,
	.owner		= THIS_MODULE,
};

static const struct of_device_id meson_gxbb_pwm_matches[] = {
	{ .compatible = "amlogic,meson-gxbb-pwm", },
	{},
};
MODULE_DEVICE_TABLE(of, meson_gxbb_pwm_matches);

static int meson_gxbb_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct meson_gxbb_pwm_chip *chip;
	struct resource *res;
	int ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	chip->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(chip->base))
		return PTR_ERR(chip->base);

	chip->clk[0] = devm_clk_get(dev, "clkin0");
	if (IS_ERR(chip->clk[0])) {
		if (PTR_ERR(chip->clk[0]) != -EPROBE_DEFER)
			dev_err(dev, "failed to get pwm 0 base clk\n");
		return PTR_ERR(chip->clk[0]);
	}

	chip->clk[1] = devm_clk_get(dev, "clkin1");
	if (IS_ERR(chip->clk[1])) {
		if (PTR_ERR(chip->clk[1]) != -EPROBE_DEFER)
			dev_err(dev, "failed to get pwm 1 base clk\n");
		return PTR_ERR(chip->clk[1]);
	}

	clk_prepare_enable(chip->clk[0]);
	clk_prepare_enable(chip->clk[1]);

	chip->chip.dev = dev;
	chip->chip.ops = &meson_gxbb_pwm_ops;
	chip->chip.base = -1;
	chip->chip.npwm = PWM_NUM;
	chip->inverter_mask = BIT(PWM_NUM) - 1;

	ret = pwmchip_add(&chip->chip);
	if (ret < 0) {
		dev_err(dev, "failed to register PWM chip\n");
		return ret;
	}

	platform_set_drvdata(pdev, chip);

	return 0;
}

static int meson_gxbb_pwm_remove(struct platform_device *pdev)
{
	struct meson_gxbb_pwm_chip *chip = platform_get_drvdata(pdev);
	int ret;

	ret = pwmchip_remove(&chip->chip);
	if (ret < 0)
		return ret;

	return 0;
}

static struct platform_driver meson_gxbb_pwm_driver = {
	.driver		= {
		.name	= "meson-gxbb-pwm",
		.of_match_table = of_match_ptr(meson_gxbb_pwm_matches),
	},
	.probe		= meson_gxbb_pwm_probe,
	.remove		= meson_gxbb_pwm_remove,
};
module_platform_driver(meson_gxbb_pwm_driver);

MODULE_ALIAS("platform:meson-gxbb-pwm");
MODULE_LICENSE("GPL");
