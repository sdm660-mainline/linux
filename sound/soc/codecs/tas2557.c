// SPDX-License-Identifier: GPL-2.0
//
// ALSA SoC Texas Instruments TAS2557 Smart Amplifier
//
// Copyright (C) 2016 Texas Instruments Inc.
// Copyright (C) 2024
//
// The TAS2557 requires DSP firmware (tas2557_uCDSP.bin for PG2.x,
// tas2557_pg1p0_uCDSP.bin for PG1.0) to route audio through its internal
// DSP and DAC.  Without loaded firmware the device produces NO audio.
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
#include <linux/hrtimer.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/unaligned.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "tas2557.h"

/* Forward declarations */
static void tas2557_hw_reset(struct tas2557_priv *tas2557);
static int tas2557_failsafe_recovery(struct tas2557_priv *tas2557);

/* Special delay markers embedded in register sequence tables */
#define TAS2557_UDELAY	0xFFFFFFFE
#define TAS2557_MDELAY	0xFFFFFFFD

/*
 * Default device initialization sequence — identical to TI Android driver
 * (p_tas2557_default_data).  Written once after every hardware/software reset.
 */
static const unsigned int tas2557_default_data[] = {
	TAS2557_SAR_ADC2_REG,  0x05,	/* enable SAR ADC */
	TAS2557_CLK_ERR_CTRL2, 0x21,	/* clk1: hysteresis 0.34 ms */
	TAS2557_CLK_ERR_CTRL3, 0x21,	/* clk2: rampDown 15 dB/us */
	TAS2557_SAFE_GUARD_REG, TAS2557_SAFE_GUARD_PATTERN,
	0xFFFFFFFF, 0xFFFFFFFF
};

/*
 * Interrupt output configuration — identical to TI reference
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
 * Startup sequence — power up Class-D, Boost, DSP/PLL clocks.
 * ASI2 GPIO wiring (GPIO5-8) is included because the hardware routes
 * PRIMARY_MI2S through the ASI2 interface.
 * PLL configuration is NOT done here — it comes from the firmware
 * PLL block loaded by tas2557_set_program() before this sequence runs.
 */
static const unsigned int tas2557_startup_data[] = {
	TAS2557_GPI_PIN_REG,              0x15,	/* enable DIN, MCLK, CCI */
	TAS2557_GPIO1_PIN_REG,            0x01,	/* enable BCLK (ASI1) */
	TAS2557_GPIO2_PIN_REG,            0x01,	/* enable WCLK (ASI1) */
	/* ASI2 interface GPIO wiring */
	TAS2557_GPIO6_PIN_REG,            0x01,	/* GPIO6 = ASI2 WCLK input */
	TAS2557_GPIO8_PIN_REG,            0x02,	/* GPIO8 = ASI2 DIN */
	TAS2557_ASI2_DAC_FORMAT_REG,      0x18,	/* ASI2: 32-bit I2S */
	TAS2557_ASI2_BDIV_CLK_SEL_REG,   0x01,
	TAS2557_ASI2_BDIV_CLK_RATIO_REG, 0x01,
	TAS2557_ASI2_BDIV_CLK_RATIO_REG, 0x81,	/* power up BDIV */
	TAS2557_ASI2_WDIV_CLK_RATIO_REG, 0x40,
	TAS2557_ASI2_WDIV_CLK_RATIO_REG, 0xc0,	/* power up WDIV */
	TAS2557_GPIO5_PIN_REG,            0x01,	/* GPIO5 = ASI2 BCLK input */
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

/* -------------------------------------------------------------------------
 * regmap configuration
 * -------------------------------------------------------------------------
 * All registers are volatile because the TAS2557 firmware can update them
 * autonomously.  The book/page switch is handled manually; regmap sees only
 * the raw 7-bit page register address.
 */
static bool tas2557_volatile(struct device *dev, unsigned int reg)
{
	return true;
}

static bool tas2557_writeable(struct device *dev, unsigned int reg)
{
	return true;
}

static const struct regmap_config tas2557_regmap_config = {
	.reg_bits    = 8,
	.val_bits    = 8,
	.writeable_reg = tas2557_writeable,
	.volatile_reg  = tas2557_volatile,
	.cache_type  = REGCACHE_NONE,
	.max_register = 0x7f,
};

/* -------------------------------------------------------------------------
 * Book / page / register access
 * -------------------------------------------------------------------------
 * The TAS2557 address space uses a book + page prefix.  The regmap is
 * configured for 8-bit register width; book/page selection is done by
 * writing to TAS2557_BOOK_REG (0x7f) and TAS2557_PAGE_REG (0x00).
 *
 * All helpers take a composite register address encoded as
 *   TAS2557_REG(book, page, reg)  →  book*256*128 + page*128 + reg
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
			goto err;
		}
		tas2557->current_page = 0;

		ret = regmap_write(tas2557->regmap, TAS2557_BOOK_REG, book);
		if (ret < 0) {
			dev_err(tas2557->dev, "book switch to %u failed: %d\n",
				book, ret);
			goto err;
		}
		tas2557->current_book = book;
	}

	if (tas2557->current_page != page) {
		ret = regmap_write(tas2557->regmap, TAS2557_PAGE_REG, page);
		if (ret < 0) {
			dev_err(tas2557->dev, "page switch to %u failed: %d\n",
				page, ret);
			goto err;
		}
		tas2557->current_page = page;
	}

	return 0;

err:
	tas2557->err_code |= ERROR_DEVA_I2C_COMM;
	return ret;
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
	if (ret < 0) {
		dev_err(tas2557->dev, "read reg 0x%06x failed: %d\n", reg, ret);
		tas2557->err_code |= ERROR_DEVA_I2C_COMM;
	} else {
		tas2557->err_code &= ~ERROR_DEVA_I2C_COMM;
	}
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
	if (ret < 0) {
		dev_err(tas2557->dev, "write reg 0x%06x = 0x%02x failed: %d\n",
			reg, value, ret);
		tas2557->err_code |= ERROR_DEVA_I2C_COMM;
	} else {
		tas2557->err_code &= ~ERROR_DEVA_I2C_COMM;
	}
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
	if (ret < 0) {
		dev_err(tas2557->dev, "update_bits reg 0x%06x failed: %d\n",
			reg, ret);
		tas2557->err_code |= ERROR_DEVA_I2C_COMM;
	} else {
		tas2557->err_code &= ~ERROR_DEVA_I2C_COMM;
	}
out:
	mutex_unlock(&tas2557->dev_lock);
	return ret;
}

static int tas2557_dev_bulk_read(struct tas2557_priv *tas2557,
				 unsigned int reg, u8 *data, size_t len)
{
	int ret;

	mutex_lock(&tas2557->dev_lock);
	ret = tas2557_change_book_page(tas2557, TAS2557_BOOK_ID(reg),
				       TAS2557_PAGE_ID(reg));
	if (ret < 0)
		goto out;

	ret = regmap_bulk_read(tas2557->regmap,
			       TAS2557_PAGE_REG_ADDR(reg), data, len);
	if (ret < 0) {
		dev_err(tas2557->dev, "bulk_read reg 0x%06x failed: %d\n",
			reg, ret);
		tas2557->err_code |= ERROR_DEVA_I2C_COMM;
	} else {
		tas2557->err_code &= ~ERROR_DEVA_I2C_COMM;
	}
out:
	mutex_unlock(&tas2557->dev_lock);
	return ret;
}

static int tas2557_dev_bulk_write(struct tas2557_priv *tas2557,
				  unsigned int reg, const u8 *data, size_t len)
{
	int ret;

	mutex_lock(&tas2557->dev_lock);
	ret = tas2557_change_book_page(tas2557, TAS2557_BOOK_ID(reg),
				       TAS2557_PAGE_ID(reg));
	if (ret < 0)
		goto out;

	ret = regmap_bulk_write(tas2557->regmap,
				TAS2557_PAGE_REG_ADDR(reg), data, len);
	if (ret < 0) {
		dev_err(tas2557->dev, "bulk_write reg 0x%06x failed: %d\n",
			reg, ret);
		tas2557->err_code |= ERROR_DEVA_I2C_COMM;
	} else {
		tas2557->err_code &= ~ERROR_DEVA_I2C_COMM;
	}
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
 * The on-disk layout follows the TI reference firmware format.
 * =========================================================================
 */

static void tas2557_fw_free(struct tas2557_firmware *fw)
{
	unsigned int i, j;

	if (!fw)
		return;

	if (fw->plls) {
		for (i = 0; i < fw->num_plls; i++) {
			kfree(fw->plls[i].description);
			kfree(fw->plls[i].block.data);
		}
		kfree(fw->plls);
	}

	if (fw->programs) {
		for (i = 0; i < fw->num_programs; i++) {
			kfree(fw->programs[i].description);
			if (fw->programs[i].data.blocks) {
				for (j = 0; j < fw->programs[i].data.num_blocks; j++)
					kfree(fw->programs[i].data.blocks[j].data);
				kfree(fw->programs[i].data.blocks);
			}
			kfree(fw->programs[i].data.description);
		}
		kfree(fw->programs);
	}

	if (fw->configs) {
		for (i = 0; i < fw->num_configs; i++) {
			kfree(fw->configs[i].description);
			if (fw->configs[i].data.blocks) {
				for (j = 0; j < fw->configs[i].data.num_blocks; j++)
					kfree(fw->configs[i].data.blocks[j].data);
				kfree(fw->configs[i].data.blocks);
			}
			kfree(fw->configs[i].data.description);
		}
		kfree(fw->configs);
	}

	kfree(fw->description);
	kfree(fw);
}

static int fw_parse_header(struct tas2557_priv *tas2557,
			   struct tas2557_firmware *fw,
			   const u8 *data, size_t size)
{
	const u8 *start = data;
	size_t desc_len;

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

	fw->size           = get_unaligned_be32(data); data += 4;
	fw->checksum       = get_unaligned_be32(data); data += 4;
	fw->ppc_version    = get_unaligned_be32(data); data += 4;
	fw->fw_version     = get_unaligned_be32(data); data += 4;
	fw->driver_version = get_unaligned_be32(data); data += 4;
	fw->timestamp      = get_unaligned_be32(data); data += 4;

	memcpy(fw->ddc_name, data, 64);
	data += 64;

	desc_len = strnlen(data, size - (size_t)(data - start));
	if (desc_len >= size - (size_t)(data - start)) {
		dev_err(tas2557->dev, "firmware header: description not NUL-terminated\n");
		return -EINVAL;
	}
	if (desc_len > 0) {
		fw->description = kmemdup(data, desc_len + 1, GFP_KERNEL);
		if (!fw->description)
			return -ENOMEM;
	}
	data += desc_len + 1;

	if ((data - start) + 8 > (ptrdiff_t)size) {
		dev_err(tas2557->dev, "firmware header truncated\n");
		return -EINVAL;
	}

	fw->device_family = get_unaligned_be32(data); data += 4;
	if (fw->device_family != 0) {
		dev_err(tas2557->dev, "unsupported device family: %u\n",
			fw->device_family);
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
		fw->ddc_name, fw->fw_version, fw->ppc_version,
		fw->driver_version);

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
		if (remaining < 4)
			return -EINVAL;
		block->pchksum_present = data[0];
		block->pchksum         = data[1];
		block->ychksum_present = data[2];
		block->ychksum         = data[3];
		data += 4;
		remaining -= 4;
	}

	if (remaining < 4)
		return -EINVAL;

	block->num_commands = get_unaligned_be32(data);
	data += 4;
	remaining -= 4;

	data_len = block->num_commands * 4;
	if (data_len > remaining) {
		dev_err(tas2557->dev,
			"block type 0x%x truncated: need %zu have %zu\n",
			block->type, data_len, remaining);
		return -EINVAL;
	}

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
	data += 64;
	remaining -= 64;

	desc_len = strnlen(data, remaining);
	if (desc_len >= remaining)
		return -EINVAL;
	if (desc_len > 0) {
		img_data->description = kmemdup(data, desc_len + 1, GFP_KERNEL);
		if (!img_data->description)
			return -ENOMEM;
	}
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
		data += 64;
		remaining -= 64;

		desc_len = strnlen(data, remaining);
		if (desc_len >= remaining)
			return -EINVAL;
		if (desc_len > 0) {
			fw->plls[i].description =
				kmemdup(data, desc_len + 1, GFP_KERNEL);
			if (!fw->plls[i].description)
				return -ENOMEM;
		}
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
		data += 64;
		remaining -= 64;

		desc_len = strnlen(data, remaining);
		if (desc_len >= remaining)
			return -EINVAL;
		if (desc_len > 0) {
			fw->programs[i].description =
				kmemdup(data, desc_len + 1, GFP_KERNEL);
			if (!fw->programs[i].description)
				return -ENOMEM;
		}
		data += desc_len + 1;
		remaining -= desc_len + 1;

		if (remaining < 3)
			return -EINVAL;

		fw->programs[i].app_mode = data[0]; data++; remaining--;
		fw->programs[i].boost    = get_unaligned_be16(data);
		data += 2; remaining -= 2;

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
		data += 64;
		remaining -= 64;

		desc_len = strnlen(data, remaining);
		if (desc_len >= remaining)
			return -EINVAL;
		if (desc_len > 0) {
			fw->configs[i].description =
				kmemdup(data, desc_len + 1, GFP_KERNEL);
			if (!fw->configs[i].description)
				return -ENOMEM;
		}
		data += desc_len + 1;
		remaining -= desc_len + 1;

		/* Device count field present when driver >= CONFDEV */
		if (fw->driver_version >= PPC_DRIVER_CONFDEV) {
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

		/* PLL source fields present when driver >= MTPLLSRC */
		if (fw->driver_version >= PPC_DRIVER_MTPLLSRC) {
			if (remaining < 5)
				return -EINVAL;
			fw->configs[i].pll_src      = data[0]; data++; remaining--;
			fw->configs[i].pll_src_rate = get_unaligned_be32(data);
			data += 4; remaining -= 4;
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
 *   [book][page][reg][val]   — single register write
 *   [hi][lo][0x81][--]       — msleep((hi<<8)|lo)
 *   [hi][lo][0x85][first]    — bulk write: length=(hi<<8)|lo, data follows
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

			if (i >= block->num_commands)
				return -EINVAL;
			/* Verify bulk payload stays within block->data */
			if ((size_t)i * 4 + 3 + bulk_len >
			    (size_t)block->num_commands * 4)
				return -EINVAL;

			book   = data[i * 4];
			page   = data[i * 4 + 1];
			offset = data[i * 4 + 2];

			if (bulk_len > 1) {
				ret = tas2557_dev_bulk_write(tas2557,
							     TAS2557_REG(book, page, offset),
							     &data[i * 4 + 3], bulk_len);
			} else {
				ret = tas2557_dev_write(tas2557,
							TAS2557_REG(book, page, offset),
							data[i * 4 + 3]);
			}
			if (ret < 0)
				return ret;

			/*
			 * The 0x85 marker word is already consumed; now skip
			 * the target-register word plus the remaining payload
			 * words, matching the TI reference loader:
			 *   nCommand++; if (len >= 2) nCommand += (len-2)/4 + 1;
			 */
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
 * @want_dev_b is always false and the DEV_A blocks are applied.
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
	default:
		/* PGM_ALL, CFG_POST/POST_POWER and any unknown type: apply */
		return true;
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
 * tas2557_load_config_data - apply a configuration's blocks in the order the
 * hardware expects: the pre-coefficient block, then the coefficient block,
 * then the device-independent post blocks.  @want_dev_b selects the DEV_B
 * (right) blocks of stereo firmware instead of DEV_A (left).  The coefficient
 * and pre blocks are stored in the opposite order in the firmware, so they
 * are applied by type rather than in storage order (matching the TI driver).
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

	ret = tas2557_load_fw_data_type(tas2557, img_data, TAS2557_BLOCK_CFG_POST);
	if (ret < 0)
		return ret;

	return tas2557_load_fw_data_type(tas2557, img_data, TAS2557_BLOCK_CFG_POST_POWER);
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
 *   hardware reset → software reset → default registers → IRQ config →
 *   program blocks (DSP coefficients) → PLL block for config →
 *   config blocks (sample-rate-specific settings)
 *
 * PLL configuration comes entirely from the firmware PLL block; no manual
 * PLL register writes are performed here or anywhere else.
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

	/* If currently powered, shut down cleanly first */
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
	dev_info(tas2557->dev, "loading program %u (%s)\n",
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
	dev_info(tas2557->dev,
		 "loading config %u (%s) rate=%u Hz PLL[%u]\n",
		 config_idx, config->name, config->sample_rate, config->pll);
	ret = tas2557_load_config_data(tas2557, &config->data, want_dev_b);
	if (ret < 0)
		return ret;

	tas2557->current_config = config_idx;

	/* Re-power if we were running before (called from failsafe) */
	if (tas2557->powered) {
		ret = tas2557_load_data(tas2557, tas2557_startup_data);
		if (ret < 0)
			return ret;
		ret = tas2557_load_data(tas2557, tas2557_unmute_data);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
 * tas2557_fw_ready - firmware load callback
 *
 * Called from request_firmware_nowait().  Parses and stores the firmware,
 * then pre-loads program 0 / config 0 so the DSP registers are ready for
 * the first tas2557_enable() call.
 */
static void tas2557_fw_ready(const struct firmware *fw_entry, void *context)
{
	struct tas2557_priv *tas2557 = context;
	struct tas2557_firmware *fw;
	unsigned int i;
	int ret;

	if (!fw_entry || !fw_entry->data || fw_entry->size == 0) {
		/*
		 * Firmware is REQUIRED.  Without it the TAS2557 DSP cannot
		 * route any audio; Class-D will produce silence or noise.
		 */
		dev_err(tas2557->dev,
			"TAS2557 firmware '%s' not found or empty — audio CANNOT pass without DSP firmware; install the firmware file to /lib/firmware/\n",
			tas2557->fw_name);
		return;
	}

	dev_dbg(tas2557->dev, "firmware received, size=%zu\n",
		fw_entry->size);

	fw = fw_parse(tas2557, fw_entry->data, fw_entry->size);
	release_firmware(fw_entry);

	if (IS_ERR(fw)) {
		dev_err(tas2557->dev,
			"TAS2557 firmware '%s' parse error %ld — audio CANNOT pass; firmware may be corrupt\n",
			tas2557->fw_name, PTR_ERR(fw));
		return;
	}

	/* Replace any previously loaded firmware */
	if (tas2557->fw)
		tas2557_fw_free(tas2557->fw);

	tas2557->fw = fw;
	/* Ensure fw pointer is visible before fw_loaded flag */
	smp_wmb();
	WRITE_ONCE(tas2557->fw_loaded, true);

	dev_info(tas2557->dev,
		 "firmware '%s' loaded: ver=0x%x ppc=0x%x drv=0x%x %u program(s) %u config(s)\n",
		 tas2557->fw_name, fw->fw_version, fw->ppc_version,
		 fw->driver_version, fw->num_programs, fw->num_configs);

	for (i = 0; i < fw->num_programs; i++)
		dev_info(tas2557->dev, "  program[%u]: %s\n",
			 i, fw->programs[i].description ?: "(unnamed)");
	for (i = 0; i < fw->num_configs; i++)
		dev_info(tas2557->dev,
			 "  config[%u]: %s (prog=%u rate=%u PLL[%u])\n",
			 i, fw->configs[i].description ?: "(unnamed)",
			 fw->configs[i].program, fw->configs[i].sample_rate,
			 fw->configs[i].pll);

	/* Pre-load program 0 / best-match config so DSP is ready */
	ret = tas2557_set_program(tas2557, 0, -1);
	if (ret < 0)
		dev_err(tas2557->dev,
			"initial program load failed: %d\n", ret);
	else
		dev_info(tas2557->dev, "firmware initialised successfully\n");
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

	mutex_lock(&tas2557->dev_lock);

	if (tas2557->err_code)
		dev_info(tas2557->dev, "reset (err_code=0x%08x)\n",
			 tas2557->err_code);

	/* Assert reset (GPIO_ACTIVE_HIGH polarity: 0 = reset asserted) */
	gpiod_set_value_cansleep(tas2557->reset_gpio, 0);
	usleep_range(5000, 6000);
	/* Release reset */
	gpiod_set_value_cansleep(tas2557->reset_gpio, 1);
	usleep_range(10000, 11000);

	tas2557->current_book = 0xff;
	tas2557->current_page = 0xff;
	tas2557->err_code = 0;

	mutex_unlock(&tas2557->dev_lock);
}

/* =========================================================================
 * IRQ handling + failsafe recovery
 * =========================================================================
 */
static void tas2557_irq_work_func(struct work_struct *work)
{
	struct tas2557_priv *tas2557 =
		container_of(work, struct tas2557_priv, irq_work.work);
	unsigned int int1 = 0, int2 = 0, pwr_flag = 0;
	int ret;

	/* Do not process interrupts before firmware is ready */
	if (!READ_ONCE(tas2557->fw_loaded) || !tas2557->powered) {
		dev_dbg(tas2557->dev, "IRQ: device not ready, skipping\n");
		return;
	}

	/* Disable INT output while reading */
	tas2557_dev_write(tas2557, TAS2557_GPIO4_PIN_REG, 0x00);

	ret = tas2557_dev_read(tas2557, TAS2557_FLAGS_1, &int1);
	if (ret >= 0)
		ret = tas2557_dev_read(tas2557, TAS2557_FLAGS_2, &int2);

	if (ret < 0) {
		dev_err(tas2557->dev, "IRQ: failed to read FLAGS\n");
		goto reset;
	}

	if ((int1 & 0xfc) || (int2 & 0x0c)) {
		dev_err(tas2557->dev, "IRQ: FLAGS_1=0x%02x FLAGS_2=0x%02x\n",
			int1, int2);

		if (int1 & 0x80)
			tas2557->err_code |= ERROR_OVER_CURRENT;
		if (int1 & 0x40)
			tas2557->err_code |= ERROR_UNDER_VOLTAGE;
		if (int1 & 0x20)
			tas2557->err_code |= ERROR_CLK_HALT;
		if (int1 & 0x10)
			tas2557->err_code |= ERROR_DIE_OVERTEMP;
		if (int1 & 0x08)
			tas2557->err_code |= ERROR_BROWNOUT;
		if (int1 & 0x04)
			tas2557->err_code |= ERROR_CLK_LOST;
		goto reset;
	}

	ret = tas2557_dev_read(tas2557, TAS2557_POWER_UP_FLAG_REG, &pwr_flag);
	if (ret < 0)
		goto reset;

	if ((pwr_flag & 0xc0) != 0xc0) {
		dev_err(tas2557->dev, "IRQ: power-up flag=0x%02x\n", pwr_flag);
		tas2557->err_code |= ERROR_CLASSD_PWR;
		goto reset;
	}

	/* All OK — re-enable INT output */
	tas2557_dev_write(tas2557, TAS2557_GPIO4_PIN_REG, 0x07);
	if (tas2557->irq_enabled && tas2557->irq)
		enable_irq(tas2557->irq);
	return;

reset:
	if (tas2557_failsafe_recovery(tas2557) == 0) {
		tas2557_dev_write(tas2557, TAS2557_GPIO4_PIN_REG, 0x07);
		if (tas2557->irq_enabled && tas2557->irq)
			enable_irq(tas2557->irq);
	}
}

static irqreturn_t tas2557_irq_handler(int irq, void *data)
{
	struct tas2557_priv *tas2557 = data;

	if (tas2557->irq_enabled) {
		disable_irq_nosync(tas2557->irq);
		schedule_delayed_work(&tas2557->irq_work, msecs_to_jiffies(100));
	}

	return IRQ_HANDLED;
}

static int tas2557_failsafe_recovery(struct tas2557_priv *tas2557)
{
	int ret;

	tas2557->restart_count++;
	dev_info(tas2557->dev, "failsafe recovery attempt %u\n",
		 tas2557->restart_count);

	if (tas2557->restart_count > 5) {
		dev_err(tas2557->dev, "too many recovery attempts, giving up\n");
		tas2557->err_code |= ERROR_FAILSAFE;
		return -EIO;
	}

	tas2557_hw_reset(tas2557);

	ret = tas2557_load_data(tas2557, tas2557_default_data);
	if (ret < 0)
		return ret;

	if (tas2557->fw_loaded && tas2557->fw) {
		ret = tas2557_set_program(tas2557, tas2557->current_program,
					  (int)tas2557->current_config);
		if (ret < 0)
			return ret;
	}

	if (tas2557->powered) {
		ret = tas2557_load_data(tas2557, tas2557_startup_data);
		if (ret < 0)
			return ret;
		ret = tas2557_load_data(tas2557, tas2557_unmute_data);
		if (ret < 0)
			return ret;
	}

	tas2557->err_code = ERROR_NONE;
	dev_info(tas2557->dev, "failsafe recovery succeeded\n");
	return 0;
}

/* =========================================================================
 * Die temperature monitoring
 * =========================================================================
 */
static int tas2557_get_die_temp(struct tas2557_priv *tas2557, int *temp)
{
	unsigned char buf[4];
	int raw, ret;

	ret = tas2557_dev_bulk_read(tas2557, TAS2557_DIE_TEMP_REG, buf, 4);
	if (ret < 0)
		return ret;

	raw = ((int)buf[0] << 24) | ((int)buf[1] << 16) |
	      ((int)buf[2] << 8)  |  (int)buf[3];

	/* Die temp is a signed Q8.23 fixed-point value already in Celsius */
	*temp = raw >> 23;
	return 0;
}

static void tas2557_temp_work_func(struct work_struct *work)
{
	struct tas2557_priv *tas2557 =
		container_of(work, struct tas2557_priv, temp_work);
	int temp, ret;

	if (!tas2557->fw_loaded || !tas2557->powered ||
	    !tas2557->temp_monitor_enabled)
		return;

	ret = tas2557_get_die_temp(tas2557, &temp);
	if (ret < 0) {
		dev_warn(tas2557->dev, "die temp read failed: %d\n", ret);
		return;
	}

	tas2557->die_temp = temp;

	if (temp > TAS2557_SAFE_TEMP_HIGH) {
		dev_err(tas2557->dev,
			"die over-temperature %d °C — muting\n", temp);
		tas2557->err_code |= ERROR_DIE_OVERTEMP;
		tas2557_dev_update_bits(tas2557, TAS2557_MUTE_REG,
					TAS2557_CLASSD_MUTE, TAS2557_CLASSD_MUTE);
	}
}

static enum hrtimer_restart tas2557_temp_timer_cb(struct hrtimer *timer)
{
	struct tas2557_priv *tas2557 =
		container_of(timer, struct tas2557_priv, temp_timer);

	if (tas2557->powered && tas2557->temp_monitor_enabled) {
		schedule_work(&tas2557->temp_work);
		hrtimer_forward_now(timer,
				    ms_to_ktime(TAS2557_TEMP_CHECK_PERIOD));
		return HRTIMER_RESTART;
	}

	return HRTIMER_NORESTART;
}

static void tas2557_start_temp_monitor(struct tas2557_priv *tas2557)
{
	if (tas2557->temp_monitor_enabled)
		hrtimer_start(&tas2557->temp_timer,
			      ms_to_ktime(TAS2557_TEMP_CHECK_PERIOD),
			      HRTIMER_MODE_REL);
}

static void tas2557_stop_temp_monitor(struct tas2557_priv *tas2557)
{
	hrtimer_cancel(&tas2557->temp_timer);
	cancel_work_sync(&tas2557->temp_work);
}

/* =========================================================================
 * Power management
 *
 * Power-up sequence (firmware-driven):
 *   1. tas2557_set_program() — hw+sw reset, load defaults+IRQ config,
 *      load firmware PLL/program/config blocks (PLL set by firmware)
 *   2. tas2557_startup_data  — enable GPIO clocks, Class-D, Boost, DSP
 *   3. Set SNS_CTRL slots, DAC gain
 *   4. tas2557_unmute_data   — clear MUTE + SOFT_MUTE
 *   5. Check POWER_UP_FLAG + FLAGS for PLL lock / clock errors
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
		dev_info(tas2557->dev,
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

static int tas2557_enable(struct tas2557_priv *tas2557, bool enable)
{
	int ret = 0;

	if (enable && !tas2557->powered) {
		if (!READ_ONCE(tas2557->fw_loaded) || !tas2557->fw) {
			dev_warn(tas2557->dev,
				 "firmware not loaded — amplifier cannot produce audio; install %s to /lib/firmware/\n",
				 tas2557->fw_name[0] ? tas2557->fw_name
						     : TAS2557_FW_NAME);
			return -ENODEV;
		}

		tas2557_stop_temp_monitor(tas2557);

		/*
		 * Re-apply firmware program + config for the current sample
		 * rate.  This performs hw/sw reset and loads the firmware's
		 * PLL block — no manual PLL register writes needed or done.
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
				sns |= tas2557->imon_slot << TAS2557_ISNS_SLOT_SHIFT;
			if (tas2557->vsense_enabled)
				sns |= tas2557->vmon_slot << TAS2557_VSNS_SLOT_SHIFT;
			tas2557_dev_write(tas2557, TAS2557_SNS_CTRL_REG, sns);
		}

		/* Apply DAC gain */
		tas2557_dev_update_bits(tas2557, TAS2557_SPK_CTRL_REG,
					TAS2557_DAC_GAIN_MASK,
					tas2557->dac_gain << TAS2557_DAC_GAIN_SHIFT);

		/* Unmute */
		ret = tas2557_load_data(tas2557, tas2557_unmute_data);
		if (ret < 0) {
			dev_err(tas2557->dev, "unmute failed: %d\n", ret);
			return ret;
		}

		tas2557->powered = true;
		tas2557->muted   = false;
		tas2557->restart_count = 0;

		/* Verify PLL lock and clock health */
		tas2557_check_pll_lock(tas2557);

		tas2557_start_temp_monitor(tas2557);

		dev_info(tas2557->dev, "amplifier powered on (prog=%u cfg=%u)\n",
			 tas2557->current_program, tas2557->current_config);

	} else if (!enable && tas2557->powered) {
		tas2557_stop_temp_monitor(tas2557);

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
	unsigned int width_n;
	unsigned int offset;
	int ret;

	dev_dbg(tas2557->dev, "hw_params: rate=%u format=%u\n",
		rate, params_format(params));

	/* Store sample rate; tas2557_enable() uses it to pick the right config */
	tas2557->sample_rate = rate;

	/*
	 * Set ASI word-length using the slot width from DT (ti,i2s-bit-width),
	 * not the stream PCM width.  The Q6AFE sends I2S frames at the
	 * configured slot width; data is left-aligned within each slot.
	 */
	switch (tas2557->i2s_bits) {
	case 16:
		width_n = 0;
		break;
	case 20:
		width_n = 1;
		break;
	case 24:
		width_n = 2;
		break;
	case 32:
		width_n = 3;
		break;
	default:
		dev_err(tas2557->dev,
			"unsupported ti,i2s-bit-width value %u\n", tas2557->i2s_bits);
		return -EINVAL;
	}

	/* ASI1 word length */
	ret = tas2557_dev_update_bits(tas2557, TAS2557_ASI1_DAC_FORMAT_REG,
				      TAS2557_WORDLENGTH_MASK, width_n << 3);
	if (ret < 0)
		return ret;

	/* Channel offset: left=0, right=slot_bytes */
	offset = (tas2557->channel == 0) ? 0 : (tas2557->i2s_bits / 8);

	ret = tas2557_dev_write(tas2557, TAS2557_ASI1_OFFSET1_REG, offset);
	if (ret < 0)
		dev_warn(tas2557->dev, "ASI1 offset write failed: %d\n", ret);

	ret = tas2557_dev_write(tas2557, TAS2557_ASI2_OFFSET1_REG, offset);
	if (ret < 0)
		dev_warn(tas2557->dev, "ASI2 offset write failed: %d\n", ret);

	dev_dbg(tas2557->dev, "ASI: %u-bit, %s channel, offset=%u bytes\n",
		tas2557->i2s_bits,
		tas2557->channel ? "right" : "left", offset);

	/*
	 * NOTE: PLL (MAIN_CLKIN, PLL_CLKIN, PLL J/P/D/N) is NOT configured
	 * here.  It is set entirely by the firmware PLL block loaded via
	 * tas2557_set_program() during tas2557_enable().
	 */

	return 0;
}

static int tas2557_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);
	unsigned int asi_fmt = 0;

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

	return tas2557_dev_update_bits(tas2557, TAS2557_ASI1_DAC_FORMAT_REG,
				       TAS2557_FORMAT_MASK, asi_fmt);
}

static int tas2557_set_tdm_slot(struct snd_soc_dai *dai,
				unsigned int tx_mask, unsigned int rx_mask,
				int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);
	int rx_slot = -1, tx_slot = -1;
	int ret;

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

	tas2557->tdm_rx_slot   = rx_slot;
	tas2557->tdm_tx_slot   = tx_slot;
	tas2557->tdm_slot_width = slot_width;

	if (rx_slot >= 0) {
		ret = tas2557_dev_write(tas2557, TAS2557_ASI1_OFFSET1_REG,
					rx_slot * slot_width / 8);
		if (ret < 0)
			return ret;
	}
	if (tx_slot >= 0) {
		ret = tas2557_dev_write(tas2557, TAS2557_ASI1_OFFSET2_REG,
					tx_slot * slot_width / 8);
		if (ret < 0)
			return ret;
	}

	dev_dbg(tas2557->dev, "TDM: rx=%d tx=%d width=%d\n",
		rx_slot, tx_slot, slot_width);
	return 0;
}

static int tas2557_mute_stream(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);

	return tas2557_enable(tas2557, !mute);
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

static int tas2557_power_get(struct snd_kcontrol *kc,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct tas2557_priv *tas2557 =
		snd_soc_component_get_drvdata(snd_kcontrol_chip(kc));
	ucontrol->value.integer.value[0] = tas2557->powered;
	return 0;
}

static int tas2557_power_put(struct snd_kcontrol *kc,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct tas2557_priv *tas2557 =
		snd_soc_component_get_drvdata(snd_kcontrol_chip(kc));
	return tas2557_enable(tas2557, ucontrol->value.integer.value[0] != 0);
}

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

	tas2557->dac_gain = gain;

	if (tas2557->powered)
		ret = tas2557_dev_update_bits(tas2557, TAS2557_SPK_CTRL_REG,
					      TAS2557_DAC_GAIN_MASK,
					      gain << TAS2557_DAC_GAIN_SHIFT);
	return ret;
}

static int tas2557_program_get(struct snd_kcontrol *kc,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct tas2557_priv *tas2557 =
		snd_soc_component_get_drvdata(snd_kcontrol_chip(kc));
	ucontrol->value.integer.value[0] = tas2557->current_program;
	return 0;
}

static int tas2557_program_put(struct snd_kcontrol *kc,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct tas2557_priv *tas2557 =
		snd_soc_component_get_drvdata(snd_kcontrol_chip(kc));
	unsigned int program = ucontrol->value.integer.value[0];

	if (!tas2557->fw_loaded || !tas2557->fw)
		return -ENODEV;
	if (program >= tas2557->fw->num_programs)
		return -EINVAL;

	return tas2557_set_program(tas2557, program, -1);
}

static int tas2557_config_get(struct snd_kcontrol *kc,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct tas2557_priv *tas2557 =
		snd_soc_component_get_drvdata(snd_kcontrol_chip(kc));
	ucontrol->value.integer.value[0] = tas2557->current_config;
	return 0;
}

static int tas2557_config_put(struct snd_kcontrol *kc,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct tas2557_priv *tas2557 =
		snd_soc_component_get_drvdata(snd_kcontrol_chip(kc));
	unsigned int config = ucontrol->value.integer.value[0];

	if (!tas2557->fw_loaded || !tas2557->fw)
		return -ENODEV;
	if (config >= tas2557->fw->num_configs)
		return -EINVAL;

	return tas2557_set_program(tas2557, tas2557->current_program,
				   (int)config);
}

static int tas2557_temp_get(struct snd_kcontrol *kc,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct tas2557_priv *tas2557 =
		snd_soc_component_get_drvdata(snd_kcontrol_chip(kc));
	int temp;

	if (tas2557->powered && tas2557_get_die_temp(tas2557, &temp) == 0)
		tas2557->die_temp = temp;

	ucontrol->value.integer.value[0] = tas2557->die_temp;
	return 0;
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

	tas2557->isense_enabled = en;
	if (tas2557->powered)
		ret = tas2557_dev_update_bits(tas2557, TAS2557_POWER_CTRL2_REG,
					      TAS2557_ISENSE_ENABLE,
					      en ? TAS2557_ISENSE_ENABLE : 0);
	return ret;
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

	tas2557->vsense_enabled = en;
	if (tas2557->powered)
		ret = tas2557_dev_update_bits(tas2557, TAS2557_POWER_CTRL2_REG,
					      TAS2557_VSENSE_ENABLE,
					      en ? TAS2557_VSENSE_ENABLE : 0);
	return ret;
}

static const struct snd_kcontrol_new tas2557_controls[] = {
	SOC_SINGLE_EXT("Speaker Switch", SND_SOC_NOPM, 0, 1, 0,
		       tas2557_power_get, tas2557_power_put),
	SOC_SINGLE_EXT_TLV("Speaker Volume", SND_SOC_NOPM, 0,
			   TAS2557_DAC_GAIN_MAX, 0,
			   tas2557_volume_get, tas2557_volume_put,
			   tas2557_dac_tlv),
	SOC_SINGLE_EXT("DSP Program", SND_SOC_NOPM, 0, 255, 0,
		       tas2557_program_get, tas2557_program_put),
	SOC_SINGLE_EXT("DSP Configuration", SND_SOC_NOPM, 0, 255, 0,
		       tas2557_config_get, tas2557_config_put),
	SOC_SINGLE_EXT("Die Temperature", SND_SOC_NOPM, 0, 200, 0,
		       tas2557_temp_get, NULL),
	SOC_SINGLE_EXT("ISENSE Enable", SND_SOC_NOPM, 0, 1, 0,
		       tas2557_isense_get, tas2557_isense_put),
	SOC_SINGLE_EXT("VSENSE Enable", SND_SOC_NOPM, 0, 1, 0,
		       tas2557_vsense_get, tas2557_vsense_put),
};

/* =========================================================================
 * DAPM topology
 *
 * Playback path:  ASI2 (AIF_IN, stream "Playback") → DAC → ClassD → OUT
 * Capture path:   ClassD → ISENSE/VSENSE → SENSE (AIF_OUT, "Capture")
 *
 * The machine driver should add an external DAPM route:
 *   { "ASI2", NULL, "Primary MI2S Playback" }
 * =========================================================================
 */
static int tas2557_classd_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		return tas2557_enable(tas2557, true);
	case SND_SOC_DAPM_PRE_PMD:
		return tas2557_enable(tas2557, false);
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
 * debugfs interface
 *
 * Compiled only when CONFIG_DEBUG_FS is enabled.
 *
 * Files:
 *   tas2557/status   — human-readable device state + live register reads
 *   tas2557/regdump  — curated register dump across books/pages
 *   tas2557/reg      — write "<book> <page> <reg> [val]" to peek/poke;
 *                      read returns the last peeked value
 * =========================================================================
 */
#if IS_ENABLED(CONFIG_DEBUG_FS)

static int tas2557_dbg_status_show(struct seq_file *s, void *unused)
{
	struct tas2557_priv *tas2557 = s->private;
	unsigned int pwr1 = 0, pwr2 = 0, mute = 0, flag = 0, f1 = 0, f2 = 0;
	unsigned int clkerr = 0, clkerr2 = 0, clkerr3 = 0;
	const char *pg_str;

	/* PG version */
	switch (tas2557->pg_id) {
	case TAS2557_PG_VERSION_1P0:
		pg_str = "PG1.0";
		break;
	case TAS2557_PG_VERSION_2P0:
		pg_str = "PG2.0";
		break;
	case TAS2557_PG_VERSION_2P1:
		pg_str = "PG2.1";
		break;
	default:
		pg_str = "unknown";
		break;
	}

	seq_printf(s, "pg_id:        0x%02x (%s)\n",
		   (unsigned int)tas2557->pg_id, pg_str);

	/* Firmware */
	if (tas2557->fw_loaded && tas2557->fw) {
		struct tas2557_firmware *fw = tas2557->fw;
		struct tas2557_config *cfg = NULL;

		if (tas2557->current_config < fw->num_configs)
			cfg = &fw->configs[tas2557->current_config];

		seq_printf(s, "firmware:     loaded (%s)\n", tas2557->fw_name);
		seq_printf(s, "fw_version:   0x%08x\n", fw->fw_version);
		seq_printf(s, "ppc_version:  0x%08x\n", fw->ppc_version);
		seq_printf(s, "num_programs: %u\n", fw->num_programs);
		seq_printf(s, "num_configs:  %u\n", fw->num_configs);
		seq_printf(s, "active_prog:  %u (%s)\n",
			   tas2557->current_program,
			   fw->programs[tas2557->current_program].name);
		seq_printf(s, "active_cfg:   %u (%s, %u Hz)\n",
			   tas2557->current_config,
			   cfg ? cfg->name : "?",
			   cfg ? cfg->sample_rate : 0);
	} else {
		seq_printf(s, "firmware:     NOT LOADED (%s)\n",
			   tas2557->fw_name[0] ? tas2557->fw_name
					       : TAS2557_FW_NAME);
	}

	/* Power state */
	seq_printf(s, "powered:      %s\n",
		   tas2557->powered ? "yes" : "no");
	seq_printf(s, "muted:        %s\n",
		   tas2557->muted   ? "yes" : "no");

	/* DT parameters */
	seq_printf(s, "i2s_bits:     %u\n",  tas2557->i2s_bits);
	seq_printf(s, "channel:      %u (%s)\n", tas2557->channel,
		   tas2557->channel ? "right" : "left");
	seq_printf(s, "imon_slot:    %u\n",  tas2557->imon_slot);
	seq_printf(s, "vmon_slot:    %u\n",  tas2557->vmon_slot);
	seq_printf(s, "sample_rate:  %u\n",  tas2557->sample_rate);

	/* Error / health */
	seq_printf(s, "err_code:     0x%08x\n", tas2557->err_code);
	seq_printf(s, "restart_cnt:  %u\n",     tas2557->restart_count);
	seq_printf(s, "die_temp:     %d C\n",   tas2557->die_temp);

	/* Live register reads */
	if (tas2557->powered) {
		tas2557_dev_read(tas2557, TAS2557_POWER_CTRL1_REG, &pwr1);
		tas2557_dev_read(tas2557, TAS2557_POWER_CTRL2_REG, &pwr2);
		tas2557_dev_read(tas2557, TAS2557_MUTE_REG,        &mute);
		tas2557_dev_read(tas2557, TAS2557_POWER_UP_FLAG_REG, &flag);
		tas2557_dev_read(tas2557, TAS2557_FLAGS_1,          &f1);
		tas2557_dev_read(tas2557, TAS2557_FLAGS_2,          &f2);
		tas2557_dev_read(tas2557, TAS2557_CLK_ERR_CTRL,    &clkerr);
		tas2557_dev_read(tas2557, TAS2557_CLK_ERR_CTRL2,   &clkerr2);
		tas2557_dev_read(tas2557, TAS2557_CLK_ERR_CTRL3,   &clkerr3);

		seq_printf(s, "POWER_CTRL1:  0x%02x\n", pwr1);
		seq_printf(s, "POWER_CTRL2:  0x%02x\n", pwr2);
		seq_printf(s, "MUTE:         0x%02x\n", mute);
		seq_printf(s, "PWR_UP_FLAG:  0x%02x  (%s)\n", flag,
			   (flag & 0xc0) == 0xc0 ? "PLL locked" : "NOT locked");
		seq_printf(s, "FLAGS_1:      0x%02x%s\n", f1,
			   (f1 & 0x04) ? "  *** CLK ERROR ***" : "");
		seq_printf(s, "FLAGS_2:      0x%02x\n", f2);
		seq_printf(s, "CLK_ERR_CTRL: 0x%02x\n", clkerr);
		seq_printf(s, "CLK_ERR_CTR2: 0x%02x\n", clkerr2);
		seq_printf(s, "CLK_ERR_CTR3: 0x%02x\n", clkerr3);
	} else {
		seq_puts(s, "(live registers not read: device powered off)\n");
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(tas2557_dbg_status);

static int tas2557_dbg_regdump_show(struct seq_file *s, void *unused)
{
	struct tas2557_priv *tas2557 = s->private;
	unsigned int val = 0;

	/* Helper macro to read and print one register */
#define DUMP(b, p, r, name) do {				\
	tas2557_dev_read(tas2557, TAS2557_REG((b), (p), (r)), &val);	\
	seq_printf(s, "B%uP%02uR%03u  %-28s = 0x%02x\n",		\
		   (b), (p), (r), (name), val);				\
} while (0)

	seq_puts(s, "=== Book 0 Page 0 (core control) ===\n");
	DUMP(0,  0,   1, "SW_RESET");
	DUMP(0,  0,   3, "REV_PGID");
	DUMP(0,  0,   4, "POWER_CTRL1");
	DUMP(0,  0,   5, "POWER_CTRL2");
	DUMP(0,  0,   6, "SPK_CTRL");
	DUMP(0,  0,   7, "MUTE");
	DUMP(0,  0,   8, "SNS_CTRL");
	DUMP(0,  0,  42, "ASI_CTL1");
	DUMP(0,  0,  44, "CLK_ERR_CTRL");
	DUMP(0,  0,  45, "CLK_ERR_CTRL2");
	DUMP(0,  0,  46, "CLK_ERR_CTRL3");
	DUMP(0,  0, 100, "POWER_UP_FLAG");
	DUMP(0,  0, 104, "FLAGS_1");
	DUMP(0,  0, 108, "FLAGS_2");

	seq_puts(s, "\n=== Book 0 Page 1 (ASI / clocks / GPIO) ===\n");
	DUMP(0,  1,   1, "ASI1_DAC_FORMAT");
	DUMP(0,  1,  21, "ASI2_DAC_FORMAT");
	DUMP(0,  1,  23, "ASI2_OFFSET1");
	DUMP(0,  1,  28, "ASI2_DAC_BCLK");
	DUMP(0,  1,  29, "ASI2_DAC_WCLK");
	DUMP(0,  1,  33, "ASI2_BDIV_CLK_SEL");
	DUMP(0,  1,  34, "ASI2_BDIV_CLK_RATIO");
	DUMP(0,  1,  35, "ASI2_WDIV_CLK_RATIO");
	DUMP(0,  1,  65, "GPIO5 (ASI2 BCLK)");
	DUMP(0,  1,  66, "GPIO6 (ASI2 WCLK)");
	DUMP(0,  1,  67, "GPIO7 (ASI2 DOUT)");
	DUMP(0,  1,  68, "GPIO8 (ASI2 DIN)");
	DUMP(0,  1,  77, "GPI_PIN");
	DUMP(0,  1, 115, "MAIN_CLKIN");
	DUMP(0,  1, 116, "PLL_CLKIN");

	seq_puts(s, "\n=== Book 100 Page 0 (DSP / PLL) ===\n");
	DUMP(100, 0,  27, "PLL_P_VAL");
	DUMP(100, 0,  28, "PLL_J_VAL");
	DUMP(100, 0,  29, "PLL_D_MSB");
	DUMP(100, 0,  30, "PLL_D_LSB");
	DUMP(100, 0,  31, "CLK_MISC");
	DUMP(100, 0,  32, "PLL_N_VAL");
	DUMP(100, 0,   7, "SOFT_MUTE");
	DUMP(100, 0,   1, "DAC_INTERPOL");

#undef DUMP
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(tas2557_dbg_regdump);

/* reg file: write "<book> <page> <reg> [val]", read returns last peek value */
static ssize_t tas2557_dbg_reg_read(struct file *file, char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct tas2557_priv *tas2557 = file->private_data;
	char tmp[32];
	int len;

	if (!tas2557->dbg_reg_valid) {
		len = scnprintf(tmp, sizeof(tmp), "no register selected\n");
	} else {
		unsigned int val = 0;

		tas2557_dev_read(tas2557,
				 TAS2557_REG(tas2557->dbg_book,
					     tas2557->dbg_page,
					     tas2557->dbg_reg),
				 &val);
		tas2557->dbg_val = val;
		len = scnprintf(tmp, sizeof(tmp),
				"B%u P%u R%u = 0x%02x\n",
				tas2557->dbg_book, tas2557->dbg_page,
				tas2557->dbg_reg, val);
	}

	return simple_read_from_buffer(buf, count, ppos, tmp, len);
}

static ssize_t tas2557_dbg_reg_write(struct file *file,
				     const char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct tas2557_priv *tas2557 = file->private_data;
	char tmp[32];
	unsigned int book, page, reg, val;
	int parsed;

	if (count >= sizeof(tmp))
		return -EINVAL;

	if (copy_from_user(tmp, buf, count))
		return -EFAULT;
	tmp[count] = '\0';

	/* Try "<book> <page> <reg> <val>" first, then "<book> <page> <reg>" */
	parsed = sscanf(tmp, "%u %u %u %u", &book, &page, &reg, &val);

	if (parsed < 3)
		return -EINVAL;

	/* Validate field ranges: book 0-255, page 0-255, reg 0-0x7f, val 0-0xff */
	if (book > 0xff || page > 0xff || reg > 0x7f)
		return -EINVAL;
	if (parsed == 4 && val > 0xff)
		return -EINVAL;

	if (parsed == 4) {
		/* Poke */
		int ret = tas2557_dev_write(tas2557,
					    TAS2557_REG(book, page, reg), val);
		if (ret < 0)
			return ret;
		tas2557->dbg_book      = book;
		tas2557->dbg_page      = page;
		tas2557->dbg_reg       = reg;
		tas2557->dbg_val       = val;
		tas2557->dbg_reg_valid = true;
	} else {
		/* Set pending peek target */
		tas2557->dbg_book      = book;
		tas2557->dbg_page      = page;
		tas2557->dbg_reg       = reg;
		tas2557->dbg_reg_valid = true;
	}

	return count;
}

static const struct file_operations tas2557_dbg_reg_fops = {
	.open  = simple_open,
	.read  = tas2557_dbg_reg_read,
	.write = tas2557_dbg_reg_write,
};

static void tas2557_debugfs_init(struct tas2557_priv *tas2557)
{
	char name[32];

	snprintf(name, sizeof(name), "tas2557-%s", dev_name(tas2557->dev));
	tas2557->debugfs_root = debugfs_create_dir(name, NULL);
	if (IS_ERR_OR_NULL(tas2557->debugfs_root)) {
		tas2557->debugfs_root = NULL;
		return;
	}

	debugfs_create_file("status",  0444, tas2557->debugfs_root, tas2557,
			    &tas2557_dbg_status_fops);
	debugfs_create_file("regdump", 0444, tas2557->debugfs_root, tas2557,
			    &tas2557_dbg_regdump_fops);
	debugfs_create_file("reg",     0644, tas2557->debugfs_root, tas2557,
			    &tas2557_dbg_reg_fops);
}

static void tas2557_debugfs_remove(struct tas2557_priv *tas2557)
{
	debugfs_remove_recursive(tas2557->debugfs_root);
	tas2557->debugfs_root = NULL;
}

#else /* !CONFIG_DEBUG_FS */

static inline void tas2557_debugfs_init(struct tas2557_priv *tas2557) {}
static inline void tas2557_debugfs_remove(struct tas2557_priv *tas2557) {}

#endif /* CONFIG_DEBUG_FS */

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

	tas2557->pg_id = pg_id;

	switch (pg_id) {
	case TAS2557_PG_VERSION_1P0:
		dev_info(tas2557->dev, "silicon: PG1.0 (0x%02x)\n", pg_id);
		default_fw = TAS2557_PG1P0_FW_NAME;
		break;
	case TAS2557_PG_VERSION_2P0:
		dev_info(tas2557->dev, "silicon: PG2.0 (0x%02x)\n", pg_id);
		default_fw = TAS2557_FW_NAME;
		break;
	case TAS2557_PG_VERSION_2P1:
		dev_info(tas2557->dev, "silicon: PG2.1 (0x%02x)\n", pg_id);
		default_fw = TAS2557_FW_NAME;
		break;
	default:
		dev_warn(tas2557->dev,
			 "unknown silicon version 0x%02x, assuming PG2.x firmware\n",
			 pg_id);
		default_fw = TAS2557_FW_NAME;
		break;
	}

	/*
	 * A "firmware-name" DT property (read at probe) overrides the
	 * PG-based default; dual-amp stereo boards load a different DSP
	 * blob (e.g. tas2557s_uCDSP.bin).
	 */
	if (!tas2557->fw_name[0])
		strscpy(tas2557->fw_name, default_fw, sizeof(tas2557->fw_name));

	ret = tas2557_load_data(tas2557, tas2557_default_data);
	if (ret < 0) {
		dev_err(tas2557->dev, "default data load failed: %d\n", ret);
		return ret;
	}

	ret = tas2557_load_data(tas2557, tas2557_irq_config);
	if (ret < 0)
		dev_warn(tas2557->dev, "IRQ config failed: %d\n", ret);

	/*
	 * Request firmware asynchronously.  tas2557_fw_ready() will be called
	 * once the firmware is available (or when the timeout elapses).
	 * fw_name was set from the DT "firmware-name" or the detected PG version.
	 */
	ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_UEVENT,
				      tas2557->fw_name, tas2557->dev,
				      GFP_KERNEL, tas2557, tas2557_fw_ready);
	if (ret) {
		dev_err(tas2557->dev,
			"failed to schedule firmware request for '%s': %d — audio will not work\n",
			tas2557->fw_name, ret);
		return ret;
	}

	dev_info(tas2557->dev,
		 "codec probed, requesting firmware '%s'\n",
		 tas2557->fw_name);
	return 0;
}

#ifdef CONFIG_PM
static int tas2557_suspend(struct snd_soc_component *component)
{
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);

	if (tas2557->powered)
		tas2557_enable(tas2557, false);

	return 0;
}

static int tas2557_resume(struct snd_soc_component *component)
{
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);
	int ret;

	tas2557_hw_reset(tas2557);

	ret = tas2557_load_data(tas2557, tas2557_default_data);
	if (ret < 0)
		dev_err(tas2557->dev, "defaults reload on resume failed: %d\n",
			ret);

	return ret;
}
#else
#define tas2557_suspend NULL
#define tas2557_resume  NULL
#endif

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
	INIT_DELAYED_WORK(&tas2557->irq_work, tas2557_irq_work_func);

	hrtimer_setup(&tas2557->temp_timer, tas2557_temp_timer_cb,
		      CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	INIT_WORK(&tas2557->temp_work, tas2557_temp_work_func);
	tas2557->temp_monitor_enabled = true;

	/* Default gain (0 dB) */
	tas2557->dac_gain = TAS2557_DAC_GAIN_MAX;

	/* DT properties with defaults per the pinned contract */
	if (of_property_read_u32(dev->of_node, "ti,i2s-bit-width",
				 &tas2557->i2s_bits))
		tas2557->i2s_bits = 16;

	if (of_property_read_u32(dev->of_node, "ti,channel",
				 &tas2557->channel))
		tas2557->channel = 0;	/* left */

	if (of_property_read_u32(dev->of_node, "ti,imon-slot",
				 &tas2557->imon_slot))
		tas2557->imon_slot = 0;

	if (of_property_read_u32(dev->of_node, "ti,vmon-slot",
				 &tas2557->vmon_slot))
		tas2557->vmon_slot = 2;

	/* Optional explicit firmware name; overrides the PG-based default. */
	if (!of_property_read_string(dev->of_node, "firmware-name", &fw_name))
		strscpy(tas2557->fw_name, fw_name, sizeof(tas2557->fw_name));

	/* Optional VDD regulator (1.8 V digital supply) */
	tas2557->vdd = devm_regulator_get_optional(dev, "vdd");
	if (IS_ERR(tas2557->vdd)) {
		ret = PTR_ERR(tas2557->vdd);
		if (ret == -EPROBE_DEFER)
			return ret;
		tas2557->vdd = NULL;
		dev_dbg(dev, "no vdd regulator\n");
	}

	if (tas2557->vdd) {
		ret = regulator_enable(tas2557->vdd);
		if (ret) {
			dev_err(dev, "vdd enable failed: %d\n", ret);
			return ret;
		}
		usleep_range(5000, 10000);
		dev_info(dev, "vdd regulator enabled\n");
	}

	/*
	 * Reset GPIO — active-high polarity flag in DT means the logical
	 * "asserted" value (reset) maps to GPIO low.  GPIOD_OUT_HIGH here
	 * sets the GPIO high on probe, i.e. device is released from reset
	 * and operational immediately.
	 */
	tas2557->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						      GPIOD_OUT_HIGH);
	if (IS_ERR(tas2557->reset_gpio)) {
		ret = PTR_ERR(tas2557->reset_gpio);
		if (ret == -EPROBE_DEFER)
			goto err_disable_reg;
		tas2557->reset_gpio = NULL;
		dev_dbg(dev, "no reset GPIO\n");
	}

	/* Also accept vendor DT property name "ti,cdc-reset" */
	if (!tas2557->reset_gpio) {
		tas2557->reset_gpio = devm_gpiod_get_optional(dev, "ti,cdc-reset",
							      GPIOD_OUT_HIGH);
		if (IS_ERR(tas2557->reset_gpio)) {
			ret = PTR_ERR(tas2557->reset_gpio);
			if (ret == -EPROBE_DEFER)
				goto err_disable_reg;
			tas2557->reset_gpio = NULL;
		}
	}

	dev_info(dev, "reset GPIO: %s\n",
		 tas2557->reset_gpio ? "present" : "not found");

	tas2557->regmap = devm_regmap_init_i2c(client, &tas2557_regmap_config);
	if (IS_ERR(tas2557->regmap)) {
		ret = PTR_ERR(tas2557->regmap);
		dev_err(dev, "regmap init failed: %d\n", ret);
		goto err_disable_reg;
	}

	/* Mark book/page as unknown so first access triggers proper switch */
	tas2557->current_book = 0xff;
	tas2557->current_page = 0xff;

	/* IRQ (from irq-gpios in DT, resolved to client->irq by OF layer) */
	if (client->irq) {
		ret = devm_request_threaded_irq(dev, client->irq,
						NULL, tas2557_irq_handler,
						IRQF_TRIGGER_LOW | IRQF_ONESHOT,
						"tas2557", tas2557);
		if (ret) {
			dev_warn(dev, "IRQ %d request failed: %d\n",
				 client->irq, ret);
		} else {
			tas2557->irq = client->irq;
			disable_irq_nosync(tas2557->irq);
			tas2557->irq_enabled = false;
			dev_info(dev, "IRQ %d registered (initially disabled)\n",
				 client->irq);
		}
	}

	tas2557_debugfs_init(tas2557);

	ret = devm_snd_soc_register_component(dev, &soc_component_tas2557,
					      &tas2557_dai, 1);
	if (ret) {
		dev_err(dev, "component registration failed: %d\n", ret);
		goto err_debugfs;
	}

	dev_info(dev, "TAS2557 probed (I2C addr 0x%02x, %s channel, %u-bit I2S)\n",
		 client->addr, tas2557->channel ? "right" : "left",
		 tas2557->i2s_bits);
	return 0;

err_debugfs:
	tas2557_debugfs_remove(tas2557);
err_disable_reg:
	if (tas2557->vdd)
		regulator_disable(tas2557->vdd);
	return ret;
}

static void tas2557_i2c_remove(struct i2c_client *client)
{
	struct tas2557_priv *tas2557 = i2c_get_clientdata(client);

	tas2557_stop_temp_monitor(tas2557);
	cancel_delayed_work_sync(&tas2557->irq_work);

	tas2557_debugfs_remove(tas2557);

	if (tas2557->fw) {
		tas2557_fw_free(tas2557->fw);
		tas2557->fw = NULL;
	}

	if (tas2557->vdd)
		regulator_disable(tas2557->vdd);
}

static const struct i2c_device_id tas2557_i2c_id[] = {
	{ "tas2557", 0 },
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
	.remove   = tas2557_i2c_remove,
	.id_table = tas2557_i2c_id,
};
module_i2c_driver(tas2557_i2c_driver);

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("ASoC TAS2557 Smart Amplifier Driver");
MODULE_LICENSE("GPL");
