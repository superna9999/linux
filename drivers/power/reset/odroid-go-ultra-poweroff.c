// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2022 Neil Armstrong <narmstrong@kernel.org>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/mfd/rk808.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/i2c.h>

static struct rk808 *rk817;
static struct rk808 *rk818;

static void odroid_go_ultra_do_poweroff(void)
{
	int ret;

	BUG_ON(!rk817);
	BUG_ON(!rk818);

	/* RK817 */
	ret = regmap_update_bits(rk817->regmap, RK817_SYS_CFG(3), DEV_OFF, DEV_OFF);
	if (ret)
		pr_err("%s: failed to poweroff rk817\n", __func__);

	/* RK818 */
	ret = regmap_update_bits(rk818->regmap, RK818_DEVCTRL_REG, DEV_OFF, DEV_OFF);
	if (ret)
		pr_err("%s: failed to poweroff rk817\n", __func__);

	WARN_ON(1);
}

static int odroid_go_ultra_poweroff_get_pmic_drvdata(struct platform_device *pdev,
						     const char *property,
						     struct rk808 **pmic)
{
	struct device_node *pmic_node;
	struct i2c_client *pmic_client;

	pmic_node = of_parse_phandle(pdev->dev.of_node, property, 0);
	if (!pmic_node)
		return -ENODEV;

	pmic_client = of_find_i2c_device_by_node(pmic_node);
	of_node_put(pmic_node);
	if (!pmic_client)
		return -EPROBE_DEFER;

	*pmic = i2c_get_clientdata(pmic_client);

	put_device(&pmic_client->dev);

	if (!*pmic)
		return -EPROBE_DEFER;

	return 0;
}

static int odroid_go_ultra_poweroff_probe(struct platform_device *pdev)
{
	int ret;

	/* RK818 */
	ret = odroid_go_ultra_poweroff_get_pmic_drvdata(pdev, "hardkernel,rk818-pmic", &rk818);
	if (ret) {
		dev_err(&pdev->dev, "failed to get rk818 mfd data (%d)\n", ret);
		return ret;
	}

	/* RK817 */
	ret = odroid_go_ultra_poweroff_get_pmic_drvdata(pdev, "hardkernel,rk817-pmic", &rk817);
	if (ret) {
		dev_err(&pdev->dev, "failed to get rk817 mfd data (%d)\n", ret);
		return ret;
	}

	/* If a pm_power_off function has already been added, warn we're overriding */
	if (pm_power_off != NULL)
		dev_warn(&pdev->dev,
			"%s: pm_power_off function already registered, overriding\n",
		       __func__);

	pm_power_off = odroid_go_ultra_do_poweroff;

	return 0;
}

static int odroid_go_ultra_poweroff_remove(struct platform_device *pdev)
{
	if (pm_power_off == &odroid_go_ultra_do_poweroff)
		pm_power_off = NULL;

	rk817 = NULL;
	rk818 = NULL;

	return 0;
}

static const struct of_device_id of_odroid_go_ultra_poweroff_match[] = {
	{ .compatible = "hardkernel,odroid-go-ultra-poweroff", },
	{},
};
MODULE_DEVICE_TABLE(of, of_odroid_go_ultra_poweroff_match);

static struct platform_driver odroid_go_ultra_poweroff_driver = {
	.probe = odroid_go_ultra_poweroff_probe,
	.remove = odroid_go_ultra_poweroff_remove,
	.driver = {
		.name = "odroid-go-ultra-poweroff",
		.of_match_table = of_odroid_go_ultra_poweroff_match,
	},
};

module_platform_driver(odroid_go_ultra_poweroff_driver);

MODULE_AUTHOR("Neil Armstrong <narmstrong@kernel.org>");
MODULE_DESCRIPTION("Odroid Go Ultra poweroff driver");
MODULE_LICENSE("GPL v2");
