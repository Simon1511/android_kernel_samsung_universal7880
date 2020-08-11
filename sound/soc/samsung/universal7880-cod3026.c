/*
 * universal7880-COD3026X Audio Machine driver.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <sound/tlv.h>

#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/exynos-audmixer.h>
#include <sound/cod3026x.h>

#include "i2s.h"
#include "i2s-regs.h"
#if defined (CONFIG_FM_SI47XX)
#include <linux/i2c/si47xx_common.h>
#endif

#define CODEC_BFS_48KHZ		32
#define CODEC_RFS_48KHZ		512
#define CODEC_SAMPLE_RATE_48KHZ	48000

#define CODEC_BFS_192KHZ		64
#define CODEC_RFS_192KHZ		128
#define CODEC_SAMPLE_RATE_192KHZ	192000

#ifdef CONFIG_SND_SOC_SAMSUNG_VERBOSE_DEBUG
#define RX_SRAM_SIZE            (0x2000)        /* 8 KB */
#ifdef dev_dbg
#undef dev_dbg
#endif
#define dev_dbg dev_err
#endif

static struct snd_soc_card universal7880_cod3026x_card;

enum {
	MB_NONE,
	MB_INT_BIAS1,
	MB_INT_BIAS2,
	MB_EXT_GPIO,
	MB_EXT_LDO,
	MB_MAX,
};

/* codec internal input */
enum {
	INT_MIC1,
	INT_MIC2,
	INT_MIC3,
	INT_LINEIN,
	INT_INPUT_MAX,
};

struct universal7880_mic_bias {
	int mode[INT_INPUT_MAX];
	int gpio[INT_INPUT_MAX];
};

struct universal7880_mic_bias_count {
	atomic_t use_count[MB_MAX];
};

struct cod3026x_machine_priv {
	struct snd_soc_codec *codec;
	int aifrate;
	struct universal7880_mic_bias mic_bias;
	struct universal7880_mic_bias_count mic_bias_count;
	bool use_external_jd;
	struct regulator *vdd;
};

static const struct snd_soc_component_driver universal7880_cmpnt = {
	.name = "universal7880-audio",
};

static int universal7880_aif1_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *amixer_dai = rtd->codec_dais[0];
	struct snd_soc_dai *codec_dai = rtd->codec_dais[1];
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct cod3026x_machine_priv *priv = snd_soc_card_get_drvdata(card);
	int ret;
	int rfs, bfs;

	dev_info(card->dev, "aif1: %dch, %dHz, %dbytes\n",
		 params_channels(params), params_rate(params),
		 params_buffer_bytes(params));

	priv->aifrate = params_rate(params);

	if (priv->aifrate == CODEC_SAMPLE_RATE_192KHZ) {
		rfs = CODEC_RFS_192KHZ;
		bfs = CODEC_BFS_192KHZ;
	} else {
		rfs = CODEC_RFS_48KHZ;
		bfs = CODEC_BFS_48KHZ;
	}

	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S
						| SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		dev_err(card->dev, "aif1: Failed to set Codec DAIFMT\n");
		return ret;
	}

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S
						| SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		dev_err(card->dev, "aif1: Failed to set CPU DAIFMT\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_CDCLK,
				rfs, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(card->dev, "aif1: Failed to set SAMSUNG_I2S_CDCLK\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_OPCLK,
						0, MOD_OPCLK_PCLK);
	if (ret < 0) {
		dev_err(card->dev, "aif1: Failed to set SAMSUNG_I2S_OPCLK\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_RCLKSRC_1, 0, 0);
	if (ret < 0) {
		dev_err(card->dev,
				"aif1: Failed to set SAMSUNG_I2S_RCLKSRC_1\n");
		return ret;
	}

	ret = snd_soc_dai_set_clkdiv(cpu_dai,
			SAMSUNG_I2S_DIV_BCLK, bfs);
	if (ret < 0) {
		dev_err(card->dev, "aif1: Failed to set BFS\n");
		return ret;
	}

	ret = snd_soc_dai_set_clkdiv(cpu_dai,
			SAMSUNG_I2S_DIV_RCLK, rfs);
	if (ret < 0) {
		dev_err(card->dev, "aif1: Failed to set RFS\n");
		return ret;
	}

	ret = snd_soc_dai_set_bclk_ratio(amixer_dai, bfs);
	if (ret < 0) {
		dev_err(card->dev, "aif1: Failed to configure mixer\n");
		return ret;
	}

	return 0;
}

static int universal7880_aif2_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *amixer_dai = rtd->codec_dais[0];
	int bfs, ret;

	dev_info(card->dev, "aif2: %dch, %dHz, %dbytes\n",
		 params_channels(params), params_rate(params),
		 params_buffer_bytes(params));

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_U24:
	case SNDRV_PCM_FORMAT_S24:
		bfs = 48;
		break;
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_S16_LE:
		bfs = 32;
		break;
	default:
		dev_err(card->dev, "aif2: Unsupported PCM_FORMAT\n");
		return -EINVAL;
	}

	ret = snd_soc_dai_set_bclk_ratio(amixer_dai, bfs);
	if (ret < 0) {
		dev_err(card->dev, "aif2: Failed to configure mixer\n");
		return ret;
	}

	return 0;
}

static int universal7880_aif3_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *amixer_dai = rtd->codec_dais[0];
	int bfs, ret;

	dev_info(card->dev, "aif3: %dch, %dHz, %dbytes\n",
		 params_channels(params), params_rate(params),
		 params_buffer_bytes(params));

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_U24:
	case SNDRV_PCM_FORMAT_S24:
		bfs = 48;
		break;
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_S16_LE:
		bfs = 32;
		break;
	default:
		dev_err(card->dev, "aif3: Unsupported PCM_FORMAT\n");
		return -EINVAL;
	}

	ret = snd_soc_dai_set_bclk_ratio(amixer_dai, bfs);
	if (ret < 0) {
		dev_err(card->dev, "aif3: Failed to configure mixer\n");
		return ret;
	}

	ret = snd_soc_dai_set_fmt(amixer_dai, SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0) {
		dev_err(card->dev, "aif3: Failed to set Mixer FMT\n");
		return ret;
	}

	return 0;
}

static int universal7880_aif4_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *amixer_dai = rtd->codec_dais[0];
	int bfs, ret;

	dev_info(card->dev, "aif4: %dch, %dHz, %dbytes\n",
		 params_channels(params), params_rate(params),
		 params_buffer_bytes(params));

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_U24:
	case SNDRV_PCM_FORMAT_S24:
		bfs = 48;
		break;
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_S16_LE:
		bfs = 32;
		break;
	default:
		dev_err(card->dev, "aif4: Unsupported PCM_FORMAT\n");
		return -EINVAL;
	}

	ret = snd_soc_dai_set_bclk_ratio(amixer_dai, bfs);
	if (ret < 0) {
		dev_err(card->dev, "aif4: Failed to configure mixer\n");
		return ret;
	}

	ret = snd_soc_dai_set_fmt(amixer_dai, SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		dev_err(card->dev, "aif4: Failed to set Mixer FMT\n");
		return ret;
	}

	return 0;
}

static int universal7880_aif5_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *amixer_dai = rtd->codec_dais[0];
	struct cod3026x_machine_priv *priv = snd_soc_card_get_drvdata(card);
	int ret;
	int rfs, bfs;

	dev_info(card->dev, "aif5: %dch, %dHz, %dbytes\n",
		 params_channels(params), params_rate(params),
		 params_buffer_bytes(params));

	priv->aifrate = params_rate(params);

	if (priv->aifrate == CODEC_SAMPLE_RATE_192KHZ) {
		rfs = CODEC_RFS_192KHZ;
		bfs = CODEC_BFS_192KHZ;
	} else {
		rfs = CODEC_RFS_48KHZ;
		bfs = CODEC_BFS_48KHZ;
	}

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S
						| SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		dev_err(card->dev, "aif5: Failed to set CPU DAIFMT\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_CDCLK,
				rfs, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(card->dev, "aif5: Failed to set SAMSUNG_I2S_CDCLK\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_OPCLK,
						0, MOD_OPCLK_PCLK);
	if (ret < 0) {
		dev_err(card->dev, "aif5: Failed to set SAMSUNG_I2S_OPCLK\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_RCLKSRC_1, 0, 0);
	if (ret < 0) {
		dev_err(card->dev,
				"aif5: Failed to set SAMSUNG_I2S_RCLKSRC_1\n");
		return ret;
	}

	ret = snd_soc_dai_set_clkdiv(cpu_dai,
			SAMSUNG_I2S_DIV_BCLK, bfs);
	if (ret < 0) {
		dev_err(card->dev, "aif5: Failed to set BFS\n");
		return ret;
	}

	ret = snd_soc_dai_set_clkdiv(cpu_dai,
			SAMSUNG_I2S_DIV_RCLK, rfs);
	if (ret < 0) {
		dev_err(card->dev, "aif5: Failed to set RFS\n");
		return ret;
	}

	ret = snd_soc_dai_set_bclk_ratio(amixer_dai, bfs);
	if (ret < 0) {
		dev_err(card->dev, "aif5: Failed to configure mixer\n");
		return ret;
	}

	return 0;
}

static int universal7880_aif6_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *amixer_dai = rtd->codec_dais[0];
	int bfs, ret;

	dev_info(card->dev, "aif6: %dch, %dHz, %dbytes\n",
			params_channels(params), params_rate(params),
			params_buffer_bytes(params));

	switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_U24:
		case SNDRV_PCM_FORMAT_S24:
			bfs = 48;
			break;
		case SNDRV_PCM_FORMAT_U16_LE:
		case SNDRV_PCM_FORMAT_S16_LE:
			bfs = 32;
			break;
		default:
			dev_err(card->dev, "aif6: Unsupported PCM_FORMAT\n");
			return -EINVAL;
	}

	ret = snd_soc_dai_set_bclk_ratio(amixer_dai, bfs);
	if (ret < 0) {
		dev_err(card->dev, "aif6: Failed to configure mixer\n");
		return ret;
	}

	return 0;
}

static int universal7880_aif1_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif1: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	return 0;
}

void universal7880_aif1_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif1: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);
}

static int universal7880_aif2_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif2: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	return 0;
}

void universal7880_aif2_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif2: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);
}

static int universal7880_aif3_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif3: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	return 0;
}

void universal7880_aif3_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif3: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);
}

static int universal7880_aif4_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif4: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	return 0;
}

void universal7880_aif4_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif4: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);
}

#if defined (CONFIG_FM_SI47XX)
static int universal7880_aif4_hw_free(struct snd_pcm_substream *substream)
{
 	si47xx_dev_digitalmode(0);
 	return 0;
}

static int universal7880_aif4_prepare(struct snd_pcm_substream *substream)
{
 	si47xx_dev_digitalmode(1);
 	return 0;
}
#endif

static int universal7880_aif5_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif5: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	return 0;
}

void universal7880_aif5_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif5: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);
}

static int universal7880_aif6_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif6: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);

	return 0;
}

void universal7880_aif6_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_dbg(card->dev, "aif6: (%s) %s called\n",
			substream->stream ? "C" : "P", __func__);
}

static int universal7880_set_bias_level(struct snd_soc_card *card,
				 struct snd_soc_dapm_context *dapm,
				 enum snd_soc_bias_level level)
{
	return 0;
}

static int universal7880_late_probe(struct snd_soc_card *card)
{

	dev_dbg(card->dev, "%s called\n", __func__);
	return 0;
}


static void universal7880_ext_gpio_bias_ev(struct snd_soc_card *card,
				int event, int gpio)
{
	dev_dbg(card->dev, "%s Called: %d, ext mic bias gpio %s\n", __func__,
			event, gpio_is_valid(gpio) ?
			"valid" : "invalid");

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (gpio_is_valid(gpio))
			gpio_set_value(gpio, 1);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (gpio_is_valid(gpio))
			gpio_set_value(gpio, 0);
		break;
	}
}

static void universal7880_ext_ldo_bias_ev(struct snd_soc_card *card,
                int event)
{
	struct cod3026x_machine_priv *priv = snd_soc_card_get_drvdata(card);
	int ret;
    dev_dbg(card->dev, "%s Called\n", __func__);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = regulator_enable(priv->vdd);
		break;
	case SND_SOC_DAPM_POST_PMD:
		ret = regulator_disable(priv->vdd);
		break;
	}
}

static int universal7880_int_bias1_ev(struct snd_soc_card *card,
				int event)
{
	struct cod3026x_machine_priv *priv = snd_soc_card_get_drvdata(card);

	dev_dbg(card->dev, "%s called\n", __func__);
	return cod3026x_mic_bias_ev(priv->codec, COD3026X_MICBIAS1, event);
}

static int universal7880_int_bias2_ev(struct snd_soc_card *card,
				int event)
{
	struct cod3026x_machine_priv *priv = snd_soc_card_get_drvdata(card);

	dev_dbg(card->dev, "%s called\n", __func__);
	return cod3026x_mic_bias_ev(priv->codec, COD3026X_MICBIAS2, event);
}

static int universal7880_configure_mic_bias(struct snd_soc_card *card,
		int index, int event)
{
	struct cod3026x_machine_priv *priv = snd_soc_card_get_drvdata(card);
	int process_event = 0;
	int mode_index = priv->mic_bias.mode[index];

	pr_err("[3026]%s called\n", __func__);
	/* validate the bias mode */
	if ((mode_index < MB_INT_BIAS1) || (mode_index > MB_EXT_LDO))
		return 0;

	/* decrement the bias mode index to match use count buffer
	 * because use count buffer size is 0-2, and the mode is 1-3
	 * so decrement, and this veriable should be used only for indexing
	 * use_count.
	 */
	mode_index--;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		atomic_inc(&priv->mic_bias_count.use_count[mode_index]);
		if (atomic_read(&priv->mic_bias_count.use_count[mode_index]) == 1)
			process_event = 1;
		break;

	case SND_SOC_DAPM_POST_PMD:
		atomic_dec(&priv->mic_bias_count.use_count[mode_index]);
		if (atomic_read(&priv->mic_bias_count.use_count[mode_index]) == 0)
			process_event = 1;
		break;

	default:
		break;
	}

	if (!process_event)
		return 0;

	switch(priv->mic_bias.mode[index]) {
	case MB_INT_BIAS1:
		universal7880_int_bias1_ev(card, event);
		break;
	case MB_INT_BIAS2:
		universal7880_int_bias2_ev(card, event);
		break;
	case MB_EXT_GPIO:
		universal7880_ext_gpio_bias_ev(card, event,
				priv->mic_bias.gpio[index]);
		break;
	case MB_EXT_LDO:
		universal7880_ext_ldo_bias_ev(card, event);
	default:
		break;
	};

	return 0;
}

static int universal7880_mic1_bias(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	return universal7880_configure_mic_bias(w->dapm->card, INT_MIC1, event);
}

static int universal7880_mic2_bias(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	return universal7880_configure_mic_bias(w->dapm->card, INT_MIC2, event);
}

static int universal7880_mic3_bias(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	return universal7880_configure_mic_bias(w->dapm->card, INT_MIC3, event);
}

static int universal7880_linein_bias(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	return universal7880_configure_mic_bias(w->dapm->card, INT_LINEIN, event);
}


static int universal7880_request_ext_mic_bias_en_gpio(struct snd_soc_card *card)
{
	struct cod3026x_machine_priv *priv = snd_soc_card_get_drvdata(card);
	struct device *dev = card->dev;
	int ret;
	int gpio;
	int i;
	char gpio_name[32];

	dev_dbg(dev, "%s called\n", __func__);

	for (i = 0; i < INT_INPUT_MAX; i++) {
		gpio = priv->mic_bias.gpio[i];

		/* This is optional GPIO, don't report if not present */
		if (!gpio_is_valid(gpio))
			continue;
		sprintf(gpio_name, "ext_mic_bias-%d", i);

		ret = devm_gpio_request_one(dev, gpio,
				GPIOF_OUT_INIT_LOW, gpio_name);

		if (ret < 0) {
			dev_err(dev, "Ext-MIC bias GPIO request failed\n");
			continue;
		}
		gpio_direction_output(gpio, 0);
	}

	return 0;
}

static int universal7880_init_soundcard(struct snd_soc_card *card)
{
	struct snd_soc_codec *codec = card->rtd[0].codec_dais[1]->codec;
	struct cod3026x_machine_priv *priv = snd_soc_card_get_drvdata(card);
	int ret;

	priv->codec = codec;

	ret = universal7880_request_ext_mic_bias_en_gpio(card);
	if (ret)
		dev_warn(codec->dev, "Failed to get ext mic bias gpios :%d\n",
								ret);
	return 0;
}

static int audmixer_init(struct snd_soc_component *cmp)
{
	dev_dbg(cmp->dev, "%s called\n", __func__);

	return 0;
}

static const struct snd_kcontrol_new universal7880_controls[] = {
};

const struct snd_soc_dapm_widget universal7880_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("MIC1 Bias", universal7880_mic1_bias),
	SND_SOC_DAPM_MIC("MIC2 Bias", universal7880_mic2_bias),
	SND_SOC_DAPM_MIC("MIC3 Bias", universal7880_mic3_bias),
	SND_SOC_DAPM_MIC("LINEIN Bias", universal7880_linein_bias),
};

const struct snd_soc_dapm_route universal7880_dapm_routes[] = {
	{"DMIC1_PGA", NULL, "MIC1 Bias"},
	{"MIC1_PGA", NULL, "MIC1 Bias"},
	{"MIC1 Bias", NULL, "IN1L"},

	{"DMIC2_PGA", NULL, "MIC2 Bias"},
	{"MIC2_PGA", NULL, "MIC2 Bias"},
	{"MIC2 Bias", NULL, "IN2L"},

	{"MIC3_PGA", NULL, "MIC3 Bias"},
	{"MIC3 Bias", NULL, "IN3L"},

	{"LINEIN_PGA", NULL, "LINEIN Bias"},
	{"LINEIN Bias", NULL, "IN4L" },
};

static struct snd_soc_ops universal7880_aif1_ops = {
	.hw_params = universal7880_aif1_hw_params,
	.startup = universal7880_aif1_startup,
	.shutdown = universal7880_aif1_shutdown,
};

static struct snd_soc_ops universal7880_aif2_ops = {
	.hw_params = universal7880_aif2_hw_params,
	.startup = universal7880_aif2_startup,
	.shutdown = universal7880_aif2_shutdown,
};

static struct snd_soc_ops universal7880_aif3_ops = {
	.hw_params = universal7880_aif3_hw_params,
	.startup = universal7880_aif3_startup,
	.shutdown = universal7880_aif3_shutdown,
};

static struct snd_soc_ops universal7880_aif4_ops = {
	.hw_params = universal7880_aif4_hw_params,
	.startup = universal7880_aif4_startup,
	.shutdown = universal7880_aif4_shutdown,
#if defined (CONFIG_FM_SI47XX)	
	.hw_free = universal7880_aif4_hw_free,
	.prepare = universal7880_aif4_prepare,
#endif
};

static struct snd_soc_ops universal7880_aif5_ops = {
	.hw_params = universal7880_aif5_hw_params,
	.startup = universal7880_aif5_startup,
	.shutdown = universal7880_aif5_shutdown,
};

static struct snd_soc_ops universal7880_aif6_ops = {
	.hw_params = universal7880_aif6_hw_params,
	.startup = universal7880_aif6_startup,
	.shutdown = universal7880_aif6_shutdown,
};


static struct snd_soc_dai_driver universal7880_ext_dai[] = {
	{
		.name = "universal7880 voice call",
		.playback = {
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			 SNDRV_PCM_FMTBIT_S24_LE)
		},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			 SNDRV_PCM_FMTBIT_S24_LE)
		},
	},
	{
		.name = "universal7880 BT",
		.playback = {
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			 SNDRV_PCM_FMTBIT_S24_LE)
		},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			 SNDRV_PCM_FMTBIT_S24_LE)
		},
	},
	{
		.name = "universal7880 FM",
		.playback = {
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000),
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			 SNDRV_PCM_FMTBIT_S24_LE)
		},
	},
};

static struct snd_soc_dai_link_component codecs_ap0[] = {{
		.name = "14880000.s1402x",
		.dai_name = "AP0",
	}, {
		.dai_name = "cod3026x-aif",
	},
};

static struct snd_soc_dai_link_component codecs_cp0[] = {{
		.name = "14880000.s1402x",
		.dai_name = "CP0",
	}, {
		.dai_name = "cod3026x-aif2",
	},
};

static struct snd_soc_dai_link_component codecs_bt[] = {{
		.name = "14880000.s1402x",
		.dai_name = "BT",
	}, {
		.dai_name = "dummy-aif2",
	},
};

static struct snd_soc_dai_link_component codecs_fm[] = {{
		.name = "14880000.s1402x",
		.dai_name = "FM",
	}, {
		.dai_name = "cod3026x-aif",
	},
};

static struct snd_soc_dai_link_component codecs_ap1[] = {{
		.name = "14880000.s1402x",
		.dai_name = "AP1",
	}, {
		.dai_name = "dummy-aif2",
	},
};

static struct snd_soc_dai_link_component codecs_cp1[] = {{
		.name = "14880000.s1402x",
		.dai_name = "CP1",
	}, {
		.dai_name = "dummy-aif2",
	},
};
static struct snd_soc_dai_link universal7880_cod3026x_dai[] = {
	/* Playback and Recording */
	{
		.name = "universal7880-cod3026x",
		.stream_name = "i2s0-pri",
		.codecs = codecs_ap0,
		.num_codecs = ARRAY_SIZE(codecs_ap0),
		.ops = &universal7880_aif1_ops,
	},
	/* Deep buffer playback */
	{
		.name = "universal7880-cod3026x-sec",
		.cpu_dai_name = "samsung-i2s-sec",
		.stream_name = "i2s0-sec",
		.platform_name = "samsung-i2s-sec",
		.codecs = codecs_ap0,
		.num_codecs = ARRAY_SIZE(codecs_ap0),
		.ops = &universal7880_aif1_ops,
	},
	/* Voice Call */
	{
		.name = "cp",
		.stream_name = "voice call",
		.cpu_dai_name = "universal7880 voice call",
		.platform_name = "snd-soc-dummy",
		.codecs = codecs_cp0,
		.num_codecs = ARRAY_SIZE(codecs_cp0),
		.ops = &universal7880_aif2_ops,
		.ignore_suspend = 1,
	},
	/* BT */
	{
		.name = "bt",
		.stream_name = "bluetooth audio",
		.cpu_dai_name = "universal7880 BT",
		.platform_name = "snd-soc-dummy",
		.codecs = codecs_bt,
		.num_codecs = ARRAY_SIZE(codecs_bt),
		.ops = &universal7880_aif3_ops,
		.ignore_suspend = 1,
	},
	/* FM */
	{
		.name = "fm",
		.stream_name = "FM audio",
		.cpu_dai_name = "universal7880 FM",
		.platform_name = "snd-soc-dummy",
		.codecs = codecs_fm,
		.num_codecs = ARRAY_SIZE(codecs_fm),
		.ops = &universal7880_aif4_ops,
		.ignore_suspend = 1,
	},
	/* AMP AP Interface */
	{
		.name = "universal7880-cod3026x-amp",
		.stream_name = "i2s1-pri",
		.codecs = codecs_ap1,
		.num_codecs = ARRAY_SIZE(codecs_ap1),
		.ops = &universal7880_aif5_ops,
	},

	/* AMP CP Interface */
	{
		.name = "cp-amp",
		.stream_name = "voice call amp",
		.cpu_dai_name = "universal7880 voice call",
		.platform_name = "snd-soc-dummy",
		.codecs = codecs_cp1,
		.num_codecs = ARRAY_SIZE(codecs_cp1),
		.ops = &universal7880_aif6_ops,
		.ignore_suspend = 1,
	},

	/* SW MIXER1 Interface */
	{
		.name = "playback-eax0",
		.stream_name = "eax0",
		.cpu_dai_name = "samsung-eax.0",
		.platform_name = "samsung-eax.0",
		.codecs = codecs_ap0,
		.num_codecs = ARRAY_SIZE(codecs_ap0),
		.ops = &universal7880_aif1_ops,
		.ignore_suspend = 1,
	},

	/* SW MIXER2 Interface */
	{
		.name = "playback-eax1",
		.stream_name = "eax1",
		.cpu_dai_name = "samsung-eax.1",
		.platform_name = "samsung-eax.1",
		.codecs = codecs_ap0,
		.num_codecs = ARRAY_SIZE(codecs_ap0),
		.ops = &universal7880_aif1_ops,
		.ignore_suspend = 1,
	},

	/* SW MIXER3 Interface */
	{
		.name = "playback-eax2",
		.stream_name = "eax2",
		.cpu_dai_name = "samsung-eax.2",
		.platform_name = "samsung-eax.2",
		.codecs = codecs_ap0,
		.num_codecs = ARRAY_SIZE(codecs_ap0),
		.ops = &universal7880_aif1_ops,
		.ignore_suspend = 1,
	},

	/* SW MIXER4 Interface */
	{
		.name = "playback-eax3",
		.stream_name = "eax3",
		.cpu_dai_name = "samsung-eax.3",
		.platform_name = "samsung-eax.3",
		.codecs = codecs_ap0,
		.num_codecs = ARRAY_SIZE(codecs_ap0),
		.ops = &universal7880_aif1_ops,
		.ignore_suspend = 1,
	},
};

static struct snd_soc_aux_dev audmixer_aux_dev[] = {
	{
		.init = audmixer_init,
	},
};

static struct snd_soc_codec_conf audmixer_codec_conf[] = {
	{
		.name_prefix = "AudioMixer",
	},
};

static struct snd_soc_card universal7880_cod3026x_card = {
	.name = "universal7880-I2S",
	.owner = THIS_MODULE,

	.dai_link = universal7880_cod3026x_dai,
	.num_links = ARRAY_SIZE(universal7880_cod3026x_dai),

	.controls = universal7880_controls,
	.num_controls = ARRAY_SIZE(universal7880_controls),
	.dapm_widgets = universal7880_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(universal7880_dapm_widgets),
	.dapm_routes = universal7880_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(universal7880_dapm_routes),

	.late_probe = universal7880_late_probe,
	.set_bias_level = universal7880_set_bias_level,
	.aux_dev = audmixer_aux_dev,
	.num_aux_devs = ARRAY_SIZE(audmixer_aux_dev),
	.codec_conf = audmixer_codec_conf,
	.num_configs = ARRAY_SIZE(audmixer_codec_conf),
};

static void universal7880_mic_bias_parse_dt(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct cod3026x_machine_priv *priv = snd_soc_card_get_drvdata(card);
	int ret;
	int i;
	int gpio, gpio_cnt;

	for (i = 0; i < INT_INPUT_MAX; i++) {
		priv->mic_bias.mode[i] = MB_NONE;
		priv->mic_bias.gpio[i] = -EINVAL;
	}

	for ( i = 0; i < MB_MAX; i++)
		atomic_set(&priv->mic_bias_count.use_count[i], 0);

	ret = of_property_read_u32_array(np, "mic-bias-mode",
			priv->mic_bias.mode, INT_INPUT_MAX);
	if (ret) {
		dev_err(&pdev->dev, "Could not read `mic-bias-mode`\n");
		return;
	}

	for (i = 0, gpio_cnt = 0; i < INT_INPUT_MAX; i++) {
		if (priv->mic_bias.mode[i] == MB_EXT_GPIO) {
			gpio = of_get_named_gpio(np, "mic-bias-gpios",
					gpio_cnt++);
			if (gpio_is_valid(gpio))
				priv->mic_bias.gpio[i] = gpio;
			else
				dev_err(&pdev->dev, "Invalid mic-bias gpio\n");
		}
	}

	dev_dbg(&pdev->dev, "BIAS: MIC1(%d), MIC2(%d), MIC3(%d), LINEIN(%d)\n",
			priv->mic_bias.mode[INT_MIC1], priv->mic_bias.mode[INT_MIC2],
			priv->mic_bias.mode[INT_MIC3], priv->mic_bias.mode[INT_LINEIN]);
}

static int universal7880_audio_probe(struct platform_device *pdev)
{
	int n, ret;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *cpu_np, *codec_np, *auxdev_np;
	struct snd_soc_card *card = &universal7880_cod3026x_card;
	struct cod3026x_machine_priv *priv;

	if (!np) {
		dev_err(&pdev->dev, "Failed to get device node\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	card->dev = &pdev->dev;
	card->num_links = 0;

	ret = snd_soc_register_component(card->dev, &universal7880_cmpnt,
			universal7880_ext_dai,
			ARRAY_SIZE(universal7880_ext_dai));
	if (ret) {
		dev_err(&pdev->dev, "Failed to register component: %d\n", ret);
		return ret;
	}

	for (n = 0; n < ARRAY_SIZE(universal7880_cod3026x_dai); n++) {
		/* Skip parsing DT for fully formed dai links */
		if (universal7880_cod3026x_dai[n].platform_name &&
				universal7880_cod3026x_dai[n].codec_name) {
			dev_dbg(card->dev,
			"Skipping dt for populated dai link %s\n",
			universal7880_cod3026x_dai[n].name);
			card->num_links++;
			continue;
		}

		cpu_np = of_parse_phandle(np, "samsung,audio-cpu", n);
		if (!cpu_np) {
			dev_err(&pdev->dev,
				"Property 'samsung,audio-cpu' missing\n");
			break;
		}

		codec_np = of_parse_phandle(np, "samsung,audio-codec", n);
		if (!codec_np) {
			dev_err(&pdev->dev,
				"Property 'samsung,audio-codec' missing\n");
			break;
		}

		universal7880_cod3026x_dai[n].codecs[1].of_node = codec_np;
		if (!universal7880_cod3026x_dai[n].cpu_dai_name)
			universal7880_cod3026x_dai[n].cpu_of_node = cpu_np;
		if (!universal7880_cod3026x_dai[n].platform_name)
			universal7880_cod3026x_dai[n].platform_of_node = cpu_np;

		card->num_links++;
	}

	for (n = 0; n < ARRAY_SIZE(audmixer_aux_dev); n++) {
		auxdev_np = of_parse_phandle(np, "samsung,auxdev", n);
		if (!auxdev_np) {
			dev_err(&pdev->dev,
				"Property 'samsung,auxdev' missing\n");
			return -EINVAL;
		}

		audmixer_aux_dev[n].codec_of_node = auxdev_np;
		audmixer_codec_conf[n].of_node = auxdev_np;
	}

	priv->vdd = devm_regulator_get(card->dev, "mic-bias-ldo");

	if (IS_ERR(priv->vdd)) {
		dev_warn(card->dev, "failed to get mic-bias-ldo vdd\n");
	}

	snd_soc_card_set_drvdata(card, priv);

	ret = snd_soc_register_card(card);
	if (ret)
		dev_err(&pdev->dev, "Failed to register card:%d\n", ret);

	if (of_find_property(np, "stand-alone-fm", NULL)) {
		universal7880_aif4_ops.hw_params = universal7880_aif4_hw_params;
		codecs_fm[0].dai_name = "FM";
	} else {
		universal7880_aif4_ops.hw_params = universal7880_aif3_hw_params;
		codecs_fm[0].dai_name = "BT";
	}

	universal7880_mic_bias_parse_dt(pdev);

	universal7880_init_soundcard(card);

	return ret;
}

static int universal7880_audio_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static const struct of_device_id universal7880_cod3026x_of_match[] = {
	{.compatible = "samsung,universal7880-cod3026x",},
	{},
};
MODULE_DEVICE_TABLE(of, universal7880_cod3026x_of_match);

static struct platform_driver universal7880_audio_driver = {
	.driver = {
		.name = "universal7880-audio",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(universal7880_cod3026x_of_match),
	},
	.probe = universal7880_audio_probe,
	.remove = universal7880_audio_remove,
};

module_platform_driver(universal7880_audio_driver);

MODULE_DESCRIPTION("ALSA SoC universal7880 COD3026X");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:universal7880-audio");
