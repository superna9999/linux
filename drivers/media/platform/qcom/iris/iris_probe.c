// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>

#include "iris_core.h"
#include "iris_vidc.h"

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

	core->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(core->reg_base))
		return PTR_ERR(core->reg_base);

	core->irq = platform_get_irq(pdev, 0);
	if (core->irq < 0)
		return core->irq;

	core->iris_platform_data = of_device_get_match_data(core->dev);
	if (!core->iris_platform_data) {
		ret = -ENODEV;
		dev_err_probe(core->dev, ret, "init platform failed\n");
		return ret;
	}

	iris_init_ops(core);
	ret = iris_init_resources(core);
	if (ret) {
		dev_err_probe(core->dev, ret, "init resource failed\n");
		return ret;
	}

	ret = v4l2_device_register(dev, &core->v4l2_dev);
	if (ret)
		return ret;

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

	return ret;
}

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
	},
};

module_platform_driver(qcom_iris_driver);
MODULE_DESCRIPTION("Qualcomm Iris video driver");
MODULE_LICENSE("GPL");
