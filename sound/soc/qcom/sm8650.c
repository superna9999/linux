// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <dt-bindings/sound/qcom,lpass.h>
#include "common.h"

struct sm8650_cpu_sndcard_data {
	struct snd_soc_card card;
};

#define DEFAULT_MCLK_RATE		9600000
#define MI2S_BCLK_RATE          1536000

static const struct snd_soc_dapm_widget sm8650_cpu_sndcard_snd_widgets[] = {
		SND_SOC_DAPM_MIC("DMIC", NULL),
};

static int sm8650_cpu_sndcard_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{                                                                                                   
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);                      
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);                                                              
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);                                   
	int ret;                                                                                    
	
	switch (cpu_dai->id) {
	case MI2S_TERTIARY:
		ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_BP_FP);
		if (ret && ret != -ENOTSUPP)
			return ret;

		ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_BC_FC |
						     SND_SOC_DAIFMT_NB_NF |
						     SND_SOC_DAIFMT_I2S);
		if (ret && ret != -ENOTSUPP)
			return ret;

		ret = snd_soc_dai_set_sysclk(cpu_dai, 0, DEFAULT_MCLK_RATE,
					     SND_SOC_CLOCK_OUT);
		if (ret && ret != -ENOTSUPP)
			return ret;

		ret = snd_soc_dai_set_sysclk(codec_dai, 0, MI2S_BCLK_RATE,
					     SND_SOC_CLOCK_IN);
		if (ret && ret != -ENOTSUPP)
			return ret;
		break;
	}

	return 0;
}

static const struct snd_soc_ops sm8650_cpu_sndcard_ops = {
	.hw_params = sm8650_cpu_sndcard_hw_params,
};

static int sm8650_cpu_sndcard_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card;
	struct sm8650_cpu_sndcard_data *data;
	struct snd_soc_dai_link *link;
	int ret;
	int i;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	card = &data->card;
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->dapm_widgets = sm8650_cpu_sndcard_snd_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(sm8650_cpu_sndcard_snd_widgets);

	ret = qcom_snd_parse_of(card);
	if (ret)
		return ret;

	snd_soc_card_set_drvdata(card, data);

	for_each_card_prelinks(card, i, link)
		link->ops = &sm8650_cpu_sndcard_ops;

	return devm_snd_soc_register_card(&pdev->dev, card);
}

static const struct of_device_id sm8650_cpu_sndcard_device_id[] __maybe_unused = {
	{ .compatible = "valve,deckard-snd-card" },
	{},
};
MODULE_DEVICE_TABLE(of, sm8650_cpu_sndcard_device_id);

static struct platform_driver sm8650_cpu_sndcard_platform_driver = {
	.driver = {
		.name = "qcom-sm8650-cpu-sndcard",
		.of_match_table = of_match_ptr(sm8650_cpu_sndcard_device_id),
	},
	.probe = sm8650_cpu_sndcard_platform_probe,
};
module_platform_driver(sm8650_cpu_sndcard_platform_driver);

MODULE_DESCRIPTION("SM8650 CPU ASoC Machine Driver");
MODULE_LICENSE("GPL");
