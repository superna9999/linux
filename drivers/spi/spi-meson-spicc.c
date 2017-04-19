/*
 * Driver for Amlogic Meson SPI communication controller (SPICC)
 *
 * Copyright (C) BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/interrupt.h>

#define SPICC_MAX_FREQ	30000000

/* Register Map */
#define SPICC_RXDATA	0x00

#define SPICC_TXDATA	0x04

#define SPICC_CONREG	0x08
#define SPICC_ENABLE		BIT(0)
#define SPICC_MODE_MASTER	BIT(1)
#define SPICC_XCH		BIT(2)
#define SPICC_SMC		BIT(3)
#define SPICC_POL		BIT(4)
#define SPICC_PHA		BIT(5)
#define SPICC_SSCTL		BIT(6)
#define SPICC_SSPOL		BIT(7)
#define SPICC_DRCTL_MASK	GENMASK(9, 8)
#define SPICC_DRCTL_IGNORE	0
#define SPICC_DRCTL_FALLING	1
#define SPICC_DRCTL_LOWLEVEL	2
#define SPICC_CS_MASK		GENMASK(13, 12)
#define SPICC_DATARATE_MASK	GENMASK(18, 16)
#define SPICC_DATARATE_DIV4	0
#define SPICC_DATARATE_DIV8	1
#define SPICC_DATARATE_DIV16	2
#define SPICC_DATARATE_DIV32	3
#define SPICC_BITLENGTH_MASK	GENMASK(24, 19)
#define SPICC_BURSTLENGTH_MASK	GENMASK(31, 25)

#define SPICC_INTREG	0x0c
#define SPICC_TE_EN	BIT(0) /* TX Fifo Empty Interrupt */
#define SPICC_TH_EN	BIT(1) /* TX Fifo Half-Full Interrupt */
#define SPICC_TF_EN	BIT(2) /* TX Fifo Full Interrupt */
#define SPICC_RR_EN	BIT(3) /* RX Fifo Ready Interrupt */
#define SPICC_RH_EN	BIT(4) /* RX Fifo Half-Full Interrupt */
#define SPICC_RF_EN	BIT(5) /* RX Fifo Full Interrupt */
#define SPICC_RO_EN	BIT(6) /* RX Fifo Overflow Interrupt */
#define SPICC_TC_EN	BIT(7) /* Transfert Complete Interrupt */

#define SPICC_DMAREG	0x10
#define SPICC_DMA_ENABLE		BIT(0)
#define SPICC_TXFIFO_THRESHOLD_MASK	GENMASK(5, 1)
#define SPICC_RXFIFO_THRESHOLD_MASK	GENMASK(10, 6)
#define SPICC_READ_BURST_MASK		GENMASK(14, 11)
#define SPICC_WRITE_BURST_MASK		GENMASK(18, 15)
#define SPICC_DMA_URGENT		BIT(19)
#define SPICC_DMA_THREADID_MASK		GENMASK(25, 20)
#define SPICC_DMA_BURSTNUM_MASK		GENMASK(31, 26)

#define SPICC_STATREG	0x14
#define SPICC_TE	BIT(0) /* TX Fifo Empty Interrupt */
#define SPICC_TH	BIT(1) /* TX Fifo Half-Full Interrupt */
#define SPICC_TF	BIT(2) /* TX Fifo Full Interrupt */
#define SPICC_RR	BIT(3) /* RX Fifo Ready Interrupt */
#define SPICC_RH	BIT(4) /* RX Fifo Half-Full Interrupt */
#define SPICC_RF	BIT(5) /* RX Fifo Full Interrupt */
#define SPICC_RO	BIT(6) /* RX Fifo Overflow Interrupt */
#define SPICC_TC	BIT(7) /* Transfert Complete Interrupt */

#define SPICC_PERIODREG	0x18
#define SPICC_PERIOD	GENMASK(14, 0)	/* Wait cycles */

#define SPICC_TESTREG	0x1c
#define SPICC_TXCNT_MASK	GENMASK(4, 0)	/* TX Fifo Counter */
#define SPICC_RXCNT_MASK	GENMASK(9, 5)	/* RX Fifo Counter */
#define SPICC_SMSTATUS_MASK	GENMASK(12, 10)	/* State Machine Status */
#define SPICC_LBC_RO		BIT(13)	/* Loop Back Control - Read-Only */
#define SPICC_LBC_W1		BIT(14) /* Loop Back Control - Write-Only */
#define SPICC_SWAP_RO		BIT(14) /* RX Fifo Data Swap - Read-Only */
#define SPICC_SWAP_W1		BIT(15) /* RX Fifo Data Swap - Write-Only */
#define SPICC_DLYCTL_RO_MASK	GENMASK(20, 15) /* Delay Control - Read-Only */
#define SPICC_DLYCTL_W1_MASK	GENMASK(21, 16) /* Delay Control - Write-Only */
#define SPICC_FIFORST_RO_MASK	GENMASK(22, 21) /* Fifo Softreset - Read-Only */
#define SPICC_FIFORST_W1_MASK	GENMASK(23, 22) /* Fifo Softreset - Write-Only */

#define SPICC_DRADDR	0x20	/* Read Address of DMA */

#define SPICC_DWADDR	0x24	/* Write Address of DMA */

#define writel_bits_relaxed(mask, val, addr) \
	writel_relaxed((readl_relaxed(addr) & ~(mask)) | (val), addr)

struct meson_spicc_device {
	struct spi_master 		*master;
	struct platform_device		*pdev;
	void __iomem			*base;
	struct completion		transfert;
	struct clk			*core;
};

static irqreturn_t meson_spicc_irq(int irq, void *data)
{
	struct meson_spicc_device *spicc = (void *) data;
	u32 stat = readl_relaxed(spicc->base + SPICC_STATREG);

	if (stat & SPICC_TC) {
		/* Ack the transfert complete */
		writel_relaxed(SPICC_TC, spicc->base + SPICC_STATREG);

		complete(&spicc->transfert);
	} else
		dev_err(&spicc->pdev->dev, "%s: Spurious Interrupt %x\n",
			__func__, stat);

	return IRQ_HANDLED;
}

static unsigned int spicc_divs[] = {4, 8, 16, 32};

static u32 meson_spicc_setup_speed(struct meson_spicc_device *spicc, u32 conf,
				   u32 speed)
{
	unsigned long parent, value;
	int i, n;

	parent = clk_get_rate(spicc->core);

	/* Iterate over the dividers */
	for (i = 0 ; i < ARRAY_SIZE(spicc_divs) ; ++i) {
		n = spicc_divs[i];
		value = parent / n;

		if (value >= speed)
			break;
	}

	dev_dbg(&spicc->pdev->dev, "parent %lu, speed %u, n %d\n", parent,
		speed, n);

	conf &= ~SPICC_DATARATE_MASK;
	conf |= FIELD_PREP(SPICC_DATARATE_MASK, i);

	return conf;
}

static void meson_spicc_setup(struct meson_spicc_device *spicc,
			      struct spi_device *spi,
			      struct spi_transfer *xfer)
{
	u32 conf, conf_orig;
	
	/* Read original configuration */
	conf = conf_orig = readl_relaxed(spicc->base + SPICC_CONREG);

	/* Calculate closest divider */
	conf = meson_spicc_setup_speed(spicc, conf, xfer->speed_hz);

	/* Setup word width */
	conf &= ~SPICC_BITLENGTH_MASK;
	conf |= FIELD_PREP(SPICC_BITLENGTH_MASK, xfer->bits_per_word);

	/* Setup transfer size */
	conf &= ~SPICC_BURSTLENGTH_MASK;
	conf |= FIELD_PREP(SPICC_BURSTLENGTH_MASK, xfer->len);

	/* Setup CS management */
	conf &= ~SPICC_CS_MASK;
	conf |= FIELD_PREP(SPICC_CS_MASK, spi->chip_select);

	if (xfer->cs_change)
		conf |= SPICC_SSCTL;
	else
		conf &= ~SPICC_SSCTL;

	/* Setup transfer mode */
	if (spi->mode & SPI_CPHA)
		conf |= SPICC_PHA;
	else
		conf &= ~SPICC_PHA;

	if (spi->mode & SPI_CPOL)
		conf |= SPICC_POL;
	else
		conf &= ~SPICC_POL;

	if (spi->mode & SPI_CS_HIGH)
		conf |= SPICC_SSPOL;
	else
		conf &= ~SPICC_SSPOL;

	if (spi->mode & SPI_READY)
		conf |= FIELD_PREP(SPICC_DRCTL_MASK, SPICC_DRCTL_LOWLEVEL);
	else
		conf |= FIELD_PREP(SPICC_DRCTL_MASK, SPICC_DRCTL_IGNORE);

	/* Ignore if unchanged */
	if (conf != conf_orig)
		writel_relaxed(conf, spicc->base + SPICC_CONREG);
}

static int meson_spicc_transfer_one(struct spi_master *master,
				    struct spi_device *spi,
				    struct spi_transfer *xfer)
{
	struct meson_spicc_device *spicc = spi_master_get_devdata(master);

	meson_spicc_setup(spicc, spi, xfer);

	/* Write physical addresses */
	writel(xfer->tx_dma, spicc->base + SPICC_DWADDR);
	writel(xfer->rx_dma, spicc->base + SPICC_DRADDR);

	/* Make sure physical addresses are written to registers */
	wmb();

	/* Start burst */
	writel_bits_relaxed(SPICC_XCH, SPICC_XCH, spicc->base + SPICC_CONREG);

	/* Enable interrupt */
	writel_relaxed(SPICC_TC_EN, spicc->base + SPICC_INTREG);

	wait_for_completion(&spicc->transfert);

	/* Disable interrupts */
	writel_relaxed(0, spicc->base + SPICC_INTREG);

	return 0;
}

static void meson_spicc_init(struct meson_spicc_device *spicc)
{
	u32 conf = 0;

	/* Set SMC == 0 */
	/* Default POL/PHA == 0 */
	/* TOFIX Default SSCTL == 0 */
	/* Default SSPOL ==0 (Active Low) */
	/* Default ignore RDY input */
	/* Default select SS0 */
	/* Default Clock rate core/4 */

	conf |= SPICC_ENABLE;
	conf |= SPICC_MODE_MASTER;
	conf |= FIELD_PREP(SPICC_BITLENGTH_MASK, 8); /* Default 8bit word */ 
	
	writel(conf, spicc->base + SPICC_CONREG);

	/* Setup no wait cycles by default */
	writel(0, spicc->base + SPICC_PERIODREG);
}

static size_t meson_spicc_max_transfer_size(struct spi_device *spi)
{
	return GENMASK(6, 0) + 1;
}

static int meson_spicc_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct meson_spicc_device *spicc;
	struct resource *res;
	int ret, irq, rate;

	master = spi_alloc_master(&pdev->dev, sizeof(*spicc));
	if (!master) {
		dev_err(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}
	spicc = spi_master_get_devdata(master);

	spicc->pdev = pdev;
	platform_set_drvdata(pdev, spicc);

	init_completion(&spicc->transfert);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	spicc->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(spicc->base)) {
		ret = PTR_ERR(spicc->base);
		goto out_master;
	}

	/* Disable all IRQs */
	writel_relaxed(0, spicc->base + SPICC_INTREG);

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, irq, meson_spicc_irq,
			       0, NULL, spicc);
	if (ret)
		goto out_master;

	spicc->core = devm_clk_get(&pdev->dev, "core");
	if (IS_ERR(spicc->core)) {
		ret = PTR_ERR(spicc->core);
		goto out_master;
	}

	ret = clk_prepare_enable(spicc->core);
	if (ret)
		goto out_master;

	rate = clk_get_rate(spicc->core);

	master->num_chipselect = 4;
	master->dev.of_node = pdev->dev.of_node;
	master->mode_bits = SPI_CPHA | SPI_CPOL | SPI_CS_HIGH;
	master->bits_per_word_mask = SPI_BIT_MASK(32);
	master->transfer_one = meson_spicc_transfer_one;
	master->min_speed_hz = rate >> 5;
	master->max_transfer_size = meson_spicc_max_transfer_size;
	
	if ((rate >> 2) > SPICC_MAX_FREQ)
		master->max_speed_hz = SPICC_MAX_FREQ;
	else
		master->max_speed_hz = rate >> 2;

	meson_spicc_init(spicc);

	ret = spi_register_master(master);
	if (!ret)
		return 0;

out_master:
	spi_master_put(master);

	return ret;
}

static int meson_spicc_remove(struct platform_device *pdev)
{
	struct meson_spicc_device *spicc = platform_get_drvdata(pdev);

	/* Disable SPI */
	writel(0, spicc->base + SPICC_CONREG);

	clk_disable_unprepare(spicc->core);

	return 0;
}

static const struct of_device_id meson_spicc_of_match[] = {
	{ .compatible = "amlogic,meson-spicc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, meson_spicc_of_match);

static struct platform_driver meson_spicc_driver = {
	.probe   = meson_spicc_probe,
	.remove  = meson_spicc_remove,
	.driver  = {
		.name = "meson-spicc",
		.of_match_table = of_match_ptr(meson_spicc_of_match),
	},
};

module_platform_driver(meson_spicc_driver);

MODULE_DESCRIPTION("Meson SPI Communication Controller driver");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("GPLv2");
