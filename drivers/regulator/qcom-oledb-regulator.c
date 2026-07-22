// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal Qualcomm QPNP OLEDB (AMOLED bias) regulator enable driver.
 * Enough for OPPO R11T / PM660L to re-enable OLEDB after bootloader
 * continuous splash leaves the rail off.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#define OLEDB_MODULE_RDY		0x45
#define OLEDB_MODULE_RDY_BIT		BIT(7)
#define OLEDB_MODULE_ENABLE		0x46
#define OLEDB_MODULE_ENABLE_BIT		BIT(7)
#define OLEDB_EXT_PIN_CTL		0x47
#define OLEDB_EXT_PIN_CTL_BIT		BIT(7)
#define OLEDB_SWIRE_CONTROL		0x48
#define OLEDB_EN_SWIRE_VOUT_UPD_BIT	BIT(6)
#define OLEDB_EN_SWIRE_PD_UPD_BIT	BIT(7)

struct qcom_oledb {
	struct device *dev;
	struct regmap *regmap;
	u32 base;
	bool swire_control;
	bool ext_pin_control;
	struct regulator_desc rdesc;
	struct regulator_dev *rdev;
};

static int oledb_write(struct qcom_oledb *oledb, u16 off, u8 val)
{
	return regmap_write(oledb->regmap, oledb->base + off, val);
}

static int oledb_update(struct qcom_oledb *oledb, u16 off, u8 mask, u8 val)
{
	return regmap_update_bits(oledb->regmap, oledb->base + off, mask, val);
}

static int oledb_enable(struct regulator_dev *rdev)
{
	struct qcom_oledb *oledb = rdev_get_drvdata(rdev);
	int ret;

	/* Module ready */
	ret = oledb_update(oledb, OLEDB_MODULE_RDY, OLEDB_MODULE_RDY_BIT,
			   OLEDB_MODULE_RDY_BIT);
	if (ret)
		return ret;

	if (oledb->ext_pin_control) {
		ret = oledb_update(oledb, OLEDB_EXT_PIN_CTL,
				   OLEDB_EXT_PIN_CTL_BIT, OLEDB_EXT_PIN_CTL_BIT);
		if (ret)
			return ret;
	}

	if (oledb->swire_control) {
		ret = oledb_update(oledb, OLEDB_SWIRE_CONTROL,
				   OLEDB_EN_SWIRE_VOUT_UPD_BIT |
				   OLEDB_EN_SWIRE_PD_UPD_BIT,
				   OLEDB_EN_SWIRE_VOUT_UPD_BIT |
				   OLEDB_EN_SWIRE_PD_UPD_BIT);
		if (ret)
			return ret;
	}

	ret = oledb_update(oledb, OLEDB_MODULE_ENABLE, OLEDB_MODULE_ENABLE_BIT,
			   OLEDB_MODULE_ENABLE_BIT);
	if (ret)
		return ret;

	dev_info(oledb->dev, "OLEDB enabled @0x%x\n", oledb->base);
	return 0;
}

static int oledb_disable(struct regulator_dev *rdev)
{
	struct qcom_oledb *oledb = rdev_get_drvdata(rdev);

	return oledb_update(oledb, OLEDB_MODULE_ENABLE, OLEDB_MODULE_ENABLE_BIT, 0);
}

static int oledb_is_enabled(struct regulator_dev *rdev)
{
	struct qcom_oledb *oledb = rdev_get_drvdata(rdev);
	unsigned int val;
	int ret;

	ret = regmap_read(oledb->regmap, oledb->base + OLEDB_MODULE_ENABLE, &val);
	if (ret)
		return ret;

	return !!(val & OLEDB_MODULE_ENABLE_BIT);
}

static const struct regulator_ops oledb_ops = {
	.enable = oledb_enable,
	.disable = oledb_disable,
	.is_enabled = oledb_is_enabled,
};

static int qcom_oledb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qcom_oledb *oledb;
	struct regulator_config cfg = {};
	struct regulator_init_data *init_data;
	int ret;

	oledb = devm_kzalloc(dev, sizeof(*oledb), GFP_KERNEL);
	if (!oledb)
		return -ENOMEM;

	oledb->dev = dev;
	oledb->regmap = dev_get_regmap(dev->parent, NULL);
	if (!oledb->regmap)
		return -ENODEV;

	ret = of_property_read_u32(dev->of_node, "reg", &oledb->base);
	if (ret)
		return ret;

	oledb->swire_control = of_property_read_bool(dev->of_node, "qcom,swire-control");
	oledb->ext_pin_control = of_property_read_bool(dev->of_node, "qcom,ext-pin-control");

	init_data = of_get_regulator_init_data(dev, dev->of_node, &oledb->rdesc);
	if (!init_data)
		return -ENOMEM;

	oledb->rdesc.name = init_data->constraints.name ?: "oledb";
	oledb->rdesc.type = REGULATOR_VOLTAGE;
	oledb->rdesc.owner = THIS_MODULE;
	oledb->rdesc.ops = &oledb_ops;
	/* Fixed-ish AMOLED bias rail; actual Vout may be SWIRE programmed. */
	oledb->rdesc.n_voltages = 1;
	oledb->rdesc.fixed_uV = 6400000;

	cfg.dev = dev;
	cfg.init_data = init_data;
	cfg.driver_data = oledb;
	cfg.of_node = dev->of_node;

	oledb->rdev = devm_regulator_register(dev, &oledb->rdesc, &cfg);
	if (IS_ERR(oledb->rdev))
		return PTR_ERR(oledb->rdev);

	platform_set_drvdata(pdev, oledb);
	return 0;
}

static const struct of_device_id qcom_oledb_match[] = {
	{ .compatible = "qcom,qpnp-oledb-regulator" },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_oledb_match);

static struct platform_driver qcom_oledb_driver = {
	.probe = qcom_oledb_probe,
	.driver = {
		.name = "qcom-oledb-regulator",
		.of_match_table = qcom_oledb_match,
	},
};
module_platform_driver(qcom_oledb_driver);

MODULE_DESCRIPTION("Qualcomm QPNP OLEDB regulator (minimal enable)");
MODULE_LICENSE("GPL");
