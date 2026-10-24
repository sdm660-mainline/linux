// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 Paul Sajna <hello@paulsajna.com>
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree
// Copyright (c) 2013, The Linux Foundation. All rights reserved.

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct synaptics_td4310;

struct synaptics_td4310_desc {
	const struct drm_display_mode *mode;
	const struct regulator_bulk_data *supplies;
	unsigned int num_supplies;
	unsigned long mode_flags;
	unsigned int delay_ms_display_off;
	void (*reset)(struct synaptics_td4310 *ctx);
	int (*init)(struct synaptics_td4310 *ctx);
};

struct synaptics_td4310 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
	const struct synaptics_td4310_desc *desc;
};

static inline struct synaptics_td4310 *to_synaptics_td4310(struct drm_panel *panel)
{
	return container_of(panel, struct synaptics_td4310, panel);
}

static const struct regulator_bulk_data syna_td4310_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "vddneg" },
	{ .supply = "vddpos" },
};

static void syna_td4310_reset(struct synaptics_td4310 *ctx)
{
	usleep_range(5000, 6000);

	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
}

static int syna_td4310_init(struct synaptics_td4310 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbb, 0x20, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0x01);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static const struct regulator_bulk_data td4310_fhdp_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static void td4310_fhdp_reset(struct synaptics_td4310 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(35);
}

static int td4310_fhdp_init(struct synaptics_td4310 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

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

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static int synaptics_td4310_off(struct synaptics_td4310 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, ctx->desc->delay_ms_display_off);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int synaptics_td4310_prepare(struct drm_panel *panel)
{
	struct synaptics_td4310 *ctx = to_synaptics_td4310(panel);
	const struct synaptics_td4310_desc *desc = ctx->desc;
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(desc->num_supplies, ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	desc->reset(ctx);

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = desc->init(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(desc->num_supplies, ctx->supplies);
		return ret;
	}

	return 0;
}

static int synaptics_td4310_unprepare(struct drm_panel *panel)
{
	struct synaptics_td4310 *ctx = to_synaptics_td4310(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = synaptics_td4310_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	regulator_bulk_disable(ctx->desc->num_supplies, ctx->supplies);

	return 0;
}

static const struct drm_display_mode syna_td4310_mode = {
	.clock = (1080 + 120 + 2 + 40) * (1620 + 8 + 2 + 6) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 120,
	.hsync_end = 1080 + 120 + 2,
	.htotal = 1080 + 120 + 2 + 40,
	.vdisplay = 1620,
	.vsync_start = 1620 + 8,
	.vsync_end = 1620 + 8 + 2,
	.vtotal = 1620 + 8 + 2 + 6,
	.width_mm = 63,
	.height_mm = 95,
	.type = DRM_MODE_TYPE_DRIVER,
};

static const struct drm_display_mode tianma_tl060fvms47_mode = {
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

static const struct drm_display_mode shenchao_pt060jc_mode = {
	.clock = (1080 + 130 + 12 + 16) * (2160 + 8 + 10 + 50) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 130,
	.hsync_end = 1080 + 130 + 12,
	.htotal = 1080 + 130 + 12 + 16,
	.vdisplay = 2160,
	.vsync_start = 2160 + 8,
	.vsync_end = 2160 + 8 + 10,
	.vtotal = 2160 + 8 + 10 + 50,
	.width_mm = 68,
	.height_mm = 136,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int synaptics_td4310_get_modes(struct drm_panel *panel,
				      struct drm_connector *connector)
{
	struct synaptics_td4310 *ctx = to_synaptics_td4310(panel);

	return drm_connector_helper_get_modes_fixed(connector, ctx->desc->mode);
}

static const struct drm_panel_funcs synaptics_td4310_panel_funcs = {
	.prepare = synaptics_td4310_prepare,
	.unprepare = synaptics_td4310_unprepare,
	.get_modes = synaptics_td4310_get_modes,
};

static int synaptics_td4310_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct synaptics_td4310 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct synaptics_td4310, panel,
				   &synaptics_td4310_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->desc = device_get_match_data(dev);
	if (!ctx->desc)
		return -ENODEV;

	ret = devm_regulator_bulk_get_const(dev, ctx->desc->num_supplies,
					    ctx->desc->supplies,
					    &ctx->supplies);
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
	dsi->mode_flags = ctx->desc->mode_flags;

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

static void synaptics_td4310_remove(struct mipi_dsi_device *dsi)
{
	struct synaptics_td4310 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct synaptics_td4310_desc syna_td4310_desc = {
	.mode = &syna_td4310_mode,
	.supplies = syna_td4310_supplies,
	.num_supplies = ARRAY_SIZE(syna_td4310_supplies),
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		      MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.delay_ms_display_off = 34,
	.reset = syna_td4310_reset,
	.init = syna_td4310_init,
};

static const struct synaptics_td4310_desc tianma_tl060fvms47_desc = {
	.mode = &tianma_tl060fvms47_mode,
	.supplies = td4310_fhdp_supplies,
	.num_supplies = ARRAY_SIZE(td4310_fhdp_supplies),
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		      MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.delay_ms_display_off = 20,
	.reset = td4310_fhdp_reset,
	.init = td4310_fhdp_init,
};

static const struct synaptics_td4310_desc shenchao_pt060jc_desc = {
	.mode = &shenchao_pt060jc_mode,
	.supplies = td4310_fhdp_supplies,
	.num_supplies = ARRAY_SIZE(td4310_fhdp_supplies),
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		      MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.delay_ms_display_off = 20,
	.reset = td4310_fhdp_reset,
	.init = td4310_fhdp_init,
};

static const struct of_device_id synaptics_td4310_of_match[] = {
	{ .compatible = "syna,td4310", .data = &syna_td4310_desc },
	{ .compatible = "tianma,tl060fvms47-00", .data = &tianma_tl060fvms47_desc },
	{ .compatible = "shenchao,pt060jc-04n", .data = &shenchao_pt060jc_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, synaptics_td4310_of_match);

static struct mipi_dsi_driver synaptics_td4310_driver = {
	.probe = synaptics_td4310_probe,
	.remove = synaptics_td4310_remove,
	.driver = {
		.name = "panel-synaptics-td4310",
		.of_match_table = synaptics_td4310_of_match,
	},
};
module_mipi_dsi_driver(synaptics_td4310_driver);

MODULE_AUTHOR("Paul Sajna <hello@paulsajna.com>");
MODULE_DESCRIPTION("DRM driver for Synaptics TD4310 based DSI panels");
MODULE_LICENSE("GPL");
