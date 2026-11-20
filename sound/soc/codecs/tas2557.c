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
// Probe reads REV_PGID to detect the PG version, then requests firmware
// asynchronously.  The firmware program, PLL and config blocks are loaded
// once the firmware arrives and again only when the chips lost them or a
// different config is needed (sample rate change, fault recovery, resume).
//
// Register access, the paged regmap and the i2c_client address swap needed
// to reach several chips through one i2c_client are provided by
// tas2781-comlib(-i2c).  One DT node drives up to TAS2557_MAX_DEV chips,
// addressed by channel index into tas_priv->tasdevice[] and
// struct tas2557_priv::devs[].  Firmware, DAI, power and mute state are
// shared by the group; DSP program bookkeeping, ASI slot offsets and the
// reset/IRQ lines are per chip.

#include <linux/cleanup.h>
#include <linux/crc8.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
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
#include <sound/tas2781.h>
#include <sound/tas2781-comlib-i2c.h>
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

/*
 * struct tas2557_dev - per-chip state that comlib's struct tasdevice has
 * no field for.  One instance per entry in tas_priv->tasdevice[].
 */
struct tas2557_dev {
	/* Hardware revision of this chip */
	int pg_id;

	/*
	 * DSP program/config currently loaded and recovery-attempt count.
	 * tas2557_set_program() drives every chip to the same program and
	 * config, so these stay in lock-step; devs[0] is read back as the
	 * group's value.
	 */
	unsigned int current_program;
	unsigned int current_config;
	unsigned int restart_count;

	/*
	 * Cached ASI RX (playback) data-slot offset in BCLK cycles for this
	 * chip, set by set_tdm_slot() and re-applied by
	 * tas2557_apply_runtime_state().
	 */
	unsigned int asi_offset;
};

/*
 * struct tas2557_priv - group driver state (one node, up to TAS2557_MAX_DEV
 * chips), wrapping the shared tas2781-comlib device.
 */
struct tas2557_priv {
	struct tasdevice_priv *tas_priv;

	/* Per-device reset lines and fault IRQs (reset-gpios/interrupts) */
	struct gpio_desc *reset[TAS2557_MAX_DEV];
	int		  irq[TAS2557_MAX_DEV];
	bool		  irq_enabled;

	/* Which ASI (1 or 2) this board wires up (ti,asi-port, default 2) */
	unsigned int asi_port;

	/* Chip-specific state, one entry per tas_priv->tasdevice[] slot */
	struct tas2557_dev devs[TAS2557_MAX_DEV];

	/* Power state */
	bool powered;
	bool dac_muted;		/* mute state for SNDRV_PCM_STREAM_PLAYBACK */

	/* Firmware state, one image shared by the group */
	struct tas2557_firmware *fw;	/* NULL until tas2557_fw_ready() parses it */
	char fw_name[64];		/* filename requested at probe */
	bool fw_requested;		/* request_firmware_nowait() was issued */
	struct completion fw_done;	/* signalled when tas2557_fw_ready() returns */

	/*
	 * devs[].current_program/current_config are still loaded on the
	 * chips.  Cleared by every reset, resume, fw (re)load and failed
	 * program load; set when tas2557_set_program() completes.
	 */
	bool prog_valid;

	/* Sample rates of the current program's configs, for DAI startup */
	unsigned int *rates;
	struct snd_pcm_hw_constraint_list rate_list;

	/* Runtime audio configuration */
	unsigned int sample_rate;
	int dai_fmt;			/* ASI format from set_dai_fmt(), -1 = never set */
	unsigned int asi_delay;		/* 1 BCLK for DSP_A, 0 otherwise */
	unsigned int wordlength;	/* ASI WORDLENGTH_* bits from hw_params */

	bool tdm_configured;		/* true once set_tdm_slot() set an RX slot */

	/* Volume control */
	unsigned int dac_gain;
};

/*
 * tas2557_asi_reg() - pick the DAC format or RX offset register of
 * whichever ASI (ti,asi-port) this board wires up.  The other ASI's
 * registers are never written by this driver.
 */
static unsigned int tas2557_asi_reg(struct tas2557_priv *tas2557,
				    unsigned int asi1_reg, unsigned int asi2_reg)
{
	return tas2557->asi_port == TAS2557_ASI_PORT1 ? asi1_reg : asi2_reg;
}

/* Forward declarations */
static void tas2557_hw_reset(struct tas2557_priv *tas2557);
static int tas2557_failsafe_recovery(struct tas2557_priv *tas2557);
static int tas2557_apply_runtime_state(struct tas2557_priv *tas2557);
static int tas2557_power_up_locked(struct tas2557_priv *tas2557,
				   unsigned int prog_idx);

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
 * (p_tas2557_irq_config).  Reloaded after every reset.  GPIO4=INT1
 * output is wired the same way regardless of which ASI carries audio,
 * so it stays common to both ASI selections.
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
 * Startup sequence - power up Class-D, Boost, DSP/PLL clocks.  Split into
 * a common part and one part per ASI: only the ASI selected by
 * ti,asi-port is programmed, the other ASI's GPIOx_PIN registers stay at
 * reset defaults.  The PLL is not configured here; it comes from the
 * firmware PLL block loaded by tas2557_set_program().
 */
static const unsigned int tas2557_startup_common_data[] = {
	TAS2557_GPI_PIN_REG,      0x15,	/* enable DIN, MCLK, CCI */
	/* Power sequencing: Class-D + Boost first, then DSP/PLL */
	TAS2557_POWER_CTRL2_REG, 0xA0,		/* Class-D, Boost power up */
	TAS2557_POWER_CTRL2_REG, 0xA3,		/* Class-D, Boost, IV-sense power up */
	TAS2557_POWER_CTRL1_REG, 0xF8,		/* PLL, DSP, clock dividers power up */
	TAS2557_UDELAY, 2000,			/* 2 ms stabilisation */
	/* CE1_IC = ASI1; TI's ASI2 build writes the same value */
	TAS2557_CLK_ERR_CTRL, 0x2b,		/* enable clock error detection */
	TAS2557_DBOOST_CFG_REG, 0x0b,		/* reduce full-band noise */
	0xFFFFFFFF, 0xFFFFFFFF
};

/* ASI1 pin wiring: BCLK/WCLK/DOUT on GPIO1/GPIO2/GPIO3 */
static const unsigned int tas2557_startup_asi1_data[] = {
	TAS2557_GPIO1_PIN_REG, 0x01,	/* enable BCLK (ASI1) */
	TAS2557_GPIO2_PIN_REG, 0x01,	/* enable WCLK (ASI1) */
	TAS2557_GPIO3_PIN_REG, 0x10,	/* GPIO3 = ASI1 DOUT */
	0xFFFFFFFF, 0xFFFFFFFF
};

/*
 * ASI2 pin wiring (GPIO5-8) and BDIV/WDIV clock dividers; arm the BCLK
 * source (GPIO5) before its dividers.
 */
static const unsigned int tas2557_startup_asi2_data[] = {
	TAS2557_GPIO6_PIN_REG,            0x01,	/* GPIO6 = ASI2 WCLK input */
	TAS2557_GPIO8_PIN_REG,            0x02,	/* GPIO8 = ASI2 DIN */
	TAS2557_GPIO5_PIN_REG,            0x01,	/* GPIO5 = ASI2 BCLK input */
	TAS2557_ASI2_BDIV_CLK_SEL_REG,   0x01,
	TAS2557_ASI2_BDIV_CLK_RATIO_REG, 0x01,
	TAS2557_ASI2_BDIV_CLK_RATIO_REG, 0x81,	/* power up BDIV */
	TAS2557_ASI2_WDIV_CLK_RATIO_REG, 0x40,
	TAS2557_ASI2_WDIV_CLK_RATIO_REG, 0xc0,	/* power up WDIV */
	TAS2557_GPIO7_PIN_REG,            0x15,	/* GPIO7 = ASI2 DOUT */
	0xFFFFFFFF, 0xFFFFFFFF
};

/* Unmute sequence */
static const unsigned int tas2557_unmute_data[] = {
	TAS2557_MUTE_REG,      0x00,	/* unmute I and V sense */
	TAS2557_SOFT_MUTE_REG, 0x00,	/* clear soft-mute in DSP */
	0xFFFFFFFF, 0xFFFFFFFF
};

/* Shutdown sequence: device-independent part, run before the ASI part */
static const unsigned int tas2557_shutdown_common_data[] = {
	TAS2557_CLK_ERR_CTRL,   0x00,	/* disable clock error detection */
	TAS2557_SOFT_MUTE_REG,  0x01,	/* soft mute */
	TAS2557_UDELAY, 10000,		/* 10 ms */
	TAS2557_MUTE_REG,        0x03,	/* mute I and V sense */
	TAS2557_POWER_CTRL1_REG, 0x60,	/* DSP power down */
	TAS2557_UDELAY, 2000,		/* 2 ms */
	TAS2557_POWER_CTRL2_REG, 0x00,	/* Class-D, Boost power down */
	TAS2557_POWER_CTRL1_REG, 0x00,	/* all power down */
	0xFFFFFFFF, 0xFFFFFFFF
};

/* Disable ASI1 GPIOs */
static const unsigned int tas2557_shutdown_asi1_data[] = {
	TAS2557_GPIO1_PIN_REG, 0x00,
	TAS2557_GPIO2_PIN_REG, 0x00,
	TAS2557_GPIO3_PIN_REG, 0x00,
	0xFFFFFFFF, 0xFFFFFFFF
};

/* Disable ASI2 GPIOs */
static const unsigned int tas2557_shutdown_asi2_data[] = {
	TAS2557_GPIO5_PIN_REG, 0x00,
	TAS2557_GPIO6_PIN_REG, 0x00,
	TAS2557_GPIO7_PIN_REG, 0x00,
	TAS2557_GPIO8_PIN_REG, 0x00,
	0xFFFFFFFF, 0xFFFFFFFF
};

/* Disable the shared DIN/MCLK/CCI enable, run after the ASI-specific part */
static const unsigned int tas2557_shutdown_tail_data[] = {
	TAS2557_GPI_PIN_REG, 0x00,
	0xFFFFFFFF, 0xFFFFFFFF
};

/*
 * Book/page/register access
 *
 * All register I/O goes through tas2781-comlib's tasdevice_dev_*()
 * helpers, which drive a paged regmap and swap the i2c_client address to
 * reach each chip in the group.  Comlib does not lock: every caller holds
 * tas_priv->codec_lock, which also protects all driver state.  @chn
 * indexes tas_priv->tasdevice[]/tas2557->devs[]; @reg is built with
 * TASDEVICE_REG(book, page, reg).
 */

static int tas2557_dev_read(struct tas2557_priv *tas2557, unsigned int chn,
			    unsigned int reg, unsigned int *value)
{
	return tasdevice_dev_read(tas2557->tas_priv, chn, reg, value);
}

static int tas2557_dev_write(struct tas2557_priv *tas2557, unsigned int chn,
			     unsigned int reg, unsigned int value)
{
	return tasdevice_dev_write(tas2557->tas_priv, chn, reg, value);
}

static int tas2557_dev_update_bits(struct tas2557_priv *tas2557, unsigned int chn,
				   unsigned int reg, unsigned int mask,
				   unsigned int value)
{
	return tasdevice_dev_update_bits(tas2557->tas_priv, chn, reg, mask, value);
}

static int tas2557_dev_bulk_write(struct tas2557_priv *tas2557, unsigned int chn,
				  unsigned int reg, const u8 *data, size_t len)
{
	/* The chip auto-increments past reg 0x7f into the page register */
	if (TASDEVICE_PAGE_REG(reg) + len > 128)
		return -EINVAL;

	return tasdevice_dev_bulk_write(tas2557->tas_priv, chn, reg,
					(unsigned char *)data, len);
}

static int tas2557_load_data(struct tas2557_priv *tas2557, unsigned int chn,
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
			ret = tas2557_dev_write(tas2557, chn, reg, val);
			if (ret < 0)
				break;
		}
		i++;
	}

	return ret;
}

/* tas2557_load_data_all - apply a {reg,val} sequence table to every chip */
static int tas2557_load_data_all(struct tas2557_priv *tas2557,
				 const unsigned int *data)
{
	unsigned int chn;
	int ret;

	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++) {
		ret = tas2557_load_data(tas2557, chn, data);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* tas2557_write_all - write the same register/value to every chip */
static int tas2557_write_all(struct tas2557_priv *tas2557,
			     unsigned int reg, unsigned int value)
{
	unsigned int chn;
	int ret;

	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++) {
		ret = tas2557_dev_write(tas2557, chn, reg, value);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* tas2557_update_bits_all - update_bits the same register/mask/value on every chip */
static int tas2557_update_bits_all(struct tas2557_priv *tas2557,
				   unsigned int reg, unsigned int mask,
				   unsigned int value)
{
	unsigned int chn;
	int ret;

	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++) {
		ret = tas2557_dev_update_bits(tas2557, chn, reg, mask, value);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
 * tas2557_asi_fmt_update_all - update_bits @mask/@value in the DAC (playback)
 * format register of the selected ASI on every chip.
 */
static int tas2557_asi_fmt_update_all(struct tas2557_priv *tas2557,
				      unsigned int mask, unsigned int value)
{
	unsigned int dac = tas2557_asi_reg(tas2557, TAS2557_ASI1_DAC_FORMAT_REG,
					   TAS2557_ASI2_DAC_FORMAT_REG);

	return tas2557_update_bits_all(tas2557, dac, mask, value);
}

/*
 * tas2557_write_rx_offset - program OFFSET1 of the selected ASI on one chip:
 * its TDM slot offset plus the 1 BCLK delay of DSP_A.  The chip's DSP
 * format at offset 0 is DSP_B.
 */
static int tas2557_write_rx_offset(struct tas2557_priv *tas2557,
				   unsigned int chn)
{
	unsigned int reg = tas2557_asi_reg(tas2557, TAS2557_ASI1_OFFSET1_REG,
					   TAS2557_ASI2_OFFSET1_REG);

	return tas2557_dev_write(tas2557, chn, reg,
				 tas2557->asi_delay + tas2557->devs[chn].asi_offset);
}

/*
 * tas2557_startup_sequence / tas2557_shutdown_sequence - run the common
 * part of the power up/down sequence together with the part of the ASI
 * selected by ti,asi-port.
 *
 * The shutdown tables only mute, stop clock error detection, power down
 * the DSP/PLL/Class-D blocks and clear the GPIO pin muxes.  They do not
 * reset the chip or write the DSP program, coefficient or PLL registers,
 * so the firmware loaded by tas2557_set_program() survives a shutdown and
 * the next power-up only has to re-run the startup sequence.
 */
static int tas2557_startup_sequence(struct tas2557_priv *tas2557)
{
	const unsigned int *asi = tas2557->asi_port == TAS2557_ASI_PORT1 ?
				  tas2557_startup_asi1_data :
				  tas2557_startup_asi2_data;
	unsigned int dout_reg = tas2557_asi_reg(tas2557, TAS2557_GPIO3_PIN_REG,
						TAS2557_GPIO7_PIN_REG);
	unsigned int chn;
	int ret;

	ret = tas2557_load_data_all(tas2557, tas2557_startup_common_data);
	if (ret < 0)
		return ret;

	ret = tas2557_load_data_all(tas2557, asi);
	if (ret < 0)
		return ret;

	/* Only chip 0 drives DOUT of the shared ASI */
	for (chn = 1; chn < tas2557->tas_priv->ndev; chn++) {
		ret = tas2557_dev_write(tas2557, chn, dout_reg, 0x00);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int tas2557_shutdown_sequence(struct tas2557_priv *tas2557)
{
	const unsigned int *asi = tas2557->asi_port == TAS2557_ASI_PORT1 ?
				  tas2557_shutdown_asi1_data :
				  tas2557_shutdown_asi2_data;
	int ret;

	ret = tas2557_load_data_all(tas2557, tas2557_shutdown_common_data);
	if (ret < 0)
		return ret;

	ret = tas2557_load_data_all(tas2557, asi);
	if (ret < 0)
		return ret;

	return tas2557_load_data_all(tas2557, tas2557_shutdown_tail_data);
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
 * Fields the driver does not use (size, checksum, timestamp, description,
 * per-block checksums, ...) are skipped, not stored.
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
		dev_err(tas2557->tas_priv->dev, "firmware header too short (%zu bytes)\n",
			size);
		return -EINVAL;
	}

	/* Magic: four bytes "5552" (0x35 0x35 0x35 0x32) */
	if (data[0] != 0x35 || data[1] != 0x35 ||
	    data[2] != 0x35 || data[3] != 0x32) {
		dev_err(tas2557->tas_priv->dev,
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
		dev_err(tas2557->tas_priv->dev, "firmware header: description not NUL-terminated\n");
		return -EINVAL;
	}
	/* description string: informational only, not stored */
	data += desc_len + 1;

	if ((data - start) + 8 > (ptrdiff_t)size) {
		dev_err(tas2557->tas_priv->dev, "firmware header truncated\n");
		return -EINVAL;
	}

	device_family = get_unaligned_be32(data); data += 4;
	if (device_family != 0) {
		dev_err(tas2557->tas_priv->dev, "unsupported device family: %u\n",
			device_family);
		return -EINVAL;
	}

	fw->device = get_unaligned_be32(data); data += 4;
	if (fw->device != TASDEVICE_DSP_TAS_2557_MONO &&
	    fw->device != TASDEVICE_DSP_TAS_2557_DUAL_MONO) {
		dev_err(tas2557->tas_priv->dev,
			"unsupported device %u in firmware (expected 2=mono or 3=stereo)\n",
			fw->device);
		return -EINVAL;
	}

	dev_dbg(tas2557->tas_priv->dev,
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
		dev_err(tas2557->tas_priv->dev,
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
		dev_err(tas2557->tas_priv->dev, "firmware contains no programs\n");
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
		dev_err(tas2557->tas_priv->dev, "firmware contains no configurations\n");
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
		if (fw->configs[i].pll >= fw->num_plls) {
			dev_err(tas2557->tas_priv->dev,
				"config %u: PLL index %u out of range (%u total)\n",
				i, fw->configs[i].pll, fw->num_plls);
			return -EINVAL;
		}
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
			      struct tas2557_block *block, unsigned int chn)
{
	const u8 *data = block->data;
	unsigned int i;
	u8 book, page, offset, value;
	u16 sleep_time, bulk_len;
	int ret;

	dev_dbg(tas2557->tas_priv->dev, "chn %u: load block type=0x%x cmds=%u\n",
		chn, block->type, block->num_commands);

	for (i = 0; i < block->num_commands; ) {
		book   = data[i * 4];
		page   = data[i * 4 + 1];
		offset = data[i * 4 + 2];
		value  = data[i * 4 + 3];
		i++;

		if (offset <= 0x7f) {
			ret = tas2557_dev_write(tas2557, chn,
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
				ret = tas2557_dev_bulk_write(tas2557, chn,
							     TAS2557_REG(book, page, offset),
							     &data[i * 4 + 3], bulk_len);
			else
				ret = tas2557_dev_write(tas2557, chn,
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
 * tas2557_block_chn_range - which devs[] a firmware block type targets
 *
 * Mirrors tasdevice_load_block()'s dispatch in tas2781-fmwlib.c: DEV_A/
 * DEV_B blocks target exactly one physical chip (selected by its index in
 * the DT `reg` array, see the ti,tas2557.yaml binding), device-independent
 * blocks (PGM_ALL, CFG_POST, CFG_POST_POWER) and the PLL block target every
 * chip in the group.  @chnend is clamped to tas2557->tas_priv->ndev, so a DEV_B block
 * becomes an empty range on a single-chip group instead of touching a
 * devs[] slot that was never populated.  Returns false for a block type
 * this driver does not recognise, so the caller does not apply it blindly.
 */
static bool tas2557_block_chn_range(struct tas2557_priv *tas2557,
				    unsigned int type,
				    unsigned int *chn, unsigned int *chnend)
{
	switch (type) {
	case TAS2557_BLOCK_PGM_DEV_A:
	case TAS2557_BLOCK_CFG_COEFF_DEV_A:
	case TAS2557_BLOCK_CFG_PRE_DEV_A:
		*chn = 0;
		*chnend = 1;
		break;
	case TAS2557_BLOCK_PGM_DEV_B:
	case TAS2557_BLOCK_CFG_COEFF_DEV_B:
	case TAS2557_BLOCK_CFG_PRE_DEV_B:
		*chn = 1;
		*chnend = 2;
		break;
	case TAS2557_BLOCK_PLL:
	case TAS2557_BLOCK_PGM_ALL:
	case TAS2557_BLOCK_CFG_POST:
	case TAS2557_BLOCK_CFG_POST_POWER:
		*chn = 0;
		*chnend = tas2557->tas_priv->ndev;
		break;
	default:
		return false;
	}

	if (*chnend > tas2557->tas_priv->ndev)
		*chnend = tas2557->tas_priv->ndev;

	return true;
}

/*
 * tas2557_load_fw_data - apply every block of a data set in storage order,
 * each to whichever devs[] its type targets.  Used for program->data,
 * which only ever contains PGM_ALL/PGM_DEV_A/PGM_DEV_B blocks with no
 * ordering constraint between them.
 */
static int tas2557_load_fw_data(struct tas2557_priv *tas2557,
				struct tas2557_data *img_data)
{
	unsigned int i, chn, chnend, c;
	int ret;

	for (i = 0; i < img_data->num_blocks; i++) {
		if (!tas2557_block_chn_range(tas2557, img_data->blocks[i].type,
					     &chn, &chnend))
			continue;

		for (c = chn; c < chnend; c++) {
			ret = tas2557_load_block(tas2557, &img_data->blocks[i], c);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

/*
 * tas2557_load_fw_data_type - apply only the blocks of a single type,
 * in storage order, each to whichever devs[] that type targets.
 */
static int tas2557_load_fw_data_type(struct tas2557_priv *tas2557,
				     struct tas2557_data *img_data,
				     unsigned int type)
{
	unsigned int i, chn, chnend, c;
	int ret;

	if (!tas2557_block_chn_range(tas2557, type, &chn, &chnend))
		return 0;

	for (i = 0; i < img_data->num_blocks; i++) {
		if (img_data->blocks[i].type != type)
			continue;

		for (c = chn; c < chnend; c++) {
			ret = tas2557_load_block(tas2557, &img_data->blocks[i], c);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

/*
 * tas2557_load_config_data - apply a configuration's pre-power blocks in the
 * order the hardware expects: each chip's pre-coefficient block, then its
 * coefficient block (DEV_A before DEV_B), then the device-independent
 * CFG_POST block.  The CFG_POST_POWER block is NOT applied here; it needs
 * active clocks and is loaded by tas2557_load_config_post_power() after the
 * device is powered up.  Blocks are applied by type, not storage order (per
 * TI); tas2557_block_chn_range() decides which chip(s) each type reaches.
 */
static int tas2557_load_config_data(struct tas2557_priv *tas2557,
				    struct tas2557_data *img_data)
{
	static const unsigned int order[] = {
		TAS2557_BLOCK_CFG_PRE_DEV_A,
		TAS2557_BLOCK_CFG_COEFF_DEV_A,
		TAS2557_BLOCK_CFG_PRE_DEV_B,
		TAS2557_BLOCK_CFG_COEFF_DEV_B,
		TAS2557_BLOCK_CFG_POST,
	};
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(order); i++) {
		ret = tas2557_load_fw_data_type(tas2557, img_data, order[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
 * tas2557_load_config_post_power - apply the CFG_POST_POWER block of the
 * current configuration.  These register writes target DSP/clock state that
 * only latches once the Class-D/DSP power-up sequence has run, so this is
 * called from the power-up path after tas2557_startup_sequence(), not during the
 * (powered-down) configuration load.  current_config is kept in lock-step
 * across every devs[] entry by tas2557_set_program(), so devs[0] is read
 * back as the group's representative value.
 */
static int tas2557_load_config_post_power(struct tas2557_priv *tas2557)
{
	struct tas2557_config *config;

	if (!tas2557->fw || tas2557->devs[0].current_config >= tas2557->fw->num_configs)
		return 0;

	config = &tas2557->fw->configs[tas2557->devs[0].current_config];

	return tas2557_load_fw_data_type(tas2557, &config->data,
					 TAS2557_BLOCK_CFG_POST_POWER);
}

/*
 * tas2557_load_pll - apply a firmware PLL block to every chip in the group.
 * PLL configuration is device-independent (each chip clocks itself off the
 * same shared reference), so this is loaded on every devs[] entry.
 */
static int tas2557_load_pll(struct tas2557_priv *tas2557, unsigned int pll_idx)
{
	unsigned int chn;
	int ret;

	if (!tas2557->fw || pll_idx >= tas2557->fw->num_plls)
		return -EINVAL;

	dev_dbg(tas2557->tas_priv->dev, "loading PLL[%u]: %s\n",
		pll_idx, tas2557->fw->plls[pll_idx].name);

	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++) {
		ret = tas2557_load_block(tas2557,
					 &tas2557->fw->plls[pll_idx].block, chn);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
 * tas2557_find_config - first config of @prog_idx for @rate (0 = any rate),
 * or -EINVAL if the firmware has none.
 */
static int tas2557_find_config(const struct tas2557_firmware *fw,
			       unsigned int prog_idx, unsigned int rate)
{
	unsigned int i;

	for (i = 0; i < fw->num_configs; i++) {
		if (fw->configs[i].program != prog_idx)
			continue;
		if (rate == 0 || rate == fw->configs[i].sample_rate)
			return i;
	}

	return -EINVAL;
}

/*
 * tas2557_set_program - reset the device group and load a firmware program
 * and config
 *
 * Hardware reset, software reset, default registers and IRQ config on every
 * chip, then the program blocks, the config's PLL block and the config
 * blocks.  The PLL is configured only by the firmware PLL block.  The group
 * is not powered up: callers run the startup sequence afterwards.  A
 * powered group must be shut down by the caller first.
 *
 * Caller must hold tas_priv->codec_lock.
 *
 * @config_idx: -1 to select the config for the current sample_rate; while
 *	that is still 0, the first config of the program
 */
static int tas2557_set_program(struct tas2557_priv *tas2557,
			       unsigned int prog_idx, int config_idx)
{
	struct tas2557_firmware *fw = tas2557->fw;
	struct tas2557_program *program;
	struct tas2557_config  *config;
	unsigned int chn;
	int ret;

	lockdep_assert_held(&tas2557->tas_priv->codec_lock);

	if (!fw || !fw->programs || !fw->configs) {
		dev_err(tas2557->tas_priv->dev, "firmware not loaded, cannot set program\n");
		return -EINVAL;
	}

	if (prog_idx >= fw->num_programs) {
		dev_err(tas2557->tas_priv->dev, "program %u out of range (%u total)\n",
			prog_idx, fw->num_programs);
		return -EINVAL;
	}

	if (config_idx < 0) {
		config_idx = tas2557_find_config(fw, prog_idx, tas2557->sample_rate);
		if (config_idx < 0) {
			dev_err(tas2557->tas_priv->dev,
				"no config for program %u at %u Hz\n",
				prog_idx, tas2557->sample_rate);
			return -EINVAL;
		}
	}

	if ((unsigned int)config_idx >= fw->num_configs) {
		dev_err(tas2557->tas_priv->dev, "config %d out of range (%u total)\n",
			config_idx, fw->num_configs);
		return -EINVAL;
	}

	config = &fw->configs[config_idx];
	if (config->program != prog_idx) {
		dev_err(tas2557->tas_priv->dev,
			"config %u belongs to program %u, not %u\n",
			config_idx, config->program, prog_idx);
		return -EINVAL;
	}

	program = &fw->programs[prog_idx];

	/* Hard + soft reset (every chip), then baseline register setup */
	tas2557_hw_reset(tas2557);

	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++) {
		ret = tas2557_dev_write(tas2557, chn, TAS2557_SW_RESET_REG, 0x01);
		if (ret < 0)
			return ret;
	}
	usleep_range(1000, 2000);

	ret = tas2557_load_data_all(tas2557, tas2557_default_data);
	if (ret < 0)
		return ret;

	/* Restore IRQ configuration lost by the reset */
	ret = tas2557_load_data_all(tas2557, tas2557_irq_config);
	if (ret < 0)
		dev_warn(tas2557->tas_priv->dev, "IRQ config reload failed: %d\n", ret);

	/*
	 * Load DSP program blocks.  Which chip(s) each block targets is
	 * decided purely by the block's own type (PGM_ALL/PGM_DEV_A/
	 * PGM_DEV_B) via tas2557_block_chn_range(); DEV_A/DEV_B tuning maps
	 * onto devs[0]/devs[1], i.e. onto the chip order of the DT `reg`
	 * array, not any runtime TDM slot selection.
	 */
	dev_dbg(tas2557->tas_priv->dev, "loading program %u (%s)\n",
		prog_idx, program->name);
	ret = tas2557_load_fw_data(tas2557, &program->data);
	if (ret < 0)
		return ret;

	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++)
		tas2557->devs[chn].current_program = prog_idx;

	/* Load the PLL configuration of this config (index checked at parse) */
	ret = tas2557_load_pll(tas2557, config->pll);
	if (ret < 0)
		return ret;

	/* Load config blocks (coefficients, sample-rate-specific settings) */
	dev_dbg(tas2557->tas_priv->dev,
		"loading config %u (%s) rate=%u Hz PLL[%u]\n",
		config_idx, config->name, config->sample_rate, config->pll);
	ret = tas2557_load_config_data(tas2557, &config->data);
	if (ret < 0)
		return ret;

	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++)
		tas2557->devs[chn].current_config = config_idx;

	tas2557->prog_valid = true;

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
 * tas2557_build_rate_list - collect the distinct sample rates of the
 * current program's configs for the DAI rate constraint.  Caller holds
 * tas_priv->codec_lock.
 */
static int tas2557_build_rate_list(struct tas2557_priv *tas2557)
{
	const struct tas2557_firmware *fw = tas2557->fw;
	unsigned int prog_idx = tas2557->devs[0].current_program;
	unsigned int *rates, num = 0, i, j;

	rates = kmalloc_array(fw->num_configs, sizeof(*rates), GFP_KERNEL);
	if (!rates)
		return -ENOMEM;

	for (i = 0; i < fw->num_configs; i++) {
		unsigned int rate = fw->configs[i].sample_rate;

		if (fw->configs[i].program != prog_idx || !rate)
			continue;

		for (j = 0; j < num; j++)
			if (rates[j] == rate)
				break;
		if (j == num)
			rates[num++] = rate;
	}

	kfree(tas2557->rates);
	tas2557->rates = rates;
	tas2557->rate_list.list = rates;
	tas2557->rate_list.count = num;

	return 0;
}

/*
 * tas2557_fw_ready - firmware load callback
 *
 * Called from request_firmware_nowait().  Parses the firmware and, if it
 * matches the configured chip count, installs it, builds the rate list and
 * loads program 0 so the DSP is ready for the first power-up.  Every path
 * reaches the completion at the end, so tas2557_fw_teardown() and DAPM can
 * always wait for this callback to finish.
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
		dev_err(tas2557->tas_priv->dev,
			"firmware '%s' not found, the amplifier cannot work without it\n",
			tas2557->fw_name);
		release_firmware(fw_entry);
		goto done;
	}

	dev_dbg(tas2557->tas_priv->dev, "firmware received, size=%zu\n", fw_entry->size);

	fw = fw_parse(tas2557, fw_entry->data, fw_entry->size);
	release_firmware(fw_entry);

	if (IS_ERR(fw)) {
		dev_err(tas2557->tas_priv->dev,
			"firmware '%s' parse error %ld, it may be corrupt\n",
			tas2557->fw_name, PTR_ERR(fw));
		fw = NULL;
		goto done;
	}

	/*
	 * The firmware's device field is the number of chips it was built
	 * for; a mismatch with the DT group size means the wrong file.
	 */
	if ((fw->device == TASDEVICE_DSP_TAS_2557_MONO &&
	     tas2557->tas_priv->ndev != 1) ||
	    (fw->device == TASDEVICE_DSP_TAS_2557_DUAL_MONO &&
	     tas2557->tas_priv->ndev != 2)) {
		dev_err(tas2557->tas_priv->dev,
			"firmware '%s' is for %s but %u chip(s) are configured\n",
			tas2557->fw_name,
			fw->device == TASDEVICE_DSP_TAS_2557_MONO ? "mono" : "stereo",
			tas2557->tas_priv->ndev);
		tas2557_fw_free(fw);
		goto done;
	}

	mutex_lock(&tas2557->tas_priv->codec_lock);

	tas2557_fw_free(tas2557->fw);
	tas2557->fw = fw;
	tas2557->prog_valid = false;

	if (tas2557_build_rate_list(tas2557) < 0)
		dev_warn(tas2557->tas_priv->dev,
			 "no memory for the rate list, rates are not constrained\n");

	ret = tas2557_set_program(tas2557, 0, -1);
	if (ret < 0)
		dev_err(tas2557->tas_priv->dev, "initial program load failed: %d\n", ret);
	else
		dev_info(tas2557->tas_priv->dev,
			 "%s ready: fw '%s' ver=0x%x drv=0x%x (%u prog, %u cfg)\n",
			 tas2557_pg_name(tas2557->devs[0].pg_id), tas2557->fw_name,
			 fw->fw_version, fw->driver_version,
			 fw->num_programs, fw->num_configs);

	mutex_unlock(&tas2557->tas_priv->codec_lock);

done:
	complete_all(&tas2557->fw_done);
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

	mutex_lock(&tas2557->tas_priv->codec_lock);
	tas2557_fw_free(tas2557->fw);
	tas2557->fw = NULL;
	kfree(tas2557->rates);
	tas2557->rates = NULL;
	mutex_unlock(&tas2557->tas_priv->codec_lock);
}

/*
 * tas2557_hw_reset - assert and release every configured reset line
 * together.  A lone reset-gpios entry is replicated into every slot by
 * tas2557_parse_dt().  Invalidates the loaded program and comlib's cached
 * book numbers; without a reset GPIO only that bookkeeping is done.
 */
static void tas2557_hw_reset(struct tas2557_priv *tas2557)
{
	unsigned int chn;
	bool have_reset = false;

	tas2557->prog_valid = false;

	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++) {
		if (tas2557->reset[chn]) {
			have_reset = true;
			/* Assert reset (active-low: logical 1 = reset asserted) */
			gpiod_set_value_cansleep(tas2557->reset[chn], 1);
		}
	}

	if (have_reset) {
		usleep_range(5000, 6000);
		for (chn = 0; chn < tas2557->tas_priv->ndev; chn++)
			if (tas2557->reset[chn])
				gpiod_set_value_cansleep(tas2557->reset[chn], 0);
		usleep_range(10000, 11000);
	}

	/* The reset clears the selected book; force comlib to reselect it */
	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++)
		tas2557->tas_priv->tasdevice[chn].cur_book = -1;
}

/*
 * tas2557_irq_arm()/tas2557_irq_disarm() - enable/disable every configured
 * fault IRQ from the power path (interrupts, minItems 1 maxItems 2,
 * one entry may apply to every chip; every distinct line is armed and
 * disarmed once, together, since the whole group is powered up/down together).
 * Called with tas_priv->codec_lock NOT held: enable_irq()/disable_irq() must not
 * run while the threaded handler could be blocked waiting for
 * tas_priv->codec_lock, or a self-deadlock results (disable_irq() waits for the
 * handler thread to finish, which is waiting on the lock the caller is
 * holding).
 */
static void tas2557_irq_arm(struct tas2557_priv *tas2557)
{
	unsigned int chn;

	if (tas2557->irq_enabled)
		return;
	tas2557->irq_enabled = true;
	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++) {
		if (chn > 0 && tas2557->irq[chn] == tas2557->irq[chn - 1])
			continue;
		if (tas2557->irq[chn] > 0)
			enable_irq(tas2557->irq[chn]);
	}
}

static void tas2557_irq_disarm(struct tas2557_priv *tas2557)
{
	unsigned int chn;

	if (!tas2557->irq_enabled)
		return;
	tas2557->irq_enabled = false;
	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++) {
		if (chn > 0 && tas2557->irq[chn] == tas2557->irq[chn - 1])
			continue;
		if (tas2557->irq[chn] > 0)
			disable_irq(tas2557->irq[chn]);
	}
}

/*
 * tas2557_irq_handler - threaded fault IRQ handler
 *
 * GPIO4 is programmed as the INT1 output (see tas2557_irq_config) on every
 * chip; every configured host IRQ line shares this same handler with
 * dev_id = tas2557, so a fault on any chip's INT line diagnoses and
 * recovers the whole group the same way.  FLAGS_1/FLAGS_2/POWER_UP_FLAG
 * are read back from each chip in turn: any single chip reporting a fault
 * triggers failsafe recovery for the whole group.
 */
static irqreturn_t tas2557_irq_handler(int irq, void *data)
{
	struct tas2557_priv *tas2557 = data;
	unsigned int chn;
	bool fault = false;
	int ret = 0;

	mutex_lock(&tas2557->tas_priv->codec_lock);

	if (!tas2557->fw || !tas2557->powered) {
		mutex_unlock(&tas2557->tas_priv->codec_lock);
		return IRQ_NONE;
	}

	/* Gate the chips' INT output while diagnosing the fault */
	tas2557_write_all(tas2557, TAS2557_GPIO4_PIN_REG, 0x00);

	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++) {
		unsigned int int1 = 0, int2 = 0, pwr_flag = 0;

		ret = tas2557_dev_read(tas2557, chn, TAS2557_FLAGS_1, &int1);
		if (ret >= 0)
			ret = tas2557_dev_read(tas2557, chn, TAS2557_FLAGS_2, &int2);

		if (ret < 0) {
			dev_err(tas2557->tas_priv->dev, "IRQ: chn %u failed to read FLAGS\n", chn);
			fault = true;
			break;
		}

		if ((int1 & 0xfc) || (int2 & 0x0c)) {
			/*
			 * Clock faults while muted are expected: the host is
			 * allowed to stop the I2S clocks around stream stop
			 * while the amplifier is still powered.  Nothing to
			 * recover from.
			 */
			if (tas2557->dac_muted && !(int1 & 0xd8)) {
				dev_dbg(tas2557->tas_priv->dev,
					"IRQ: chn %u clock stop while muted (FLAGS_1=0x%02x FLAGS_2=0x%02x)\n",
					chn, int1, int2);
				continue;
			}
			dev_err(tas2557->tas_priv->dev, "IRQ: chn %u FLAGS_1=0x%02x FLAGS_2=0x%02x\n",
				chn, int1, int2);
			fault = true;
			break;
		}

		if (!tas2557->dac_muted) {
			ret = tas2557_dev_read(tas2557, chn, TAS2557_POWER_UP_FLAG_REG,
					       &pwr_flag);
			if (ret < 0) {
				fault = true;
				break;
			}

			if ((pwr_flag & 0xc0) != 0xc0) {
				dev_err(tas2557->tas_priv->dev, "IRQ: chn %u power-up flag=0x%02x\n",
					chn, pwr_flag);
				fault = true;
				break;
			}
		}
	}

	if (!fault) {
		/* No fault (or expected clock stop): re-enable the INT output */
		tas2557_write_all(tas2557, TAS2557_GPIO4_PIN_REG, 0x07);
		mutex_unlock(&tas2557->tas_priv->codec_lock);
		return IRQ_HANDLED;
	}

	if (tas2557_failsafe_recovery(tas2557) == 0)
		tas2557_write_all(tas2557, TAS2557_GPIO4_PIN_REG, 0x07);

	mutex_unlock(&tas2557->tas_priv->codec_lock);
	return IRQ_HANDLED;
}

/*
 * tas2557_clk_err_detect - gate the chips' clock error/halt detection
 *
 * The host is allowed to stop the I2S clocks whenever no audio is
 * playing (amplifier muted), so detection is armed only between unmute
 * and mute; otherwise a normal stream stop looks like a clock-halt
 * fault.  This matches the TI reference sequences, which disable
 * detection as the first step of the shutdown sequence.
 */
static int tas2557_clk_err_detect(struct tas2557_priv *tas2557, bool enable)
{
	return tas2557_write_all(tas2557, TAS2557_CLK_ERR_CTRL,
				 enable ? 0x2b : 0x00);
}

/*
 * tas2557_apply_mute_state - program the cached mute state into a freshly
 * powered group.
 *
 * With the DAC unmuted, unmute (I/V sense are unmuted with it, as the DSP
 * needs the IV feedback while the amplifier plays) and arm clock error
 * detection.  With the DAC muted the host may not be clocking the bus, so
 * keep detection off.  tas2557_mute_stream() arms detection when the DAC
 * is unmuted later.
 */
static int tas2557_apply_mute_state(struct tas2557_priv *tas2557)
{
	int ret;

	if (!tas2557->dac_muted) {
		ret = tas2557_load_data_all(tas2557, tas2557_unmute_data);
		if (ret >= 0)
			ret = tas2557_clk_err_detect(tas2557, true);
	} else {
		ret = tas2557_clk_err_detect(tas2557, false);
	}

	return ret;
}

/*
 * tas2557_failsafe_recovery - re-initialise the device group after a fault IRQ
 *
 * Caller must hold tas_priv->codec_lock (called from tas2557_irq_handler()).
 * Capped at 5 attempts so a permanently faulty amplifier does not spin
 * the IRQ thread forever; the group is recovered together, so restart_count
 * is incremented on every chip and devs[0] is read back.
 */
static int tas2557_failsafe_recovery(struct tas2557_priv *tas2557)
{
	unsigned int chn;
	int ret;

	lockdep_assert_held(&tas2557->tas_priv->codec_lock);

	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++)
		tas2557->devs[chn].restart_count++;

	dev_info(tas2557->tas_priv->dev, "failsafe recovery attempt %u\n",
		 tas2557->devs[0].restart_count);

	if (tas2557->devs[0].restart_count > 5) {
		dev_err(tas2557->tas_priv->dev, "too many recovery attempts, giving up\n");
		/* The handler zeroed INT1 routing; force a reload at next power-up */
		tas2557->prog_valid = false;
		return -EIO;
	}

	/*
	 * tas2557_set_program() does the hardware reset, the software
	 * reset and the defaults/IRQ config reload.  The faulted chips are
	 * reset directly, without a graceful shutdown.
	 */
	ret = tas2557_set_program(tas2557, tas2557->devs[0].current_program,
				  (int)tas2557->devs[0].current_config);
	if (ret < 0)
		return ret;

	if (tas2557->powered) {
		ret = tas2557_startup_sequence(tas2557);
		if (ret < 0)
			return ret;

		ret = tas2557_apply_runtime_state(tas2557);
		if (ret < 0)
			return ret;

		ret = tas2557_load_config_post_power(tas2557);
		if (ret < 0)
			return ret;

		ret = tas2557_apply_mute_state(tas2557);
		if (ret < 0)
			return ret;
	}

	dev_info(tas2557->tas_priv->dev, "failsafe recovery succeeded\n");
	return 0;
}

static void tas2557_check_pll_lock(struct tas2557_priv *tas2557)
{
	unsigned int chn;

	/*
	 * The PLL needs a few ms to lock after the I2S bit clock starts
	 * flowing.  Poll POWER_UP_FLAG rather than sampling it once, so we
	 * don't emit a spurious "not locked" warning during clock ramp-up.
	 */
	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++) {
		unsigned int flag = 0, f1 = 0, f2 = 0;
		int i;

		for (i = 0; i < 20; i++) {
			tas2557_dev_read(tas2557, chn, TAS2557_POWER_UP_FLAG_REG, &flag);
			if ((flag & 0xc0) == 0xc0)
				break;
			usleep_range(2000, 2500);
		}

		tas2557_dev_read(tas2557, chn, TAS2557_FLAGS_1, &f1);
		tas2557_dev_read(tas2557, chn, TAS2557_FLAGS_2, &f2);

		if ((flag & 0xc0) == 0xc0)
			dev_dbg(tas2557->tas_priv->dev,
				"chn %u: PLL locked, power-up OK (FLAG=0x%02x F1=0x%02x F2=0x%02x)\n",
				chn, flag, f1, f2);
		else
			dev_warn(tas2557->tas_priv->dev,
				 "chn %u: PLL or clock may not have locked (FLAG=0x%02x F1=0x%02x F2=0x%02x)\n",
				 chn, flag, f1, f2);

		if (f1 & 0x04)
			dev_warn(tas2557->tas_priv->dev, "chn %u: clock error detected (FLAGS_1=0x%02x)\n",
				 chn, f1);
	}
}

/*
 * Power-up sequence, see tas2557_power_up_locked():
 *   1. tas2557_set_program() - only if the chips no longer hold the needed
 *      program/config: reset, defaults, IRQ config, firmware program, PLL
 *      and config blocks
 *   2. tas2557_startup_sequence() - GPIO clocks, Class-D, Boost, DSP
 *   3. DAC gain, ASI word length/format and slot offset
 *   4. CFG_POST_POWER firmware block
 *   5. Cached mute state
 *   6. POWER_UP_FLAG/FLAGS check for PLL lock and clock errors
 *
 * tas2557_hw_params() runs it again while powered for a live sample rate
 * change, which always reloads the program and config.
 */

/*
 * tas2557_apply_runtime_state - program the cached ALSA/DAI state: DAC
 * gain, ASI word length/format and the RX data-slot offset of the ASI
 * selected by ti,asi-port.  Run after every startup sequence and after a
 * reset.  Caller holds tas_priv->codec_lock.
 */
static int tas2557_apply_runtime_state(struct tas2557_priv *tas2557)
{
	unsigned int chn;
	int ret;

	lockdep_assert_held(&tas2557->tas_priv->codec_lock);

	ret = tas2557_update_bits_all(tas2557, TAS2557_SPK_CTRL_REG,
				      TAS2557_DAC_GAIN_MASK,
				      tas2557->dac_gain << TAS2557_DAC_GAIN_SHIFT);
	if (ret < 0)
		return ret;

	/* Word length is always valid (seeded at probe); format only if set */
	ret = tas2557_asi_fmt_update_all(tas2557, TAS2557_WORDLENGTH_MASK,
					 tas2557->wordlength);
	if (ret < 0)
		return ret;

	if (tas2557->dai_fmt >= 0) {
		ret = tas2557_asi_fmt_update_all(tas2557, TAS2557_FORMAT_MASK,
						 tas2557->dai_fmt);
		if (ret < 0)
			return ret;
	}

	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++) {
		ret = tas2557_write_rx_offset(tas2557, chn);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
 * tas2557_power_up_locked - power up the amplifier group with @prog_idx at
 * the current tas2557->sample_rate, loading the program and config first
 * when the chips do not hold them.
 *
 * The program is reloaded if the loaded state was invalidated, a different
 * config is needed for the rate, or the group is already powered (live rate
 * change from hw_params(); it is shut down first).
 *
 * Caller must hold tas_priv->codec_lock and must have already verified that
 * firmware is loaded.
 */
static int tas2557_power_up_locked(struct tas2557_priv *tas2557,
				   unsigned int prog_idx)
{
	int cfg_idx, ret;

	lockdep_assert_held(&tas2557->tas_priv->codec_lock);

	cfg_idx = tas2557_find_config(tas2557->fw, prog_idx, tas2557->sample_rate);
	if (cfg_idx < 0) {
		dev_err(tas2557->tas_priv->dev, "no config for program %u at %u Hz\n",
			prog_idx, tas2557->sample_rate);
		return cfg_idx;
	}

	if (tas2557->powered || !tas2557->prog_valid ||
	    tas2557->devs[0].current_program != prog_idx ||
	    tas2557->devs[0].current_config != cfg_idx) {
		if (tas2557->powered) {
			ret = tas2557_shutdown_sequence(tas2557);
			if (ret < 0)
				return ret;
			/* Callers set powered again on success */
			tas2557->powered = false;
			tas2557->dac_muted = true;
		}

		ret = tas2557_set_program(tas2557, prog_idx, cfg_idx);
		if (ret < 0) {
			dev_err(tas2557->tas_priv->dev,
				"firmware program load failed on power-up: %d\n",
				ret);
			return ret;
		}
	}

	/* Power up Class-D + Boost + selected-ASI clocks on every chip */
	ret = tas2557_startup_sequence(tas2557);
	if (ret < 0) {
		dev_err(tas2557->tas_priv->dev, "startup sequence failed: %d\n",
			ret);
		goto err_shutdown;
	}

	ret = tas2557_apply_runtime_state(tas2557);
	if (ret < 0)
		goto err_shutdown;

	/* DSP post-power coefficients need active clocks to latch */
	ret = tas2557_load_config_post_power(tas2557);
	if (ret < 0) {
		dev_err(tas2557->tas_priv->dev,
			"post-power config load failed: %d\n", ret);
		goto err_shutdown;
	}

	/*
	 * On DPCM systems mute_stream(0) for a back-end DAI can run before
	 * the DAPM widgets power up, so the unmute may already be recorded
	 * while the device is still off.
	 */
	ret = tas2557_apply_mute_state(tas2557);
	if (ret < 0)
		goto err_shutdown;

	/* Verify PLL lock and clock health on every chip */
	tas2557_check_pll_lock(tas2557);

	return 0;

err_shutdown:
	/* Do not leave the chips powered while powered == false */
	tas2557_shutdown_sequence(tas2557);
	tas2557->powered = false;
	tas2557->dac_muted = true;
	return ret;
}

/*
 * tas2557_enable - power the amplifier group up or down
 *
 * Caller must hold tas_priv->codec_lock.  Does not touch the fault IRQ or the
 * mute state; see tas2557_classd_event() and tas2557_mute_stream().
 */
static int tas2557_enable(struct tas2557_priv *tas2557, bool enable)
{
	unsigned int chn;
	int ret = 0;

	lockdep_assert_held(&tas2557->tas_priv->codec_lock);

	if (enable && !tas2557->powered) {
		if (!tas2557->fw) {
			dev_warn(tas2557->tas_priv->dev,
				 "firmware not loaded, cannot produce audio; install %s\n",
				 tas2557->fw_name[0] ? tas2557->fw_name
						     : TAS2557_FW_NAME);
			return -ENODEV;
		}

		ret = tas2557_power_up_locked(tas2557, tas2557->devs[0].current_program);
		if (ret < 0)
			return ret;

		tas2557->powered = true;
		for (chn = 0; chn < tas2557->tas_priv->ndev; chn++)
			tas2557->devs[chn].restart_count = 0;

		dev_dbg(tas2557->tas_priv->dev, "amplifier group powered on (prog=%u cfg=%u)\n",
			tas2557->devs[0].current_program, tas2557->devs[0].current_config);

	} else if (!enable && tas2557->powered) {
		ret = tas2557_shutdown_sequence(tas2557);
		if (ret < 0)
			dev_err(tas2557->tas_priv->dev, "shutdown failed: %d\n", ret);

		tas2557->powered = false;
		tas2557->dac_muted = true;

		dev_dbg(tas2557->tas_priv->dev, "amplifier group powered off\n");
	}

	return ret;
}

/* =========================================================================
 * ASoC DAI operations
 * =========================================================================
 */

/*
 * tas2557_wait_fw - wait, bounded, for the asynchronous firmware load to
 * finish.  Must be called without tas_priv->codec_lock held:
 * tas2557_fw_ready() takes it before it completes fw_done.
 */
static void tas2557_wait_fw(struct tas2557_priv *tas2557)
{
	if (tas2557->fw_requested &&
	    !wait_for_completion_timeout(&tas2557->fw_done,
					 msecs_to_jiffies(5000)))
		dev_warn(tas2557->tas_priv->dev, "firmware load timed out\n");
}

/*
 * The firmware configs have fixed sample rates, so once the firmware is
 * loaded restrict the stream to the rates of the current program.  Without
 * firmware the stream opens unconstrained and power-up reports the error.
 */
static int tas2557_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(dai->component);

	tas2557_wait_fw(tas2557);

	guard(mutex)(&tas2557->tas_priv->codec_lock);

	if (!tas2557->fw || !tas2557->rate_list.count)
		return 0;

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  &tas2557->rate_list);
}

static int tas2557_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);
	unsigned int rate = params_rate(params);
	unsigned int chn, wordlength;
	bool rate_changed, fmt_changed, live, remute;
	int ret;

	guard(mutex)(&tas2557->tas_priv->codec_lock);

	dev_dbg(tas2557->tas_priv->dev, "hw_params: rate=%u width=%d\n",
		rate, params_physical_width(params));

	/*
	 * Firmware configs have fixed sample rates.  Reject a rate the
	 * program has no config for before changing any state; without
	 * firmware the check is left to power-up.
	 */
	if (tas2557->fw &&
	    tas2557_find_config(tas2557->fw, tas2557->devs[0].current_program,
				rate) < 0) {
		dev_err(tas2557->tas_priv->dev,
			"no firmware config for program %u at %u Hz\n",
			tas2557->devs[0].current_program, rate);
		return -EINVAL;
	}

	/* Validate the ASI word length before committing any cached state */
	switch (params_width(params)) {
	case 16:
		wordlength = TAS2557_WORDLENGTH_16BIT;
		break;
	case 20:
		wordlength = TAS2557_WORDLENGTH_20BIT;
		break;
	case 24:
		wordlength = TAS2557_WORDLENGTH_24BIT;
		break;
	case 32:
		wordlength = TAS2557_WORDLENGTH_32BIT;
		break;
	default:
		dev_err(tas2557->tas_priv->dev, "unsupported sample width %d\n",
			params_width(params));
		return -EINVAL;
	}

	/*
	 * Without set_tdm_slot() the slot offset is 0 for every chip (the
	 * DSP_A BCLK is added when it is written); an explicit
	 * set_tdm_slot() takes precedence and leaves asi_offset alone.
	 */
	if (!tas2557->tdm_configured) {
		for (chn = 0; chn < tas2557->tas_priv->ndev; chn++)
			tas2557->devs[chn].asi_offset = 0;
	}

	/* A lost program (failed reload, recovery give-up) forces a reload too */
	rate_changed = tas2557->powered &&
		       (rate != tas2557->sample_rate || !tas2557->prog_valid);
	fmt_changed = tas2557->powered && wordlength != tas2557->wordlength;
	live = rate_changed || fmt_changed;

	/*
	 * If the group is powered and the rate or word length changed, apply
	 * it now instead of at the next power-up.  A rate change needs the
	 * new config's PLL block and goes through tas2557_power_up_locked();
	 * a word-length change is written to the format registers under
	 * mute.
	 */
	remute = live && fmt_changed && !rate_changed && !tas2557->dac_muted;
	if (remute) {
		ret = tas2557_update_bits_all(tas2557, TAS2557_SOFT_MUTE_REG,
					      TAS2557_PDM_SOFT_MUTE,
					      TAS2557_PDM_SOFT_MUTE);
		if (ret >= 0) {
			usleep_range(10000, 11000);
			ret = tas2557_write_all(tas2557, TAS2557_MUTE_REG,
						TAS2557_MUTE_ISNS |
						TAS2557_MUTE_VSNS);
		}
		if (ret < 0)
			return ret;
	}

	/* Program the selected ASI's data-slot offset on every chip */
	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++) {
		ret = tas2557_write_rx_offset(tas2557, chn);
		if (ret < 0) {
			dev_err(tas2557->tas_priv->dev, "chn %u: ASI offset write failed: %d\n",
				chn, ret);
			return ret;
		}
	}

	if (live && fmt_changed && !rate_changed) {
		ret = tas2557_asi_fmt_update_all(tas2557, TAS2557_WORDLENGTH_MASK,
						 wordlength);
		if (ret < 0)
			return ret;
	}

	/* Commit cached stream state now the hardware writes have succeeded */
	tas2557->sample_rate = rate;
	tas2557->wordlength = wordlength;

	if (rate_changed) {
		ret = tas2557_power_up_locked(tas2557, tas2557->devs[0].current_program);
		if (ret < 0) {
			/*
			 * The chips are down but ClassD is still on: keep
			 * powered so that a retry reloads the program and
			 * PRE_PMD still runs the shutdown.
			 */
			tas2557->powered = true;
			tas2557->dac_muted = true;
			tas2557->prog_valid = false;
			return ret;
		}
		tas2557->powered = true;
	} else if (remute) {
		ret = tas2557_write_all(tas2557, TAS2557_MUTE_REG, 0);
		if (ret >= 0)
			ret = tas2557_update_bits_all(tas2557,
						      TAS2557_SOFT_MUTE_REG,
						      TAS2557_PDM_SOFT_MUTE, 0);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int tas2557_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);
	unsigned int asi_fmt = 0, asi_delay = 0, chn;
	int ret;

	/* The chip is always the clock consumer and has no inversion support */
	if ((fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) != SND_SOC_DAIFMT_CBC_CFC ||
	    (fmt & SND_SOC_DAIFMT_INV_MASK) != SND_SOC_DAIFMT_NB_NF) {
		dev_err(tas2557->tas_priv->dev, "unsupported DAI format 0x%x\n", fmt);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		asi_fmt = TAS2557_FORMAT_I2S;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		/* The DSP format at OFFSET1 = 0 is DSP_B; DSP_A is 1 BCLK later */
		asi_delay = 1;
		fallthrough;
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
		dev_err(tas2557->tas_priv->dev, "unsupported DAI format 0x%x\n", fmt);
		return -EINVAL;
	}

	guard(mutex)(&tas2557->tas_priv->codec_lock);

	tas2557->dai_fmt = asi_fmt;
	tas2557->asi_delay = asi_delay;

	ret = tas2557_asi_fmt_update_all(tas2557, TAS2557_FORMAT_MASK, asi_fmt);
	if (ret < 0)
		return ret;

	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++) {
		ret = tas2557_write_rx_offset(tas2557, chn);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int tas2557_set_tdm_slot(struct snd_soc_dai *dai,
				unsigned int tx_mask, unsigned int rx_mask,
				int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);
	unsigned int ndev = tas2557->tas_priv->ndev;
	unsigned int offsets[TAS2557_MAX_DEV] = { };
	bool per_chip_slots = false;
	unsigned int mask = rx_mask;
	unsigned int chn;
	int ret = 0;

	switch (slot_width) {
	case 0:
	case 16:
	case 24:
	case 32:
		break;
	default:
		dev_err(tas2557->tas_priv->dev, "unsupported TDM slot width %d\n",
			slot_width);
		return -EINVAL;
	}

	/*
	 * With at least two slots in the mask and at least as many as
	 * chips, chip n takes the n-th slot; otherwise every chip takes the
	 * first one.  Only the slots actually used are checked.
	 */
	if (rx_mask) {
		per_chip_slots = hweight32(rx_mask) >= 2 &&
				 hweight32(rx_mask) >= ndev;

		for (chn = 0; chn < ndev; chn++) {
			unsigned int slot = __ffs(mask);

			if (slot && !slot_width) {
				dev_err(tas2557->tas_priv->dev,
					"TDM slot %u needs a slot width\n", slot);
				return -EINVAL;
			}
			if (per_chip_slots)
				mask &= mask - 1;
			offsets[chn] = slot * slot_width;
		}
	}

	guard(mutex)(&tas2557->tas_priv->codec_lock);

	/* The offset plus the DSP_A BCLK must fit the 8-bit OFFSET1 register */
	for (chn = 0; chn < ndev; chn++)
		if (offsets[chn] + tas2557->asi_delay > U8_MAX) {
			dev_err(tas2557->tas_priv->dev,
				"TDM slot offset %u out of range\n", offsets[chn]);
			return -EINVAL;
		}

	/*
	 * tx_mask is accepted but not programmed: DOUT slot placement is not
	 * handled by this driver.  Clearing both masks leaves TDM mode and
	 * restores the default offset.
	 */
	if (!rx_mask && tx_mask)
		return 0;

	tas2557->tdm_configured = !!rx_mask;

	for (chn = 0; chn < ndev; chn++) {
		tas2557->devs[chn].asi_offset = offsets[chn];

		ret = tas2557_write_rx_offset(tas2557, chn);
		if (ret < 0)
			return ret;
	}

	dev_dbg(tas2557->tas_priv->dev, "TDM: rx_mask=0x%x width=%d\n",
		rx_mask, slot_width);

	return 0;
}

static int tas2557_mute_stream(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	guard(mutex)(&tas2557->tas_priv->codec_lock);

	if (!tas2557->powered) {
		tas2557->dac_muted = mute;
		return 0;
	}

	if (mute) {
		/* Detection off first: the host is about to stop the clocks */
		ret = tas2557_clk_err_detect(tas2557, false);
		if (ret >= 0)
			ret = tas2557_update_bits_all(tas2557,
						      TAS2557_SOFT_MUTE_REG,
						      TAS2557_PDM_SOFT_MUTE,
						      TAS2557_PDM_SOFT_MUTE);
		if (ret >= 0) {
			usleep_range(10000, 11000);
			ret = tas2557_write_all(tas2557, TAS2557_MUTE_REG,
						TAS2557_MUTE_ISNS |
						TAS2557_MUTE_VSNS);
		}
		tas2557->dac_muted = mute;
	} else {
		/* Clocks are running now: unmute and arm error detection */
		tas2557->dac_muted = false;
		ret = tas2557_apply_mute_state(tas2557);
	}

	return ret;
}

static const struct snd_soc_dai_ops tas2557_dai_ops = {
	.startup       = tas2557_startup,
	.hw_params     = tas2557_hw_params,
	.set_fmt       = tas2557_set_dai_fmt,
	.set_tdm_slot  = tas2557_set_tdm_slot,
	.mute_stream   = tas2557_mute_stream,
	/*
	 * The DSP needs the IV sense feedback while the amplifier plays, so
	 * only the playback stream is muted.
	 */
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
	/*
	 * One firmware config (hence one rate) is active at a time, and
	 * playback and capture share the single ASI word length, so both
	 * substreams must agree on rate and sample width.
	 */
	.symmetric_rate = 1,
	.symmetric_sample_bits = 1,
};

/* =========================================================================
 * ALSA mixer controls
 * =========================================================================
 */
static DECLARE_TLV_DB_SCALE(tas2557_dac_tlv, 0, 100, 0);

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

	guard(mutex)(&tas2557->tas_priv->codec_lock);

	if (gain == tas2557->dac_gain)
		return 0;

	if (tas2557->powered) {
		ret = tas2557_update_bits_all(tas2557, TAS2557_SPK_CTRL_REG,
					      TAS2557_DAC_GAIN_MASK,
					      gain << TAS2557_DAC_GAIN_SHIFT);
		if (ret < 0)
			return ret;
	}

	/* Only commit the cached gain once the hardware write has succeeded */
	tas2557->dac_gain = gain;

	return 1;
}

static const struct snd_kcontrol_new tas2557_controls[] = {
	SOC_SINGLE_EXT_TLV("Speaker Playback Volume", SND_SOC_NOPM, 0,
			   TAS2557_DAC_GAIN_MAX, 0,
			   tas2557_volume_get, tas2557_volume_put,
			   tas2557_dac_tlv),
};

/* =========================================================================
 * DAPM topology
 *
 * Playback path: ASI (AIF_IN, stream "Playback") -> DAC -> ClassD -> OUT
 * Capture path:  ClassD -> ISENSE/VSENSE -> SENSE (AIF_OUT, "Capture")
 *
 * One AIF widget serves whichever ASI ti,asi-port selects; the pin and
 * format programming is done by the startup sequence, not by DAPM.
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
		/* Not under codec_lock, see tas2557_wait_fw() */
		tas2557_wait_fw(tas2557);

		mutex_lock(&tas2557->tas_priv->codec_lock);
		ret = tas2557_enable(tas2557, true);
		mutex_unlock(&tas2557->tas_priv->codec_lock);
		if (ret < 0)
			return ret;
		tas2557_irq_arm(tas2557);
		return 0;
	case SND_SOC_DAPM_PRE_PMD:
		tas2557_irq_disarm(tas2557);
		mutex_lock(&tas2557->tas_priv->codec_lock);
		ret = tas2557_enable(tas2557, false);
		mutex_unlock(&tas2557->tas_priv->codec_lock);
		return ret;
	default:
		return 0;
	}
}

static const struct snd_soc_dapm_widget tas2557_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("ASI", "Playback", 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_OUT_DRV_E("ClassD", SND_SOC_NOPM, 0, 0, NULL, 0,
			       tas2557_classd_event,
			       SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	/* IV sense is powered by the startup sequence, not by DAPM writes */
	SND_SOC_DAPM_ADC("ISENSE", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("VSENSE", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_OUTPUT("OUT"),
	SND_SOC_DAPM_AIF_OUT("SENSE", "Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route tas2557_dapm_routes[] = {
	{ "ASI",    NULL, "Playback" },
	{ "DAC",    NULL, "ASI" },
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
	const char *default_fw;
	unsigned int chn;
	int ret;

	/* Serialise against a pending tas2557_fw_ready() on a card re-bind */
	guard(mutex)(&tas2557->tas_priv->codec_lock);

	tas2557_hw_reset(tas2557);

	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++) {
		ret = tas2557_dev_write(tas2557, chn, TAS2557_SW_RESET_REG, 0x01);
		if (ret < 0) {
			dev_err(tas2557->tas_priv->dev, "chn %u: software reset failed: %d\n",
				chn, ret);
			return ret;
		}
	}
	usleep_range(1000, 2000);

	for (chn = 0; chn < tas2557->tas_priv->ndev; chn++) {
		unsigned int pg_id;

		ret = tas2557_dev_read(tas2557, chn, TAS2557_REV_PGID_REG, &pg_id);
		if (ret < 0) {
			dev_err(tas2557->tas_priv->dev, "chn %u: failed to read REV_PGID: %d\n",
				chn, ret);
			return ret;
		}
		tas2557->devs[chn].pg_id = pg_id;
	}

	/*
	 * Firmware selection is group-wide (one shared image); the PG
	 * version of devs[0] is used as the representative silicon
	 * revision, matching how current_program/current_config/pg_id are
	 * otherwise read back from devs[0] elsewhere in this driver.
	 */
	switch (tas2557->devs[0].pg_id) {
	case TAS2557_PG_VERSION_1P0:
		dev_dbg(tas2557->tas_priv->dev, "silicon: PG1.0 (0x%02x)\n",
			tas2557->devs[0].pg_id);
		default_fw = TAS2557_PG1P0_FW_NAME;
		break;
	case TAS2557_PG_VERSION_2P0:
		dev_dbg(tas2557->tas_priv->dev, "silicon: PG2.0 (0x%02x)\n",
			tas2557->devs[0].pg_id);
		default_fw = TAS2557_FW_NAME;
		break;
	case TAS2557_PG_VERSION_2P1:
		dev_dbg(tas2557->tas_priv->dev, "silicon: PG2.1 (0x%02x)\n",
			tas2557->devs[0].pg_id);
		default_fw = TAS2557_FW_NAME;
		break;
	default:
		dev_warn(tas2557->tas_priv->dev,
			 "unknown silicon version 0x%02x, assuming PG2.x fw\n",
			 tas2557->devs[0].pg_id);
		default_fw = TAS2557_FW_NAME;
		break;
	}

	/*
	 * A "firmware-name" DT property (read at i2c probe) overrides the
	 * PG-based default; dual-amp stereo boards load a different DSP
	 * blob (e.g. tas2557s_uCDSP.bin).
	 */
	if (!tas2557->fw_name[0])
		strscpy(tas2557->fw_name, default_fw, sizeof(tas2557->fw_name));

	ret = tas2557_load_data_all(tas2557, tas2557_default_data);
	if (ret < 0) {
		dev_err(tas2557->tas_priv->dev, "default data load failed: %d\n", ret);
		return ret;
	}

	ret = tas2557_load_data_all(tas2557, tas2557_irq_config);
	if (ret < 0)
		dev_warn(tas2557->tas_priv->dev, "IRQ config failed: %d\n", ret);

	/*
	 * Component rebind (e.g. after an unbind/bind cycle): firmware is
	 * already parsed and stored, so do not request it again.
	 */
	if (!tas2557->fw_requested) {
		reinit_completion(&tas2557->fw_done);
		ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_UEVENT,
					      tas2557->fw_name, tas2557->tas_priv->dev,
					      GFP_KERNEL, tas2557,
					      tas2557_fw_ready);
		if (ret) {
			return dev_err_probe(tas2557->tas_priv->dev, ret,
					     "failed to request fw '%s'\n",
					     tas2557->fw_name);
		}
		tas2557->fw_requested = true;
	}

	dev_dbg(tas2557->tas_priv->dev, "codec probed (%u chip(s)), requesting firmware '%s'\n",
		tas2557->tas_priv->ndev, tas2557->fw_name);
	return 0;
}

static int tas2557_suspend(struct snd_soc_component *component)
{
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);

	tas2557_irq_disarm(tas2557);

	guard(mutex)(&tas2557->tas_priv->codec_lock);
	if (!tas2557->powered)
		return 0;

	return tas2557_enable(tas2557, false);
}

static int tas2557_resume(struct snd_soc_component *component)
{
	struct tas2557_priv *tas2557 = snd_soc_component_get_drvdata(component);
	int ret;

	guard(mutex)(&tas2557->tas_priv->codec_lock);

	/* Also invalidates the loaded program, even without a reset GPIO */
	tas2557_hw_reset(tas2557);

	ret = tas2557_load_data_all(tas2557, tas2557_default_data);
	if (ret < 0)
		dev_err(tas2557->tas_priv->dev, "defaults reload on resume failed: %d\n",
			ret);
	else if (tas2557_load_data_all(tas2557, tas2557_irq_config) < 0)
		dev_warn(tas2557->tas_priv->dev, "IRQ config reload on resume failed\n");

	/*
	 * The group stays powered down until the next DAPM power-up, which
	 * reloads the program and applies both mute states.
	 */

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

/*
 * tas2557_parse_dt - populate tas_priv->tasdevice[]/ndev, the per-chip
 * reset/IRQ lines and ti,asi-port
 *
 * `reg` holds one I2C address per chip (up to TAS2557_MAX_DEV), read with
 * of_property_read_reg() as tasdevice_parse_dt() does, since
 * i2c_client::addr only reflects the first entry.  Without an OF node a
 * single chip at the i2c_client's address is used.
 *
 * A single reset-gpios/interrupts entry applies to every chip.
 */
static int tas2557_parse_dt(struct tas2557_priv *tas2557, struct i2c_client *client)
{
	struct tasdevice_priv *tas_priv = tas2557->tas_priv;
	struct device *dev = tas_priv->dev;
	struct device_node *np = dev->of_node;
	unsigned int i;
	int ndev = 0;
	int ret;

	if (np) {
		u64 addr;

		for (i = 0; i < TAS2557_MAX_DEV; i++) {
			if (of_property_read_reg(np, i, &addr, NULL))
				break;
			tas_priv->tasdevice[ndev++].dev_addr = addr;
		}
	}

	if (ndev == 0) {
		ndev = 1;
		tas_priv->tasdevice[0].dev_addr = client->addr;
	}

	tas_priv->ndev = ndev;

	/*
	 * Reserve every secondary address on the bus so no other
	 * driver can claim it; tasdevice_change_chn_book() keeps swapping
	 * client->addr to reach each chip, these dummies only reserve the
	 * i2c_client namespace.
	 */
	for (i = 1; i < tas_priv->ndev; i++) {
		struct i2c_client *dummy = devm_i2c_new_dummy_device(dev,
			client->adapter, tas_priv->tasdevice[i].dev_addr);
		if (IS_ERR(dummy))
			return dev_err_probe(dev, PTR_ERR(dummy),
					     "failed to reserve i2c addr 0x%02x\n",
					     tas_priv->tasdevice[i].dev_addr);
	}

	/*
	 * reset-gpios / interrupts, minItems 1 maxItems 2; a single
	 * entry applies to every chip.  Reset GPIOs are modeled active-low
	 * in DT: the logical value 0 (GPIOD_OUT_LOW) deasserts reset,
	 * releasing the device group.
	 */
	for (i = 0; i < tas_priv->ndev; i++) {
		tas2557->reset[i] = devm_gpiod_get_index_optional(dev, "reset", i,
								  GPIOD_OUT_LOW);
		if (IS_ERR(tas2557->reset[i]))
			return dev_err_probe(dev, PTR_ERR(tas2557->reset[i]),
					     "failed to get reset GPIO %u\n", i);
		if (!tas2557->reset[i] && i > 0)
			tas2557->reset[i] = tas2557->reset[0];

		tas2557->irq[i] = of_irq_get(np, i);
		if (tas2557->irq[i] == -EPROBE_DEFER)
			return dev_err_probe(dev, tas2557->irq[i],
					     "interrupt %u not ready\n", i);
		if (tas2557->irq[i] <= 0 && i > 0)
			tas2557->irq[i] = tas2557->irq[0];
		/* Index 0 falls back to the primary i2c_client IRQ */
		if (i == 0 && tas2557->irq[i] <= 0)
			tas2557->irq[i] = client->irq;
	}

	/* Which ASI's pins/dividers/format registers to program */
	tas2557->asi_port = TAS2557_ASI_PORT_DEFAULT;
	ret = of_property_read_u32(np, "ti,asi-port", &tas2557->asi_port);
	if (ret && ret != -EINVAL)
		return dev_err_probe(dev, ret, "failed to read ti,asi-port\n");
	if (tas2557->asi_port != TAS2557_ASI_PORT1 &&
	    tas2557->asi_port != TAS2557_ASI_PORT2)
		return dev_err_probe(dev, -EINVAL,
				     "ti,asi-port must be 1 or 2, got %u\n",
				     tas2557->asi_port);

	for (i = 0; i < tas_priv->ndev; i++) {
		tas2557->devs[i].pg_id = 0;
		tas2557->devs[i].current_program = 0;
		tas2557->devs[i].current_config = 0;
		tas2557->devs[i].restart_count = 0;
		tas2557->devs[i].asi_offset = 0;
	}

	return 0;
}

/*
 * Destroys codec_lock; must run after everything that can take it.
 * comlib leaves client->addr on the last chip it accessed; put the primary
 * address back so a later probe can reserve it again.
 */
static void tas2557_comlib_remove(void *data)
{
	struct tasdevice_priv *tas_priv = data;
	struct i2c_client *client = tas_priv->client;

	client->addr = tas_priv->tasdevice[0].dev_addr;
	tasdevice_remove(tas_priv);
}

static int tas2557_i2c_probe(struct i2c_client *client)
{
	static const char * const tas2557_supplies[] = {
		"vbat", "iovdd", "avdd", "dvdd"
	};
	struct device *dev = &client->dev;
	struct tas2557_priv *tas2557;
	struct tasdevice_priv *tas_priv;
	const char *fw_name;
	unsigned int i, j;
	int irq, ret;

	tas2557 = devm_kzalloc(dev, sizeof(*tas2557), GFP_KERNEL);
	if (!tas2557)
		return -ENOMEM;

	/* tas2781-comlib owns the paged regmap and per-chip register I/O */
	tas_priv = tasdevice_kzalloc(client);
	if (!tas_priv)
		return -ENOMEM;
	tas2557->tas_priv = tas_priv;

	i2c_set_clientdata(client, tas2557);

	init_completion(&tas2557->fw_done);
	tas2557->fw_requested = false;

	ret = tas2557_parse_dt(tas2557, client);
	if (ret)
		return ret;

	/* Default gain: +15 dB, as the firmware programs write */
	tas2557->dac_gain = TAS2557_DAC_GAIN_MAX;

	/* The device group comes out of reset muted */
	tas2557->dac_muted = true;

	/* -1 means "never set"; see tas2557_apply_runtime_state() */
	tas2557->dai_fmt = -1;

	/* Seed a valid ASI word length before the first hw_params() */
	tas2557->wordlength = TAS2557_WORDLENGTH_32BIT;

	/* Explicit firmware name; overrides the PG-based mono default */
	if (!of_property_read_string(dev->of_node, "firmware-name", &fw_name))
		strscpy(tas2557->fw_name, fw_name, sizeof(tas2557->fw_name));
	else if (tas_priv->ndev > 1)
		return dev_err_probe(dev, -EINVAL,
				     "firmware-name is required for two chips\n");

	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(tas2557_supplies),
					     tas2557_supplies);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to get/enable supplies\n");
	usleep_range(5000, 10000);

	ret = tasdevice_init(tas_priv);
	if (ret)
		return dev_err_probe(dev, ret, "comlib init failed\n");

	/*
	 * Both actions are registered right after tasdevice_init().  devm
	 * actions run in LIFO order: the component registered below goes
	 * first, then the IRQs, then tas2557_fw_teardown() (nothing can
	 * still run tas2557_irq_handler() or the DAI ops at that point, and
	 * it waits for tas2557_fw_ready()), and only then is codec_lock
	 * destroyed, before the comlib regmap, reset GPIOs and regulators
	 * are released.
	 */
	ret = devm_add_action_or_reset(dev, tas2557_comlib_remove, tas_priv);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, tas2557_fw_teardown, tas2557);
	if (ret)
		return ret;

	/* One threaded handler per configured IRQ, same dev_id */
	for (i = 0; i < tas_priv->ndev; i++) {
		if (i > 0 && tas2557->irq[i] == tas2557->irq[i - 1])
			continue;
		if (tas2557->irq[i] <= 0)
			continue;

		irq = tas2557->irq[i];
		ret = devm_request_threaded_irq(dev, irq, NULL,
						tas2557_irq_handler,
						IRQF_ONESHOT | IRQF_NO_AUTOEN,
						"tas2557", tas2557);
		if (ret) {
			dev_warn(dev, "IRQ %d request failed: %d\n", irq, ret);
			/* Every chip sharing the line loses it */
			for (j = i; j < tas_priv->ndev; j++)
				if (tas2557->irq[j] == irq)
					tas2557->irq[j] = 0;
		} else {
			dev_dbg(dev, "IRQ %d registered\n", irq);
		}
	}

	ret = devm_snd_soc_register_component(dev, &soc_component_tas2557,
					      &tas2557_dai, 1);
	if (ret)
		return dev_err_probe(dev, ret,
				     "component registration failed\n");

	dev_dbg(dev, "TAS2557 probed (%u chip(s), primary I2C addr 0x%02x, ASI%u)\n",
		tas_priv->ndev, client->addr, tas2557->asi_port);
	return 0;
}

static const struct i2c_device_id tas2557_i2c_id[] = {
	{ .name = "tas2557" },
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
