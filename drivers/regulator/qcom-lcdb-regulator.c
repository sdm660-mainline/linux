// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020, The Linux Foundation. All rights reserved.

#include <linux/linear_range.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define PM660L_LCDB_BASE		0xec00

#define LCDB_STS1_REG			0x08
#define INT_RT_STATUS_REG		0x10
 #define VREG_OK_RT_STS_BIT		BIT(0)
 #define SC_ERROR_RT_STS_BIT		BIT(1)

#define LCDB_MODULE_RDY_REG		0x45
 #define MODULE_RDY_BIT			BIT(7)	/* 0x80 */
#define LCDB_ENABLE_CTL1_REG		0x46
 #define MODULE_EN_BIT			BIT(7)	/* 0x80 */

#define LCDB_BST_OUTPUT_VOLTAGE_REG	0x41
#define LCDB_LDO_OUTPUT_VOLTAGE_REG	0x71
#define LCDB_NCP_OUTPUT_VOLTAGE_REG	0x81
 #define LCDB_SET_VOLTAGE_MASK		GENMASK(4, 0)	/* 0x1f */


enum lcdb_regulator_type {
	LCDB_BOOST,
	LCDB_LDO,
	LCDB_NCP
};

/* Holds data about each individual regulator - either LDO, or NCP */
struct lcdb_regulator {
	struct regulator_desc		desc;
	struct device			*dev;
	struct regmap			*regmap;
	struct regulator_dev		*rdev;
	struct delayed_work		sc_recovery_work;
	enum lcdb_regulator_type	type;
	u16				base;
	int				sc_irq;
	int				sc_count;
};

/* something to match on specific DT compatible */
struct lcdb_regulator_data {
	const char			*name; /* child subnode name in DT */
	enum lcdb_regulator_type	type;
	u16				base;
	const struct regulator_desc	*desc;
};

/* These are absolute minimum/maximum values for voltages */
#define LCDB_MIN_VOLTAGE_UV			4000000
#define LCDB_MAX_VOLTAGE_UV			6000000
#define LCDB_MIN_BST_VOLTAGE_UV			4700000
#define LCDB_MAX_BST_VOLTAGE_UV			6250000
/* Additional "space" to reserve on top of requested voltage for BOOST regulator */
#define LCDB_BOOST_HEADROOM_UV			100000

/*
 * There are 2 ranges for LCDB regulator:
 * - 4000mV to 4900mV is in 100mV steps (10 steps, reg selectors from  0 to 9)
 * - 4950mV to 6000mV is in 50mV steps  (21 steps, reg selectors from 10 to 20)
 * Together they form one continuous range, but it is not a single linear range.
 * Values below are expressed in microvolts, although named as millivolts.
 */
#define VOLTAGE_MIN_100_MV			LCDB_MIN_VOLTAGE_UV
#define VOLTAGE_MIN_50_MV			4950000
#define VOLTAGE_STEP_100_MV			100000
#define VOLTAGE_STEP_50_MV			50000
#define VOLTAGE_SEL_50MV_FIRST			10
#define VOLTAGE_SEL_50MV_LAST			20

struct linear_range pm660l_lcdb_linear_ranges[] = {
	/* { min, min_sel, max_sel, step } */
	LINEAR_RANGE(VOLTAGE_MIN_100_MV, 0, VOLTAGE_SEL_50MV_FIRST - 1, VOLTAGE_STEP_100_MV),
	LINEAR_RANGE(VOLTAGE_MIN_50_MV, VOLTAGE_SEL_50MV_FIRST, VOLTAGE_SEL_50MV_LAST, VOLTAGE_STEP_50_MV),
};

/* For BOOST, one linear range: 32 steps of 50mV  [4700mV .. 6250mV] */
struct linear_range pm660_lcdb_boost_ranges[] = {
	LINEAR_RANGE(LCDB_MIN_BST_VOLTAGE_UV, 0, 31, VOLTAGE_STEP_50_MV)
};

#ifdef DEBUG
static const char *lcdbtype(struct lcdb_regulator *lcdb_vreg)
{
	switch (lcdb_vreg->type) 
	{
		case LCDB_BOOST: return "BOOST"; break;
		case LCDB_LDO: return "LDO"; break;
		case LCDB_NCP: return "NCP"; break;
		default: return "(unk)"; break;
	}
}

static int dbg_regulator_enable(struct regulator_dev *rdev)
{
	struct lcdb_regulator *lcdb_vreg = rdev_get_drvdata(rdev);
	dev_info(&rdev->dev, "%s: Request to enable\n", lcdbtype(lcdb_vreg));
	regulator_enable_regmap(rdev);
	return 0;
}

static int dbg_regulator_disable(struct regulator_dev *rdev)
{
	struct lcdb_regulator *lcdb_vreg = rdev_get_drvdata(rdev);
	dev_info(&rdev->dev, "%s: Request to disable\n", lcdbtype(lcdb_vreg));
	regulator_disable_regmap(rdev);
	return 0;
}

static int dbg_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct lcdb_regulator *lcdb_vreg = rdev_get_drvdata(rdev);
	int is_enabled = regulator_is_enabled_regmap(rdev); // call real function
	dev_info(&rdev->dev, "is_enabled %s: %d\n", lcdbtype(lcdb_vreg), is_enabled);
	return is_enabled;
}

static int dbg_regulator_map_voltage_linear_range(struct regulator_dev *rdev, int min_uV, int max_uV)
{
	struct lcdb_regulator *lcdb_vreg = rdev_get_drvdata(rdev);
	int selector = regulator_map_voltage_linear_range(rdev, min_uV, max_uV); // call real function
	dev_info(&rdev->dev, "%s: map selector for voltage: %d..%d: 0x%x\n",
		lcdbtype(lcdb_vreg), min_uV, max_uV, selector);
	return selector;
}

static int dbg_regulator_list_voltage_linear_range(struct regulator_dev *rdev, unsigned int selector)
{
	struct lcdb_regulator *lcdb_vreg = rdev_get_drvdata(rdev);
	int voltage = regulator_list_voltage_linear_range(rdev, selector); // call real function
	dev_info(&rdev->dev, "%s: list voltage for selector 0x%x: %d\n",
		lcdbtype(lcdb_vreg), selector, voltage);
	return voltage;
}

static int dbg_regulator_set_voltage_sel_regmap(struct regulator_dev *rdev, unsigned int sel)
{
	struct lcdb_regulator *lcdb_vreg = rdev_get_drvdata(rdev);
	dev_info(&rdev->dev, "%s: SET voltage sel: 0x%x\n", lcdbtype(lcdb_vreg), sel);
	//ret = regulator_set_voltage_sel_regmap(rdev, sel); // do not actually call that
	return 0;
}

static int dbg_regulator_get_voltage_sel_regmap(struct regulator_dev *rdev)
{
	struct lcdb_regulator *lcdb_vreg = rdev_get_drvdata(rdev);
	int sel = regulator_get_voltage_sel_regmap(rdev); // call original getter
	dev_info(&rdev->dev, "%s: GET voltage sel: 0x%x\n", lcdbtype(lcdb_vreg), sel);
	return sel;
}
#endif

/*
 * We could just use standard regulator regmap helpers for this,
 * if not one thing: in downstream driver whenever LDO/NCP voltage is set,
 * BOOST regulator voltage is also configured as +100mV from requested
 * value. This is called BOOST_HEADROOM and vendor driver also supports
 * configuring it from device tree using "qcom,bst-headroom-mv" property,
 * but it was never used in practice in vendor device trees and default
 * is assumed (which is 100 mV).
 */
static int pm660l_lcdb_set_voltage(struct regulator_dev *rdev, int min_uV,
				   int max_uV, unsigned *selector)
{
	int sel, voltage, ret;
	bool found;
	//struct lcdb_regulator *lcdb = rdev_get_drvdata(rdev);
	//dev_info(&rdev->dev, "%s: set_voltage [%d..%d]", lcdbtype(lcdb), min_uV, max_uV);

	if ((min_uV < LCDB_MIN_VOLTAGE_UV) || (max_uV > LCDB_MAX_VOLTAGE_UV))
		return -ERANGE; /* or maybe -EINVAL? */

	/* Use standard helpers to calculate voltage selector for LDO/NCP */
	sel = regulator_map_voltage_linear_range(rdev, min_uV, max_uV);
	if (sel < 0)
		return sel;
	/* Use standard helper to set the selector */
	ret = regulator_set_voltage_sel_regmap(rdev, sel);

	/* query currently selected voltage */
	voltage = regulator_list_voltage_linear_range(rdev, sel);
	if (voltage < 0)
		return voltage;

	/* Formula from vendor driver: Vout_Boost = headroom + max(Vout_LDO, abs(Vout_NCP)) */
	voltage += LCDB_BOOST_HEADROOM_UV;

	/* check if it fits the constraints for BOOST */
	if ((voltage >= LCDB_MIN_BST_VOLTAGE_UV) && (voltage <= LCDB_MAX_BST_VOLTAGE_UV)) {
		/* find selector value from range */
		ret = linear_range_get_selector_high_array(pm660_lcdb_boost_ranges,
			ARRAY_SIZE(pm660_lcdb_boost_ranges), voltage, &sel, &found);
		if (found) {
			//dev_info(&rdev->dev, "%s: will set BOOST to %d uV, sel=%d", lcdbtype(lcdb), voltage, sel);
			regmap_update_bits(rdev->regmap,
			 	PM660L_LCDB_BASE + LCDB_BST_OUTPUT_VOLTAGE_REG,
				LCDB_SET_VOLTAGE_MASK, sel);
		}
	}

	return ret;
}

#ifdef DEBUG
/* same ops as regular ones but are read-only and do logging */
static const struct regulator_ops pm660l_lcdb_debug_ops = {
	.enable			= dbg_regulator_enable,
	.disable		= dbg_regulator_disable,
	.is_enabled		= dbg_regulator_is_enabled,
	.set_voltage		= pm660l_lcdb_set_voltage,
	.get_voltage_sel	= dbg_regulator_get_voltage_sel_regmap,
	.list_voltage		= dbg_regulator_list_voltage_linear_range,
	.map_voltage		= dbg_regulator_map_voltage_linear_range,
};
#endif

static const struct regulator_ops pm660l_lcdb_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_voltage		= pm660l_lcdb_set_voltage,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
};

static const struct regulator_desc pm660l_ldo_desc = {
	.enable_reg		= (PM660L_LCDB_BASE + LCDB_ENABLE_CTL1_REG),
	.enable_val		= MODULE_EN_BIT,
	.enable_mask		= MODULE_EN_BIT,
	.disable_val		= 0x00,
	.vsel_reg		= (PM660L_LCDB_BASE + LCDB_LDO_OUTPUT_VOLTAGE_REG),
	.vsel_mask		= LCDB_SET_VOLTAGE_MASK,
	.owner			= THIS_MODULE,
	.type			= REGULATOR_VOLTAGE,
	.linear_ranges		= pm660l_lcdb_linear_ranges,
	.n_linear_ranges	= ARRAY_SIZE(pm660l_lcdb_linear_ranges),
#ifdef DEBUG
	.ops			= &pm660l_lcdb_debug_ops,
#else
	.ops			= &pm660l_lcdb_ops,
#endif
};

static const struct regulator_desc pm660l_ncp_desc = {
	/* enable_reg is the same as for LDO, no separate one for NCP */
	.enable_reg		= (PM660L_LCDB_BASE + LCDB_ENABLE_CTL1_REG),
	.enable_val		= MODULE_EN_BIT,
	.enable_mask		= MODULE_EN_BIT,
	.disable_val		= 0x00,
	.vsel_reg		= (PM660L_LCDB_BASE + LCDB_NCP_OUTPUT_VOLTAGE_REG),
	.vsel_mask		= LCDB_SET_VOLTAGE_MASK,
	.owner			= THIS_MODULE,
	.type			= REGULATOR_VOLTAGE,
	.linear_ranges		= pm660l_lcdb_linear_ranges,
	.n_linear_ranges	= ARRAY_SIZE(pm660l_lcdb_linear_ranges),
#ifdef DEBUG
	.ops			= &pm660l_lcdb_debug_ops,
#else
	.ops			= &pm660l_lcdb_ops,
#endif
};

static const struct lcdb_regulator_data pm660l_lcdb_data[] = {
	{"ldo", LCDB_LDO, PM660L_LCDB_BASE, &pm660l_ldo_desc},
	{"ncp", LCDB_NCP, PM660L_LCDB_BASE, &pm660l_ncp_desc},
	{ },
};

static const struct of_device_id qcom_lcdb_match[] = {
	{ .compatible = "qcom,pm660l-lcdb", .data = &pm660l_lcdb_data},
	{ },
};
MODULE_DEVICE_TABLE(of, qcom_lcdb_match);

static int qcom_lcdb_regulator_probe(struct platform_device *pdev)
{
	struct lcdb_regulator *lcdb_vreg;
	struct device *dev = &pdev->dev;
	struct regulator_config cfg = {};
	const struct lcdb_regulator_data *reg_data;
	struct regmap *reg_regmap;
	//int ret;

	reg_regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!reg_regmap) {
		dev_err(&pdev->dev, "Couldn't get parent's regmap\n");
		return -ENODEV;
	}

	reg_data = device_get_match_data(&pdev->dev);
	if (!reg_data)
		return -ENODEV;

	for (; reg_data->name; reg_data++) {
		char *sc_irq_name;
		int irq = 0;

		lcdb_vreg  = devm_kzalloc(&pdev->dev, sizeof(*lcdb_vreg),
					   GFP_KERNEL);
		if (!lcdb_vreg)
			return -ENOMEM;

		sc_irq_name = devm_kasprintf(dev, GFP_KERNEL,
					     "lcdb-%s-sc", reg_data->name);
		if (!sc_irq_name)
			return -ENOMEM;

		/* The Short Circuit interrupt is critical */
		irq = of_irq_get_byname(pdev->dev.of_node, "sc-irq");
		if (irq <= 0) {
			if (irq == 0)
				irq = -EINVAL;
			return dev_err_probe(lcdb_vreg->dev, irq,
					     "Short-circuit irq not found.\n");
		}
		lcdb_vreg->sc_irq = irq;
		/*
			// for labibb 2, inside each subnode
			interrupts = <0x3 0xdc 0x2 IRQ_TYPE_EDGE_RISING>,
				     <0x3 0xdc 0x0 IRQ_TYPE_LEVEL_HIGH>;
			interrupt-names = "sc-err", "ocp";

			// for lcdb downstream one in "root" lcdb node, not in subnodes
			interrupts = <0x3 0xec 0x1 IRQ_TYPE_EDGE_RISING>;
			interrupt-names = "sc-irq";
		*/

		lcdb_vreg->regmap = reg_regmap;
		lcdb_vreg->dev = dev;
		lcdb_vreg->base = reg_data->base;
		lcdb_vreg->type = reg_data->type;
		//INIT_DELAYED_WORK(&lcdb_vreg->sc_recovery_work,
		//		  qcom_lcdb_sc_recovery_worker);

		memcpy(&lcdb_vreg->desc, reg_data->desc, sizeof(lcdb_vreg->desc));
		lcdb_vreg->desc.of_match = reg_data->name;
		lcdb_vreg->desc.name = reg_data->name;

		cfg.dev = lcdb_vreg->dev;
		cfg.driver_data = lcdb_vreg;
		cfg.regmap = lcdb_vreg->regmap;

		lcdb_vreg->rdev = devm_regulator_register(lcdb_vreg->dev,
			&lcdb_vreg->desc, &cfg);

		if (IS_ERR(lcdb_vreg->rdev)) {
			dev_err(dev, "qcom_lcdb: error registering %s : %ld\n",
					reg_data->name, PTR_ERR(lcdb_vreg->rdev));
			return PTR_ERR(lcdb_vreg->rdev);
		}

		//ret = devm_request_threaded_irq(lcdb_vreg->dev, lcdb_vreg->sc_irq,
		// 				NULL, qcom_lcdb_sc_isr,
		//				IRQF_ONESHOT | IRQF_TRIGGER_RISING,
		//				sc_irq_name, lcdb_vreg);
		//if (ret)
		//	return ret;
	}

	return 0;
}

static struct platform_driver qcom_lcdb_regulator_driver = {
	.driver	= {
		.name = "qcom-lcdb-regulator",
		.of_match_table	= qcom_lcdb_match,
	},
	.probe = qcom_lcdb_regulator_probe,
};
module_platform_driver(qcom_lcdb_regulator_driver);

MODULE_DESCRIPTION("Qualcomm LCDB driver");
MODULE_AUTHOR("Anirudh Ghayal <aghayal@codeaurora.org>");
MODULE_AUTHOR("Alexey Minnekhanov <alexeymin@minlexx.ru>");
MODULE_LICENSE("GPL v2");
