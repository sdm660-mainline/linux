// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 Fabricio Akio <fabricioakio@gmail.com>
//
// The driver's structure is based on panel-boe-td4320.c,
// Copyright (c) 2024 Barnabas Czeman <barnabas.czeman@mainlining.org>,
// generated with linux-mdss-dsi-panel-driver-generator from a vendor device
// tree: Copyright (c) 2013, The Linux Foundation. All rights reserved.
//
// Initialisation and timings transcribed from the vendor device tree on the
// device, node qcom,mdss_dsi_mot_boe_520_1080p_vid_v0.

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct boe_td4300 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data boe_td4300_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct boe_td4300 *to_boe_td4300(struct drm_panel *panel)
{
	return container_of(panel, struct boe_td4300, panel);
}

static int boe_td4300_on(struct boe_td4300 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	/*
	 * The vendor sends 0x51 with a single parameter; the u16 helper
	 * always writes two, so the command is spelled out here.
	 */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
				     0xcc);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x2c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x01);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 100);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc1,
					 0x80, 0x08, 0x01, 0xff, 0xab, 0x2b,
					 0xa3, 0x39, 0x4f, 0x29, 0xc5, 0x9a,
					 0x53, 0xea, 0x7f, 0xa5, 0x5c, 0x63,
					 0x4a, 0x29, 0x70, 0x8c, 0x18, 0x57,
					 0xfd, 0x0f, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x40, 0x00,
					 0x22, 0x03, 0x02, 0x03, 0x82, 0x00,
					 0x01, 0x00, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb8,
					 0x57, 0x3d, 0x19, 0x1e, 0x0a, 0x00,
					 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xca,
					 0x1d, 0xfc, 0xfc, 0xfc, 0x00, 0x0e,
					 0xf3, 0x9b, 0x00, 0xa8, 0xc4, 0xcd,
					 0x00, 0xf1, 0xfe, 0xf4, 0xf7, 0x0e,
					 0xd7, 0xee, 0xdb, 0xf4, 0xff, 0x00,
					 0x00, 0xff, 0x00, 0x00, 0xff, 0x00,
					 0xff, 0x00, 0x00, 0xff, 0x00, 0xff,
					 0x00, 0x00, 0xff, 0x00, 0x00, 0xff,
					 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb,
					 0xfc, 0xff, 0xf9, 0xff, 0x03, 0x04,
					 0x8e, 0x19, 0x07, 0x02, 0xf8, 0x01,
					 0x00, 0xf8, 0x01, 0x00, 0x40);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 10);

	return dsi_ctx.accum_err;
}

static int boe_td4300_off(struct boe_td4300 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 35);

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc1,
					 0x80, 0x08, 0x01, 0xff, 0x2b, 0xa5,
					 0x94, 0x52, 0x4a, 0x29, 0xa5, 0x94,
					 0x52, 0xea, 0x7f, 0xa5, 0x94, 0x52,
					 0x4a, 0x29, 0xa5, 0x94, 0x52, 0x4a,
					 0xfd, 0x0f, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x40, 0x00,
					 0x22, 0x03, 0x02, 0x03, 0x82, 0x00,
					 0x01, 0x00, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb,
					 0xfc, 0xff, 0xf9, 0xff, 0x03, 0x00,
					 0x78, 0xe0, 0x01, 0x00, 0xfc, 0xff,
					 0xf9, 0xff, 0x03, 0x00, 0x40);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x03);
	mipi_dsi_msleep(&dsi_ctx, 20);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int boe_td4300_prepare(struct drm_panel *panel)
{
	struct boe_td4300 *ctx = to_boe_td4300(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(boe_td4300_supplies),
				    ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to enable regulators\n");

	/*
	 * The vendor supply entries for this panel ask for 10ms after vddio
	 * and 10ms after the last source rail, either side of releasing reset.
	 * Reset itself is asserted in unprepare and held low for the whole of
	 * the off period, which is what the vendor reset sequence <1 0> -- a
	 * bare release, no pulse -- assumes has already happened.
	 */
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);

	ret = boe_td4300_on(ctx);
	if (ret < 0) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(boe_td4300_supplies),
				       ctx->supplies);
		return dev_err_probe(dev, ret, "Failed to initialize panel\n");
	}

	return 0;
}

static int boe_td4300_unprepare(struct drm_panel *panel)
{
	struct boe_td4300 *ctx = to_boe_td4300(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = boe_td4300_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(boe_td4300_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode boe_td4300_mode = {
	.clock = (1080 + 104 + 4 + 100) * (1920 + 8 + 2 + 4) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 104,
	.hsync_end = 1080 + 104 + 4,
	.htotal = 1080 + 104 + 4 + 100,
	.vdisplay = 1920,
	.vsync_start = 1920 + 8,
	.vsync_end = 1920 + 8 + 2,
	.vtotal = 1920 + 8 + 2 + 4,
	.width_mm = 64,
	.height_mm = 115,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int boe_td4300_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector,
						    &boe_td4300_mode);
}

static const struct drm_panel_funcs boe_td4300_panel_funcs = {
	.prepare = boe_td4300_prepare,
	.unprepare = boe_td4300_unprepare,
	.get_modes = boe_td4300_get_modes,
};

static int boe_td4300_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct boe_td4300 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct boe_td4300, panel,
				   &boe_td4300_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(boe_td4300_supplies),
					    boe_td4300_supplies,
					    &ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

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

static void boe_td4300_remove(struct mipi_dsi_device *dsi)
{
	struct boe_td4300 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id boe_td4300_of_match[] = {
	{ .compatible = "boe,td4300" },
	{ }
};
MODULE_DEVICE_TABLE(of, boe_td4300_of_match);

static struct mipi_dsi_driver boe_td4300_driver = {
	.probe = boe_td4300_probe,
	.remove = boe_td4300_remove,
	.driver = {
		.name = "panel-boe-td4300",
		.of_match_table = boe_td4300_of_match,
	},
};
module_mipi_dsi_driver(boe_td4300_driver);

MODULE_AUTHOR("Fabricio Akio <fabricioakio@gmail.com>");
MODULE_DESCRIPTION("DRM driver for the BOE TD4300 DSI panel");
MODULE_LICENSE("GPL");
