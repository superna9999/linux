/*
 * lockup.c
 *
 * Deliberately do "stupid" things to see if we can detect/debug them
 * properly.
 *
 * The sole purpose of this module is to help with debugging of system
 * debug tools.
 *
 * Copyright (C) 2014 Linaro Limited
 *                    Daniel Thompson <daniel.thompson@linaro.org>
 */

#define pr_fmt(fmt) "lockup[%u]: " fmt, smp_processor_id()

#include <linux/debugfs.h>
#include <linux/debug_locks.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/irq_work.h>

static DEFINE_SPINLOCK(lockup_lock);

static int lockup_do_action_on_cpu(int cpu, long (*action)(void *))
{
	if (cpu == -1)
		(void) action(NULL);

	if (cpu < 0 || cpu > num_possible_cpus())
		return -EINVAL;

	pr_info("About to run %pf on cpu %d\n", action, cpu);

	/* work_on_cpu() ends up performing an uninterruptible
	 * wait-for-completion. This means we'll lose the prompt
	 * regardless of the CPU we send the work to.
	 */
	work_on_cpu(cpu, action, NULL);

	return 0;
}

#define DEFINE_LOCKUP_ATTRIBUTE(__fops, __action)                              \
	static int __fops##_get(void *data, u64 *val)                          \
	{                                                                      \
		*val = __action(NULL);                                         \
		return 0;                                                      \
	}                                                                      \
	static int __fops##_set(void *data, u64 val)                           \
	{                                                                      \
		lockup_do_action_on_cpu(val, __action);                        \
		return 0;                                                      \
	}                                                                      \
	DEFINE_SIMPLE_ATTRIBUTE(__fops, __fops##_get, __fops##_set, "%llu\n")

static long do_lockup_livelock_irq(void *info)
{
	pr_warn("About to live lock after spin_lock_irq\n");
	spin_lock_irq(&lockup_lock);
	while (1)
		cpu_relax();
	spin_unlock_irq(&lockup_lock);
	return 0;
}

DEFINE_LOCKUP_ATTRIBUTE(lockup_livelock_irq_fops, do_lockup_livelock_irq);


static long do_lockup_spin_lock(void *info)
{
	pr_warn("About to wedge in spin_lock\n");
	spin_lock(&lockup_lock);
	debug_locks_off(); /* we plan to deadlock here */
	spin_lock(&lockup_lock);
	spin_unlock(&lockup_lock);
	spin_unlock(&lockup_lock);
	return 0;
}
DEFINE_LOCKUP_ATTRIBUTE(lockup_spin_lock_fops, do_lockup_spin_lock);

static long do_lockup_spin_lock_irqsave(void *info)
{
	unsigned long flags1, flags2;

	pr_warn("About to wedge in spin_lock_irqsave\n");
	spin_lock_irqsave(&lockup_lock, flags1);
	debug_locks_off(); /* we plan to deadlock here... */
	spin_lock_irqsave(&lockup_lock, flags2);
	spin_unlock_irqrestore(&lockup_lock, flags2);
	spin_unlock_irqrestore(&lockup_lock, flags1);
	return 0;
}
DEFINE_LOCKUP_ATTRIBUTE(lockup_spin_lock_irqsave_fops,
			do_lockup_spin_lock_irqsave);

static void infinite_loop_holding_a_spinlock(struct irq_work *w)
{
	unsigned long flags;

	pr_warn("About to live lock after spin_lock_irqsave\n");
	spin_lock_irqsave(&lockup_lock, flags);
	while (1)
		cpu_relax();
	spin_unlock_irqrestore(&lockup_lock, flags);
}
DEFINE_IRQ_WORK(infinite_loop_holding_a_spinlock_work,
		infinite_loop_holding_a_spinlock);

static void stuck_waiting_for_spinlock(struct irq_work *w)
{
	unsigned long flags;

	pr_warn("About to wedge in spin_lock_irqsave\n");
	spin_lock_irqsave(&lockup_lock, flags);
	spin_unlock_irqrestore(&lockup_lock, flags);
}
DEFINE_IRQ_WORK(stuck_waiting_for_spinlock_work,
		stuck_waiting_for_spinlock);

#ifdef CONFIG_SMP
static int lockup_wedge_get(void *data, u64 *val)
{
	pr_info("About to run infinite_loop_holding_a_spinlock on cpu 2\n");
	irq_work_queue_on(&infinite_loop_holding_a_spinlock_work, 2);
	mdelay(20);
	pr_info("About to run stuck_waiting_for_spinlock on cpu 3\n");
	irq_work_queue_on(&stuck_waiting_for_spinlock_work, 3);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(lockup_wedge_fops, lockup_wedge_get, NULL, "%llu\n");
#endif

static int __init lockup_init(void)
{
#define E(x) { #x, &lockup_##x##_fops }
	const struct {
		const char *name;
		const struct file_operations *fops;
	} fops_table[] = {
		E(livelock_irq),
		E(spin_lock),
		E(spin_lock_irqsave),
#ifdef CONFIG_SMP
		E(wedge),
#endif
	};
#undef E

	struct dentry *dir;
	unsigned int i;

	dir = debugfs_create_dir("lockup", NULL);
	if (dir)
		for (i = 0; i < ARRAY_SIZE(fops_table); i++)
			(void)debugfs_create_file(fops_table[i].name,
						  S_IRUGO | S_IWUSR, dir, NULL,
						  fops_table[i].fops);

	pr_info("created attributes\n");
	return 0;
}

module_init(lockup_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Thompson");
