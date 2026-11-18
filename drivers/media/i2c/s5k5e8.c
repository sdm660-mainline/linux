// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Kulesha Evgeniy <voovdop@gmail.com>
/*
 * Samsung S5K5E8 5MP CMOS image sensor driver
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define S5K5E8_REG_CHIP_ID		CCI_REG16(0x0000)
#define S5K5E8_CHIP_ID			0x5e80

#define S5K5E8_REG_MODE_SELECT		CCI_REG8(0x0100)
#define S5K5E8_REG_GROUP_HOLD		CCI_REG8(0x0104)
#define S5K5E8_REG_EXPOSURE		CCI_REG16(0x0202)
#define S5K5E8_REG_ANALOG_GAIN		CCI_REG16(0x0204)
#define S5K5E8_REG_VTS			CCI_REG16(0x0340)

#define S5K5E8_VTS_MAX			0xffff
#define S5K5E8_EXPOSURE_MIN		4
#define S5K5E8_EXPOSURE_MARGIN		4

/* Gain code: 0x20 == 1x, 0x200 == 16x. */
#define S5K5E8_GAIN_MIN			0x20
#define S5K5E8_GAIN_MAX			0x200
#define S5K5E8_GAIN_DEFAULT		0x20

#define S5K5E8_DATA_LANES		2
#define S5K5E8_XCLK_FREQ		24000000

/* DDR clock in Hz (bit rate / 2). */
#define S5K5E8_LINK_FREQ_418MHZ		418000000ULL
#define S5K5E8_LINK_FREQ_300MHZ		300000000ULL

enum {
	S5K5E8_LINK_FREQ_INDEX_418MHZ,
	S5K5E8_LINK_FREQ_INDEX_300MHZ,
};

static const s64 s5k5e8_link_freq_menu[] = {
	S5K5E8_LINK_FREQ_418MHZ,
	S5K5E8_LINK_FREQ_300MHZ,
};

#define S5K5E8_MBUS_CODE		MEDIA_BUS_FMT_SGRBG10_1X10

struct s5k5e8_reg {
	u16 addr;
	u8 val;
	u16 delay;	/* delay after this write, in ms */
};

struct s5k5e8_reg_list {
	u32 num_of_regs;
	const struct s5k5e8_reg *regs;
};

struct s5k5e8_mode {
	u32 width;
	u32 height;
	u32 hts;		/* pixels per line */
	u32 vts_def;		/* lines per frame */
	u32 exposure_def;	/* default exposure, lines */
	u32 pixel_rate;		/* Hz */
	u32 link_freq_index;
	struct s5k5e8_reg_list reg_list;
};

/*
 * Power-up order: AVDD, DVDD (PM660L gpio4), CAM_VDIG (TLMM45, chained
 * behind DVDD), then DOVDD.
 */
static const char * const s5k5e8_supply_names[] = {
	"avdd",		/* 2.8 V */
	"dvdd",		/* 1.2 V */
	"dovdd",	/* 1.8 V */
};
#define S5K5E8_NUM_SUPPLIES ARRAY_SIZE(s5k5e8_supply_names)

struct s5k5e8 {
	struct i2c_client *client;
	struct regmap *regmap;
	struct clk *xclk;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[S5K5E8_NUM_SUPPLIES];

	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *exposure;

	const struct s5k5e8_mode *cur_mode;
};

static inline struct s5k5e8 *to_s5k5e8(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s5k5e8, sd);
}

static int s5k5e8_write_table(struct s5k5e8 *s, const struct s5k5e8_reg *t,
			      unsigned int n)
{
	int ret = 0;
	unsigned int i;

	for (i = 0; i < n && !ret; i++) {
		cci_write(s->regmap, CCI_REG8(t[i].addr), t[i].val, &ret);
		if (t[i].delay)
			usleep_range(t[i].delay * 1000,
				     t[i].delay * 1000 + 1000);
	}

	return ret;
}


static const struct s5k5e8_reg s5k5e8_init_regs[] = {
	{ 0x3906, 0x7e },
	{ 0x3c01, 0x0f },
	{ 0x3c14, 0x00 },
	{ 0x3235, 0x08 },
	{ 0x3063, 0x2e },
	{ 0x307a, 0x10 },
	{ 0x307b, 0x0e },
	{ 0x3079, 0x20 },
	{ 0x3070, 0x05 },
	{ 0x3067, 0x06 },
	{ 0x3071, 0x62 },
	{ 0x3203, 0x43 },
	{ 0x3205, 0x43 },
	{ 0x320b, 0x42 },
	{ 0x3007, 0x00 },
	{ 0x3008, 0x14 },
	{ 0x3020, 0x58 },
	{ 0x300d, 0x34 },
	{ 0x300e, 0x17 },
	{ 0x3021, 0x02 },
	{ 0x3010, 0x59 },
	{ 0x3002, 0x01 },
	{ 0x3005, 0x01 },
	{ 0x3008, 0x04 },
	{ 0x300f, 0x70 },
	{ 0x3010, 0x69 },
	{ 0x3017, 0x10 },
	{ 0x3019, 0x19 },
	{ 0x300c, 0x62 },
	{ 0x3064, 0x10 },
	{ 0x3c08, 0x0e },
	{ 0x3c09, 0x10 },
	{ 0x3c31, 0x0d },
	{ 0x3c32, 0xac },
	{ 0x3929, 0x07 },
};

static const struct s5k5e8_reg s5k5e8_mode_2592x1944_regs[] = {
	{ 0x0136, 0x18 },
	{ 0x0137, 0x00 },
	{ 0x0305, 0x06 },
	{ 0x0306, 0x18 },
	{ 0x0307, 0xa8 },
	{ 0x0308, 0x34 },
	{ 0x0309, 0x42 },
	{ 0x3c1f, 0x00 },
	{ 0x3c17, 0x00 },
	{ 0x3c0b, 0x04 },
	{ 0x3c1c, 0x47 },
	{ 0x3c1d, 0x15 },
	{ 0x3c14, 0x04 },
	{ 0x3c16, 0x00 },
	{ 0x0820, 0x03 },
	{ 0x0821, 0x44 },
	{ 0x0114, 0x01 },
	{ 0x0344, 0x00 },
	{ 0x0345, 0x08 },
	{ 0x0346, 0x00 },
	{ 0x0347, 0x08 },
	{ 0x0348, 0x0a },
	{ 0x0349, 0x27 },
	{ 0x034a, 0x07 },
	{ 0x034b, 0x9f },
	{ 0x034c, 0x0a },
	{ 0x034d, 0x20 },
	{ 0x034e, 0x07 },
	{ 0x034f, 0x98 },
	{ 0x0900, 0x00 },
	{ 0x0901, 0x00 },
	{ 0x0381, 0x01 },
	{ 0x0383, 0x01 },
	{ 0x0385, 0x01 },
	{ 0x0387, 0x01 },
	{ 0x0340, 0x07 },
	{ 0x0341, 0xb0 },
	{ 0x0342, 0x0b },
	{ 0x0343, 0x28 },
	{ 0x0200, 0x00 },
	{ 0x0201, 0x00 },
	{ 0x0202, 0x07 },
	{ 0x0203, 0xaa },
	{ 0x3303, 0x02 },
	{ 0x3400, 0x00 },
	{ 0x323b, 0x02 },
	{ 0x3301, 0x00 },
	{ 0x3321, 0x04 },
	{ 0x3306, 0x00 },
	{ 0x3307, 0x08 },
	{ 0x3308, 0x0a },
	{ 0x3309, 0x27 },
	{ 0x330a, 0x01 },
	{ 0x330b, 0x01 },
	{ 0x330e, 0x00 },
	{ 0x330f, 0x08 },
	{ 0x3310, 0x07 },
	{ 0x3311, 0x9f },
	{ 0x3312, 0x01 },
	{ 0x3313, 0x01 },
};

static const struct s5k5e8_reg s5k5e8_mode_1296x972_regs[] = {
	{ 0x0136, 0x18 },
	{ 0x0137, 0x00 },
	{ 0x0305, 0x06 },
	{ 0x0306, 0x18 },
	{ 0x0307, 0xa8 },
	{ 0x0308, 0x34 },
	{ 0x0309, 0x42 },
	{ 0x3c1f, 0x00 },
	{ 0x3c17, 0x00 },
	{ 0x3c0b, 0x04 },
	{ 0x3c1c, 0x47 },
	{ 0x3c1d, 0x15 },
	{ 0x3c14, 0x04 },
	{ 0x3c16, 0x00 },
	{ 0x0820, 0x03 },
	{ 0x0821, 0x44 },
	{ 0x0114, 0x01 },
	{ 0x0344, 0x00 },
	{ 0x0345, 0x08 },
	{ 0x0346, 0x00 },
	{ 0x0347, 0x08 },
	{ 0x0348, 0x0a },
	{ 0x0349, 0x27 },
	{ 0x034a, 0x07 },
	{ 0x034b, 0x9f },
	{ 0x034c, 0x05 },
	{ 0x034d, 0x10 },
	{ 0x034e, 0x03 },
	{ 0x034f, 0xcc },
	{ 0x0900, 0x01 },
	{ 0x0901, 0x22 },
	{ 0x0381, 0x01 },
	{ 0x0383, 0x01 },
	{ 0x0385, 0x01 },
	{ 0x0387, 0x03 },
	{ 0x0340, 0x04 },
	{ 0x0341, 0x26 },
	{ 0x0342, 0x14 },
	{ 0x0343, 0x50 },
	{ 0x0200, 0x00 },
	{ 0x0201, 0x00 },
	{ 0x0202, 0x03 },
	{ 0x0203, 0xde },
	{ 0x3303, 0x02 },
	{ 0x3400, 0x00 },
	{ 0x323b, 0x02 },
	{ 0x3301, 0x00 },
	{ 0x3321, 0x04 },
	{ 0x3306, 0x00 },
	{ 0x3307, 0x08 },
	{ 0x3308, 0x0a },
	{ 0x3309, 0x27 },
	{ 0x330a, 0x01 },
	{ 0x330b, 0x01 },
	{ 0x330e, 0x00 },
	{ 0x330f, 0x08 },
	{ 0x3310, 0x07 },
	{ 0x3311, 0x9f },
	{ 0x3312, 0x03 },
	{ 0x3313, 0x01 },
};

static const struct s5k5e8_reg s5k5e8_mode_1304x980_regs[] = {
	{ 0x0136, 0x18 },
	{ 0x0137, 0x00 },
	{ 0x0305, 0x06 },
	{ 0x0306, 0x14 },
	{ 0x0307, 0xa8 },
	{ 0x0308, 0x1f },
	{ 0x0309, 0x42 },
	{ 0x3c1f, 0x00 },
	{ 0x3c17, 0x00 },
	{ 0x3c0b, 0x04 },
	{ 0x3c1c, 0x47 },
	{ 0x3c1d, 0x15 },
	{ 0x3c14, 0x04 },
	{ 0x3c16, 0x00 },
	{ 0x0820, 0x02 },
	{ 0x0821, 0x58 },
	{ 0x0114, 0x01 },
	{ 0x0344, 0x00 },
	{ 0x0345, 0x00 },
	{ 0x0346, 0x00 },
	{ 0x0347, 0x00 },
	{ 0x0348, 0x0a },
	{ 0x0349, 0x2f },
	{ 0x034a, 0x07 },
	{ 0x034b, 0xa7 },
	{ 0x034c, 0x05 },
	{ 0x034d, 0x18 },
	{ 0x034e, 0x03 },
	{ 0x034f, 0xd4 },
	{ 0x0900, 0x01 },
	{ 0x0901, 0x22 },
	{ 0x0381, 0x01 },
	{ 0x0383, 0x01 },
	{ 0x0385, 0x01 },
	{ 0x0387, 0x03 },
	{ 0x0340, 0x03 },
	{ 0x0341, 0xf1 },
	{ 0x0342, 0x0a },
	{ 0x0343, 0xe0 },
	{ 0x0200, 0x00 },
	{ 0x0201, 0x00 },
	{ 0x0202, 0x03 },
	{ 0x0203, 0xde },
	{ 0x3303, 0x02 },
	{ 0x3400, 0x00 },
	{ 0x323b, 0x02 },
	{ 0x3301, 0x00 },
	{ 0x3321, 0x04 },
	{ 0x3306, 0x00 },
	{ 0x3307, 0x00 },
	{ 0x3308, 0x0a },
	{ 0x3309, 0x2f },
	{ 0x330a, 0x01 },
	{ 0x330b, 0x01 },
	{ 0x330e, 0x00 },
	{ 0x330f, 0x00 },
	{ 0x3310, 0x07 },
	{ 0x3311, 0xa7 },
	{ 0x3312, 0x03 },
	{ 0x3313, 0x01 },
};

static const struct s5k5e8_reg s5k5e8_mode_800x600_regs[] = {
	{ 0x0136, 0x18 },
	{ 0x0137, 0x00 },
	{ 0x0305, 0x06 },
	{ 0x0306, 0x18 },
	{ 0x0307, 0xa8 },
	{ 0x0308, 0x34 },
	{ 0x0309, 0x42 },
	{ 0x3c1f, 0x00 },
	{ 0x3c17, 0x00 },
	{ 0x3c0b, 0x04 },
	{ 0x3c1c, 0x47 },
	{ 0x3c1d, 0x15 },
	{ 0x3c14, 0x04 },
	{ 0x3c16, 0x00 },
	{ 0x0820, 0x03 },
	{ 0x0821, 0x44 },
	{ 0x0114, 0x01 },
	{ 0x0344, 0x01 },
	{ 0x0345, 0xf8 },
	{ 0x0346, 0x01 },
	{ 0x0347, 0x7c },
	{ 0x0348, 0x08 },
	{ 0x0349, 0x39 },
	{ 0x034a, 0x06 },
	{ 0x034b, 0x2d },
	{ 0x034c, 0x03 },
	{ 0x034d, 0x20 },
	{ 0x034e, 0x02 },
	{ 0x034f, 0x58 },
	{ 0x0900, 0x01 },
	{ 0x0901, 0x22 },
	{ 0x0381, 0x01 },
	{ 0x0383, 0x01 },
	{ 0x0385, 0x01 },
	{ 0x0387, 0x03 },
	{ 0x0340, 0x02 },
	{ 0x0341, 0x96 },
	{ 0x0342, 0x0b },
	{ 0x0343, 0x00 },
	{ 0x0200, 0x00 },
	{ 0x0201, 0x00 },
	{ 0x0202, 0x01 },
	{ 0x0203, 0x2a },
	{ 0x3303, 0x02 },
	{ 0x3400, 0x00 },
	{ 0x323b, 0x02 },
	{ 0x3301, 0x00 },
	{ 0x3321, 0x04 },
	{ 0x3306, 0x01 },
	{ 0x3307, 0xf8 },
	{ 0x3308, 0x08 },
	{ 0x3309, 0x39 },
	{ 0x330a, 0x01 },
	{ 0x330b, 0x01 },
	{ 0x330e, 0x01 },
	{ 0x330f, 0x7c },
	{ 0x3310, 0x06 },
	{ 0x3311, 0x2d },
	{ 0x3312, 0x03 },
	{ 0x3313, 0x01 },
};

static const struct s5k5e8_reg s5k5e8_mode_640x480_regs[] = {
	{ 0x0136, 0x18 },
	{ 0x0137, 0x00 },
	{ 0x0305, 0x06 },
	{ 0x0306, 0x18 },
	{ 0x0307, 0xa8 },
	{ 0x0308, 0x34 },
	{ 0x0309, 0x42 },
	{ 0x3c1f, 0x00 },
	{ 0x3c17, 0x00 },
	{ 0x3c0b, 0x04 },
	{ 0x3c1c, 0x47 },
	{ 0x3c1d, 0x15 },
	{ 0x3c14, 0x04 },
	{ 0x3c16, 0x00 },
	{ 0x0820, 0x03 },
	{ 0x0821, 0x44 },
	{ 0x0114, 0x01 },
	{ 0x0344, 0x00 },
	{ 0x0345, 0x00 },
	{ 0x0346, 0x00 },
	{ 0x0347, 0x00 },
	{ 0x0348, 0x09 },
	{ 0x0349, 0xff },
	{ 0x034a, 0x07 },
	{ 0x034b, 0x7f },
	{ 0x034c, 0x02 },
	{ 0x034d, 0x80 },
	{ 0x034e, 0x01 },
	{ 0x034f, 0xe0 },
	{ 0x0900, 0x01 },
	{ 0x0901, 0x42 },
	{ 0x0381, 0x01 },
	{ 0x0383, 0x01 },
	{ 0x0385, 0x01 },
	{ 0x0387, 0x07 },
	{ 0x0340, 0x01 },
	{ 0x0341, 0xf8 },
	{ 0x0342, 0x0b },
	{ 0x0343, 0x00 },
	{ 0x0200, 0x00 },
	{ 0x0201, 0x00 },
	{ 0x0202, 0x01 },
	{ 0x0203, 0x2a },
	{ 0x3303, 0x02 },
	{ 0x3400, 0x00 },
	{ 0x323b, 0x02 },
	{ 0x3301, 0x00 },
	{ 0x3321, 0x04 },
	{ 0x3306, 0x00 },
	{ 0x3307, 0x00 },
	{ 0x3308, 0x09 },
	{ 0x3309, 0xff },
	{ 0x330a, 0x01 },
	{ 0x330b, 0x01 },
	{ 0x330e, 0x00 },
	{ 0x330f, 0x00 },
	{ 0x3310, 0x07 },
	{ 0x3311, 0x7f },
	{ 0x3312, 0x07 },
	{ 0x3313, 0x01 },
};

static const struct s5k5e8_mode s5k5e8_supported_modes[] = {
	{
		.width = 2592,
		.height = 1944,
		.hts = 2856,
		.vts_def = 1968,
		.exposure_def = 1962,
		.pixel_rate = 167200000,
		.link_freq_index = 0,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(s5k5e8_mode_2592x1944_regs),
			.regs = s5k5e8_mode_2592x1944_regs,
		},
	},
	{
		.width = 1296,
		.height = 972,
		.hts = 5200,
		.vts_def = 1062,
		.exposure_def = 990,
		.pixel_rate = 167200000,
		.link_freq_index = 0,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(s5k5e8_mode_1296x972_regs),
			.regs = s5k5e8_mode_1296x972_regs,
		},
	},
	{
		.width = 1304,
		.height = 980,
		.hts = 2784,
		.vts_def = 1009,
		.exposure_def = 990,
		.pixel_rate = 120000000,
		.link_freq_index = 1,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(s5k5e8_mode_1304x980_regs),
			.regs = s5k5e8_mode_1304x980_regs,
		},
	},
	{
		.width = 800,
		.height = 600,
		.hts = 2816,
		.vts_def = 662,
		.exposure_def = 298,
		.pixel_rate = 167200000,
		.link_freq_index = 0,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(s5k5e8_mode_800x600_regs),
			.regs = s5k5e8_mode_800x600_regs,
		},
	},
	{
		.width = 640,
		.height = 480,
		.hts = 2816,
		.vts_def = 504,
		.exposure_def = 298,
		.pixel_rate = 167200000,
		.link_freq_index = 0,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(s5k5e8_mode_640x480_regs),
			.regs = s5k5e8_mode_640x480_regs,
		},
	},
};

/* Stream-start sequence; includes 0x0100 = 1 and a 1 ms delay. */
static const struct s5k5e8_reg s5k5e8_start_regs[] = {
	{ 0x323b, 0x00 },
	{ 0x3301, 0x00 },
	{ 0x3321, 0x04 },
	{ 0x0101, 0x00 },
	{ 0x3c16, 0x00 },
	{ 0x3c0d, 0x04 },
	{ 0x0100, 0x01, 1 },
	{ 0x3c0d, 0x00 },
};

static int s5k5e8_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5k5e8 *s = to_s5k5e8(sd);
	unsigned int i;
	int ret;

	/* Assert reset, enable rails (5 ms each), then MCLK. */
	gpiod_set_value_cansleep(s->reset_gpio, 1);
	usleep_range(1000, 2000);

	for (i = 0; i < S5K5E8_NUM_SUPPLIES; i++) {
		ret = regulator_enable(s->supplies[i].consumer);
		if (ret)
			goto err_disable;
		usleep_range(5000, 6000);
	}

	ret = clk_prepare_enable(s->xclk);
	if (ret)
		goto err_disable;
	usleep_range(1000, 2000);

	gpiod_set_value_cansleep(s->reset_gpio, 0);
	usleep_range(10000, 12000);

	return 0;

err_disable:
	while (i--)
		regulator_disable(s->supplies[i].consumer);
	return ret;
}

static int s5k5e8_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5k5e8 *s = to_s5k5e8(sd);
	unsigned int i;

	gpiod_set_value_cansleep(s->reset_gpio, 1);
	clk_disable_unprepare(s->xclk);

	for (i = S5K5E8_NUM_SUPPLIES; i--;)
		regulator_disable(s->supplies[i].consumer);

	return 0;
}

static int s5k5e8_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5k5e8 *s = container_of(ctrl->handler, struct s5k5e8, ctrls);
	const struct s5k5e8_mode *mode = s->cur_mode;
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		s64 max = mode->height + ctrl->val - S5K5E8_EXPOSURE_MARGIN;

		__v4l2_ctrl_modify_range(s->exposure, S5K5E8_EXPOSURE_MIN,
					 max, 1,
					 min_t(s64, s->exposure->val, max));
	}

	if (!pm_runtime_get_if_in_use(&s->client->dev))
		return 0;

	/* AEC updates are wrapped in group hold. */
	cci_write(s->regmap, S5K5E8_REG_GROUP_HOLD, 1, &ret);

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		cci_write(s->regmap, S5K5E8_REG_ANALOG_GAIN, ctrl->val, &ret);
		break;
	case V4L2_CID_EXPOSURE:
		cci_write(s->regmap, S5K5E8_REG_EXPOSURE, ctrl->val, &ret);
		break;
	case V4L2_CID_VBLANK:
		cci_write(s->regmap, S5K5E8_REG_VTS,
			  mode->height + ctrl->val, &ret);
		break;
	}

	cci_write(s->regmap, S5K5E8_REG_GROUP_HOLD, 0, &ret);

	pm_runtime_put(&s->client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops s5k5e8_ctrl_ops = {
	.s_ctrl = s5k5e8_set_ctrl,
};

static int s5k5e8_init_controls(struct s5k5e8 *s)
{
	struct v4l2_fwnode_device_properties props;
	const struct s5k5e8_mode *mode = s->cur_mode;
	s64 pixel_rate_min = mode->pixel_rate;
	s64 pixel_rate_max = mode->pixel_rate;
	s64 hblank = mode->hts - mode->width;
	s64 vblank_def = mode->vts_def - mode->height;
	s64 exp_max = mode->vts_def - S5K5E8_EXPOSURE_MARGIN;
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(s5k5e8_supported_modes); i++) {
		pixel_rate_min = min_t(s64, pixel_rate_min,
				       s5k5e8_supported_modes[i].pixel_rate);
		pixel_rate_max = max_t(s64, pixel_rate_max,
				       s5k5e8_supported_modes[i].pixel_rate);
	}

	v4l2_ctrl_handler_init(&s->ctrls, 8);

	s->link_freq = v4l2_ctrl_new_int_menu(&s->ctrls, NULL,
					      V4L2_CID_LINK_FREQ,
					      ARRAY_SIZE(s5k5e8_link_freq_menu) - 1,
					      0, s5k5e8_link_freq_menu);
	if (s->link_freq)
		s->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	s->pixel_rate = v4l2_ctrl_new_std(&s->ctrls, NULL,
					  V4L2_CID_PIXEL_RATE,
					  pixel_rate_min, pixel_rate_max, 1,
					  mode->pixel_rate);

	s->hblank = v4l2_ctrl_new_std(&s->ctrls, NULL, V4L2_CID_HBLANK,
				      hblank, hblank, 1, hblank);
	if (s->hblank)
		s->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	s->vblank = v4l2_ctrl_new_std(&s->ctrls, &s5k5e8_ctrl_ops,
				      V4L2_CID_VBLANK, 0,
				      S5K5E8_VTS_MAX - mode->height, 1,
				      vblank_def);

	s->exposure = v4l2_ctrl_new_std(&s->ctrls, &s5k5e8_ctrl_ops,
					V4L2_CID_EXPOSURE,
					S5K5E8_EXPOSURE_MIN, exp_max, 1,
					min_t(s64, mode->exposure_def, exp_max));

	v4l2_ctrl_new_std(&s->ctrls, &s5k5e8_ctrl_ops,
			  V4L2_CID_ANALOGUE_GAIN, S5K5E8_GAIN_MIN,
			  S5K5E8_GAIN_MAX, 1, S5K5E8_GAIN_DEFAULT);

	if (s->ctrls.error) {
		ret = s->ctrls.error;
		goto err;
	}

	ret = v4l2_fwnode_device_parse(&s->client->dev, &props);
	if (ret)
		goto err;
	ret = v4l2_ctrl_new_fwnode_properties(&s->ctrls, &s5k5e8_ctrl_ops,
					      &props);
	if (ret)
		goto err;

	s->sd.ctrl_handler = &s->ctrls;

	return 0;

err:
	v4l2_ctrl_handler_free(&s->ctrls);
	return ret;
}

static void s5k5e8_fill_fmt(const struct s5k5e8_mode *mode,
			    struct v4l2_mbus_framefmt *f)
{
	f->width = mode->width;
	f->height = mode->height;
	f->code = S5K5E8_MBUS_CODE;
	f->field = V4L2_FIELD_NONE;
	f->colorspace = V4L2_COLORSPACE_RAW;
	f->ycbcr_enc = V4L2_YCBCR_ENC_601;
	f->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	f->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int s5k5e8_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *st,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;

	code->code = S5K5E8_MBUS_CODE;

	return 0;
}

static int s5k5e8_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *st,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(s5k5e8_supported_modes))
		return -EINVAL;
	if (fse->code != S5K5E8_MBUS_CODE)
		return -EINVAL;

	fse->min_width = s5k5e8_supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = s5k5e8_supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int s5k5e8_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *st,
			  struct v4l2_subdev_format *fmt)
{
	struct s5k5e8 *s = to_s5k5e8(sd);
	const struct s5k5e8_mode *mode;
	s64 hblank;
	s64 vblank_def;
	s64 exp_max;

	if (fmt->format.code != S5K5E8_MBUS_CODE)
		fmt->format.code = S5K5E8_MBUS_CODE;

	mode = v4l2_find_nearest_size(s5k5e8_supported_modes,
				      ARRAY_SIZE(s5k5e8_supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	s5k5e8_fill_fmt(mode, &fmt->format);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_state_get_format(st, fmt->pad) = fmt->format;
		return 0;
	}

	s->cur_mode = mode;

	__v4l2_ctrl_s_ctrl(s->link_freq, mode->link_freq_index);
	__v4l2_ctrl_s_ctrl_int64(s->pixel_rate, mode->pixel_rate);

	vblank_def = mode->vts_def - mode->height;
	__v4l2_ctrl_modify_range(s->vblank, 0,
				 S5K5E8_VTS_MAX - mode->height, 1,
				 vblank_def);
	__v4l2_ctrl_s_ctrl(s->vblank, vblank_def);

	hblank = mode->hts - mode->width;
	__v4l2_ctrl_modify_range(s->hblank, hblank, hblank, 1, hblank);

	exp_max = mode->vts_def - S5K5E8_EXPOSURE_MARGIN;
	__v4l2_ctrl_modify_range(s->exposure, S5K5E8_EXPOSURE_MIN, exp_max, 1,
				 min_t(s64, mode->exposure_def, exp_max));

	*v4l2_subdev_state_get_format(st, fmt->pad) = fmt->format;

	return 0;
}

static int s5k5e8_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *st)
{
	struct s5k5e8 *s = to_s5k5e8(sd);

	s5k5e8_fill_fmt(s->cur_mode, v4l2_subdev_state_get_format(st, 0));

	return 0;
}

static int s5k5e8_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *st,
				struct v4l2_subdev_selection *sel)
{
	struct s5k5e8 *s = to_s5k5e8(sd);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r = (struct v4l2_rect){
			0, 0, s->cur_mode->width, s->cur_mode->height
		};
		return 0;
	}

	return -EINVAL;
}

static int s5k5e8_start_streaming(struct s5k5e8 *s)
{
	const struct s5k5e8_mode *mode = s->cur_mode;
	int ret;

	ret = pm_runtime_resume_and_get(&s->client->dev);
	if (ret < 0)
		return ret;

	/* Init table is written once per power-up. */
	ret = s5k5e8_write_table(s, s5k5e8_init_regs,
				 ARRAY_SIZE(s5k5e8_init_regs));
	if (ret)
		goto err_pm;

	ret = s5k5e8_write_table(s, mode->reg_list.regs,
				 mode->reg_list.num_of_regs);
	if (ret)
		goto err_pm;

	/* Apply controls before stream-on. */
	ret = __v4l2_ctrl_handler_setup(&s->ctrls);
	if (ret)
		goto err_pm;

	ret = s5k5e8_write_table(s, s5k5e8_start_regs,
				 ARRAY_SIZE(s5k5e8_start_regs));
	if (ret)
		goto err_pm;

	return 0;

err_pm:
	pm_runtime_put(&s->client->dev);
	return ret;
}

static int s5k5e8_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct s5k5e8 *s = to_s5k5e8(sd);
	struct v4l2_subdev_state *st;
	int ret = 0;

	st = v4l2_subdev_lock_and_get_active_state(sd);

	if (enable) {
		ret = s5k5e8_start_streaming(s);
	} else {
		cci_write(s->regmap, S5K5E8_REG_MODE_SELECT, 0, NULL);
		pm_runtime_put(&s->client->dev);
	}

	v4l2_subdev_unlock_state(st);

	return ret;
}

static const struct v4l2_subdev_video_ops s5k5e8_video_ops = {
	.s_stream = s5k5e8_set_stream,
};

static const struct v4l2_subdev_pad_ops s5k5e8_pad_ops = {
	.enum_mbus_code = s5k5e8_enum_mbus_code,
	.enum_frame_size = s5k5e8_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = s5k5e8_set_fmt,
	.get_selection = s5k5e8_get_selection,
};

static const struct v4l2_subdev_ops s5k5e8_subdev_ops = {
	.video = &s5k5e8_video_ops,
	.pad = &s5k5e8_pad_ops,
};

static const struct v4l2_subdev_internal_ops s5k5e8_internal_ops = {
	.init_state = s5k5e8_init_state,
};

static const struct media_entity_operations s5k5e8_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int s5k5e8_identify(struct s5k5e8 *s)
{
	struct device *dev = &s->client->dev;
	u64 id = 0;
	int ret = 0;

	cci_read(s->regmap, S5K5E8_REG_CHIP_ID, &id, &ret);
	if (ret)
		return dev_err_probe(dev, ret, "chip id read failed\n");

	if (id != S5K5E8_CHIP_ID)
		return dev_err_probe(dev, -ENODEV,
				     "wrong chip id 0x%04llx, expected 0x%04x\n",
				     id, S5K5E8_CHIP_ID);

	return 0;
}

static int s5k5e8_parse_dt(struct s5k5e8 *s)
{
	struct device *dev = &s->client->dev;
	struct v4l2_fwnode_endpoint ep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	struct fwnode_handle *fwnode;
	int ret;

	fwnode = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!fwnode)
		return -EINVAL;

	ret = v4l2_fwnode_endpoint_alloc_parse(fwnode, &ep);
	fwnode_handle_put(fwnode);
	if (ret)
		return ret;

	if (ep.bus.mipi_csi2.num_data_lanes != S5K5E8_DATA_LANES)
		ret = dev_err_probe(dev, -EINVAL, "need %d data lanes\n",
				    S5K5E8_DATA_LANES);

	v4l2_fwnode_endpoint_free(&ep);

	return ret;
}

static int s5k5e8_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct s5k5e8 *s;
	unsigned int i;
	int ret;

	s = devm_kzalloc(dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	s->client = client;
	s->cur_mode = &s5k5e8_supported_modes[0];

	s->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(s->regmap))
		return PTR_ERR(s->regmap);

	ret = s5k5e8_parse_dt(s);
	if (ret)
		return dev_err_probe(dev, ret, "bad endpoint\n");

	s->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(s->xclk))
		return dev_err_probe(dev, PTR_ERR(s->xclk), "no clock\n");

	ret = clk_set_rate(s->xclk, S5K5E8_XCLK_FREQ);
	if (ret)
		return dev_err_probe(dev, ret, "cannot set 24 MHz\n");

	s->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(s->reset_gpio))
		return PTR_ERR(s->reset_gpio);

	for (i = 0; i < S5K5E8_NUM_SUPPLIES; i++)
		s->supplies[i].supply = s5k5e8_supply_names[i];
	ret = devm_regulator_bulk_get(dev, S5K5E8_NUM_SUPPLIES, s->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "supplies\n");

	v4l2_i2c_subdev_init(&s->sd, client, &s5k5e8_subdev_ops);
	s->sd.internal_ops = &s5k5e8_internal_ops;

	ret = s5k5e8_power_on(dev);
	if (ret)
		return ret;

	ret = s5k5e8_identify(s);
	if (ret)
		goto err_power;

	ret = s5k5e8_init_controls(s);
	if (ret)
		goto err_power;

	s->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	s->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	s->sd.entity.ops = &s5k5e8_entity_ops;
	s->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&s->sd.entity, 1, &s->pad);
	if (ret)
		goto err_ctrls;

	s->sd.state_lock = s->ctrls.lock;
	ret = v4l2_subdev_init_finalize(&s->sd);
	if (ret)
		goto err_entity;

	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);

	ret = v4l2_async_register_subdev_sensor(&s->sd);
	if (ret)
		goto err_pm;

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

err_pm:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	v4l2_subdev_cleanup(&s->sd);
err_entity:
	media_entity_cleanup(&s->sd.entity);
err_ctrls:
	v4l2_ctrl_handler_free(&s->ctrls);
err_power:
	s5k5e8_power_off(dev);

	return ret;
}

static void s5k5e8_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k5e8 *s = to_s5k5e8(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&s->ctrls);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		s5k5e8_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static DEFINE_RUNTIME_DEV_PM_OPS(s5k5e8_pm_ops, s5k5e8_power_off,
				 s5k5e8_power_on, NULL);

static const struct of_device_id s5k5e8_of_match[] = {
	{ .compatible = "samsung,s5k5e8" },
	{ }
};
MODULE_DEVICE_TABLE(of, s5k5e8_of_match);

static struct i2c_driver s5k5e8_i2c_driver = {
	.driver = {
		.name = "s5k5e8",
		.pm = pm_ptr(&s5k5e8_pm_ops),
		.of_match_table = s5k5e8_of_match,
	},
	.probe = s5k5e8_probe,
	.remove = s5k5e8_remove,
};
module_i2c_driver(s5k5e8_i2c_driver);

MODULE_AUTHOR("Kulesha Evgeniy <voovdop@gmail.com>");
MODULE_DESCRIPTION("Samsung S5K5E8 sensor driver");
MODULE_LICENSE("GPL");
