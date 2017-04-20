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
#include <linux/reset.h>

#define SPICC_MAX_FREQ	30000000
#define SPICC_MAX_BURST	128

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
#define SPICC_LBC_RO		BIT(13)	/* Loop Back Control Read-Only */
#define SPICC_LBC_W1		BIT(14) /* Loop Back Control Write-Only */
#define SPICC_SWAP_RO		BIT(14) /* RX Fifo Data Swap Read-Only */
#define SPICC_SWAP_W1		BIT(15) /* RX Fifo Data Swap Write-Only */
#define SPICC_DLYCTL_RO_MASK	GENMASK(20, 15) /* Delay Control Read-Only */
#define SPICC_DLYCTL_W1_MASK	GENMASK(21, 16) /* Delay Control Write-Only */
#define SPICC_FIFORST_RO_MASK	GENMASK(22, 21) /* Fifo Softreset Read-Only */
#define SPICC_FIFORST_W1_MASK	GENMASK(23, 22) /* Fifo Softreset Write-Only */

#define SPICC_DRADDR	0x20	/* Read Address of DMA */

#define SPICC_DWADDR	0x24	/* Write Address of DMA */

#define writel_bits_relaxed(mask, val, addr) \
	writel_relaxed((readl_relaxed(addr) & ~(mask)) | (val), addr)

struct meson_spicc_device {
	struct spi_master 		*master;
	struct platform_device		*pdev;
	void __iomem			*base;
	struct clk			*core;
	struct spi_message		*message;
	struct spi_transfer		*transfer;
	struct scatterlist 		*tx_sgl;
	struct scatterlist 		*rx_sgl;
	u32 				tx_sgl_len;
	u32 				rx_sgl_len;
	u32				burst_len;
};

static void meson_spicc_transfer_dma(struct meson_spicc_device *spicc,
				     struct spi_transfer *xfer);

static irqreturn_t meson_spicc_irq(int irq, void *data)
{
	struct meson_spicc_device *spicc = (void *) data;
	struct spi_transfer *xfer = spicc->transfer;

	u32 stat = readl_relaxed(spicc->base + SPICC_STATREG);

	if (stat & SPICC_TC) {
		/* Ack the transfer complete */
		writel_relaxed(SPICC_TC, spicc->base + SPICC_STATREG);

		/* Update remaining lengths */
		spicc->tx_sgl_len -= spicc->burst_len;
		spicc->rx_sgl_len -= spicc->burst_len;

		/* Update DMA addresses */
		xfer->tx_dma += spicc->burst_len;
		xfer->rx_dma += spicc->burst_len;
		
		/* Walk the TX SG list */
		if (!spicc->tx_sgl_len) {
			spicc->tx_sgl = sg_next(spicc->tx_sgl);
			if (spicc->tx_sgl) {
				xfer->tx_dma = sg_dma_address(spicc->tx_sgl);
				spicc->tx_sgl_len = sg_dma_len(spicc->tx_sgl);
			}
		}
		
		/* Walk the RX SG list */
		if (!spicc->rx_sgl_len) {
			spicc->rx_sgl = sg_next(spicc->rx_sgl);
			if (spicc->rx_sgl) {
				xfer->rx_dma = sg_dma_address(spicc->rx_sgl);
				spicc->rx_sgl_len = sg_dma_len(spicc->rx_sgl);
			}
		}

		if (!spicc->rx_sgl_len || !spicc->tx_sgl_len) {
			/* Disable DMA IRQ */
			writel_relaxed(0, spicc->base + SPICC_INTREG);

			/* Clean up pointers */
			spicc->transfer = NULL;
			spicc->tx_sgl = NULL;
			spicc->rx_sgl = NULL;

			spi_finalize_current_transfer(spicc->master);
		} else
			meson_spicc_transfer_dma(spicc, xfer);
	} else
		dev_err(&spicc->pdev->dev, "%s: Spurious Interrupt %x\n",
			__func__, stat);

	return IRQ_HANDLED;
}

static u32 meson_spicc_setup_speed(struct meson_spicc_device *spicc, u32 conf,
				   u32 speed)
{
	unsigned long parent, value;
	unsigned int i, div;

	parent = clk_get_rate(spicc->core);

	/* Find closest inferior/equal possible speed */
	for (i = 0 ; i < 7 ; ++i) {
		/* 2^(data_rate+2) */
		value = parent >> (i + 2);

		if (value <= speed)
			break;
	}

	/* If provided speed it lower than max divider, use max divider */
	if (i > 7) {
		div = 7;
		dev_warn_once(&spicc->pdev->dev, "unable to get close to speed %u\n",
			      speed);
	}
	else
		div = i;

	dev_dbg(&spicc->pdev->dev, "parent %lu, speed %u -> %lu (%u)\n",
		parent, speed, value, div);

	conf &= ~SPICC_DATARATE_MASK;
	conf |= FIELD_PREP(SPICC_DATARATE_MASK, div);

	return conf;
}

static void meson_spicc_setup_xfer(struct meson_spicc_device *spicc,
				   struct spi_transfer *xfer)
{
	u32 conf, conf_orig;

	/* Store current transfer */
	spicc->transfer = xfer;
	
	/* Read original configuration */
	conf = conf_orig = readl_relaxed(spicc->base + SPICC_CONREG);

	/* Select closest divider */
	conf = meson_spicc_setup_speed(spicc, conf, xfer->speed_hz);

	/* Setup word width */
	conf &= ~SPICC_BITLENGTH_MASK;
	conf |= FIELD_PREP(SPICC_BITLENGTH_MASK, xfer->bits_per_word - 1);

	/* Ignore if unchanged */
	if (conf != conf_orig)
		writel_relaxed(conf, spicc->base + SPICC_CONREG);
}

static void meson_spicc_transfer_dma(struct meson_spicc_device *spicc,
				     struct spi_transfer *xfer)
{ 
	u32 sg_len = min_t(u32, spicc->tx_sgl_len, spicc->rx_sgl_len);
	bool is_last_burst = (spicc->burst_len < SPICC_MAX_BURST &&
			      (sg_is_last(spicc->rx_sgl) || 
			       sg_is_last(spicc->tx_sgl)));
	bool is_last_xfer = list_is_last(&xfer->transfer_list,
					 &spicc->message->transfers);

	/* Disable CS at end of last burst only if last xfer xor cs_change */
	if (is_last_burst && (xfer->cs_change ^ is_last_xfer))
		writel_bits_relaxed(SPICC_SSCTL, 0,
				    spicc->base + SPICC_CONREG);
	else
		writel_bits_relaxed(SPICC_SSCTL, SPICC_SSCTL,
				    spicc->base + SPICC_CONREG);

	/* Write buffer physical addresses */
	writel(xfer->tx_dma, spicc->base + SPICC_DWADDR);
	writel(xfer->rx_dma, spicc->base + SPICC_DRADDR);

	/* Calculate burst len */
	spicc->burst_len = min_t(u32, sg_len, SPICC_MAX_BURST);
	writel_bits_relaxed(SPICC_BURSTLENGTH_MASK, spicc->burst_len,
			    spicc->base + SPICC_CONREG);

	/* Make sure physical addresses are written to registers */
	wmb();

	/* Start burst */
	writel_bits_relaxed(SPICC_XCH, SPICC_XCH, spicc->base + SPICC_CONREG);
}

static int meson_spicc_transfer_one(struct spi_master *master,
				    struct spi_device *spi,
				    struct spi_transfer *xfer)
{
	struct meson_spicc_device *spicc = spi_master_get_devdata(master);

	meson_spicc_setup_xfer(spicc, xfer);

	/* Store SG list */
	spicc->tx_sgl = xfer->tx_sg.sgl;
	spicc->rx_sgl = xfer->rx_sg.sgl;

	/* Map first SG to physical addresses */
	xfer->tx_dma = sg_dma_address(spicc->tx_sgl);
	xfer->rx_dma = sg_dma_address(spicc->rx_sgl);
	spicc->tx_sgl_len = sg_dma_len(spicc->tx_sgl);
	spicc->rx_sgl_len = sg_dma_len(spicc->rx_sgl);

	/* Run DMA transfert */
	meson_spicc_transfer_dma(spicc, xfer);

	/* Enable interrupt */
	writel_relaxed(SPICC_TC_EN, spicc->base + SPICC_INTREG);

	return 1;
}

static int meson_spicc_prepare_message(struct spi_master *master,
				       struct spi_message *message)
{
	struct meson_spicc_device *spicc = spi_master_get_devdata(master);
	struct spi_device *spi = message->spi;
	u32 conf = 0;

	/* Store current message */
	spicc->message = message;
	
	/* Enable Master */
	conf |= SPICC_ENABLE;
	conf |= SPICC_MODE_MASTER;

	/* Set SMC == 0 */

	/* Setup transfer mode */
	if (spi->mode & SPI_CPOL)
		conf |= SPICC_POL;
	else
		conf &= ~SPICC_POL;

	if (spi->mode & SPI_CPHA)
		conf |= SPICC_PHA;
	else
		conf &= ~SPICC_PHA;

	/* Setup CS management */
	conf |= SPICC_SSCTL;

	if (spi->mode & SPI_CS_HIGH)
		conf |= SPICC_SSPOL;
	else
		conf &= ~SPICC_SSPOL;

	if (spi->mode & SPI_READY)
		conf |= FIELD_PREP(SPICC_DRCTL_MASK, SPICC_DRCTL_LOWLEVEL);
	else
		conf |= FIELD_PREP(SPICC_DRCTL_MASK, SPICC_DRCTL_IGNORE);

	/* Select CS */
	conf |= FIELD_PREP(SPICC_CS_MASK, spi->chip_select);

	/* Default Clock rate core/4 */

	/* Default 8bit word */
	conf |= FIELD_PREP(SPICC_BITLENGTH_MASK, 8 - 1); 
	
	writel_relaxed(conf, spicc->base + SPICC_CONREG);

	/* Setup no wait cycles by default */
	writel_relaxed(0, spicc->base + SPICC_PERIODREG);

	/* Enable DMA */
	writel_bits_relaxed(SPICC_DMA_ENABLE, SPICC_DMA_ENABLE,
			    spicc->base + SPICC_DMAREG);

	return 0;
}

static int meson_spicc_unprepare_transfer(struct spi_master *master)
{
	struct meson_spicc_device *spicc = spi_master_get_devdata(master);

	writel_bits_relaxed(SPICC_DMA_ENABLE, 0,
			    spicc->base + SPICC_DMAREG);

	writel_bits_relaxed(SPICC_ENABLE, 0, spicc->base + SPICC_CONREG);

	return 0;
}

static bool meson_spicc_can_dma(struct spi_master *master,
				struct spi_device *spi,
				struct spi_transfer *xfer)
{
	/* TOFIX probably we could use PIO for xfers less than FIFO size */
	return true;
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

	device_reset_optional(&pdev->dev);

	master->num_chipselect = 4;
	master->dev.of_node = pdev->dev.of_node;
	master->mode_bits = SPI_CPHA | SPI_CPOL | SPI_CS_HIGH;
	master->bits_per_word_mask = SPI_BIT_MASK(32);
	master->flags = (SPI_MASTER_MUST_RX | SPI_MASTER_MUST_TX);
	master->min_speed_hz = rate >> 9;
	master->can_dma = meson_spicc_can_dma;
	master->prepare_message = meson_spicc_prepare_message;
	master->unprepare_transfer_hardware = meson_spicc_unprepare_transfer;
	master->transfer_one = meson_spicc_transfer_one;
	
	/* Setup max rate according to the Meson GX datasheet */
	if ((rate >> 2) > SPICC_MAX_FREQ)
		master->max_speed_hz = SPICC_MAX_FREQ;
	else
		master->max_speed_hz = rate >> 2;

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
