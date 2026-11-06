// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on lpass-sc7280.c:
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2026, Linaro Limited. All rights reserved.
 */

#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <linux/pm.h>

#include <dt-bindings/sound/qcom,lpass.h>

#include "lpass-lpaif-reg.h"
#include "lpass.h"

static struct snd_soc_dai_driver sm8650_lpass_cpu_dai_driver[] = {
	{
		.id = MI2S_PRIMARY,
		.name = "Primary MI2S",
		.playback = {
			.stream_name = "Primary Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.ops    = &asoc_qcom_lpass_cpu_dai_ops,
	}, {
		.id = MI2S_SECONDARY,
		.name = "Secondary MI2S",
		.playback = {
			.stream_name = "Secondary MI2S Playback",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.ops	= &asoc_qcom_lpass_cpu_dai_ops,
	}, {
		.id = MI2S_TERTIARY,
		.name = "Tertiary MI2S",
		.playback = {
			.stream_name = "Tertiary MI2S Playback",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.ops	= &asoc_qcom_lpass_cpu_dai_ops,
	}, {
		.id = MI2S_SENARY,
		.name = "Senary MI2S",
		.playback = {
			.stream_name = "Senary MI2S Playback",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.ops	= &asoc_qcom_lpass_cpu_dai_ops,
	}, {
		.id = MI2S_SEPTENARY,
		.name = "Septenary MI2S",
		.playback = {
			.stream_name = "Septenary MI2S Playback",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.ops	= &asoc_qcom_lpass_cpu_dai_ops,
	}, {
		.id = LPASS_DP_RX,
		.name = "Hdmi",
		.playback = {
			.stream_name = "DP Playback",
			.formats = SNDRV_PCM_FMTBIT_S24,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.ops	= &asoc_qcom_lpass_hdmi_dai_ops,
	}, {
		.id = LPASS_CDC_DMA_RX0,
		.name = "CDC DMA RX",
		.playback = {
			.stream_name = "WCD Playback",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 2,
		},
		.ops	= &asoc_qcom_lpass_cdc_dma_dai_ops,
	}, {
		.id = LPASS_CDC_DMA_TX3,
		.name = "CDC DMA TX",
		.capture = {
			.stream_name = "WCD Capture",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 1,
			.channels_max	= 1,
		},
		.ops	= &asoc_qcom_lpass_cdc_dma_dai_ops,
	}, {
		.id = LPASS_CDC_DMA_VA_TX0,
		.name = "CDC DMA VA",
		.capture = {
			.stream_name = "DMIC Capture",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 2,
			.channels_max	= 4,
		},
		.ops	= &asoc_qcom_lpass_cdc_dma_dai_ops,
	},
};

static int sm8650_lpass_alloc_dma_channel(struct lpass_data *drvdata,
					  int direction, unsigned int dai_id)
{
	const struct lpass_variant *v = drvdata->variant;
	unsigned long *ch_map;
	int chan = 0;

	switch (dai_id) {
	case MI2S_PRIMARY ... MI2S_QUINARY:
	case MI2S_SENARY ... MI2S_SEPTENARY:
		ch_map = &lpass_get_lpaif(drvdata, dai_id)->dma_ch_bit_map;
		if (direction == SNDRV_PCM_STREAM_PLAYBACK) {
			chan = find_first_zero_bit(ch_map, v->rdma_channels);

			if (chan >= v->rdma_channels)
				return -EBUSY;
		} else {
			chan = find_next_zero_bit(ch_map,
						  v->wrdma_channel_start +
						  v->wrdma_channels,
						  v->wrdma_channel_start);

			if (chan >= v->wrdma_channel_start + v->wrdma_channels)
				return -EBUSY;
		}
		set_bit(chan, ch_map);
		break;
	case LPASS_DP_RX:
		chan = find_first_zero_bit(&drvdata->hdmi_dma_ch_bit_map,
					   v->hdmi_rdma_channels);
		if (chan >= v->hdmi_rdma_channels)
			return -EBUSY;
		set_bit(chan, &drvdata->hdmi_dma_ch_bit_map);
		break;
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
		chan = find_first_zero_bit(&drvdata->rxtx_dma_ch_bit_map,
					   v->rxtx_rdma_channels);
		if (chan >= v->rxtx_rdma_channels)
			return -EBUSY;
		break;
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
		chan = find_next_zero_bit(&drvdata->rxtx_dma_ch_bit_map,
					  v->rxtx_wrdma_channel_start +
					  v->rxtx_wrdma_channels,
					  v->rxtx_wrdma_channel_start);
		if (chan >= v->rxtx_wrdma_channel_start + v->rxtx_wrdma_channels)
			return -EBUSY;
		set_bit(chan, &drvdata->rxtx_dma_ch_bit_map);
		break;
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		chan = find_next_zero_bit(&drvdata->va_dma_ch_bit_map,
					  v->va_wrdma_channel_start +
					  v->va_wrdma_channels,
					  v->va_wrdma_channel_start);
		if (chan >= v->va_wrdma_channel_start + v->va_wrdma_channels)
			return -EBUSY;
		set_bit(chan, &drvdata->va_dma_ch_bit_map);
		break;
	default:
		break;
	}

	return chan;
}

static int sm8650_lpass_free_dma_channel(struct lpass_data *drvdata, int chan, unsigned int dai_id)
{
	switch (dai_id) {
	case MI2S_PRIMARY ... MI2S_QUINARY:
	case MI2S_SENARY ... MI2S_SEPTENARY:
		clear_bit(chan, &lpass_get_lpaif(drvdata, dai_id)->dma_ch_bit_map);
		break;
	case LPASS_DP_RX:
		clear_bit(chan, &drvdata->hdmi_dma_ch_bit_map);
		break;
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
		clear_bit(chan, &drvdata->rxtx_dma_ch_bit_map);
		break;
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		clear_bit(chan, &drvdata->va_dma_ch_bit_map);
		break;
	default:
		break;
	}

	return 0;
}

static int sm8650_lpass_init(struct platform_device *pdev)
{
	struct lpass_data *drvdata = platform_get_drvdata(pdev);
	const struct lpass_variant *variant = drvdata->variant;
	struct device *dev = &pdev->dev;
	int ret, i;

	drvdata->clks = devm_kcalloc(dev, variant->num_clks,
				     sizeof(*drvdata->clks), GFP_KERNEL);
	if (!drvdata->clks)
		return -ENOMEM;

	drvdata->num_clks = variant->num_clks;

	for (i = 0; i < drvdata->num_clks; i++)
		drvdata->clks[i].id = variant->clk_name[i];

	ret = devm_clk_bulk_get(dev, drvdata->num_clks, drvdata->clks);
	if (ret) {
		dev_err(dev, "Failed to get clocks %d\n", ret);
		return ret;
	}

	ret = clk_bulk_prepare_enable(drvdata->num_clks, drvdata->clks);
	if (ret) {
		dev_err(dev, "sm8650 clk_enable failed\n");
		return ret;
	}

	return 0;
}

static int sm8650_lpass_exit(struct platform_device *pdev)
{
	struct lpass_data *drvdata = platform_get_drvdata(pdev);

	clk_bulk_disable_unprepare(drvdata->num_clks, drvdata->clks);
	return 0;
}

static int sm8650_lpass_dev_resume(struct device *dev)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);

	return clk_bulk_prepare_enable(drvdata->num_clks, drvdata->clks);
}

static int sm8650_lpass_dev_suspend(struct device *dev)
{
	struct lpass_data *drvdata = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(drvdata->num_clks, drvdata->clks);
	return 0;
}

static const struct dev_pm_ops sm8650_lpass_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(sm8650_lpass_dev_suspend, sm8650_lpass_dev_resume)
};

/*
 * LPAIF instances beside the core one, index 1 and up in sm8650_dai_intf.
 *
 * Quaternary and Quinary MI2S are on the RXTX and VA LPAIFs, whose registers,
 * interrupt and DMA channels are owned by the codec DMA: they are not
 * supported.
 */
#define SM8650_LPAIF_CORE	0
#define SM8650_LPAIF_WSA	1
#define SM8650_LPAIF_AUD	2

static const struct lpass_lpaif_desc sm8650_ext_lpaif[] = {
	[SM8650_LPAIF_WSA - 1] = {
		.reg_name = "lpass-wsa-lpaif",
		.irq_name = "lpass-irq-wsa-lpaif",
	},
	[SM8650_LPAIF_AUD - 1] = {
		.reg_name = "lpass-aud-lpaif",
		.irq_name = "lpass-irq-aud-lpaif",
	},
};

/*
 * LPAIF instance, interface and LPM block of each DAI, indexed like the DAI
 * driver. The core LPAIF DMAs access DDR through the SMMU, but the other
 * LPAIFs can only access LPM: their DMA buffers are in the codec LPM, whose
 * block 0 holds the RXTX codec DMA buffers.
 */
static const struct lpass_dai_intf sm8650_dai_intf[] = {
	/* Primary MI2S */
	{ .lpaif = SM8650_LPAIF_CORE, .intf = 0 },
	/* Secondary MI2S */
	{ .lpaif = SM8650_LPAIF_CORE, .intf = 1 },
	/* Tertiary MI2S */
	{ .lpaif = SM8650_LPAIF_AUD, .intf = 1, .lpm = LPASS_LPM_CODEC, .lpm_block = 1 },
	/* Senary MI2S */
	{ .lpaif = SM8650_LPAIF_WSA, .intf = 0, .lpm = LPASS_LPM_CODEC, .lpm_block = 2 },
	/* Septenary MI2S */
	{ .lpaif = SM8650_LPAIF_AUD, .intf = 0, .lpm = LPASS_LPM_CODEC, .lpm_block = 3 },
	{ }, { }, { }, { },		/* DP & codec DMA, unused */
};

static const struct lpass_variant sm8650_data = {
	.i2sctrl_reg_base		= 0x1000,
	.i2sctrl_reg_stride		= 0x1000,
	.i2s_ports			= 3,
	.irq_reg_base			= 0x9000,
	.irq_reg_stride			= 0x1000,
	.irq_ports			= 5,
	.rdma_reg_base			= 0xe000,
	.rdma_reg_stride		= 0x1000,
	.rdma_channels			= 3,
	.rxtx_rdma_reg_base		= 0xe000,
	.rxtx_rdma_reg_stride		= 0x1000,
	.rxtx_rdma_channels		= 10,
	.hdmi_rdma_reg_base		= 0x64000,
	.hdmi_rdma_reg_stride		= 0x1000,
	.hdmi_rdma_channels		= 1,
	.dmactl_audif_start		= 1,
	.wrdma_reg_base			= 0x1a000,
	.wrdma_reg_stride		= 0x1000,
	.wrdma_channel_start		= 5,
	.wrdma_channels			= 3,
	.rxtx_irq_reg_base		= 0x9000,
	.rxtx_irq_reg_stride		= 0x1000,
	.rxtx_irq_ports			= 5,
	.rxtx_wrdma_reg_base		= 0x1a000,
	.rxtx_wrdma_reg_stride		= 0x1000,
	.rxtx_wrdma_channel_start	= 5,
	.rxtx_wrdma_channels		= 7,
	.va_wrdma_reg_base		= 0x1a000,
	.va_wrdma_reg_stride		= 0x1000,
	.va_wrdma_channel_start		= 5,
	.va_wrdma_channels		= 3,
	.va_irq_reg_base		= 0x9000,
	.va_irq_reg_stride		= 0x1000,
	.va_irq_ports			= 5,

	.loopback			= REG_FIELD_ID(0x1000, 17, 17, 3, 0x1000),
	.spken				= REG_FIELD_ID(0x1000, 16, 16, 3, 0x1000),
	.spkmode			= REG_FIELD_ID(0x1000, 11, 15, 3, 0x1000),
	.spkmono			= REG_FIELD_ID(0x1000, 10, 10, 3, 0x1000),
	.micen				= REG_FIELD_ID(0x1000, 9, 9, 3, 0x1000),
	.micmode			= REG_FIELD_ID(0x1000, 4, 8, 3, 0x1000),
	.micmono			= REG_FIELD_ID(0x1000, 3, 3, 3, 0x1000),
	.wssrc				= REG_FIELD_ID(0x1000, 2, 2, 3, 0x1000),
	.bitwidth			= REG_FIELD_ID(0x1000, 0, 1, 3, 0x1000),

	.rdma_dyncclk			= REG_FIELD_ID(0xe000, 21, 21, 3, 0x1000),
	.rdma_bursten			= REG_FIELD_ID(0xe000, 20, 20, 3, 0x1000),
	.rdma_wpscnt			= REG_FIELD_ID(0xe000, 16, 19, 3, 0x1000),
	.rdma_intf			= REG_FIELD_ID(0xe000, 12, 15, 3, 0x1000),
	.rdma_fifowm			= REG_FIELD_ID(0xe000, 1, 5, 3, 0x1000),
	.rdma_enable			= REG_FIELD_ID(0xe000, 0, 0, 3, 0x1000),

	.wrdma_dyncclk			= REG_FIELD_ID(0x1a000, 22, 22, 3, 0x1000),
	.wrdma_bursten			= REG_FIELD_ID(0x1a000, 21, 21, 3, 0x1000),
	.wrdma_wpscnt			= REG_FIELD_ID(0x1a000, 17, 20, 3, 0x1000),
	.wrdma_intf			= REG_FIELD_ID(0x1a000, 12, 16, 3, 0x1000),
	.wrdma_fifowm			= REG_FIELD_ID(0x1a000, 1, 5, 3, 0x1000),
	.wrdma_enable			= REG_FIELD_ID(0x1a000, 0, 0, 3, 0x1000),

	.rxtx_rdma_dyncclk		= REG_FIELD_ID(0xe000, 21, 21, 10, 0x1000),
	.rxtx_rdma_bursten		= REG_FIELD_ID(0xe000, 20, 20, 10, 0x1000),
	.rxtx_rdma_wpscnt		= REG_FIELD_ID(0xe000, 16, 19, 10, 0x1000),
	.rxtx_rdma_intf			= REG_FIELD_ID(0xe000, 12, 15, 10, 0x1000),
	.rxtx_rdma_fifowm		= REG_FIELD_ID(0xe000, 1, 11, 10, 0x1000),
	.rxtx_rdma_enable		= REG_FIELD_ID(0xe000, 0, 0, 10, 0x1000),

	.rxtx_rdma_codec_enable		= REG_FIELD_ID(0xe050, 30, 30, 10, 0x1000),
	.rxtx_rdma_codec_pack		= REG_FIELD_ID(0xe050, 29, 29, 10, 0x1000),
	.rxtx_rdma_codec_fs_sel		= REG_FIELD_ID(0xe050, 25, 27, 10, 0x1000),
	.rxtx_rdma_codec_fs_delay	= REG_FIELD_ID(0xe050, 21, 24, 10, 0x1000),
	.rxtx_rdma_codec_intf		= REG_FIELD_ID(0xe050, 16, 19, 10, 0x1000),
	.rxtx_rdma_codec_ch		= REG_FIELD_ID(0xe050, 0, 7, 10, 0x1000),

	.rxtx_wrdma_dyncclk		= REG_FIELD_ID(0x1a000, 22, 22, 7, 0x1000),
	.rxtx_wrdma_bursten		= REG_FIELD_ID(0x1a000, 21, 21, 7, 0x1000),
	.rxtx_wrdma_wpscnt		= REG_FIELD_ID(0x1a000, 17, 20, 7, 0x1000),
	.rxtx_wrdma_intf		= REG_FIELD_ID(0x1a000, 12, 16, 7, 0x1000),
	.rxtx_wrdma_fifowm		= REG_FIELD_ID(0x1a000, 1, 11, 7, 0x1000),
	.rxtx_wrdma_enable		= REG_FIELD_ID(0x1a000, 0, 0, 7, 0x1000),

	.rxtx_wrdma_codec_enable	= REG_FIELD_ID(0x1a050, 30, 30, 7, 0x1000),
	.rxtx_wrdma_codec_pack		= REG_FIELD_ID(0x1a050, 29, 29, 7, 0x1000),
	.rxtx_wrdma_codec_fs_sel	= REG_FIELD_ID(0x1a050, 25, 27, 7, 0x1000),
	.rxtx_wrdma_codec_fs_delay	= REG_FIELD_ID(0x1a050, 21, 24, 7, 0x1000),
	.rxtx_wrdma_codec_intf		= REG_FIELD_ID(0x1a050, 16, 19, 7, 0x1000),
	.rxtx_wrdma_codec_ch		= REG_FIELD_ID(0x1a050, 0, 7, 7, 0x1000),

	.va_wrdma_dyncclk		= REG_FIELD_ID(0x1a000, 22, 22, 3, 0x1000),
	.va_wrdma_bursten		= REG_FIELD_ID(0x1a000, 21, 21, 3, 0x1000),
	.va_wrdma_wpscnt		= REG_FIELD_ID(0x1a000, 17, 20, 3, 0x1000),
	.va_wrdma_intf			= REG_FIELD_ID(0x1a000, 12, 16, 3, 0x1000),
	.va_wrdma_fifowm		= REG_FIELD_ID(0x1a000, 1, 11, 3, 0x1000),
	.va_wrdma_enable		= REG_FIELD_ID(0x1a000, 0, 0, 3, 0x1000),

	.va_wrdma_codec_enable		= REG_FIELD_ID(0x1a050, 30, 30, 3, 0x1000),
	.va_wrdma_codec_pack		= REG_FIELD_ID(0x1a050, 29, 29, 3, 0x1000),
	.va_wrdma_codec_fs_sel		= REG_FIELD_ID(0x1a050, 25, 27, 3, 0x1000),
	.va_wrdma_codec_fs_delay	= REG_FIELD_ID(0x1a050, 21, 24, 3, 0x1000),
	.va_wrdma_codec_intf		= REG_FIELD_ID(0x1a050, 16, 19, 3, 0x1000),
	.va_wrdma_codec_ch		= REG_FIELD_ID(0x1a050, 0, 7, 3, 0x1000),

	.hdmi_tx_ctl_addr		= 0x1000,
	.hdmi_legacy_addr		= 0x1008,
	.hdmi_vbit_addr			= 0x610c0,
	.hdmi_ch_lsb_addr		= 0x61048,
	.hdmi_ch_msb_addr		= 0x6104c,
	.ch_stride			= 0x8,
	.hdmi_parity_addr		= 0x61034,
	.hdmi_dmactl_addr		= 0x61038,
	.hdmi_dma_stride		= 0x4,
	.hdmi_DP_addr			= 0x610c8,
	.hdmi_sstream_addr		= 0x6101c,
	.hdmi_irq_reg_base		= 0x63000,
	.hdmi_irq_ports			= 1,

	.hdmi_rdma_dyncclk		= REG_FIELD_ID(0x64000, 14, 14, 4, 0x1000),
	.hdmi_rdma_bursten		= REG_FIELD_ID(0x64000, 13, 13, 4, 0x1000),
	.hdmi_rdma_burst8		= REG_FIELD_ID(0x64000, 15, 15, 4, 0x1000),
	.hdmi_rdma_burst16		= REG_FIELD_ID(0x64000, 16, 16, 4, 0x1000),
	.hdmi_rdma_dynburst		= REG_FIELD_ID(0x64000, 18, 18, 4, 0x1000),
	.hdmi_rdma_wpscnt		= REG_FIELD_ID(0x64000, 10, 12, 4, 0x1000),
	.hdmi_rdma_fifowm		= REG_FIELD_ID(0x64000, 1, 5, 4, 0x1000),
	.hdmi_rdma_enable		= REG_FIELD_ID(0x64000, 0, 0, 4, 0x1000),

	.sstream_en			= REG_FIELD(0x6101c, 0, 0),
	.dma_sel			= REG_FIELD(0x6101c, 1, 2),
	.auto_bbit_en			= REG_FIELD(0x6101c, 3, 3),
	.layout				= REG_FIELD(0x6101c, 4, 4),
	.layout_sp			= REG_FIELD(0x6101c, 5, 8),
	.set_sp_on_en			= REG_FIELD(0x6101c, 10, 10),
	.dp_audio			= REG_FIELD(0x6101c, 11, 11),
	.dp_staffing_en			= REG_FIELD(0x6101c, 12, 12),
	.dp_sp_b_hw_en			= REG_FIELD(0x6101c, 13, 13),

	.mute				= REG_FIELD(0x610c8, 0, 0),
	.as_sdp_cc			= REG_FIELD(0x610c8, 1, 3),
	.as_sdp_ct			= REG_FIELD(0x610c8, 4, 7),
	.aif_db4			= REG_FIELD(0x610c8, 8, 15),
	.frequency			= REG_FIELD(0x610c8, 16, 21),
	.mst_index			= REG_FIELD(0x610c8, 28, 29),
	.dptx_index			= REG_FIELD(0x610c8, 30, 31),

	.soft_reset			= REG_FIELD(0x1000, 31, 31),
	.force_reset			= REG_FIELD(0x1000, 30, 30),

	.use_hw_chs			= REG_FIELD(0x61038, 0, 0),
	.use_hw_usr			= REG_FIELD(0x61038, 1, 1),
	.hw_chs_sel			= REG_FIELD(0x61038, 2, 4),
	.hw_usr_sel			= REG_FIELD(0x61038, 5, 6),

	.replace_vbit			= REG_FIELD(0x610c0, 0, 0),
	.vbit_stream			= REG_FIELD(0x610c0, 1, 1),

	.legacy_en			=  REG_FIELD(0x1008, 0, 0),
	.calc_en			=  REG_FIELD(0x61034, 0, 0),
	.lsb_bits			=  REG_FIELD(0x61048, 0, 31),
	.msb_bits			=  REG_FIELD(0x6104c, 0, 31),

	.clk_name			= (const char*[]) {
							"gcc_lpass_cfg_noc_sway",
							"core_cc_sysnoc_mport_core",
						},
	.num_clks			= 2,

	.dai_driver			= sm8650_lpass_cpu_dai_driver,
	.num_dai			= ARRAY_SIZE(sm8650_lpass_cpu_dai_driver),
	.dai_osr_clk_names		= (const char *[]) {
							"core_cc_ext_mclk0",
							"core_cc_ext_mclk0",
							"core_cc_ext_mclk0",
							"core_cc_ext_mclk0",
							"core_cc_ext_mclk0",
							},
	.dai_bit_clk_names		= (const char *[]) {
							"core_cc_ext_if0_ibit",
							"core_cc_ext_if1_ibit",
							"audio_cc_ext_if5_ibit",
							"audio_cc_ext_if2_ibit",
							"audio_cc_ext_if4_ibit",
							},
	.ext_lpaif			= sm8650_ext_lpaif,
	.num_ext_lpaif			= ARRAY_SIZE(sm8650_ext_lpaif),
	.dai_intf			= sm8650_dai_intf,
	.init				= sm8650_lpass_init,
	.exit				= sm8650_lpass_exit,
	.alloc_dma_channel		= sm8650_lpass_alloc_dma_channel,
	.free_dma_channel		= sm8650_lpass_free_dma_channel,
};

static const struct of_device_id sm8650_lpass_cpu_device_id[] = {
	{.compatible = "qcom,sm8650-lpass-cpu", .data = &sm8650_data},
	{}
};
MODULE_DEVICE_TABLE(of, sm8650_lpass_cpu_device_id);

static struct platform_driver sm8650_lpass_cpu_platform_driver = {
	.driver = {
		.name = "sm8650-lpass-cpu",
		.of_match_table = of_match_ptr(sm8650_lpass_cpu_device_id),
		.pm = pm_ptr(&sm8650_lpass_pm_ops),
	},
	.probe = asoc_qcom_lpass_cpu_platform_probe,
	.remove = asoc_qcom_lpass_cpu_platform_remove,
	.shutdown = asoc_qcom_lpass_cpu_platform_shutdown,
};

module_platform_driver(sm8650_lpass_cpu_platform_driver);

MODULE_DESCRIPTION("SM8650 LPASS CPU DRIVER");
MODULE_LICENSE("GPL");
