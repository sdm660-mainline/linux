// SPDX-License-Identifier: GPL-2.0-only
//
// Qualcomm PMIC VBUS output regulator driver
//
// Copyright (c) 2020, The Linux Foundation. All rights reserved.

#include <linux/module.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regmap.h>

#define CMD_OTG				0x40
#define OTG_EN				BIT(0)
#define OTG_CURRENT_LIMIT_CFG		0x52
#define OTG_CURRENT_LIMIT_MASK		GENMASK(2, 0)
#define OTG_CFG				0x53
#define OTG_EN_SRC_CFG			BIT(1)

static const unsigned int curr_table[] = {
	500000, 1000000, 1500000, 2000000, 2500000, 3000000,
};

static const unsigned int pm660_curr_table[] = {
	500000, 750000, 1000000, 1250000, 1500000,
};

struct qcom_usb_vbus_data {
	unsigned int cmd_offset;
	unsigned int current_limit_offset;
	unsigned int cfg_offset;
	const unsigned int *curr_table;
	unsigned int n_current_limits;
	bool disable_at_probe;
};

static const struct qcom_usb_vbus_data pm8150b_vbus_data = {
	.cmd_offset = CMD_OTG,
	.current_limit_offset = OTG_CURRENT_LIMIT_CFG,
	.cfg_offset = OTG_CFG,
	.curr_table = curr_table,
	.n_current_limits = ARRAY_SIZE(curr_table),
};

/* PM660's OTG command/config registers follow its DCDC peripheral. */
static const struct qcom_usb_vbus_data pm660_vbus_data = {
	.cmd_offset = 0x140,
	.current_limit_offset = 0x42,
	.cfg_offset = 0x153,
	.curr_table = pm660_curr_table,
	.n_current_limits = ARRAY_SIZE(pm660_curr_table),
	.disable_at_probe = true,
};

static const struct regulator_ops qcom_usb_vbus_reg_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_current_limit = regulator_get_current_limit_regmap,
	.set_current_limit = regulator_set_current_limit_regmap,
};

static const struct regulator_desc qcom_usb_vbus_rdesc = {
	.name = "usb_vbus",
	.ops = &qcom_usb_vbus_reg_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.curr_table = curr_table,
	.n_current_limits = ARRAY_SIZE(curr_table),
};

static int qcom_usb_vbus_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct qcom_usb_vbus_data *data;
	struct regulator_desc *rdesc;
	struct regulator_dev *rdev;
	struct regmap *regmap;
	struct regulator_config config = { };
	struct regulator_init_data *init_data;
	int ret;
	u32 base;

	data = device_get_match_data(dev);
	if (!data) {
		if (of_device_is_compatible(dev->of_node,
					    "qcom,pm660-vbus-reg"))
			data = &pm660_vbus_data;
		else if (of_device_is_compatible(dev->of_node,
						 "qcom,pm8150b-vbus-reg"))
			data = &pm8150b_vbus_data;
		else
			return dev_err_probe(dev, -EINVAL,
					     "missing VBUS register layout\n");
	}

	rdesc = devm_kmemdup(dev, &qcom_usb_vbus_rdesc, sizeof(*rdesc),
			      GFP_KERNEL);
	if (!rdesc)
		return -ENOMEM;

	rdesc->curr_table = data->curr_table;
	rdesc->n_current_limits = data->n_current_limits;

	ret = of_property_read_u32(dev->of_node, "reg", &base);
	if (ret < 0) {
		dev_err(dev, "no base address found\n");
		return ret;
	}

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap) {
		dev_err(dev, "Failed to get regmap\n");
		return -ENOENT;
	}

	init_data = of_get_regulator_init_data(dev, dev->of_node,
					       rdesc);
	if (!init_data)
		return -ENOMEM;

	rdesc->enable_reg = base + data->cmd_offset;
	rdesc->enable_mask = OTG_EN;
	rdesc->csel_reg = base + data->current_limit_offset;
	rdesc->csel_mask = OTG_CURRENT_LIMIT_MASK;
	config.dev = dev;
	config.init_data = init_data;
	config.of_node = dev->of_node;
	config.regmap = regmap;

	rdev = devm_regulator_register(dev, rdesc, &config);
	if (IS_ERR(rdev)) {
		ret = PTR_ERR(rdev);
		dev_err(dev, "not able to register vbus reg %d\n", ret);
		return ret;
	}

	/* Disable HW logic for VBUS enable */
	regmap_update_bits(regmap, base + data->cfg_offset,
			   OTG_EN_SRC_CFG, 0);
	if (data->disable_at_probe)
		regmap_update_bits(regmap, rdesc->enable_reg, OTG_EN, 0);

	return 0;
}

static const struct of_device_id qcom_usb_vbus_regulator_match[] = {
	{ .compatible = "qcom,pm660-vbus-reg", .data = &pm660_vbus_data },
	{ .compatible = "qcom,pm8150b-vbus-reg", .data = &pm8150b_vbus_data },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_usb_vbus_regulator_match);

static struct platform_driver qcom_usb_vbus_regulator_driver = {
	.driver		= {
		.name	= "qcom-usb-vbus-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = qcom_usb_vbus_regulator_match,
	},
	.probe		= qcom_usb_vbus_regulator_probe,
};
module_platform_driver(qcom_usb_vbus_regulator_driver);

MODULE_DESCRIPTION("Qualcomm USB vbus regulator driver");
MODULE_LICENSE("GPL v2");
