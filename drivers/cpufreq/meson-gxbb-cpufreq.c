/*
 * System Control and Power Interface (SCPI) based Meson GXBB Cpufreq driver.
 *
 * Copyright (C) 2016 BayLibre, SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * Based on scpi-cpufreq.c from :
 * Copyright (C) 2015 ARM Ltd.
 * Sudeep Holla <sudeep.holla@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/export.h>
#include <linux/pm_opp.h>
#include <linux/scpi_protocol.h>

struct meson_gxbb_cpufreq {
	struct platform_device *pdev;
	struct clk *armclk;
	struct scpi_ops *scpi_ops;
	struct scpi_dvfs_info *info;
	struct cpufreq_driver drv;
	struct cpufreq_frequency_table *freq_table;
};

static struct meson_gxbb_cpufreq *cpufreq;

int meson_gxbb_cpufreq_target_index(struct cpufreq_policy *policy,
		unsigned int index)
{
	struct device *cpu_dev;
	unsigned int new_freq;
	unsigned long freq_hz;
	int ret;

	if (!cpufreq)
		return -ENODEV;
	
	cpu_dev = get_cpu_device(0);
	if (!cpu_dev) {
		pr_err("failed to get cpu0 device\n");
		return -ENODEV;
	}

	new_freq = cpufreq->freq_table[index].frequency;
	freq_hz = new_freq * 1000;

	ret = clk_set_rate(cpufreq->armclk, new_freq * 1000);
	if (ret) {
		dev_err(cpu_dev, "failed to set clock rate: %d\n", ret);
		return ret;
	}

	return 0;
}

int meson_gxbb_cpufreq_init(struct cpufreq_policy *policy)
{
	if (!cpufreq)
		return -ENODEV;

	policy->clk = cpufreq->armclk;
	return cpufreq_generic_init(policy, cpufreq->freq_table,
					cpufreq->info->latency);
}

static int meson_gxbb_cpufreq_probe(struct platform_device *pdev)
{
	struct device *cpu_dev;
	u8 domain = 0;
	int ret, i;
	
	cpu_dev = get_cpu_device(0);
	if (!cpu_dev) {
		pr_err("failed to get cpu0 device\n");
		return -ENODEV;
	}

	cpufreq = devm_kzalloc(&pdev->dev, sizeof(*cpufreq), GFP_KERNEL);
	if (!cpufreq)
		return -ENOMEM;

	cpufreq->pdev = pdev;

	cpufreq->scpi_ops = of_scpi_ops_get(of_get_parent(pdev->dev.of_node));
	if (IS_ERR(cpufreq->scpi_ops))
		return PTR_ERR(cpufreq->scpi_ops);

	cpufreq->armclk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(cpufreq->armclk))
		return PTR_ERR(cpufreq->armclk);

	cpufreq->info = cpufreq->scpi_ops->dvfs_get_info(domain);
	if (IS_ERR(cpufreq->info))
		return PTR_ERR(cpufreq->info);

	/* Load OPPs */
	for (i = 0 ; i < cpufreq->info->count ; ++i) {
		unsigned int f = cpufreq->info->opps[i].freq;
		unsigned int v = cpufreq->info->opps[i].m_volt * 1000;

		ret = dev_pm_opp_add(cpu_dev, f, v);
		if (ret)
			return ret;
	}

	ret = dev_pm_opp_init_cpufreq_table(cpu_dev, &cpufreq->freq_table);
	if (ret) {
		dev_err(cpu_dev, "failed to init cpufreq table: %d\n", ret);
		return ret;
	}

	cpufreq->drv.flags = CPUFREQ_STICKY;
	cpufreq->drv.verify = cpufreq_generic_frequency_table_verify;
	cpufreq->drv.target_index = meson_gxbb_cpufreq_target_index;
	cpufreq->drv.get = cpufreq_generic_get;
	cpufreq->drv.init = meson_gxbb_cpufreq_init;
	strcpy(cpufreq->drv.name, "meson_gxbb_cpufreq");
	cpufreq->drv.attr = cpufreq_generic_attr;
	cpufreq->drv.driver_data = cpufreq;

	return cpufreq_register_driver(&cpufreq->drv);
}

static int meson_gxbb_cpufreq_remove(struct platform_device *pdev)
{
	struct device *cpu_dev = get_cpu_device(0);

	if (cpu_dev) {
		dev_pm_opp_free_cpufreq_table(cpu_dev, &cpufreq->freq_table);

		dev_pm_opp_of_remove_table(cpu_dev);
	}

	clk_put(cpufreq->armclk);

	return cpufreq_unregister_driver(&cpufreq->drv);
}

static const struct of_device_id meson_gxbb_cpufreq_of_match[] = {
	{.compatible = "amlogic,meson-gxbb-cpufreq"},
	{},
};

MODULE_DEVICE_TABLE(of, meson_gxbb_cpufreq_of_match);

static struct platform_driver meson_gxbb_cpufreq_driver = {
	.driver = {
		.name	 = "cpufreq-meson-gxbb",
		.of_match_table = meson_gxbb_cpufreq_of_match,
	},
	.probe = meson_gxbb_cpufreq_probe,
	.remove = meson_gxbb_cpufreq_remove,
};

module_platform_driver(meson_gxbb_cpufreq_driver);
