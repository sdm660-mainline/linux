// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Barnabas Czeman <trabarni@gmail.com>
 * Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
 *   Copyright (c) 2013, The Linux Foundation. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct boe_td4320 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
};

static inline struct boe_td4320 *to_boe_td4320(struct drm_panel *panel)
{
	return container_of(panel, struct boe_td4320, panel);
}

static void boe_td4320_reset(struct boe_td4320 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(30);
}

static int boe_td4320_on(struct boe_td4320 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x04);
	mipi_dsi_generic_write_seq(dsi, 0xd6, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xb8,
				   0x19, 0x55, 0x00, 0xbe, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xb9,
				   0x4d, 0x55, 0x05, 0xe6, 0x00, 0x02, 0x03);
	mipi_dsi_generic_write_seq(dsi, 0xba,
				   0x9b, 0x5b, 0x07, 0xe6, 0x00, 0x13, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xf9, 0x44, 0x3f, 0x00, 0x8d, 0xbf);
	mipi_dsi_generic_write_seq(dsi, 0xce,
				   0x5d, 0x00, 0x0f, 0x1f, 0x2f, 0x3f, 0x4f,
				   0x5f, 0x6f, 0x7f, 0x8f, 0x9f, 0xaf, 0xbf,
				   0xcf, 0xdf, 0xef, 0xff, 0x04, 0x00, 0x02,
				   0x02, 0x42, 0x01, 0x69, 0x5a, 0x40, 0x40,
				   0x00, 0x00, 0x04, 0xfa, 0x00);

	ret = mipi_dsi_dcs_set_display_brightness(dsi, 0x00b8);
	if (ret < 0) {
		dev_err(dev, "Failed to set display brightness: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x2c);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0x11, 0x00);
	msleep(96);
	mipi_dsi_dcs_write_seq(dsi, 0x29, 0x00);
	msleep(20);

	return 0;
}

static int boe_td4320_off(struct boe_td4320 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq(dsi, 0x28, 0x00);
	msleep(20);
	mipi_dsi_dcs_write_seq(dsi, 0x10, 0x00);
	msleep(120);

	return 0;
}

static int boe_td4320_prepare(struct drm_panel *panel)
{
	struct boe_td4320 *ctx = to_boe_td4320(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	boe_td4320_reset(ctx);

	ret = boe_td4320_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	return 0;
}

static int boe_td4320_unprepare(struct drm_panel *panel)
{
	struct boe_td4320 *ctx = to_boe_td4320(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = boe_td4320_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return 0;
}

static const struct drm_display_mode boe_td4320_mode = {
	.clock = (1080 + 86 + 2 + 100) * (2340 + 4 + 4 + 60) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 86,
	.hsync_end = 1080 + 86 + 2,
	.htotal = 1080 + 86 + 2 + 100,
	.vdisplay = 2340,
	.vsync_start = 2340 + 4,
	.vsync_end = 2340 + 4 + 4,
	.vtotal = 2340 + 4 + 4 + 60,
	.width_mm = 67,
	.height_mm = 145,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int boe_td4320_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &boe_td4320_mode);
}

static const struct drm_panel_funcs boe_td4320_panel_funcs = {
	.prepare = boe_td4320_prepare,
	.unprepare = boe_td4320_unprepare,
	.get_modes = boe_td4320_get_modes,
};

static int boe_td4320_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct boe_td4320 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &boe_td4320_panel_funcs,
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

static void boe_td4320_remove(struct mipi_dsi_device *dsi)
{
	struct boe_td4320 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id boe_td4320_of_match[] = {
	{ .compatible = "boe,td4320" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, boe_td4320_of_match);

static struct mipi_dsi_driver boe_td4320_driver = {
	.probe = boe_td4320_probe,
	.remove = boe_td4320_remove,
	.driver = {
		.name = "panel-boe-td4320",
		.of_match_table = boe_td4320_of_match,
	},
};
module_mipi_dsi_driver(boe_td4320_driver);

MODULE_AUTHOR("Barnabas Czeman <trabarni@gmail.com>");
MODULE_DESCRIPTION("DRM driver for boe td4320 fhdplus video mode dsi panel");
MODULE_LICENSE("GPL");
