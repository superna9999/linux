// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/pm_runtime.h>

#include "iris_core.h"
#include "iris_ctrls.h"
#include "iris_vidc.h"

static inline int iris_init_isr(struct iris_core *core)
{
	int ret;

	ret = devm_request_threaded_irq(core->dev, core->irq, iris_hfi_isr,
					iris_hfi_isr_handler, IRQF_TRIGGER_HIGH, "iris", core);
	if (ret) {
		dev_err(core->dev, "failed to allocate irq\n");
		return ret;
	}
	disable_irq_nosync(core->irq);

	return ret;
}

static int iris_register_video_device(struct iris_core *core)
{
	struct video_device *vdev;
	int ret;

	vdev = video_device_alloc();
	if (!vdev)
		return -ENOMEM;

	strscpy(vdev->name, "qcom-iris-decoder", sizeof(vdev->name));
	vdev->release = video_device_release;
	vdev->fops = core->iris_v4l2_file_ops;
	vdev->ioctl_ops = core->iris_v4l2_ioctl_ops;
	vdev->vfl_dir = VFL_DIR_M2M;
	vdev->v4l2_dev = &core->v4l2_dev;
	vdev->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto err_vdev_release;

	core->vdev_dec = vdev;
	video_set_drvdata(vdev, core);

	return 0;

err_vdev_release:
	video_device_release(vdev);

	return ret;
}

static void iris_remove(struct platform_device *pdev)
{
	struct iris_core *core;

	core = platform_get_drvdata(pdev);
	if (!core)
		return;

	iris_core_deinit(core);

	video_unregister_device(core->vdev_dec);

	v4l2_device_unregister(&core->v4l2_dev);

	mutex_destroy(&core->lock);
	core->state = IRIS_CORE_DEINIT;
}

static void iris_sys_error_handler(struct work_struct *work)
{
	struct iris_core *core =
			container_of(work, struct iris_core, sys_error_handler.work);

	iris_core_deinit(core);
	iris_core_init(core);
}

static int iris_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iris_core *core;
	u64 dma_mask;
	int ret;

	core = devm_kzalloc(&pdev->dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;
	core->dev = dev;

	core->state = IRIS_CORE_DEINIT;
	mutex_init(&core->lock);
	init_completion(&core->core_init_done);

	core->response_packet = devm_kzalloc(core->dev, IFACEQ_CORE_PKT_SIZE, GFP_KERNEL);
	if (!core->response_packet)
		return -ENOMEM;

	INIT_LIST_HEAD(&core->instances);
	INIT_DELAYED_WORK(&core->sys_error_handler, iris_sys_error_handler);

	core->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(core->reg_base))
		return PTR_ERR(core->reg_base);

	core->irq = platform_get_irq(pdev, 0);
	if (core->irq < 0)
		return core->irq;

	pm_runtime_set_autosuspend_delay(core->dev, AUTOSUSPEND_DELAY_VALUE);
	pm_runtime_use_autosuspend(core->dev);
	ret = devm_pm_runtime_enable(core->dev);
	if (ret) {
		dev_err(core->dev, "failed to enable runtime pm\n");
		goto err_runtime_disable;
	}

	ret = iris_init_isr(core);
	if (ret) {
		dev_err_probe(core->dev, ret, "Failed to init isr\n");
		goto err_runtime_disable;
	}

	core->iris_platform_data = of_device_get_match_data(core->dev);
	if (!core->iris_platform_data) {
		ret = -ENODEV;
		dev_err_probe(core->dev, ret, "init platform failed\n");
		goto err_runtime_disable;
	}

	iris_init_ops(core);
	core->iris_platform_data->init_hfi_command_ops(core);
	core->iris_platform_data->init_hfi_response_ops(core);

	ret = iris_init_resources(core);
	if (ret) {
		dev_err_probe(core->dev, ret, "init resource failed\n");
		goto err_runtime_disable;
	}

	ret = iris_session_init_caps(core);
	if (ret) {
		dev_err_probe(core->dev, ret, "init inst caps failed\n");
		goto err_runtime_disable;
	}

	ret = v4l2_device_register(dev, &core->v4l2_dev);
	if (ret)
		goto err_runtime_disable;

	ret = iris_register_video_device(core);
	if (ret)
		goto err_v4l2_unreg;

	platform_set_drvdata(pdev, core);

	dma_mask = core->iris_platform_data->dma_mask;

	ret = dma_set_mask_and_coherent(dev, dma_mask);
	if (ret)
		goto err_vdev_unreg;

	dma_set_max_seg_size(&pdev->dev, DMA_BIT_MASK(32));
	dma_set_seg_boundary(&pdev->dev, DMA_BIT_MASK(64));

	return 0;

err_vdev_unreg:
	video_unregister_device(core->vdev_dec);
err_v4l2_unreg:
	v4l2_device_unregister(&core->v4l2_dev);
err_runtime_disable:
	pm_runtime_set_suspended(core->dev);

	return ret;
}

static int iris_pm_suspend(struct device *dev)
{
	struct iris_core *core;
	int ret;

	if (!dev || !dev->driver)
		return 0;

	core = dev_get_drvdata(dev);

	mutex_lock(&core->lock);
	ret = iris_hfi_pm_suspend(core);
	mutex_unlock(&core->lock);

	return ret;
}

static int iris_pm_resume(struct device *dev)
{
	struct iris_core *core;
	int ret = 0;

	if (!dev || !dev->driver)
		return 0;

	core = dev_get_drvdata(dev);

	mutex_lock(&core->lock);
	if (core->state != IRIS_CORE_INIT)
		goto exit;

	ret = iris_hfi_pm_resume(core);
	pm_runtime_mark_last_busy(core->dev);

exit:
	mutex_unlock(&core->lock);

	return ret;
}

static const struct dev_pm_ops iris_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(iris_pm_suspend, iris_pm_resume, NULL)
};

static const struct of_device_id iris_dt_match[] = {
	{
		.compatible = "qcom,sm8550-iris",
		.data = &sm8550_data,
	},
	{
		.compatible = "qcom,sm8250-venus",
		.data = &sm8250_data,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, iris_dt_match);

static struct platform_driver qcom_iris_driver = {
	.probe = iris_probe,
	.remove_new = iris_remove,
	.driver = {
		.name = "qcom-iris",
		.of_match_table = iris_dt_match,
		.pm = &iris_pm_ops,
	},
};

module_platform_driver(qcom_iris_driver);
MODULE_DESCRIPTION("Qualcomm Iris video driver");
MODULE_LICENSE("GPL");
