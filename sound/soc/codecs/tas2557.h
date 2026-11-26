/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ALSA SoC Texas Instruments TAS2557 Smart Amplifier
 *
 * Copyright (C) 2016 Texas Instruments Inc.
 * Copyright (C) 2026 Gianluca Boiano <morf3089@gmail.com>
 */

#ifndef __TAS2557_H__
#define __TAS2557_H__

#include <linux/bits.h>
#include <sound/tas2781.h>

/*
 * TAS2557 uses a book/page/register addressing scheme, same layout as
 * tas2781-comlib's TASDEVICE_REG(): 8-bit registers, page selected via
 * register 0, book selected via register 0x7f in page 0.  Register
 * access, book/page switching and the paged-window regmap are all
 * provided by tas2781-comlib(-i2c); this header only keeps the TAS2557_*
 * register names for readability, defined in terms of TASDEVICE_REG().
 */

/* Build a register address from firmware-block book/page/offset bytes */
#define TAS2557_REG(book, page, reg)	TASDEVICE_REG(book, page, reg)

/* Book 0, Page 0 registers */
#define TAS2557_SW_RESET_REG		TASDEVICE_REG(0, 0, 1)
#define TAS2557_REV_PGID_REG		TASDEVICE_REG(0, 0, 3)
#define TAS2557_POWER_CTRL1_REG		TASDEVICE_REG(0, 0, 4)
#define TAS2557_POWER_CTRL2_REG		TASDEVICE_REG(0, 0, 5)
#define TAS2557_SPK_CTRL_REG		TASDEVICE_REG(0, 0, 6)
#define TAS2557_MUTE_REG		TASDEVICE_REG(0, 0, 7)
#define TAS2557_ADC_INPUT_SEL_REG	TASDEVICE_REG(0, 0, 9)
#define TAS2557_DBOOST_CTL_REG		TASDEVICE_REG(0, 0, 10)
#define TAS2557_SAR_SAMPLING_TIME_REG	TASDEVICE_REG(0, 0, 19)
#define TAS2557_SAR_ADC1_REG		TASDEVICE_REG(0, 0, 20)
#define TAS2557_SAR_ADC2_REG		TASDEVICE_REG(0, 0, 21)
#define TAS2557_CRC_CHECKSUM_REG	TASDEVICE_REG(0, 0, 32)
#define TAS2557_CRC_RESET_REG		TASDEVICE_REG(0, 0, 33)
#define TAS2557_DSP_MODE_SELECT_REG	TASDEVICE_REG(0, 0, 34)
#define TAS2557_SAFE_GUARD_REG		TASDEVICE_REG(0, 0, 37)
#define TAS2557_ASI_CTL1_REG		TASDEVICE_REG(0, 0, 42)
#define TAS2557_CLK_ERR_CTRL		TASDEVICE_REG(0, 0, 44)
#define TAS2557_CLK_ERR_CTRL2		TASDEVICE_REG(0, 0, 45)
#define TAS2557_CLK_ERR_CTRL3		TASDEVICE_REG(0, 0, 46)
#define TAS2557_DBOOST_CFG_REG		TASDEVICE_REG(0, 0, 52)
#define TAS2557_POWER_UP_FLAG_REG	TASDEVICE_REG(0, 0, 100)
#define TAS2557_FLAGS_1			TASDEVICE_REG(0, 0, 104)
#define TAS2557_FLAGS_2			TASDEVICE_REG(0, 0, 108)

/* Book 0, Page 1 registers - ASI1 configuration */
#define TAS2557_ASI1_DAC_FORMAT_REG	TASDEVICE_REG(0, 1, 1)
#define TAS2557_ASI1_OFFSET1_REG	TASDEVICE_REG(0, 1, 3)
#define TAS2557_ASI1_ADC_PATH_REG	TASDEVICE_REG(0, 1, 7)
#define TAS2557_ASI1_DAC_BCLK_REG	TASDEVICE_REG(0, 1, 8)
#define TAS2557_ASI1_DAC_WCLK_REG	TASDEVICE_REG(0, 1, 9)
#define TAS2557_ASI1_ADC_BCLK_REG	TASDEVICE_REG(0, 1, 10)
#define TAS2557_ASI1_ADC_WCLK_REG	TASDEVICE_REG(0, 1, 11)
#define TAS2557_ASI1_DIN_DOUT_MUX_REG	TASDEVICE_REG(0, 1, 12)

/* Book 0, Page 1 - ASI2 configuration */
#define TAS2557_ASI2_DAC_FORMAT_REG	TASDEVICE_REG(0, 1, 21)
#define TAS2557_ASI2_OFFSET1_REG	TASDEVICE_REG(0, 1, 23)
#define TAS2557_ASI2_ADC_PATH_REG	TASDEVICE_REG(0, 1, 27)
#define TAS2557_ASI2_DAC_BCLK_REG	TASDEVICE_REG(0, 1, 28)
#define TAS2557_ASI2_DAC_WCLK_REG	TASDEVICE_REG(0, 1, 29)
#define TAS2557_ASI2_ADC_BCLK_REG	TASDEVICE_REG(0, 1, 30)
#define TAS2557_ASI2_ADC_WCLK_REG	TASDEVICE_REG(0, 1, 31)
#define TAS2557_ASI2_DIN_DOUT_MUX_REG	TASDEVICE_REG(0, 1, 32)
#define TAS2557_ASI2_BDIV_CLK_SEL_REG	TASDEVICE_REG(0, 1, 33)
#define TAS2557_ASI2_BDIV_CLK_RATIO_REG	TASDEVICE_REG(0, 1, 34)
#define TAS2557_ASI2_WDIV_CLK_RATIO_REG	TASDEVICE_REG(0, 1, 35)
#define TAS2557_ASI2_DAC_CLKOUT_REG	TASDEVICE_REG(0, 1, 36)
#define TAS2557_ASI2_ADC_CLKOUT_REG	TASDEVICE_REG(0, 1, 37)

/* Book 0, Page 1 - GPIO pin configuration */
#define TAS2557_GPIO1_PIN_REG		TASDEVICE_REG(0, 1, 61)
#define TAS2557_GPIO2_PIN_REG		TASDEVICE_REG(0, 1, 62)
#define TAS2557_GPIO3_PIN_REG		TASDEVICE_REG(0, 1, 63)
#define TAS2557_GPIO4_PIN_REG		TASDEVICE_REG(0, 1, 64)
#define TAS2557_GPIO5_PIN_REG		TASDEVICE_REG(0, 1, 65)
#define TAS2557_GPIO6_PIN_REG		TASDEVICE_REG(0, 1, 66)
#define TAS2557_GPIO7_PIN_REG		TASDEVICE_REG(0, 1, 67)
#define TAS2557_GPIO8_PIN_REG		TASDEVICE_REG(0, 1, 68)
#define TAS2557_GPI_PIN_REG		TASDEVICE_REG(0, 1, 77)

/* Book 0, Page 1 - interrupt configuration */
#define TAS2557_CLK_HALT_REG		TASDEVICE_REG(0, 1, 106)
#define TAS2557_INT_GEN1_REG		TASDEVICE_REG(0, 1, 108)
#define TAS2557_INT_GEN2_REG		TASDEVICE_REG(0, 1, 109)
#define TAS2557_INT_GEN3_REG		TASDEVICE_REG(0, 1, 110)
#define TAS2557_INT_GEN4_REG		TASDEVICE_REG(0, 1, 111)
#define TAS2557_INT_MODE_REG		TASDEVICE_REG(0, 1, 114)
#define TAS2557_MAIN_CLKIN_REG		TASDEVICE_REG(0, 1, 115)
#define TAS2557_PLL_CLKIN_REG		TASDEVICE_REG(0, 1, 116)

/* Book 0, Page 2 */
#define TAS2557_SLEEPMODE_CTL_REG	TASDEVICE_REG(0, 2, 7)

/* Book 100, Page 0 - DSP / PLL registers */
#define TAS2557_DAC_INTERPOL_REG	TASDEVICE_REG(100, 0, 1)
#define TAS2557_SOFT_MUTE_REG		TASDEVICE_REG(100, 0, 7)
#define TAS2557_PLL_P_VAL_REG		TASDEVICE_REG(100, 0, 27)
#define TAS2557_PLL_J_VAL_REG		TASDEVICE_REG(100, 0, 28)
#define TAS2557_PLL_D_VAL_MSB_REG	TASDEVICE_REG(100, 0, 29)
#define TAS2557_PLL_D_VAL_LSB_REG	TASDEVICE_REG(100, 0, 30)
#define TAS2557_CLK_MISC_REG		TASDEVICE_REG(100, 0, 31)
#define TAS2557_PLL_N_VAL_REG		TASDEVICE_REG(100, 0, 32)
#define TAS2557_DAC_MADC_VAL_REG	TASDEVICE_REG(100, 0, 33)
#define TAS2557_VBOOST_CTL_REG		TASDEVICE_REG(100, 0, 64)

/* Book 130, Page 2 - Die temperature */
#define TAS2557_DIE_TEMP_REG		TASDEVICE_REG(130, 2, 124)

/* DSP coefficient swap register (skipped by the firmware CRC checker) */
#define TAS2557_SA_COEFF_SWAP_REG	TASDEVICE_REG(0, 53, 44)

/* PG (Process Generation) version identifiers from REV_PGID_REG */
#define TAS2557_PG_VERSION_1P0		0x80
#define TAS2557_PG_VERSION_2P0		0x90
#define TAS2557_PG_VERSION_2P1		0xa0

/* B0P0R4 - TAS2557_POWER_CTRL1_REG */
#define TAS2557_SW_SHUTDOWN		BIT(0)
#define TAS2557_MADC_POWER_UP		BIT(3)
#define TAS2557_MDAC_POWER_UP		BIT(4)
#define TAS2557_NDIV_POWER_UP		BIT(5)
#define TAS2557_PLL_POWER_UP		BIT(6)
#define TAS2557_DSP_POWER_UP		BIT(7)

/* B0P0R5 - TAS2557_POWER_CTRL2_REG */
#define TAS2557_BOOST_ENABLE		BIT(5)
#define TAS2557_CLASSD_ENABLE		BIT(7)

/* B0P0R6 - TAS2557_SPK_CTRL_REG */
#define TAS2557_DAC_GAIN_MASK		(0xf << 3)
#define TAS2557_DAC_GAIN_SHIFT		3
#define TAS2557_DAC_GAIN_MAX		0x0f

/* B0P0R7 - TAS2557_MUTE_REG */
#define TAS2557_MUTE_VSNS		BIT(0)
#define TAS2557_MUTE_ISNS		BIT(1)

/* B100P0R7 - TAS2557_SOFT_MUTE_REG */
#define TAS2557_PDM_SOFT_MUTE		BIT(0)

/* ASI format field in ASIx_DAC_FORMAT_REG bits [7:5] */
#define TAS2557_FORMAT_I2S		(0x0 << 5)
#define TAS2557_FORMAT_DSP		(0x1 << 5)
#define TAS2557_FORMAT_RIGHT_J		(0x2 << 5)
#define TAS2557_FORMAT_LEFT_J		(0x3 << 5)
#define TAS2557_FORMAT_MONO_PCM		(0x4 << 5)
#define TAS2557_FORMAT_MASK		(0x7 << 5)

/* ASI word length field in ASIx_DAC_FORMAT_REG bits [4:3] */
#define TAS2557_WORDLENGTH_16BIT	(0x0 << 3)
#define TAS2557_WORDLENGTH_20BIT	(0x1 << 3)
#define TAS2557_WORDLENGTH_24BIT	(0x2 << 3)
#define TAS2557_WORDLENGTH_32BIT	(0x3 << 3)
#define TAS2557_WORDLENGTH_MASK		(0x3 << 3)

/* Safe guard pattern written to TAS2557_SAFE_GUARD_REG */
#define TAS2557_SAFE_GUARD_PATTERN	0x5a

/* Firmware filenames - PG version determines which file is loaded */
#define TAS2557_FW_NAME			"tas2557_uCDSP.bin"
#define TAS2557_PG1P0_FW_NAME		"tas2557_pg1p0_uCDSP.bin"

/* Firmware block type codes */
#define TAS2557_BLOCK_PLL		0x00
#define TAS2557_BLOCK_PGM_ALL		0x0d
#define TAS2557_BLOCK_PGM_DEV_A		0x01
#define TAS2557_BLOCK_PGM_DEV_B		0x08
#define TAS2557_BLOCK_CFG_COEFF_DEV_A	0x03
#define TAS2557_BLOCK_CFG_COEFF_DEV_B	0x0a
#define TAS2557_BLOCK_CFG_PRE_DEV_A	0x04
#define TAS2557_BLOCK_CFG_PRE_DEV_B	0x0b
#define TAS2557_BLOCK_CFG_POST		0x05
#define TAS2557_BLOCK_CFG_POST_POWER	0x06

/*
 * ti,asi-port selects which ASI's pin mux/clock dividers/format registers
 * this driver programs; the other ASI's registers are left untouched at
 * reset defaults.
 */
#define TAS2557_ASI_PORT1	1
#define TAS2557_ASI_PORT2	2
#define TAS2557_ASI_PORT_DEFAULT	TAS2557_ASI_PORT2

/*
 * One DT node manages an array of up to TAS2557_MAX_DEV physical chips
 * (one entry per `reg` address), all sharing one firmware image, DAI and
 * mute/power state via tas2781-comlib's per-channel register access
 * (tasdevice_dev_*(tas_priv, chn, ...)).  reset-gpios/interrupts follow
 * the same per-index convention (a single entry applies to every chip).
 */
#define TAS2557_MAX_DEV	2

/* Firmware driver version flags (driver_version field) */
#define PPC_DRIVER_CRCCHK	0x00000200
#define PPC_DRIVER_CONFDEV	0x00000300
#define PPC_DRIVER_MTPLLSRC	0x00000400
/* Present from driver_version 0x101; carries a 2-byte device field */
#define PPC_DRIVER_CFGDEV_NONCRC	0x00000101

#endif /* __TAS2557_H__ */
