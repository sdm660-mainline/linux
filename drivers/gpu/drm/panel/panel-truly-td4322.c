// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024, Bhushan Shah <bshah@kde.org>
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved.

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct truly_td4322 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
};

static inline struct truly_td4322 *to_truly_td4322(struct drm_panel *panel)
{
	return container_of(panel, struct truly_td4322, panel);
}

static void truly_td4322_reset(struct truly_td4322 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(30);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(150);
}

static int truly_td4322_on(struct truly_td4322 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xd5,
				   0x03, 0x00, 0x00, 0x02, 0x23, 0x02, 0x23);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	msleep(30);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	return 0;
}

static int truly_td4322_off(struct truly_td4322 *ctx)
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
	msleep(20);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	return 0;
}

static int truly_td4322_prepare(struct drm_panel *panel)
{
	struct truly_td4322 *ctx = to_truly_td4322(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	truly_td4322_reset(ctx);

	ret = truly_td4322_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	return 0;
}

static int truly_td4322_unprepare(struct drm_panel *panel)
{
	struct truly_td4322 *ctx = to_truly_td4322(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = truly_td4322_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return 0;
}

static const struct drm_display_mode truly_td4322_mode = {
	.clock = (1080 + 104 + 20 + 56) * (1920 + 10 + 2 + 8) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 104,
	.hsync_end = 1080 + 104 + 20,
	.htotal = 1080 + 104 + 20 + 56,
	.vdisplay = 1920,
	.vsync_start = 1920 + 10,
	.vsync_end = 1920 + 10 + 2,
	.vtotal = 1920 + 10 + 2 + 8,
	.width_mm = 64,
	.height_mm = 115,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int truly_td4322_get_modes(struct drm_panel *panel,
				  struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &truly_td4322_mode);
}

static const struct drm_panel_funcs truly_td4322_panel_funcs = {
	.prepare = truly_td4322_prepare,
	.unprepare = truly_td4322_unprepare,
	.get_modes = truly_td4322_get_modes,
};

static int truly_td4322_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct truly_td4322 *ctx;
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
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &truly_td4322_panel_funcs,
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

static void truly_td4322_remove(struct mipi_dsi_device *dsi)
{
	struct truly_td4322 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id truly_td4322_of_match[] = {
	{ .compatible = "truly,td4322" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, truly_td4322_of_match);

static struct mipi_dsi_driver truly_td4322_driver = {
	.probe = truly_td4322_probe,
	.remove = truly_td4322_remove,
	.driver = {
		.name = "panel-td4322-truly",
		.of_match_table = truly_td4322_of_match,
	},
};
module_mipi_dsi_driver(truly_td4322_driver);

MODULE_AUTHOR("Bhushan Shah <bshah@kde.org>");
MODULE_DESCRIPTION("DRM driver for truly td4322 command mode dsi panel");
MODULE_LICENSE("GPL");
