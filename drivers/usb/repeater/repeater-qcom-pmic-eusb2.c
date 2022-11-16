// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2022, Linaro Limited
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <linux/usb/repeater.h>

/* eUSB2 status registers */
#define EUSB2_RPTR_STATUS		0x08
#define	RPTR_OK				BIT(7)

/* eUSB2 control registers */
#define EUSB2_EN_CTL1			0x46
#define EUSB2_RPTR_EN			BIT(7)

#define PHY_HOST_MODE			BIT(0)
#define EUSB2_FORCE_EN_5		0xE8
#define F_CLK_19P2M_EN			BIT(6)
#define F_CLK_19P2M_EN_SHIFT		6

#define EUSB2_FORCE_VAL_5		0xED
#define V_CLK_19P2M_EN			BIT(6)
#define V_CLK_19P2M_EN_SHIFT		6

#define EUSB2_TUNE_IUSB2		0x51
#define EUSB2_TUNE_SQUELCH_U		0x54
#define EUSB2_TUNE_USB2_PREEM		0x57

#define QCOM_EUSB2_REPEATER_INIT_CFG(o, v)	\
	{					\
		.offset = o,			\
		.val = v,			\
	}

#define to_qcom_eusb2_repeater(x) \
	container_of((x), struct qcom_eusb2_repeater, repeater)

struct qcom_eusb2_repeater_init_tbl {
	unsigned int offset;
	unsigned int val;
};

struct qcom_eusb2_repeater {
	struct usb_repeater	repeater;
	struct regmap		*regmap;
	u16			base;
	struct regulator_bulk_data *vregs;
	const struct qcom_eusb2_repeater_cfg *cfg;
};

struct qcom_eusb2_repeater_cfg {
	const struct qcom_eusb2_repeater_init_tbl *init_tbl;
	int init_tbl_num;
	/* regulators to be requested */
	const char * const *vreg_list;
	int num_vregs;
};

static const char * const pm8550b_vreg_l[] = {
	"vdd18", "vdd3",
};

static const struct qcom_eusb2_repeater_init_tbl pm8550b_init_tbl[] = {
	QCOM_EUSB2_REPEATER_INIT_CFG(EUSB2_TUNE_IUSB2, 0x8),
	QCOM_EUSB2_REPEATER_INIT_CFG(EUSB2_TUNE_SQUELCH_U, 0x3),
	QCOM_EUSB2_REPEATER_INIT_CFG(EUSB2_TUNE_USB2_PREEM, 0x5),
};

static const struct qcom_eusb2_repeater_cfg pm8550b_eusb2_cfg = {
	.init_tbl	= pm8550b_init_tbl,
	.init_tbl_num	= ARRAY_SIZE(pm8550b_init_tbl),
	.vreg_list	= pm8550b_vreg_l,
	.num_vregs	= ARRAY_SIZE(pm8550b_vreg_l),
};

static int qcom_eusb2_repeater_init_vregs(struct qcom_eusb2_repeater *rptr)
{
	int num = rptr->cfg->num_vregs;
	struct device *dev = rptr->repeater.dev;
	int i;

	rptr->vregs = devm_kcalloc(dev, num, sizeof(*rptr->vregs), GFP_KERNEL);
	if (!rptr->vregs)
		return -ENOMEM;

	for (i = 0; i < num; i++)
		rptr->vregs[i].supply = rptr->cfg->vreg_list[i];

	return devm_regulator_bulk_get(dev, num, rptr->vregs);
}

static int qcom_eusb2_repeater_cfg_init(struct qcom_eusb2_repeater *rptr)
{
	const struct qcom_eusb2_repeater_init_tbl *init_tbl = rptr->cfg->init_tbl;
	int num = rptr->cfg->init_tbl_num;
	int i;

	for (i = 0; i < num; i++)
		regmap_update_bits(rptr->regmap, rptr->base + init_tbl[i].offset,
					init_tbl[i].val, init_tbl[i].val);
	return 0;
}

static int qcom_eusb2_repeater_init(struct usb_repeater *r)
{
	struct qcom_eusb2_repeater *rptr = to_qcom_eusb2_repeater(r);
	int ret = 0;
	u32 val;

	qcom_eusb2_repeater_cfg_init(rptr);

	if (r->flags & PHY_HOST_MODE) {
		/*
		 * CM.Lx is prohibited when repeater is already into Lx state as
		 * per eUSB 1.2 Spec. Below implement software workaround until
		 * PHY and controller is fixing seen observation.
		 */
		regmap_update_bits(rptr->regmap, rptr->base + EUSB2_FORCE_EN_5,
				F_CLK_19P2M_EN, F_CLK_19P2M_EN);
		regmap_update_bits(rptr->regmap, rptr->base + EUSB2_FORCE_VAL_5,
				V_CLK_19P2M_EN, V_CLK_19P2M_EN);
	} else {
		/*
		 * In device mode clear host mode related workaround as there
		 * is no repeater reset available, and enable/disable of
		 * repeater doesn't clear previous value due to shared
		 * regulators (say host <-> device mode switch).
		 */
		regmap_update_bits(rptr->regmap, rptr->base + EUSB2_FORCE_EN_5,
				F_CLK_19P2M_EN, 0);
		regmap_update_bits(rptr->regmap, rptr->base + EUSB2_FORCE_VAL_5,
				V_CLK_19P2M_EN, 0);
	}

	ret = regmap_read_poll_timeout(rptr->regmap, rptr->base + EUSB2_RPTR_STATUS, val,
					val & RPTR_OK, 10, 5);
	if (ret)
		dev_err(r->dev, "initialization timed-out\n");

	return ret;
}

static int qcom_eusb2_repeater_reset(struct usb_repeater *r, bool assert)
{
	struct qcom_eusb2_repeater *rptr = to_qcom_eusb2_repeater(r);

	return regmap_update_bits(rptr->regmap, rptr->base + EUSB2_EN_CTL1,
				EUSB2_RPTR_EN, assert ? EUSB2_RPTR_EN : 0x0);
}

static int qcom_eusb2_repeater_power_on(struct usb_repeater *r)
{
	struct qcom_eusb2_repeater *rptr = to_qcom_eusb2_repeater(r);

	return regulator_bulk_enable(rptr->cfg->num_vregs, rptr->vregs);
}

static int qcom_eusb2_repeater_power_off(struct usb_repeater *r)
{
	struct qcom_eusb2_repeater *rptr = to_qcom_eusb2_repeater(r);

	return regulator_bulk_disable(rptr->cfg->num_vregs, rptr->vregs);
}

static int qcom_eusb2_repeater_probe(struct platform_device *pdev)
{
	struct qcom_eusb2_repeater *rptr;
	struct device *dev = &pdev->dev;
	struct device_node *node;
	u32 res;
	int ret;

	node = dev->of_node;

	rptr = devm_kzalloc(dev, sizeof(*rptr), GFP_KERNEL);
	if (!rptr)
		return -ENOMEM;

	rptr->repeater.dev = dev;
	dev_set_drvdata(dev, rptr);

	/* Get the specific init parameters of QMP phy */
	rptr->cfg = of_device_get_match_data(dev);
	if (!rptr->cfg)
		return -EINVAL;

	rptr->regmap = dev_get_regmap(dev->parent, NULL);
	if (!rptr->regmap)
		return -ENXIO;

	ret = of_property_read_u32(node, "reg", &res);
	if (ret < 0)
		return ret;

	rptr->base = res;

	ret = qcom_eusb2_repeater_init_vregs(rptr);
	if (ret < 0) {
		dev_err(dev, "unable to get supplies\n");
		return ret;
	}

	rptr->repeater.init		= qcom_eusb2_repeater_init;
	rptr->repeater.reset		= qcom_eusb2_repeater_reset;
	rptr->repeater.power_on		= qcom_eusb2_repeater_power_on;
	rptr->repeater.power_off	= qcom_eusb2_repeater_power_off;

	return usb_add_repeater_dev(&rptr->repeater);
}

static int qcom_eusb2_repeater_remove(struct platform_device *pdev)
{
	struct qcom_eusb2_repeater *rptr = platform_get_drvdata(pdev);

	if (!rptr)
		return 0;

	usb_remove_repeater_dev(&rptr->repeater);
	qcom_eusb2_repeater_power_off(&rptr->repeater);
	return 0;
}

static const struct of_device_id qcom_eusb2_repeater_id_table[] = {
	{
		.compatible = "qcom,pm8550b-eusb2-repeater",
		.data = &pm8550b_eusb2_cfg,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, qcom_eusb2_repeater_id_table);

static struct platform_driver qcom_eusb2_repeater_driver = {
	.probe		= qcom_eusb2_repeater_probe,
	.remove		= qcom_eusb2_repeater_remove,
	.driver = {
		.name	= "qcom-pmic-eusb2-repeater",
		.of_match_table = of_match_ptr(qcom_eusb2_repeater_id_table),
	},
};

module_platform_driver(qcom_eusb2_repeater_driver);
MODULE_DESCRIPTION("Qualcomm PMIC eUSB2 repeater driver");
MODULE_LICENSE("GPL");
