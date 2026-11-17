// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017 Intel Corporation.

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>

#define OV13855_REG_VALUE_08BIT		1
#define OV13855_REG_VALUE_16BIT		2
#define OV13855_REG_VALUE_24BIT		3

#define OV13855_REG_MODE_SELECT		0x0100
#define OV13855_MODE_STANDBY		0x00
#define OV13855_MODE_STREAMING		0x01

#define OV13855_REG_SOFTWARE_RST	0x0103
#define OV13855_SOFTWARE_RST		0x01

/* PLL1 generates PCLK and MIPI_PHY_CLK */
#define OV13855_REG_PLL1_CTRL_0		0x0300
#define OV13855_REG_PLL1_CTRL_1		0x0301
#define OV13855_REG_PLL1_CTRL_2		0x0302
#define OV13855_REG_PLL1_CTRL_3		0x0303
#define OV13855_REG_PLL1_CTRL_4		0x0304
#define OV13855_REG_PLL1_CTRL_5		0x0305

/* PLL2 generates DAC_CLK, SCLK and SRAM_CLK */
#define OV13855_REG_PLL2_CTRL_B		0x030b
#define OV13855_REG_PLL2_CTRL_C		0x030c
#define OV13855_REG_PLL2_CTRL_D		0x030d
#define OV13855_REG_PLL2_CTRL_E		0x030e
#define OV13855_REG_PLL2_CTRL_F		0x030f
#define OV13855_REG_PLL2_CTRL_12	0x0312
#define OV13855_REG_MIPI_SC_CTRL0	0x3016
#define OV13855_REG_MIPI_SC_CTRL1	0x3022

/* Chip ID */
#define OV13855_REG_CHIP_ID		0x300a
#define OV13855_CHIP_ID			0x00d855

/* V_TIMING internal */
#define OV13855_REG_VTS			0x380e
#define OV13855_VTS_30FPS		0x0c8e /* 30 fps */
#define OV13855_VTS_60FPS		0x0648 /* 60 fps */
#define OV13855_VTS_MAX			0x7fff

/* HBLANK control - read only */
#define OV13855_PPL_270MHZ		2244
#define OV13855_PPL_540MHZ		4488

/* Exposure control */
#define OV13855_REG_EXPOSURE		0x3500
#define OV13855_EXPOSURE_MIN		4
#define OV13855_EXPOSURE_STEP		1
#define OV13855_EXPOSURE_DEFAULT	0x640

/* Analog gain control */
#define OV13855_REG_ANALOG_GAIN		0x3508
#define OV13855_ANA_GAIN_MIN		0
#define OV13855_ANA_GAIN_MAX		0x1fff
#define OV13855_ANA_GAIN_STEP		1
#define OV13855_ANA_GAIN_DEFAULT	0x80

/* Digital gain control */
#define OV13855_REG_B_MWB_GAIN		0x5100
#define OV13855_REG_G_MWB_GAIN		0x5102
#define OV13855_REG_R_MWB_GAIN		0x5104
#define OV13855_DGTL_GAIN_MIN		0
#define OV13855_DGTL_GAIN_MAX		16384	/* Max = 16 X */
#define OV13855_DGTL_GAIN_DEFAULT	1024	/* Default gain = 1 X */
#define OV13855_DGTL_GAIN_STEP		1	/* Each step = 1/1024 */

/* Test Pattern Control */
#define OV13855_REG_TEST_PATTERN	0x4503
#define OV13855_TEST_PATTERN_ENABLE	BIT(7)
#define OV13855_TEST_PATTERN_MASK	0xfc

/* Number of frames to skip */
#define OV13855_NUM_OF_SKIP_FRAMES	2

struct ov13855_reg {
	u16 address;
	u8 val;
};

struct ov13855_reg_list {
	u32 num_of_regs;
	const struct ov13855_reg *regs;
};

/* Link frequency config */
struct ov13855_link_freq_config {
	u32 pixels_per_line;

	/* PLL registers for this link frequency */
	struct ov13855_reg_list reg_list;
};

/* Mode : resolution and related config&values */
struct ov13855_mode {
	/* Frame width */
	u32 width;
	/* Frame height */
	u32 height;

	/* V-timing */
	u32 vts_def;
	u32 vts_min;

	/* Index of Link frequency config to be used */
	u32 link_freq_index;
	/* Default register values */
	struct ov13855_reg_list reg_list;
};

/* 4224x3136 needs 1080Mbps/lane, 4 lanes */
/* PLL1/PLL2 settings for 1080Mbps/lane, 4 lanes (from vendor lib) */
static const struct ov13855_reg mipi_data_rate_1080mbps[] = {
	{0x0300, 0x02},
	{0x0301, 0x00},
	{0x0302, 0x5c},
	{0x0303, 0x00},
	{0x0304, 0x00},
	{0x0305, 0x01},
	{0x030b, 0x06},
	{0x030c, 0x02},
	{0x030d, 0x88},
	{0x0312, 0x11},
};


/* 4224x3136: common init + vendor mode delta */
static const struct ov13855_reg mode_4224x3136_regs[] = {
	{0x0103, 0x01},
	{0x0300, 0x02},
	{0x0301, 0x00},
	{0x0302, 0x5c},
	{0x0303, 0x00},
	{0x0304, 0x00},
	{0x0305, 0x01},
	{0x030b, 0x06},
	{0x030c, 0x02},
	{0x030d, 0x88},
	{0x0312, 0x11},
	{0x3022, 0x01},
	{0x3012, 0x40},
	{0x3013, 0x72},
	{0x3016, 0x72},
	{0x301b, 0xf0},
	{0x301f, 0xd0},
	{0x3106, 0x15},
	{0x3107, 0x23},
	{0x3500, 0x00},
	{0x3501, 0x80},
	{0x3502, 0x00},
	{0x3508, 0x02},
	{0x3509, 0x00},
	{0x350a, 0x00},
	{0x350e, 0x00},
	{0x3510, 0x00},
	{0x3511, 0x02},
	{0x3512, 0x00},
	{0x3600, 0x2b},
	{0x3601, 0x52},
	{0x3602, 0x60},
	{0x3612, 0x05},
	{0x3613, 0xa4},
	{0x3620, 0x80},
	{0x3621, 0x10},
	{0x3622, 0x30},
	{0x3624, 0x1c},
	{0x3640, 0x10},
	{0x3641, 0x70},
	{0x3660, 0x04},
	{0x3661, 0x80},
	{0x3662, 0x12},
	{0x3664, 0x73},
	{0x3665, 0xa7},
	{0x366e, 0xff},
	{0x366f, 0xf4},
	{0x3674, 0x00},
	{0x3679, 0x0c},
	{0x367f, 0x01},
	{0x3680, 0x0c},
	{0x3681, 0x50},
	{0x3682, 0x50},
	{0x3683, 0xa9},
	{0x3684, 0xa9},
	{0x3706, 0x40},
	{0x3709, 0x5f},
	{0x3714, 0x24},
	{0x371a, 0x3e},
	{0x3737, 0x04},
	{0x3738, 0xcc},
	{0x3739, 0x12},
	{0x373d, 0x26},
	{0x3764, 0x20},
	{0x3765, 0x20},
	{0x37a1, 0x36},
	{0x37a8, 0x3b},
	{0x37ab, 0x31},
	{0x37c2, 0x04},
	{0x37c3, 0xf1},
	{0x37c5, 0x00},
	{0x37d8, 0x03},
	{0x37d9, 0x0c},
	{0x37da, 0xc2},
	{0x37dc, 0x02},
	{0x37e0, 0x00},
	{0x37e1, 0x0a},
	{0x37e2, 0x14},
	{0x37e3, 0x04},
	{0x37e4, 0x2a},
	{0x37e5, 0x03},
	{0x37e6, 0x04},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x08},
	{0x3804, 0x10},
	{0x3805, 0x9f},
	{0x3806, 0x0c},
	{0x3807, 0x57},
	{0x3808, 0x10},
	{0x3809, 0x80},
	{0x380a, 0x0c},
	{0x380b, 0x40},
	{0x380c, 0x04},
	{0x380d, 0x62},
	{0x380e, 0x0c},
	{0x380f, 0x8e},
	{0x3811, 0x10},
	{0x3813, 0x08},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
	{0x3820, 0xa8},
	{0x3821, 0x00},
	{0x3822, 0xc2},
	{0x3823, 0x18},
	{0x3826, 0x11},
	{0x3827, 0x1c},
	{0x3829, 0x03},
	{0x3832, 0x00},
	{0x3c80, 0x00},
	{0x3c87, 0x01},
	{0x3c8c, 0x19},
	{0x3c8d, 0x1c},
	{0x3c90, 0x00},
	{0x3c91, 0x00},
	{0x3c92, 0x00},
	{0x3c93, 0x00},
	{0x3c94, 0x40},
	{0x3c95, 0x54},
	{0x3c96, 0x34},
	{0x3c97, 0x04},
	{0x3c98, 0x00},
	{0x3d8c, 0x73},
	{0x3d8d, 0xc0},
	{0x3f00, 0x0b},
	{0x3f03, 0x00},
	{0x4001, 0xe0},
	{0x4008, 0x00},
	{0x4009, 0x0f},
	{0x4011, 0xf0},
	{0x4017, 0x08},
	{0x4050, 0x04},
	{0x4051, 0x0b},
	{0x4052, 0x00},
	{0x4053, 0x80},
	{0x4054, 0x00},
	{0x4055, 0x80},
	{0x4056, 0x00},
	{0x4057, 0x80},
	{0x4058, 0x00},
	{0x4059, 0x80},
	{0x405e, 0x00},
	{0x4500, 0x07},
	{0x4503, 0x00},
	{0x450a, 0x04},
	{0x4809, 0x04},
	{0x480c, 0x12},
	{0x481f, 0x30},
	{0x4833, 0x10},
	{0x4837, 0x0e},
	{0x4902, 0x01},
	{0x4d00, 0x03},
	{0x4d01, 0xc9},
	{0x4d02, 0xbc},
	{0x4d03, 0xd7},
	{0x4d04, 0xf0},
	{0x4d05, 0xa2},
	{0x5000, 0xff},
	{0x5001, 0x07},
	{0x5040, 0x39},
	{0x5041, 0x10},
	{0x5042, 0x10},
	{0x5043, 0x84},
	{0x5044, 0x62},
	{0x5180, 0x00},
	{0x5181, 0x10},
	{0x5182, 0x02},
	{0x5183, 0x0f},
	{0x5200, 0x1b},
	{0x520b, 0x07},
	{0x520c, 0x0f},
	{0x5300, 0x04},
	{0x5301, 0x0c},
	{0x5302, 0x0c},
	{0x5303, 0x0f},
	{0x5304, 0x00},
	{0x5305, 0x70},
	{0x5306, 0x00},
	{0x5307, 0x80},
	{0x5308, 0x00},
	{0x5309, 0xa5},
	{0x530a, 0x00},
	{0x530b, 0xd3},
	{0x530c, 0x00},
	{0x530d, 0xf0},
	{0x530e, 0x01},
	{0x530f, 0x10},
	{0x5310, 0x01},
	{0x5311, 0x20},
	{0x5312, 0x01},
	{0x5313, 0x20},
	{0x5314, 0x01},
	{0x5315, 0x20},
	{0x5316, 0x08},
	{0x5317, 0x08},
	{0x5318, 0x10},
	{0x5319, 0x88},
	{0x531a, 0x88},
	{0x531b, 0xa9},
	{0x531c, 0xaa},
	{0x531d, 0x0a},
	{0x5405, 0x02},
	{0x5406, 0x67},
	{0x5407, 0x01},
	{0x5408, 0x4a},
	{0x0303, 0x00},
	{0x3501, 0x80},
	{0x3662, 0x12},
	{0x3714, 0x24},
	{0x3737, 0x04},
	{0x3739, 0x12},
	{0x37c2, 0x04},
	{0x37d9, 0x0c},
	{0x37e3, 0x04},
	{0x37e4, 0x2a},
	{0x37e6, 0x04},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x08},
	{0x3805, 0x9f},
	{0x3806, 0x0c},
	{0x3807, 0x57},
	{0x3808, 0x10},
	{0x3809, 0x80},
	{0x380a, 0x0c},
	{0x380b, 0x40},
	{0x380e, 0x0c},
	{0x380f, 0x8e},
	{0x3811, 0x10},
	{0x3813, 0x08},
	{0x3814, 0x01},
	{0x3816, 0x01},
	{0x3820, 0xa8},
	{0x3826, 0x11},
	{0x3827, 0x1c},
	{0x3829, 0x03},
	{0x4009, 0x0f},
	{0x4050, 0x04},
	{0x4051, 0x0b},
	{0x4837, 0x0e},
	{0x4902, 0x01},
};

/* 2112x1568: common init + vendor mode delta */
static const struct ov13855_reg mode_2112x1568_regs[] = {
	{0x0103, 0x01},
	{0x0300, 0x02},
	{0x0301, 0x00},
	{0x0302, 0x5c},
	{0x0303, 0x00},
	{0x0304, 0x00},
	{0x0305, 0x01},
	{0x030b, 0x06},
	{0x030c, 0x02},
	{0x030d, 0x88},
	{0x0312, 0x11},
	{0x3022, 0x01},
	{0x3012, 0x40},
	{0x3013, 0x72},
	{0x3016, 0x72},
	{0x301b, 0xf0},
	{0x301f, 0xd0},
	{0x3106, 0x15},
	{0x3107, 0x23},
	{0x3500, 0x00},
	{0x3501, 0x80},
	{0x3502, 0x00},
	{0x3508, 0x02},
	{0x3509, 0x00},
	{0x350a, 0x00},
	{0x350e, 0x00},
	{0x3510, 0x00},
	{0x3511, 0x02},
	{0x3512, 0x00},
	{0x3600, 0x2b},
	{0x3601, 0x52},
	{0x3602, 0x60},
	{0x3612, 0x05},
	{0x3613, 0xa4},
	{0x3620, 0x80},
	{0x3621, 0x10},
	{0x3622, 0x30},
	{0x3624, 0x1c},
	{0x3640, 0x10},
	{0x3641, 0x70},
	{0x3660, 0x04},
	{0x3661, 0x80},
	{0x3662, 0x12},
	{0x3664, 0x73},
	{0x3665, 0xa7},
	{0x366e, 0xff},
	{0x366f, 0xf4},
	{0x3674, 0x00},
	{0x3679, 0x0c},
	{0x367f, 0x01},
	{0x3680, 0x0c},
	{0x3681, 0x50},
	{0x3682, 0x50},
	{0x3683, 0xa9},
	{0x3684, 0xa9},
	{0x3706, 0x40},
	{0x3709, 0x5f},
	{0x3714, 0x24},
	{0x371a, 0x3e},
	{0x3737, 0x04},
	{0x3738, 0xcc},
	{0x3739, 0x12},
	{0x373d, 0x26},
	{0x3764, 0x20},
	{0x3765, 0x20},
	{0x37a1, 0x36},
	{0x37a8, 0x3b},
	{0x37ab, 0x31},
	{0x37c2, 0x04},
	{0x37c3, 0xf1},
	{0x37c5, 0x00},
	{0x37d8, 0x03},
	{0x37d9, 0x0c},
	{0x37da, 0xc2},
	{0x37dc, 0x02},
	{0x37e0, 0x00},
	{0x37e1, 0x0a},
	{0x37e2, 0x14},
	{0x37e3, 0x04},
	{0x37e4, 0x2a},
	{0x37e5, 0x03},
	{0x37e6, 0x04},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x08},
	{0x3804, 0x10},
	{0x3805, 0x9f},
	{0x3806, 0x0c},
	{0x3807, 0x57},
	{0x3808, 0x10},
	{0x3809, 0x80},
	{0x380a, 0x0c},
	{0x380b, 0x40},
	{0x380c, 0x04},
	{0x380d, 0x62},
	{0x380e, 0x0c},
	{0x380f, 0x8e},
	{0x3811, 0x10},
	{0x3813, 0x08},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
	{0x3820, 0xa8},
	{0x3821, 0x00},
	{0x3822, 0xc2},
	{0x3823, 0x18},
	{0x3826, 0x11},
	{0x3827, 0x1c},
	{0x3829, 0x03},
	{0x3832, 0x00},
	{0x3c80, 0x00},
	{0x3c87, 0x01},
	{0x3c8c, 0x19},
	{0x3c8d, 0x1c},
	{0x3c90, 0x00},
	{0x3c91, 0x00},
	{0x3c92, 0x00},
	{0x3c93, 0x00},
	{0x3c94, 0x40},
	{0x3c95, 0x54},
	{0x3c96, 0x34},
	{0x3c97, 0x04},
	{0x3c98, 0x00},
	{0x3d8c, 0x73},
	{0x3d8d, 0xc0},
	{0x3f00, 0x0b},
	{0x3f03, 0x00},
	{0x4001, 0xe0},
	{0x4008, 0x00},
	{0x4009, 0x0f},
	{0x4011, 0xf0},
	{0x4017, 0x08},
	{0x4050, 0x04},
	{0x4051, 0x0b},
	{0x4052, 0x00},
	{0x4053, 0x80},
	{0x4054, 0x00},
	{0x4055, 0x80},
	{0x4056, 0x00},
	{0x4057, 0x80},
	{0x4058, 0x00},
	{0x4059, 0x80},
	{0x405e, 0x00},
	{0x4500, 0x07},
	{0x4503, 0x00},
	{0x450a, 0x04},
	{0x4809, 0x04},
	{0x480c, 0x12},
	{0x481f, 0x30},
	{0x4833, 0x10},
	{0x4837, 0x0e},
	{0x4902, 0x01},
	{0x4d00, 0x03},
	{0x4d01, 0xc9},
	{0x4d02, 0xbc},
	{0x4d03, 0xd7},
	{0x4d04, 0xf0},
	{0x4d05, 0xa2},
	{0x5000, 0xff},
	{0x5001, 0x07},
	{0x5040, 0x39},
	{0x5041, 0x10},
	{0x5042, 0x10},
	{0x5043, 0x84},
	{0x5044, 0x62},
	{0x5180, 0x00},
	{0x5181, 0x10},
	{0x5182, 0x02},
	{0x5183, 0x0f},
	{0x5200, 0x1b},
	{0x520b, 0x07},
	{0x520c, 0x0f},
	{0x5300, 0x04},
	{0x5301, 0x0c},
	{0x5302, 0x0c},
	{0x5303, 0x0f},
	{0x5304, 0x00},
	{0x5305, 0x70},
	{0x5306, 0x00},
	{0x5307, 0x80},
	{0x5308, 0x00},
	{0x5309, 0xa5},
	{0x530a, 0x00},
	{0x530b, 0xd3},
	{0x530c, 0x00},
	{0x530d, 0xf0},
	{0x530e, 0x01},
	{0x530f, 0x10},
	{0x5310, 0x01},
	{0x5311, 0x20},
	{0x5312, 0x01},
	{0x5313, 0x20},
	{0x5314, 0x01},
	{0x5315, 0x20},
	{0x5316, 0x08},
	{0x5317, 0x08},
	{0x5318, 0x10},
	{0x5319, 0x88},
	{0x531a, 0x88},
	{0x531b, 0xa9},
	{0x531c, 0xaa},
	{0x531d, 0x0a},
	{0x5405, 0x02},
	{0x5406, 0x67},
	{0x5407, 0x01},
	{0x5408, 0x4a},
	{0x0303, 0x01},
	{0x3501, 0x40},
	{0x3662, 0x10},
	{0x3714, 0x28},
	{0x3737, 0x08},
	{0x3739, 0x20},
	{0x37c2, 0x14},
	{0x37d9, 0x0c},
	{0x37e3, 0x08},
	{0x37e4, 0x38},
	{0x37e6, 0x08},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x08},
	{0x3805, 0x9f},
	{0x3806, 0x0c},
	{0x3807, 0x4f},
	{0x3808, 0x08},
	{0x3809, 0x40},
	{0x380a, 0x06},
	{0x380b, 0x20},
	{0x380e, 0x0c},
	{0x380f, 0x8e},
	{0x3811, 0x08},
	{0x3813, 0x02},
	{0x3814, 0x03},
	{0x3816, 0x03},
	{0x3820, 0xab},
	{0x3826, 0x04},
	{0x3827, 0x90},
	{0x3829, 0x07},
	{0x4009, 0x0d},
	{0x4050, 0x04},
	{0x4051, 0x0b},
	{0x4837, 0x1d},
	{0x4902, 0x01},
};

/* 1024x768: common init + vendor mode delta */
static const struct ov13855_reg mode_1024x768_regs[] = {
	{0x0103, 0x01},
	{0x0300, 0x02},
	{0x0301, 0x00},
	{0x0302, 0x5c},
	{0x0303, 0x00},
	{0x0304, 0x00},
	{0x0305, 0x01},
	{0x030b, 0x06},
	{0x030c, 0x02},
	{0x030d, 0x88},
	{0x0312, 0x11},
	{0x3022, 0x01},
	{0x3012, 0x40},
	{0x3013, 0x72},
	{0x3016, 0x72},
	{0x301b, 0xf0},
	{0x301f, 0xd0},
	{0x3106, 0x15},
	{0x3107, 0x23},
	{0x3500, 0x00},
	{0x3501, 0x80},
	{0x3502, 0x00},
	{0x3508, 0x02},
	{0x3509, 0x00},
	{0x350a, 0x00},
	{0x350e, 0x00},
	{0x3510, 0x00},
	{0x3511, 0x02},
	{0x3512, 0x00},
	{0x3600, 0x2b},
	{0x3601, 0x52},
	{0x3602, 0x60},
	{0x3612, 0x05},
	{0x3613, 0xa4},
	{0x3620, 0x80},
	{0x3621, 0x10},
	{0x3622, 0x30},
	{0x3624, 0x1c},
	{0x3640, 0x10},
	{0x3641, 0x70},
	{0x3660, 0x04},
	{0x3661, 0x80},
	{0x3662, 0x12},
	{0x3664, 0x73},
	{0x3665, 0xa7},
	{0x366e, 0xff},
	{0x366f, 0xf4},
	{0x3674, 0x00},
	{0x3679, 0x0c},
	{0x367f, 0x01},
	{0x3680, 0x0c},
	{0x3681, 0x50},
	{0x3682, 0x50},
	{0x3683, 0xa9},
	{0x3684, 0xa9},
	{0x3706, 0x40},
	{0x3709, 0x5f},
	{0x3714, 0x24},
	{0x371a, 0x3e},
	{0x3737, 0x04},
	{0x3738, 0xcc},
	{0x3739, 0x12},
	{0x373d, 0x26},
	{0x3764, 0x20},
	{0x3765, 0x20},
	{0x37a1, 0x36},
	{0x37a8, 0x3b},
	{0x37ab, 0x31},
	{0x37c2, 0x04},
	{0x37c3, 0xf1},
	{0x37c5, 0x00},
	{0x37d8, 0x03},
	{0x37d9, 0x0c},
	{0x37da, 0xc2},
	{0x37dc, 0x02},
	{0x37e0, 0x00},
	{0x37e1, 0x0a},
	{0x37e2, 0x14},
	{0x37e3, 0x04},
	{0x37e4, 0x2a},
	{0x37e5, 0x03},
	{0x37e6, 0x04},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x08},
	{0x3804, 0x10},
	{0x3805, 0x9f},
	{0x3806, 0x0c},
	{0x3807, 0x57},
	{0x3808, 0x10},
	{0x3809, 0x80},
	{0x380a, 0x0c},
	{0x380b, 0x40},
	{0x380c, 0x04},
	{0x380d, 0x62},
	{0x380e, 0x0c},
	{0x380f, 0x8e},
	{0x3811, 0x10},
	{0x3813, 0x08},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
	{0x3820, 0xa8},
	{0x3821, 0x00},
	{0x3822, 0xc2},
	{0x3823, 0x18},
	{0x3826, 0x11},
	{0x3827, 0x1c},
	{0x3829, 0x03},
	{0x3832, 0x00},
	{0x3c80, 0x00},
	{0x3c87, 0x01},
	{0x3c8c, 0x19},
	{0x3c8d, 0x1c},
	{0x3c90, 0x00},
	{0x3c91, 0x00},
	{0x3c92, 0x00},
	{0x3c93, 0x00},
	{0x3c94, 0x40},
	{0x3c95, 0x54},
	{0x3c96, 0x34},
	{0x3c97, 0x04},
	{0x3c98, 0x00},
	{0x3d8c, 0x73},
	{0x3d8d, 0xc0},
	{0x3f00, 0x0b},
	{0x3f03, 0x00},
	{0x4001, 0xe0},
	{0x4008, 0x00},
	{0x4009, 0x0f},
	{0x4011, 0xf0},
	{0x4017, 0x08},
	{0x4050, 0x04},
	{0x4051, 0x0b},
	{0x4052, 0x00},
	{0x4053, 0x80},
	{0x4054, 0x00},
	{0x4055, 0x80},
	{0x4056, 0x00},
	{0x4057, 0x80},
	{0x4058, 0x00},
	{0x4059, 0x80},
	{0x405e, 0x00},
	{0x4500, 0x07},
	{0x4503, 0x00},
	{0x450a, 0x04},
	{0x4809, 0x04},
	{0x480c, 0x12},
	{0x481f, 0x30},
	{0x4833, 0x10},
	{0x4837, 0x0e},
	{0x4902, 0x01},
	{0x4d00, 0x03},
	{0x4d01, 0xc9},
	{0x4d02, 0xbc},
	{0x4d03, 0xd7},
	{0x4d04, 0xf0},
	{0x4d05, 0xa2},
	{0x5000, 0xff},
	{0x5001, 0x07},
	{0x5040, 0x39},
	{0x5041, 0x10},
	{0x5042, 0x10},
	{0x5043, 0x84},
	{0x5044, 0x62},
	{0x5180, 0x00},
	{0x5181, 0x10},
	{0x5182, 0x02},
	{0x5183, 0x0f},
	{0x5200, 0x1b},
	{0x520b, 0x07},
	{0x520c, 0x0f},
	{0x5300, 0x04},
	{0x5301, 0x0c},
	{0x5302, 0x0c},
	{0x5303, 0x0f},
	{0x5304, 0x00},
	{0x5305, 0x70},
	{0x5306, 0x00},
	{0x5307, 0x80},
	{0x5308, 0x00},
	{0x5309, 0xa5},
	{0x530a, 0x00},
	{0x530b, 0xd3},
	{0x530c, 0x00},
	{0x530d, 0xf0},
	{0x530e, 0x01},
	{0x530f, 0x10},
	{0x5310, 0x01},
	{0x5311, 0x20},
	{0x5312, 0x01},
	{0x5313, 0x20},
	{0x5314, 0x01},
	{0x5315, 0x20},
	{0x5316, 0x08},
	{0x5317, 0x08},
	{0x5318, 0x10},
	{0x5319, 0x88},
	{0x531a, 0x88},
	{0x531b, 0xa9},
	{0x531c, 0xaa},
	{0x531d, 0x0a},
	{0x5405, 0x02},
	{0x5406, 0x67},
	{0x5407, 0x01},
	{0x5408, 0x4a},
	{0x0303, 0x03},
	{0x3501, 0x20},
	{0x3662, 0x08},
	{0x3714, 0x30},
	{0x3737, 0x08},
	{0x3739, 0x20},
	{0x37c2, 0x2c},
	{0x37d9, 0x06},
	{0x37e3, 0x08},
	{0x37e4, 0x36},
	{0x37e6, 0x08},
	{0x3801, 0x40},
	{0x3802, 0x00},
	{0x3803, 0x40},
	{0x3805, 0x5f},
	{0x3806, 0x0c},
	{0x3807, 0x5f},
	{0x3808, 0x04},
	{0x3809, 0x00},
	{0x380a, 0x03},
	{0x380b, 0x00},
	{0x380e, 0x04},
	{0x380f, 0x30},
	{0x3811, 0x04},
	{0x3813, 0x04},
	{0x3814, 0x07},
	{0x3816, 0x07},
	{0x3820, 0xac},
	{0x3826, 0x04},
	{0x3827, 0x48},
	{0x3829, 0x03},
	{0x4009, 0x05},
	{0x4050, 0x02},
	{0x4051, 0x05},
	{0x4837, 0x3a},
	{0x4902, 0x02},
};

/* mode removed */
/* mode removed */
static const char * const ov13855_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Configurations for supported link frequencies */
#define OV13855_NUM_OF_LINK_FREQS	2
#define OV13855_LINK_FREQ_540MHZ	540000000ULL
#define OV13855_LINK_FREQ_270MHZ	270000000ULL
#define OV13855_LINK_FREQ_INDEX_0	0

/*
 * pixel_rate = link_freq * data-rate * nr_of_lanes / bits_per_sample
 * data rate => double data rate; number of lanes => 4; bits per pixel => 10
 */
static u64 link_freq_to_pixel_rate(u64 f)
{
	f *= 2 * 4;
	do_div(f, 10);

	return f;
}

/* Menu items for LINK_FREQ V4L2 control */
static const s64 link_freq_menu_items[OV13855_NUM_OF_LINK_FREQS] = {
	540000000ULL,
	270000000ULL,
};

/* Link frequency configs */
static const struct ov13855_link_freq_config
			link_freq_configs[OV13855_NUM_OF_LINK_FREQS] = {
	{
		.pixels_per_line = 4488,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_data_rate_1080mbps),
			.regs = mipi_data_rate_1080mbps,
		},
	},
	{
		.pixels_per_line = 2244,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_data_rate_1080mbps),
			.regs = mipi_data_rate_1080mbps,
		},
	},
};

/* Mode configs */
static const struct ov13855_mode supported_modes[] = {
	{
		.width = 4224,
		.height = 3136,
		.vts_def = 0x0c8e,
		.vts_min = 0x0c8e,
		.link_freq_index = OV13855_LINK_FREQ_INDEX_0,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_4224x3136_regs),
			.regs = mode_4224x3136_regs,
		},
	},
	{
		.width = 2112,
		.height = 1568,
		.vts_def = 0x0c8e,
		.vts_min = 0x0c8e,
		.link_freq_index = OV13855_LINK_FREQ_INDEX_0,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2112x1568_regs),
			.regs = mode_2112x1568_regs,
		},
	},
	{
		.width = 1024,
		.height = 768,
		.vts_def = 0x0430,
		.vts_min = 0x0430,
		.link_freq_index = OV13855_LINK_FREQ_INDEX_0,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1024x768_regs),
			.regs = mode_1024x768_regs,
		},
	},
};

struct ov13855 {
	struct device *dev;
	struct clk *xvclk;
	struct clk *xvclk1;
	struct clk *xvclk2;
	struct clk *xvclk3;
	struct regulator_bulk_data supplies[3];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *standby_gpio;

	struct v4l2_subdev sd;
	struct media_pad pad;

	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;

	/* Current mode */
	const struct ov13855_mode *cur_mode;

	/* Mutex for serialized access */
	struct mutex mutex;
};

#define to_ov13855(_sd)	container_of(_sd, struct ov13855, sd)

/* Read registers up to 4 at a time */
static int ov13855_read_reg(struct ov13855 *ov13855, u16 reg, u32 len,
			    u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov13855->sd);
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	int ret;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);

	if (len > 4)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

/* Write registers up to 4 at a time */
static int ov13855_write_reg(struct ov13855 *ov13855, u16 reg, u32 len,
			     u32 __val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov13855->sd);
	int buf_i, val_i;
	u8 buf[6], *val_p;
	__be32 val;

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val = cpu_to_be32(__val);
	val_p = (u8 *)&val;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

/* Write a list of registers */
static int ov13855_write_regs(struct ov13855 *ov13855,
			      const struct ov13855_reg *regs, u32 len)
{
	int ret;
	u32 i;

	for (i = 0; i < len; i++) {
		ret = ov13855_write_reg(ov13855, regs[i].address, 1,
					regs[i].val);
		if (ret) {
			dev_err_ratelimited(
				ov13855->dev,
				"Failed to write reg 0x%4.4x. error = %d\n",
				regs[i].address, ret);

			return ret;
		}
	}

	return 0;
}

static int ov13855_write_reg_list(struct ov13855 *ov13855,
				  const struct ov13855_reg_list *r_list)
{
	return ov13855_write_regs(ov13855, r_list->regs, r_list->num_of_regs);
}

/* Open sub-device */
static int ov13855_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov13855 *ov13855 = to_ov13855(sd);
	struct v4l2_mbus_framefmt *try_fmt = v4l2_subdev_state_get_format(fh->state,
									  0);

	mutex_lock(&ov13855->mutex);

	/* Initialize try_fmt */
	try_fmt->width = ov13855->cur_mode->width;
	try_fmt->height = ov13855->cur_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	/* No crop or compose */
	mutex_unlock(&ov13855->mutex);

	return 0;
}

static int ov13855_update_digital_gain(struct ov13855 *ov13855, u32 d_gain)
{
	int ret;

	ret = ov13855_write_reg(ov13855, OV13855_REG_B_MWB_GAIN,
				OV13855_REG_VALUE_16BIT, d_gain);
	if (ret)
		return ret;

	ret = ov13855_write_reg(ov13855, OV13855_REG_G_MWB_GAIN,
				OV13855_REG_VALUE_16BIT, d_gain);
	if (ret)
		return ret;

	ret = ov13855_write_reg(ov13855, OV13855_REG_R_MWB_GAIN,
				OV13855_REG_VALUE_16BIT, d_gain);

	return ret;
}

static int ov13855_enable_test_pattern(struct ov13855 *ov13855, u32 pattern)
{
	int ret;
	u32 val;

	ret = ov13855_read_reg(ov13855, OV13855_REG_TEST_PATTERN,
			       OV13855_REG_VALUE_08BIT, &val);
	if (ret)
		return ret;

	if (pattern) {
		val &= OV13855_TEST_PATTERN_MASK;
		val |= (pattern - 1) | OV13855_TEST_PATTERN_ENABLE;
	} else {
		val &= ~OV13855_TEST_PATTERN_ENABLE;
	}

	return ov13855_write_reg(ov13855, OV13855_REG_TEST_PATTERN,
				 OV13855_REG_VALUE_08BIT, val);
}

static int ov13855_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov13855 *ov13855 = container_of(ctrl->handler,
					       struct ov13855, ctrl_handler);
	s64 max;
	int ret;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov13855->cur_mode->height + ctrl->val - 8;
		__v4l2_ctrl_modify_range(ov13855->exposure,
					 ov13855->exposure->minimum,
					 max, ov13855->exposure->step, max);
		break;
	}

	/*
	 * Applying V4L2 control value only happens
	 * when power is up for streaming
	 */
	if (!pm_runtime_get_if_in_use(ov13855->dev))
		return 0;

	ret = 0;
	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov13855_write_reg(ov13855, OV13855_REG_ANALOG_GAIN,
					OV13855_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = ov13855_update_digital_gain(ov13855, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		ret = ov13855_write_reg(ov13855, OV13855_REG_EXPOSURE,
					OV13855_REG_VALUE_24BIT,
					ctrl->val << 4);
		break;
	case V4L2_CID_VBLANK:
		/* Update VTS that meets expected vertical blanking */
		ret = ov13855_write_reg(ov13855, OV13855_REG_VTS,
					OV13855_REG_VALUE_16BIT,
					ov13855->cur_mode->height
					  + ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov13855_enable_test_pattern(ov13855, ctrl->val);
		break;
	default:
		dev_info(ov13855->dev,
			 "ctrl(id:0x%x,val:0x%x) is not handled\n",
			 ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(ov13855->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov13855_ctrl_ops = {
	.s_ctrl = ov13855_set_ctrl,
};

static int ov13855_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	/* Only one bayer order(GRBG) is supported */
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int ov13855_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static void ov13855_update_pad_format(const struct ov13855_mode *mode,
				      struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->format.field = V4L2_FIELD_NONE;
}

static int ov13855_do_get_pad_format(struct ov13855 *ov13855,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		fmt->format = *framefmt;
	} else {
		ov13855_update_pad_format(ov13855->cur_mode, fmt);
	}

	return 0;
}

static int ov13855_get_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_format *fmt)
{
	struct ov13855 *ov13855 = to_ov13855(sd);
	int ret;

	mutex_lock(&ov13855->mutex);
	ret = ov13855_do_get_pad_format(ov13855, sd_state, fmt);
	mutex_unlock(&ov13855->mutex);

	return ret;
}

static int
ov13855_set_pad_format(struct v4l2_subdev *sd,
		       struct v4l2_subdev_state *sd_state,
		       struct v4l2_subdev_format *fmt)
{
	struct ov13855 *ov13855 = to_ov13855(sd);
	const struct ov13855_mode *mode;
	struct v4l2_mbus_framefmt *framefmt;
	s32 vblank_def;
	s32 vblank_min;
	s64 h_blank;
	s64 pixel_rate;
	s64 link_freq;

	mutex_lock(&ov13855->mutex);

	/* Only one raw bayer(GRBG) order is supported */
	if (fmt->format.code != MEDIA_BUS_FMT_SBGGR10_1X10)
		fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);
	ov13855_update_pad_format(mode, fmt);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else {
		ov13855->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(ov13855->link_freq, mode->link_freq_index);
		link_freq = link_freq_menu_items[mode->link_freq_index];
		pixel_rate = link_freq_to_pixel_rate(link_freq);
		__v4l2_ctrl_s_ctrl_int64(ov13855->pixel_rate, pixel_rate);

		/* Update limits and set FPS to default */
		vblank_def = ov13855->cur_mode->vts_def -
			     ov13855->cur_mode->height;
		vblank_min = ov13855->cur_mode->vts_min -
			     ov13855->cur_mode->height;
		__v4l2_ctrl_modify_range(
			ov13855->vblank, vblank_min,
			OV13855_VTS_MAX - ov13855->cur_mode->height, 1,
			vblank_def);
		__v4l2_ctrl_s_ctrl(ov13855->vblank, vblank_def);
		h_blank =
			link_freq_configs[mode->link_freq_index].pixels_per_line
			 - ov13855->cur_mode->width;
		__v4l2_ctrl_modify_range(ov13855->hblank, h_blank,
					 h_blank, 1, h_blank);
	}

	mutex_unlock(&ov13855->mutex);

	return 0;
}

static int ov13855_get_skip_frames(struct v4l2_subdev *sd, u32 *frames)
{
	*frames = OV13855_NUM_OF_SKIP_FRAMES;

	return 0;
}

/* Start streaming */
/* bring-up aid: keep the sensor powered after probe so i2cdetect can be used */
static bool keep_power;
static int clk_idx;
static int pwdn_gpio = -1;
module_param(pwdn_gpio, int, 0644);
MODULE_PARM_DESC(pwdn_gpio, "Optional PWDN/standby GPIO number to drive (e.g. 44)");
static int pwdn_level;
static int pwr_order;
module_param(pwr_order, int, 0644);
MODULE_PARM_DESC(pwr_order, "0: dovdd,avdd,dvdd  1: avdd,dovdd,dvdd  2: avdd,dvdd,dovdd");
static int clk_first = 1;
module_param(clk_first, int, 0644);
MODULE_PARM_DESC(clk_first, "1: MCLK before rails (default), 0: after rails");
static int reset_delay_ms = 10;
module_param(reset_delay_ms, int, 0644);
MODULE_PARM_DESC(reset_delay_ms, "delay after releasing reset, ms");
module_param(pwdn_level, int, 0644);
MODULE_PARM_DESC(pwdn_level, "Level to drive the PWDN GPIO to (0 or 1)");
module_param(clk_idx, int, 0644);
MODULE_PARM_DESC(clk_idx, "Which xvclk to use (0 or 1; rear camera is MCLK2=1 on clover)");
module_param(keep_power, bool, 0644);
MODULE_PARM_DESC(keep_power, "Keep sensor powered after probe (bring-up only)");

static const char * const ov13855_supply_names[] = {
	"dovdd",
	"avdd",
	"dvdd",
};

static int ov13855_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov13855 *ov13855 = to_ov13855(sd);
	struct clk *clk = clk_idx == 1 && ov13855->xvclk1 ? ov13855->xvclk1 :
			  clk_idx == 2 && ov13855->xvclk2 ? ov13855->xvclk2 :
			  clk_idx == 3 && ov13855->xvclk3 ? ov13855->xvclk3 :
			  ov13855->xvclk;
	static const int order[3][3] = {
		{ 0, 1, 2 },	/* dovdd, avdd, dvdd */
		{ 1, 0, 2 },	/* avdd, dovdd, dvdd */
		{ 1, 2, 0 },	/* avdd, dvdd, dovdd */
	};
	int k = (pwr_order >= 0 && pwr_order <= 2) ? pwr_order : 0;
	int enabled = 0;
	int ret, i;

	if (is_acpi_node(dev_fwnode(dev)))
		return 0;

	/* keep the sensor in reset (and standby) while the rails come up */
	gpiod_set_value_cansleep(ov13855->reset_gpio, 1);
	if (ov13855->standby_gpio) {
		gpiod_set_value_cansleep(ov13855->standby_gpio, 1);
		dev_dbg(dev, "standby asserted during power-up\n");
	}
	if (pwdn_gpio >= 0) {
		gpio_set_value(pwdn_gpio, 1);
		dev_info(dev, "standby gpio %d asserted\n", pwdn_gpio);
	}

	if (clk_first) {
		ret = clk_prepare_enable(clk);
		if (ret) {
			dev_err(dev, "xvclk%d enable failed: %d\n", clk_idx, ret);
			return ret;
		}
		dev_info(dev, "xvclk%d on at %lu Hz (before rails)\n",
			 clk_idx, clk_get_rate(clk));
	}

	for (i = 0; i < 3; i++) {
		int idx = order[k][i];

		ret = regulator_enable(ov13855->supplies[idx].consumer);
		if (ret) {
			dev_err(dev, "regulator %s enable failed: %d\n",
				ov13855_supply_names[idx], ret);
			goto err;
		}
		enabled++;
		usleep_range(2000, 3000);
	}
	usleep_range(5000, 6000);

	if (!clk_first) {
		ret = clk_prepare_enable(clk);
		if (ret) {
			dev_err(dev, "xvclk%d enable failed (late): %d\n",
				clk_idx, ret);
			goto err;
		}
		dev_info(dev, "xvclk%d on at %lu Hz (after rails)\n",
			 clk_idx, clk_get_rate(clk));
		usleep_range(1000, 2000);
	}

	if (pwdn_gpio >= 0) {
		gpio_set_value(pwdn_gpio, pwdn_level);
		dev_info(dev, "standby gpio %d driven to %d\n",
			 pwdn_gpio, pwdn_level);
	}
	usleep_range(1000, 2000);

	if (ov13855->standby_gpio) {
		gpiod_set_value_cansleep(ov13855->standby_gpio, 0);
		dev_dbg(dev, "standby released\n");
		usleep_range(1000, 2000);
	}

	gpiod_set_value_cansleep(ov13855->reset_gpio, 0);
	msleep(reset_delay_ms > 0 ? reset_delay_ms : 10);

	return 0;

err:
	while (enabled > 0) {
		enabled--;
		regulator_disable(ov13855->supplies[order[k][enabled]].consumer);
	}
	clk_disable_unprepare(clk);
	gpiod_set_value_cansleep(ov13855->reset_gpio, 1);
	return ret;
}

static int ov13855_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov13855 *ov13855 = to_ov13855(sd);

	if (is_acpi_node(dev_fwnode(dev)))
		return 0;

	gpiod_set_value_cansleep(ov13855->reset_gpio, 1);
	if (ov13855->standby_gpio)
		gpiod_set_value_cansleep(ov13855->standby_gpio, 1);
	if (pwdn_gpio >= 0)
		gpio_set_value(pwdn_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ov13855_supply_names),
			       ov13855->supplies);
	clk_disable_unprepare(clk_idx == 1 && ov13855->xvclk1 ? ov13855->xvclk1 :
			      clk_idx == 2 && ov13855->xvclk2 ? ov13855->xvclk2 :
			      clk_idx == 3 && ov13855->xvclk3 ? ov13855->xvclk3 :
			      ov13855->xvclk);
	return 0;
}

static int ov13855_start_streaming(struct ov13855 *ov13855)
{
	const struct ov13855_reg_list *reg_list;
	int ret, link_freq_index;

	if (pwdn_gpio >= 0) {
		int pr = gpio_request(pwdn_gpio, "cam_pwdn");
		if (pr) {
			dev_warn(ov13855->dev, "pwdn gpio %d request failed: %d\n",
				 pwdn_gpio, pr);
			pwdn_gpio = -1;
		} else {
			gpio_direction_output(pwdn_gpio, pwdn_level);
			dev_info(ov13855->dev, "pwdn gpio %d driven to %d\n",
				 pwdn_gpio, pwdn_level);
		}
	}

	ret = ov13855_power_on(ov13855->dev);
	if (ret)
		return ret;

	/* Get out of from software reset */
	ret = ov13855_write_reg(ov13855, OV13855_REG_SOFTWARE_RST,
				OV13855_REG_VALUE_08BIT, OV13855_SOFTWARE_RST);
	if (ret) {
		dev_err(ov13855->dev, "%s failed to set powerup registers\n",
			__func__);
		return ret;
	}

	/* Setup PLL */
	link_freq_index = ov13855->cur_mode->link_freq_index;
	reg_list = &link_freq_configs[link_freq_index].reg_list;
	ret = ov13855_write_reg_list(ov13855, reg_list);
	if (ret) {
		dev_err(ov13855->dev, "%s failed to set plls\n", __func__);
		return ret;
	}

	/* Apply default values of current mode */
	reg_list = &ov13855->cur_mode->reg_list;
	ret = ov13855_write_reg_list(ov13855, reg_list);
	if (ret) {
		dev_err(ov13855->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(ov13855->sd.ctrl_handler);
	if (ret)
		return ret;

	return ov13855_write_reg(ov13855, OV13855_REG_MODE_SELECT,
				 OV13855_REG_VALUE_08BIT,
				 OV13855_MODE_STREAMING);
}

/* Stop streaming */
static int ov13855_stop_streaming(struct ov13855 *ov13855)
{
	return ov13855_write_reg(ov13855, OV13855_REG_MODE_SELECT,
				 OV13855_REG_VALUE_08BIT, OV13855_MODE_STANDBY);

	ov13855_power_off(ov13855->dev);
}

static int ov13855_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov13855 *ov13855 = to_ov13855(sd);
	int ret = 0;

	mutex_lock(&ov13855->mutex);

	if (enable) {
		ret = pm_runtime_resume_and_get(ov13855->dev);
		if (ret < 0)
			goto err_unlock;

		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = ov13855_start_streaming(ov13855);
		if (ret)
			goto err_rpm_put;
	} else {
		ov13855_stop_streaming(ov13855);
		pm_runtime_put(ov13855->dev);
	}

	mutex_unlock(&ov13855->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(ov13855->dev);
err_unlock:
	mutex_unlock(&ov13855->mutex);

	return ret;
}

/* Verify chip ID */
static int ov13855_identify_module(struct ov13855 *ov13855)
{
	int ret;
	u32 val;

	ret = ov13855_read_reg(ov13855, OV13855_REG_CHIP_ID,
			       OV13855_REG_VALUE_24BIT, &val);
	if (ret)
		return ret;

	if (val != OV13855_CHIP_ID)
		dev_warn(ov13855->dev,
			 "chip id mismatch: expected %x, read %x (continuing for bring-up)\n",
			 OV13855_CHIP_ID, val);
	else
		dev_info(ov13855->dev, "OV13855 detected (id %x)\n", val);

	return 0;
}

static const struct v4l2_subdev_core_ops ov13855_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops ov13855_video_ops = {
	.s_stream = ov13855_set_stream,
};

static const struct v4l2_subdev_pad_ops ov13855_pad_ops = {
	.enum_mbus_code = ov13855_enum_mbus_code,
	.get_fmt = ov13855_get_pad_format,
	.set_fmt = ov13855_set_pad_format,
	.enum_frame_size = ov13855_enum_frame_size,
};

static const struct v4l2_subdev_sensor_ops ov13855_sensor_ops = {
	.g_skip_frames = ov13855_get_skip_frames,
};

static const struct v4l2_subdev_ops ov13855_subdev_ops = {
	.core = &ov13855_core_ops,
	.video = &ov13855_video_ops,
	.pad = &ov13855_pad_ops,
	.sensor = &ov13855_sensor_ops,
};

static const struct media_entity_operations ov13855_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops ov13855_internal_ops = {
	.open = ov13855_open,
};

/* Initialize control handlers */
static int ov13855_init_controls(struct ov13855 *ov13855)
{
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	s64 exposure_max;
	s64 vblank_def;
	s64 vblank_min;
	s64 hblank;
	s64 pixel_rate_min;
	s64 pixel_rate_max;
	const struct ov13855_mode *mode;
	int ret;

	ctrl_hdlr = &ov13855->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 10);
	if (ret)
		return ret;

	mutex_init(&ov13855->mutex);
	ctrl_hdlr->lock = &ov13855->mutex;
	ov13855->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr,
				&ov13855_ctrl_ops,
				V4L2_CID_LINK_FREQ,
				OV13855_NUM_OF_LINK_FREQS - 1,
				0,
				link_freq_menu_items);
	if (ov13855->link_freq)
		ov13855->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate_max = link_freq_to_pixel_rate(link_freq_menu_items[0]);
	pixel_rate_min = link_freq_to_pixel_rate(link_freq_menu_items[1]);
	/* By default, PIXEL_RATE is read only */
	ov13855->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &ov13855_ctrl_ops,
						V4L2_CID_PIXEL_RATE,
						pixel_rate_min, pixel_rate_max,
						1, pixel_rate_max);

	mode = ov13855->cur_mode;
	vblank_def = mode->vts_def - mode->height;
	vblank_min = mode->vts_min - mode->height;
	ov13855->vblank = v4l2_ctrl_new_std(
				ctrl_hdlr, &ov13855_ctrl_ops, V4L2_CID_VBLANK,
				vblank_min, OV13855_VTS_MAX - mode->height, 1,
				vblank_def);

	hblank = link_freq_configs[mode->link_freq_index].pixels_per_line -
		 mode->width;
	ov13855->hblank = v4l2_ctrl_new_std(
				ctrl_hdlr, &ov13855_ctrl_ops, V4L2_CID_HBLANK,
				hblank, hblank, 1, hblank);
	if (ov13855->hblank)
		ov13855->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	exposure_max = mode->vts_def - 8;
	ov13855->exposure = v4l2_ctrl_new_std(
				ctrl_hdlr, &ov13855_ctrl_ops,
				V4L2_CID_EXPOSURE, OV13855_EXPOSURE_MIN,
				exposure_max, OV13855_EXPOSURE_STEP,
				OV13855_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov13855_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OV13855_ANA_GAIN_MIN, OV13855_ANA_GAIN_MAX,
			  OV13855_ANA_GAIN_STEP, OV13855_ANA_GAIN_DEFAULT);

	/* Digital gain */
	v4l2_ctrl_new_std(ctrl_hdlr, &ov13855_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OV13855_DGTL_GAIN_MIN, OV13855_DGTL_GAIN_MAX,
			  OV13855_DGTL_GAIN_STEP, OV13855_DGTL_GAIN_DEFAULT);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ov13855_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov13855_test_pattern_menu) - 1,
				     0, 0, ov13855_test_pattern_menu);
	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(ov13855->dev, "%s control init failed (%d)\n",
			__func__, ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(ov13855->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &ov13855_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	ov13855->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&ov13855->mutex);

	return ret;
}

static void ov13855_free_controls(struct ov13855 *ov13855)
{
	v4l2_ctrl_handler_free(ov13855->sd.ctrl_handler);
	mutex_destroy(&ov13855->mutex);
}


/* ---- bring-up debug attributes ------------------------------------------ */
static ssize_t dbg_pin_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t n, bool reset)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov13855 *ov = to_ov13855(sd);
	struct gpio_desc *g = reset ? ov->reset_gpio : ov->standby_gpio;
	int v;

	if (kstrtoint(buf, 0, &v))
		return -EINVAL;
	if (g)
		gpiod_set_value_cansleep(g, !!v);
	dev_info(dev, "dbg: %s -> %d\n", reset ? "reset" : "standby", !!v);
	return n;
}

static ssize_t dbg_pin_show(struct device *dev, struct device_attribute *attr,
			    char *buf, bool reset)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov13855 *ov = to_ov13855(sd);
	struct gpio_desc *g = reset ? ov->reset_gpio : ov->standby_gpio;

	return sprintf(buf, "%d\n", g ? gpiod_get_value_cansleep(g) : -1);
}

static ssize_t dbg_reset_store(struct device *d, struct device_attribute *a,
			       const char *b, size_t n)
{ return dbg_pin_store(d, a, b, n, true); }
static ssize_t dbg_reset_show(struct device *d, struct device_attribute *a, char *b)
{ return dbg_pin_show(d, a, b, true); }
static DEVICE_ATTR_RW(dbg_reset);

static ssize_t dbg_standby_store(struct device *d, struct device_attribute *a,
				 const char *b, size_t n)
{ return dbg_pin_store(d, a, b, n, false); }
static ssize_t dbg_standby_show(struct device *d, struct device_attribute *a, char *b)
{ return dbg_pin_show(d, a, b, false); }
static DEVICE_ATTR_RW(dbg_standby);

static ssize_t dbg_id_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov13855 *ov = to_ov13855(sd);
	u32 val = 0xdeadbeef;
	int ret = ov13855_read_reg(ov, OV13855_REG_CHIP_ID,
				   OV13855_REG_VALUE_24BIT, &val);

	return sprintf(buf, "ret=%d id=0x%06x\n", ret, val);
}
static DEVICE_ATTR_RO(dbg_id);

static struct attribute *ov13855_dbg_attrs[] = {
	&dev_attr_dbg_reset.attr,
	&dev_attr_dbg_standby.attr,
	&dev_attr_dbg_id.attr,
	NULL
};

static const struct attribute_group ov13855_dbg_group = {
	.attrs = ov13855_dbg_attrs,
};
/* ------------------------------------------------------------------------ */

static int ov13855_probe(struct i2c_client *client)
{
	bool keep_power_mode = false;
	struct ov13855 *ov13855;
	unsigned long freq;
	int ret, i;

	ov13855 = devm_kzalloc(&client->dev, sizeof(*ov13855), GFP_KERNEL);
	if (!ov13855)
		return -ENOMEM;

	ov13855->dev = &client->dev;

	ov13855->xvclk = devm_v4l2_sensor_clk_get_legacy(ov13855->dev, "xvclk",
							false, 0);
	if (IS_ERR(ov13855->xvclk))
		return dev_err_probe(ov13855->dev, PTR_ERR(ov13855->xvclk),
				     "failed to get xvclk\n");

	ov13855->xvclk1 = devm_clk_get_optional(ov13855->dev, "xvclk1");
	if (IS_ERR(ov13855->xvclk1))
		ov13855->xvclk1 = NULL;
	ov13855->xvclk2 = devm_clk_get_optional(ov13855->dev, "xvclk2");
	if (IS_ERR(ov13855->xvclk2))
		ov13855->xvclk2 = NULL;
	ov13855->xvclk3 = devm_clk_get_optional(ov13855->dev, "xvclk3");
	if (IS_ERR(ov13855->xvclk3))
		ov13855->xvclk3 = NULL;

	freq = clk_get_rate(ov13855->xvclk);
	if (freq != 24000000)
		return dev_err_probe(ov13855->dev, -EINVAL,
				     "external clock %lu is not supported\n",
				     freq);

	ov13855->reset_gpio = devm_gpiod_get_optional(ov13855->dev, "reset",
						      GPIOD_OUT_HIGH);
	if (IS_ERR(ov13855->reset_gpio))
		return dev_err_probe(ov13855->dev, PTR_ERR(ov13855->reset_gpio),
				     "failed to get reset gpio\n");

	ov13855->standby_gpio = devm_gpiod_get_optional(ov13855->dev, "standby",
							GPIOD_OUT_HIGH);
	if (IS_ERR(ov13855->standby_gpio))
		return dev_err_probe(ov13855->dev,
				     PTR_ERR(ov13855->standby_gpio),
				     "failed to get standby gpio\n");

	for (i = 0; i < ARRAY_SIZE(ov13855_supply_names); i++)
		ov13855->supplies[i].supply = ov13855_supply_names[i];

	ret = devm_regulator_bulk_get(ov13855->dev,
				      ARRAY_SIZE(ov13855_supply_names),
				      ov13855->supplies);
	if (ret)
		return dev_err_probe(ov13855->dev, ret,
				     "failed to get regulators\n");

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&ov13855->sd, client, &ov13855_subdev_ops);

	ret = devm_device_add_group(&client->dev, &ov13855_dbg_group);
	if (ret)
		dev_warn(&client->dev, "dbg attrs not added: %d\n", ret);

	ret = ov13855_power_on(ov13855->dev);
	if (ret)
		return ret;

	/* Check module identity */
	ret = ov13855_identify_module(ov13855);
	if (ret) {
		dev_err(ov13855->dev, "failed to find sensor: %d\n", ret);
		if (!keep_power) {
			ov13855_power_off(ov13855->dev);
			return ret;
		}
		dev_info(ov13855->dev,
			 "keep_power: continuing probe with sensor powered (bring-up)\n");
		/* fall through: keep pinctrl/gpios/rails/clk claimed so they can be
		 * inspected via debugfs while the bus is probed by hand */
		keep_power_mode = 1;
	}
	if (!keep_power_mode)
		ov13855_power_off(ov13855->dev);

	/* Set default mode to max resolution */
	ov13855->cur_mode = &supported_modes[0];

	ret = ov13855_init_controls(ov13855);
	if (ret)
		return ret;

	/* Initialize subdev */
	ov13855->sd.internal_ops = &ov13855_internal_ops;
	ov13855->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			     V4L2_SUBDEV_FL_HAS_EVENTS;
	ov13855->sd.entity.ops = &ov13855_subdev_entity_ops;
	ov13855->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	ov13855->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&ov13855->sd.entity, 1, &ov13855->pad);
	if (ret) {
		dev_err(ov13855->dev, "%s failed:%d\n", __func__, ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&ov13855->sd);
	if (ret < 0)
		goto error_media_entity;

	/*
	 * Device is already turned on by i2c-core with ACPI domain PM.
	 * Enable runtime PM and turn off the device.
	 */
	pm_runtime_set_active(ov13855->dev);
	pm_runtime_enable(ov13855->dev);
	pm_runtime_idle(ov13855->dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&ov13855->sd.entity);

error_handler_free:
	ov13855_free_controls(ov13855);
	dev_err(ov13855->dev, "%s failed:%d\n", __func__, ret);

	return ret;
}

static void ov13855_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov13855 *ov13855 = to_ov13855(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	ov13855_free_controls(ov13855);

	pm_runtime_disable(ov13855->dev);
}

static const struct i2c_device_id ov13855_id_table[] = {
	{ "ov13855" },
	{}
};

MODULE_DEVICE_TABLE(i2c, ov13855_id_table);

#ifdef CONFIG_ACPI
static const struct acpi_device_id ov13855_acpi_ids[] = {
	{"OVTID858"},
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(acpi, ov13855_acpi_ids);
#endif

static struct i2c_driver ov13855_i2c_driver = {
	.driver = {
		.name = "ov13855",
		.acpi_match_table = ACPI_PTR(ov13855_acpi_ids),
	},
	.probe = ov13855_probe,
	.remove = ov13855_remove,
	.id_table = ov13855_id_table,
};

module_i2c_driver(ov13855_i2c_driver);

MODULE_AUTHOR("Kan, Chris <chris.kan@intel.com>");
MODULE_AUTHOR("Rapolu, Chiranjeevi");
MODULE_AUTHOR("Yang, Hyungwoo");
MODULE_DESCRIPTION("Omnivision ov13855 sensor driver");
MODULE_LICENSE("GPL v2");
