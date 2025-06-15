// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 Linaro Ltd
 * Author: Sumit Semwal <sumit.semwal@linaro.org>
 *
 * This driver is for the DSI interface to panels using the NT36672A display driver IC
 * from Novatek.
 * Currently supported are the Tianma FHD+ panels found in some Xiaomi phones, including
 * some variants of the Poco F1 phone.
 *
 * Panels using the Novatek NT37762A IC should add appropriate configuration per-panel and
 * use this driver.
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>

#include <linux/gpio/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

static const char * const nt36672a_regulator_names[] = {
	"vddio",
	"vddpos",
	"vddneg",
};

static unsigned long const nt36672a_regulator_enable_loads[] = {
	62000,
	100000,
	100000
};

struct nt36672a_panel_desc {
	const struct drm_display_mode *display_mode;
	const char *panel_name;

	unsigned int width_mm;
	unsigned int height_mm;

	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	unsigned int lanes;
	enum gpiod_flags reset_gpio_flags;

	int (*init_cmds)(struct drm_panel *panel);
	int (*off_cmds)(struct drm_panel *panel);
};

struct nt36672a_panel {
	struct drm_panel base;
	struct mipi_dsi_device *link;
	const struct nt36672a_panel_desc *desc;

	struct regulator_bulk_data supplies[ARRAY_SIZE(nt36672a_regulator_names)];

	struct gpio_desc *reset_gpio;
};

static inline struct nt36672a_panel *to_nt36672a_panel(struct drm_panel *panel)
{
	return container_of(panel, struct nt36672a_panel, base);
}

static int tianma_beryllium_init_cmds(struct drm_panel *panel)
{
	struct nt36672a_panel *pinfo = to_nt36672a_panel(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = pinfo->link };

	/* send first part of init cmds (.on_cmds_1) */
	/* skin enhancement mode */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFF, 0x22);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x01, 0xC0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x02, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x03, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x07, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x08, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x09, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0A, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0B, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0C, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0D, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0E, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0F, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x60);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x13, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x58);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x15, 0x68);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x78);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0x39);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x2D);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1A, 0x2E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1B, 0x32);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1C, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1D, 0x3A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1E, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1F, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x21, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x22, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x23, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x25, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x26, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x27, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2D, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2F, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x30, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x31, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x32, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x33, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x34, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x36, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x37, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x38, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x39, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3A, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3B, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3D, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3F, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x40, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x41, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x42, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x43, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x44, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x45, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x46, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x47, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x48, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x49, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4A, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4B, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4C, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4D, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4E, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4F, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x51, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x52, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x54, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x55, 0xFE);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x56, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0xCD);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x59, 0xD0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5A, 0xD0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5B, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5C, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5D, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5E, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5F, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x69, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6A, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6B, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6C, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6D, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6E, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6F, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x70, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x71, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x72, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x73, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x74, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x75, 0x0C);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x76, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x77, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x0F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x79, 0x68);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7A, 0x88);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7C, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7D, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7E, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7F, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x83, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x84, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x85, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x86, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x87, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x88, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x89, 0x91);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8A, 0x98);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8B, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8C, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8D, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8E, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8F, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x92, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x93, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x95, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x96, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x97, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x98, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x99, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9A, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9B, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9C, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9D, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9E, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9F, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xA0, 0x8A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xA2, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xA6, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xA7, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xA9, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xAA, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xAB, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xAC, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xAD, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xAE, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xAF, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB7, 0x76);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB8, 0x76);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xB9, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBA, 0x0D);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBB, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBC, 0x0F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBD, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBE, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xBF, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC0, 0x0D);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC1, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC2, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC3, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC4, 0x0A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC5, 0xA0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC6, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC7, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC8, 0x39);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC9, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xCA, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xCD, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xDB, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xDC, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xDD, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xE0, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xE1, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xE2, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xE3, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xE4, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xE5, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xE6, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xE7, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xE8, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xE9, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xEA, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xEB, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xEC, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xED, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xEE, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xEF, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF0, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF1, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF2, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF3, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF4, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF5, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xF6, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFB, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFF, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFB, 0x01);
	/* dimming enable */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x01, 0x84);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x2D);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0x00);
	 /* resolution 1080*2246 */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x7B);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x15, 0x6F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x0B);
	 /* UI mode */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29, 0x0A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x30, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x31, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x32, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x33, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x34, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x36, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x37, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x38, 0xFC);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x39, 0xF8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3A, 0xF4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3B, 0xF1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3D, 0xEE);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3F, 0xEB);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x40, 0xE8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x41, 0xE5);
	 /* STILL mode */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2A, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x45, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x46, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x47, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x48, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x49, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4A, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4B, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4C, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4D, 0xED);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4E, 0xD5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4F, 0xBF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0xA6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x51, 0x96);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x52, 0x86);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x76);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x54, 0x66);
	 /* MOVING mode */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2B, 0x0E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x59, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5A, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5B, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5C, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5D, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5E, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5F, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0xF6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0xEA);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0xE1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0xD8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0xCE);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0xC3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0xBA);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0xB3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFF, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFB, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFF, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFB, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1C, 0xAF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFF, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFB, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x51, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x55, 0x00);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);

	/* 0x46 = 70 ms delay */
	mipi_dsi_msleep(&dsi_ctx, 70);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	/* Send rest of the init cmds (.on_cmds_2) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFF, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFB, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC3, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC4, 0x54);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFF, 0x10);

	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

/* Tianma-beryllium specific handler with specific delays */
static int tianma_beryllium_off_cmds(struct drm_panel *panel)
{
	struct nt36672a_panel *pinfo = to_nt36672a_panel(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = pinfo->link };

	/* send off cmds */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFF, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFB, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xC3, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xFF, 0x10);

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);

	/* 120ms delay required here as per DCS spec */
	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	/* 0x3C = 60ms delay */
	mipi_dsi_msleep(&dsi_ctx, 60);

	return dsi_ctx.accum_err;
}

static int lavender_tulip_init_cmds(struct drm_panel *panel)
{
	struct nt36672a_panel *pinfo = to_nt36672a_panel(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = pinfo->link };

	/*
	 * This function supports 3 similar panels with minimal differences
	 * in their init sequences. We assume base to be lavender-tianma
	 * panel, and 2 variations (lavender-shenchao and tulip) are handled as
	 * differences to base.
	 */
	const struct device_node *node = dev_of_node(&pinfo->link->dev);
	bool is_shenchao = of_device_is_compatible(node, "shenchao,fhdplus-video");
	bool is_tulip = of_device_is_compatible(node, "tianma,tl063fvmc43-02");

	pinfo->link->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0x96);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x04);

	if (is_shenchao)
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x27);
	else
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x20);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);

	if (is_shenchao)
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd9, 0x10);
	else
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x01);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x84, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x88, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8a, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8e, 0xe4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa9, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xaa, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xae, 0x8a);

	if (is_shenchao)
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0xfa);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x10);

	if (is_shenchao)
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x01);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 80);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x01);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);

	if (is_tulip) {
		mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x00ff);
	} else {
		/* lavender tianma + shenchao specific part */
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x03, 0x04);
		mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x00b8);
	}

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x2c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int lavender_tulip_off_cmds(struct drm_panel *panel)
{
	struct nt36672a_panel *pinfo = to_nt36672a_panel(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = pinfo->link };

	pinfo->link->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int txd_x00td_init_cmds(struct drm_panel *panel)
{
	struct nt36672a_panel *pinfo = to_nt36672a_panel(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = pinfo->link };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0x9e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x07, 0x94);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0e, 0x35);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0f, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6d, 0x66);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x69, 0x99);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x95, 0xf5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x96, 0xf5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x6c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x15, 0xe6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x01, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x02, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x03, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0xa9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x07, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x08, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x09, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0a, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0b, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0c, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0d, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0e, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0f, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x13, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x15, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0xa9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1a, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1b, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1d, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1e, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x21, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x22, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x23, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2f, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x30, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x31, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x32, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x33, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x34, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x37, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x38, 0x72);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x39, 0x72);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3f, 0x72);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x83);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x79, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7a, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7b, 0x9c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7d, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7e, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8e, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x92, 0x76);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x93, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x99, 0x33);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9b, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdc, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdd, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xde, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdf, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe9, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xed, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0,
				     0x00, 0x00, 0x00, 0x15, 0x00, 0x37, 0x00,
				     0x54, 0x00, 0x6d, 0x00, 0x84, 0x00, 0x98,
				     0x00, 0xac);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb1,
				     0x00, 0xbd, 0x00, 0xf9, 0x01, 0x25, 0x01,
				     0x6b, 0x01, 0x9c, 0x01, 0xec, 0x02, 0x22,
				     0x02, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2,
				     0x02, 0x5e, 0x02, 0x9e, 0x02, 0xc9, 0x02,
				     0xfd, 0x03, 0x21, 0x03, 0x4d, 0x03, 0x5a,
				     0x03, 0x68);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3,
				     0x03, 0x78, 0x03, 0x8b, 0x03, 0xa1, 0x03,
				     0xbd, 0x03, 0xd6, 0x03, 0xda);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4,
				     0x00, 0x00, 0x00, 0x15, 0x00, 0x37, 0x00,
				     0x54, 0x00, 0x6d, 0x00, 0x84, 0x00, 0x98,
				     0x00, 0xac);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5,
				     0x00, 0xbd, 0x00, 0xf9, 0x01, 0x25, 0x01,
				     0x6b, 0x01, 0x9c, 0x01, 0xec, 0x02, 0x22,
				     0x02, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb6,
				     0x02, 0x5e, 0x02, 0x9e, 0x02, 0xc9, 0x02,
				     0xfd, 0x03, 0x21, 0x03, 0x4d, 0x03, 0x5a,
				     0x03, 0x68);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7,
				     0x03, 0x78, 0x03, 0x8b, 0x03, 0xa1, 0x03,
				     0xbd, 0x03, 0xd6, 0x03, 0xda);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb8,
				     0x00, 0x00, 0x00, 0x15, 0x00, 0x37, 0x00,
				     0x54, 0x00, 0x6d, 0x00, 0x84, 0x00, 0x98,
				     0x00, 0xac);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9,
				     0x00, 0xbd, 0x00, 0xf9, 0x01, 0x25, 0x01,
				     0x6b, 0x01, 0x9c, 0x01, 0xec, 0x02, 0x22,
				     0x02, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba,
				     0x02, 0x5e, 0x02, 0x9e, 0x02, 0xc9, 0x02,
				     0xfd, 0x03, 0x21, 0x03, 0x4d, 0x03, 0x5a,
				     0x03, 0x68);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbb,
				     0x03, 0x78, 0x03, 0x8b, 0x03, 0xa1, 0x03,
				     0xbd, 0x03, 0xd6, 0x03, 0xda);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x21);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0,
				     0x00, 0x00, 0x00, 0x15, 0x00, 0x37, 0x00,
				     0x54, 0x00, 0x6d, 0x00, 0x84, 0x00, 0x98,
				     0x00, 0xac);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb1,
				     0x00, 0xbd, 0x00, 0xf9, 0x01, 0x25, 0x01,
				     0x6b, 0x01, 0x9c, 0x01, 0xec, 0x02, 0x22,
				     0x02, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2,
				     0x02, 0x5e, 0x02, 0x9e, 0x02, 0xc9, 0x02,
				     0xfd, 0x03, 0x21, 0x03, 0x4d, 0x03, 0x5a,
				     0x03, 0x68);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3,
				     0x03, 0x78, 0x03, 0x8b, 0x03, 0xa1, 0x03,
				     0xbd, 0x03, 0xd6, 0x03, 0xda);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4,
				     0x00, 0x00, 0x00, 0x15, 0x00, 0x37, 0x00,
				     0x54, 0x00, 0x6d, 0x00, 0x84, 0x00, 0x98,
				     0x00, 0xac);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5,
				     0x00, 0xbd, 0x00, 0xf9, 0x01, 0x25, 0x01,
				     0x6b, 0x01, 0x9c, 0x01, 0xec, 0x02, 0x22,
				     0x02, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb6,
				     0x02, 0x5e, 0x02, 0x9e, 0x02, 0xc9, 0x02,
				     0xfd, 0x03, 0x21, 0x03, 0x4d, 0x03, 0x5a,
				     0x03, 0x68);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7,
				     0x03, 0x78, 0x03, 0x8b, 0x03, 0xa1, 0x03,
				     0xbd, 0x03, 0xd6, 0x03, 0xda);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb8,
				     0x00, 0x00, 0x00, 0x15, 0x00, 0x37, 0x00,
				     0x54, 0x00, 0x6d, 0x00, 0x84, 0x00, 0x98,
				     0x00, 0xac);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9,
				     0x00, 0xbd, 0x00, 0xf9, 0x01, 0x25, 0x01,
				     0x6b, 0x01, 0x9c, 0x01, 0xec, 0x02, 0x22,
				     0x02, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba,
				     0x02, 0x5e, 0x02, 0x9e, 0x02, 0xc9, 0x02,
				     0xfd, 0x03, 0x21, 0x03, 0x4d, 0x03, 0x5a,
				     0x03, 0x68);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbb,
				     0x03, 0x78, 0x03, 0x8b, 0x03, 0xa1, 0x03,
				     0xbd, 0x03, 0xd6, 0x03, 0xda);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0a, 0x81);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0b, 0xd7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0c, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x82);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x21, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x22, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0x76);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x25, 0x76);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5c, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5d, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5e, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0x22);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x69, 0x60);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x71, 0x2d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8d, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd7, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd9, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xda, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdb, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdc, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0xc8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1a, 0x97);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1d, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1e, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x99, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x27);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x13, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0xb0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0xd0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 70);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);

	return dsi_ctx.accum_err;
}

static int txd_x00td_off(struct drm_panel *panel)
{
	struct nt36672a_panel *pinfo = to_nt36672a_panel(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = pinfo->link };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int tianma_jasmine_init_cmds(struct drm_panel *panel)
{
	struct nt36672a_panel *pinfo = to_nt36672a_panel(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = pinfo->link };

	pinfo->link->mode_flags |= MIPI_DSI_MODE_LPM;

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
	usleep_range(5000, 6000);

	return dsi_ctx.accum_err;
}

static int nt36672a_panel_power_off(struct drm_panel *panel)
{
	struct nt36672a_panel *pinfo = to_nt36672a_panel(panel);
	int ret = 0;

	gpiod_set_value(pinfo->reset_gpio, 1);

	ret = regulator_bulk_disable(ARRAY_SIZE(pinfo->supplies), pinfo->supplies);
	if (ret)
		dev_err(panel->dev, "regulator_bulk_disable failed %d\n", ret);

	return ret;
}

static int nt36672a_panel_unprepare(struct drm_panel *panel)
{
	struct nt36672a_panel *pinfo = to_nt36672a_panel(panel);
	int ret;

	/* send off cmds */
	if (pinfo->desc->off_cmds) {
		ret = pinfo->desc->off_cmds(panel);
		if (ret < 0)
			dev_err(panel->dev, "failed to send DCS off cmds: %d\n", ret);
		return ret;
	}

	ret = nt36672a_panel_power_off(panel);
	if (ret < 0)
		dev_err(panel->dev, "power_off failed ret = %d\n", ret);

	return ret;
}

static int nt36672a_panel_power_on(struct nt36672a_panel *pinfo)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(pinfo->supplies), pinfo->supplies);
	if (ret < 0)
		return ret;

	/*
	 * As per downstream kernel, Reset sequence of Tianma FHD panel requires the panel to
	 * be out of reset for 10ms, followed by being held in reset for 10ms. But for Android
	 * AOSP, we needed to bump it upto 200ms otherwise we get white screen sometimes.
	 * FIXME: Try to reduce this 200ms to a lesser value.
	 */
	gpiod_set_value(pinfo->reset_gpio, 1);
	msleep(200);
	gpiod_set_value(pinfo->reset_gpio, 0);
	msleep(200);

	return 0;
}

static int nt36672a_panel_prepare(struct drm_panel *panel)
{
	struct nt36672a_panel *pinfo = to_nt36672a_panel(panel);
	int err;

	err = nt36672a_panel_power_on(pinfo);
	if (err < 0)
		goto poweroff;

	err = pinfo->desc->init_cmds(panel);
	if (err < 0) {
		dev_err(panel->dev, "Failed to init panel!\n");
		goto poweroff;
	}

	return 0;

poweroff:
	gpiod_set_value(pinfo->reset_gpio, 0);
	return err;
}

static int nt36672a_panel_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	struct nt36672a_panel *pinfo = to_nt36672a_panel(panel);
	const struct drm_display_mode *m = pinfo->desc->display_mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, m);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n", m->hdisplay,
			m->vdisplay, drm_mode_vrefresh(m));
		return -ENOMEM;
	}

	connector->display_info.width_mm = pinfo->desc->width_mm;
	connector->display_info.height_mm = pinfo->desc->height_mm;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs panel_funcs = {
	.unprepare = nt36672a_panel_unprepare,
	.prepare = nt36672a_panel_prepare,
	.get_modes = nt36672a_panel_get_modes,
};

static const struct drm_display_mode tianma_fhd_video_panel_default_mode = {
	.clock		= 161331,

	.hdisplay	= 1080,
	.hsync_start	= 1080 + 40,
	.hsync_end	= 1080 + 40 + 20,
	.htotal		= 1080 + 40 + 20 + 44,

	.vdisplay	= 2246,
	.vsync_start	= 2246 + 15,
	.vsync_end	= 2246 + 15 + 2,
	.vtotal		= 2246 + 15 + 2 + 8,

	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct nt36672a_panel_desc tianma_fhd_video_panel_desc = {
	.display_mode = &tianma_fhd_video_panel_default_mode,

	.width_mm = 68,
	.height_mm = 136,

	.mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_VIDEO
			| MIPI_DSI_MODE_VIDEO_HSE
			| MIPI_DSI_CLOCK_NON_CONTINUOUS
			| MIPI_DSI_MODE_VIDEO_BURST,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
	.reset_gpio_flags = GPIOD_OUT_LOW,

	.init_cmds = tianma_beryllium_init_cmds,
	.off_cmds = tianma_beryllium_off_cmds,
};

/* common for both lavender panels */
static const struct drm_display_mode lavender_panel_default_mode = {
	.clock		= (1080 + 90 + 2 + 120) * (2340 + 10 + 3 + 8) * 60 / 1000,

	.hdisplay	= 1080,
	.hsync_start	= 1080 + 90,
	.hsync_end	= 1080 + 90 + 2,
	.htotal		= 1080 + 90 + 2 + 120,

	.vdisplay	= 2340,
	.vsync_start	= 2340 + 10,
	.vsync_end	= 2340 + 10 + 3,
	.vtotal		= 2340 + 10 + 3 + 8,

	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct nt36672a_panel_desc shenchao_lavender_panel_desc = {
	.display_mode = &lavender_panel_default_mode,

	.width_mm = 67,
	.height_mm = 145,

	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
	.reset_gpio_flags = GPIOD_OUT_HIGH,

	.init_cmds = lavender_tulip_init_cmds,
	.off_cmds = lavender_tulip_off_cmds,
};

static const struct nt36672a_panel_desc tianma_lavender_panel_desc = {
	.display_mode = &lavender_panel_default_mode,

	.width_mm = 67,
	.height_mm = 145,

	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
	.reset_gpio_flags = GPIOD_OUT_HIGH,

	.init_cmds = lavender_tulip_init_cmds,
	.off_cmds = lavender_tulip_off_cmds,
};

static const struct drm_display_mode tianma_jasmine_panel_default_mode = {
	.clock		= (1080 + 96 + 4 + 56) * (2160 + 4 + 2 + 33) * 60 / 1000,

	.hdisplay	= 1080,
	.hsync_start 	= 1080 + 96,
	.hsync_end	= 1080 + 96 + 4,
	.htotal		= 1080 + 96 + 4 + 56,

	.vdisplay	= 2160,
	.vsync_start	= 2160 + 4,
	.vsync_end	= 2160 + 4 + 2,
	.vtotal		= 2160 + 4 + 2 + 33,

	.width_mm = 68,
	.height_mm = 136,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct nt36672a_panel_desc tianma_jasmine_panel_desc = {
	.display_mode = &tianma_jasmine_panel_default_mode,

	.width_mm = 68,
	.height_mm = 136,

	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
	.reset_gpio_flags = GPIOD_OUT_HIGH,

	.init_cmds = tianma_jasmine_init_cmds,
	/* jasmine panel uses the same off sequence as lavender/tulip */
	.off_cmds = lavender_tulip_off_cmds,
};

static const struct drm_display_mode tianmaplus_e7t_tulip_mode = {
	.clock		= (1080 + 100 + 28 + 120) * (2280 + 10 + 3 + 8) * 60 / 1000,

	.hdisplay	= 1080,
	.hsync_start	= 1080 + 100,
	.hsync_end	= 1080 + 100 + 28,
	.htotal		= 1080 + 100 + 28 + 120,

	.vdisplay	= 2280,
	.vsync_start	= 2280 + 10,
	.vsync_end	= 2280 + 10 + 3,
	.vtotal		= 2280 + 10 + 3 + 8,

	.width_mm = 68,
	.height_mm = 143,

	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct nt36672a_panel_desc tianmaplus_e7t_tulip_panel_desc = {
	.display_mode = &tianmaplus_e7t_tulip_mode,

	.width_mm = 68,
	.height_mm = 143,

	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
	.reset_gpio_flags = GPIOD_OUT_HIGH,

	/* tulip panel uses almost the same init/off sequences as lavender */
	.init_cmds = lavender_tulip_init_cmds,
	.off_cmds = lavender_tulip_off_cmds,
};

static const struct drm_display_mode txd_x00td_mode = {
	.clock 		= (1080 + 122 + 8 + 76) * (2160 + 20 + 4 + 28) * 60 / 1000,

	.hdisplay	= 1080,
	.hsync_start	= 1080 + 122,
	.hsync_end	= 1080 + 122 + 8,
	.htotal		= 1080 + 122 + 8 + 76,

	.vdisplay	= 2160,
	.vsync_start	= 2160 + 20,
	.vsync_end	= 2160 + 20 + 4,
	.vtotal		= 2160 + 20 + 4 + 28,

	.width_mm	= 68,
	.height_mm	= 136,

	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct nt36672a_panel_desc txd_x00td_panel_desc = {
	.display_mode = &txd_x00td_mode,

	.width_mm = 68,
	.height_mm = 136,

	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
	.reset_gpio_flags = GPIOD_OUT_HIGH,

	.init_cmds = txd_x00td_init_cmds,
	.off_cmds = txd_x00td_off,
};

static int nt36672a_panel_add(struct nt36672a_panel *pinfo)
{
	struct device *dev = &pinfo->link->dev;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(pinfo->supplies); i++) {
		pinfo->supplies[i].supply = nt36672a_regulator_names[i];
		pinfo->supplies[i].init_load_uA = nt36672a_regulator_enable_loads[i];
	}

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(pinfo->supplies),
				      pinfo->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	pinfo->reset_gpio = devm_gpiod_get(dev, "reset", pinfo->desc->reset_gpio_flags);
	if (IS_ERR(pinfo->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(pinfo->reset_gpio),
				     "failed to get reset gpio from DT\n");

	pinfo->base.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&pinfo->base);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&pinfo->base);

	return 0;
}

static int nt36672a_panel_probe(struct mipi_dsi_device *dsi)
{
	struct nt36672a_panel *pinfo;
	const struct nt36672a_panel_desc *desc;
	int err;

	pinfo = devm_drm_panel_alloc(&dsi->dev, struct nt36672a_panel, base,
				     &panel_funcs, DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(pinfo))
		return PTR_ERR(pinfo);

	desc = of_device_get_match_data(&dsi->dev);
	dsi->mode_flags = desc->mode_flags;
	dsi->format = desc->format;
	dsi->lanes = desc->lanes;
	pinfo->desc = desc;
	pinfo->link = dsi;

	mipi_dsi_set_drvdata(dsi, pinfo);

	err = nt36672a_panel_add(pinfo);
	if (err < 0)
		return err;

	err = mipi_dsi_attach(dsi);
	if (err < 0) {
		drm_panel_remove(&pinfo->base);
		return err;
	}

	return 0;
}

static void nt36672a_panel_remove(struct mipi_dsi_device *dsi)
{
	struct nt36672a_panel *pinfo = mipi_dsi_get_drvdata(dsi);
	int err;

	err = mipi_dsi_detach(dsi);
	if (err < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", err);

	drm_panel_remove(&pinfo->base);
}

static const struct of_device_id panel_nt36672a_match[] = {
	{ .compatible = "shenchao,fhdplus-video", .data = &shenchao_lavender_panel_desc },
	{ .compatible = "tianma,fhd-video", .data = &tianma_fhd_video_panel_desc },
	{ .compatible = "tianma,tl060fvxs16-00", .data = &tianma_jasmine_panel_desc },
	{ .compatible = "tianma,tl063fvmca01-00", .data = &tianma_lavender_panel_desc },
	{ .compatible = "tianma,tl063fvmc43-02", .data = &tianmaplus_e7t_tulip_panel_desc },
	{ .compatible = "txd,txdi600yanpa-43v3", .data = &txd_x00td_panel_desc },
	{ },
};
MODULE_DEVICE_TABLE(of, panel_nt36672a_match);

static struct mipi_dsi_driver nt36672a_panel_driver = {
	.driver = {
		.name = "panel-novatek-nt36672a",
		.of_match_table = panel_nt36672a_match,
	},
	.probe = nt36672a_panel_probe,
	.remove = nt36672a_panel_remove,
};
module_mipi_dsi_driver(nt36672a_panel_driver);

MODULE_AUTHOR("Sumit Semwal <sumit.semwal@linaro.org>");
MODULE_DESCRIPTION("NOVATEK NT36672A based MIPI-DSI LCD panel driver");
MODULE_LICENSE("GPL");
