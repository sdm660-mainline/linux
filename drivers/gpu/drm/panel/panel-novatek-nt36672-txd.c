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
#include <drm/drm_probe_helper.h>

struct nt36672_txd {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[3];
	struct gpio_desc *reset_gpio;
};

static inline struct nt36672_txd *to_nt36672_txd(struct drm_panel *panel)
{
	return container_of(panel, struct nt36672_txd, panel);
}

static void nt36672_txd_reset(struct nt36672_txd *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
}

static int nt36672_txd_on(struct nt36672_txd *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x06, 0x9e);
	mipi_dsi_dcs_write_seq(dsi, 0x07, 0x94);
	mipi_dsi_dcs_write_seq(dsi, 0x0e, 0x35);
	mipi_dsi_dcs_write_seq(dsi, 0x0f, 0x24);
	mipi_dsi_dcs_write_seq(dsi, 0x6d, 0x66);
	mipi_dsi_dcs_write_seq(dsi, 0x69, 0x99);
	mipi_dsi_dcs_write_seq(dsi, 0x95, 0xf5);
	mipi_dsi_dcs_write_seq(dsi, 0x96, 0xf5);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x23);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x12, 0x6c);
	mipi_dsi_dcs_write_seq(dsi, 0x15, 0xe6);
	mipi_dsi_dcs_write_seq(dsi, 0x16, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x24);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x01, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x02, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x03, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x04, 0x0b);
	mipi_dsi_dcs_write_seq(dsi, 0x05, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, 0x06, 0xa9);
	mipi_dsi_dcs_write_seq(dsi, 0x07, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0x08, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x09, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x0a, 0x0f);
	mipi_dsi_dcs_write_seq(dsi, 0x0b, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x0c, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x0d, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x0e, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x0f, 0x17);
	mipi_dsi_dcs_write_seq(dsi, 0x10, 0x15);
	mipi_dsi_dcs_write_seq(dsi, 0x11, 0x13);
	mipi_dsi_dcs_write_seq(dsi, 0x12, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x13, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x14, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x15, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x16, 0x0b);
	mipi_dsi_dcs_write_seq(dsi, 0x17, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, 0x18, 0xa9);
	mipi_dsi_dcs_write_seq(dsi, 0x19, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0x1a, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0x1b, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x1c, 0x0f);
	mipi_dsi_dcs_write_seq(dsi, 0x1d, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x1e, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x1f, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x20, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x21, 0x17);
	mipi_dsi_dcs_write_seq(dsi, 0x22, 0x15);
	mipi_dsi_dcs_write_seq(dsi, 0x23, 0x13);
	mipi_dsi_dcs_write_seq(dsi, 0x2f, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x30, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0x31, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x32, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0x33, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0x34, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0x35, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x37, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0x38, 0x72);
	mipi_dsi_dcs_write_seq(dsi, 0x39, 0x72);
	mipi_dsi_dcs_write_seq(dsi, 0x3b, 0x40);
	mipi_dsi_dcs_write_seq(dsi, 0x3f, 0x72);
	mipi_dsi_dcs_write_seq(dsi, 0x60, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0x61, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x68, 0x83);
	mipi_dsi_dcs_write_seq(dsi, 0x78, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x79, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x7a, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0x7b, 0x9c);
	mipi_dsi_dcs_write_seq(dsi, 0x7d, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0x7e, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0x80, 0x45);
	mipi_dsi_dcs_write_seq(dsi, 0x81, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0x8e, 0xf0);
	mipi_dsi_dcs_write_seq(dsi, 0x90, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x92, 0x76);
	mipi_dsi_dcs_write_seq(dsi, 0x93, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0x94, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0x99, 0x33);
	mipi_dsi_dcs_write_seq(dsi, 0x9b, 0xff);
	mipi_dsi_dcs_write_seq(dsi, 0xb3, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xb4, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0xdc, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xdd, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xde, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xdf, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xe0, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0xe9, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0xed, 0x40);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xb0,
			       0x00, 0x00, 0x00, 0x15, 0x00, 0x37, 0x00, 0x54,
			       0x00, 0x6d, 0x00, 0x84, 0x00, 0x98, 0x00, 0xac);
	mipi_dsi_dcs_write_seq(dsi, 0xb1,
			       0x00, 0xbd, 0x00, 0xf9, 0x01, 0x25, 0x01, 0x6b,
			       0x01, 0x9c, 0x01, 0xec, 0x02, 0x22, 0x02, 0x25);
	mipi_dsi_dcs_write_seq(dsi, 0xb2,
			       0x02, 0x5e, 0x02, 0x9e, 0x02, 0xc9, 0x02, 0xfd,
			       0x03, 0x21, 0x03, 0x4d, 0x03, 0x5a, 0x03, 0x68);
	mipi_dsi_dcs_write_seq(dsi, 0xb3,
			       0x03, 0x78, 0x03, 0x8b, 0x03, 0xa1, 0x03, 0xbd,
			       0x03, 0xd6, 0x03, 0xda);
	mipi_dsi_dcs_write_seq(dsi, 0xb4,
			       0x00, 0x00, 0x00, 0x15, 0x00, 0x37, 0x00, 0x54,
			       0x00, 0x6d, 0x00, 0x84, 0x00, 0x98, 0x00, 0xac);
	mipi_dsi_dcs_write_seq(dsi, 0xb5,
			       0x00, 0xbd, 0x00, 0xf9, 0x01, 0x25, 0x01, 0x6b,
			       0x01, 0x9c, 0x01, 0xec, 0x02, 0x22, 0x02, 0x25);
	mipi_dsi_dcs_write_seq(dsi, 0xb6,
			       0x02, 0x5e, 0x02, 0x9e, 0x02, 0xc9, 0x02, 0xfd,
			       0x03, 0x21, 0x03, 0x4d, 0x03, 0x5a, 0x03, 0x68);
	mipi_dsi_dcs_write_seq(dsi, 0xb7,
			       0x03, 0x78, 0x03, 0x8b, 0x03, 0xa1, 0x03, 0xbd,
			       0x03, 0xd6, 0x03, 0xda);
	mipi_dsi_dcs_write_seq(dsi, 0xb8,
			       0x00, 0x00, 0x00, 0x15, 0x00, 0x37, 0x00, 0x54,
			       0x00, 0x6d, 0x00, 0x84, 0x00, 0x98, 0x00, 0xac);
	mipi_dsi_dcs_write_seq(dsi, 0xb9,
			       0x00, 0xbd, 0x00, 0xf9, 0x01, 0x25, 0x01, 0x6b,
			       0x01, 0x9c, 0x01, 0xec, 0x02, 0x22, 0x02, 0x25);
	mipi_dsi_dcs_write_seq(dsi, 0xba,
			       0x02, 0x5e, 0x02, 0x9e, 0x02, 0xc9, 0x02, 0xfd,
			       0x03, 0x21, 0x03, 0x4d, 0x03, 0x5a, 0x03, 0x68);
	mipi_dsi_dcs_write_seq(dsi, 0xbb,
			       0x03, 0x78, 0x03, 0x8b, 0x03, 0xa1, 0x03, 0xbd,
			       0x03, 0xd6, 0x03, 0xda);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x21);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xb0,
			       0x00, 0x00, 0x00, 0x15, 0x00, 0x37, 0x00, 0x54,
			       0x00, 0x6d, 0x00, 0x84, 0x00, 0x98, 0x00, 0xac);
	mipi_dsi_dcs_write_seq(dsi, 0xb1,
			       0x00, 0xbd, 0x00, 0xf9, 0x01, 0x25, 0x01, 0x6b,
			       0x01, 0x9c, 0x01, 0xec, 0x02, 0x22, 0x02, 0x25);
	mipi_dsi_dcs_write_seq(dsi, 0xb2,
			       0x02, 0x5e, 0x02, 0x9e, 0x02, 0xc9, 0x02, 0xfd,
			       0x03, 0x21, 0x03, 0x4d, 0x03, 0x5a, 0x03, 0x68);
	mipi_dsi_dcs_write_seq(dsi, 0xb3,
			       0x03, 0x78, 0x03, 0x8b, 0x03, 0xa1, 0x03, 0xbd,
			       0x03, 0xd6, 0x03, 0xda);
	mipi_dsi_dcs_write_seq(dsi, 0xb4,
			       0x00, 0x00, 0x00, 0x15, 0x00, 0x37, 0x00, 0x54,
			       0x00, 0x6d, 0x00, 0x84, 0x00, 0x98, 0x00, 0xac);
	mipi_dsi_dcs_write_seq(dsi, 0xb5,
			       0x00, 0xbd, 0x00, 0xf9, 0x01, 0x25, 0x01, 0x6b,
			       0x01, 0x9c, 0x01, 0xec, 0x02, 0x22, 0x02, 0x25);
	mipi_dsi_dcs_write_seq(dsi, 0xb6,
			       0x02, 0x5e, 0x02, 0x9e, 0x02, 0xc9, 0x02, 0xfd,
			       0x03, 0x21, 0x03, 0x4d, 0x03, 0x5a, 0x03, 0x68);
	mipi_dsi_dcs_write_seq(dsi, 0xb7,
			       0x03, 0x78, 0x03, 0x8b, 0x03, 0xa1, 0x03, 0xbd,
			       0x03, 0xd6, 0x03, 0xda);
	mipi_dsi_dcs_write_seq(dsi, 0xb8,
			       0x00, 0x00, 0x00, 0x15, 0x00, 0x37, 0x00, 0x54,
			       0x00, 0x6d, 0x00, 0x84, 0x00, 0x98, 0x00, 0xac);
	mipi_dsi_dcs_write_seq(dsi, 0xb9,
			       0x00, 0xbd, 0x00, 0xf9, 0x01, 0x25, 0x01, 0x6b,
			       0x01, 0x9c, 0x01, 0xec, 0x02, 0x22, 0x02, 0x25);
	mipi_dsi_dcs_write_seq(dsi, 0xba,
			       0x02, 0x5e, 0x02, 0x9e, 0x02, 0xc9, 0x02, 0xfd,
			       0x03, 0x21, 0x03, 0x4d, 0x03, 0x5a, 0x03, 0x68);
	mipi_dsi_dcs_write_seq(dsi, 0xbb,
			       0x03, 0x78, 0x03, 0x8b, 0x03, 0xa1, 0x03, 0xbd,
			       0x03, 0xd6, 0x03, 0xda);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x25);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x05, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, 0x0a, 0x81);
	mipi_dsi_dcs_write_seq(dsi, 0x0b, 0xd7);
	mipi_dsi_dcs_write_seq(dsi, 0x0c, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x17, 0x82);
	mipi_dsi_dcs_write_seq(dsi, 0x21, 0x1c);
	mipi_dsi_dcs_write_seq(dsi, 0x22, 0x1c);
	mipi_dsi_dcs_write_seq(dsi, 0x24, 0x76);
	mipi_dsi_dcs_write_seq(dsi, 0x25, 0x76);
	mipi_dsi_dcs_write_seq(dsi, 0x5c, 0x25);
	mipi_dsi_dcs_write_seq(dsi, 0x5d, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0x5e, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0x5f, 0x22);
	mipi_dsi_dcs_write_seq(dsi, 0x65, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x69, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x6b, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x71, 0x2d);
	mipi_dsi_dcs_write_seq(dsi, 0x80, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x8d, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0xd7, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xd8, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xd9, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xda, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xdb, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xdc, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x26);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x06, 0xc8);
	mipi_dsi_dcs_write_seq(dsi, 0x12, 0x5a);
	mipi_dsi_dcs_write_seq(dsi, 0x19, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0x1a, 0x97);
	mipi_dsi_dcs_write_seq(dsi, 0x1d, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0x1e, 0x1e);
	mipi_dsi_dcs_write_seq(dsi, 0x99, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x27);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x13, 0x0e);
	mipi_dsi_dcs_write_seq(dsi, 0x16, 0xb0);
	mipi_dsi_dcs_write_seq(dsi, 0x17, 0xd0);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(70);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	usleep_range(10000, 11000);

	return 0;
}

static int nt36672_txd_off(struct nt36672_txd *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	msleep(50);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	return 0;
}

static int nt36672_txd_prepare(struct drm_panel *panel)
{
	struct nt36672_txd *ctx = to_nt36672_txd(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	nt36672_txd_reset(ctx);

	ret = nt36672_txd_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int nt36672_txd_unprepare(struct drm_panel *panel)
{
	struct nt36672_txd *ctx = to_nt36672_txd(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = nt36672_txd_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode nt36672_txd_mode = {
	.clock = (1080 + 122 + 8 + 76) * (2160 + 20 + 4 + 28) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 122,
	.hsync_end = 1080 + 122 + 8,
	.htotal = 1080 + 122 + 8 + 76,
	.vdisplay = 2160,
	.vsync_start = 2160 + 20,
	.vsync_end = 2160 + 20 + 4,
	.vtotal = 2160 + 20 + 4 + 28,
	.width_mm = 68,
	.height_mm = 136,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int nt36672_txd_get_modes(struct drm_panel *panel,
				 struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &nt36672_txd_mode);
}

static const struct drm_panel_funcs nt36672_txd_panel_funcs = {
	.prepare = nt36672_txd_prepare,
	.unprepare = nt36672_txd_unprepare,
	.get_modes = nt36672_txd_get_modes,
};

static int nt36672_txd_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct nt36672_txd *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "vddio";
	ctx->supplies[1].supply = "lab";
	ctx->supplies[2].supply = "ibb";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &nt36672_txd_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void nt36672_txd_remove(struct mipi_dsi_device *dsi)
{
	struct nt36672_txd *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id nt36672_txd_of_match[] = {
	{ .compatible = "mdss,novatek-nt36672-txd" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nt36672_txd_of_match);

static struct mipi_dsi_driver nt36672_txd_driver = {
	.probe = nt36672_txd_probe,
	.remove = nt36672_txd_remove,
	.driver = {
		.name = "panel-nt36672-txd",
		.of_match_table = nt36672_txd_of_match,
	},
};
module_mipi_dsi_driver(nt36672_txd_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for nt36672 1080p video mode dsi txd panel");
MODULE_LICENSE("GPL");
