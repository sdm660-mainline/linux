// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Kulesha Evgeniy <voovdop@gmail.com>
/*
 * OmniVision OV13855 13MP CMOS image sensor driver
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

#define OV13855_REG_CHIP_ID		CCI_REG16(0x300b)
#define OV13855_CHIP_ID			0xd855

#define OV13855_REG_SOFT_RESET		CCI_REG8(0x0103)
#define OV13855_REG_MODE_SELECT		CCI_REG8(0x0100)
#define OV13855_MODE_STANDBY		0x00
#define OV13855_MODE_STREAMING		0x01

#define OV13855_REG_GROUP_ACCESS	CCI_REG8(0x3208)

#define OV13855_REG_EXPOSURE		CCI_REG24(0x3500)
#define OV13855_REG_ANALOG_GAIN		CCI_REG16(0x3508)
#define OV13855_REG_VTS			CCI_REG16(0x380e)
#define OV13855_VTS_MAX			0x7fff

#define OV13855_EXPOSURE_MIN		4
#define OV13855_EXPOSURE_STEP		1
#define OV13855_EXPOSURE_MARGIN		8

#define OV13855_GAIN_MIN		0x80
#define OV13855_GAIN_MAX		0x7c0
#define OV13855_GAIN_DEFAULT		0x80

#define OV13855_DATA_LANES		4
#define OV13855_XCLK_FREQ		24000000

/* DDR clock in Hz (bit rate / 2). */
#define OV13855_LINK_FREQ_540MHZ	540000000ULL
#define OV13855_LINK_FREQ_270MHZ	270000000ULL

enum {
	OV13855_LINK_FREQ_INDEX_540MHZ,
	OV13855_LINK_FREQ_INDEX_270MHZ,
};

static const s64 ov13855_link_freq_menu[] = {
	OV13855_LINK_FREQ_540MHZ,
	OV13855_LINK_FREQ_270MHZ,
};

#define OV13855_MBUS_CODE		MEDIA_BUS_FMT_SGRBG10_1X10

struct ov13855_reg {
	u16 addr;
	u8 val;
};

struct ov13855_reg_list {
	u32 num_of_regs;
	const struct ov13855_reg *regs;
};

struct ov13855_mode {
	u32 width;
	u32 height;
	u32 hts;		/* pixels per line */
	u32 vts_def;		/* lines per frame */
	u32 exposure_def;	/* default exposure, lines */
	u32 pixel_rate;		/* Hz */
	u32 link_freq_index;
	struct ov13855_reg_list reg_list;
};

static const char * const ov13855_supply_names[] = {
	"dovdd",	/* digital I/O, 1.8 V */
	"dvdd",		/* digital core, 1.2 V */
	"avdd",		/* analog, 2.8 V */
};
#define OV13855_NUM_SUPPLIES ARRAY_SIZE(ov13855_supply_names)

struct ov13855 {
	struct i2c_client *client;
	struct regmap *regmap;
	struct clk *xclk;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[OV13855_NUM_SUPPLIES];

	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *exposure;

	const struct ov13855_mode *cur_mode;
};

static inline struct ov13855 *to_ov13855(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov13855, sd);
}

/*
 * Quantise the gain code to the sensor's steps: 1/8 up to 2x, 1/16 up to
 * 4x, 1/32 up to 8x and 1/64 above.
 */
static u32 ov13855_gain_code(u32 val)
{
	if (val >= OV13855_GAIN_MAX)
		return OV13855_GAIN_MAX;

	if (val < 0x100)
		return val & ~0x7;
	if (val < 0x200)
		return val & ~0xf;
	if (val < 0x400)
		return val & ~0x1f;

	return val & ~0x3f;
}

static int ov13855_write_table(struct ov13855 *s, const struct ov13855_reg *t,
			       unsigned int n)
{
	int ret = 0;
	unsigned int i;

	for (i = 0; i < n && !ret; i++)
		cci_write(s->regmap, CCI_REG8(t[i].addr), t[i].val, &ret);

	return ret;
}


/* 0x0103 soft reset is issued in start_streaming(). */
static const struct ov13855_reg ov13855_init_regs[] = {
	{ 0x0300, 0x02 },
	{ 0x0301, 0x00 },
	{ 0x0302, 0x5a },
	{ 0x0303, 0x00 },
	{ 0x0304, 0x00 },
	{ 0x0305, 0x01 },
	{ 0x030b, 0x06 },
	{ 0x030c, 0x02 },
	{ 0x030d, 0x88 },
	{ 0x0312, 0x11 },
	{ 0x3022, 0x01 },
	{ 0x3013, 0x32 },
	{ 0x3016, 0x72 },
	{ 0x301b, 0xf0 },
	{ 0x301f, 0xd0 },
	{ 0x3106, 0x15 },
	{ 0x3107, 0x23 },
	{ 0x3500, 0x00 },
	{ 0x3501, 0x80 },
	{ 0x3502, 0x00 },
	{ 0x3508, 0x02 },
	{ 0x3509, 0x00 },
	{ 0x350a, 0x00 },
	{ 0x350e, 0x00 },
	{ 0x3510, 0x00 },
	{ 0x3511, 0x02 },
	{ 0x3512, 0x00 },
	{ 0x3600, 0x2b },
	{ 0x3601, 0x52 },
	{ 0x3602, 0x60 },
	{ 0x3612, 0x05 },
	{ 0x3613, 0xa4 },
	{ 0x3620, 0x80 },
	{ 0x3621, 0x10 },
	{ 0x3622, 0x30 },
	{ 0x3624, 0x1c },
	{ 0x3640, 0x10 },
	{ 0x3641, 0x70 },
	{ 0x3661, 0x80 },
	{ 0x3662, 0x12 },
	{ 0x3664, 0x73 },
	{ 0x3665, 0xa7 },
	{ 0x366e, 0xff },
	{ 0x366f, 0xf4 },
	{ 0x3674, 0x00 },
	{ 0x3679, 0x0c },
	{ 0x367f, 0x01 },
	{ 0x3680, 0x0c },
	{ 0x3681, 0x50 },
	{ 0x3682, 0x50 },
	{ 0x3683, 0xa9 },
	{ 0x3684, 0xa9 },
	{ 0x3709, 0x5f },
	{ 0x3714, 0x24 },
	{ 0x371a, 0x3e },
	{ 0x3737, 0x04 },
	{ 0x3738, 0xcc },
	{ 0x3739, 0x12 },
	{ 0x373d, 0x26 },
	{ 0x3764, 0x20 },
	{ 0x3765, 0x20 },
	{ 0x37a1, 0x36 },
	{ 0x37a8, 0x3b },
	{ 0x37ab, 0x31 },
	{ 0x37c2, 0x04 },
	{ 0x37c3, 0xf1 },
	{ 0x37c5, 0x00 },
	{ 0x37d8, 0x03 },
	{ 0x37d9, 0x0c },
	{ 0x37da, 0xc2 },
	{ 0x37dc, 0x02 },
	{ 0x37e0, 0x00 },
	{ 0x37e1, 0x0a },
	{ 0x37e2, 0x14 },
	{ 0x37e3, 0x04 },
	{ 0x37e4, 0x2a },
	{ 0x37e5, 0x03 },
	{ 0x37e6, 0x04 },
	{ 0x3800, 0x00 },
	{ 0x3801, 0x00 },
	{ 0x3802, 0x00 },
	{ 0x3803, 0x08 },
	{ 0x3804, 0x10 },
	{ 0x3805, 0x9f },
	{ 0x3806, 0x0c },
	{ 0x3807, 0x57 },
	{ 0x3808, 0x10 },
	{ 0x3809, 0x80 },
	{ 0x380a, 0x0c },
	{ 0x380b, 0x40 },
	{ 0x380c, 0x04 },
	{ 0x380d, 0x62 },
	{ 0x380e, 0x0c },
	{ 0x380f, 0x8e },
	{ 0x3811, 0x10 },
	{ 0x3813, 0x08 },
	{ 0x3814, 0x01 },
	{ 0x3815, 0x01 },
	{ 0x3816, 0x01 },
	{ 0x3817, 0x01 },
	{ 0x3820, 0xb0 },
	{ 0x3821, 0x00 },
	{ 0x3822, 0xc2 },
	{ 0x3823, 0x18 },
	{ 0x3826, 0x11 },
	{ 0x3827, 0x1c },
	{ 0x3829, 0x03 },
	{ 0x3832, 0x00 },
	{ 0x3c80, 0x00 },
	{ 0x3c87, 0x01 },
	{ 0x3c8c, 0x19 },
	{ 0x3c8d, 0x1c },
	{ 0x3c90, 0x00 },
	{ 0x3c91, 0x00 },
	{ 0x3c92, 0x00 },
	{ 0x3c93, 0x00 },
	{ 0x3c94, 0x40 },
	{ 0x3c95, 0x54 },
	{ 0x3c96, 0x34 },
	{ 0x3c97, 0x04 },
	{ 0x3c98, 0x00 },
	{ 0x3d8c, 0x73 },
	{ 0x3d8d, 0xc0 },
	{ 0x3f00, 0x0b },
	{ 0x3f03, 0x00 },
	{ 0x4001, 0xe0 },
	{ 0x4008, 0x00 },
	{ 0x4009, 0x0f },
	{ 0x4011, 0xf0 },
	{ 0x4017, 0x08 },
	{ 0x4050, 0x04 },
	{ 0x4051, 0x0b },
	{ 0x4052, 0x00 },
	{ 0x4053, 0x80 },
	{ 0x4054, 0x00 },
	{ 0x4055, 0x80 },
	{ 0x4056, 0x00 },
	{ 0x4057, 0x80 },
	{ 0x4058, 0x00 },
	{ 0x4059, 0x80 },
	{ 0x405e, 0x20 },
	{ 0x4500, 0x07 },
	{ 0x4503, 0x00 },
	{ 0x450a, 0x04 },
	{ 0x4809, 0x04 },
	{ 0x480c, 0x12 },
	{ 0x481f, 0x30 },
	{ 0x4833, 0x10 },
	{ 0x4837, 0x0e },
	{ 0x4902, 0x01 },
	{ 0x4d00, 0x03 },
	{ 0x4d01, 0xc9 },
	{ 0x4d02, 0xbc },
	{ 0x4d03, 0xd7 },
	{ 0x4d04, 0xf0 },
	{ 0x4d05, 0xa2 },
	{ 0x5000, 0xed },
	{ 0x5001, 0x07 },
	{ 0x5040, 0x39 },
	{ 0x5041, 0x10 },
	{ 0x5042, 0x10 },
	{ 0x5043, 0x84 },
	{ 0x5044, 0x62 },
	{ 0x5180, 0x00 },
	{ 0x5181, 0x10 },
	{ 0x5182, 0x02 },
	{ 0x5183, 0x0f },
	{ 0x5200, 0x1b },
	{ 0x520b, 0x07 },
	{ 0x520c, 0x0f },
	{ 0x5300, 0x04 },
	{ 0x5301, 0x0c },
	{ 0x5302, 0x0c },
	{ 0x5303, 0x0f },
	{ 0x5304, 0x00 },
	{ 0x5305, 0x70 },
	{ 0x5306, 0x00 },
	{ 0x5307, 0x80 },
	{ 0x5308, 0x00 },
	{ 0x5309, 0xa5 },
	{ 0x530a, 0x00 },
	{ 0x530b, 0xd3 },
	{ 0x530c, 0x00 },
	{ 0x530d, 0xf0 },
	{ 0x530e, 0x01 },
	{ 0x530f, 0x10 },
	{ 0x5310, 0x01 },
	{ 0x5311, 0x20 },
	{ 0x5312, 0x01 },
	{ 0x5313, 0x20 },
	{ 0x5314, 0x01 },
	{ 0x5315, 0x20 },
	{ 0x5316, 0x08 },
	{ 0x5317, 0x08 },
	{ 0x5318, 0x10 },
	{ 0x5319, 0x88 },
	{ 0x531a, 0x88 },
	{ 0x531b, 0xa9 },
	{ 0x531c, 0xaa },
	{ 0x531d, 0x0a },
	{ 0x5405, 0x02 },
	{ 0x5406, 0x67 },
	{ 0x5407, 0x01 },
	{ 0x5408, 0x4a },
};

static const struct ov13855_reg ov13855_mode_4224x3136_regs[] = {
	{ 0x0300, 0x02 },
	{ 0x0301, 0x00 },
	{ 0x0302, 0x5a },
	{ 0x0303, 0x00 },
	{ 0x0304, 0x00 },
	{ 0x0305, 0x01 },
	{ 0x3022, 0x01 },
	{ 0x3013, 0x32 },
	{ 0x3016, 0x72 },
	{ 0x301b, 0xf0 },
	{ 0x301f, 0xd0 },
	{ 0x3106, 0x15 },
	{ 0x3107, 0x23 },
	{ 0x3500, 0x00 },
	{ 0x3501, 0xc8 },
	{ 0x3502, 0x60 },
	{ 0x3622, 0x30 },
	{ 0x3624, 0x1c },
	{ 0x3662, 0x12 },
	{ 0x3709, 0x68 },
	{ 0x3714, 0x24 },
	{ 0x3737, 0x04 },
	{ 0x3739, 0x12 },
	{ 0x37a1, 0x36 },
	{ 0x37a8, 0x3b },
	{ 0x37ab, 0x31 },
	{ 0x37c2, 0x04 },
	{ 0x37d9, 0x0c },
	{ 0x37e1, 0x0a },
	{ 0x37e2, 0x14 },
	{ 0x37e3, 0x04 },
	{ 0x37e4, 0x2a },
	{ 0x37e5, 0x03 },
	{ 0x37e6, 0x04 },
	{ 0x3800, 0x00 },
	{ 0x3801, 0x00 },
	{ 0x3802, 0x00 },
	{ 0x3803, 0x08 },
	{ 0x3804, 0x10 },
	{ 0x3805, 0x9f },
	{ 0x3806, 0x0c },
	{ 0x3807, 0x57 },
	{ 0x3808, 0x10 },
	{ 0x3809, 0x80 },
	{ 0x380a, 0x0c },
	{ 0x380b, 0x40 },
	{ 0x380c, 0x04 },
	{ 0x380d, 0x62 },
	{ 0x380e, 0x0c },
	{ 0x380f, 0x8e },
	{ 0x3811, 0x10 },
	{ 0x3812, 0x00 },
	{ 0x3813, 0x08 },
	{ 0x3814, 0x01 },
	{ 0x3815, 0x01 },
	{ 0x3816, 0x01 },
	{ 0x3817, 0x01 },
	{ 0x3820, 0xb0 },
	{ 0x3821, 0x00 },
	{ 0x3826, 0x11 },
	{ 0x3827, 0x1c },
	{ 0x3829, 0x03 },
	{ 0x3f03, 0x00 },
	{ 0x4009, 0x0f },
	{ 0x4011, 0xf0 },
	{ 0x4050, 0x04 },
	{ 0x4051, 0x0b },
	{ 0x4500, 0x07 },
	{ 0x4837, 0x0e },
	{ 0x4902, 0x01 },
	{ 0x4d00, 0x03 },
	{ 0x4d01, 0xc9 },
	{ 0x4d02, 0xbc },
	{ 0x4d03, 0xd7 },
	{ 0x4d04, 0xf0 },
	{ 0x4d05, 0xa2 },
	{ 0x5000, 0xed },
	{ 0x5041, 0x10 },
	{ 0x5042, 0x10 },
	{ 0x5043, 0x84 },
	{ 0x5044, 0x62 },
	{ 0x5300, 0x04 },
	{ 0x5301, 0x0c },
	{ 0x5302, 0x0c },
	{ 0x5303, 0x0f },
	{ 0x5305, 0x70 },
	{ 0x5307, 0x80 },
	{ 0x5309, 0xa5 },
	{ 0x530b, 0xd3 },
	{ 0x5319, 0x88 },
	{ 0x531a, 0x88 },
	{ 0x531b, 0xa9 },
	{ 0x531c, 0xaa },
	{ 0x531d, 0x0a },
	{ 0x5405, 0x02 },
	{ 0x5406, 0x67 },
	{ 0x5407, 0x01 },
	{ 0x5408, 0x4a },
};

static const struct ov13855_reg ov13855_mode_2112x1568_regs[] = {
	{ 0x0300, 0x02 },
	{ 0x0301, 0x00 },
	{ 0x0302, 0x5a },
	{ 0x0303, 0x01 },
	{ 0x0304, 0x00 },
	{ 0x0305, 0x01 },
	{ 0x3022, 0x01 },
	{ 0x3013, 0x32 },
	{ 0x3016, 0x72 },
	{ 0x301b, 0xf0 },
	{ 0x301f, 0xd0 },
	{ 0x3106, 0x15 },
	{ 0x3107, 0x23 },
	{ 0x3500, 0x00 },
	{ 0x3501, 0x64 },
	{ 0x3502, 0x00 },
	{ 0x3622, 0x30 },
	{ 0x3624, 0x1c },
	{ 0x3662, 0x10 },
	{ 0x3709, 0x5f },
	{ 0x3714, 0x28 },
	{ 0x3737, 0x08 },
	{ 0x3739, 0x20 },
	{ 0x37a1, 0x36 },
	{ 0x37a8, 0x3b },
	{ 0x37ab, 0x31 },
	{ 0x37c2, 0x14 },
	{ 0x37d9, 0x0c },
	{ 0x37e1, 0x0a },
	{ 0x37e2, 0x14 },
	{ 0x37e3, 0x08 },
	{ 0x37e4, 0x38 },
	{ 0x37e5, 0x03 },
	{ 0x37e6, 0x08 },
	{ 0x3800, 0x00 },
	{ 0x3801, 0x00 },
	{ 0x3802, 0x00 },
	{ 0x3803, 0x08 },
	{ 0x3804, 0x10 },
	{ 0x3805, 0x9f },
	{ 0x3806, 0x0c },
	{ 0x3807, 0x4f },
	{ 0x3808, 0x08 },
	{ 0x3809, 0x40 },
	{ 0x380a, 0x06 },
	{ 0x380b, 0x20 },
	{ 0x380c, 0x04 },
	{ 0x380d, 0x62 },
	{ 0x380e, 0x0c },
	{ 0x380f, 0x8e },
	{ 0x3811, 0x08 },
	{ 0x3812, 0x00 },
	{ 0x3813, 0x02 },
	{ 0x3814, 0x03 },
	{ 0x3815, 0x01 },
	{ 0x3816, 0x03 },
	{ 0x3817, 0x01 },
	{ 0x3820, 0xb3 },
	{ 0x3821, 0x00 },
	{ 0x3826, 0x04 },
	{ 0x3827, 0x90 },
	{ 0x3829, 0x07 },
	{ 0x3f03, 0x00 },
	{ 0x4009, 0x0d },
	{ 0x4011, 0xf0 },
	{ 0x4050, 0x04 },
	{ 0x4051, 0x0b },
	{ 0x4500, 0x07 },
	{ 0x4837, 0x1c },
	{ 0x4902, 0x01 },
	{ 0x4d00, 0x03 },
	{ 0x4d01, 0xc9 },
	{ 0x4d02, 0xbc },
	{ 0x4d03, 0xd7 },
	{ 0x4d04, 0xf0 },
	{ 0x4d05, 0xa2 },
	{ 0x5000, 0xed },
	{ 0x5041, 0x10 },
	{ 0x5042, 0x10 },
	{ 0x5043, 0x84 },
	{ 0x5044, 0x62 },
	{ 0x5300, 0x04 },
	{ 0x5301, 0x0c },
	{ 0x5302, 0x0c },
	{ 0x5303, 0x0f },
	{ 0x5305, 0x70 },
	{ 0x5307, 0x80 },
	{ 0x5309, 0xa5 },
	{ 0x530b, 0xd3 },
	{ 0x5319, 0x88 },
	{ 0x531a, 0x88 },
	{ 0x531b, 0xa9 },
	{ 0x531c, 0xaa },
	{ 0x531d, 0x0a },
	{ 0x5405, 0x02 },
	{ 0x5406, 0x67 },
	{ 0x5407, 0x01 },
	{ 0x5408, 0x4a },
};

static const struct ov13855_mode ov13855_supported_modes[] = {
	{
		.width = 4224,
		.height = 3136,
		.hts = 4488,
		.vts_def = 3214,
		.exposure_def = 3206,
		.pixel_rate = 432000000,
		.link_freq_index = 0,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(ov13855_mode_4224x3136_regs),
			.regs = ov13855_mode_4224x3136_regs,
		},
	},
	{
		.width = 2112,
		.height = 1568,
		.hts = 2244,
		.vts_def = 3214,
		.exposure_def = 1600,
		.pixel_rate = 216000000,
		.link_freq_index = 1,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(ov13855_mode_2112x1568_regs),
			.regs = ov13855_mode_2112x1568_regs,
		},
	},
};

static int ov13855_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov13855 *s = to_ov13855(sd);
	int ret;

	/* Power-up: assert reset, enable rails, then MCLK. */
	gpiod_set_value_cansleep(s->reset_gpio, 1);

	ret = regulator_bulk_enable(OV13855_NUM_SUPPLIES, s->supplies);
	if (ret)
		return ret;
	usleep_range(2000, 3000);

	ret = clk_prepare_enable(s->xclk);
	if (ret) {
		regulator_bulk_disable(OV13855_NUM_SUPPLIES, s->supplies);
		return ret;
	}
	usleep_range(1000, 2000);

	gpiod_set_value_cansleep(s->reset_gpio, 0);
	usleep_range(10000, 12000);

	return 0;
}

static int ov13855_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov13855 *s = to_ov13855(sd);

	clk_disable_unprepare(s->xclk);
	gpiod_set_value_cansleep(s->reset_gpio, 1);
	regulator_bulk_disable(OV13855_NUM_SUPPLIES, s->supplies);

	return 0;
}

static int ov13855_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov13855 *s = container_of(ctrl->handler, struct ov13855, ctrls);
	const struct ov13855_mode *mode = s->cur_mode;
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		s64 max = mode->height + ctrl->val - OV13855_EXPOSURE_MARGIN;

		__v4l2_ctrl_modify_range(s->exposure, OV13855_EXPOSURE_MIN,
					 max, OV13855_EXPOSURE_STEP,
					 min_t(s64, s->exposure->val, max));
	}

	if (!pm_runtime_get_if_in_use(&s->client->dev))
		return 0;

	/* Start group access. */
	cci_write(s->regmap, OV13855_REG_GROUP_ACCESS, 0x00, &ret);

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		cci_write(s->regmap, OV13855_REG_ANALOG_GAIN,
			  ov13855_gain_code(ctrl->val), &ret);
		break;
	case V4L2_CID_EXPOSURE:
		cci_write(s->regmap, OV13855_REG_EXPOSURE,
			  (u32)ctrl->val << 4, &ret);
		break;
	case V4L2_CID_VBLANK:
		cci_write(s->regmap, OV13855_REG_VTS,
			  mode->height + ctrl->val, &ret);
		break;
	}

	cci_write(s->regmap, OV13855_REG_GROUP_ACCESS, 0x10, &ret);
	cci_write(s->regmap, OV13855_REG_GROUP_ACCESS, 0xa0, &ret);

	pm_runtime_put(&s->client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov13855_ctrl_ops = {
	.s_ctrl = ov13855_set_ctrl,
};

static int ov13855_init_controls(struct ov13855 *s)
{
	struct v4l2_fwnode_device_properties props;
	const struct ov13855_mode *mode = s->cur_mode;
	s64 pixel_rate_min = mode->pixel_rate;
	s64 pixel_rate_max = mode->pixel_rate;
	s64 hblank = mode->hts - mode->width;
	s64 vblank_def = mode->vts_def - mode->height;
	s64 exp_max = mode->vts_def - OV13855_EXPOSURE_MARGIN;
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(ov13855_supported_modes); i++) {
		pixel_rate_min = min_t(s64, pixel_rate_min,
				       ov13855_supported_modes[i].pixel_rate);
		pixel_rate_max = max_t(s64, pixel_rate_max,
				       ov13855_supported_modes[i].pixel_rate);
	}

	v4l2_ctrl_handler_init(&s->ctrls, 8);

	s->link_freq = v4l2_ctrl_new_int_menu(&s->ctrls, NULL,
					      V4L2_CID_LINK_FREQ,
					      ARRAY_SIZE(ov13855_link_freq_menu) - 1,
					      0, ov13855_link_freq_menu);
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

	s->vblank = v4l2_ctrl_new_std(&s->ctrls, &ov13855_ctrl_ops,
				      V4L2_CID_VBLANK, 0,
				      OV13855_VTS_MAX - mode->height, 1,
				      vblank_def);

	s->exposure = v4l2_ctrl_new_std(&s->ctrls, &ov13855_ctrl_ops,
					V4L2_CID_EXPOSURE,
					OV13855_EXPOSURE_MIN, exp_max,
					OV13855_EXPOSURE_STEP,
					min_t(s64, mode->exposure_def, exp_max));

	v4l2_ctrl_new_std(&s->ctrls, &ov13855_ctrl_ops,
			  V4L2_CID_ANALOGUE_GAIN, OV13855_GAIN_MIN,
			  OV13855_GAIN_MAX, 1, OV13855_GAIN_DEFAULT);

	if (s->ctrls.error) {
		ret = s->ctrls.error;
		goto err;
	}

	ret = v4l2_fwnode_device_parse(&s->client->dev, &props);
	if (ret)
		goto err;
	ret = v4l2_ctrl_new_fwnode_properties(&s->ctrls, &ov13855_ctrl_ops,
					      &props);
	if (ret)
		goto err;

	s->sd.ctrl_handler = &s->ctrls;

	return 0;

err:
	v4l2_ctrl_handler_free(&s->ctrls);
	return ret;
}

static void ov13855_fill_fmt(const struct ov13855_mode *mode,
			     struct v4l2_mbus_framefmt *f)
{
	f->width = mode->width;
	f->height = mode->height;
	f->code = OV13855_MBUS_CODE;
	f->field = V4L2_FIELD_NONE;
	f->colorspace = V4L2_COLORSPACE_RAW;
	f->ycbcr_enc = V4L2_YCBCR_ENC_601;
	f->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	f->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int ov13855_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *st,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;

	code->code = OV13855_MBUS_CODE;

	return 0;
}

static int ov13855_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *st,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(ov13855_supported_modes))
		return -EINVAL;
	if (fse->code != OV13855_MBUS_CODE)
		return -EINVAL;

	fse->min_width = ov13855_supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = ov13855_supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int ov13855_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *st,
			   struct v4l2_subdev_format *fmt)
{
	struct ov13855 *s = to_ov13855(sd);
	const struct ov13855_mode *mode;
	s64 hblank;
	s64 vblank_def;
	s64 exp_max;

	if (fmt->format.code != OV13855_MBUS_CODE)
		fmt->format.code = OV13855_MBUS_CODE;

	mode = v4l2_find_nearest_size(ov13855_supported_modes,
				      ARRAY_SIZE(ov13855_supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	ov13855_fill_fmt(mode, &fmt->format);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_state_get_format(st, fmt->pad) = fmt->format;
		return 0;
	}

	s->cur_mode = mode;

	__v4l2_ctrl_s_ctrl(s->link_freq, mode->link_freq_index);
	__v4l2_ctrl_s_ctrl_int64(s->pixel_rate, mode->pixel_rate);

	vblank_def = mode->vts_def - mode->height;
	__v4l2_ctrl_modify_range(s->vblank, 0,
				 OV13855_VTS_MAX - mode->height, 1,
				 vblank_def);
	__v4l2_ctrl_s_ctrl(s->vblank, vblank_def);

	hblank = mode->hts - mode->width;
	__v4l2_ctrl_modify_range(s->hblank, hblank, hblank, 1, hblank);

	exp_max = mode->vts_def - OV13855_EXPOSURE_MARGIN;
	__v4l2_ctrl_modify_range(s->exposure, OV13855_EXPOSURE_MIN, exp_max,
				 OV13855_EXPOSURE_STEP,
				 min_t(s64, mode->exposure_def, exp_max));

	*v4l2_subdev_state_get_format(st, fmt->pad) = fmt->format;

	return 0;
}

static int ov13855_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *st)
{
	struct ov13855 *s = to_ov13855(sd);

	ov13855_fill_fmt(s->cur_mode, v4l2_subdev_state_get_format(st, 0));

	return 0;
}

static int ov13855_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *st,
				 struct v4l2_subdev_selection *sel)
{
	struct ov13855 *s = to_ov13855(sd);

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

static int ov13855_start_streaming(struct ov13855 *s)
{
	const struct ov13855_mode *mode = s->cur_mode;
	int ret;

	ret = pm_runtime_resume_and_get(&s->client->dev);
	if (ret < 0)
		return ret;

	/* Soft reset, then the required 5 ms delay. */
	ret = cci_write(s->regmap, OV13855_REG_SOFT_RESET, 0x01, NULL);
	if (ret)
		goto err_pm;
	usleep_range(5000, 6000);

	ret = ov13855_write_table(s, ov13855_init_regs,
				  ARRAY_SIZE(ov13855_init_regs));
	if (ret)
		goto err_pm;

	ret = ov13855_write_table(s, mode->reg_list.regs,
				  mode->reg_list.num_of_regs);
	if (ret)
		goto err_pm;

	ret = __v4l2_ctrl_handler_setup(&s->ctrls);
	if (ret)
		goto err_pm;

	ret = cci_write(s->regmap, OV13855_REG_MODE_SELECT,
			OV13855_MODE_STREAMING, NULL);
	if (ret)
		goto err_pm;

	return 0;

err_pm:
	pm_runtime_put(&s->client->dev);
	return ret;
}

static int ov13855_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov13855 *s = to_ov13855(sd);
	struct v4l2_subdev_state *st;
	int ret = 0;

	st = v4l2_subdev_lock_and_get_active_state(sd);

	if (enable) {
		ret = ov13855_start_streaming(s);
	} else {
		cci_write(s->regmap, OV13855_REG_MODE_SELECT,
			  OV13855_MODE_STANDBY, NULL);
		pm_runtime_put(&s->client->dev);
	}

	v4l2_subdev_unlock_state(st);

	return ret;
}

static const struct v4l2_subdev_video_ops ov13855_video_ops = {
	.s_stream = ov13855_set_stream,
};

static const struct v4l2_subdev_pad_ops ov13855_pad_ops = {
	.enum_mbus_code = ov13855_enum_mbus_code,
	.enum_frame_size = ov13855_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ov13855_set_fmt,
	.get_selection = ov13855_get_selection,
};

static const struct v4l2_subdev_ops ov13855_subdev_ops = {
	.video = &ov13855_video_ops,
	.pad = &ov13855_pad_ops,
};

static const struct v4l2_subdev_internal_ops ov13855_internal_ops = {
	.init_state = ov13855_init_state,
};

static const struct media_entity_operations ov13855_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int ov13855_identify(struct ov13855 *s)
{
	struct device *dev = &s->client->dev;
	u64 id = 0;
	int ret = 0;

	cci_read(s->regmap, OV13855_REG_CHIP_ID, &id, &ret);
	if (ret)
		return dev_err_probe(dev, ret, "chip id read failed\n");

	if (id != OV13855_CHIP_ID)
		return dev_err_probe(dev, -ENODEV,
				     "wrong chip id 0x%04llx, expected 0x%04x\n",
				     id, OV13855_CHIP_ID);

	return 0;
}

static int ov13855_parse_dt(struct ov13855 *s)
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

	if (ep.bus.mipi_csi2.num_data_lanes != OV13855_DATA_LANES)
		ret = dev_err_probe(dev, -EINVAL, "need %d data lanes\n",
				    OV13855_DATA_LANES);

	v4l2_fwnode_endpoint_free(&ep);

	return ret;
}

static int ov13855_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ov13855 *s;
	unsigned int i;
	int ret;

	s = devm_kzalloc(dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	s->client = client;
	s->cur_mode = &ov13855_supported_modes[0];

	s->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(s->regmap))
		return PTR_ERR(s->regmap);

	ret = ov13855_parse_dt(s);
	if (ret)
		return dev_err_probe(dev, ret, "bad endpoint\n");

	s->xclk = devm_clk_get(dev, NULL);
	if (IS_ERR(s->xclk))
		return dev_err_probe(dev, PTR_ERR(s->xclk), "no clock\n");

	ret = clk_set_rate(s->xclk, OV13855_XCLK_FREQ);
	if (ret)
		return dev_err_probe(dev, ret, "cannot set 24 MHz\n");

	s->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(s->reset_gpio))
		return PTR_ERR(s->reset_gpio);

	for (i = 0; i < OV13855_NUM_SUPPLIES; i++)
		s->supplies[i].supply = ov13855_supply_names[i];
	ret = devm_regulator_bulk_get(dev, OV13855_NUM_SUPPLIES, s->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "supplies\n");

	v4l2_i2c_subdev_init(&s->sd, client, &ov13855_subdev_ops);
	s->sd.internal_ops = &ov13855_internal_ops;

	ret = ov13855_power_on(dev);
	if (ret)
		return ret;

	ret = ov13855_identify(s);
	if (ret)
		goto err_power;

	ret = ov13855_init_controls(s);
	if (ret)
		goto err_power;

	s->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	s->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	s->sd.entity.ops = &ov13855_entity_ops;
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
	ov13855_power_off(dev);

	return ret;
}

static void ov13855_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov13855 *s = to_ov13855(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&s->ctrls);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		ov13855_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static DEFINE_RUNTIME_DEV_PM_OPS(ov13855_pm_ops, ov13855_power_off,
				 ov13855_power_on, NULL);

static const struct of_device_id ov13855_of_match[] = {
	{ .compatible = "ovti,ov13855" },
	{ }
};
MODULE_DEVICE_TABLE(of, ov13855_of_match);

static struct i2c_driver ov13855_i2c_driver = {
	.driver = {
		.name = "ov13855",
		.pm = pm_ptr(&ov13855_pm_ops),
		.of_match_table = ov13855_of_match,
	},
	.probe = ov13855_probe,
	.remove = ov13855_remove,
};
module_i2c_driver(ov13855_i2c_driver);

MODULE_AUTHOR("Kulesha Evgeniy <voovdop@gmail.com>");
MODULE_DESCRIPTION("Omnivision OV13855 sensor driver");
MODULE_LICENSE("GPL");
