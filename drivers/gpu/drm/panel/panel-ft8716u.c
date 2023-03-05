// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct ft8716u {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	bool prepared;
};

static inline struct ft8716u *to_ft8716u(struct drm_panel *panel)
{
	return container_of(panel, struct ft8716u, panel);
}

static void ft8716u_reset(struct ft8716u *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(2000, 3000);
	/* FIXME: panel should work without 2 above lines */
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int ft8716u_on(struct ft8716u *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x87, 0x16, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x87, 0x16);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x81);
	mipi_dsi_dcs_write_seq(dsi, 0xf3, 0xe8);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x82);
	mipi_dsi_dcs_write_seq(dsi, 0xf3, 0x85);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x83);
	mipi_dsi_dcs_write_seq(dsi, 0xf3, 0xc0);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x84);
	mipi_dsi_dcs_write_seq(dsi, 0xf3, 0xe8);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x85);
	mipi_dsi_dcs_write_seq(dsi, 0xf3, 0x85);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x86);
	mipi_dsi_dcs_write_seq(dsi, 0xf3, 0xc0);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x87);
	mipi_dsi_dcs_write_seq(dsi, 0xf3, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x88);
	mipi_dsi_dcs_write_seq(dsi, 0xf3, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x89);
	mipi_dsi_dcs_write_seq(dsi, 0xf3, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x8a);
	mipi_dsi_dcs_write_seq(dsi, 0xf3, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x8b);
	mipi_dsi_dcs_write_seq(dsi, 0xf3, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x8c);
	mipi_dsi_dcs_write_seq(dsi, 0xf3, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x98);
	mipi_dsi_dcs_write_seq(dsi, 0xc5, 0x33);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x00, 0x00);
	usleep_range(5000, 6000);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0x35);
	usleep_range(10000, 11000);

	return 0;
}

static int ft8716u_off(struct ft8716u *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	usleep_range(10000, 11000);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	return 0;
}

static int ft8716u_prepare(struct drm_panel *panel)
{
	struct ft8716u *ctx = to_ft8716u(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ft8716u_reset(ctx);

	ret = ft8716u_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int ft8716u_unprepare(struct drm_panel *panel)
{
	struct ft8716u *ctx = to_ft8716u(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = ft8716u_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	ctx->prepared = false;
	return 0;
}

static const struct drm_display_mode ft8716u_mode = {
	.clock = (1080 + 106 + 4 + 106) * (1920 + 16 + 2 + 16) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 106,
	.hsync_end = 1080 + 106 + 4,
	.htotal = 1080 + 106 + 4 + 106,
	.vdisplay = 1920,
	.vsync_start = 1920 + 16,
	.vsync_end = 1920 + 16 + 2,
	.vtotal = 1920 + 16 + 2 + 16,
	.width_mm = 68,
	.height_mm = 120,
};

static int ft8716u_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &ft8716u_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs ft8716u_panel_funcs = {
	.prepare = ft8716u_prepare,
	.unprepare = ft8716u_unprepare,
	.get_modes = ft8716u_get_modes,
};

static int ft8716u_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ft8716u *ctx;
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
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &ft8716u_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

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

static void ft8716u_remove(struct mipi_dsi_device *dsi)
{
	struct ft8716u *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ft8716u_of_match[] = {
	{ .compatible = "focaltech,ft8716u" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ft8716u_of_match);

static struct mipi_dsi_driver ft8716u_driver = {
	.probe = ft8716u_probe,
	.remove = ft8716u_remove,
	.driver = {
		.name = "panel-ft8716u",
		.of_match_table = ft8716u_of_match,
	},
};
module_mipi_dsi_driver(ft8716u_driver);

MODULE_AUTHOR("Alexander Richards <electrodeyt@gmail.com>");
MODULE_DESCRIPTION("DRM driver for the FT8716u panel");
MODULE_LICENSE("GPL");
