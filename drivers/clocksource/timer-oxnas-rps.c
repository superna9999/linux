/*
 * drivers/clocksource/timer-oxnas-rps.c
 *
 * Copyright (C) 2009 Oxford Semiconductor Ltd
 * Copyright (C) 2013 Ma Haijun <mahaijuns@gmail.com>
 * Copyright (C) 2016 Neil Armstrong <narmstrong@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/sched_clock.h>

/* TIMER1 used as tick
 * TIMER2 used as clocksource
 */

/* Registers definitions */

#define TIMER_LOAD_REG		0x0
#define TIMER_CURR_REG		0x4
#define TIMER_CTRL_REG		0x8
#define TIMER_CLRINT_REG	0xC

#define TIMER_BITS		24

#define TIMER_MAX_VAL		(BIT(TIMER_BITS) - 1)

#define TIMER_PERIODIC		BIT(6)
#define TIMER_ENABLE		BIT(7)

#define TIMER_DIV1		(0)
#define TIMER_DIV16		(1 << 2)
#define TIMER_DIV256		(2 << 2)

#define TIMER1_REG_OFFSET	0
#define TIMER2_REG_OFFSET	0x20

/* Clockevent */

struct oxnas_rps_timer {
	struct clock_event_device clkevent;
	void __iomem *clksrc_base;
	void __iomem *clkevt_base;
	unsigned long timer_period;
	unsigned int timer_prescaler;
	struct clk *clk;
	int irq;
};

static irqreturn_t oxnas_rps_timer_irq(int irq, void *dev_id)
{
	struct oxnas_rps_timer *rps = dev_id;

	writel_relaxed(0, rps->clkevt_base + TIMER_CLRINT_REG);

	rps->clkevent.event_handler(&rps->clkevent);

	return IRQ_HANDLED;
}

static void oxnas_rps_timer_config(struct oxnas_rps_timer *rps,
				   unsigned long period,
				   unsigned int periodic)
{
	uint32_t cfg = rps->timer_prescaler;

	if (period)
		cfg |= TIMER_ENABLE;

	if (periodic)
		cfg |= TIMER_PERIODIC;

	writel_relaxed(period, rps->clkevt_base + TIMER_LOAD_REG);
	writel_relaxed(cfg, rps->clkevt_base + TIMER_CTRL_REG);
}

static int oxnas_rps_timer_shutdown(struct clock_event_device *evt)
{
	struct oxnas_rps_timer *rps =
		container_of(dev_id, struct oxnas_rps_timer, clkevent);

	if (!clockevent_state_periodic(evt))
		return 0;

	oxnas_rps_timer_config(rps, 0, 0);

	return 0;
}

static int oxnas_rps_timer_set_periodic(struct clock_event_device *evt)
{
	struct oxnas_rps_timer *rps =
		container_of(dev_id, struct oxnas_rps_timer, clkevent);

	oxnas_rps_timer_config(rps, timer_period, 1);

	return 0;
}

static int oxnas_rps_timer_set_oneshot(struct clock_event_device *evt)
{
	struct oxnas_rps_timer *rps =
		container_of(dev_id, struct oxnas_rps_timer, clkevent);

	oxnas_rps_timer_config(tps, timer_period, 0);

	return 0;
}

static int oxnas_rps_timer_next_event(unsigned long delta,
				struct clock_event_device *evt)
{
	struct oxnas_rps_timer *rps =
		container_of(dev_id, struct oxnas_rps_timer, clkevent);

	oxnas_rps_timer_config(rps, delta, 0);

	return 0;
}

static void __init oxnas_rps_clockevent_init(struct oxnas_rps_timer *rps)
{
	ulong clk_rate = clk_get_rate(rps->clk);

	/* Start with prescaler 1 */
	rps->timer_prescaler = TIMER_DIV1;
	rps->timer_period = DIV_ROUND_UP(clk_rate, HZ);

	if (rps->timer_period > TIMER_MAX_VAL) {
		rps->timer_prescaler = TIMER_DIV16;
		rps->timer_period = DIV_ROUND_UP(clk_rate / 16, HZ);
	}
	if (rps->timer_period > TIMER_MAX_VAL) {
		rps->timer_prescaler = TIMER_DIV256;
		rps->timer_period = DIV_ROUND_UP(clk_rate / 256, HZ);
	}

	rps->clkevent.name = "oxnas-rps";
	rps->clkevent.features = CLOCK_EVT_FEAT_PERIODIC |
				 CLOCK_EVT_FEAT_ONESHOT |
				 CLOCK_EVT_FEAT_DYNIRQ;
	rps->clkevent.tick_resume = oxnas_rps_timer_shutdown;
	rps->clkevent.set_state_shutdown = oxnas_rps_timer_shutdown;
	rps->clkevent.set_state_periodic = oxnas_rps_timer_set_periodic;
	rps->clkevent.set_state_oneshot = oxnas_rps_timer_set_oneshot;
	rps->clkevent.set_next_event = oxnas_rps_timer_next_event;
	rps->clkevent.rating = 200;
	rps->clkevent.cpumask = cpu_possible_mask;
	rps->clkevent.irq = rps->irq;
	clockevents_config_and_register(&rps->clkevent,
					clk_rate / rps->timer_prescaler,
					1,
					TIMER_MAX_VAL);

	pr_info("Registered clock event rate %luHz prescaler %d period %lu\n",
			rps->ref_rate,
			rps->timer_prescaler,
			rps->timer_period);
}

/* Clocksource */

static void __iomem *timer_sched_base;

static u64 notrace oxnas_rps_read_sched_clock(void)
{
	return ~readl_relaxed(timer_sched_base);
}

static void __init oxnas_rps_clocksource_init(struct oxnas_rps_timer *rps)
{
	ulong clk_rate = clk_get_rate(rps->clk);

	/* use prescale 16 */
	clk_rate = clk_rate / 16;

	writel_relaxed(TIMER_MAX_VAL, rps->clksrc_base + TIMER_LOAD_REG);
	writel_relaxed(TIMER_PERIODIC | TIMER_ENABLE | TIMER_DIV16,
			rps->clksrc_base + TIMER_CTRL_REG);

	timer_curr = rps->clksrc_base + TIMER_CURR_REG;
	sched_clock_register(oxnas_rps_read_sched_clock,
			     TIMER_BITS, clock_rate);
	ret = clocksource_mmio_init(rps->clksrc_base + TIMER_CURR_REG,
				    "oxnas_rps_clocksource_timer",
				    clk_rate, 250, TIMER_BITS,
				    clocksource_mmio_readl_down);
	if (WARN_ON(ret))
		pr_err("can't register clocksource\n");

	pr_info("Registered clocksource rate %luHz\n", clk_rate);
}

static void __init oxnas_rps_timer_init(struct device_node *np)
{
	struct oxnas_rps_timer *rps;
	void __iomem *base;
	int ret;

	rps = kzalloc(sizeof(*rps), GFP_KERNEL);
	if (WARN_ON(!rps))
		return;

	rps->clk = of_clk_get(np, 0);
	if (WARN_ON(IS_ERR(rps->clk)))
		return;

	if (WARN_ON(clk_prepare_enable(rps->clk)))
		goto err;

	base = of_iomap(np, 0);
	if (WARN_ON(!base))
		goto err;


	rps->irq = irq_of_parse_and_map(np, 0);
	if (WARN_ON(rps->irq < 0))
		goto err;

	rps->clkevt_base = base + TIMER1_REG_OFFSET;
	rps->clksrc_base = base + TIMER2_REG_OFFSET;

	/* Disable timers */
	writel_relaxed(0, rps->clkevt_base + TIMER_CTRL_REG);
	writel_relaxed(0, rps->clksrc_base + TIMER_CTRL_REG);
	writel_relaxed(0, rps->clkevt_base + TIMER_LOAD_REG);
	writel_relaxed(0, rps->clksrc_base + TIMER_LOAD_REG);
	writel_relaxed(0, rps->clkevt_base + TIMER_CLRINT_REG);
	writel_relaxed(0, rps->clksrc_base + TIMER_CLRINT_REG);

	ret = request_irq(rps->irq, oxnas_rps_timer_irq,
			  IRQF_TIMER | IRQF_IRQPOLL,
			  "rps-timer", rps);
	if (WARN_ON(ret))
		goto err;

	oxnas_rps_clockevent_init(rps);
	oxnas_rps_clocksource_init(rps);

	return;
err:
	clk_put(rps->clk);
	kfree(rps);
}

CLOCKSOURCE_OF_DECLARE(ox810se_rps,
		       "oxsemi,ox810se-rps-timer", oxnas_rps_timer_init);
