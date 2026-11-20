// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Intel Corporation
/*
 * Sony IMX376 image sensor driver.
 *
 * Based on imx258.c (Copyright (C) 2018 Intel Corporation) via the imx376
 * driver from https://github.com/GH4NG/meizu-m1882-mainline-linux
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

/* The PLL settings are only valid for a 24 MHz input clock */
#define IMX376_INCK_FREQ		24000000

#define IMX376_REG_MODE_SELECT		CCI_REG8(0x0100)
#define IMX376_MODE_STANDBY		0x00
#define IMX376_MODE_STREAMING		0x01

#define IMX376_REG_RESET		CCI_REG8(0x0103)

/* Chip ID */
#define IMX376_REG_CHIP_ID		CCI_REG16(0x0016)
#define IMX376_CHIP_ID			0x0376

/* V_TIMING internal */
#define IMX376_VTS_30FPS		4736
#define IMX376_VTS_MAX			65525

/* HBLANK control - read only */
#define IMX376_PPL_DEFAULT		5624

/* Exposure control */
#define IMX376_REG_EXPOSURE		CCI_REG16(0x0202)
#define IMX376_EXPOSURE_OFFSET		10
#define IMX376_EXPOSURE_MIN		4
#define IMX376_EXPOSURE_STEP		1
#define IMX376_EXPOSURE_DEFAULT		0x640
#define IMX376_EXPOSURE_MAX		(IMX376_VTS_MAX - IMX376_EXPOSURE_OFFSET)

/* Analog gain control */
#define IMX376_REG_ANALOG_GAIN		CCI_REG16(0x0204)
#define IMX376_ANA_GAIN_MIN		0
#define IMX376_ANA_GAIN_MAX		960	/* 1024 / (1024 - code), x16 */
#define IMX376_ANA_GAIN_STEP		1
#define IMX376_ANA_GAIN_DEFAULT		0x0

/* Digital gain control */
#define IMX376_REG_GR_DIGITAL_GAIN	CCI_REG16(0x020e)
#define IMX376_REG_R_DIGITAL_GAIN	CCI_REG16(0x0210)
#define IMX376_REG_B_DIGITAL_GAIN	CCI_REG16(0x0212)
#define IMX376_REG_GB_DIGITAL_GAIN	CCI_REG16(0x0214)
#define IMX376_DGTL_GAIN_MIN		0
#define IMX376_DGTL_GAIN_MAX		4096	/* Max = 0xFFF */
#define IMX376_DGTL_GAIN_DEFAULT	1024
#define IMX376_DGTL_GAIN_STEP		1

/* Test Pattern Control */
#define IMX376_REG_TEST_PATTERN		CCI_REG16(0x0600)

/* Orientation */
#define REG_MIRROR_FLIP_CONTROL		CCI_REG8(0x0101)
#define REG_CONFIG_MIRROR_HFLIP		0x01
#define REG_CONFIG_MIRROR_VFLIP		0x02

/*
 * IMX376 native and active pixel array size. The mode registers read the
 * whole array starting at (0, 0): X/Y_ADD_STA = 0, X_ADD_END = 5183 and
 * Y_ADD_END = 3879.
 */
#define IMX376_NATIVE_WIDTH		5184U
#define IMX376_NATIVE_HEIGHT		3880U
#define IMX376_PIXEL_ARRAY_LEFT		0U
#define IMX376_PIXEL_ARRAY_TOP		0U
#define IMX376_PIXEL_ARRAY_WIDTH	5184U
#define IMX376_PIXEL_ARRAY_HEIGHT	3880U

/* CCS regs */
#define IMX376_REG_FRM_LENGTH_LINES	CCI_REG16(0x0340)
#define IMX376_REG_EXCK_FREQ		CCI_REG16(0x0136)
#define IMX376_REG_IVTPXCK_DIV		CCI_REG8(0x0301)
#define IMX376_REG_IVTSYCK_DIV		CCI_REG8(0x0303)
#define IMX376_REG_PREPLLCK_VT_DIV	CCI_REG8(0x0305)
#define IMX376_REG_PLL_IVT_MPY		CCI_REG16(0x0306)
#define IMX376_REG_IOPSYCK_DIV		CCI_REG8(0x030b)
#define IMX376_REG_PREPLLCK_OP_DIV	CCI_REG8(0x030d)
#define IMX376_REG_PLL_IOP_MPY		CCI_REG16(0x030e)
#define IMX376_REG_PLL_MULT_DRIV	CCI_REG8(0x0310)
#define IMX376_REG_CSI_LANE_MODE	CCI_REG8(0x0114)
#define IMX376_REG_REQ_LINK_BIT_RATE_MBPS_H	CCI_REG16(0x0820)
#define IMX376_REG_REQ_LINK_BIT_RATE_MBPS_L	CCI_REG16(0x0822)

struct imx376_reg_list {
	u32 num_of_regs;
	const struct cci_reg_sequence *regs;
};

/* Link frequency config */
struct imx376_link_freq_config {
	u32 pixels_per_line;

	/*
	 * Video timing pixel rate:
	 * INCK / PREPLLCK_VT_DIV * PLL_IVT_MPY / IVTPXCK_DIV * 2
	 */
	u64 pixel_rate;

	/* PLL configuration for this link frequency */
	struct imx376_reg_list reg_list;
};

/* Mode : resolution and related config&values */
struct imx376_mode {
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
	struct imx376_reg_list reg_list;

	/* Analog crop rectangle */
	struct v4l2_rect crop;
};

static const struct cci_reg_sequence mipi_1000mbps_24mhz_4l[] = {
	{ IMX376_REG_EXCK_FREQ, 0x1800 },
	{ IMX376_REG_IVTPXCK_DIV, 5 },
	{ IMX376_REG_IVTSYCK_DIV, 2 },
	{ IMX376_REG_PREPLLCK_VT_DIV, 3 },
	{ IMX376_REG_PLL_IVT_MPY, 250 },
	{ IMX376_REG_IOPSYCK_DIV, 2 },
	{ IMX376_REG_PREPLLCK_OP_DIV, 2 },
	{ IMX376_REG_PLL_IOP_MPY, 350 },
	{ IMX376_REG_PLL_MULT_DRIV, 0 },
	{ IMX376_REG_CSI_LANE_MODE, 3 },
	{ IMX376_REG_REQ_LINK_BIT_RATE_MBPS_H, 1000 * 4 },
	{ IMX376_REG_REQ_LINK_BIT_RATE_MBPS_L, 0 },
};

static const struct cci_reg_sequence mode_common_regs[] = {
	{CCI_REG8(0x3C7D), 0x28},
	{CCI_REG8(0x3C7E), 0x04},
	{CCI_REG8(0x3C7F), 0x03},
	{CCI_REG8(0x0B06), 0x00},
	{CCI_REG8(0x3F02), 0x02},
	{CCI_REG8(0x3F22), 0x01},
	{CCI_REG8(0x3F7F), 0x01},
	{CCI_REG8(0x4421), 0x04},
	{CCI_REG8(0x4430), 0x05},
	{CCI_REG8(0x4431), 0xDC},
	{CCI_REG8(0x5222), 0x02},
	{CCI_REG8(0x56B7), 0x74},
	{CCI_REG8(0x6204), 0xC6},
	{CCI_REG8(0x620E), 0x27},
	{CCI_REG8(0x6210), 0x69},
	{CCI_REG8(0x6211), 0xD6},
	{CCI_REG8(0x6213), 0x01},
	{CCI_REG8(0x6215), 0x5A},
	{CCI_REG8(0x6216), 0x75},
	{CCI_REG8(0x6218), 0x5A},
	{CCI_REG8(0x6219), 0x75},
	{CCI_REG8(0x6220), 0x06},
	{CCI_REG8(0x6222), 0x0C},
	{CCI_REG8(0x6225), 0x19},
	{CCI_REG8(0x6228), 0x32},
	{CCI_REG8(0x6229), 0x70},
	{CCI_REG8(0x622B), 0x64},
	{CCI_REG8(0x622E), 0xB0},
	{CCI_REG8(0x6231), 0x71},
	{CCI_REG8(0x6234), 0x06},
	{CCI_REG8(0x6236), 0x46},
	{CCI_REG8(0x6237), 0x46},
	{CCI_REG8(0x6239), 0x0C},
	{CCI_REG8(0x623C), 0x19},
	{CCI_REG8(0x623F), 0x32},
	{CCI_REG8(0x6240), 0x71},
	{CCI_REG8(0x6242), 0x64},
	{CCI_REG8(0x6243), 0x44},
	{CCI_REG8(0x6245), 0xB0},
	{CCI_REG8(0x6246), 0xA8},
	{CCI_REG8(0x6248), 0x71},
	{CCI_REG8(0x624B), 0x06},
	{CCI_REG8(0x624D), 0x46},
	{CCI_REG8(0x625C), 0xC9},
	{CCI_REG8(0x625F), 0x92},
	{CCI_REG8(0x6262), 0x26},
	{CCI_REG8(0x6264), 0x46},
	{CCI_REG8(0x6265), 0x46},
	{CCI_REG8(0x6267), 0x0C},
	{CCI_REG8(0x626A), 0x19},
	{CCI_REG8(0x626D), 0x32},
	{CCI_REG8(0x626E), 0x72},
	{CCI_REG8(0x6270), 0x64},
	{CCI_REG8(0x6271), 0x68},
	{CCI_REG8(0x6273), 0xC8},
	{CCI_REG8(0x6276), 0x91},
	{CCI_REG8(0x6279), 0x27},
	{CCI_REG8(0x627B), 0x46},
	{CCI_REG8(0x627C), 0x55},
	{CCI_REG8(0x627F), 0x95},
	{CCI_REG8(0x6282), 0x84},
	{CCI_REG8(0x6283), 0x40},
	{CCI_REG8(0x6284), 0x00},
	{CCI_REG8(0x6285), 0x00},
	{CCI_REG8(0x6286), 0x08},
	{CCI_REG8(0x6287), 0xC0},
	{CCI_REG8(0x6288), 0x00},
	{CCI_REG8(0x6289), 0x00},
	{CCI_REG8(0x628A), 0x1B},
	{CCI_REG8(0x628B), 0x80},
	{CCI_REG8(0x628C), 0x20},
	{CCI_REG8(0x628E), 0x35},
	{CCI_REG8(0x628F), 0x00},
	{CCI_REG8(0x6290), 0x50},
	{CCI_REG8(0x6291), 0x00},
	{CCI_REG8(0x6292), 0x14},
	{CCI_REG8(0x6293), 0x00},
	{CCI_REG8(0x6294), 0x00},
	{CCI_REG8(0x6296), 0x54},
	{CCI_REG8(0x6297), 0x00},
	{CCI_REG8(0x6298), 0x00},
	{CCI_REG8(0x6299), 0x01},
	{CCI_REG8(0x629A), 0x10},
	{CCI_REG8(0x629B), 0x01},
	{CCI_REG8(0x629C), 0x00},
	{CCI_REG8(0x629D), 0x03},
	{CCI_REG8(0x629E), 0x50},
	{CCI_REG8(0x629F), 0x05},
	{CCI_REG8(0x62A0), 0x00},
	{CCI_REG8(0x62B1), 0x00},
	{CCI_REG8(0x62B2), 0x00},
	{CCI_REG8(0x62B3), 0x00},
	{CCI_REG8(0x62B5), 0x00},
	{CCI_REG8(0x62B6), 0x00},
	{CCI_REG8(0x62B7), 0x00},
	{CCI_REG8(0x62B8), 0x00},
	{CCI_REG8(0x62B9), 0x00},
	{CCI_REG8(0x62BA), 0x00},
	{CCI_REG8(0x62BB), 0x00},
	{CCI_REG8(0x62BC), 0x00},
	{CCI_REG8(0x62BD), 0x00},
	{CCI_REG8(0x62BE), 0x00},
	{CCI_REG8(0x62BF), 0x00},
	{CCI_REG8(0x62D0), 0x0C},
	{CCI_REG8(0x62D1), 0x00},
	{CCI_REG8(0x62D2), 0x00},
	{CCI_REG8(0x62D4), 0x40},
	{CCI_REG8(0x62D5), 0x00},
	{CCI_REG8(0x62D6), 0x00},
	{CCI_REG8(0x62D7), 0x00},
	{CCI_REG8(0x62D8), 0xD8},
	{CCI_REG8(0x62D9), 0x00},
	{CCI_REG8(0x62DA), 0x00},
	{CCI_REG8(0x62DB), 0x02},
	{CCI_REG8(0x62DC), 0xB0},
	{CCI_REG8(0x62DD), 0x03},
	{CCI_REG8(0x62DE), 0x00},
	{CCI_REG8(0x62EF), 0x14},
	{CCI_REG8(0x62F0), 0x00},
	{CCI_REG8(0x62F1), 0x00},
	{CCI_REG8(0x62F3), 0x58},
	{CCI_REG8(0x62F4), 0x00},
	{CCI_REG8(0x62F5), 0x00},
	{CCI_REG8(0x62F6), 0x01},
	{CCI_REG8(0x62F7), 0x20},
	{CCI_REG8(0x62F8), 0x00},
	{CCI_REG8(0x62F9), 0x00},
	{CCI_REG8(0x62FA), 0x03},
	{CCI_REG8(0x62FB), 0x80},
	{CCI_REG8(0x62FC), 0x00},
	{CCI_REG8(0x62FD), 0x00},
	{CCI_REG8(0x62FE), 0x04},
	{CCI_REG8(0x62FF), 0x60},
	{CCI_REG8(0x6300), 0x04},
	{CCI_REG8(0x6301), 0x00},
	{CCI_REG8(0x6302), 0x09},
	{CCI_REG8(0x6303), 0x00},
	{CCI_REG8(0x6304), 0x0C},
	{CCI_REG8(0x6305), 0x00},
	{CCI_REG8(0x6306), 0x1B},
	{CCI_REG8(0x6307), 0x80},
	{CCI_REG8(0x6308), 0x30},
	{CCI_REG8(0x630A), 0x38},
	{CCI_REG8(0x630B), 0x00},
	{CCI_REG8(0x630C), 0x60},
	{CCI_REG8(0x630E), 0x14},
	{CCI_REG8(0x630F), 0x00},
	{CCI_REG8(0x6310), 0x00},
	{CCI_REG8(0x6312), 0x58},
	{CCI_REG8(0x6313), 0x00},
	{CCI_REG8(0x6314), 0x00},
	{CCI_REG8(0x6315), 0x01},
	{CCI_REG8(0x6316), 0x18},
	{CCI_REG8(0x6317), 0x01},
	{CCI_REG8(0x6318), 0x80},
	{CCI_REG8(0x6319), 0x03},
	{CCI_REG8(0x631A), 0x60},
	{CCI_REG8(0x631B), 0x06},
	{CCI_REG8(0x631C), 0x00},
	{CCI_REG8(0x632D), 0x0E},
	{CCI_REG8(0x632E), 0x00},
	{CCI_REG8(0x632F), 0x00},
	{CCI_REG8(0x6331), 0x44},
	{CCI_REG8(0x6332), 0x00},
	{CCI_REG8(0x6333), 0x00},
	{CCI_REG8(0x6334), 0x00},
	{CCI_REG8(0x6335), 0xE8},
	{CCI_REG8(0x6336), 0x00},
	{CCI_REG8(0x6337), 0x00},
	{CCI_REG8(0x6338), 0x02},
	{CCI_REG8(0x6339), 0xF0},
	{CCI_REG8(0x633A), 0x00},
	{CCI_REG8(0x633B), 0x00},
	{CCI_REG8(0x634C), 0x0C},
	{CCI_REG8(0x634D), 0x00},
	{CCI_REG8(0x634E), 0x00},
	{CCI_REG8(0x6350), 0x40},
	{CCI_REG8(0x6351), 0x00},
	{CCI_REG8(0x6352), 0x00},
	{CCI_REG8(0x6353), 0x00},
	{CCI_REG8(0x6354), 0xD8},
	{CCI_REG8(0x6355), 0x00},
	{CCI_REG8(0x6356), 0x00},
	{CCI_REG8(0x6357), 0x02},
	{CCI_REG8(0x6358), 0xB0},
	{CCI_REG8(0x6359), 0x04},
	{CCI_REG8(0x635A), 0x00},
	{CCI_REG8(0x636B), 0x00},
	{CCI_REG8(0x636C), 0x00},
	{CCI_REG8(0x636D), 0x00},
	{CCI_REG8(0x636F), 0x00},
	{CCI_REG8(0x6370), 0x00},
	{CCI_REG8(0x6371), 0x00},
	{CCI_REG8(0x6372), 0x00},
	{CCI_REG8(0x6373), 0x00},
	{CCI_REG8(0x6374), 0x00},
	{CCI_REG8(0x6375), 0x00},
	{CCI_REG8(0x6376), 0x00},
	{CCI_REG8(0x6377), 0x00},
	{CCI_REG8(0x6378), 0x00},
	{CCI_REG8(0x6379), 0x00},
	{CCI_REG8(0x637A), 0x13},
	{CCI_REG8(0x637B), 0xD4},
	{CCI_REG8(0x6388), 0x22},
	{CCI_REG8(0x6389), 0x82},
	{CCI_REG8(0x638A), 0xC8},
	{CCI_REG8(0x639D), 0x20},
	{CCI_REG8(0x7BA0), 0x01},
	{CCI_REG8(0x7BA9), 0x00},
	{CCI_REG8(0x7BAA), 0x01},
	{CCI_REG8(0x7BAD), 0x00},
	{CCI_REG8(0x9002), 0x00},
	{CCI_REG8(0x9003), 0x00},
	{CCI_REG8(0x9004), 0x0D},
	{CCI_REG8(0x9006), 0x01},
	{CCI_REG8(0x9200), 0x93},
	{CCI_REG8(0x9201), 0x85},
	{CCI_REG8(0x9202), 0x93},
	{CCI_REG8(0x9203), 0x87},
	{CCI_REG8(0x9204), 0x93},
	{CCI_REG8(0x9205), 0x8D},
	{CCI_REG8(0x9206), 0x93},
	{CCI_REG8(0x9207), 0x8F},
	{CCI_REG8(0x9208), 0x62},
	{CCI_REG8(0x9209), 0x2C},
	{CCI_REG8(0x920A), 0x62},
	{CCI_REG8(0x920B), 0x2F},
	{CCI_REG8(0x920C), 0x6A},
	{CCI_REG8(0x920D), 0x23},
	{CCI_REG8(0x920E), 0x71},
	{CCI_REG8(0x920F), 0x08},
	{CCI_REG8(0x9210), 0x71},
	{CCI_REG8(0x9211), 0x09},
	{CCI_REG8(0x9212), 0x71},
	{CCI_REG8(0x9213), 0x0B},
	{CCI_REG8(0x9214), 0x6A},
	{CCI_REG8(0x9215), 0x0F},
	{CCI_REG8(0x9216), 0x71},
	{CCI_REG8(0x9217), 0x07},
	{CCI_REG8(0x9218), 0x71},
	{CCI_REG8(0x9219), 0x03},
	{CCI_REG8(0x935D), 0x01},
	{CCI_REG8(0x9389), 0x05},
	{CCI_REG8(0x938B), 0x05},
	{CCI_REG8(0x9391), 0x05},
	{CCI_REG8(0x9393), 0x05},
	{CCI_REG8(0x9395), 0x65},
	{CCI_REG8(0x9397), 0x5A},
	{CCI_REG8(0x9399), 0x05},
	{CCI_REG8(0x939B), 0x05},
	{CCI_REG8(0x939D), 0x05},
	{CCI_REG8(0x939F), 0x05},
	{CCI_REG8(0x93A1), 0x05},
	{CCI_REG8(0x93A3), 0x05},
	{CCI_REG8(0xB3F1), 0x80},
	{CCI_REG8(0xB3F2), 0x0E},
	{CCI_REG8(0xBC40), 0x03},
	{CCI_REG8(0xBC82), 0x07},
	{CCI_REG8(0xBC83), 0xB0},
	{CCI_REG8(0xBC84), 0x0D},
	{CCI_REG8(0xBC85), 0x08},
	{CCI_REG8(0xE0A6), 0x0A},
	{CCI_REG8(0xAA3F), 0x04},
	{CCI_REG8(0xAA41), 0x03},
	{CCI_REG8(0xAA43), 0x02},
	{CCI_REG8(0xAA5D), 0x05},
	{CCI_REG8(0xAA5F), 0x03},
	{CCI_REG8(0xAA61), 0x02},
	{CCI_REG8(0xAACF), 0x04},
	{CCI_REG8(0xAAD1), 0x03},
	{CCI_REG8(0xAAD3), 0x02},
	{CCI_REG8(0xAAED), 0x05},
	{CCI_REG8(0xAAEF), 0x03},
	{CCI_REG8(0xAAF1), 0x02},
	{CCI_REG8(0xB6D9), 0x00},
};

static const struct cci_reg_sequence mode_2592x1940_regs[] = {
	{CCI_REG8(0x0112), 0x0A},
	{CCI_REG8(0x0113), 0x0A},
	{CCI_REG8(0x0114), 0x03},
	{CCI_REG8(0x0342), 0x15},
	{CCI_REG8(0x0343), 0xF8},
	{CCI_REG8(0x0340), 0x12},
	{CCI_REG8(0x0341), 0x80},
	{CCI_REG8(0x3F39), 0x00},
	{CCI_REG8(0x3F3A), 0x12},
	{CCI_REG8(0x3F3B), 0x80},
	{CCI_REG8(0x0344), 0x00},
	{CCI_REG8(0x0345), 0x00},
	{CCI_REG8(0x0346), 0x00},
	{CCI_REG8(0x0347), 0x00},
	{CCI_REG8(0x0348), 0x14},
	{CCI_REG8(0x0349), 0x3F},
	{CCI_REG8(0x034A), 0x0F},
	{CCI_REG8(0x034B), 0x27},
	{CCI_REG8(0x0381), 0x01},
	{CCI_REG8(0x0383), 0x01},
	{CCI_REG8(0x0385), 0x01},
	{CCI_REG8(0x0387), 0x01},
	{CCI_REG8(0x0900), 0x01},
	{CCI_REG8(0x0901), 0x22},
	{CCI_REG8(0x0902), 0x08},
	{CCI_REG8(0x3F4D), 0x81},
	{CCI_REG8(0x3F4C), 0x81},
	{CCI_REG8(0x4254), 0x7F},
	{CCI_REG8(0x0401), 0x00},
	{CCI_REG8(0x0404), 0x00},
	{CCI_REG8(0x0405), 0x10},
	{CCI_REG8(0x0408), 0x00},
	{CCI_REG8(0x0409), 0x00},
	{CCI_REG8(0x040A), 0x00},
	{CCI_REG8(0x040B), 0x00},
	{CCI_REG8(0x040C), 0x0A},
	{CCI_REG8(0x040D), 0x20},
	{CCI_REG8(0x040E), 0x07},
	{CCI_REG8(0x040F), 0x94},
	{CCI_REG8(0x034C), 0x0A},
	{CCI_REG8(0x034D), 0x20},
	{CCI_REG8(0x034E), 0x07},
	{CCI_REG8(0x034F), 0x94},
	{CCI_REG8(0x0301), 0x05},
	{CCI_REG8(0x0303), 0x02},
	{CCI_REG8(0x0305), 0x03},
	{CCI_REG8(0x0306), 0x00},
	{CCI_REG8(0x0307), 0xFA},
	{CCI_REG8(0x030B), 0x02},
	{CCI_REG8(0x030D), 0x02},
	{CCI_REG8(0x030E), 0x01},
	{CCI_REG8(0x030F), 0x5E},
	{CCI_REG8(0x0310), 0x00},
	{CCI_REG8(0x0820), 0x0F},
	{CCI_REG8(0x0821), 0xA0},
	{CCI_REG8(0x0822), 0x00},
	{CCI_REG8(0x0823), 0x00},
	{CCI_REG8(0xBC41), 0x03},
	{CCI_REG8(0x0106), 0x00},
	{CCI_REG8(0x0B00), 0x00},
	{CCI_REG8(0x0B05), 0x01},
	{CCI_REG8(0x3230), 0x00},
	{CCI_REG8(0x3602), 0x01},
	{CCI_REG8(0x3607), 0x00},
	{CCI_REG8(0x3C00), 0x74},
	{CCI_REG8(0x3C01), 0x5F},
	{CCI_REG8(0x3C02), 0x73},
	{CCI_REG8(0x3C03), 0x64},
	{CCI_REG8(0x3C04), 0x54},
	{CCI_REG8(0x3C05), 0xA8},
	{CCI_REG8(0x3C06), 0xBE},
	{CCI_REG8(0x3C07), 0x00},
	{CCI_REG8(0x3C08), 0x00},
	{CCI_REG8(0x3C09), 0x01},
	{CCI_REG8(0x3C0A), 0x14},
	{CCI_REG8(0x3C0B), 0x01},
	{CCI_REG8(0x3C0C), 0x01},
	{CCI_REG8(0x3E20), 0x03},
	{CCI_REG8(0x3E3D), 0x00},
	{CCI_REG8(0x3F14), 0x00},
	{CCI_REG8(0x3F17), 0x00},
	{CCI_REG8(0x3F3C), 0x00},
	{CCI_REG8(0x3F78), 0x03},
	{CCI_REG8(0x3F79), 0x14},
	{CCI_REG8(0x3F7A), 0x03},
	{CCI_REG8(0x3F7B), 0xBC},
	{CCI_REG8(0x562B), 0x32},
	{CCI_REG8(0x562D), 0x34},
	{CCI_REG8(0x5617), 0x32},
	{CCI_REG8(0x7849), 0x01},
	{CCI_REG8(0x9104), 0x04},
	{CCI_REG8(0x0202), 0x12},
	{CCI_REG8(0x0203), 0x70},
	{CCI_REG8(0x0204), 0x00},
	{CCI_REG8(0x0205), 0x00},
	{CCI_REG8(0x020E), 0x01},
	{CCI_REG8(0x020F), 0x00},
};

/*
 * The supported formats.
 * This table MUST contain 4 entries per format, to cover the various flip
 * combinations in the order
 * - no flip
 * - h flip
 * - v flip
 * - h&v flips
 */
static const u32 codes[] = {
	/* 10-bit modes. */
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SBGGR10_1X10
};

static const char * const imx376_test_pattern_menu[] = {
	"Disabled",
	"Solid Colour",
	"Eight Vertical Colour Bars",
	"Colour Bars With Fade to Grey",
	"Pseudorandom Sequence (PN9)",
};

/* regulator supplies */
static const char * const imx376_supply_name[] = {
	/* Supplies can be enabled in any order */
	"vana",  /* Analog (2.8V) supply */
	"vcore",  /* Digital Core (1.2V) supply */
	"vio",  /* IF (1.8V) supply */
};

#define IMX376_NUM_SUPPLIES ARRAY_SIZE(imx376_supply_name)

enum {
	IMX376_LINK_FREQ_1000MBPS,
};

static const s64 link_freq_menu_items[] = {
	[IMX376_LINK_FREQ_1000MBPS] = 500000000ULL,
};

static const struct imx376_link_freq_config link_freq_configs[] = {
	[IMX376_LINK_FREQ_1000MBPS] = {
		.pixels_per_line = IMX376_PPL_DEFAULT,
		.pixel_rate = 800000000,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_1000mbps_24mhz_4l),
			.regs = mipi_1000mbps_24mhz_4l,
		},
	},
};

/* Mode configs */
static const struct imx376_mode supported_modes[] = {
	{
		.width = 2592,
		.height = 1940,
		.vts_def = IMX376_VTS_30FPS,
		.vts_min = IMX376_VTS_30FPS,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2592x1940_regs),
			.regs = mode_2592x1940_regs,
		},
		.link_freq_index = IMX376_LINK_FREQ_1000MBPS,
		.crop = {
			.left = IMX376_PIXEL_ARRAY_LEFT,
			.top = IMX376_PIXEL_ARRAY_TOP,
			.width = IMX376_PIXEL_ARRAY_WIDTH,
			.height = IMX376_PIXEL_ARRAY_HEIGHT,
		},
	},
};

struct imx376 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct regmap *regmap;

	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;

	struct gpio_desc *reset_gpio;

	/*
	 * Streaming on/off, protected by the subdev state lock, which is
	 * shared with the control handler.
	 */
	bool streaming;

	struct clk *clk;
	struct regulator_bulk_data supplies[IMX376_NUM_SUPPLIES];
};

static inline struct imx376 *to_imx376(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx376, sd);
}

/*
 * Get bayer order based on flip setting. Called with the subdev state lock
 * (shared with the control handler) held.
 */
static u32 imx376_get_format_code(const struct imx376 *imx376)
{
	unsigned int i;

	i = (imx376->vflip->val ? 2 : 0) |
	    (imx376->hflip->val ? 1 : 0);

	return codes[i];
}

static int imx376_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	struct imx376 *imx376 = to_imx376(sd);
	struct v4l2_mbus_framefmt *fmt =
		v4l2_subdev_state_get_format(state, 0);
	struct v4l2_rect *crop = v4l2_subdev_state_get_crop(state, 0);

	/* Initialize the format */
	fmt->width = supported_modes[0].width;
	fmt->height = supported_modes[0].height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->code = imx376_get_format_code(imx376);

	/* Initialize the crop rectangle */
	crop->left = IMX376_PIXEL_ARRAY_LEFT;
	crop->top = IMX376_PIXEL_ARRAY_TOP;
	crop->width = IMX376_PIXEL_ARRAY_WIDTH;
	crop->height = IMX376_PIXEL_ARRAY_HEIGHT;

	return 0;
}

static int imx376_update_digital_gain(struct imx376 *imx376, u32 val)
{
	int ret = 0;

	cci_write(imx376->regmap, IMX376_REG_GR_DIGITAL_GAIN, val, &ret);
	cci_write(imx376->regmap, IMX376_REG_GB_DIGITAL_GAIN, val, &ret);
	cci_write(imx376->regmap, IMX376_REG_R_DIGITAL_GAIN, val, &ret);
	cci_write(imx376->regmap, IMX376_REG_B_DIGITAL_GAIN, val, &ret);

	return ret;
}

/* Find the mode matching the format stored in the given state. */
static const struct imx376_mode *
imx376_get_mode(struct v4l2_subdev_state *state)
{
	const struct v4l2_mbus_framefmt *fmt =
		v4l2_subdev_state_get_format(state, 0);

	return v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height, fmt->width, fmt->height);
}

static void imx376_adjust_exposure_range(struct imx376 *imx376,
					 const struct imx376_mode *mode)
{
	int exposure_max, exposure_def;

	/* Honour the VBLANK limits when setting exposure. */
	exposure_max = mode->height + imx376->vblank->val -
		       IMX376_EXPOSURE_OFFSET;
	exposure_def = min(exposure_max, imx376->exposure->val);
	__v4l2_ctrl_modify_range(imx376->exposure, imx376->exposure->minimum,
				 exposure_max, imx376->exposure->step,
				 exposure_def);
}

static int imx376_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx376 *imx376 =
		container_of(ctrl->handler, struct imx376, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&imx376->sd);
	struct v4l2_subdev_state *state =
		v4l2_subdev_get_locked_active_state(&imx376->sd);
	const struct imx376_mode *mode = imx376_get_mode(state);
	int ret = 0;

	/*
	 * The VBLANK control may change the limits of usable exposure, so check
	 * and adjust if necessary.
	 */
	if (ctrl->id == V4L2_CID_VBLANK)
		imx376_adjust_exposure_range(imx376, mode);

	/* The Bayer order depends on the flips, keep the active format in sync */
	if (ctrl->id == V4L2_CID_HFLIP || ctrl->id == V4L2_CID_VFLIP)
		v4l2_subdev_state_get_format(state, 0)->code =
			imx376_get_format_code(imx376);

	/*
	 * Applying V4L2 control value only happens
	 * when power is up for streaming
	 */
	if (pm_runtime_get_if_in_use(&client->dev) <= 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(imx376->regmap, IMX376_REG_ANALOG_GAIN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_EXPOSURE:
		ret = cci_write(imx376->regmap, IMX376_REG_EXPOSURE,
				ctrl->val, NULL);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = imx376_update_digital_gain(imx376, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = cci_write(imx376->regmap, IMX376_REG_TEST_PATTERN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_VBLANK:
		ret = cci_write(imx376->regmap, IMX376_REG_FRM_LENGTH_LINES,
				mode->height + ctrl->val, NULL);
		break;
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
		ret = cci_write(imx376->regmap, REG_MIRROR_FLIP_CONTROL,
				(imx376->hflip->val ?
				 REG_CONFIG_MIRROR_HFLIP : 0) |
				(imx376->vflip->val ?
				 REG_CONFIG_MIRROR_VFLIP : 0),
				NULL);
		break;
	case V4L2_CID_LINK_FREQ:
	case V4L2_CID_PIXEL_RATE:
	case V4L2_CID_HBLANK:
		/* Read-only controls, only updated internally on mode change */
		ret = 0;
		break;
	default:
		dev_dbg(&client->dev,
			"ctrl(id:0x%x,val:0x%x) is not handled\n",
			ctrl->id, ctrl->val);
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx376_ctrl_ops = {
	.s_ctrl = imx376_set_ctrl,
};

static int imx376_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx376 *imx376 = to_imx376(sd);

	/* Only one bayer format (10 bit) is supported */
	if (code->index > 0)
		return -EINVAL;

	code->code = imx376_get_format_code(imx376);

	return 0;
}

static int imx376_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx376 *imx376 = to_imx376(sd);

	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != imx376_get_format_code(imx376))
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int imx376_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx376 *imx376 = to_imx376(sd);
	const struct imx376_link_freq_config *link_freq_cfg;
	const struct imx376_mode *mode;
	struct v4l2_mbus_framefmt *framefmt;
	s32 vblank_def;
	s32 vblank_min;
	s64 h_blank;
	s64 pixel_rate;

	/* The mode cannot be changed while streaming */
	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE && imx376->streaming)
		return -EBUSY;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	fmt->format.code = imx376_get_format_code(imx376);
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;

	framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
	*framefmt = fmt->format;
	*v4l2_subdev_state_get_crop(sd_state, fmt->pad) = mode->crop;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		__v4l2_ctrl_s_ctrl(imx376->link_freq, mode->link_freq_index);

		link_freq_cfg = &link_freq_configs[mode->link_freq_index];

		pixel_rate = link_freq_cfg->pixel_rate;
		__v4l2_ctrl_modify_range(imx376->pixel_rate, pixel_rate,
					 pixel_rate, 1, pixel_rate);
		/* Update limits and set FPS to default */
		vblank_def = mode->vts_def - mode->height;
		vblank_min = mode->vts_min - mode->height;
		__v4l2_ctrl_modify_range(imx376->vblank, vblank_min,
					 IMX376_VTS_MAX - mode->height, 1,
					 vblank_def);
		__v4l2_ctrl_s_ctrl(imx376->vblank, vblank_def);
		h_blank = link_freq_cfg->pixels_per_line - mode->width;
		__v4l2_ctrl_modify_range(imx376->hblank, h_blank,
					 h_blank, 1, h_blank);
	}

	return 0;
}

static int imx376_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_state_get_crop(sd_state, sel->pad);

		return 0;

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX376_NATIVE_WIDTH;
		sel->r.height = IMX376_NATIVE_HEIGHT;

		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = IMX376_PIXEL_ARRAY_LEFT;
		sel->r.top = IMX376_PIXEL_ARRAY_TOP;
		sel->r.width = IMX376_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX376_PIXEL_ARRAY_HEIGHT;

		return 0;
	}

	return -EINVAL;
}

/* Start streaming */
static int imx376_start_streaming(struct imx376 *imx376,
				  const struct imx376_mode *mode)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx376->sd);
	const struct imx376_reg_list *reg_list;
	int ret;

	ret = cci_write(imx376->regmap, IMX376_REG_RESET, 0x01, NULL);
	if (ret) {
		dev_err(&client->dev, "%s failed to reset sensor\n", __func__);
		return ret;
	}

	/* 12ms is required from poweron to standby */
	fsleep(12000);

	/* Setup PLL */
	reg_list = &link_freq_configs[mode->link_freq_index].reg_list;
	ret = cci_multi_reg_write(imx376->regmap, reg_list->regs,
				  reg_list->num_of_regs, NULL);
	if (ret) {
		dev_err(&client->dev, "%s failed to set plls\n", __func__);
		return ret;
	}

	ret = cci_multi_reg_write(imx376->regmap, mode_common_regs,
				  ARRAY_SIZE(mode_common_regs), NULL);
	if (ret) {
		dev_err(&client->dev, "%s failed to set common regs\n", __func__);
		return ret;
	}

	/* Apply default values of current mode */
	reg_list = &mode->reg_list;
	ret = cci_multi_reg_write(imx376->regmap, reg_list->regs, reg_list->num_of_regs, NULL);
	if (ret) {
		dev_err(&client->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(imx376->sd.ctrl_handler);
	if (ret)
		return ret;

	/* set stream on register */
	return cci_write(imx376->regmap, IMX376_REG_MODE_SELECT,
			 IMX376_MODE_STREAMING, NULL);
}

/* Stop streaming */
static int imx376_stop_streaming(struct imx376 *imx376)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx376->sd);
	int ret;

	/* set stream off register */
	ret = cci_write(imx376->regmap, IMX376_REG_MODE_SELECT,
			IMX376_MODE_STANDBY, NULL);
	if (ret)
		dev_err(&client->dev, "%s failed to set stream\n", __func__);

	/*
	 * Return success even if it was an error, as there is nothing the
	 * caller can do about it.
	 */
	return 0;
}

static int imx376_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx376 *imx376 = to_imx376(sd);
	int ret;

	ret = regulator_bulk_enable(IMX376_NUM_SUPPLIES,
				    imx376->supplies);
	if (ret) {
		dev_err(dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	usleep_range(400, 600);

	gpiod_set_value_cansleep(imx376->reset_gpio, 0);

	ret = clk_prepare_enable(imx376->clk);
	if (ret) {
		dev_err(dev, "failed to enable inclk\n");
		goto error_reset;
	}

	usleep_range(1000, 1200);

	return 0;

error_reset:
	gpiod_set_value_cansleep(imx376->reset_gpio, 1);
	regulator_bulk_disable(IMX376_NUM_SUPPLIES, imx376->supplies);

	return ret;
}

static int imx376_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx376 *imx376 = to_imx376(sd);

	clk_disable_unprepare(imx376->clk);

	gpiod_set_value_cansleep(imx376->reset_gpio, 1);

	regulator_bulk_disable(IMX376_NUM_SUPPLIES, imx376->supplies);

	return 0;
}

static int imx376_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx376 *imx376 = to_imx376(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct v4l2_subdev_state *state;
	int ret = 0;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	if (enable) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0)
			goto err_unlock;

		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = imx376_start_streaming(imx376, imx376_get_mode(state));
		if (ret)
			goto err_rpm_put;

		/* The Bayer order cannot change during streaming */
		__v4l2_ctrl_grab(imx376->vflip, true);
		__v4l2_ctrl_grab(imx376->hflip, true);

		imx376->streaming = true;
	} else {
		imx376_stop_streaming(imx376);
		__v4l2_ctrl_grab(imx376->vflip, false);
		__v4l2_ctrl_grab(imx376->hflip, false);
		imx376->streaming = false;
		pm_runtime_put(&client->dev);
	}

	v4l2_subdev_unlock_state(state);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	v4l2_subdev_unlock_state(state);

	return ret;
}

/* Verify chip ID */
static int imx376_identify_module(struct imx376 *imx376)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx376->sd);
	int ret;
	u64 val;

	ret = cci_read(imx376->regmap, IMX376_REG_CHIP_ID,
		       &val, NULL);
	if (ret) {
		dev_err(&client->dev, "failed to read chip id %x\n",
			IMX376_CHIP_ID);
		return ret;
	}

	if (val != IMX376_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%llx\n",
			IMX376_CHIP_ID, val);
		return -EIO;
	}

	return 0;
}

static const struct v4l2_subdev_video_ops imx376_video_ops = {
	.s_stream = imx376_set_stream,
};

static const struct v4l2_subdev_pad_ops imx376_pad_ops = {
	.enum_mbus_code = imx376_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = imx376_set_pad_format,
	.enum_frame_size = imx376_enum_frame_size,
	.get_selection = imx376_get_selection,
};

static const struct v4l2_subdev_ops imx376_subdev_ops = {
	.video = &imx376_video_ops,
	.pad = &imx376_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx376_internal_ops = {
	.init_state = imx376_init_state,
};

/* Initialize control handlers */
static int imx376_init_controls(struct imx376 *imx376)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx376->sd);
	const struct imx376_mode *mode = &supported_modes[0];
	const struct imx376_link_freq_config *link_freq_cfg;
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	s64 vblank_def;
	s64 vblank_min;
	s64 pixel_rate;
	s64 hblank;
	int ret;

	ctrl_hdlr = &imx376->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 12);
	if (ret)
		return ret;

	imx376->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx376_ctrl_ops,
						   V4L2_CID_LINK_FREQ,
						   ARRAY_SIZE(link_freq_menu_items) - 1,
						   0, link_freq_menu_items);

	if (imx376->link_freq)
		imx376->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx376->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx376_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 1);
	if (imx376->hflip)
		imx376->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	imx376->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx376_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 1);
	if (imx376->vflip)
		imx376->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	link_freq_cfg = &link_freq_configs[mode->link_freq_index];
	pixel_rate = link_freq_cfg->pixel_rate;
	dev_dbg(&client->dev, "pixel_rate: %lld\n", pixel_rate);

	/* By default, PIXEL_RATE is read only */
	imx376->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &imx376_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       pixel_rate, pixel_rate,
					       1, pixel_rate);

	vblank_def = mode->vts_def - mode->height;
	vblank_min = mode->vts_min - mode->height;
	imx376->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx376_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_min,
					   IMX376_VTS_MAX - mode->height, 1,
					   vblank_def);

	hblank = link_freq_cfg->pixels_per_line - mode->width;
	imx376->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx376_ctrl_ops,
					   V4L2_CID_HBLANK, hblank, hblank,
					   1, hblank);

	if (imx376->hblank)
		imx376->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx376->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx376_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX376_EXPOSURE_MIN,
					     IMX376_EXPOSURE_MAX,
					     IMX376_EXPOSURE_STEP,
					     IMX376_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx376_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  IMX376_ANA_GAIN_MIN, IMX376_ANA_GAIN_MAX,
			  IMX376_ANA_GAIN_STEP, IMX376_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx376_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  IMX376_DGTL_GAIN_MIN, IMX376_DGTL_GAIN_MAX,
			  IMX376_DGTL_GAIN_STEP, IMX376_DGTL_GAIN_DEFAULT);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx376_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx376_test_pattern_menu) - 1,
				     0, 0, imx376_test_pattern_menu);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n",
			__func__, ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx376_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	imx376->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static void imx376_free_controls(struct imx376 *imx376)
{
	v4l2_ctrl_handler_free(imx376->sd.ctrl_handler);
}

static int imx376_get_regulators(struct imx376 *imx376,
				 struct i2c_client *client)
{
	unsigned int i;

	for (i = 0; i < IMX376_NUM_SUPPLIES; i++)
		imx376->supplies[i].supply = imx376_supply_name[i];

	return devm_regulator_bulk_get(&client->dev,
				    IMX376_NUM_SUPPLIES, imx376->supplies);
}

/*
 * Check that the endpoint describes the only supported configuration: four
 * data lanes and a 500 MHz link frequency.
 */
static int imx376_check_endpoint(struct device *dev)
{
	struct v4l2_fwnode_endpoint ep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct fwnode_handle *endpoint;
	unsigned long bitmap;
	int ret;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint)
		return dev_err_probe(dev, -EINVAL, "endpoint node not found\n");

	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep);
	fwnode_handle_put(endpoint);
	if (ret)
		return dev_err_probe(dev, ret, "parsing endpoint node failed\n");

	if (ep.bus.mipi_csi2.num_data_lanes != 4) {
		ret = dev_err_probe(dev, -EINVAL, "invalid data lanes: %u\n",
				    ep.bus.mipi_csi2.num_data_lanes);
		goto out_free;
	}

	ret = v4l2_link_freq_to_bitmap(dev, ep.link_frequencies,
				       ep.nr_of_link_frequencies,
				       link_freq_menu_items,
				       ARRAY_SIZE(link_freq_menu_items),
				       &bitmap);
	if (ret)
		dev_err_probe(dev, ret, "link frequency not supported\n");

out_free:
	v4l2_fwnode_endpoint_free(&ep);

	return ret;
}

static int imx376_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx376 *imx376;
	int ret;

	imx376 = devm_kzalloc(dev, sizeof(*imx376), GFP_KERNEL);
	if (!imx376)
		return -ENOMEM;

	imx376->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(imx376->regmap))
		return dev_err_probe(dev, PTR_ERR(imx376->regmap),
				     "failed to initialize CCI\n");

	ret = imx376_get_regulators(imx376, client);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	imx376->clk = devm_v4l2_sensor_clk_get(dev, NULL);
	if (IS_ERR(imx376->clk))
		return dev_err_probe(dev, PTR_ERR(imx376->clk),
				     "failed to get clock\n");

	if (clk_get_rate(imx376->clk) != IMX376_INCK_FREQ)
		return dev_err_probe(dev, -EINVAL,
				     "input clock frequency must be %u Hz\n",
				     IMX376_INCK_FREQ);

	ret = imx376_check_endpoint(dev);
	if (ret)
		return ret;

	/* request optional reset pin */
	imx376->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(imx376->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(imx376->reset_gpio),
				     "failed to get reset gpio\n");

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&imx376->sd, client, &imx376_subdev_ops);

	/* Will be powered off via pm_runtime_idle */
	ret = imx376_power_on(dev);
	if (ret)
		return ret;

	/* Check module identity */
	ret = imx376_identify_module(imx376);
	if (ret)
		goto error_identify;

	ret = imx376_init_controls(imx376);
	if (ret)
		goto error_identify;

	/* Initialize subdev */
	imx376->sd.internal_ops = &imx376_internal_ops;
	imx376->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx376->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	imx376->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx376->sd.entity, 1, &imx376->pad);
	if (ret)
		goto error_handler_free;

	imx376->sd.state_lock = imx376->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&imx376->sd);
	if (ret < 0) {
		dev_err_probe(dev, ret, "subdev init error\n");
		goto error_media_entity;
	}

	/*
	 * The device is powered on at this point. Enable runtime PM before
	 * registering the subdev, so that it can be used as soon as it is
	 * visible.
	 */
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = v4l2_async_register_subdev_sensor(&imx376->sd);
	if (ret < 0) {
		dev_err_probe(dev, ret,
			      "failed to register sensor sub-device\n");
		goto error_subdev_cleanup;
	}

	pm_runtime_idle(dev);

	return 0;

error_subdev_cleanup:
	v4l2_subdev_cleanup(&imx376->sd);
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);

error_media_entity:
	media_entity_cleanup(&imx376->sd.entity);

error_handler_free:
	imx376_free_controls(imx376);

error_identify:
	imx376_power_off(dev);

	return ret;
}

static void imx376_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx376 *imx376 = to_imx376(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	imx376_free_controls(imx376);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev)) {
		imx376_power_off(&client->dev);
		pm_runtime_set_suspended(&client->dev);
	}
}

static const struct dev_pm_ops imx376_pm_ops = {
	SET_RUNTIME_PM_OPS(imx376_power_off, imx376_power_on, NULL)
};

static const struct of_device_id imx376_dt_ids[] = {
	{ .compatible = "sony,imx376"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx376_dt_ids);

static struct i2c_driver imx376_i2c_driver = {
	.driver = {
		.name = "imx376",
		.pm = &imx376_pm_ops,
		.of_match_table	= imx376_dt_ids,
	},
	.probe = imx376_probe,
	.remove = imx376_remove,
};

module_i2c_driver(imx376_i2c_driver);

MODULE_AUTHOR("Gianluca Boiano <morf3089@gmail.com>");
MODULE_AUTHOR("Yeh, Andy <andy.yeh@intel.com>");
MODULE_AUTHOR("Chiang, Alan");
MODULE_AUTHOR("Chen, Jason");
MODULE_DESCRIPTION("Sony IMX376 sensor driver");
MODULE_LICENSE("GPL");
