// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung S6E3FA3 1080x1920 command-mode AMOLED panel (OPPO R11T / 16051).
 * Init sequence extracted from stock OPPO device tree.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct s6e3fa3 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
	enum drm_panel_orientation orientation;
};

static const struct regulator_bulk_data s6e3fa3_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "vdd" },
	{ .supply = "oledb" },
};

static inline struct s6e3fa3 *to_s6e3fa3(struct drm_panel *panel)
{
	return container_of(panel, struct s6e3fa3, panel);
}

static void s6e3fa3_reset(struct s6e3fa3 *ctx)
{
	/* Stock physical: high 5ms, low 2ms, high 12ms; leave high (run). */
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(12000, 13000);
}

static int s6e3fa3_dcs_write(struct s6e3fa3 *ctx, u8 type,
			     const u8 *data, size_t len, unsigned int delay_ms)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.type = type,
		.flags = dsi->mode_flags & MIPI_DSI_MODE_LPM ?
			 MIPI_DSI_MSG_USE_LPM : 0,
		.tx_len = len,
		.tx_buf = data,
	};
	ssize_t ret;

	if (!dsi->host->ops || !dsi->host->ops->transfer)
		return -ENOSYS;

	ret = dsi->host->ops->transfer(dsi->host, &msg);
	if (ret < 0)
		return ret;

	if (delay_ms)
		msleep(delay_ms);

	return 0;
}

#define S6E3FA3_DCS_WRITE(ctx, type, delay_ms, seq...) ({ \
	const u8 d[] = { seq }; \
	s6e3fa3_dcs_write(ctx, type, d, ARRAY_SIZE(d), delay_ms); \
})

static int s6e3fa3_on(struct s6e3fa3 *ctx)
{
	struct device *dev = &ctx->dsi->dev;
	u8 power_mode;
	int ret;

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_SHORT_WRITE, 5,
				 MIPI_DCS_EXIT_SLEEP_MODE);
	if (ret)
		return ret;

	/* Stock requires DCS long packets even for two-byte vendor writes. */
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 1,
				 0xf0, 0xa5, 0xa5);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 1,
				 0xfc, 0x5a, 0x5a);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 1, 0xb0, 0x1c);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 1, 0xb5, 0x34);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 120,
				 0xfc, 0xa5, 0xa5);
	if (ret)
		return ret;

	/* Gamma / seed path from stock on-command */
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 1,
				 0xf0, 0x5a, 0x5a);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 1, 0xc3, 0xe0);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 1, 0xb0, 0x01);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 1, 0xc3, 0x49);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 1, 0xb0, 0x0d);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 1, 0xc3, 0x14);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 1, 0xb0, 0x0e);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 1, 0xc3, 0x25);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 1, 0xb0, 0x18);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 1, 0xc3, 0x53);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 1, 0xb0, 0x19);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_LONG_WRITE, 1,
				 0xc3, 0xbe, 0x03, 0x05, 0x05, 0xff, 0x02, 0x00,
				 0x00, 0xff, 0x00, 0xff, 0xff, 0xf0, 0x00, 0xf0,
				 0xe6, 0xbe, 0x0f, 0xff, 0xf3, 0xff);
	if (ret)
		return ret;

	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_SHORT_WRITE_PARAM, 1,
				 MIPI_DCS_SET_TEAR_ON, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_SHORT_WRITE_PARAM, 1,
				 MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_SHORT_WRITE_PARAM, 10,
				 MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0xb4);
	if (ret)
		return ret;
	ret = S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_SHORT_WRITE, 1,
				 MIPI_DCS_SET_DISPLAY_ON);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_get_power_mode(ctx->dsi, &power_mode);
	if (ret < 0)
		dev_warn(dev, "failed to read power mode: %d\n", ret);
	else
		dev_info(dev, "power mode: 0x%02x (expected 0x9c)\n", power_mode);

	return 0;
}

static int s6e3fa3_off(struct s6e3fa3 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 40);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int s6e3fa3_prepare(struct drm_panel *panel)
{
	struct s6e3fa3 *ctx = to_s6e3fa3(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(s6e3fa3_supplies), ctx->supplies);
	if (ret)
		return ret;

	/* prepare_prev_first puts the host in LP11 before this stock delay. */
	usleep_range(5000, 6000);
	s6e3fa3_reset(ctx);

	ret = s6e3fa3_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		regulator_bulk_disable(ARRAY_SIZE(s6e3fa3_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int s6e3fa3_unprepare(struct drm_panel *panel)
{
	struct s6e3fa3 *ctx = to_s6e3fa3(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = s6e3fa3_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	regulator_bulk_disable(ARRAY_SIZE(s6e3fa3_supplies), ctx->supplies);

	return 0;
}

/* Stock porch: HFP=118 HBP=70 HPW=16, VFP=20 VBP=4 VPW=2 @ 60Hz */
static const struct drm_display_mode s6e3fa3_mode = {
	.clock = (1080 + 118 + 16 + 70) * (1920 + 20 + 2 + 4) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 118,
	.hsync_end = 1080 + 118 + 16,
	.htotal = 1080 + 118 + 16 + 70,
	.vdisplay = 1920,
	.vsync_start = 1920 + 20,
	.vsync_end = 1920 + 20 + 2,
	.vtotal = 1920 + 20 + 2 + 4,
	.width_mm = 68,
	.height_mm = 122,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int s6e3fa3_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &s6e3fa3_mode);
}

static enum drm_panel_orientation s6e3fa3_get_orientation(struct drm_panel *panel)
{
	struct s6e3fa3 *ctx = to_s6e3fa3(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs s6e3fa3_panel_funcs = {
	.prepare = s6e3fa3_prepare,
	.unprepare = s6e3fa3_unprepare,
	.get_modes = s6e3fa3_get_modes,
	.get_orientation = s6e3fa3_get_orientation,
};

static int s6e3fa3_bl_update_status(struct backlight_device *bl)
{
	struct s6e3fa3 *ctx = bl_get_data(bl);
	u8 brightness = backlight_get_brightness(bl);

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return S6E3FA3_DCS_WRITE(ctx, MIPI_DSI_DCS_SHORT_WRITE_PARAM, 0,
				  MIPI_DCS_SET_DISPLAY_BRIGHTNESS, brightness);
}

static const struct backlight_ops s6e3fa3_bl_ops = {
	.update_status = s6e3fa3_bl_update_status,
};

static struct backlight_device *s6e3fa3_create_backlight(struct s6e3fa3 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 180,
		.max_brightness = 255,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, ctx,
					      &s6e3fa3_bl_ops, &props);
}

static int s6e3fa3_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6e3fa3 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev, ARRAY_SIZE(s6e3fa3_supplies),
					    s6e3fa3_supplies, &ctx->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	ret = of_drm_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get orientation\n");

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	/*
	 * Stock is dsi_cmd_mode. Keep non-continuous clock + LPM.
	 * Do not set MIPI_DSI_MODE_VIDEO.
	 */
	dsi->mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &s6e3fa3_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = s6e3fa3_create_backlight(ctx);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void s6e3fa3_remove(struct mipi_dsi_device *dsi)
{
	struct s6e3fa3 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id s6e3fa3_of_match[] = {
	{ .compatible = "samsung,s6e3fa3" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s6e3fa3_of_match);

static struct mipi_dsi_driver s6e3fa3_driver = {
	.probe = s6e3fa3_probe,
	.remove = s6e3fa3_remove,
	.driver = {
		.name = "panel-samsung-s6e3fa3",
		.of_match_table = s6e3fa3_of_match,
	},
};
module_mipi_dsi_driver(s6e3fa3_driver);

MODULE_DESCRIPTION("DRM driver for Samsung S6E3FA3 command mode DSI panel");
MODULE_LICENSE("GPL");
