// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct td4320_boeplus {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	bool prepared;
};

static inline struct td4320_boeplus *to_td4320_boeplus(struct drm_panel *panel)
{
	return container_of(panel, struct td4320_boeplus, panel);
}

static void td4320_boeplus_reset(struct td4320_boeplus *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(30);
}

static int td4320_boeplus_on(struct td4320_boeplus *ctx)
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

static int td4320_boeplus_off(struct td4320_boeplus *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq(dsi, 0x28, 0x00);
	msleep(20);
	mipi_dsi_dcs_write_seq(dsi, 0x10, 0x00);
	msleep(120);

	return 0;
}

static int td4320_boeplus_prepare(struct drm_panel *panel)
{
	struct td4320_boeplus *ctx = to_td4320_boeplus(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	td4320_boeplus_reset(ctx);

	ret = td4320_boeplus_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int td4320_boeplus_unprepare(struct drm_panel *panel)
{
	struct td4320_boeplus *ctx = to_td4320_boeplus(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = td4320_boeplus_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	ctx->prepared = false;
	return 0;
}

static const struct drm_display_mode td4320_boeplus_mode = {
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
};

static int td4320_boeplus_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &td4320_boeplus_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs td4320_boeplus_panel_funcs = {
	.prepare = td4320_boeplus_prepare,
	.unprepare = td4320_boeplus_unprepare,
	.get_modes = td4320_boeplus_get_modes,
};

static int td4320_boeplus_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct td4320_boeplus *ctx;
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

	drm_panel_init(&ctx->panel, dev, &td4320_boeplus_panel_funcs,
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

static void td4320_boeplus_remove(struct mipi_dsi_device *dsi)
{
	struct td4320_boeplus *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id td4320_boeplus_of_match[] = {
	{ .compatible = "xiaomi,lavender-td4320" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, td4320_boeplus_of_match);

static struct mipi_dsi_driver td4320_boeplus_driver = {
	.probe = td4320_boeplus_probe,
	.remove = td4320_boeplus_remove,
	.driver = {
		.name = "panel-td4320-boeplus",
		.of_match_table = td4320_boeplus_of_match,
	},
};
module_mipi_dsi_driver(td4320_boeplus_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for boe td4320 fhdplus video mode dsi panel");
MODULE_LICENSE("GPL");
