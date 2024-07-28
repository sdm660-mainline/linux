// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Richard Acayan. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-card.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>
#include <sound/soc-jack.h>

#include "common.h"
#include "qdsp6/q6afe.h"

#define DEFAULT_SAMPLE_RATE_48K		48000
#define DEFAULT_INT_MCLK_RATE		9600000
#define TDM_BCLK_RATE			6144000
#define MI2S_BCLK_RATE			1536000

struct sdm660_int_snd_data {
	struct snd_soc_jack jack;
	bool jack_setup;
	uint32_t sec_tdm_clk_count;
	uint32_t int0_mi2s_clk_count;
	uint32_t int3_mi2s_clk_count;
};

static int snd_sdm660_int_startup(struct snd_pcm_substream *stream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(stream);
	struct sdm660_int_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec;
	int i;

	switch (cpu->id) {
	case SECONDARY_TDM_RX_0:
		data->sec_tdm_clk_count++;
		if (data->sec_tdm_clk_count == 1)
			snd_soc_dai_set_sysclk(cpu,
				Q6AFE_LPASS_CLK_ID_SEC_TDM_IBIT,
				TDM_BCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);

		for_each_rtd_codec_dais(rtd, i, codec) {
			snd_soc_dai_set_fmt(codec, SND_SOC_DAIFMT_DSP_A
						 | SND_SOC_DAIFMT_CBC_CFC
						 | SND_SOC_DAIFMT_IB_NF);

			snd_soc_dai_set_sysclk(codec, 0,
					       TDM_BCLK_RATE,
					       SNDRV_PCM_STREAM_PLAYBACK);
			snd_soc_component_set_sysclk(codec->component,
						     0, 0,
						     TDM_BCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);
		}
		break;
	case INT0_MI2S_RX:
		data->int0_mi2s_clk_count++;
		if (data->int0_mi2s_clk_count == 1)
			snd_soc_dai_set_sysclk(cpu,
				Q6AFE_LPASS_CLK_ID_INT0_MI2S_IBIT,
				MI2S_BCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);

		/*
		 * Downstream specifies that the AFE is a clock consumer, but
		 * the sound is distorted (loud on the right channel and sped
		 * up) unless we set it as a producer.
		 */
		snd_soc_dai_set_fmt(cpu, SND_SOC_DAIFMT_CBP_CFP);

		break;
	case INT3_MI2S_TX:
		data->int3_mi2s_clk_count++;
		if (data->int3_mi2s_clk_count == 1)
			snd_soc_dai_set_sysclk(cpu,
				Q6AFE_LPASS_CLK_ID_INT3_MI2S_IBIT,
				MI2S_BCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);

		/*
		 * Downstream specifies that the AFE is a clock consumer, but
		 * the sound is distorted (slowed down) unless we set it as a
		 * producer.
		 */
		snd_soc_dai_set_fmt(cpu, SND_SOC_DAIFMT_CBP_CFP);

		break;
	default:
		dev_err(cpu->dev, "unimplemented afe dai\n");
		return -ENOSYS;
	}

	return 0;
}

static void snd_sdm660_int_shutdown(struct snd_pcm_substream *stream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(stream);
	struct sdm660_int_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu = asoc_rtd_to_cpu(rtd, 0);

	switch (cpu->id) {
	case SECONDARY_TDM_RX_0:
		data->sec_tdm_clk_count--;
		if (data->sec_tdm_clk_count == 0)
			snd_soc_dai_set_sysclk(cpu,
				Q6AFE_LPASS_CLK_ID_SEC_TDM_IBIT,
				0, SNDRV_PCM_STREAM_PLAYBACK);

		break;
	case INT0_MI2S_RX:
		data->int0_mi2s_clk_count--;
		if (data->int0_mi2s_clk_count == 0)
			snd_soc_dai_set_sysclk(cpu,
				Q6AFE_LPASS_CLK_ID_INT0_MI2S_IBIT,
				0, SNDRV_PCM_STREAM_PLAYBACK);

		break;
	case INT3_MI2S_TX:
		data->int3_mi2s_clk_count--;
		if (data->int3_mi2s_clk_count == 0)
			snd_soc_dai_set_sysclk(cpu,
				Q6AFE_LPASS_CLK_ID_INT3_MI2S_IBIT,
				0, SNDRV_PCM_STREAM_PLAYBACK);

		break;
	default:
		dev_err(cpu->dev, "unimplemented afe dai\n");
		break;
	}
}

static unsigned int tdm_slot_off[] = {
	0, 4, 8, 12, 16, 20, 24, 28
};

static int snd_sdm660_int_hw_params(struct snd_pcm_substream *stream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(stream);
	struct snd_soc_dai *cpu = asoc_rtd_to_cpu(rtd, 0);
	unsigned int channels;
	int ret;

	switch (cpu->id) {
	case SECONDARY_TDM_RX_0:
		channels = params_channels(params);

		ret = snd_soc_dai_set_tdm_slot(cpu, 0, (1 << channels) - 1, 8, 16);
		if (ret) {
			dev_err(cpu->dev, "set tdm slot failed\n");
			return ret;
		}

		ret = snd_soc_dai_set_channel_map(cpu, 0, NULL,
						  channels, tdm_slot_off);
		if (ret) {
			dev_err(cpu->dev, "set channel map failed\n");
			return ret;
		}
		break;
	case INT0_MI2S_RX:
	case INT3_MI2S_TX:
	default:
		break;
	}

	return 0;
}

static int snd_sdm660_int_hw_free(struct snd_pcm_substream *stream)
{
	return 0;
}

static int snd_sdm660_int_prepare(struct snd_pcm_substream *stream)
{
	return 0;
}

static const struct snd_soc_ops sdm660_int_ops = {
	.startup = snd_sdm660_int_startup,
	.shutdown = snd_sdm660_int_shutdown,
	.hw_params = snd_sdm660_int_hw_params,
	.hw_free = snd_sdm660_int_hw_free,
	.prepare = snd_sdm660_int_prepare,
};

static int sdm660_int_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					 struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	rate->min = rate->max = DEFAULT_SAMPLE_RATE_48K;
	channels->min = channels->max = 2;
	snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S16_LE);

	return 0;
}

static void sdm660_int_jack_free(struct snd_jack *jack)
{
	struct snd_soc_component *component = jack->private_data;

	snd_soc_component_set_jack(component, NULL, NULL);
}

static int sdm660_int_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct sdm660_int_snd_data *data = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *cpu = asoc_rtd_to_cpu(rtd, 0);
	/* first codec on INT0_MI2S_RX must be the analog codec */
	struct snd_soc_dai *codec = asoc_rtd_to_codec(rtd, 0);
	struct snd_jack *jack;
	int ret;

	if (!data->jack_setup) {
		/* headset buttons not tested */
		ret = snd_soc_card_jack_new(card, "Headset Jack",
					    SND_JACK_HEADSET | SND_JACK_BTN_0
					  | SND_JACK_BTN_1 | SND_JACK_BTN_2
					  | SND_JACK_BTN_3 | SND_JACK_BTN_4,
					    &data->jack);
		if (ret < 0) {
			dev_err(card->dev, "could not create headset jack\n");
			return ret;
		}

		data->jack_setup = true;
	}

	switch (cpu->id) {
	case INT0_MI2S_RX:
		jack = data->jack.jack;

		jack->private_data = codec->component;
		jack->private_free = sdm660_int_jack_free;

		ret = snd_soc_component_set_jack(codec->component,
						 &data->jack,
						 NULL);
		if (ret < 0) {
			dev_err(card->dev, "could not set headset jack\n");
			return ret;
		}

		break;
	default:
		break;
	}

	return 0;
}

static void snd_sdm660_int_add_ops(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *link;
	int i;

	for_each_card_prelinks(card, i, link) {
		if (link->no_pcm == 1) {
			link->ops = &sdm660_int_ops;
			link->be_hw_params_fixup = sdm660_int_be_hw_params_fixup;
		}

		link->init = sdm660_int_dai_init;
	}
}

static const struct snd_soc_dapm_widget snd_sdm660_int_dapm_widgets[] = {
};

static int snd_sdm660_int_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct sdm660_int_snd_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	card = devm_kzalloc(dev, sizeof(struct snd_soc_card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	data = devm_kzalloc(dev, sizeof(struct sdm660_int_snd_data), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->driver_name = "sdm660-internal";
	card->dapm_widgets = snd_sdm660_int_dapm_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(snd_sdm660_int_dapm_widgets);
	card->dev = dev;
	card->owner = THIS_MODULE;

	ret = qcom_snd_parse_of(card);
	if (ret)
		return ret;

	snd_soc_card_set_drvdata(card, data);

	snd_sdm660_int_add_ops(card);

	return devm_snd_soc_register_card(dev, card);
}

static int snd_sdm660_int_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id snd_sdm660_int_device_id[] = {
	{ .compatible = "qcom,sdm660-internal-sndcard", },
	{ }
};
MODULE_DEVICE_TABLE(of, snd_sdm660_int_device_id);

static struct platform_driver snd_sdm660_int_driver = {
	.probe = snd_sdm660_int_probe,
	.remove = snd_sdm660_int_remove,
	.driver = {
		.name = "sdm660-int-sndcard",
		.of_match_table = snd_sdm660_int_device_id,
		.pm = &snd_soc_pm_ops,
	},
};
module_platform_driver(snd_sdm660_int_driver);

MODULE_DESCRIPTION("sdm660 Internal ASoC Machine Driver");
MODULE_LICENSE("GPL");
