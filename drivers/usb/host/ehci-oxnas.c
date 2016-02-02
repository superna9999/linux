/*
 * drivers/usb/host/ehci-oxnas.c
 *
 * Tzachi Perelstein <tzachi@marvell.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/dma-mapping.h>
#include <linux/gpio/consumer.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "ehci.h"

struct oxnas_hcd {
	struct clk *clk;
	struct reset_control *rst_host;
	struct reset_control *rst_phy;
	struct regmap *regmap;
	unsigned polarity_invert;
	struct gpio_desc *hub_reset;
};

#define USBHSPHY_SUSPENDM_MANUAL_ENABLE    16
#define USBHSPHY_SUSPENDM_MANUAL_STATE     15
#define USBHSPHY_ATE_ESET                  14
#define USBHSPHY_TEST_DIN                   6
#define USBHSPHY_TEST_ADD                   2
#define USBHSPHY_TEST_DOUT_SEL              1
#define USBHSPHY_TEST_CLK                   0

#define USBHSPHY_CTRL_REGOFFSET		0x84

#define USBHSMPH_IP_POL_A_BIT  0
#define USBHSMPH_IP_POL_B_BIT  1
#define USBHSMPH_IP_POL_C_BIT  2
#define USBHSMPH_OP_POL_A_BIT  3
#define USBHSMPH_OP_POL_B_BIT  4
#define USBHSMPH_OP_POL_C_BIT  5

#define USBHSMPH_CTRL_REGOFFSET		0x40

#define DRIVER_DESC "Oxnas On-Chip EHCI Host Controller"

static struct hc_driver __read_mostly oxnas_hc_driver;

static void start_oxnas_usb_ehci(struct oxnas_hcd *oxnas)
{
	u32 reg;

	if (oxnas->hub_reset) {
		gpiod_direction_output(oxnas->hub_reset, 0);
		msleep(10);
		gpiod_direction_output(oxnas->hub_reset, 1);
	}

	if (oxnas->polarity_invert) {
		reg = ((1UL << USBHSMPH_IP_POL_A_BIT) |
                       (1UL << USBHSMPH_IP_POL_B_BIT) |
                       (1UL << USBHSMPH_IP_POL_C_BIT))|
		      ((1UL << USBHSMPH_OP_POL_A_BIT) |
                       (1UL << USBHSMPH_OP_POL_B_BIT) |
                       (1UL << USBHSMPH_OP_POL_C_BIT))|
		      BIT(6);
		regmap_write(oxnas->regmap, USBHSMPH_CTRL_REGOFFSET, reg);
	}

	/* Ensure the USB block is properly reset */
	if (!IS_ERR(oxnas->rst_host))
		reset_control_reset(oxnas->rst_host);
	
	if (!IS_ERR(oxnas->rst_phy))
		reset_control_reset(oxnas->rst_phy);

	/* Force the high speed clock to be generated all the time, via serial
	 programming of the USB HS PHY */
	reg = (2UL << USBHSPHY_TEST_ADD) | (0xe0UL << USBHSPHY_TEST_DIN);
	regmap_write(oxnas->regmap, USBHSPHY_CTRL_REGOFFSET, reg);

	reg = (1UL << USBHSPHY_TEST_CLK) | (2UL << USBHSPHY_TEST_ADD) |
	      (0xe0UL << USBHSPHY_TEST_DIN);
	regmap_write(oxnas->regmap, USBHSPHY_CTRL_REGOFFSET, reg);

	reg = (0xfUL << USBHSPHY_TEST_ADD) | (0xaaUL << USBHSPHY_TEST_DIN);
	regmap_write(oxnas->regmap, USBHSPHY_CTRL_REGOFFSET, reg);

	reg = (1UL << USBHSPHY_TEST_CLK) | (0xfUL << USBHSPHY_TEST_ADD) |
	      (0xaaUL << USBHSPHY_TEST_DIN);
	regmap_write(oxnas->regmap, USBHSPHY_CTRL_REGOFFSET, reg);

	/* Enable the clock to the USB block */
	clk_prepare_enable(oxnas->clk);
}

static void stop_oxnas_usb_ehci(struct oxnas_hcd *oxnas)
{
	reset_control_assert(oxnas->rst_host);
	reset_control_assert(oxnas->rst_phy);

	clk_disable_unprepare(oxnas->clk);
}

static int ehci_oxnas_reset(struct usb_hcd *hcd)
{
	#define  txttfill_tuning	reserved2[0]

	struct ehci_hcd	*ehci;
	u32 tmp;
	int retval = ehci_setup(hcd);
	if (retval)
		return retval;

	ehci = hcd_to_ehci(hcd);
	tmp = ehci_readl(ehci, &ehci->regs->txfill_tuning);
	tmp &= ~0x00ff0000;
	tmp |= 0x003f0000; /* set burst pre load count to 0x40 (63 * 4 bytes)  */
	tmp |= 0x16; /* set sheduler overhead to 22 * 1.267us (HS) or 22 * 6.33us (FS/LS)*/
	ehci_writel(ehci, tmp,  &ehci->regs->txfill_tuning);

	tmp = ehci_readl(ehci, &ehci->regs->txttfill_tuning);
	tmp |= 0x2; /* set sheduler overhead to 2 * 6.333us */
	ehci_writel(ehci, tmp,  &ehci->regs->txttfill_tuning);

	return retval;
}

static int ehci_oxnas_drv_probe(struct platform_device *ofdev)
{
	struct device_node *np = ofdev->dev.of_node;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct resource res;
	struct oxnas_hcd *oxnas;
	int irq, err;
	struct reset_control *rstc;

	if (usb_disabled())
		return -ENODEV;

	if (!ofdev->dev.dma_mask)
		ofdev->dev.dma_mask = &ofdev->dev.coherent_dma_mask;
	if (!ofdev->dev.coherent_dma_mask)
		ofdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	hcd = usb_create_hcd(&oxnas_hc_driver,	&ofdev->dev,
					dev_name(&ofdev->dev));
	if (!hcd)
		return -ENOMEM;

	err = of_address_to_resource(np, 0, &res);
	if (err)
		goto err_res;

	hcd->rsrc_start = res.start;
	hcd->rsrc_len = resource_size(&res);

	hcd->regs = devm_ioremap_resource(&ofdev->dev, &res);
	if (IS_ERR(hcd->regs)) {
		dev_err(&ofdev->dev, "devm_ioremap_resource failed\n");
		err = PTR_ERR(hcd->regs);
		goto err_ioremap;
	}

	oxnas = (struct oxnas_hcd *)hcd_to_ehci(hcd)->priv;

	oxnas->regmap = syscon_regmap_lookup_by_phandle(np,
						       "plxtech,sys-ctrl");
	if (IS_ERR(oxnas->regmap)) {
		dev_err(&ofdev->dev, "failed to get sys ctrl regmap\n");
		return -ENODEV;
	}

	oxnas->hub_reset = devm_gpiod_get_optional(&ofdev->dev, "hub", 0);

	oxnas->polarity_invert = of_property_read_bool(np, "plxtech,polarity-invert");

	oxnas->clk = of_clk_get_by_name(np, "usb");
	if (IS_ERR(oxnas->clk)) {
		err = PTR_ERR(oxnas->clk);
		goto err_clk;
	}

	rstc = devm_reset_control_get(&ofdev->dev, "host");
	if (IS_ERR(rstc)) {
		err = PTR_ERR(rstc);
		goto err_rst;
	}
	oxnas->rst_host = rstc;

	rstc = devm_reset_control_get(&ofdev->dev, "phy");
	if (IS_ERR(rstc)) {
		err = PTR_ERR(rstc);
		goto err_rst;
	}
	oxnas->rst_phy = rstc;

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		dev_err(&ofdev->dev, "irq_of_parse_and_map failed\n");
		err = -EBUSY;
		goto err_irq;
	}

	hcd->has_tt = 1;
	ehci = hcd_to_ehci(hcd);
	ehci->caps = hcd->regs;

	start_oxnas_usb_ehci(oxnas);

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err)
		goto err_hcd;

	return 0;

err_hcd:
	stop_oxnas_usb_ehci(oxnas);
err_irq:
err_rst:
	clk_put(oxnas->clk);
err_clk:
err_ioremap:
err_res:
	usb_put_hcd(hcd);

	return err;
}

static int ehci_oxnas_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct oxnas_hcd *oxnas = (struct oxnas_hcd *)hcd_to_ehci(hcd)->priv;

	usb_remove_hcd(hcd);
	clk_disable_unprepare(oxnas->clk);
	usb_put_hcd(hcd);

	return 0;
}

static const struct of_device_id oxnas_ehci_dt_ids[] = {
	{ .compatible = "plxtech,nas782x-ehci" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, oxnas_ehci_dt_ids);

static struct platform_driver ehci_oxnas_driver = {
	.probe		= ehci_oxnas_drv_probe,
	.remove		= ehci_oxnas_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver.name	= "oxnas-ehci",
	.driver.of_match_table	= oxnas_ehci_dt_ids,
};

static const struct ehci_driver_overrides oxnas_overrides __initconst = {
	.reset = ehci_oxnas_reset,
	.extra_priv_size = sizeof(struct oxnas_hcd),
};

static int __init ehci_oxnas_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	ehci_init_driver(&oxnas_hc_driver, &oxnas_overrides);
	return platform_driver_register(&ehci_oxnas_driver);
}
module_init(ehci_oxnas_init);

static void __exit ehci_oxnas_cleanup(void)
{
	platform_driver_unregister(&ehci_oxnas_driver);
}
module_exit(ehci_oxnas_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:oxnas-ehci");
MODULE_LICENSE("GPL");
