// SPDX-License-Identifier: GPL-2.0
/* Copyright 2018 Marty E. Plummer <hanetzer@startmail.com> */
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */

#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include "panfrost_device.h"
#include "panfrost_gpu.h"
#include "panfrost_job.h"
#include "panfrost_mmu.h"

static int panfrost_reset_init(struct panfrost_device *pfdev)
{
	int err;

	pfdev->rstc = devm_reset_control_array_get(pfdev->dev, false, true);
	if (IS_ERR(pfdev->rstc)) {
		dev_err(pfdev->dev, "get reset failed %ld\n", PTR_ERR(pfdev->clock));
		return PTR_ERR(pfdev->rstc);
	}

	err = reset_control_deassert(pfdev->rstc);
	if (err)
		return err;

	return 0;
}

static void panfrost_reset_fini(struct panfrost_device *pfdev)
{
	reset_control_assert(pfdev->rstc);
}

static int panfrost_clk_init(struct panfrost_device *pfdev)
{
	int err;
	unsigned long rate;

	pfdev->clock = devm_clk_get(pfdev->dev, NULL);
	if (IS_ERR(pfdev->clock)) {
		dev_err(pfdev->dev, "get clock failed %ld\n", PTR_ERR(pfdev->clock));
		return PTR_ERR(pfdev->clock);
	}

	rate = clk_get_rate(pfdev->clock);
	dev_info(pfdev->dev, "clock rate = %lu\n", rate);

	err = clk_prepare_enable(pfdev->clock);
	if (err)
		return err;

	return 0;
}

static void panfrost_clk_fini(struct panfrost_device *pfdev)
{
	clk_disable_unprepare(pfdev->clock);
}

static int panfrost_regulator_init(struct panfrost_device *pfdev)
{
	int ret;

	pfdev->regulator = devm_regulator_get_optional(pfdev->dev, "mali");
	if (IS_ERR(pfdev->regulator)) {
		ret = PTR_ERR(pfdev->regulator);
		pfdev->regulator = NULL;
		if (ret == -ENODEV)
			return 0;
		dev_err(pfdev->dev, "failed to get regulator: %ld\n", PTR_ERR(pfdev->regulator));
		return ret;
	}

	ret = regulator_enable(pfdev->regulator);
	if (ret < 0) {
		dev_err(pfdev->dev, "failed to enable regulator: %d\n", ret);
		return ret;
	}

	return 0;
}

static void panfrost_regulator_fini(struct panfrost_device *pfdev)
{
	if (pfdev->regulator)
		regulator_disable(pfdev->regulator);
}


int panfrost_device_init(struct panfrost_device *pfdev)
{
	int err;
	struct resource *res;

	mutex_init(&pfdev->sched_lock);
	INIT_LIST_HEAD(&pfdev->scheduled_jobs);

	err = panfrost_clk_init(pfdev);
	if (err) {
		dev_err(pfdev->dev, "clk init failed %d\n", err);
		return err;
	}

	err = panfrost_regulator_init(pfdev);
	if (err) {
		dev_err(pfdev->dev, "regulator init failed %d\n", err);
		goto err_out0;
	}

	err = panfrost_reset_init(pfdev);
	if (err) {
		dev_err(pfdev->dev, "reset init failed %d\n", err);
		goto err_out1;
	}

	res = platform_get_resource(pfdev->pdev, IORESOURCE_MEM, 0);
	pfdev->iomem = devm_ioremap_resource(pfdev->dev, res);
	if (IS_ERR(pfdev->iomem)) {
		dev_err(pfdev->dev, "failed to ioremap iomem\n");
		err = PTR_ERR(pfdev->iomem);
		goto err_out2;
	}

	err = panfrost_gpu_init(pfdev);
	if (err)
		goto err_out2;

	err = panfrost_mmu_init(pfdev);
	if (err)
		goto err_out3;

	err = panfrost_job_init(pfdev);
	if (err)
		goto err_out4;

	return 0;
err_out4:
	panfrost_mmu_fini(pfdev);
err_out3:
	panfrost_gpu_fini(pfdev);
err_out2:
	panfrost_reset_fini(pfdev);
err_out1:
	panfrost_regulator_fini(pfdev);
err_out0:
	panfrost_clk_fini(pfdev);
	return err;
}

void panfrost_device_fini(struct panfrost_device *pfdev)
{
	panfrost_job_fini(pfdev);
	panfrost_mmu_fini(pfdev);
	panfrost_gpu_fini(pfdev);
	panfrost_regulator_fini(pfdev);

	panfrost_clk_fini(pfdev);
}
