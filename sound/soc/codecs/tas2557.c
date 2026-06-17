// SPDX-License-Identifier: GPL-2.0-only
//
// ALSA SoC Texas Instruments TAS2557 Smart Amplifier
//
// Copyright (C) 2016 Texas Instruments Inc.
// Copyright (C) 2026 Gianluca Boiano <morf3089@gmail.com>
//
// The TAS2557 requires DSP firmware (tas2557_uCDSP.bin for PG2.x,
// tas2557_pg1p0_uCDSP.bin for PG1.0) to route audio through its internal
// DSP and DAC.  Without loaded firmware the device produces no audio.
//
// Probe reads REV_PGID to detect PG version, then requests firmware
// asynchronously.  Every call to tas2557_enable() re-applies the firmware
// program + config (firmware-driven PLL) before powering the Class-D stage.

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/unaligned.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "tas2557.h"

/* Special delay markers embedded in register sequence tables */
#define TAS2557_UDELAY	0xFFFFFFFE
#define TAS2557_MDELAY	0xFFFFFFFD

/* Supported PCM formats exposed to the ASoC core */
#define TAS2557_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
	 SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

/* struct tas2557_block - one block of register-write commands from firmware */
struct tas2557_block {
	u32 type;
	u32 num_commands;
	u8 *data;		/* num_commands * 4 bytes: [book][page][reg][val] */
};

/* struct tas2557_data - named container of firmware blocks */
struct tas2557_data {
	char  name[64];
	u32   num_blocks;
	struct tas2557_block *blocks;
};

/* struct tas2557_pll - one PLL configuration */
struct tas2557_pll {
	char  name[64];
	struct tas2557_block block;
};

/* struct tas2557_program - one DSP program */
struct tas2557_program {
	char  name[64];
	struct tas2557_data data;
};

/* struct tas2557_config - one audio configuration (tied to a sample rate) */
struct tas2557_config {
	char  name[64];
	u32   program;		/* which program this config is for */
	u32   pll;		/* which PLL config to use */
	u32   sample_rate;
	struct tas2557_data data;
};

/* struct tas2557_firmware - top-level parsed firmware container */
struct tas2557_firmware {
	u32   ppc_version;
	u32   fw_version;
	u32   driver_version;
	u32   device;
	u32   num_plls;
	struct tas2557_pll     *plls;
	u32   num_programs;
	struct tas2557_program *programs;
	u32   num_configs;
	struct tas2557_config  *configs;
};

/* struct tas2557_priv - per-device driver state */
struct tas2557_priv {
	struct device	*dev;
	struct regmap	*regmap;
	struct mutex	 dev_lock;	/* protects device register I/O and book/page state */
	struct mutex	 lock;		/* protects all other driver state below */

	struct gpio_desc *reset_gpio;
	int		  irq;
	bool		  irq_enabled;

	/* Book/page tracking for book-paged regmap */
	unsigned char current_book;
	unsigned char current_page;

	/* Hardware revision */
	int pg_id;

	/* DT-configured audio parameters */
	unsigned int channel;		/* 0=left, 1=right from ti,channel */
	unsigned int imon_slot;		/* from ti,imon-slot-no */
	unsigned int vmon_slot;		/* from ti,vmon-slot-no */
	bool	     isense_enabled;
	bool	     vsense_enabled;

	/* Power state */
	bool powered;
	bool muted;

	/* Firmware state */
	struct tas2557_firmware *fw;	/* NULL until tas2557_fw_ready() parses it */
	char fw_name[64];		/* filename requested at probe */
	bool fw_requested;		/* request_firmware_nowait() call is pending */
	struct completion fw_done;	/* signalled when tas2557_fw_ready() returns */
	unsigned int current_program;
	unsigned int current_config;

	/* Runtime audio configuration */
	unsigned int sample_rate;

	/*
	 * Cached ASI data-slot offset in BCLK cycles.  Computed in
	 * hw_params()/set_tdm_slot() and re-applied in tas2557_enable()
	 * after the SW reset in tas2557_set_program() wipes the ASI
	 * offset registers.
	 */
	unsigned int asi_offset;
	int tdm_slot_width;

	/* Health tracking */
	unsigned int restart_count;

	/* Volume control */
	unsigned int dac_gain;
};

/* Forward declarations */
static void tas2557_hw_reset(struct tas2557_priv *tas2557);
static int tas2557_failsafe_recovery(struct tas2557_priv *tas2557);

/*
 * Default device initialization sequence - identical to the TI Android
 * driver (p_tas2557_default_data).  Written once after every
 * hardware/software reset.
 */
static const unsigned int tas2557_default_data[] = {
	TAS2557_SAR_ADC2_REG,  0x05,	/* enable SAR ADC */
	TAS2557_CLK_ERR_CTRL2, 0x21,	/* clk1: hysteresis 0.34 ms */
	TAS2557_CLK_ERR_CTRL3, 0x21,	/* clk2: rampDown 15 dB/us */
	TAS2557_SAFE_GUARD_REG, TAS2557_SAFE_GUARD_PATTERN,
	0xFFFFFFFF, 0xFFFFFFFF
};

/*
 * Interrupt output configuration - identical to the TI reference
 * (p_tas2557_irq_config).  Reloaded after every reset.
 */
static const unsigned int tas2557_irq_config[] = {
	TAS2557_CLK_HALT_REG, 0x71,	/* enable clk-halt detect2 interrupt */
	TAS2557_INT_GEN1_REG, 0x11,	/* enable SPK OC and OV */
	TAS2557_INT_GEN2_REG, 0x11,	/* enable clk-err1 and die OT */
	TAS2557_INT_GEN3_REG, 0x11,	/* enable clk-err2 and brownout */
	TAS2557_INT_GEN4_REG, 0x01,	/* disable SAR, enable clk-halt */
	TAS2557_GPIO4_PIN_REG, 0x07,	/* GPIO4 = INT1 output */
	TAS2557_INT_MODE_REG,  0x80,	/* active-high, held until cleared */
	0xFFFFFFFF, 0xFFFFFFFF
};

/*
 * Startup sequence - power up Class-D, Boost, DSP/PLL clocks.
 * The ASI2 pin/divider setup (GPIO5-8) is the pin configuration from the
 * TI reference sequence; both ASI interfaces are enabled and configured
 * identically, and the board wiring determines which one carries audio.
 * PLL configuration is NOT done here; it comes from the firmware PLL
 * block loaded by tas2557_set_program() before this sequence runs.
 */
static const unsigned int tas2557_startup_data[] = {
	TAS2557_GPI_PIN_REG,              0x15,	/* enable DIN, MCLK, CCI */
	TAS2557_GPIO1_PIN_REG,            0x01,	/* enable BCLK (ASI1) */
	TAS2557_GPIO2_PIN_REG,            0x01,	/* enable WCLK (ASI1) */
	/* ASI2 GPIO wiring; arm the BCLK source (GPIO5) before its dividers */
	TAS2557_GPIO6_PIN_REG,            0x01,	/* GPIO6 = ASI2 WCLK input */
	TAS2557_GPIO8_PIN_REG,            0x02,	/* GPIO8 = ASI2 DIN */
	TAS2557_GPIO5_PIN_REG,            0x01,	/* GPIO5 = ASI2 BCLK input */
	TAS2557_ASI2_DAC_FORMAT_REG,      0x18,	/* ASI2: 32-bit I2S */
	TAS2557_ASI2_BDIV_CLK_SEL_REG,   0x01,
	TAS2557_ASI2_BDIV_CLK_RATIO_REG, 0x01,
	TAS2557_ASI2_BDIV_CLK_RATIO_REG, 0x81,	/* power up BDIV */
	TAS2557_ASI2_WDIV_CLK_RATIO_REG, 0x40,
	TAS2557_ASI2_WDIV_CLK_RATIO_REG, 0xc0,	/* power up WDIV */
	TAS2557_GPIO7_PIN_REG,            0x15,	/* GPIO7 = ASI2 DOUT */
	/* Power sequencing: Class-D + Boost first, then DSP/PLL */
	TAS2557_POWER_CTRL2_REG, 0xA0,		/* Class-D, Boost power up */
	TAS2557_POWER_CTRL2_REG, 0xA3,		/* Class-D, Boost, IV-sense power up */
	TAS2557_POWER_CTRL1_REG, 0xF8,		/* PLL, DSP, clock dividers power up */
	TAS2557_UDELAY, 2000,			/* 2 ms stabilisation */
	TAS2557_CLK_ERR_CTRL, 0x2b,		/* enable clock error detection */
	TAS2557_DBOOST_CFG_REG, 0x0b,		/* reduce full-band noise */
	0xFFFFFFFF, 0xFFFFFFFF
};

/* Unmute sequence */
static const unsigned int tas2557_unmute_data[] = {
	TAS2557_MUTE_REG,      0x00,	/* unmute Class-D + ISENSE */
	TAS2557_SOFT_MUTE_REG, 0x00,	/* clear soft-mute in DSP */
	0xFFFFFFFF, 0xFFFFFFFF
};

/* Shutdown sequence */
static const unsigned int tas2557_shutdown_data[] = {
	TAS2557_CLK_ERR_CTRL,   0x00,	/* disable clock error detection */
	TAS2557_SOFT_MUTE_REG,  0x01,	/* soft mute */
	TAS2557_UDELAY, 10000,		/* 10 ms */
	TAS2557_MUTE_REG,        0x03,	/* hard mute */
	TAS2557_POWER_CTRL1_REG, 0x60,	/* DSP power down */
	TAS2557_UDELAY, 2000,		/* 2 ms */
	TAS2557_POWER_CTRL2_REG, 0x00,	/* Class-D, Boost power down */
	TAS2557_POWER_CTRL1_REG, 0x00,	/* all power down */
	/* Disable ASI2 GPIOs */
	TAS2557_GPIO5_PIN_REG, 0x00,
	TAS2557_GPIO6_PIN_REG, 0x00,
	TAS2557_GPIO7_PIN_REG, 0x00,
	TAS2557_GPIO8_PIN_REG, 0x00,
	TAS2557_GPIO1_PIN_REG, 0x00,
	TAS2557_GPIO2_PIN_REG, 0x00,
	TAS2557_GPI_PIN_REG,   0x00,
	0xFFFFFFFF, 0xFFFFFFFF
};

/*
 * regmap configuration
 *
 * Book/page switching is handled manually (see below); regmap only sees
 * the raw 7-bit page-register address, with caching disabled because the
 * TAS2557 firmware can update registers autonomously.
 */
static const struct regmap_config tas2557_regmap_config = {
	.reg_bits    = 8,
	.val_bits    = 8,
	.cache_type  = REGCACHE_NONE,
	.max_register = 0x7f,
};

/*
 * Book / page / register access
 *
 * The TAS2557 address space uses a book + page prefix.  The regmap is
 * configured for 8-bit register width; book/page selection is done by
 * writing to TAS2557_BOOK_REG (0x7f) and TAS2557_PAGE_REG (0x00).
 *
 * All helpers take a composite register address encoded as
 *   TAS2557_REG(book, page, reg) = book*256*128 + page*128 + reg
 */
static int tas2557_change_book_page(struct tas2557_priv *tas2557,
				    unsigned char book, unsigned char page)
{
	int ret = 0;

	if (tas2557->current_book == book && tas2557->current_page == page)
		return 0;

	if (tas2557->current_book != book) {
		/* Always switch to page 0 before changing books */
		ret = regmap_write(tas2557->regmap, TAS2557_PAGE_REG, 0);
		if (ret < 0) {
			dev_err(tas2557->dev, "page-0 switch failed: %d\n", ret);
			return ret;
		}
		tas2557->current_page = 0;

		ret = regmap_write(tas2557->regmap, TAS2557_BOOK_REG, book);
		if (ret < 0) {
			dev_err(tas2557->dev, "book switch to %u failed: %d\n",
				book, ret);
			return ret;
		}
		tas2557->current_book = book;
	}

	if (tas2557->current_page != page) {
		ret = regmap_write(tas2557->regmap, TAS2557_PAGE_REG, page);
		if (ret < 0) {
			dev_err(tas2557->dev, "page switch to %u failed: %d\n",
				page, ret);
			return ret;
		}
		tas2557->current_page = page;
	}

	return 0;
}

static int tas2557_dev_read(struct tas2557_priv *tas2557,
			    unsigned int reg, unsigned int *value)
{
	int ret;

	mutex_lock(&tas2557->dev_lock);
	ret = tas2557_change_book_page(tas2557, TAS2557_BOOK_ID(reg),
				       TAS2557_PAGE_ID(reg));
	if (ret < 0)
		goto out;

	ret = regmap_read(tas2557->regmap, TAS2557_PAGE_REG_ADDR(reg), value);
	if (ret < 0)
		dev_err(tas2557->dev, "read reg 0x%06x failed: %d\n", reg, ret);
out:
	mutex_unlock(&tas2557->dev_lock);
	return ret;
}

static int tas2557_dev_write(struct tas2557_priv *tas2557,
			     unsigned int reg, unsigned int value)
{
	int ret;

	mutex_lock(&tas2557->dev_lock);
	ret = tas2557_change_book_page(tas2557, TAS2557_BOOK_ID(reg),
				       TAS2557_PAGE_ID(reg));
	if (ret < 0)
		goto out;

	ret = regmap_write(tas2557->regmap, TAS2557_PAGE_REG_ADDR(reg), value);
	if (ret < 0)
		dev_err(tas2557->dev, "write reg 0x%06x = 0x%02x failed: %d\n",
			reg, value, ret);
out:
	mutex_unlock(&tas2557->dev_lock);
	return ret;
}

static int tas2557_dev_update_bits(struct tas2557_priv *tas2557,
				   unsigned int reg, unsigned int mask,
				   unsigned int value)
{
	int ret;

	mutex_lock(&tas2557->dev_lock);
	ret = tas2557_change_book_page(tas2557, TAS2557_BOOK_ID(reg),
				       TAS2557_PAGE_ID(reg));
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(tas2557->regmap,
				 TAS2557_PAGE_REG_ADDR(reg), mask, value);
	if (ret < 0)
		dev_err(tas2557->dev, "update_bits reg 0x%06x failed: %d\n",
			reg, ret);
out:
	mutex_unlock(&tas2557->dev_lock);
	return ret;
}

static int tas2557_dev_bulk_write(struct tas2557_priv *tas2557,
				  unsigned int reg, const u8 *data, size_t len)
{
	int ret;

	/* The chip auto-increments past reg 0x7f into the page register */
	if (TAS2557_PAGE_REG_ADDR(reg) + len > 128)
		return -EINVAL;

	mutex_lock(&tas2557->dev_lock);
	ret = tas2557_change_book_page(tas2557, TAS2557_BOOK_ID(reg),
				       TAS2557_PAGE_ID(reg));
	if (ret < 0)
		goto out;

	ret = regmap_bulk_write(tas2557->regmap,
				TAS2557_PAGE_REG_ADDR(reg), data, len);
	if (ret < 0)
		dev_err(tas2557->dev, "bulk_write reg 0x%06x failed: %d\n",
			reg, ret);
out:
	mutex_unlock(&tas2557->dev_lock);
	return ret;
}

/*
 * tas2557_load_data - walk a null-terminated {reg,val} sequence table
 *
 * Special reg values TAS2557_UDELAY and TAS2557_MDELAY insert delays;
 * 0xFFFFFFFF terminates.
 */
static int tas2557_load_data(struct tas2557_priv *tas2557,
			     const unsigned int *data)
{
	unsigned int reg, val;
	int i = 0, ret = 0;

	while (1) {
		reg = data[i * 2];
		val = data[i * 2 + 1];

		if (reg == 0xFFFFFFFF)
			break;

		if (reg == TAS2557_UDELAY) {
			usleep_range(val, val + 100);
		} else if (reg == TAS2557_MDELAY) {
			msleep(val);
		} else {
			ret = tas2557_dev_write(tas2557, reg, val);
			if (ret < 0)
				break;
		}
		i++;
	}

	return ret;
}

/* =========================================================================
 * Firmware parsing
 *
 * Format (all multi-byte fields are big-endian):
 *   4 bytes  magic  0x35 0x35 0x35 0x32  ("5552")
 *   4 bytes  size
 *   4 bytes  checksum
 *   4 bytes  ppc_version
 *   4 bytes  fw_version
 *   4 bytes  driver_version
 *   4 bytes  timestamp
 *  64 bytes  ddc_name (null-padded)
 *   N bytes  description (null-terminated string)
 *   4 bytes  device_family  (must be 0)
 *   4 bytes  device         (2 = mono, 3 = stereo)
 *   then PLLs, programs, configs
 *
 * The on-disk layout follows the TI reference firmware format.  Fields
 * that the driver does not need at runtime (size, checksum, timestamp,
 * ddc_name, description, per-block checksums, ...) are consumed from the
 * byte stream to keep the parser in sync but are not stored.
 * =========================================================================
 */

static void tas2557_fw_free(struct tas2557_firmware *fw)
{
	unsigned int i, j;

	if (!fw)
		return;

	if (fw->plls) {
		for (i = 0; i < fw->num_plls; i++)
			kfree(fw->plls[i].block.data);
		kfree(fw->plls);
	}

	if (fw->programs) {
		for (i = 0; i < fw->num_programs; i++) {
			if (fw->programs[i].data.blocks) {
				for (j = 0; j < fw->programs[i].data.num_blocks; j++)
					kfree(fw->programs[i].data.blocks[j].data);
				kfree(fw->programs[i].data.blocks);
			}
		}
		kfree(fw->programs);
	}

	if (fw->configs) {
		for (i = 0; i < fw->num_configs; i++) {
			if (fw->configs[i].data.blocks) {
				for (j = 0; j < fw->configs[i].data.num_blocks; j++)
					kfree(fw->configs[i].data.blocks[j].data);
				kfree(fw->configs[i].data.blocks);
			}
		}
		kfree(fw->configs);
	}

	kfree(fw);
}

static int fw_parse_header(struct tas2557_priv *tas2557,
			   struct tas2557_firmware *fw,
			   const u8 *data, size_t size)
{
	const u8 *start = data;
	size_t desc_len;
	u32 device_family;
	char ddc_name[64];

	if (size < 104) {
		dev_err(tas2557->dev, "firmware header too short (%zu bytes)\n",
			size);
		return -EINVAL;
	}

	/* Magic: four bytes "5552" (0x35 0x35 0x35 0x32) */
	if (data[0] != 0x35 || data[1] != 0x35 ||
	    data[2] != 0x35 || data[3] != 0x32) {
		dev_err(tas2557->dev,
			"firmware magic mismatch: %02x%02x%02x%02x (expected 35353532)\n",
			data[0], data[1], data[2], data[3]);
		return -EINVAL;
	}
	data += 4;

	/* size, checksum: not needed at runtime, skip */
	data += 8;

	fw->ppc_version    = get_unaligned_be32(data); data += 4;
	fw->fw_version     = get_unaligned_be32(data); data += 4;
	fw->driver_version = get_unaligned_be32(data); data += 4;

	/* timestamp: not needed at runtime, skip */
	data += 4;

	memcpy(ddc_name, data, 64);
	ddc_name[63] = '\0';
	data += 64;

	desc_len = strnlen(data, size - (size_t)(data - start));
	if (desc_len >= size - (size_t)(data - start)) {
		dev_err(tas2557->dev, "firmware header: description not NUL-terminated\n");
		return -EINVAL;
	}
	/* description string: informational only, not stored */
	data += desc_len + 1;

	if ((data - start) + 8 > (ptrdiff_t)size) {
		dev_err(tas2557->dev, "firmware header truncated\n");
		return -EINVAL;
	}

	device_family = get_unaligned_be32(data); data += 4;
	if (device_family != 0) {
		dev_err(tas2557->dev, "unsupported device family: %u\n",
			device_family);
		return -EINVAL;
	}

	fw->device = get_unaligned_be32(data); data += 4;
	if (fw->device != TAS2557_FW_DEVICE_MONO &&
	    fw->device != TAS2557_FW_DEVICE_STEREO) {
		dev_err(tas2557->dev,
			"unsupported device %u in firmware (expected 2=mono or 3=stereo)\n",
			fw->device);
		return -EINVAL;
	}

	dev_dbg(tas2557->dev,
		"firmware header: DDC=%s fw_ver=0x%x ppc=0x%x drv=0x%x\n",
		ddc_name, fw->fw_version, fw->ppc_version, fw->driver_version);

	return data - start;
}

static int fw_parse_block(struct tas2557_priv *tas2557,
			  struct tas2557_firmware *fw,
			  struct tas2557_block *block,
			  const u8 *data, size_t remaining)
{
	const u8 *start = data;
	size_t data_len;

	if (remaining < 4)
		return -EINVAL;

	block->type = get_unaligned_be32(data);
	data += 4;
	remaining -= 4;

	if (fw->driver_version >= PPC_DRIVER_CRCCHK) {
		/* Per-block checksum bytes (pchksum/ychksum): not used, skip */
		if (remaining < 4)
			return -EINVAL;
		data += 4;
		remaining -= 4;
	}

	if (remaining < 4)
		return -EINVAL;

	block->num_commands = get_unaligned_be32(data);
	data += 4;
	remaining -= 4;

	/*
	 * num_commands comes straight from the firmware file; bound it
	 * against the bytes left before multiplying so the size cannot wrap.
	 */
	if (block->num_commands > remaining / 4) {
		dev_err(tas2557->dev,
			"block type 0x%x truncated: %u cmds, %zu bytes left\n",
			block->type, block->num_commands, remaining);
		return -EINVAL;
	}
	data_len = (size_t)block->num_commands * 4;

	if (data_len > 0) {
		block->data = kmemdup(data, data_len, GFP_KERNEL);
		if (!block->data)
			return -ENOMEM;
	}
	data += data_len;

	return data - start;
}

static int fw_parse_data(struct tas2557_priv *tas2557,
			 struct tas2557_firmware *fw,
			 struct tas2557_data *img_data,
			 const u8 *data, size_t remaining)
{
	const u8 *start = data;
	size_t desc_len;
	unsigned int i;
	int ret;

	if (remaining < 64)
		return -EINVAL;

	memcpy(img_data->name, data, 64);
	img_data->name[63] = '\0';
	data += 64;
	remaining -= 64;

	desc_len = strnlen(data, remaining);
	if (desc_len >= remaining)
		return -EINVAL;
	/* description string: informational only, not stored */
	data += desc_len + 1;
	remaining -= desc_len + 1;

	if (remaining < 2)
		return -EINVAL;

	img_data->num_blocks = get_unaligned_be16(data);
	data += 2;
	remaining -= 2;

	if (img_data->num_blocks == 0)
		return data - start;

	img_data->blocks = kcalloc(img_data->num_blocks,
				   sizeof(struct tas2557_block), GFP_KERNEL);
	if (!img_data->blocks)
		return -ENOMEM;

	for (i = 0; i < img_data->num_blocks; i++) {
		ret = fw_parse_block(tas2557, fw, &img_data->blocks[i],
				     data, remaining);
		if (ret < 0)
			return ret;
		data += ret;
		remaining -= ret;
	}

	return data - start;
}

static int fw_parse_plls(struct tas2557_priv *tas2557,
			 struct tas2557_firmware *fw,
			 const u8 *data, size_t remaining)
{
	const u8 *start = data;
	size_t desc_len;
	unsigned int i;
	int ret;

	if (remaining < 2)
		return -EINVAL;

	fw->num_plls = get_unaligned_be16(data);
	data += 2;
	remaining -= 2;

	if (fw->num_plls == 0)
		return data - start;

	fw->plls = kcalloc(fw->num_plls, sizeof(struct tas2557_pll),
			   GFP_KERNEL);
	if (!fw->plls)
		return -ENOMEM;

	for (i = 0; i < fw->num_plls; i++) {
		if (remaining < 64)
			return -EINVAL;

		memcpy(fw->plls[i].name, data, 64);
		fw->plls[i].name[63] = '\0';
		data += 64;
		remaining -= 64;

		desc_len = strnlen(data, remaining);
		if (desc_len >= remaining)
			return -EINVAL;
		data += desc_len + 1;
		remaining -= desc_len + 1;

		ret = fw_parse_block(tas2557, fw, &fw->plls[i].block,
				     data, remaining);
		if (ret < 0)
			return ret;
		data += ret;
		remaining -= ret;
	}

	return data - start;
}

static int fw_parse_programs(struct tas2557_priv *tas2557,
			     struct tas2557_firmware *fw,
			     const u8 *data, size_t remaining)
{
	const u8 *start = data;
	size_t desc_len;
	unsigned int i;
	int ret;

	if (remaining < 2)
		return -EINVAL;

	fw->num_programs = get_unaligned_be16(data);
	data += 2;
	remaining -= 2;

	if (fw->num_programs == 0) {
		dev_err(tas2557->dev, "firmware contains no programs\n");
		return -EINVAL;
	}

	fw->programs = kcalloc(fw->num_programs, sizeof(struct tas2557_program),
			       GFP_KERNEL);
	if (!fw->programs)
		return -ENOMEM;

	for (i = 0; i < fw->num_programs; i++) {
		if (remaining < 64)
			return -EINVAL;

		memcpy(fw->programs[i].name, data, 64);
		fw->programs[i].name[63] = '\0';
		data += 64;
		remaining -= 64;

		desc_len = strnlen(data, remaining);
		if (desc_len >= remaining)
			return -EINVAL;
		data += desc_len + 1;
		remaining -= desc_len + 1;

		/* app_mode (1 byte) + boost (2 bytes): not used, skip */
		if (remaining < 3)
			return -EINVAL;
		data += 3;
		remaining -= 3;

		ret = fw_parse_data(tas2557, fw, &fw->programs[i].data,
				    data, remaining);
		if (ret < 0)
			return ret;
		data += ret;
		remaining -= ret;
	}

	return data - start;
}

static int fw_parse_configs(struct tas2557_priv *tas2557,
			    struct tas2557_firmware *fw,
			    const u8 *data, size_t remaining)
{
	const u8 *start = data;
	size_t desc_len;
	unsigned int i;
	int ret;

	if (remaining < 2)
		return -EINVAL;

	fw->num_configs = get_unaligned_be16(data);
	data += 2;
	remaining -= 2;

	if (fw->num_configs == 0) {
		dev_err(tas2557->dev, "firmware contains no configurations\n");
		return -EINVAL;
	}

	fw->configs = kcalloc(fw->num_configs, sizeof(struct tas2557_config),
			      GFP_KERNEL);
	if (!fw->configs)
		return -ENOMEM;

	for (i = 0; i < fw->num_configs; i++) {
		if (remaining < 64)
			return -EINVAL;

		memcpy(fw->configs[i].name, data, 64);
		fw->configs[i].name[63] = '\0';
		data += 64;
		remaining -= 64;

		desc_len = strnlen(data, remaining);
		if (desc_len >= remaining)
			return -EINVAL;
		data += desc_len + 1;
		remaining -= desc_len + 1;

		/*
		 * Device-count field present from driver_version 0x101.
		 * Consume it (unused here) across the [0x101, 0x1ff] band
		 * and for >= 0x300, matching the TI fw_parse_configuration.
		 */
		if (fw->driver_version >= PPC_DRIVER_CONFDEV ||
		    (fw->driver_version >= PPC_DRIVER_CFGDEV_NONCRC &&
		     fw->driver_version < PPC_DRIVER_CRCCHK)) {
			if (remaining < 2)
				return -EINVAL;
			data += 2; remaining -= 2;
		}

		if (remaining < 6)
			return -EINVAL;

		fw->configs[i].program     = data[0]; data++; remaining--;
		fw->configs[i].pll         = data[0]; data++; remaining--;
		fw->configs[i].sample_rate = get_unaligned_be32(data);
		data += 4; remaining -= 4;

		/* PLL source fields present when driver >= MTPLLSRC: skip */
		if (fw->driver_version >= PPC_DRIVER_MTPLLSRC) {
			if (remaining < 5)
				return -EINVAL;
			data += 5; remaining -= 5;
		}

		ret = fw_parse_data(tas2557, fw, &fw->configs[i].data,
				    data, remaining);
		if (ret < 0)
			return ret;
		data += ret;
		remaining -= ret;
	}

	return data - start;
}

static struct tas2557_firmware *fw_parse(struct tas2557_priv *tas2557,
					 const u8 *data, size_t size)
{
	struct tas2557_firmware *fw;
	size_t remaining = size;
	int ret;

	fw = kzalloc_obj(*fw, GFP_KERNEL);
	if (!fw)
		return ERR_PTR(-ENOMEM);

	ret = fw_parse_header(tas2557, fw, data, remaining);
	if (ret < 0)
		goto err;
	data += ret; remaining -= ret;

	ret = fw_parse_plls(tas2557, fw, data, remaining);
	if (ret < 0)
		goto err;
	data += ret; remaining -= ret;

	ret = fw_parse_programs(tas2557, fw, data, remaining);
	if (ret < 0)
		goto err;
	data += ret; remaining -= ret;

	ret = fw_parse_configs(tas2557, fw, data, remaining);
	if (ret < 0)
		goto err;

	return fw;

err:
	tas2557_fw_free(fw);
	return ERR_PTR(ret);
}

/* =========================================================================
 * Firmware application
 *
 * Each firmware block contains a sequence of 4-byte commands:
 *   [book][page][reg][val]   - single register write
 *   [hi][lo][0x81][--]       - msleep((hi<<8)|lo)
 *   [hi][lo][0x85][first]    - bulk write: length=(hi<<8)|lo, data follows
 * =========================================================================
 */

static int tas2557_load_block(struct tas2557_priv *tas2557,
			      struct tas2557_block *block)
{
	const u8 *data = block->data;
	unsigned int i;
	u8 book, page, offset, value;
	u16 sleep_time, bulk_len;
	int ret;

	dev_dbg(tas2557->dev, "load block type=0x%x cmds=%u\n",
		block->type, block->num_commands);

	for (i = 0; i < block->num_commands; ) {
		book   = data[i * 4];
		page   = data[i * 4 + 1];
		offset = data[i * 4 + 2];
		value  = data[i * 4 + 3];
		i++;

		if (offset <= 0x7f) {
			ret = tas2557_dev_write(tas2557,
						TAS2557_REG(book, page, offset),
						value);
			if (ret < 0)
				return ret;
		} else if (offset == 0x81) {
			sleep_time = ((u16)book << 8) | page;
			msleep(sleep_time);
		} else if (offset == 0x85) {
			bulk_len = ((u16)book << 8) | page;

			if (bulk_len == 0)
				return -EINVAL;

			if (i >= block->num_commands)
				return -EINVAL;
			/* Verify bulk payload stays within block->data */
			if ((size_t)i * 4 + 3 + bulk_len >
			    (size_t)block->num_commands * 4)
				return -EINVAL;

			book   = data[i * 4];
			page   = data[i * 4 + 1];
			offset = data[i * 4 + 2];

			if (bulk_len > 1)
				ret = tas2557_dev_bulk_write(tas2557,
							     TAS2557_REG(book, page, offset),
							     &data[i * 4 + 3], bulk_len);
			else
				ret = tas2557_dev_write(tas2557,
							TAS2557_REG(book, page, offset),
							data[i * 4 + 3]);
			if (ret < 0)
				return ret;

			i++;
			if (bulk_len >= 2)
				i += (bulk_len - 2) / 4 + 1;
		}
	}

	return 0;
}

/*
 * tas2557_block_wanted - should a firmware block be applied to this instance?
 *
 * Stereo (device == 3) firmware carries tuning for two amplifiers: DEV_A
 * blocks for the left channel and DEV_B blocks for the right.  An instance
 * applies the device-independent blocks (PGM_ALL, CFG_POST*) plus the blocks
 * for its own channel.  Mono (device == 2) firmware has no DEV_B blocks, so
 * @want_dev_b is always false and the DEV_A blocks are applied.  Any other
 * (unknown) block type is rejected rather than applied to both devices.
 */
static bool tas2557_block_wanted(unsigned int type, bool want_dev_b)
{
	switch (type) {
	case TAS2557_BLOCK_PGM_DEV_A:
	case TAS2557_BLOCK_CFG_COEFF_DEV_A:
	case TAS2557_BLOCK_CFG_PRE_DEV_A:
		return !want_dev_b;
	case TAS2557_BLOCK_PGM_DEV_B:
	case TAS2557_BLOCK_CFG_COEFF_DEV_B:
	case TAS2557_BLOCK_CFG_PRE_DEV_B:
		return want_dev_b;
	case TAS2557_BLOCK_PGM_ALL:
	case TAS2557_BLOCK_CFG_POST:
	case TAS2557_BLOCK_CFG_POST_POWER:
		/* device-independent blocks: apply to either instance */
		return true;
	default:
		/* unknown type in program data: do not apply blindly */
		return false;
	}
}

/*
 * tas2557_load_fw_data - apply the blocks of a data set for this channel
 * @want_dev_b: apply DEV_B blocks (right channel of stereo firmware) rather
 *              than DEV_A; device-independent blocks are applied either way
 */
static int tas2557_load_fw_data(struct tas2557_priv *tas2557,
				struct tas2557_data *img_data,
				bool want_dev_b)
{
	unsigned int i;
	int ret;

	for (i = 0; i < img_data->num_blocks; i++) {
		if (!tas2557_block_wanted(img_data->blocks[i].type, want_dev_b))
			continue;

		ret = tas2557_load_block(tas2557, &img_data->blocks[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
 * tas2557_load_fw_data_type - apply only the blocks of a single type.
 */
static int tas2557_load_fw_data_type(struct tas2557_priv *tas2557,
				     struct tas2557_data *img_data,
				     unsigned int type)
{
	unsigned int i;
	int ret;

	for (i = 0; i < img_data->num_blocks; i++) {
		if (img_data->blocks[i].type != type)
			continue;

		ret = tas2557_load_block(tas2557, &img_data->blocks[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
 * tas2557_load_config_data - apply a configuration's pre-power blocks in the
 * order the hardware expects: the pre-coefficient block, the coefficient
 * block, then the device-independent CFG_POST block.  The CFG_POST_POWER
 * block is NOT applied here; it needs active clocks and is loaded by
 * tas2557_load_config_post_power() after the device is powered up.
 * @want_dev_b selects the DEV_B (right) blocks of stereo firmware instead of
 * DEV_A (left); blocks are applied by type, not storage order (per TI).
 */
static int tas2557_load_config_data(struct tas2557_priv *tas2557,
				    struct tas2557_data *img_data,
				    bool want_dev_b)
{
	unsigned int pre, coeff;
	int ret;

	if (want_dev_b) {
		pre = TAS2557_BLOCK_CFG_PRE_DEV_B;
		coeff = TAS2557_BLOCK_CFG_COEFF_DEV_B;
	} else {
		pre = TAS2557_BLOCK_CFG_PRE_DEV_A;
		coeff = TAS2557_BLOCK_CFG_COEFF_DEV_A;
	}

	ret = tas2557_load_fw_data_type(tas2557, img_data, pre);
	if (ret < 0)
		return ret;

	ret = tas2557_load_fw_data_type(tas2557, img_data, coeff);
	if (ret < 0)
		return ret;

	return tas2557_load_fw_data_type(tas2557, img_data,
					TAS2557_BLOCK_CFG_POST);
}

/*
 * tas2557_load_config_post_power - apply the CFG_POST_POWER block of the
 * current configuration.  These register writes target DSP/clock state that
 * only latches once the Class-D/DSP power-up sequence has run, so this is
 * called from the power-up path after tas2557_startup_data, not during the
 * (powered-down) configuration load.
 */
static int tas2557_load_config_post_power(struct tas2557_priv *tas2557)
{
	struct tas2557_config *config;

	if (!tas2557->fw || tas2557->current_config >= tas2557->fw->num_configs)
		return 0;

	config = &tas2557->fw->configs[tas2557->current_config];

	return tas2557_load_fw_data_type(tas2557, &config->data,
					 TAS2557_BLOCK_CFG_POST_POWER);
}

static int tas2557_load_pll(struct tas2557_priv *tas2557, unsigned int pll_idx)
{
	if (!tas2557->fw || pll_idx >= tas2557->fw->num_plls)
		return -EINVAL;

	dev_dbg(tas2557->dev, "loading PLL[%u]: %s\n",
		pll_idx, tas2557->fw->plls[pll_idx].name);

	return tas2557_load_block(tas2557, &tas2557->fw->plls[pll_idx].block);
}

/*
 * tas2557_set_program - reset device and load firmware program + config
 *
 * This is the authoritative firmware-apply path.  It performs:
 *   (graceful shutdown if currently powered) -> hardware reset ->
 *   software reset -> default registers -> IRQ config ->
 *   program blocks (DSP coefficients) -> PLL block for config ->
 *   config blocks (sample-rate-specific settings)
 *
 * It does not re-power the device: callers that need the amplifier
 * running again after this call (tas2557_enable(), failsafe recovery)
 * re-apply the startup sequence themselves.
 *
 * PLL configuration comes entirely from the firmware PLL block; no manual
 * PLL register writes are performed here or anywhere else.
 *
 * Caller must hold tas2557->lock.
 *
 * @config_idx: -1 to auto-select based on current sample_rate (0 = any)
 */
static int tas2557_set_program(struct tas2557_priv *tas2557,
			       unsigned int prog_idx, int config_idx)
{
	struct tas2557_firmware *fw = tas2557->fw;
	struct tas2557_program *program;
	struct tas2557_config  *config;
	unsigned int cfg_idx;
	int ret;
	bool want_dev_b;

	lockdep_assert_held(&tas2557->lock);

	if (!fw || !fw->programs || !fw->configs) {
		dev_err(tas2557->dev, "firmware not loaded, cannot set program\n");
		return -EINVAL;
	}

	if (prog_idx >= fw->num_programs) {
		dev_err(tas2557->dev, "program %u out of range (%u total)\n",
			prog_idx, fw->num_programs);
		return -EINVAL;
	}

	/* Auto-select config: match program + sample_rate (0 = accept any) */
	if (config_idx < 0) {
		config_idx = -1;
		for (cfg_idx = 0; cfg_idx < fw->num_configs; cfg_idx++) {
			if (fw->configs[cfg_idx].program != prog_idx)
				continue;
			if (tas2557->sample_rate == 0 ||
			    tas2557->sample_rate ==
					fw->configs[cfg_idx].sample_rate) {
				config_idx = (int)cfg_idx;
				break;
			}
		}
		if (config_idx < 0) {
			dev_err(tas2557->dev,
				"no config for program %u at %u Hz\n",
				prog_idx, tas2557->sample_rate);
			return -EINVAL;
		}
	}

	if ((unsigned int)config_idx >= fw->num_configs) {
		dev_err(tas2557->dev, "config %d out of range (%u total)\n",
			config_idx, fw->num_configs);
		return -EINVAL;
	}

	config = &fw->configs[config_idx];
	if (config->program != prog_idx) {
		dev_err(tas2557->dev,
			"config %u belongs to program %u, not %u\n",
			config_idx, config->program, prog_idx);
		return -EINVAL;
	}

	program = &fw->programs[prog_idx];

	/* Ramp down gracefully before the reset if currently powered */
	if (tas2557->powered) {
		ret = tas2557_load_data(tas2557, tas2557_shutdown_data);
		if (ret < 0)
			return ret;
	}

	/* Hard + soft reset, then baseline register setup */
	tas2557_hw_reset(tas2557);

	ret = tas2557_dev_write(tas2557, TAS2557_SW_RESET_REG, 0x01);
	if (ret < 0)
		return ret;
	usleep_range(1000, 2000);

	ret = tas2557_load_data(tas2557, tas2557_default_data);
	if (ret < 0)
		return ret;

	/* Restore IRQ configuration lost by the reset */
	ret = tas2557_load_data(tas2557, tas2557_irq_config);
	if (ret < 0)
		dev_warn(tas2557->dev, "IRQ config reload failed: %d\n", ret);

	/*
	 * Stereo firmware (device == 3) carries DEV_A (left) and DEV_B (right)
	 * tuning; select this instance's channel.  Mono firmware has only
	 * DEV_A blocks, so this stays false.
	 */
	want_dev_b = fw->device == TAS2557_FW_DEVICE_STEREO &&
		     tas2557->channel == 1;

	/* Load DSP program blocks */
	dev_dbg(tas2557->dev, "loading program %u (%s)\n",
		prog_idx, program->name);
	ret = tas2557_load_fw_data(tas2557, &program->data, want_dev_b);
	if (ret < 0)
		return ret;

	tas2557->current_program = prog_idx;

	/* Load PLL configuration for this config (firmware-driven) */
	if (config->pll < fw->num_plls) {
		ret = tas2557_load_pll(tas2557, config->pll);
		if (ret < 0)
			return ret;
	}

	/* Load config blocks (coefficients, sample-rate-specific settings) */
	dev_dbg(tas2557->dev,
		"loading config %u (%s) rate=%u Hz PLL[%u]\n",
		config_idx, config->name, config->sample_rate, config->pll);
	ret = tas2557_load_config_data(tas2557, &config->data, want_dev_b);
	if (ret < 0)
		return ret;

	tas2557->current_config = config_idx;

	return 0;
}

static const char *tas2557_pg_name(int pg_id)
{
	switch (pg_id) {
	case TAS2557_PG_VERSION_1P0:
		return "PG1.0";
	case TAS2557_PG_VERSION_2P0:
		return "PG2.0";
	case TAS2557_PG_VERSION_2P1:
		return "PG2.1";
	default:
		return "PG?";
	}
}

/*
 * tas2557_fw_ready - firmware load callback
 *
 * Called from request_firmware_nowait().  Parses and stores the firmware,
 * then pre-loads program 0 / config 0 so the DSP registers are ready for
 * the first tas2557_enable() call.  Every path (missing firmware, parse
 * error, success) reaches the completion at the end, so
 * tas2557_fw_teardown() can always wait for this callback to finish.
 */
static void tas2557_fw_ready(const struct firmware *fw_entry, void *context)
{
	struct tas2557_priv *tas2557 = context;
	struct tas2557_firmware *fw = NULL;
	int ret;

	if (!fw_entry || !fw_entry->data || fw_entry->size == 0) {
		/*
		 * Firmware is required: without it the DSP cannot route
		 * audio and the Class-D stage stays muted.
		 */
		dev_err(tas2557->dev,
			"firmware '%s' not found, the amplifier cannot work without it\n",
			tas2557->fw_name);
		release_firmware(fw_entry);
		goto done;
	}

	dev_dbg(tas2557->dev, "firmware received, size=%zu\n", fw_entry->size);

	fw = fw_parse(tas2557, fw_entry->data, fw_entry->size);
	release_firmware(fw_entry);

	if (IS_ERR(fw)) {
		dev_err(tas2557->dev,
			"firmware '%s' parse error %ld, it may be corrupt\n",
			tas2557->fw_name, PTR_ERR(fw));
		fw = NULL;
		goto done;
	}

	mutex_lock(&tas2557->lock);

	tas2557_fw_free(tas2557->fw);
	tas2557->fw = fw;

	ret = tas2557_set_program(tas2557, 0, -1);
	if (ret < 0)
		dev_err(tas2557->dev, "initial program load failed: %d\n", ret);
	else
		dev_info(tas2557->dev,
			 "%s %s ready: fw '%s' ver=0x%x drv=0x%x (%u prog, %u cfg)\n",
			 tas2557_pg_name(tas2557->pg_id),
			 tas2557->channel ? "right" : "left", tas2557->fw_name,
			 fw->fw_version, fw->driver_version,
			 fw->num_programs, fw->num_configs);

	mutex_unlock(&tas2557->lock);

done:
	complete(&tas2557->fw_done);
}

/*
 * tas2557_fw_teardown - release firmware resources on driver teardown
 *
 * Waits for a pending tas2557_fw_ready() callback (if a request is still
 * in flight) before freeing the parsed firmware, closing the
 * use-after-free window where the firmware core could still be about to
 * invoke tas2557_fw_ready() while the device is being unbound.  See the
 * comment above its devm_add_action_or_reset() registration in
 * tas2557_i2c_probe() for why this ordering is safe.
 */
static void tas2557_fw_teardown(void *data)
{
	struct tas2557_priv *tas2557 = data;

	if (tas2557->fw_requested)
		wait_for_completion(&tas2557->fw_done);

	mutex_lock(&tas2557->lock);
	tas2557_fw_free(tas2557->fw);
	tas2557->fw = NULL;
	mutex_unlock(&tas2557->lock);
}

/* =========================================================================
 * Hardware reset
 * =========================================================================
 */
static void tas2557_hw_reset(struct tas2557_priv *tas2557)
{
	if (!tas2557->reset_gpio) {
		mutex_lock(&tas2557->dev_lock);
		tas2557->current_book = 0xff;
		tas2557->current_page = 0xff;
		mutex_unlock(&tas2557->dev_lock);
		return;
	}

	/* Assert reset (active-low: logical 1 = reset asserted) */
	gpiod_set_value_cansleep(tas2557->reset_gpio, 1);
	usleep_range(5000, 6000);
	/* Release reset */
	gpiod_set_value_cansleep(tas2557->reset_gpio, 0);
	usleep_range(10000, 11000);

	mutex_lock(&tas2557->dev_lock);
	tas2557->current_book = 0xff;
	tas2557->current_page = 0xff;
	mutex_unlock(&tas2557->dev_lock);
}

/* =========================================================================
 * IRQ handling + failsafe recovery
 * =========================================================================
 */

/*
 * tas2557_irq_arm()/tas2557_irq_disarm() - enable/disable the fault IRQ
 * from the power path.  Called with tas2557->lock NOT held: enable_irq()/
 * disable_irq() must not run while the threaded handler could be blocked
 * waiting for tas2557->lock, or a self-deadlock results (disable_irq()
 * waits for the handler thread to finish, which is waiting on the lock
 * the caller is holding).
 */
static void tas2557_irq_arm(struct tas2557_priv *tas2557)
{
	if (tas2557->irq && !tas2557->irq_enabled) {
		tas2557->irq_enabled = true;
		enable_irq(tas2557->irq);
	}
}

static void tas2557_irq_disarm(struct tas2557_priv *tas2557)
{
	if (tas2557->irq && tas2557->irq_enabled) {
		tas2557->irq_enabled = false;
		disable_irq(tas2557->irq);
	}
}

/*
 * tas2557_irq_handler - threaded fault IRQ handler
 *
 * GPIO4 is programmed as the INT1 output (see tas2557_irq_config); the
 * chip drives it active-high and holds it until FLAGS_1/FLAGS_2 are read.
 * The chip's INT output is gated off while diagnosing the fault and
 * re-enabled once the condition has been read/handled.
 */
static irqreturn_t tas2557_irq_handler(int irq, void *data)
{
	struct tas2557_priv *tas2557 = data;
	unsigned int int1 = 0, int2 = 0, pwr_flag = 0;
	int ret;

	mutex_lock(&tas2557->lock);

	if (!tas2557->fw || !tas2557->powered) {
		mutex_unlock(&tas2557->lock);
		return IRQ_HANDLED;
	}

	/* Gate the chip's INT output while diagnosing the fault */
	tas2557_dev_write(tas2557, TAS2557_GPIO4_PIN_REG, 0x00);

	ret = tas2557_dev_read(tas2557, TAS2557_FLAGS_1, &int1);
	if (ret >= 0)
		ret = tas2557_dev_read(tas2557, TAS2557_FLAGS_2, &int2);

	if (ret < 0) {
		dev_err(tas2557->dev, "IRQ: failed to read FLAGS\n");
		goto reset;
	}

	if ((int1 & 0xfc) || (int2 & 0x0c)) {
		/*
		 * Clock faults while muted are expected: the host is
		 * allowed to stop the I2S clocks around stream stop while
		 * the amplifier is still powered.  Nothing to recover from.
		 */
		if (tas2557->muted && !(int1 & 0xd8)) {
			dev_dbg(tas2557->dev,
				"IRQ: clock stop while muted (FLAGS_1=0x%02x FLAGS_2=0x%02x)\n",
				int1, int2);
			goto out;
		}
		dev_err(tas2557->dev, "IRQ: FLAGS_1=0x%02x FLAGS_2=0x%02x\n",
			int1, int2);
		goto reset;
	}

	if (!tas2557->muted) {
		ret = tas2557_dev_read(tas2557, TAS2557_POWER_UP_FLAG_REG,
				       &pwr_flag);
		if (ret < 0)
			goto reset;

		if ((pwr_flag & 0xc0) != 0xc0) {
			dev_err(tas2557->dev, "IRQ: power-up flag=0x%02x\n",
				pwr_flag);
			goto reset;
		}
	}

	/* No fault (or expected clock stop): re-enable the INT output */
out:
	tas2557_dev_write(tas2557, TAS2557_GPIO4_PIN_REG, 0x07);
	mutex_unlock(&tas2557->lock);
	return IRQ_HANDLED;

reset:
	if (tas2557_failsafe_recovery(tas2557) == 0)
		tas2557_dev_write(tas2557, TAS2557_GPIO4_PIN_REG, 0x07);

	mutex_unlock(&tas2557->lock);
	return IRQ_HANDLED;
}

/*
 * tas2557_clk_err_detect - gate the chip's clock error/halt detection
 *
 * The host is allowed to stop the I2S clocks whenever no audio is
 * playing (amplifier muted), so detection is armed only between unmute
 * and mute; otherwise a normal stream stop looks like a clock-halt
 * fault.  This matches the TI reference sequences, which disable
 * detection as the first step of the shutdown sequence.
 */
static int tas2557_clk_err_detect(struct tas2557_priv *tas2557, bool enable)
{
	return tas2557_dev_write(tas2557, TAS2557_CLK_ERR_CTRL,
				 enable ? 0x2b : 0x00);
}

/*
 * tas2557_failsafe_recovery - re-initialise the device after a fault IRQ
 *
 * Caller must hold tas2557->lock (called from tas2557_irq_handler()).
 * Capped at 5 attempts so a permanently faulty amplifier does not spin
 * the IRQ thread forever.
 */
static int tas2557_failsafe_recovery(struct tas2557_priv *tas2557)
{
	int ret;

	lockdep_assert_held(&tas2557->lock);

	tas2557->restart_count++;
	dev_info(tas2557->dev, "failsafe recovery attempt %u\n",
		 tas2557->restart_count);

	if (tas2557->restart_count > 5) {
		dev_err(tas2557->dev, "too many recovery attempts, giving up\n");
		return -EIO;
	}

	tas2557_hw_reset(tas2557);

	ret = tas2557_load_data(tas2557, tas2557_default_data);
	if (ret < 0)
		return ret;

	if (tas2557->fw) {
		ret = tas2557_set_program(tas2557, tas2557->current_program,
					  (int)tas2557->current_config);
		if (ret < 0)
			return ret;
	}

	if (tas2557->powered) {
		ret = tas2557_load_data(tas2557, tas2557_startup_data);
		if (ret < 0)
			return ret;

		ret = tas2557_load_config_post_power(tas2557);
		if (ret < 0)
			return ret;

		if (!tas2557->muted) {
			ret = tas2557_load_data(tas2557, tas2557_unmute_data);
			if (ret < 0)
				return ret;
		} else {
			/* Muted: clocks not guaranteed, keep detection off */
			ret = tas2557_clk_err_detect(tas2557, false);
			if (ret < 0)
				return ret;
		}
	}

	dev_info(tas2557->dev, "failsafe recovery succeeded\n");
	return 0;
}

/* =========================================================================
 * Power management
 *
 * Power-up sequence (firmware-driven), see tas2557_enable():
 *   1. tas2557_set_program() - hw+sw reset, load defaults+IRQ config,
 *      load firmware PLL/program/config blocks (PLL set by firmware)
 *   2. tas2557_startup_data  - enable GPIO clocks, Class-D, Boost, DSP
 *   3. Set SNS_CTRL slots, DAC gain, re-apply the ASI data-slot offset
 *   4. Load the CFG_POST_POWER firmware block
 *   5. Check POWER_UP_FLAG + FLAGS for PLL lock / clock errors
 *
 * The device stays muted after power-up; tas2557_mute_stream() applies
 * the unmute (or mute) register sequence separately, so a stream that
 * starts muted (e.g. during a volume ramp) never briefly unmutes.
 * =========================================================================
 */
static void tas2557_check_pll_lock(struct tas2557_priv *tas2557)
{
	unsigned int flag = 0, f1 = 0, f2 = 0;
	int i;

	/*
	 * The PLL needs a few ms to lock after the I2S bit clock starts
	 * flowing.  Poll POWER_UP_FLAG rather than sampling it once, so we
	 * don't emit a spurious "not locked" warning during clock ramp-up.
	 */
	for (i = 0; i < 20; i++) {
		tas2557_dev_read(tas2557, TAS2557_POWER_UP_FLAG_REG, &flag);
		if ((flag & 0xc0) == 0xc0)
			break;
		usleep_range(2000, 2500);
	}

	tas2557_dev_read(tas2557, TAS2557_FLAGS_1, &f1);
	tas2557_dev_read(tas2557, TAS2557_FLAGS_2, &f2);

	if ((flag & 0xc0) == 0xc0)
		dev_dbg(tas2557->dev,
			"PLL locked, power-up OK (FLAG=0x%02x F1=0x%02x F2=0x%02x)\n",
			flag, f1, f2);
	else
		dev_warn(tas2557->dev,
			 "PLL or clock may not have locked (FLAG=0x%02x F1=0x%02x F2=0x%02x)\n",
			 flag, f1, f2);

	if (f1 & 0x04)
		dev_warn(tas2557->dev, "clock error detected (FLAGS_1=0x%02x)\n",
			 f1);
}

/*
 * tas2557_enable - power the amplifier up or down
 *
 * Caller must hold tas2557->lock.  Does not touch the fault IRQ or the
 * mute state; see tas2557_classd_event() and tas2557_mute_stream().
 */
static int tas2557_enable(struct tas2557_priv *tas2557, bool enable)
{
	int ret = 0;

	lockdep_assert_held(&tas2557->lock);

	if (enable && !tas2557->powered) {
		if (!tas2557->fw) {
			dev_warn(tas2557->dev,
				 "firmware not loaded, cannot produce audio; install %s\n",
				 tas2557->fw_name[0] ? tas2557->fw_name
						     : TAS2557_FW_NAME);
			return -ENODEV;
		}

		/*
		 * Re-apply firmware program + config for the current sample
		 * rate.  This performs hw/sw reset and loads the firmware's
		 * PLL block; no manual PLL register writes are needed or done.
		 */
		ret = tas2557_set_program(tas2557, tas2557->current_program, -1);
		if (ret < 0) {
			dev_err(tas2557->dev,
				"firmware program load failed on enable: %d\n",
				ret);
			return ret;
		}

		/* Power up Class-D + Boost + ASI2 clocks */
		ret = tas2557_load_data(tas2557, tas2557_startup_data);
		if (ret < 0) {
			dev_err(tas2557->dev, "startup sequence failed: %d\n",
				ret);
			return ret;
		}

		/* Configure current/voltage sense output slots */
		if (tas2557->isense_enabled || tas2557->vsense_enabled) {
			unsigned int sns = 0;

			if (tas2557->isense_enabled)
				sns |= (tas2557->imon_slot & 0x7) <<
					TAS2557_ISNS_SLOT_SHIFT;
			if (tas2557->vsense_enabled)
				sns |= (tas2557->vmon_slot & 0x7) <<
					TAS2557_VSNS_SLOT_SHIFT;
			tas2557_dev_write(tas2557, TAS2557_SNS_CTRL_REG, sns);
		}

		/* Apply DAC gain */
		tas2557_dev_update_bits(tas2557, TAS2557_SPK_CTRL_REG,
					TAS2557_DAC_GAIN_MASK,
					tas2557->dac_gain << TAS2557_DAC_GAIN_SHIFT);

		/*
		 * Re-apply the cached ASI data-slot offset (BCLK cycles).
		 * The SW reset in tas2557_set_program() cleared the ASI
		 * offset registers, so program them now the clocks are up.
		 * Both ASIs are written; the board wiring selects which one
		 * actually carries audio.
		 */
		tas2557_dev_write(tas2557, TAS2557_ASI2_OFFSET1_REG,
				  tas2557->asi_offset);
		tas2557_dev_write(tas2557, TAS2557_ASI1_OFFSET1_REG,
				  tas2557->asi_offset);

		/* DSP post-power coefficients need active clocks to latch */
		ret = tas2557_load_config_post_power(tas2557);
		if (ret < 0) {
			dev_err(tas2557->dev,
				"post-power config load failed: %d\n", ret);
			return ret;
		}

		/*
		 * Apply the recorded mute state.  On DPCM systems the
		 * mute_stream(0) callback for a back-end DAI runs at
		 * back-end prepare time, BEFORE the DAPM widgets power up,
		 * so the unmute may already have been recorded while the
		 * device was still off: honour it now.  When still muted,
		 * also disarm the clock error detection that
		 * tas2557_startup_data enabled, since the host may not be
		 * clocking the bus yet; tas2557_mute_stream() re-arms it.
		 */
		if (tas2557->muted) {
			ret = tas2557_clk_err_detect(tas2557, false);
		} else {
			ret = tas2557_load_data(tas2557, tas2557_unmute_data);
			if (ret >= 0)
				ret = tas2557_clk_err_detect(tas2557, true);
		}
		if (ret < 0)
			return ret;

		/* Verify PLL lock and clock health */
		tas2557_check_pll_lock(tas2557);

		tas2557->powered = true;
		tas2557->restart_count = 0;

		dev_dbg(tas2557->dev, "amplifier powered on (prog=%u cfg=%u)\n",
			tas2557->current_program, tas2557->current_config);

	} else if (!enable && tas2557->powered) {
		ret = tas2557_load_data(tas2557, tas2557_shutdown_data);
		if (ret < 0)
			dev_err(tas2557->dev, "shutdown failed: %d\n", ret);

		tas2557->powered = false;
		tas2557->muted   = true;

		dev_dbg(tas2557->dev, "amplifier powered off\n");
	}

	return ret;
}

/* =========================================================================
 * ASoC DAI operations
 * =========================================================================
 */
static int tas2557_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);
	unsigned int rate = params_rate(params);
	unsigned int width, offset;
	int ret;

	mutex_lock(&tas2557->lock);

	dev_dbg(tas2557->dev, "hw_params: rate=%u width=%d\n",
		rate, params_physical_width(params));

	/* Store the rate; tas2557_enable() uses it to pick the config */
	tas2557->sample_rate = rate;

	/*
	 * The ASI word length is fixed by tas2557_startup_data to match
	 * what the DSP firmware expects; it does not depend on the stream
	 * format, so no WORDLENGTH bits are written here.  Only the
	 * channel data-slot offset (in BCLK cycles) depends on the
	 * stream: channel 0 sits at offset 1 (the validated hardware
	 * configuration); later channels follow, width*2 apart.
	 */
	width = tas2557->tdm_slot_width > 0 ? tas2557->tdm_slot_width :
					      params_physical_width(params);
	offset = (tas2557->channel == 0) ? 1 : 1 + width * 2;
	tas2557->asi_offset = offset;

	/* Playback runs over ASI2; ASI1 is programmed too (per TI driver) */
	ret = tas2557_dev_write(tas2557, TAS2557_ASI2_OFFSET1_REG, offset);
	if (ret < 0)
		dev_warn(tas2557->dev, "ASI2 offset write failed: %d\n", ret);

	ret = tas2557_dev_write(tas2557, TAS2557_ASI1_OFFSET1_REG, offset);
	if (ret < 0)
		dev_warn(tas2557->dev, "ASI1 offset write failed: %d\n", ret);

	dev_dbg(tas2557->dev, "ASI: %s channel, offset=%u BCLK cycles\n",
		tas2557->channel ? "right" : "left", offset);

	/*
	 * PLL (MAIN_CLKIN, PLL_CLKIN, PLL J/P/D/N) is not configured here.
	 * It is set entirely by the firmware PLL block loaded via
	 * tas2557_set_program() during tas2557_enable().
	 */

	mutex_unlock(&tas2557->lock);

	return 0;
}

static int tas2557_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);
	unsigned int asi_fmt = 0;
	int ret;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		asi_fmt = TAS2557_FORMAT_I2S;
		break;
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		asi_fmt = TAS2557_FORMAT_DSP;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		asi_fmt = TAS2557_FORMAT_RIGHT_J;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		asi_fmt = TAS2557_FORMAT_LEFT_J;
		break;
	default:
		dev_err(tas2557->dev, "unsupported DAI format 0x%x\n", fmt);
		return -EINVAL;
	}

	mutex_lock(&tas2557->lock);
	ret = tas2557_dev_update_bits(tas2557, TAS2557_ASI1_DAC_FORMAT_REG,
				      TAS2557_FORMAT_MASK, asi_fmt);
	if (ret >= 0)
		ret = tas2557_dev_update_bits(tas2557,
					      TAS2557_ASI2_DAC_FORMAT_REG,
					      TAS2557_FORMAT_MASK, asi_fmt);
	mutex_unlock(&tas2557->lock);

	return ret;
}

static int tas2557_set_tdm_slot(struct snd_soc_dai *dai,
				unsigned int tx_mask, unsigned int rx_mask,
				int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);
	int rx_slot = -1, tx_slot = -1;
	int ret = 0;

	if (rx_mask) {
		rx_slot = ffs(rx_mask) - 1;
		if (rx_slot < 0 || rx_slot > 7)
			return -EINVAL;
	}
	if (tx_mask) {
		tx_slot = ffs(tx_mask) - 1;
		if (tx_slot < 0 || tx_slot > 7)
			return -EINVAL;
	}

	switch (slot_width) {
	case 0:
	case 16:
	case 24:
	case 32:
		break;
	default:
		dev_err(tas2557->dev, "unsupported TDM slot width %d\n",
			slot_width);
		return -EINVAL;
	}

	mutex_lock(&tas2557->lock);

	tas2557->tdm_slot_width = slot_width;

	if (rx_slot >= 0) {
		/* RX (playback) data-slot offset in BCLK cycles */
		tas2557->asi_offset = rx_slot * slot_width;
		ret = tas2557_dev_write(tas2557, TAS2557_ASI1_OFFSET1_REG,
					tas2557->asi_offset);
		if (ret >= 0)
			ret = tas2557_dev_write(tas2557,
						TAS2557_ASI2_OFFSET1_REG,
						tas2557->asi_offset);
	}

	if (ret >= 0 && tx_slot >= 0) {
		unsigned int tx_offset = tx_slot * slot_width;

		ret = tas2557_dev_write(tas2557, TAS2557_ASI1_OFFSET2_REG,
					tx_offset);
		if (ret >= 0)
			ret = tas2557_dev_write(tas2557,
						TAS2557_ASI2_OFFSET2_REG,
						tx_offset);
	}

	if (ret >= 0)
		dev_dbg(tas2557->dev, "TDM: rx=%d tx=%d width=%d\n",
			rx_slot, tx_slot, slot_width);

	mutex_unlock(&tas2557->lock);
	return ret;
}

static int tas2557_mute_stream(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	mutex_lock(&tas2557->lock);

	if (!tas2557->powered) {
		tas2557->muted = mute;
		mutex_unlock(&tas2557->lock);
		return 0;
	}

	if (mute) {
		/* Detection off first: the host is about to stop the clocks */
		ret = tas2557_clk_err_detect(tas2557, false);
		if (ret >= 0)
			ret = tas2557_dev_write(tas2557,
						TAS2557_SOFT_MUTE_REG, 0x01);
		if (ret >= 0) {
			usleep_range(10000, 11000);
			ret = tas2557_dev_write(tas2557,
						TAS2557_MUTE_REG, 0x03);
		}
	} else {
		/* Same register values as tas2557_unmute_data, same order */
		ret = tas2557_load_data(tas2557, tas2557_unmute_data);
		/* Clocks are running now: arm clock error detection */
		if (ret >= 0)
			ret = tas2557_clk_err_detect(tas2557, true);
	}

	tas2557->muted = mute;

	mutex_unlock(&tas2557->lock);
	return ret;
}

static const struct snd_soc_dai_ops tas2557_dai_ops = {
	.hw_params     = tas2557_hw_params,
	.set_fmt       = tas2557_set_dai_fmt,
	.set_tdm_slot  = tas2557_set_tdm_slot,
	.mute_stream   = tas2557_mute_stream,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver tas2557_dai = {
	.name = "tas2557-amplifier",
	.playback = {
		.stream_name  = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates   = SNDRV_PCM_RATE_8000_192000,
		.formats = TAS2557_FORMATS,
	},
	.capture = {
		.stream_name  = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates   = SNDRV_PCM_RATE_8000_192000,
		.formats = TAS2557_FORMATS,
	},
	.ops = &tas2557_dai_ops,
};

/* =========================================================================
 * ALSA mixer controls
 * =========================================================================
 */
static DECLARE_TLV_DB_SCALE(tas2557_dac_tlv, -2800, 200, 0);

static int tas2557_volume_get(struct snd_kcontrol *kc,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct tas2557_priv *tas2557 =
		snd_soc_component_get_drvdata(snd_kcontrol_chip(kc));

	ucontrol->value.integer.value[0] = tas2557->dac_gain;
	return 0;
}

static int tas2557_volume_put(struct snd_kcontrol *kc,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct tas2557_priv *tas2557 =
		snd_soc_component_get_drvdata(snd_kcontrol_chip(kc));
	unsigned int gain = ucontrol->value.integer.value[0];
	int ret = 0;

	if (gain > TAS2557_DAC_GAIN_MAX)
		return -EINVAL;

	mutex_lock(&tas2557->lock);

	if (gain == tas2557->dac_gain) {
		mutex_unlock(&tas2557->lock);
		return 0;
	}

	tas2557->dac_gain = gain;

	if (tas2557->powered)
		ret = tas2557_dev_update_bits(tas2557, TAS2557_SPK_CTRL_REG,
					      TAS2557_DAC_GAIN_MASK,
					      gain << TAS2557_DAC_GAIN_SHIFT);

	mutex_unlock(&tas2557->lock);

	if (ret < 0)
		return ret;

	return 1;
}

static int tas2557_isense_get(struct snd_kcontrol *kc,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct tas2557_priv *tas2557 =
		snd_soc_component_get_drvdata(snd_kcontrol_chip(kc));

	ucontrol->value.integer.value[0] = tas2557->isense_enabled;
	return 0;
}

static int tas2557_isense_put(struct snd_kcontrol *kc,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct tas2557_priv *tas2557 =
		snd_soc_component_get_drvdata(snd_kcontrol_chip(kc));
	bool en = ucontrol->value.integer.value[0];
	int ret = 0;

	mutex_lock(&tas2557->lock);

	if (en == tas2557->isense_enabled) {
		mutex_unlock(&tas2557->lock);
		return 0;
	}

	tas2557->isense_enabled = en;

	if (tas2557->powered)
		ret = tas2557_dev_update_bits(tas2557, TAS2557_POWER_CTRL2_REG,
					      TAS2557_ISENSE_ENABLE,
					      en ? TAS2557_ISENSE_ENABLE : 0);

	mutex_unlock(&tas2557->lock);

	if (ret < 0)
		return ret;

	return 1;
}

static int tas2557_vsense_get(struct snd_kcontrol *kc,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct tas2557_priv *tas2557 =
		snd_soc_component_get_drvdata(snd_kcontrol_chip(kc));

	ucontrol->value.integer.value[0] = tas2557->vsense_enabled;
	return 0;
}

static int tas2557_vsense_put(struct snd_kcontrol *kc,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct tas2557_priv *tas2557 =
		snd_soc_component_get_drvdata(snd_kcontrol_chip(kc));
	bool en = ucontrol->value.integer.value[0];
	int ret = 0;

	mutex_lock(&tas2557->lock);

	if (en == tas2557->vsense_enabled) {
		mutex_unlock(&tas2557->lock);
		return 0;
	}

	tas2557->vsense_enabled = en;

	if (tas2557->powered)
		ret = tas2557_dev_update_bits(tas2557, TAS2557_POWER_CTRL2_REG,
					      TAS2557_VSENSE_ENABLE,
					      en ? TAS2557_VSENSE_ENABLE : 0);

	mutex_unlock(&tas2557->lock);

	if (ret < 0)
		return ret;

	return 1;
}

static const struct snd_kcontrol_new tas2557_controls[] = {
	SOC_SINGLE_EXT_TLV("Speaker Playback Volume", SND_SOC_NOPM, 0,
			   TAS2557_DAC_GAIN_MAX, 0,
			   tas2557_volume_get, tas2557_volume_put,
			   tas2557_dac_tlv),
	SOC_SINGLE_BOOL_EXT("ISENSE Switch", 0,
			    tas2557_isense_get, tas2557_isense_put),
	SOC_SINGLE_BOOL_EXT("VSENSE Switch", 0,
			    tas2557_vsense_get, tas2557_vsense_put),
};

/* =========================================================================
 * DAPM topology
 *
 * Playback path: ASI1/ASI2 (AIF_IN, stream "Playback") -> DAC -> ClassD -> OUT
 * Capture path:  ClassD -> ISENSE/VSENSE -> SENSE (AIF_OUT, "Capture")
 *
 * Both ASI1 and ASI2 are configured identically (see tas2557_startup_data);
 * the board wiring determines which one actually carries audio.
 * =========================================================================
 */
static int tas2557_classd_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		mutex_lock(&tas2557->lock);
		ret = tas2557_enable(tas2557, true);
		mutex_unlock(&tas2557->lock);
		if (ret < 0)
			return ret;
		tas2557_irq_arm(tas2557);
		return 0;
	case SND_SOC_DAPM_PRE_PMD:
		tas2557_irq_disarm(tas2557);
		mutex_lock(&tas2557->lock);
		ret = tas2557_enable(tas2557, false);
		mutex_unlock(&tas2557->lock);
		return ret;
	default:
		return 0;
	}
}

static const struct snd_soc_dapm_widget tas2557_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("ASI1", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("ASI2", "Playback", 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_OUT_DRV_E("ClassD", SND_SOC_NOPM, 0, 0, NULL, 0,
			       tas2557_classd_event,
			       SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	/*
	 * ISENSE and VSENSE are controlled via tas2557_enable() and the
	 * ISENSE/VSENSE mixer controls, not via DAPM register writes.
	 * Use SND_SOC_NOPM to avoid direct regmap access to book-paged regs.
	 */
	SND_SOC_DAPM_ADC("ISENSE", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("VSENSE", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_OUTPUT("OUT"),
	SND_SOC_DAPM_AIF_OUT("SENSE", "Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route tas2557_dapm_routes[] = {
	{ "ASI1",   NULL, "Playback" },
	{ "ASI2",   NULL, "Playback" },
	{ "DAC",    NULL, "ASI1" },
	{ "DAC",    NULL, "ASI2" },
	{ "ClassD", NULL, "DAC" },
	{ "OUT",    NULL, "ClassD" },

	{ "ISENSE", NULL, "ClassD" },
	{ "VSENSE", NULL, "ClassD" },
	{ "SENSE",  NULL, "ISENSE" },
	{ "SENSE",  NULL, "VSENSE" },
	{ "Capture", NULL, "SENSE" },
};

/* =========================================================================
 * ASoC component probe / suspend / resume
 * =========================================================================
 */
static int tas2557_codec_probe(struct snd_soc_component *component)
{
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);
	unsigned int pg_id;
	const char *default_fw;
	int ret;

	tas2557_hw_reset(tas2557);

	ret = tas2557_dev_write(tas2557, TAS2557_SW_RESET_REG, 0x01);
	if (ret < 0) {
		dev_err(tas2557->dev, "software reset failed: %d\n", ret);
		return ret;
	}
	usleep_range(1000, 2000);

	ret = tas2557_dev_read(tas2557, TAS2557_REV_PGID_REG, &pg_id);
	if (ret < 0) {
		dev_err(tas2557->dev, "failed to read REV_PGID: %d\n", ret);
		return ret;
	}

	switch (pg_id) {
	case TAS2557_PG_VERSION_1P0:
		dev_dbg(tas2557->dev, "silicon: PG1.0 (0x%02x)\n", pg_id);
		default_fw = TAS2557_PG1P0_FW_NAME;
		break;
	case TAS2557_PG_VERSION_2P0:
		dev_dbg(tas2557->dev, "silicon: PG2.0 (0x%02x)\n", pg_id);
		default_fw = TAS2557_FW_NAME;
		break;
	case TAS2557_PG_VERSION_2P1:
		dev_dbg(tas2557->dev, "silicon: PG2.1 (0x%02x)\n", pg_id);
		default_fw = TAS2557_FW_NAME;
		break;
	default:
		dev_warn(tas2557->dev,
			 "unknown silicon version 0x%02x, assuming PG2.x fw\n",
			 pg_id);
		default_fw = TAS2557_FW_NAME;
		break;
	}

	mutex_lock(&tas2557->lock);
	tas2557->pg_id = pg_id;
	/*
	 * A "firmware-name" DT property (read at i2c probe) overrides the
	 * PG-based default; dual-amp stereo boards load a different DSP
	 * blob (e.g. tas2557s_uCDSP.bin).
	 */
	if (!tas2557->fw_name[0])
		strscpy(tas2557->fw_name, default_fw, sizeof(tas2557->fw_name));
	mutex_unlock(&tas2557->lock);

	ret = tas2557_load_data(tas2557, tas2557_default_data);
	if (ret < 0) {
		dev_err(tas2557->dev, "default data load failed: %d\n", ret);
		return ret;
	}

	ret = tas2557_load_data(tas2557, tas2557_irq_config);
	if (ret < 0)
		dev_warn(tas2557->dev, "IRQ config failed: %d\n", ret);

	/*
	 * Component rebind (e.g. after an unbind/bind cycle): firmware is
	 * already parsed and stored, so do not request it again.
	 */
	mutex_lock(&tas2557->lock);
	if (!tas2557->fw_requested) {
		reinit_completion(&tas2557->fw_done);
		ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_UEVENT,
					      tas2557->fw_name, tas2557->dev,
					      GFP_KERNEL, tas2557,
					      tas2557_fw_ready);
		if (ret) {
			mutex_unlock(&tas2557->lock);
			return dev_err_probe(tas2557->dev, ret,
					     "failed to request fw '%s'\n",
					     tas2557->fw_name);
		}
		tas2557->fw_requested = true;
	}
	mutex_unlock(&tas2557->lock);

	dev_dbg(tas2557->dev, "codec probed, requesting firmware '%s'\n",
		tas2557->fw_name);
	return 0;
}

static int tas2557_suspend(struct snd_soc_component *component)
{
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);

	tas2557_irq_disarm(tas2557);

	mutex_lock(&tas2557->lock);
	if (tas2557->powered)
		tas2557_enable(tas2557, false);
	mutex_unlock(&tas2557->lock);

	return 0;
}

static int tas2557_resume(struct snd_soc_component *component)
{
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);
	int ret;

	mutex_lock(&tas2557->lock);

	tas2557_hw_reset(tas2557);

	ret = tas2557_load_data(tas2557, tas2557_default_data);
	if (ret < 0)
		dev_err(tas2557->dev, "defaults reload on resume failed: %d\n",
			ret);
	else if (tas2557_load_data(tas2557, tas2557_irq_config) < 0)
		dev_warn(tas2557->dev, "IRQ config reload on resume failed\n");

	mutex_unlock(&tas2557->lock);

	return ret;
}

static const struct snd_soc_component_driver soc_component_tas2557 = {
	.probe           = tas2557_codec_probe,
	.suspend         = tas2557_suspend,
	.resume          = tas2557_resume,
	.controls        = tas2557_controls,
	.num_controls    = ARRAY_SIZE(tas2557_controls),
	.dapm_widgets    = tas2557_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tas2557_dapm_widgets),
	.dapm_routes     = tas2557_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(tas2557_dapm_routes),
	.idle_bias_on    = 1,
	.use_pmdown_time = 1,
	.endianness      = 1,
};

/* =========================================================================
 * I2C driver
 * =========================================================================
 */
static int tas2557_i2c_probe(struct i2c_client *client)
{
	static const char * const tas2557_supplies[] = {
		"vbat", "iovdd", "avdd", "dvdd"
	};
	struct device *dev = &client->dev;
	struct tas2557_priv *tas2557;
	const char *fw_name;
	int ret;

	tas2557 = devm_kzalloc(dev, sizeof(*tas2557), GFP_KERNEL);
	if (!tas2557)
		return -ENOMEM;

	tas2557->dev = dev;
	i2c_set_clientdata(client, tas2557);

	mutex_init(&tas2557->dev_lock);
	mutex_init(&tas2557->lock);
	init_completion(&tas2557->fw_done);
	tas2557->fw_requested = false;

	/* Default gain (0 dB) */
	tas2557->dac_gain = TAS2557_DAC_GAIN_MAX;

	/* The device comes out of reset muted */
	tas2557->muted = true;

	/* DT properties with defaults per the pinned contract */
	if (of_property_read_u32(dev->of_node, "ti,channel", &tas2557->channel))
		tas2557->channel = 0;	/* left */
	else if (tas2557->channel > 1)
		return dev_err_probe(dev, -EINVAL,
				     "ti,channel must be 0 or 1\n");

	if (of_property_read_u32(dev->of_node, "ti,imon-slot-no",
				 &tas2557->imon_slot))
		tas2557->imon_slot = 0;
	else if (tas2557->imon_slot > 7)
		return dev_err_probe(dev, -EINVAL,
				     "ti,imon-slot-no must be 0-7\n");

	if (of_property_read_u32(dev->of_node, "ti,vmon-slot-no",
				 &tas2557->vmon_slot))
		tas2557->vmon_slot = 2;
	else if (tas2557->vmon_slot > 7)
		return dev_err_probe(dev, -EINVAL,
				     "ti,vmon-slot-no must be 0-7\n");

	/* Optional explicit firmware name; overrides the PG-based default */
	if (!of_property_read_string(dev->of_node, "firmware-name", &fw_name))
		strscpy(tas2557->fw_name, fw_name, sizeof(tas2557->fw_name));

	/*
	 * Seed the cached ASI data-slot offset before any hw_params() or
	 * set_tdm_slot() call arrives; 32 is the fixed ASI word length
	 * used by tas2557_startup_data.
	 */
	tas2557->asi_offset = (tas2557->channel == 0) ? 1 : 1 + 32 * 2;

	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(tas2557_supplies),
					     tas2557_supplies);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to get/enable supplies\n");
	usleep_range(5000, 10000);

	/*
	 * Reset GPIO is modeled active-low in DT: the logical value 0
	 * (GPIOD_OUT_LOW) deasserts reset, releasing the device.
	 */
	tas2557->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						      GPIOD_OUT_LOW);
	if (IS_ERR(tas2557->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(tas2557->reset_gpio),
				     "failed to get reset GPIO\n");

	tas2557->regmap = devm_regmap_init_i2c(client, &tas2557_regmap_config);
	if (IS_ERR(tas2557->regmap))
		return dev_err_probe(dev, PTR_ERR(tas2557->regmap),
				     "regmap init failed\n");

	/* Mark book/page as unknown so the first access triggers a switch */
	tas2557->current_book = 0xff;
	tas2557->current_page = 0xff;

	/*
	 * Registered here, right after regmap init: devm actions run in
	 * LIFO order, so tas2557_fw_teardown() runs AFTER the component
	 * registered below is unregistered and the IRQ requested below is
	 * freed (so neither tas2557_fw_ready() nor tas2557_irq_handler()
	 * can still be running), but BEFORE regmap, the reset GPIO and
	 * the regulators are released.
	 */
	ret = devm_add_action_or_reset(dev, tas2557_fw_teardown, tas2557);
	if (ret)
		return ret;

	if (client->irq) {
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						tas2557_irq_handler,
						IRQF_ONESHOT | IRQF_NO_AUTOEN,
						"tas2557", tas2557);
		if (ret) {
			dev_warn(dev, "IRQ %d request failed: %d\n",
				 client->irq, ret);
			tas2557->irq = 0;
		} else {
			tas2557->irq = client->irq;
			dev_dbg(dev, "IRQ %d registered\n", client->irq);
		}
	}

	ret = devm_snd_soc_register_component(dev, &soc_component_tas2557,
					      &tas2557_dai, 1);
	if (ret)
		return dev_err_probe(dev, ret,
				     "component registration failed\n");

	dev_dbg(dev, "TAS2557 probed (I2C addr 0x%02x, %s channel)\n",
		client->addr, tas2557->channel ? "right" : "left");
	return 0;
}

static const struct i2c_device_id tas2557_i2c_id[] = {
	{ "tas2557" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas2557_i2c_id);

static const struct of_device_id tas2557_of_match[] = {
	{ .compatible = "ti,tas2557" },
	{ }
};
MODULE_DEVICE_TABLE(of, tas2557_of_match);

static struct i2c_driver tas2557_i2c_driver = {
	.driver = {
		.name           = "tas2557",
		.of_match_table = tas2557_of_match,
	},
	.probe    = tas2557_i2c_probe,
	.id_table = tas2557_i2c_id,
};
module_i2c_driver(tas2557_i2c_driver);

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_AUTHOR("Gianluca Boiano <morf3089@gmail.com>");
MODULE_DESCRIPTION("ASoC TAS2557 Smart Amplifier Driver");
MODULE_FIRMWARE(TAS2557_FW_NAME);
MODULE_FIRMWARE(TAS2557_PG1P0_FW_NAME);
MODULE_LICENSE("GPL");
