// SPDX-License-Identifier: GPL-2.0-only
/*
 * This driver is solely based on the limited information in downstream code.
 * Any verification with schematics would be greatly appreciated.
 *
 * Copyright (c) 2023, Richard Acayan. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-lpass-lpi.h"

enum lpass_lpi_functions {
	LPI_MUX_comp_rx,
	LPI_MUX_dmic12,
	LPI_MUX_dmic34,
	LPI_MUX_mclk0,
	LPI_MUX_pdm_2_gpios,
	LPI_MUX_pdm_clk,
	LPI_MUX_pdm_rx,
	LPI_MUX_pdm_sync,

	LPI_MUX_gpio,
	LPI_MUX__,
};

static int gpio0_pins[] = { 0 };
static int gpio1_pins[] = { 1 };
static int gpio2_pins[] = { 2 };
static int gpio3_pins[] = { 3 };
static int gpio4_pins[] = { 4 };
static int gpio5_pins[] = { 5 };
static int gpio6_pins[] = { 6 };
static int gpio7_pins[] = { 7 };
static int gpio8_pins[] = { 8 };
static int gpio9_pins[] = { 9 };
static int gpio10_pins[] = { 10 };
static int gpio11_pins[] = { 11 };
static int gpio12_pins[] = { 12 };
static int gpio13_pins[] = { 13 };
static int gpio14_pins[] = { 14 };
static int gpio15_pins[] = { 15 };
static int gpio16_pins[] = { 16 };
static int gpio17_pins[] = { 17 };
static int gpio18_pins[] = { 18 };
static int gpio19_pins[] = { 19 };
static int gpio20_pins[] = { 20 };
static int gpio21_pins[] = { 21 };
static int gpio22_pins[] = { 22 };
static int gpio23_pins[] = { 23 };
static int gpio24_pins[] = { 24 };
static int gpio25_pins[] = { 25 };
static int gpio26_pins[] = { 26 };
static int gpio27_pins[] = { 27 };
static int gpio28_pins[] = { 28 };
static int gpio29_pins[] = { 29 };
static int gpio30_pins[] = { 30 };
static int gpio31_pins[] = { 31 };

static const struct pinctrl_pin_desc sdm660_lpi_pinctrl_pins[] = {
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),
	PINCTRL_PIN(8, "gpio8"),
	PINCTRL_PIN(9, "gpio9"),
	PINCTRL_PIN(10, "gpio10"),
	PINCTRL_PIN(11, "gpio11"),
	PINCTRL_PIN(12, "gpio12"),
	PINCTRL_PIN(13, "gpio13"),
	PINCTRL_PIN(14, "gpio14"),
	PINCTRL_PIN(15, "gpio15"),
	PINCTRL_PIN(16, "gpio16"),
	PINCTRL_PIN(17, "gpio17"),
	PINCTRL_PIN(18, "gpio18"),
	PINCTRL_PIN(19, "gpio19"),
	PINCTRL_PIN(20, "gpio20"),
	PINCTRL_PIN(21, "gpio21"),
	PINCTRL_PIN(22, "gpio22"),
	PINCTRL_PIN(23, "gpio23"),
	PINCTRL_PIN(24, "gpio24"),
	PINCTRL_PIN(25, "gpio25"),
	PINCTRL_PIN(26, "gpio26"),
	PINCTRL_PIN(27, "gpio27"),
	PINCTRL_PIN(28, "gpio28"),
	PINCTRL_PIN(29, "gpio29"),
	PINCTRL_PIN(30, "gpio30"),
	PINCTRL_PIN(31, "gpio31"),
};

static const char *comp_rx_groups[] = { "gpio22", "gpio24" };
static const char *dmic12_groups[] = { "gpio26", "gpio28" };
static const char *dmic34_groups[] = { "gpio27", "gpio29" };
static const char *mclk0_groups[] = { "gpio18" };
static const char *pdm_2_gpios_groups[] = { "gpio20" };
static const char *pdm_clk_groups[] = { "gpio18" };
static const char *pdm_rx_groups[] = { "gpio21", "gpio23", "gpio25" };
static const char *pdm_sync_groups[] = { "gpio19" };

const struct lpi_pingroup sdm660_lpi_pinctrl_groups[] = {
	LPI_PINGROUP(0, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(1, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(2, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(3, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(4, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(5, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(6, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(7, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(8, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(9, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(10, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(11, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(12, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(13, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(14, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(15, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(16, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(17, LPI_NO_SLEW, _, _, _, _),

	/* The function names of the PDM GPIOs are derived from SDM670 */
	LPI_PINGROUP(18, LPI_NO_SLEW, pdm_clk, mclk0, _, _),
	LPI_PINGROUP(19, LPI_NO_SLEW, pdm_sync, _, _, _),
	LPI_PINGROUP(20, LPI_NO_SLEW, pdm_2_gpios, _, _, _),
	LPI_PINGROUP(21, LPI_NO_SLEW, pdm_rx, _, _, _),
	LPI_PINGROUP(22, LPI_NO_SLEW, comp_rx, _, _, _),
	LPI_PINGROUP(23, LPI_NO_SLEW, pdm_rx, _, _, _),
	LPI_PINGROUP(24, LPI_NO_SLEW, comp_rx, _, _, _),
	LPI_PINGROUP(25, LPI_NO_SLEW, pdm_rx, _, _, _),
	LPI_PINGROUP(26, LPI_NO_SLEW, dmic12, _, _, _),
	LPI_PINGROUP(27, LPI_NO_SLEW, dmic34, _, _, _),
	LPI_PINGROUP(28, LPI_NO_SLEW, dmic12, _, _, _),
	LPI_PINGROUP(29, LPI_NO_SLEW, dmic34, _, _, _),

	LPI_PINGROUP(30, LPI_NO_SLEW, _, _, _, _),
	LPI_PINGROUP(31, LPI_NO_SLEW, _, _, _, _),
};

const struct lpi_function sdm660_lpi_pinctrl_functions[] = {
	LPI_FUNCTION(comp_rx),
	LPI_FUNCTION(dmic12),
	LPI_FUNCTION(dmic34),
	LPI_FUNCTION(mclk0),
	LPI_FUNCTION(pdm_2_gpios),
	LPI_FUNCTION(pdm_clk),
	LPI_FUNCTION(pdm_rx),
	LPI_FUNCTION(pdm_sync),
};

static const struct lpi_pinctrl_variant_data sdm660_lpi_pinctrl_data = {
	.pins = sdm660_lpi_pinctrl_pins,
	.npins = ARRAY_SIZE(sdm660_lpi_pinctrl_pins),
	.groups = sdm660_lpi_pinctrl_groups,
	.ngroups = ARRAY_SIZE(sdm660_lpi_pinctrl_groups),
	.functions = sdm660_lpi_pinctrl_functions,
	.nfunctions = ARRAY_SIZE(sdm660_lpi_pinctrl_functions),
};

static const struct of_device_id sdm660_lpi_pinctrl_of_match[] = {
	{
		.compatible = "qcom,sdm660-lpass-lpi-pinctrl",
		.data = &sdm660_lpi_pinctrl_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, sdm660_lpi_pinctrl_of_match);

static struct platform_driver sdm660_lpi_pinctrl_driver = {
	.driver = {
		.name = "qcom-sdm660-lpass-lpi-pinctrl",
		.of_match_table = sdm660_lpi_pinctrl_of_match,
	},
	.probe = lpi_pinctrl_probe,
	.remove = lpi_pinctrl_remove,
};
module_platform_driver(sdm660_lpi_pinctrl_driver);

MODULE_AUTHOR("Richard Acayan <mailingradian@gmail.com>");
MODULE_DESCRIPTION("QTI SDM660 LPI GPIO pin control driver");
MODULE_LICENSE("GPL");
