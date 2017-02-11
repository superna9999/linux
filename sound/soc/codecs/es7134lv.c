/*
 * Copyright (c) 2017 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 */

#include <linux/module.h>
#include <sound/soc.h>

/*
 * The everest 7134lv is a very simple DA converter with no register
 */

static const struct snd_soc_dapm_widget es7134lv_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("AOUTL"),
	SND_SOC_DAPM_OUTPUT("AOUTR"),
};

static const struct snd_soc_dapm_route es7134lv_dapm_routes[] = {
	{ "AOUTL", NULL, "Playback" },
	{ "AOUTR", NULL, "Playback" },
};

static struct snd_soc_dai_driver es7134lv_dai = {
	.name = "es7134lv-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = (SNDRV_PCM_FMTBIT_S16_LE  |
			    SNDRV_PCM_FMTBIT_S18_3LE |
			    SNDRV_PCM_FMTBIT_S20_3LE |
			    SNDRV_PCM_FMTBIT_S24_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE),
	},
};

static struct snd_soc_codec_driver es7134lv_codec_driver = {
	.component_driver = {
		.dapm_widgets		= es7134lv_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(es7134lv_dapm_widgets),
		.dapm_routes		= es7134lv_dapm_routes,
		.num_dapm_routes	= ARRAY_SIZE(es7134lv_dapm_routes),
	},
};

static int es7134lv_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev,
				      &es7134lv_codec_driver,
				      &es7134lv_dai, 1);
}

static int es7134lv_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id es7134lv_ids[] = {
	{ .compatible = "everest,es7134lv", },
	{ }
};
MODULE_DEVICE_TABLE(of, es7134lv_ids);
#endif

static struct platform_driver es7134lv_driver = {
	.driver = {
		.name = "es7134lv",
		.of_match_table = of_match_ptr(es7134lv_ids),
	},
	.probe = es7134lv_probe,
	.remove = es7134lv_remove,
};

module_platform_driver(es7134lv_driver);

MODULE_DESCRIPTION("ASoC ES7134LV audio codec driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL");
