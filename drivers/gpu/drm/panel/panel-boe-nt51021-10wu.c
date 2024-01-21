// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct nt51021_boe_10wu {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator *supply;
	struct gpio_desc *reset_gpio;
	bool prepared;
};

static inline
struct nt51021_boe_10wu *to_nt51021_boe_10wu(struct drm_panel *panel)
{
	return container_of(panel, struct nt51021_boe_10wu, panel);
}

static void nt51021_boe_10wu_reset(struct nt51021_boe_10wu *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int nt51021_boe_10wu_on(struct nt51021_boe_10wu *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	mipi_dsi_generic_write_seq(dsi, 0x8f, 0xa5);
	usleep_range(1000, 2000);
	mipi_dsi_generic_write_seq(dsi, 0x01, 0x00);
	msleep(20);
	mipi_dsi_generic_write_seq(dsi, 0x8f, 0xa5);
	usleep_range(1000, 2000);
	mipi_dsi_generic_write_seq(dsi, 0x83, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0x84, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0x8c, 0x8e);
	mipi_dsi_generic_write_seq(dsi, 0xfa, 0x12);
	mipi_dsi_generic_write_seq(dsi, 0xfd, 0x1b);
	mipi_dsi_generic_write_seq(dsi, 0xcd, 0x6c);
	mipi_dsi_generic_write_seq(dsi, 0xc8, 0xfc);
	mipi_dsi_generic_write_seq(dsi, 0x97, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0x8b, 0x10);
	mipi_dsi_generic_write_seq(dsi, 0xa9, 0x20);
	mipi_dsi_generic_write_seq(dsi, 0x83, 0xaa);
	mipi_dsi_generic_write_seq(dsi, 0x84, 0x11);
	mipi_dsi_generic_write_seq(dsi, 0xa9, 0x4b);
	mipi_dsi_generic_write_seq(dsi, 0x85, 0x04);
	mipi_dsi_generic_write_seq(dsi, 0x86, 0x08);
	mipi_dsi_generic_write_seq(dsi, 0x98, 0xc1);
	mipi_dsi_generic_write_seq(dsi, 0x9c, 0x10);
	mipi_dsi_generic_write_seq(dsi, 0x83, 0xaa);
	mipi_dsi_generic_write_seq(dsi, 0x84, 0x11);
	mipi_dsi_generic_write_seq(dsi, 0xc0, 0x0e);
	mipi_dsi_generic_write_seq(dsi, 0xc1, 0x11);
	mipi_dsi_generic_write_seq(dsi, 0xc2, 0x1f);
	mipi_dsi_generic_write_seq(dsi, 0xc3, 0x30);
	mipi_dsi_generic_write_seq(dsi, 0xc4, 0x3f);
	mipi_dsi_generic_write_seq(dsi, 0xc5, 0x4b);
	mipi_dsi_generic_write_seq(dsi, 0xc6, 0x55);
	mipi_dsi_generic_write_seq(dsi, 0xc7, 0x5d);
	mipi_dsi_generic_write_seq(dsi, 0xc8, 0x68);
	mipi_dsi_generic_write_seq(dsi, 0xc9, 0xe3);
	mipi_dsi_generic_write_seq(dsi, 0xca, 0xea);
	mipi_dsi_generic_write_seq(dsi, 0xcb, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xcc, 0x0a);
	mipi_dsi_generic_write_seq(dsi, 0xcd, 0x0a);
	mipi_dsi_generic_write_seq(dsi, 0xce, 0x07);
	mipi_dsi_generic_write_seq(dsi, 0xcf, 0x08);
	mipi_dsi_generic_write_seq(dsi, 0xd0, 0x09);
	mipi_dsi_generic_write_seq(dsi, 0xd1, 0x1d);
	mipi_dsi_generic_write_seq(dsi, 0xd2, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xd3, 0x52);
	mipi_dsi_generic_write_seq(dsi, 0xd4, 0x5b);
	mipi_dsi_generic_write_seq(dsi, 0xd5, 0xb6);
	mipi_dsi_generic_write_seq(dsi, 0xd6, 0xbd);
	mipi_dsi_generic_write_seq(dsi, 0xd7, 0xc5);
	mipi_dsi_generic_write_seq(dsi, 0xd8, 0xcd);
	mipi_dsi_generic_write_seq(dsi, 0xd9, 0xd6);
	mipi_dsi_generic_write_seq(dsi, 0xda, 0xe0);
	mipi_dsi_generic_write_seq(dsi, 0xdb, 0xeb);
	mipi_dsi_generic_write_seq(dsi, 0xdc, 0xf8);
	mipi_dsi_generic_write_seq(dsi, 0xdd, 0xff);
	mipi_dsi_generic_write_seq(dsi, 0xde, 0xfc);
	mipi_dsi_generic_write_seq(dsi, 0xdf, 0x0f);
	mipi_dsi_generic_write_seq(dsi, 0xe0, 0x0e);
	mipi_dsi_generic_write_seq(dsi, 0xe1, 0x11);
	mipi_dsi_generic_write_seq(dsi, 0xe2, 0x1f);
	mipi_dsi_generic_write_seq(dsi, 0xe3, 0x30);
	mipi_dsi_generic_write_seq(dsi, 0xe4, 0x3f);
	mipi_dsi_generic_write_seq(dsi, 0xe5, 0x4b);
	mipi_dsi_generic_write_seq(dsi, 0xe6, 0x55);
	mipi_dsi_generic_write_seq(dsi, 0xe7, 0x5d);
	mipi_dsi_generic_write_seq(dsi, 0xe8, 0x68);
	mipi_dsi_generic_write_seq(dsi, 0xe9, 0xe3);
	mipi_dsi_generic_write_seq(dsi, 0xea, 0xea);
	mipi_dsi_generic_write_seq(dsi, 0xeb, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xec, 0x0a);
	mipi_dsi_generic_write_seq(dsi, 0xed, 0x0a);
	mipi_dsi_generic_write_seq(dsi, 0xee, 0x07);
	mipi_dsi_generic_write_seq(dsi, 0xef, 0x08);
	mipi_dsi_generic_write_seq(dsi, 0xf0, 0x09);
	mipi_dsi_generic_write_seq(dsi, 0xf1, 0x1d);
	mipi_dsi_generic_write_seq(dsi, 0xf2, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xf3, 0x52);
	mipi_dsi_generic_write_seq(dsi, 0xf4, 0x5b);
	mipi_dsi_generic_write_seq(dsi, 0xf5, 0xb6);
	mipi_dsi_generic_write_seq(dsi, 0xf6, 0xbd);
	mipi_dsi_generic_write_seq(dsi, 0xf7, 0xc5);
	mipi_dsi_generic_write_seq(dsi, 0xf8, 0xcd);
	mipi_dsi_generic_write_seq(dsi, 0xf9, 0xd6);
	mipi_dsi_generic_write_seq(dsi, 0xfa, 0xe0);
	mipi_dsi_generic_write_seq(dsi, 0xfb, 0xeb);
	mipi_dsi_generic_write_seq(dsi, 0xfc, 0xf8);
	mipi_dsi_generic_write_seq(dsi, 0xfd, 0xff);
	mipi_dsi_generic_write_seq(dsi, 0xfe, 0xfc);
	mipi_dsi_generic_write_seq(dsi, 0xff, 0x0f);
	mipi_dsi_generic_write_seq(dsi, 0x83, 0xbb);
	mipi_dsi_generic_write_seq(dsi, 0x84, 0x22);
	mipi_dsi_generic_write_seq(dsi, 0xa1, 0xff);
	mipi_dsi_generic_write_seq(dsi, 0xa2, 0xfe);
	mipi_dsi_generic_write_seq(dsi, 0xa3, 0xfa);
	mipi_dsi_generic_write_seq(dsi, 0xa4, 0xf7);
	mipi_dsi_generic_write_seq(dsi, 0xa5, 0xf3);
	mipi_dsi_generic_write_seq(dsi, 0xa6, 0xf1);
	mipi_dsi_generic_write_seq(dsi, 0xa7, 0xed);
	mipi_dsi_generic_write_seq(dsi, 0xa8, 0xeb);
	mipi_dsi_generic_write_seq(dsi, 0xa9, 0xe9);
	mipi_dsi_generic_write_seq(dsi, 0xaa, 0xe6);
	mipi_dsi_generic_write_seq(dsi, 0xaf, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x12);
	mipi_dsi_generic_write_seq(dsi, 0xb1, 0x23);
	mipi_dsi_generic_write_seq(dsi, 0xb2, 0x34);
	mipi_dsi_generic_write_seq(dsi, 0xb3, 0x77);
	mipi_dsi_generic_write_seq(dsi, 0xb4, 0x0d);
	mipi_dsi_generic_write_seq(dsi, 0xb5, 0x1a);
	mipi_dsi_generic_write_seq(dsi, 0xb6, 0x16);
	mipi_dsi_generic_write_seq(dsi, 0x9a, 0x10);
	mipi_dsi_generic_write_seq(dsi, 0x9b, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0x96, 0xe6);
	mipi_dsi_generic_write_seq(dsi, 0x99, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xc0, 0x0e);
	mipi_dsi_generic_write_seq(dsi, 0xc1, 0x11);
	mipi_dsi_generic_write_seq(dsi, 0xc2, 0x1f);
	mipi_dsi_generic_write_seq(dsi, 0xc3, 0x30);
	mipi_dsi_generic_write_seq(dsi, 0xc4, 0x3f);
	mipi_dsi_generic_write_seq(dsi, 0xc5, 0x4b);
	mipi_dsi_generic_write_seq(dsi, 0xc6, 0x55);
	mipi_dsi_generic_write_seq(dsi, 0xc7, 0x5d);
	mipi_dsi_generic_write_seq(dsi, 0xc8, 0x68);
	mipi_dsi_generic_write_seq(dsi, 0xc9, 0xe3);
	mipi_dsi_generic_write_seq(dsi, 0xca, 0xea);
	mipi_dsi_generic_write_seq(dsi, 0xcb, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xcc, 0x0a);
	mipi_dsi_generic_write_seq(dsi, 0xcd, 0x0a);
	mipi_dsi_generic_write_seq(dsi, 0xce, 0x07);
	mipi_dsi_generic_write_seq(dsi, 0xcf, 0x08);
	mipi_dsi_generic_write_seq(dsi, 0xd0, 0x09);
	mipi_dsi_generic_write_seq(dsi, 0xd1, 0x1d);
	mipi_dsi_generic_write_seq(dsi, 0xd2, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xd3, 0x52);
	mipi_dsi_generic_write_seq(dsi, 0xd4, 0x5b);
	mipi_dsi_generic_write_seq(dsi, 0xd5, 0xb6);
	mipi_dsi_generic_write_seq(dsi, 0xd6, 0xbd);
	mipi_dsi_generic_write_seq(dsi, 0xd7, 0xc5);
	mipi_dsi_generic_write_seq(dsi, 0xd8, 0xcd);
	mipi_dsi_generic_write_seq(dsi, 0xd9, 0xd6);
	mipi_dsi_generic_write_seq(dsi, 0xda, 0xe0);
	mipi_dsi_generic_write_seq(dsi, 0xdb, 0xeb);
	mipi_dsi_generic_write_seq(dsi, 0xdc, 0xf8);
	mipi_dsi_generic_write_seq(dsi, 0xdd, 0xff);
	mipi_dsi_generic_write_seq(dsi, 0xde, 0xfc);
	mipi_dsi_generic_write_seq(dsi, 0xdf, 0x0f);
	mipi_dsi_generic_write_seq(dsi, 0xe0, 0x0e);
	mipi_dsi_generic_write_seq(dsi, 0xe1, 0x11);
	mipi_dsi_generic_write_seq(dsi, 0xe2, 0x1f);
	mipi_dsi_generic_write_seq(dsi, 0xe3, 0x30);
	mipi_dsi_generic_write_seq(dsi, 0xe4, 0x3f);
	mipi_dsi_generic_write_seq(dsi, 0xe5, 0x4b);
	mipi_dsi_generic_write_seq(dsi, 0xe6, 0x55);
	mipi_dsi_generic_write_seq(dsi, 0xe7, 0x5d);
	mipi_dsi_generic_write_seq(dsi, 0xe8, 0x68);
	mipi_dsi_generic_write_seq(dsi, 0xe9, 0xe3);
	mipi_dsi_generic_write_seq(dsi, 0xea, 0xea);
	mipi_dsi_generic_write_seq(dsi, 0xeb, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xec, 0x0a);
	mipi_dsi_generic_write_seq(dsi, 0xed, 0x0a);
	mipi_dsi_generic_write_seq(dsi, 0xee, 0x07);
	mipi_dsi_generic_write_seq(dsi, 0xef, 0x08);
	mipi_dsi_generic_write_seq(dsi, 0xf0, 0x09);
	mipi_dsi_generic_write_seq(dsi, 0xf1, 0x1d);
	mipi_dsi_generic_write_seq(dsi, 0xf2, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xf3, 0x52);
	mipi_dsi_generic_write_seq(dsi, 0xf4, 0x5b);
	mipi_dsi_generic_write_seq(dsi, 0xf5, 0xb6);
	mipi_dsi_generic_write_seq(dsi, 0xf6, 0xbd);
	mipi_dsi_generic_write_seq(dsi, 0xf7, 0xc5);
	mipi_dsi_generic_write_seq(dsi, 0xf8, 0xcd);
	mipi_dsi_generic_write_seq(dsi, 0xf9, 0xd6);
	mipi_dsi_generic_write_seq(dsi, 0xfa, 0xe0);
	mipi_dsi_generic_write_seq(dsi, 0xfb, 0xeb);
	mipi_dsi_generic_write_seq(dsi, 0xfc, 0xf8);
	mipi_dsi_generic_write_seq(dsi, 0xfd, 0xff);
	mipi_dsi_generic_write_seq(dsi, 0xfe, 0xfc);
	mipi_dsi_generic_write_seq(dsi, 0xff, 0x0f);
	mipi_dsi_generic_write_seq(dsi, 0x83, 0xcc);
	mipi_dsi_generic_write_seq(dsi, 0x84, 0x33);
	mipi_dsi_generic_write_seq(dsi, 0xc0, 0x0e);
	mipi_dsi_generic_write_seq(dsi, 0xc1, 0x11);
	mipi_dsi_generic_write_seq(dsi, 0xc2, 0x1f);
	mipi_dsi_generic_write_seq(dsi, 0xc3, 0x30);
	mipi_dsi_generic_write_seq(dsi, 0xc4, 0x3f);
	mipi_dsi_generic_write_seq(dsi, 0xc5, 0x4b);
	mipi_dsi_generic_write_seq(dsi, 0xc6, 0x55);
	mipi_dsi_generic_write_seq(dsi, 0xc7, 0x5d);
	mipi_dsi_generic_write_seq(dsi, 0xc8, 0x68);
	mipi_dsi_generic_write_seq(dsi, 0xc9, 0xe3);
	mipi_dsi_generic_write_seq(dsi, 0xca, 0xea);
	mipi_dsi_generic_write_seq(dsi, 0xcb, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xcc, 0x0a);
	mipi_dsi_generic_write_seq(dsi, 0xcd, 0x0a);
	mipi_dsi_generic_write_seq(dsi, 0xce, 0x07);
	mipi_dsi_generic_write_seq(dsi, 0xcf, 0x08);
	mipi_dsi_generic_write_seq(dsi, 0xd0, 0x09);
	mipi_dsi_generic_write_seq(dsi, 0xd1, 0x1d);
	mipi_dsi_generic_write_seq(dsi, 0xd2, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xd3, 0x52);
	mipi_dsi_generic_write_seq(dsi, 0xd4, 0x5b);
	mipi_dsi_generic_write_seq(dsi, 0xd5, 0xb6);
	mipi_dsi_generic_write_seq(dsi, 0xd6, 0xbd);
	mipi_dsi_generic_write_seq(dsi, 0xd7, 0xc5);
	mipi_dsi_generic_write_seq(dsi, 0xd8, 0xcd);
	mipi_dsi_generic_write_seq(dsi, 0xd9, 0xd6);
	mipi_dsi_generic_write_seq(dsi, 0xda, 0xe0);
	mipi_dsi_generic_write_seq(dsi, 0xdb, 0xeb);
	mipi_dsi_generic_write_seq(dsi, 0xdc, 0xf8);
	mipi_dsi_generic_write_seq(dsi, 0xdd, 0xff);
	mipi_dsi_generic_write_seq(dsi, 0xde, 0xfc);
	mipi_dsi_generic_write_seq(dsi, 0xdf, 0x0f);
	mipi_dsi_generic_write_seq(dsi, 0xe0, 0x0e);
	mipi_dsi_generic_write_seq(dsi, 0xe1, 0x11);
	mipi_dsi_generic_write_seq(dsi, 0xe2, 0x1f);
	mipi_dsi_generic_write_seq(dsi, 0xe3, 0x30);
	mipi_dsi_generic_write_seq(dsi, 0xe4, 0x3f);
	mipi_dsi_generic_write_seq(dsi, 0xe5, 0x4b);
	mipi_dsi_generic_write_seq(dsi, 0xe6, 0x55);
	mipi_dsi_generic_write_seq(dsi, 0xe7, 0x5d);
	mipi_dsi_generic_write_seq(dsi, 0xe8, 0x68);
	mipi_dsi_generic_write_seq(dsi, 0xe9, 0xe3);
	mipi_dsi_generic_write_seq(dsi, 0xea, 0xea);
	mipi_dsi_generic_write_seq(dsi, 0xeb, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xec, 0x0a);
	mipi_dsi_generic_write_seq(dsi, 0xed, 0x0a);
	mipi_dsi_generic_write_seq(dsi, 0xee, 0x07);
	mipi_dsi_generic_write_seq(dsi, 0xef, 0x08);
	mipi_dsi_generic_write_seq(dsi, 0xf0, 0x09);
	mipi_dsi_generic_write_seq(dsi, 0xf1, 0x1d);
	mipi_dsi_generic_write_seq(dsi, 0xf2, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xf3, 0x52);
	mipi_dsi_generic_write_seq(dsi, 0xf4, 0x5b);
	mipi_dsi_generic_write_seq(dsi, 0xf5, 0xb6);
	mipi_dsi_generic_write_seq(dsi, 0xf6, 0xbd);
	mipi_dsi_generic_write_seq(dsi, 0xf7, 0xc5);
	mipi_dsi_generic_write_seq(dsi, 0xf8, 0xcd);
	mipi_dsi_generic_write_seq(dsi, 0xf9, 0xd6);
	mipi_dsi_generic_write_seq(dsi, 0xfa, 0xe0);
	mipi_dsi_generic_write_seq(dsi, 0xfb, 0xeb);
	mipi_dsi_generic_write_seq(dsi, 0xfc, 0xf8);
	mipi_dsi_generic_write_seq(dsi, 0xfd, 0xff);
	mipi_dsi_generic_write_seq(dsi, 0xfe, 0xfc);
	mipi_dsi_generic_write_seq(dsi, 0xff, 0x0f);
	mipi_dsi_generic_write_seq(dsi, 0x83, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0x84, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0x9f, 0xff);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	usleep_range(5000, 6000);

	mipi_dsi_generic_write_seq(dsi, 0x8f, 0x00);
	usleep_range(1000, 2000);

	return 0;
}

static int nt51021_boe_10wu_off(struct nt51021_boe_10wu *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	mipi_dsi_generic_write_seq(dsi, 0x8f, 0xa5);
	msleep(20);

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	msleep(130);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(70);

	mipi_dsi_generic_write_seq(dsi, 0x8f, 0x00);
	usleep_range(4000, 5000);

	return 0;
}

static int nt51021_boe_10wu_prepare(struct drm_panel *panel)
{
	struct nt51021_boe_10wu *ctx = to_nt51021_boe_10wu(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_enable(ctx->supply);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulator: %d\n", ret);
		return ret;
	}

	nt51021_boe_10wu_reset(ctx);
	
	ctx->prepared = true;
	return 0;
}

static int nt51021_boe_10wu_enable(struct drm_panel *panel)
{
	struct nt51021_boe_10wu *ctx = to_nt51021_boe_10wu(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = nt51021_boe_10wu_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_disable(ctx->supply);
		return ret;
	}
	
	return 0;
}

static int nt51021_boe_10wu_unprepare(struct drm_panel *panel)
{
	struct nt51021_boe_10wu *ctx = to_nt51021_boe_10wu(panel);
	
	if (!ctx->prepared)
		return 0;

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_disable(ctx->supply);

	ctx->prepared = false;
	return 0;
}

static int nt51021_boe_10wu_disable(struct drm_panel *panel)
{
	struct nt51021_boe_10wu *ctx = to_nt51021_boe_10wu(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = nt51021_boe_10wu_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	return 0;


}

static const struct drm_display_mode nt51021_boe_10wu_mode = {
	.clock = (1200 + 172 + 4 + 32) * (1920 + 25 + 1 + 14) * 60 / 1000,
	.hdisplay = 1200,
	.hsync_start = 1200 + 172,
	.hsync_end = 1200 + 172 + 4,
	.htotal = 1200 + 172 + 4 + 32,
	.vdisplay = 1920,
	.vsync_start = 1920 + 25,
	.vsync_end = 1920 + 25 + 1,
	.vtotal = 1920 + 25 + 1 + 14,
	.width_mm = 135,
	.height_mm = 216,
};

static int nt51021_boe_10wu_get_modes(struct drm_panel *panel,
				      struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &nt51021_boe_10wu_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs nt51021_boe_10wu_panel_funcs = {
	.prepare = nt51021_boe_10wu_prepare,
	.enable = nt51021_boe_10wu_enable,
	.unprepare = nt51021_boe_10wu_unprepare,
	.disable = nt51021_boe_10wu_disable,
	.get_modes = nt51021_boe_10wu_get_modes,
};

static int nt51021_boe_10wu_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct nt51021_boe_10wu *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supply = devm_regulator_get(dev, "avdd");
	if (IS_ERR(ctx->supply))
		return dev_err_probe(dev, PTR_ERR(ctx->supply),
				     "Failed to get avdd regulator\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_NO_EOT_PACKET | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &nt51021_boe_10wu_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void nt51021_boe_10wu_remove(struct mipi_dsi_device *dsi)
{
	struct nt51021_boe_10wu *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id nt51021_boe_10wu_of_match[] = {
	{ .compatible = "boe,nt51021-10wu" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nt51021_boe_10wu_of_match);

static struct mipi_dsi_driver nt51021_boe_10wu_driver = {
	.probe = nt51021_boe_10wu_probe,
	.remove = nt51021_boe_10wu_remove,
	.driver = {
		.name = "panel-nt51021-boe-10wu",
		.of_match_table = nt51021_boe_10wu_of_match,
	},
};
module_mipi_dsi_driver(nt51021_boe_10wu_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for NT51021_BOE_BOE10");
MODULE_LICENSE("GPL");
