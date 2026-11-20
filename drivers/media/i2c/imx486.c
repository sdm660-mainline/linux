// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Intel Corporation
/*
 * Sony IMX486 image sensor driver.
 *
 * Derived from imx376.c, which is based on imx258.c (Copyright (C) 2018 Intel
 * Corporation) via the imx376 driver from
 * https://github.com/GH4NG/meizu-m1882-mainline-linux
 *
 * Register settings from the MediaTek imx486mipiraw driver.
 */

#include <linux/bits.h>
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
#define IMX486_INCK_FREQ		24000000

#define IMX486_REG_MODE_SELECT		CCI_REG8(0x0100)
#define IMX486_MODE_STANDBY		0x00
#define IMX486_MODE_STREAMING		0x01

#define IMX486_REG_RESET		CCI_REG8(0x0103)

/* Chip ID */
#define IMX486_REG_CHIP_ID		CCI_REG16(0x0016)
#define IMX486_CHIP_ID			0x0486

/* V_TIMING internal */
#define IMX486_VTS_30FPS		3068
#define IMX486_VTS_MAX			65525

/* HBLANK control - read only */
#define IMX486_PPL_DEFAULT		4192
#define IMX486_VTS_BINNED		2670
#define IMX486_PPL_BINNED		2096

/* Exposure control */
#define IMX486_REG_EXPOSURE		CCI_REG16(0x0202)
#define IMX486_EXPOSURE_OFFSET		10
#define IMX486_EXPOSURE_MIN		4
#define IMX486_EXPOSURE_STEP		1
#define IMX486_EXPOSURE_DEFAULT		0x0bf2
#define IMX486_EXPOSURE_MAX		(IMX486_VTS_MAX - IMX486_EXPOSURE_OFFSET)

/* Analog gain control */
#define IMX486_REG_ANALOG_GAIN		CCI_REG16(0x0204)
#define IMX486_ANA_GAIN_MIN		0
#define IMX486_ANA_GAIN_MAX		480
#define IMX486_ANA_GAIN_STEP		1
#define IMX486_ANA_GAIN_DEFAULT		0x0

/* Digital gain control */
#define IMX486_REG_GR_DIGITAL_GAIN	CCI_REG16(0x020e)
#define IMX486_REG_R_DIGITAL_GAIN	CCI_REG16(0x0210)
#define IMX486_REG_B_DIGITAL_GAIN	CCI_REG16(0x0212)
#define IMX486_REG_GB_DIGITAL_GAIN	CCI_REG16(0x0214)
#define IMX486_DGTL_GAIN_MIN		0
#define IMX486_DGTL_GAIN_MAX		4096	/* Max = 0xFFF */
#define IMX486_DGTL_GAIN_DEFAULT	1024
#define IMX486_DGTL_GAIN_STEP		1

/* Test Pattern Control */
#define IMX486_REG_TEST_PATTERN		CCI_REG16(0x0600)

/* Orientation */
#define REG_MIRROR_FLIP_CONTROL		CCI_REG8(0x0101)
#define REG_CONFIG_MIRROR_HFLIP		0x01
#define REG_CONFIG_MIRROR_VFLIP		0x02

/* IMX486 native and active pixel array size. */
#define IMX486_NATIVE_WIDTH		4032U
#define IMX486_NATIVE_HEIGHT		3016U
#define IMX486_PIXEL_ARRAY_LEFT		0U
#define IMX486_PIXEL_ARRAY_TOP		0U
#define IMX486_PIXEL_ARRAY_WIDTH	4032U
#define IMX486_PIXEL_ARRAY_HEIGHT	3016U

/* CCS regs */
#define IMX486_REG_FRM_LENGTH_LINES	CCI_REG16(0x0340)
#define IMX486_REG_EXCK_FREQ		CCI_REG16(0x0136)
#define IMX486_REG_IVTPXCK_DIV		CCI_REG8(0x0301)
#define IMX486_REG_IVTSYCK_DIV		CCI_REG8(0x0303)
#define IMX486_REG_PREPLLCK_VT_DIV	CCI_REG8(0x0305)
#define IMX486_REG_PLL_IVT_MPY		CCI_REG16(0x0306)
#define IMX486_REG_IOPSYCK_DIV		CCI_REG8(0x030b)
#define IMX486_REG_PREPLLCK_OP_DIV	CCI_REG8(0x030d)
#define IMX486_REG_PLL_IOP_MPY		CCI_REG16(0x030e)
#define IMX486_REG_PLL_MULT_DRIV	CCI_REG8(0x0310)
#define IMX486_REG_CSI_LANE_MODE	CCI_REG8(0x0114)
#define IMX486_REG_REQ_LINK_BIT_RATE_MBPS_H	CCI_REG16(0x0820)
#define IMX486_REG_REQ_LINK_BIT_RATE_MBPS_L	CCI_REG16(0x0822)

struct imx486_reg_list {
	u32 num_of_regs;
	const struct cci_reg_sequence *regs;
};

/* Link frequency config */
struct imx486_link_freq_config {
	u32 pixels_per_line;

	/*
	 * Video timing pixel rate:
	 * INCK / PREPLLCK_VT_DIV * PLL_IVT_MPY / IVTPXCK_DIV * 2
	 */
	u64 pixel_rate;

	/* PLL configuration for this link frequency */
	struct imx486_reg_list reg_list;
};

/* Mode : resolution and related config&values */
struct imx486_mode {
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
	struct imx486_reg_list reg_list;

	/* Analog crop rectangle */
	struct v4l2_rect crop;
};

static const struct cci_reg_sequence mipi_984mbps_24mhz_4l[] = {
	{ IMX486_REG_EXCK_FREQ, 0x1800 },
	{ IMX486_REG_IVTPXCK_DIV, 6 },
	{ IMX486_REG_IVTSYCK_DIV, 2 },
	{ IMX486_REG_PREPLLCK_VT_DIV, 4 },
	{ IMX486_REG_PLL_IVT_MPY, 193 },
	{ IMX486_REG_IOPSYCK_DIV, 1 },
	{ IMX486_REG_PREPLLCK_OP_DIV, 2 },
	{ IMX486_REG_PLL_IOP_MPY, 82 },
	{ IMX486_REG_PLL_MULT_DRIV, 1 },
	{ IMX486_REG_CSI_LANE_MODE, 3 },
	{ IMX486_REG_REQ_LINK_BIT_RATE_MBPS_H, 984 * 4 },
	{ IMX486_REG_REQ_LINK_BIT_RATE_MBPS_L, 0 },
};

static const struct cci_reg_sequence mode_common_regs[] = {
	{ CCI_REG8(0x4979), 0x0D },
	{ CCI_REG8(0x3040), 0x03 },
	{ CCI_REG8(0x3041), 0x04 },
	{ CCI_REG8(0x4340), 0x01 },
	{ CCI_REG8(0x4341), 0x02 },
	{ CCI_REG8(0x4342), 0x04 },
	{ CCI_REG8(0x4344), 0x00 },
	{ CCI_REG8(0x4345), 0x2C },
	{ CCI_REG8(0x4346), 0x00 },
	{ CCI_REG8(0x4347), 0x26 },
	{ CCI_REG8(0x434C), 0x00 },
	{ CCI_REG8(0x434D), 0x31 },
	{ CCI_REG8(0x434E), 0x00 },
	{ CCI_REG8(0x434F), 0x2C },
	{ CCI_REG8(0x4350), 0x00 },
	{ CCI_REG8(0x4351), 0x0A },
	{ CCI_REG8(0x4354), 0x00 },
	{ CCI_REG8(0x4355), 0x2A },
	{ CCI_REG8(0x4356), 0x00 },
	{ CCI_REG8(0x4357), 0x1F },
	{ CCI_REG8(0x435C), 0x00 },
	{ CCI_REG8(0x435D), 0x2F },
	{ CCI_REG8(0x435E), 0x00 },
	{ CCI_REG8(0x435F), 0x2A },
	{ CCI_REG8(0x4360), 0x00 },
	{ CCI_REG8(0x4361), 0x0A },
	{ CCI_REG8(0x4600), 0x2A },
	{ CCI_REG8(0x4601), 0x30 },
	{ CCI_REG8(0x4920), 0x0A },
	{ CCI_REG8(0x4926), 0x01 },
	{ CCI_REG8(0x4929), 0x01 },
	{ CCI_REG8(0x4938), 0x01 },
	{ CCI_REG8(0x4959), 0x15 },
	{ CCI_REG8(0x4A38), 0x0A },
	{ CCI_REG8(0x4A39), 0x05 },
	{ CCI_REG8(0x4A3C), 0x0F },
	{ CCI_REG8(0x4A3D), 0x05 },
	{ CCI_REG8(0x4A3E), 0x05 },
	{ CCI_REG8(0x4A40), 0x0F },
	{ CCI_REG8(0x4A4A), 0x01 },
	{ CCI_REG8(0x4A4B), 0x01 },
	{ CCI_REG8(0x4A4C), 0x01 },
	{ CCI_REG8(0x4A4D), 0x01 },
	{ CCI_REG8(0x4A4E), 0x01 },
	{ CCI_REG8(0x4A4F), 0x01 },
	{ CCI_REG8(0x4A50), 0x01 },
	{ CCI_REG8(0x4A51), 0x01 },
	{ CCI_REG8(0x4A76), 0x16 },
	{ CCI_REG8(0x4A77), 0x16 },
	{ CCI_REG8(0x4A78), 0x16 },
	{ CCI_REG8(0x4A79), 0x16 },
	{ CCI_REG8(0x4A7A), 0x16 },
	{ CCI_REG8(0x4A7B), 0x16 },
	{ CCI_REG8(0x4A7C), 0x16 },
	{ CCI_REG8(0x4A7D), 0x16 },
	{ CCI_REG8(0x555E), 0x01 },
	{ CCI_REG8(0x5563), 0x01 },
	{ CCI_REG8(0x70DA), 0x02 },
};

static const struct cci_reg_sequence mode_4032x3016_regs[] = {
	{ CCI_REG8(0x0112), 0x0A },
	{ CCI_REG8(0x0113), 0x0A },
	{ CCI_REG8(0x0342), 0x10 },
	{ CCI_REG8(0x0343), 0x60 },
	{ CCI_REG8(0x0340), 0x0B },
	{ CCI_REG8(0x0341), 0xFC },
	{ CCI_REG8(0x0344), 0x00 },
	{ CCI_REG8(0x0345), 0x00 },
	{ CCI_REG8(0x0346), 0x00 },
	{ CCI_REG8(0x0347), 0x00 },
	{ CCI_REG8(0x0348), 0x0F },
	{ CCI_REG8(0x0349), 0xBF },
	{ CCI_REG8(0x034A), 0x0B },
	{ CCI_REG8(0x034B), 0xC7 },
	{ CCI_REG8(0x0220), 0x00 },
	{ CCI_REG8(0x0222), 0x01 },
	{ CCI_REG8(0x0900), 0x00 },
	{ CCI_REG8(0x0901), 0x11 },
	{ CCI_REG8(0x0902), 0x00 },
	{ CCI_REG8(0x3130), 0x01 },
	{ CCI_REG8(0x034C), 0x0F },
	{ CCI_REG8(0x034D), 0xC0 },
	{ CCI_REG8(0x034E), 0x0B },
	{ CCI_REG8(0x034F), 0xC8 },
	{ CCI_REG8(0x0700), 0x00 },
	{ CCI_REG8(0x0701), 0x50 },
	{ CCI_REG8(0x3100), 0x06 },
	{ CCI_REG8(0x7036), 0x00 },
	{ CCI_REG8(0x704E), 0x00 },
	{ CCI_REG8(0x0202), 0x0B },
	{ CCI_REG8(0x0203), 0xF2 },
	{ CCI_REG8(0x0224), 0x01 },
	{ CCI_REG8(0x0225), 0xF4 },
	{ CCI_REG8(0x0204), 0x00 },
	{ CCI_REG8(0x0205), 0x00 },
	{ CCI_REG8(0x020F), 0x00 },
	{ CCI_REG8(0x0216), 0x00 },
	{ CCI_REG8(0x0217), 0x00 },
	{ CCI_REG8(0x0226), 0x01 },
	{ CCI_REG8(0x0227), 0x00 },
};

static const struct cci_reg_sequence mipi_504mbps_24mhz_4l[] = {
	{ IMX486_REG_EXCK_FREQ, 0x1800 },
	{ IMX486_REG_IVTPXCK_DIV, 6 },
	{ IMX486_REG_IVTSYCK_DIV, 2 },
	{ IMX486_REG_PREPLLCK_VT_DIV, 4 },
	{ IMX486_REG_PLL_IVT_MPY, 84 },
	{ IMX486_REG_IOPSYCK_DIV, 1 },
	{ IMX486_REG_PREPLLCK_OP_DIV, 2 },
	{ IMX486_REG_PLL_IOP_MPY, 42 },
	{ IMX486_REG_PLL_MULT_DRIV, 1 },
	{ IMX486_REG_CSI_LANE_MODE, 3 },
	{ IMX486_REG_REQ_LINK_BIT_RATE_MBPS_H, 504 * 4 },
	{ IMX486_REG_REQ_LINK_BIT_RATE_MBPS_L, 0 },
};

/* 2x2 binned full field of view */
static const struct cci_reg_sequence mode_2016x1508_regs[] = {
	{ CCI_REG8(0x0112), 0x0A },
	{ CCI_REG8(0x0113), 0x0A },
	{ CCI_REG8(0x0342), 0x08 },
	{ CCI_REG8(0x0343), 0x30 },
	{ CCI_REG8(0x0340), 0x0A },
	{ CCI_REG8(0x0341), 0x6E },
	{ CCI_REG8(0x0344), 0x00 },
	{ CCI_REG8(0x0345), 0x00 },
	{ CCI_REG8(0x0346), 0x00 },
	{ CCI_REG8(0x0347), 0x00 },
	{ CCI_REG8(0x0348), 0x0F },
	{ CCI_REG8(0x0349), 0xBF },
	{ CCI_REG8(0x034A), 0x0B },
	{ CCI_REG8(0x034B), 0xC7 },
	{ CCI_REG8(0x0220), 0x00 },
	{ CCI_REG8(0x0222), 0x01 },
	{ CCI_REG8(0x0900), 0x01 },
	{ CCI_REG8(0x0901), 0x22 },
	{ CCI_REG8(0x0902), 0x00 },
	{ CCI_REG8(0x3130), 0x01 },
	{ CCI_REG8(0x034C), 0x07 },
	{ CCI_REG8(0x034D), 0xE0 },
	{ CCI_REG8(0x034E), 0x05 },
	{ CCI_REG8(0x034F), 0xE4 },
	{ CCI_REG8(0x0700), 0x01 },
	{ CCI_REG8(0x0701), 0x50 },
	{ CCI_REG8(0x3100), 0x06 },
	{ CCI_REG8(0x7036), 0x02 },
	{ CCI_REG8(0x704E), 0x02 },
	{ CCI_REG8(0x0202), 0x0A },
	{ CCI_REG8(0x0203), 0x64 },
	{ CCI_REG8(0x0224), 0x01 },
	{ CCI_REG8(0x0225), 0xF4 },
	{ CCI_REG8(0x0204), 0x00 },
	{ CCI_REG8(0x0205), 0x00 },
	{ CCI_REG8(0x020E), 0x01 },
	{ CCI_REG8(0x020F), 0x00 },
	{ CCI_REG8(0x0216), 0x00 },
	{ CCI_REG8(0x0217), 0x00 },
	{ CCI_REG8(0x0226), 0x01 },
	{ CCI_REG8(0x0227), 0x00 },
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

static const char * const imx486_test_pattern_menu[] = {
	"Disabled",
	"Solid Colour",
	"Eight Vertical Colour Bars",
	"Colour Bars With Fade to Grey",
	"Pseudorandom Sequence (PN9)",
};

/* regulator supplies */
static const char * const imx486_supply_name[] = {
	/* Supplies can be enabled in any order */
	"vana",  /* Analog (2.8V) supply */
	"vcore",  /* Digital Core (1.2V) supply */
	"vio",  /* IF (1.8V) supply */
};

#define IMX486_NUM_SUPPLIES ARRAY_SIZE(imx486_supply_name)

enum {
	IMX486_LINK_FREQ_984MBPS,
	IMX486_LINK_FREQ_504MBPS,
};

static const s64 link_freq_menu_items[] = {
	[IMX486_LINK_FREQ_984MBPS] = 492000000ULL,
	[IMX486_LINK_FREQ_504MBPS] = 252000000ULL,
};

static const struct imx486_link_freq_config link_freq_configs[] = {
	[IMX486_LINK_FREQ_984MBPS] = {
		.pixels_per_line = IMX486_PPL_DEFAULT,
		.pixel_rate = 386000000,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_984mbps_24mhz_4l),
			.regs = mipi_984mbps_24mhz_4l,
		},
	},
	[IMX486_LINK_FREQ_504MBPS] = {
		.pixels_per_line = IMX486_PPL_BINNED,
		.pixel_rate = 168000000,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_504mbps_24mhz_4l),
			.regs = mipi_504mbps_24mhz_4l,
		},
	},
};

/* Mode configs */
static const struct imx486_mode supported_modes[] = {
	{
		.width = 4032,
		.height = 3016,
		.vts_def = IMX486_VTS_30FPS,
		.vts_min = IMX486_VTS_30FPS,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_4032x3016_regs),
			.regs = mode_4032x3016_regs,
		},
		.link_freq_index = IMX486_LINK_FREQ_984MBPS,
		.crop = {
			.left = IMX486_PIXEL_ARRAY_LEFT,
			.top = IMX486_PIXEL_ARRAY_TOP,
			.width = IMX486_PIXEL_ARRAY_WIDTH,
			.height = IMX486_PIXEL_ARRAY_HEIGHT,
		},
	},
	{
		.width = 2016,
		.height = 1508,
		.vts_def = IMX486_VTS_BINNED,
		.vts_min = IMX486_VTS_BINNED,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2016x1508_regs),
			.regs = mode_2016x1508_regs,
		},
		.link_freq_index = IMX486_LINK_FREQ_504MBPS,
		.crop = {
			.left = IMX486_PIXEL_ARRAY_LEFT,
			.top = IMX486_PIXEL_ARRAY_TOP,
			.width = IMX486_PIXEL_ARRAY_WIDTH,
			.height = IMX486_PIXEL_ARRAY_HEIGHT,
		},
	},
};

struct imx486 {
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
	struct regulator_bulk_data supplies[IMX486_NUM_SUPPLIES];
};

static inline struct imx486 *to_imx486(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx486, sd);
}

/*
 * Get bayer order based on flip setting. Called with the subdev state lock
 * (shared with the control handler) held.
 */
static u32 imx486_get_format_code(const struct imx486 *imx486)
{
	unsigned int i;

	i = (imx486->vflip->val ? 2 : 0) |
	    (imx486->hflip->val ? 1 : 0);

	return codes[i];
}

static int imx486_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	struct imx486 *imx486 = to_imx486(sd);
	struct v4l2_mbus_framefmt *fmt =
		v4l2_subdev_state_get_format(state, 0);
	struct v4l2_rect *crop = v4l2_subdev_state_get_crop(state, 0);

	/* Initialize the format */
	fmt->width = supported_modes[0].width;
	fmt->height = supported_modes[0].height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->code = imx486_get_format_code(imx486);

	/* Initialize the crop rectangle */
	crop->left = IMX486_PIXEL_ARRAY_LEFT;
	crop->top = IMX486_PIXEL_ARRAY_TOP;
	crop->width = IMX486_PIXEL_ARRAY_WIDTH;
	crop->height = IMX486_PIXEL_ARRAY_HEIGHT;

	return 0;
}

static int imx486_update_digital_gain(struct imx486 *imx486, u32 val)
{
	int ret = 0;

	cci_write(imx486->regmap, IMX486_REG_GR_DIGITAL_GAIN, val, &ret);
	cci_write(imx486->regmap, IMX486_REG_GB_DIGITAL_GAIN, val, &ret);
	cci_write(imx486->regmap, IMX486_REG_R_DIGITAL_GAIN, val, &ret);
	cci_write(imx486->regmap, IMX486_REG_B_DIGITAL_GAIN, val, &ret);

	return ret;
}

/* Find the mode matching the format stored in the given state. */
static const struct imx486_mode *
imx486_get_mode(struct v4l2_subdev_state *state)
{
	const struct v4l2_mbus_framefmt *fmt =
		v4l2_subdev_state_get_format(state, 0);

	return v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height, fmt->width, fmt->height);
}

static void imx486_adjust_exposure_range(struct imx486 *imx486,
					 const struct imx486_mode *mode)
{
	int exposure_max, exposure_def;

	/* Honour the VBLANK limits when setting exposure. */
	exposure_max = mode->height + imx486->vblank->val -
		       IMX486_EXPOSURE_OFFSET;
	exposure_def = min(exposure_max, imx486->exposure->val);
	__v4l2_ctrl_modify_range(imx486->exposure, imx486->exposure->minimum,
				 exposure_max, imx486->exposure->step,
				 exposure_def);
}

static int imx486_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx486 *imx486 =
		container_of(ctrl->handler, struct imx486, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&imx486->sd);
	struct v4l2_subdev_state *state =
		v4l2_subdev_get_locked_active_state(&imx486->sd);
	const struct imx486_mode *mode = imx486_get_mode(state);
	int ret = 0;

	/*
	 * The VBLANK control may change the limits of usable exposure, so check
	 * and adjust if necessary.
	 */
	if (ctrl->id == V4L2_CID_VBLANK)
		imx486_adjust_exposure_range(imx486, mode);

	/* The Bayer order depends on the flips, keep the active format in sync */
	if (ctrl->id == V4L2_CID_HFLIP || ctrl->id == V4L2_CID_VFLIP)
		v4l2_subdev_state_get_format(state, 0)->code =
			imx486_get_format_code(imx486);

	/*
	 * Applying V4L2 control value only happens
	 * when power is up for streaming
	 */
	if (pm_runtime_get_if_in_use(&client->dev) <= 0)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(imx486->regmap, IMX486_REG_ANALOG_GAIN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_EXPOSURE:
		ret = cci_write(imx486->regmap, IMX486_REG_EXPOSURE,
				ctrl->val, NULL);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = imx486_update_digital_gain(imx486, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = cci_write(imx486->regmap, IMX486_REG_TEST_PATTERN,
				ctrl->val, NULL);
		break;
	case V4L2_CID_VBLANK:
		ret = cci_write(imx486->regmap, IMX486_REG_FRM_LENGTH_LINES,
				mode->height + ctrl->val, NULL);
		break;
	case V4L2_CID_VFLIP:
	case V4L2_CID_HFLIP:
		ret = cci_write(imx486->regmap, REG_MIRROR_FLIP_CONTROL,
				(imx486->hflip->val ?
				 REG_CONFIG_MIRROR_HFLIP : 0) |
				(imx486->vflip->val ?
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

static const struct v4l2_ctrl_ops imx486_ctrl_ops = {
	.s_ctrl = imx486_set_ctrl,
};

static int imx486_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx486 *imx486 = to_imx486(sd);

	/* Only one bayer format (10 bit) is supported */
	if (code->index > 0)
		return -EINVAL;

	code->code = imx486_get_format_code(imx486);

	return 0;
}

static int imx486_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx486 *imx486 = to_imx486(sd);

	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != imx486_get_format_code(imx486))
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int imx486_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx486 *imx486 = to_imx486(sd);
	const struct imx486_link_freq_config *link_freq_cfg;
	const struct imx486_mode *mode;
	struct v4l2_mbus_framefmt *framefmt;
	s32 vblank_def;
	s32 vblank_min;
	s64 h_blank;
	s64 pixel_rate;

	/*
	 * The modes use different PLL settings, so the mode cannot be changed
	 * while streaming.
	 */
	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE && imx486->streaming)
		return -EBUSY;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);

	fmt->format.code = imx486_get_format_code(imx486);
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;

	framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
	*framefmt = fmt->format;
	*v4l2_subdev_state_get_crop(sd_state, fmt->pad) = mode->crop;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		__v4l2_ctrl_s_ctrl(imx486->link_freq, mode->link_freq_index);

		link_freq_cfg = &link_freq_configs[mode->link_freq_index];

		pixel_rate = link_freq_cfg->pixel_rate;
		__v4l2_ctrl_modify_range(imx486->pixel_rate, pixel_rate,
					 pixel_rate, 1, pixel_rate);
		/* Update limits and set FPS to default */
		vblank_def = mode->vts_def - mode->height;
		vblank_min = mode->vts_min - mode->height;
		__v4l2_ctrl_modify_range(imx486->vblank, vblank_min,
					 IMX486_VTS_MAX - mode->height, 1,
					 vblank_def);
		__v4l2_ctrl_s_ctrl(imx486->vblank, vblank_def);
		h_blank = link_freq_cfg->pixels_per_line - mode->width;
		__v4l2_ctrl_modify_range(imx486->hblank, h_blank,
					 h_blank, 1, h_blank);
	}

	return 0;
}

static int imx486_get_selection(struct v4l2_subdev *sd,
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
		sel->r.width = IMX486_NATIVE_WIDTH;
		sel->r.height = IMX486_NATIVE_HEIGHT;

		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = IMX486_PIXEL_ARRAY_LEFT;
		sel->r.top = IMX486_PIXEL_ARRAY_TOP;
		sel->r.width = IMX486_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX486_PIXEL_ARRAY_HEIGHT;

		return 0;
	}

	return -EINVAL;
}

/* Start streaming */
static int imx486_start_streaming(struct imx486 *imx486,
				  const struct imx486_mode *mode)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx486->sd);
	const struct imx486_reg_list *reg_list;
	int ret;

	ret = cci_write(imx486->regmap, IMX486_REG_RESET, 0x01, NULL);
	if (ret) {
		dev_err(&client->dev, "%s failed to reset sensor\n", __func__);
		return ret;
	}

	/* 12ms is required from poweron to standby */
	fsleep(12000);

	/* Setup PLL */
	reg_list = &link_freq_configs[mode->link_freq_index].reg_list;
	ret = cci_multi_reg_write(imx486->regmap, reg_list->regs,
				  reg_list->num_of_regs, NULL);
	if (ret) {
		dev_err(&client->dev, "%s failed to set plls\n", __func__);
		return ret;
	}

	ret = cci_multi_reg_write(imx486->regmap, mode_common_regs,
				  ARRAY_SIZE(mode_common_regs), NULL);
	if (ret) {
		dev_err(&client->dev, "%s failed to set common regs\n", __func__);
		return ret;
	}

	/* Apply default values of current mode */
	reg_list = &mode->reg_list;
	ret = cci_multi_reg_write(imx486->regmap, reg_list->regs, reg_list->num_of_regs, NULL);
	if (ret) {
		dev_err(&client->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(imx486->sd.ctrl_handler);
	if (ret)
		return ret;

	/* set stream on register */
	return cci_write(imx486->regmap, IMX486_REG_MODE_SELECT,
			 IMX486_MODE_STREAMING, NULL);
}

/* Stop streaming */
static int imx486_stop_streaming(struct imx486 *imx486)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx486->sd);
	int ret;

	/* set stream off register */
	ret = cci_write(imx486->regmap, IMX486_REG_MODE_SELECT,
			IMX486_MODE_STANDBY, NULL);
	if (ret)
		dev_err(&client->dev, "%s failed to set stream\n", __func__);

	/*
	 * Return success even if it was an error, as there is nothing the
	 * caller can do about it.
	 */
	return 0;
}

static int imx486_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx486 *imx486 = to_imx486(sd);
	int ret;

	ret = regulator_bulk_enable(IMX486_NUM_SUPPLIES,
				    imx486->supplies);
	if (ret) {
		dev_err(dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	usleep_range(400, 600);

	gpiod_set_value_cansleep(imx486->reset_gpio, 0);

	ret = clk_prepare_enable(imx486->clk);
	if (ret) {
		dev_err(dev, "failed to enable inclk\n");
		goto error_reset;
	}

	usleep_range(1000, 1200);

	return 0;

error_reset:
	gpiod_set_value_cansleep(imx486->reset_gpio, 1);
	regulator_bulk_disable(IMX486_NUM_SUPPLIES, imx486->supplies);

	return ret;
}

static int imx486_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx486 *imx486 = to_imx486(sd);

	clk_disable_unprepare(imx486->clk);

	gpiod_set_value_cansleep(imx486->reset_gpio, 1);

	regulator_bulk_disable(IMX486_NUM_SUPPLIES, imx486->supplies);

	return 0;
}

static int imx486_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx486 *imx486 = to_imx486(sd);
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
		ret = imx486_start_streaming(imx486, imx486_get_mode(state));
		if (ret)
			goto err_rpm_put;

		/* The Bayer order cannot change during streaming */
		__v4l2_ctrl_grab(imx486->vflip, true);
		__v4l2_ctrl_grab(imx486->hflip, true);

		imx486->streaming = true;
	} else {
		imx486_stop_streaming(imx486);
		__v4l2_ctrl_grab(imx486->vflip, false);
		__v4l2_ctrl_grab(imx486->hflip, false);
		imx486->streaming = false;
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
static int imx486_identify_module(struct imx486 *imx486)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx486->sd);
	int ret;
	u64 val;

	ret = cci_read(imx486->regmap, IMX486_REG_CHIP_ID,
		       &val, NULL);
	if (ret) {
		dev_err(&client->dev, "failed to read chip id %x\n",
			IMX486_CHIP_ID);
		return ret;
	}

	if (val != IMX486_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%llx\n",
			IMX486_CHIP_ID, val);
		return -EIO;
	}

	return 0;
}

static const struct v4l2_subdev_video_ops imx486_video_ops = {
	.s_stream = imx486_set_stream,
};

static const struct v4l2_subdev_pad_ops imx486_pad_ops = {
	.enum_mbus_code = imx486_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = imx486_set_pad_format,
	.enum_frame_size = imx486_enum_frame_size,
	.get_selection = imx486_get_selection,
};

static const struct v4l2_subdev_ops imx486_subdev_ops = {
	.video = &imx486_video_ops,
	.pad = &imx486_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx486_internal_ops = {
	.init_state = imx486_init_state,
};

/* Initialize control handlers */
static int imx486_init_controls(struct imx486 *imx486)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx486->sd);
	const struct imx486_mode *mode = &supported_modes[0];
	const struct imx486_link_freq_config *link_freq_cfg;
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	s64 vblank_def;
	s64 vblank_min;
	s64 pixel_rate;
	s64 hblank;
	int ret;

	ctrl_hdlr = &imx486->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 12);
	if (ret)
		return ret;

	imx486->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx486_ctrl_ops,
						   V4L2_CID_LINK_FREQ,
						   ARRAY_SIZE(link_freq_menu_items) - 1,
						   0, link_freq_menu_items);

	if (imx486->link_freq)
		imx486->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx486->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx486_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 1);
	if (imx486->hflip)
		imx486->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	imx486->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &imx486_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 1);
	if (imx486->vflip)
		imx486->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;

	link_freq_cfg = &link_freq_configs[mode->link_freq_index];
	pixel_rate = link_freq_cfg->pixel_rate;
	dev_dbg(&client->dev, "pixel_rate: %lld\n", pixel_rate);

	/* By default, PIXEL_RATE is read only */
	imx486->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &imx486_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       pixel_rate, pixel_rate,
					       1, pixel_rate);

	vblank_def = mode->vts_def - mode->height;
	vblank_min = mode->vts_min - mode->height;
	imx486->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx486_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_min,
					   IMX486_VTS_MAX - mode->height, 1,
					   vblank_def);

	hblank = link_freq_cfg->pixels_per_line - mode->width;
	imx486->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx486_ctrl_ops,
					   V4L2_CID_HBLANK, hblank, hblank,
					   1, hblank);

	if (imx486->hblank)
		imx486->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx486->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx486_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX486_EXPOSURE_MIN,
					     IMX486_EXPOSURE_MAX,
					     IMX486_EXPOSURE_STEP,
					     IMX486_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx486_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  IMX486_ANA_GAIN_MIN, IMX486_ANA_GAIN_MAX,
			  IMX486_ANA_GAIN_STEP, IMX486_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx486_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  IMX486_DGTL_GAIN_MIN, IMX486_DGTL_GAIN_MAX,
			  IMX486_DGTL_GAIN_STEP, IMX486_DGTL_GAIN_DEFAULT);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &imx486_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx486_test_pattern_menu) - 1,
				     0, 0, imx486_test_pattern_menu);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n",
			__func__, ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx486_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	imx486->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static void imx486_free_controls(struct imx486 *imx486)
{
	v4l2_ctrl_handler_free(imx486->sd.ctrl_handler);
}

static int imx486_get_regulators(struct imx486 *imx486,
				 struct i2c_client *client)
{
	unsigned int i;

	for (i = 0; i < IMX486_NUM_SUPPLIES; i++)
		imx486->supplies[i].supply = imx486_supply_name[i];

	return devm_regulator_bulk_get(&client->dev,
				    IMX486_NUM_SUPPLIES, imx486->supplies);
}

/*
 * Check that the endpoint describes the only supported configuration: four
 * data lanes and both the 492 MHz and 252 MHz link frequencies, as each mode
 * is tied to its own link frequency.
 */
static int imx486_check_endpoint(struct device *dev)
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
	if (ret) {
		dev_err_probe(dev, ret, "link frequency not supported\n");
		goto out_free;
	}

	if (bitmap != GENMASK(ARRAY_SIZE(link_freq_menu_items) - 1, 0))
		ret = dev_err_probe(dev, -EINVAL,
				    "link-frequencies must list 492000000 and 252000000\n");

out_free:
	v4l2_fwnode_endpoint_free(&ep);

	return ret;
}

static int imx486_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx486 *imx486;
	int ret;

	imx486 = devm_kzalloc(dev, sizeof(*imx486), GFP_KERNEL);
	if (!imx486)
		return -ENOMEM;

	imx486->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(imx486->regmap))
		return dev_err_probe(dev, PTR_ERR(imx486->regmap),
				     "failed to initialize CCI\n");

	ret = imx486_get_regulators(imx486, client);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	imx486->clk = devm_v4l2_sensor_clk_get(dev, NULL);
	if (IS_ERR(imx486->clk))
		return dev_err_probe(dev, PTR_ERR(imx486->clk),
				     "failed to get clock\n");

	if (clk_get_rate(imx486->clk) != IMX486_INCK_FREQ)
		return dev_err_probe(dev, -EINVAL,
				     "input clock frequency must be %u Hz\n",
				     IMX486_INCK_FREQ);

	ret = imx486_check_endpoint(dev);
	if (ret)
		return ret;

	/* request optional reset pin */
	imx486->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(imx486->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(imx486->reset_gpio),
				     "failed to get reset gpio\n");

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&imx486->sd, client, &imx486_subdev_ops);

	/* Will be powered off via pm_runtime_idle */
	ret = imx486_power_on(dev);
	if (ret)
		return ret;

	/* Check module identity */
	ret = imx486_identify_module(imx486);
	if (ret)
		goto error_identify;

	ret = imx486_init_controls(imx486);
	if (ret)
		goto error_identify;

	/* Initialize subdev */
	imx486->sd.internal_ops = &imx486_internal_ops;
	imx486->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx486->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	imx486->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx486->sd.entity, 1, &imx486->pad);
	if (ret)
		goto error_handler_free;

	imx486->sd.state_lock = imx486->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&imx486->sd);
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

	ret = v4l2_async_register_subdev_sensor(&imx486->sd);
	if (ret < 0) {
		dev_err_probe(dev, ret,
			      "failed to register sensor sub-device\n");
		goto error_subdev_cleanup;
	}

	pm_runtime_idle(dev);

	return 0;

error_subdev_cleanup:
	v4l2_subdev_cleanup(&imx486->sd);
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);

error_media_entity:
	media_entity_cleanup(&imx486->sd.entity);

error_handler_free:
	imx486_free_controls(imx486);

error_identify:
	imx486_power_off(dev);

	return ret;
}

static void imx486_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx486 *imx486 = to_imx486(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	imx486_free_controls(imx486);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev)) {
		imx486_power_off(&client->dev);
		pm_runtime_set_suspended(&client->dev);
	}
}

static const struct dev_pm_ops imx486_pm_ops = {
	SET_RUNTIME_PM_OPS(imx486_power_off, imx486_power_on, NULL)
};

static const struct of_device_id imx486_dt_ids[] = {
	{ .compatible = "sony,imx486"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx486_dt_ids);

static struct i2c_driver imx486_i2c_driver = {
	.driver = {
		.name = "imx486",
		.pm = &imx486_pm_ops,
		.of_match_table	= imx486_dt_ids,
	},
	.probe = imx486_probe,
	.remove = imx486_remove,
};

module_i2c_driver(imx486_i2c_driver);

MODULE_AUTHOR("Gianluca Boiano <morf3089@gmail.com>");
MODULE_AUTHOR("Yeh, Andy <andy.yeh@intel.com>");
MODULE_AUTHOR("Chiang, Alan");
MODULE_AUTHOR("Chen, Jason");
MODULE_DESCRIPTION("Sony IMX486 sensor driver");
MODULE_LICENSE("GPL");
