// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 Alexey Minnekhanov <alexeymin@postmarketos.org>
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree
//   Copyright (c) 2013, The Linux Foundation. All rights reserved.

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

struct td4310_tianma {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
};

static const struct regulator_bulk_data td4310_tianma_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct td4310_tianma *to_td4310_tianma(struct drm_panel *panel)
{
	return container_of(panel, struct td4310_tianma, panel);
}

static void td4310_tianma_reset(struct td4310_tianma *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(35);
}

static int td4310_tianma_on(struct td4310_tianma *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	/* mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_EXIT_SLEEP_MODE, 0x00); */
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 70);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xea, 0x0a);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x00ff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x2c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce,
					 0x55, 0x40, 0x56, 0x6e, 0x87, 0x95,
					 0xa0, 0xb0, 0xc0, 0xc2, 0xc9, 0xce,
					 0xdf, 0xe3, 0xe4, 0xec, 0xff, 0x04,
					 0x00, 0x00, 0x04, 0x46, 0x04, 0x69,
					 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);

	/* mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_DISPLAY_ON, 0x00); */
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static int td4310_tianma_off(struct td4310_tianma *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int td4310_tianma_prepare(struct drm_panel *panel)
{
	struct td4310_tianma *ctx = to_td4310_tianma(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(td4310_tianma_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	td4310_tianma_reset(ctx);

	ret = td4310_tianma_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(td4310_tianma_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int td4310_tianma_unprepare(struct drm_panel *panel)
{
	struct td4310_tianma *ctx = to_td4310_tianma(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = td4310_tianma_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(td4310_tianma_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode td4310_tianma_mode = {
	.clock = (1080 + 108 + 12 + 60) * (2160 + 6 + 4 + 33) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 108,
	.hsync_end = 1080 + 108 + 12,
	.htotal = 1080 + 108 + 12 + 60,
	.vdisplay = 2160,
	.vsync_start = 2160 + 6,
	.vsync_end = 2160 + 6 + 4,
	.vtotal = 2160 + 6 + 4 + 33,
	.width_mm = 68,
	.height_mm = 136,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int td4310_tianma_get_modes(struct drm_panel *panel,
				   struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &td4310_tianma_mode);
}

static const struct drm_panel_funcs td4310_tianma_panel_funcs = {
	.prepare = td4310_tianma_prepare,
	.unprepare = td4310_tianma_unprepare,
	.get_modes = td4310_tianma_get_modes,
};

static int td4310_tianma_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct td4310_tianma *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct td4310_tianma, panel,
				   &td4310_tianma_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(td4310_tianma_supplies),
					    td4310_tianma_supplies,
					    &ctx->supplies);

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

static void td4310_tianma_remove(struct mipi_dsi_device *dsi)
{
	struct td4310_tianma *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id td4310_tianma_of_match[] = {
	{ .compatible = "tianma,td4310-xiaomi-whyred" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, td4310_tianma_of_match);

static struct mipi_dsi_driver td4310_tianma_driver = {
	.probe = td4310_tianma_probe,
	.remove = td4310_tianma_remove,
	.driver = {
		.name = "panel-tianma-td4310",
		.of_match_table = td4310_tianma_of_match,
	},
};
module_mipi_dsi_driver(td4310_tianma_driver);

MODULE_AUTHOR("Alexey Minnekhanov <alexeymin@postmarketos.org>");
MODULE_DESCRIPTION("DRM driver for Tianma TD4310 video mode DSI panel");
MODULE_LICENSE("GPL");
