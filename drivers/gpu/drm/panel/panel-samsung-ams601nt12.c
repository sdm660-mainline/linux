// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025, Jack Matthews <jm5112356@gmail.com>
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved.

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

/* Manufacturer Command Set */
#define MCS_ACCESS_PROT_OFF 0xb0
#define MCS_UNKNOWN_B1 0xb1
#define MCS_PASSWD1 0xf0

struct ams601nt12 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
};

static inline struct ams601nt12 *to_ams601nt12(struct drm_panel *panel)
{
	return container_of(panel, struct ams601nt12, panel);
}

static void ams601nt12_reset(struct ams601nt12 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(11000, 12000);
}

static int ams601nt12_on(struct ams601nt12 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD1, 0x5a,
				     0x5a); // unlock
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	//mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD1, 0xa5, 0xa5); // lock
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0x0000, 0x086f);
	//mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD1, 0x5a, 0x5a); //unlock
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_UNKNOWN_B1,
				     0x59); // 10-bit dimming
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MCS_PASSWD1, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x20);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x0000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int ams601nt12_off(struct ams601nt12 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 35);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int ams601nt12_prepare(struct drm_panel *panel)
{
	struct ams601nt12 *ctx = to_ams601nt12(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ams601nt12_reset(ctx);

	ret = ams601nt12_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	return 0;
}

static int ams601nt12_unprepare(struct drm_panel *panel)
{
	struct ams601nt12 *ctx = to_ams601nt12(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = ams601nt12_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return 0;
}

static const struct drm_display_mode ams601nt12_mode = {
	.clock = (1080 + 44 + 12 + 32) * (2160 + 8 + 4 + 4) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 44,
	.hsync_end = 1080 + 44 + 12,
	.htotal = 1080 + 44 + 12 + 32,
	.vdisplay = 2160,
	.vsync_start = 2160 + 8,
	.vsync_end = 2160 + 8 + 4,
	.vtotal = 2160 + 8 + 4 + 4,
	.width_mm = 68,
	.height_mm = 121,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int ams601nt12_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector,
						    &ams601nt12_mode);
}

static const struct drm_panel_funcs ams601nt12_panel_funcs = {
	.prepare = ams601nt12_prepare,
	.unprepare = ams601nt12_unprepare,
	.get_modes = ams601nt12_get_modes,
};

static int ams601nt12_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int ams601nt12_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops ams601nt12_bl_ops = {
	.update_status = ams601nt12_bl_update_status,
	.get_brightness = ams601nt12_bl_get_brightness,
};

static struct backlight_device *
ams601nt12_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 1023,
		.max_brightness = 1023,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &ams601nt12_bl_ops, &props);
}

static int ams601nt12_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ams601nt12 *ctx;
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
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &ams601nt12_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = ams601nt12_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret,
				     "Failed to attach to DSI host\n");
	}

	return 0;
}

static void ams601nt12_remove(struct mipi_dsi_device *dsi)
{
	struct ams601nt12 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ams601nt12_of_match[] = {
	{ .compatible = "samsung,ams601nt12" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ams601nt12_of_match);

static struct mipi_dsi_driver ams601nt12_driver = {
	.probe = ams601nt12_probe,
	.remove = ams601nt12_remove,
	.driver = {
		.name = "panel-samsung-ams601nt12",
		.of_match_table = ams601nt12_of_match,
	},
};
module_mipi_dsi_driver(ams601nt12_driver);

MODULE_AUTHOR("Jack Matthews <jm5112356@gmail.com>");
MODULE_DESCRIPTION("DRM driver for SAMSUNG AMS601NT12 cmd mode dsi panel");
MODULE_LICENSE("GPL");
