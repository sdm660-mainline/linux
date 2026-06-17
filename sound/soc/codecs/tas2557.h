/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ALSA SoC Texas Instruments TAS2557 Smart Amplifier
 *
 * Copyright (C) 2016 Texas Instruments Inc.
 * Copyright (C) 2024
 */

#ifndef __TAS2557_H__
#define __TAS2557_H__

#include <linux/types.h>

struct mutex;
struct delayed_work;
struct dentry;

/*
 * TAS2557 uses a book/page/register addressing scheme.
 * Physical I2C address has 8-bit registers; page is selected via register 0;
 * book is selected via register 0x7f in page 0.
 * Encoding: 0xBBBBBBBB_PPPPPPPP_RRRRRRR (book * 256 * 128 + page * 128 + reg)
 */

/* Page Control Register - same in all pages */
#define TAS2557_PAGE_REG		0x00

/* Book Control Register - in page 0 of each book */
#define TAS2557_BOOK_REG		0x7f

/* Address encoding macros */
#define TAS2557_REG(book, page, reg) \
	((((unsigned int)(book) * 256 * 128) + \
	  ((unsigned int)(page) * 128)) + (reg))

#define TAS2557_BOOK_ID(reg)		((unsigned char)((reg) / (256 * 128)))
#define TAS2557_PAGE_ID(reg)		((unsigned char)(((reg) % (256 * 128)) / 128))
#define TAS2557_PAGE_REG_ADDR(reg)	((unsigned char)((reg) % 128))

/* Book 0, Page 0 registers */
#define TAS2557_SW_RESET_REG		TAS2557_REG(0, 0, 1)
#define TAS2557_REV_PGID_REG		TAS2557_REG(0, 0, 3)
#define TAS2557_POWER_CTRL1_REG		TAS2557_REG(0, 0, 4)
#define TAS2557_POWER_CTRL2_REG		TAS2557_REG(0, 0, 5)
#define TAS2557_SPK_CTRL_REG		TAS2557_REG(0, 0, 6)
#define TAS2557_MUTE_REG		TAS2557_REG(0, 0, 7)
#define TAS2557_SNS_CTRL_REG		TAS2557_REG(0, 0, 8)
#define TAS2557_ADC_INPUT_SEL_REG	TAS2557_REG(0, 0, 9)
#define TAS2557_DBOOST_CTL_REG		TAS2557_REG(0, 0, 10)
#define TAS2557_SAR_SAMPLING_TIME_REG	TAS2557_REG(0, 0, 19)
#define TAS2557_SAR_ADC1_REG		TAS2557_REG(0, 0, 20)
#define TAS2557_SAR_ADC2_REG		TAS2557_REG(0, 0, 21)
#define TAS2557_CRC_CHECKSUM_REG	TAS2557_REG(0, 0, 32)
#define TAS2557_CRC_RESET_REG		TAS2557_REG(0, 0, 33)
#define TAS2557_DSP_MODE_SELECT_REG	TAS2557_REG(0, 0, 34)
#define TAS2557_SAFE_GUARD_REG		TAS2557_REG(0, 0, 37)
#define TAS2557_ASI_CTL1_REG		TAS2557_REG(0, 0, 42)
#define TAS2557_CLK_ERR_CTRL		TAS2557_REG(0, 0, 44)
#define TAS2557_CLK_ERR_CTRL2		TAS2557_REG(0, 0, 45)
#define TAS2557_CLK_ERR_CTRL3		TAS2557_REG(0, 0, 46)
#define TAS2557_DBOOST_CFG_REG		TAS2557_REG(0, 0, 52)
#define TAS2557_POWER_UP_FLAG_REG	TAS2557_REG(0, 0, 100)
#define TAS2557_FLAGS_1			TAS2557_REG(0, 0, 104)
#define TAS2557_FLAGS_2			TAS2557_REG(0, 0, 108)

/* Book 0, Page 1 registers - ASI1 configuration */
#define TAS2557_ASI1_DAC_FORMAT_REG	TAS2557_REG(0, 1, 1)
#define TAS2557_ASI1_ADC_FORMAT_REG	TAS2557_REG(0, 1, 2)
#define TAS2557_ASI1_OFFSET1_REG	TAS2557_REG(0, 1, 3)
#define TAS2557_ASI1_OFFSET2_REG	TAS2557_REG(0, 1, 4)
#define TAS2557_ASI1_ADC_PATH_REG	TAS2557_REG(0, 1, 7)
#define TAS2557_ASI1_DAC_BCLK_REG	TAS2557_REG(0, 1, 8)
#define TAS2557_ASI1_DAC_WCLK_REG	TAS2557_REG(0, 1, 9)
#define TAS2557_ASI1_ADC_BCLK_REG	TAS2557_REG(0, 1, 10)
#define TAS2557_ASI1_ADC_WCLK_REG	TAS2557_REG(0, 1, 11)
#define TAS2557_ASI1_DIN_DOUT_MUX_REG	TAS2557_REG(0, 1, 12)

/* Book 0, Page 1 - ASI2 configuration */
#define TAS2557_ASI2_DAC_FORMAT_REG	TAS2557_REG(0, 1, 21)
#define TAS2557_ASI2_ADC_FORMAT_REG	TAS2557_REG(0, 1, 22)
#define TAS2557_ASI2_OFFSET1_REG	TAS2557_REG(0, 1, 23)
#define TAS2557_ASI2_OFFSET2_REG	TAS2557_REG(0, 1, 24)
#define TAS2557_ASI2_ADC_PATH_REG	TAS2557_REG(0, 1, 27)
#define TAS2557_ASI2_DAC_BCLK_REG	TAS2557_REG(0, 1, 28)
#define TAS2557_ASI2_DAC_WCLK_REG	TAS2557_REG(0, 1, 29)
#define TAS2557_ASI2_ADC_BCLK_REG	TAS2557_REG(0, 1, 30)
#define TAS2557_ASI2_ADC_WCLK_REG	TAS2557_REG(0, 1, 31)
#define TAS2557_ASI2_DIN_DOUT_MUX_REG	TAS2557_REG(0, 1, 32)
#define TAS2557_ASI2_BDIV_CLK_SEL_REG	TAS2557_REG(0, 1, 33)
#define TAS2557_ASI2_BDIV_CLK_RATIO_REG	TAS2557_REG(0, 1, 34)
#define TAS2557_ASI2_WDIV_CLK_RATIO_REG	TAS2557_REG(0, 1, 35)
#define TAS2557_ASI2_DAC_CLKOUT_REG	TAS2557_REG(0, 1, 36)
#define TAS2557_ASI2_ADC_CLKOUT_REG	TAS2557_REG(0, 1, 37)

/* Book 0, Page 1 - GPIO pin configuration */
#define TAS2557_GPIO1_PIN_REG		TAS2557_REG(0, 1, 61)
#define TAS2557_GPIO2_PIN_REG		TAS2557_REG(0, 1, 62)
#define TAS2557_GPIO3_PIN_REG		TAS2557_REG(0, 1, 63)
#define TAS2557_GPIO4_PIN_REG		TAS2557_REG(0, 1, 64)
#define TAS2557_GPIO5_PIN_REG		TAS2557_REG(0, 1, 65)
#define TAS2557_GPIO6_PIN_REG		TAS2557_REG(0, 1, 66)
#define TAS2557_GPIO7_PIN_REG		TAS2557_REG(0, 1, 67)
#define TAS2557_GPIO8_PIN_REG		TAS2557_REG(0, 1, 68)
#define TAS2557_GPI_PIN_REG		TAS2557_REG(0, 1, 77)

/* Book 0, Page 1 - interrupt configuration */
#define TAS2557_CLK_HALT_REG		TAS2557_REG(0, 1, 106)
#define TAS2557_INT_GEN1_REG		TAS2557_REG(0, 1, 108)
#define TAS2557_INT_GEN2_REG		TAS2557_REG(0, 1, 109)
#define TAS2557_INT_GEN3_REG		TAS2557_REG(0, 1, 110)
#define TAS2557_INT_GEN4_REG		TAS2557_REG(0, 1, 111)
#define TAS2557_INT_MODE_REG		TAS2557_REG(0, 1, 114)
#define TAS2557_MAIN_CLKIN_REG		TAS2557_REG(0, 1, 115)
#define TAS2557_PLL_CLKIN_REG		TAS2557_REG(0, 1, 116)

/* Book 0, Page 2 */
#define TAS2557_SLEEPMODE_CTL_REG	TAS2557_REG(0, 2, 7)

/* Book 100, Page 0 - DSP / PLL registers */
#define TAS2557_DAC_INTERPOL_REG	TAS2557_REG(100, 0, 1)
#define TAS2557_SOFT_MUTE_REG		TAS2557_REG(100, 0, 7)
#define TAS2557_PLL_P_VAL_REG		TAS2557_REG(100, 0, 27)
#define TAS2557_PLL_J_VAL_REG		TAS2557_REG(100, 0, 28)
#define TAS2557_PLL_D_VAL_MSB_REG	TAS2557_REG(100, 0, 29)
#define TAS2557_PLL_D_VAL_LSB_REG	TAS2557_REG(100, 0, 30)
#define TAS2557_CLK_MISC_REG		TAS2557_REG(100, 0, 31)
#define TAS2557_PLL_N_VAL_REG		TAS2557_REG(100, 0, 32)
#define TAS2557_DAC_MADC_VAL_REG	TAS2557_REG(100, 0, 33)
#define TAS2557_VBOOST_CTL_REG		TAS2557_REG(100, 0, 64)

/* Book 130, Page 2 - Die temperature */
#define TAS2557_DIE_TEMP_REG		TAS2557_REG(130, 2, 124)

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
#define TAS2557_VSENSE_ENABLE		BIT(0)
#define TAS2557_ISENSE_ENABLE		BIT(1)
#define TAS2557_BOOST_ENABLE		BIT(5)
#define TAS2557_CLASSD_ENABLE		BIT(7)

/* B0P0R6 - TAS2557_SPK_CTRL_REG */
#define TAS2557_DAC_GAIN_MASK		(0xf << 3)
#define TAS2557_DAC_GAIN_SHIFT		3
#define TAS2557_DAC_GAIN_MAX		0x0f

/* B0P0R8 - TAS2557_SNS_CTRL_REG */
#define TAS2557_ISNS_SLOT_MASK		(0x7 << 4)
#define TAS2557_ISNS_SLOT_SHIFT		4
#define TAS2557_VSNS_SLOT_MASK		(0x7 << 0)
#define TAS2557_VSNS_SLOT_SHIFT		0

/* B0P0R7 - TAS2557_MUTE_REG */
#define TAS2557_CLASSD_MUTE		BIT(0)
#define TAS2557_ISENSE_MUTE		BIT(1)

/* B100P0R7 - TAS2557_SOFT_MUTE_REG */
#define TAS2557_PDM_SOFT_MUTE		BIT(0)
#define TAS2557_VSENSE_SOFT_MUTE	BIT(1)
#define TAS2557_ISENSE_SOFT_MUTE	BIT(2)
#define TAS2557_CLASSD_SOFT_MUTE	BIT(3)

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

/* Supported PCM formats exposed to the ASoC core */
#define TAS2557_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
	 SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

/* Error code bits tracked in tas2557_priv.err_code */
#define ERROR_NONE		0x00000000
#define ERROR_PLL_ABSENT	0x00000001
#define ERROR_DEVA_I2C_COMM	0x00000002
#define ERROR_PRAM_CRCCHK	0x00000008
#define ERROR_YRAM_CRCCHK	0x00000010
#define ERROR_CLK_DET2		0x00000020
#define ERROR_CLK_DET1		0x00000040
#define ERROR_CLK_LOST		0x00000080
#define ERROR_BROWNOUT		0x00000100
#define ERROR_DIE_OVERTEMP	0x00000200
#define ERROR_CLK_HALT		0x00000400
#define ERROR_UNDER_VOLTAGE	0x00000800
#define ERROR_OVER_CURRENT	0x00001000
#define ERROR_CLASSD_PWR	0x00002000
#define ERROR_FAILSAFE		0x40000000

/*
 * Firmware binary format
 *
 * Header: 4-byte magic ("5552" = 0x35 0x35 0x35 0x32), then 6 BE32 fields
 * (size, checksum, ppc_version, fw_version, driver_version, timestamp),
 * 64-byte DDC name, null-terminated description, then device_family (BE32)
 * and device (BE32, must be 2 for TAS2557).
 */

/* Firmware magic: ASCII "5552" stored as four bytes 0x35,0x35,0x35,0x32 */
#define TAS2557_FW_MAGIC		0x35353532

/* "device" field in the firmware header */
#define TAS2557_FW_DEVICE_MONO		2
#define TAS2557_FW_DEVICE_STEREO	3

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

/* Firmware driver version flags (driver_version field) */
#define PPC_DRIVER_CRCCHK	0x00000200
#define PPC_DRIVER_CONFDEV	0x00000300
#define PPC_DRIVER_MTPLLSRC	0x00000400

/** struct tas2557_block - one block of register-write commands from firmware */
struct tas2557_block {
	u32 type;
	u8  pchksum_present;
	u8  pchksum;
	u8  ychksum_present;
	u8  ychksum;
	u32 num_commands;
	u8 *data;		/* num_commands * 4 bytes: [book][page][reg][val] */
};

/** struct tas2557_data - named container of firmware blocks */
struct tas2557_data {
	char  name[64];
	char *description;
	u32   num_blocks;
	struct tas2557_block *blocks;
};

/** struct tas2557_pll - one PLL configuration */
struct tas2557_pll {
	char  name[64];
	char *description;
	struct tas2557_block block;
};

/** struct tas2557_program - one DSP program */
struct tas2557_program {
	char  name[64];
	char *description;
	u8    app_mode;
	u16   boost;
	struct tas2557_data data;
};

/** struct tas2557_config - one audio configuration (tied to a sample rate) */
struct tas2557_config {
	char  name[64];
	char *description;
	u32   program;		/* which program this config is for */
	u32   pll;		/* which PLL config to use */
	u32   sample_rate;
	u8    pll_src;
	u32   pll_src_rate;
	struct tas2557_data data;
};

/** struct tas2557_firmware - top-level parsed firmware container */
struct tas2557_firmware {
	u32   size;
	u32   checksum;
	u32   ppc_version;
	u32   fw_version;
	u32   driver_version;
	u32   timestamp;
	char  ddc_name[64];
	char *description;
	u32   device_family;
	u32   device;
	u32   num_plls;
	struct tas2557_pll     *plls;
	u32   num_programs;
	struct tas2557_program *programs;
	u32   num_configs;
	struct tas2557_config  *configs;
};

struct regulator;
struct gpio_desc;
struct hrtimer;
struct work_struct;

/* Temperature monitoring period and limits */
#define TAS2557_TEMP_CHECK_PERIOD	1000	/* ms */
#define TAS2557_SAFE_TEMP_HIGH		150	/* degrees C */

/** struct tas2557_priv - per-device driver state */
struct tas2557_priv {
	struct device	*dev;
	struct regmap	*regmap;
	struct mutex	 dev_lock;	/* protects device register I/O and state */

	struct gpio_desc *reset_gpio;
	int		  irq;
	bool		  irq_enabled;

	/* Regulators */
	struct regulator *vdd;

	/* Book/page tracking for book-paged regmap */
	unsigned char current_book;
	unsigned char current_page;

	/* Hardware revision */
	int pg_id;

	/* DT-configured audio parameters */
	unsigned int i2s_bits;		/* slot width from ti,i2s-bit-width */
	unsigned int channel;		/* 0=left, 1=right from ti,channel */
	unsigned int imon_slot;		/* from ti,imon-slot */
	unsigned int vmon_slot;		/* from ti,vmon-slot */
	bool	     isense_enabled;
	bool	     vsense_enabled;

	/* Power state */
	bool powered;
	bool muted;

	/* Firmware state */
	struct tas2557_firmware *fw;
	bool fw_loaded;
	char fw_name[64];		/* filename that was successfully loaded */
	unsigned int current_program;
	unsigned int current_config;

	/* Runtime audio configuration */
	unsigned int sample_rate;

	/* Error / health tracking */
	unsigned int err_code;
	unsigned int restart_count;

	/* Volume control */
	unsigned int dac_gain;

	/* TDM slot configuration */
	int tdm_rx_slot;
	int tdm_tx_slot;
	int tdm_slot_width;

	/* IRQ deferred work */
	struct delayed_work irq_work;

	/* Die temperature monitoring */
	struct hrtimer	temp_timer;
	struct work_struct temp_work;
	int		  die_temp;
	bool		  temp_monitor_enabled;

	/* debugfs */
	struct dentry	*debugfs_root;
	unsigned int	 dbg_book;
	unsigned int	 dbg_page;
	unsigned int	 dbg_reg;
	unsigned int	 dbg_val;
	bool		 dbg_reg_valid;
};

#endif /* __TAS2557_H__ */
