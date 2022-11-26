// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct nt36672_tianma {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator *supply;
	struct gpio_desc *reset_gpio;
};

static inline struct nt36672_tianma *to_nt36672_tianma(struct drm_panel *panel)
{
	return container_of(panel, struct nt36672_tianma, panel);
}

static void nt36672_tianma_reset(struct nt36672_tianma *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(15000, 16000);
}

static int nt36672_tianma_on(struct nt36672_tianma *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_PARTIAL_ROWS, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_PARTIAL_COLUMNS, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x32, 0x2f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x10);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 70);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x00ff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x03, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x2c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 5000, 6000);

	return dsi_ctx.accum_err;
}

static int nt36672_tianma_off(struct nt36672_tianma *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int nt36672_tianma_prepare(struct drm_panel *panel)
{
	struct nt36672_tianma *ctx = to_nt36672_tianma(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_enable(ctx->supply);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulator: %d\n", ret);
		return ret;
	}

	nt36672_tianma_reset(ctx);

	ret = nt36672_tianma_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_disable(ctx->supply);
		return ret;
	}

	return 0;
}

static int nt36672_tianma_unprepare(struct drm_panel *panel)
{
	struct nt36672_tianma *ctx = to_nt36672_tianma(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = nt36672_tianma_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_disable(ctx->supply);

	return 0;
}

static const struct drm_display_mode nt36672_tianma_mode = {
	.clock = (1080 + 96 + 4 + 56) * (2160 + 4 + 2 + 33) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 96,
	.hsync_end = 1080 + 96 + 4,
	.htotal = 1080 + 96 + 4 + 56,
	.vdisplay = 2160,
	.vsync_start = 2160 + 4,
	.vsync_end = 2160 + 4 + 2,
	.vtotal = 2160 + 4 + 2 + 33,
	.width_mm = 68,
	.height_mm = 136,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int nt36672_tianma_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &nt36672_tianma_mode);
}

static const struct drm_panel_funcs nt36672_tianma_panel_funcs = {
	.prepare = nt36672_tianma_prepare,
	.unprepare = nt36672_tianma_unprepare,
	.get_modes = nt36672_tianma_get_modes,
};

static int nt36672_tianma_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct nt36672_tianma *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct nt36672_tianma, panel,
				   &nt36672_tianma_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->supply = devm_regulator_get(dev, "vdda");
	if (IS_ERR(ctx->supply))
		return dev_err_probe(dev, PTR_ERR(ctx->supply),
				     "Failed to get vdda regulator\n");

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

static void nt36672_tianma_remove(struct mipi_dsi_device *dsi)
{
	struct nt36672_tianma *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id nt36672_tianma_of_match[] = {
	{ .compatible = "mdss,nt36672-tianma" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nt36672_tianma_of_match);

static struct mipi_dsi_driver nt36672_tianma_driver = {
	.probe = nt36672_tianma_probe,
	.remove = nt36672_tianma_remove,
	.driver = {
		.name = "panel-nt36672-tianma",
		.of_match_table = nt36672_tianma_of_match,
	},
};
module_mipi_dsi_driver(nt36672_tianma_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for tianma nt36672 fhd video mode dsi panel");
MODULE_LICENSE("GPL");
