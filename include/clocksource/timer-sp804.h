#ifndef __CLKSOURCE_TIMER_SP804_H
#define __CLKSOURCE_TIMER_SP804_H

struct clk;

struct timer_sp804 {
	void __iomem *clockevent_base;
	void __iomem *clocksource_base;
	const char *name;
	struct clk *clockevent_clk;
	struct clk *clocksource_clk;
	unsigned int irq;
	unsigned int width;
};

void __sp804_clocksource_and_sched_clock_init(struct timer_sp804 *sp804, bool);
void __sp804_clockevents_init(struct timer_sp804 *sp804);
void sp804_timer_disable(void __iomem *);

static inline void sp804_clocksource_init(void __iomem *base, const char *name)
{
	struct timer_sp804 sp804 = {
		.clocksource_base = base,
		.name = name,
		.clocksource_clk = NULL,
		.width = 32,
	};

	__sp804_clocksource_and_sched_clock_init(&sp804, false);
}

static inline void sp804_clocksource_and_sched_clock_init(void __iomem *base,
							  const char *name)
{
	struct timer_sp804 sp804 = {
		.clocksource_base = base,
		.name = name,
		.clocksource_clk = NULL,
		.width = 32,
	};

	__sp804_clocksource_and_sched_clock_init(&sp804, true);
}

static inline void sp804_clockevents_init(void __iomem *base, unsigned int irq, const char *name)
{
	struct timer_sp804 sp804 = {
		.clockevent_base = base,
		.name = name,
		.clockevent_clk = NULL,
		.width = 32,
	};

	__sp804_clockevents_init(&sp804);
}
#endif
