// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct mdss_dsi_ft8716u {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
};

static inline
struct mdss_dsi_ft8716u *to_mdss_dsi_ft8716u(struct drm_panel *panel)
{
	return container_of(panel, struct mdss_dsi_ft8716u, panel);
}

static void mdss_dsi_ft8716u_reset(struct mdss_dsi_ft8716u *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int mdss_dsi_ft8716u_on(struct mdss_dsi_ft8716u *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x87, 0x16, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x87, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x81);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0xe8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x82);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0x85);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x83);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x84);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0xe8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x85);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0x85);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x86);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x87);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x88);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x89);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x8a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x8b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x8c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x98);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc5, 0x33);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x00, 0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 5000, 6000);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);

	return dsi_ctx.accum_err;
}

static int mdss_dsi_ft8716u_off(struct mdss_dsi_ft8716u *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int mdss_dsi_ft8716u_prepare(struct drm_panel *panel)
{
	struct mdss_dsi_ft8716u *ctx = to_mdss_dsi_ft8716u(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	mdss_dsi_ft8716u_reset(ctx);

	ret = mdss_dsi_ft8716u_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	return 0;
}

static int mdss_dsi_ft8716u_unprepare(struct drm_panel *panel)
{
	struct mdss_dsi_ft8716u *ctx = to_mdss_dsi_ft8716u(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = mdss_dsi_ft8716u_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return 0;
}

static const struct drm_display_mode mdss_dsi_ft8716u_mode = {
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
	.type = DRM_MODE_TYPE_DRIVER,
};

static int mdss_dsi_ft8716u_get_modes(struct drm_panel *panel,
				      struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &mdss_dsi_ft8716u_mode);
}

static const struct drm_panel_funcs mdss_dsi_ft8716u_panel_funcs = {
	.prepare = mdss_dsi_ft8716u_prepare,
	.unprepare = mdss_dsi_ft8716u_unprepare,
	.get_modes = mdss_dsi_ft8716u_get_modes,
};

static int mdss_dsi_ft8716u_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct mdss_dsi_ft8716u *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct mdss_dsi_ft8716u, panel,
				   &mdss_dsi_ft8716u_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

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

static void mdss_dsi_ft8716u_remove(struct mipi_dsi_device *dsi)
{
	struct mdss_dsi_ft8716u *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id mdss_dsi_ft8716u_of_match[] = {
	{ .compatible = "focaltech,ft8716u" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mdss_dsi_ft8716u_of_match);

static struct mipi_dsi_driver mdss_dsi_ft8716u_driver = {
	.probe = mdss_dsi_ft8716u_probe,
	.remove = mdss_dsi_ft8716u_remove,
	.driver = {
		.name = "panel-mdss-dsi-ft8716u",
		.of_match_table = mdss_dsi_ft8716u_of_match,
	},
};
module_mipi_dsi_driver(mdss_dsi_ft8716u_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for ft8716 1080p video mode dsi panel pl2");
MODULE_LICENSE("GPL");
