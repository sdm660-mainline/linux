// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm SMB1351 charger driver
 *
 * The SMB1351 is a switch-mode battery charger that some phones use in
 * parallel with the charger in their PMIC. On some of them, such as the
 * Vsmart Active 1, it is powered from VBUS only: without VBUS it does not
 * answer on I2C, and when VBUS returns it starts over from its OTP
 * configuration. This driver supports only that wiring and treats a chip
 * that answers as having VBUS. It keeps charging disabled, and reapplies
 * its configuration whenever the PMIC charger reports a change or a status
 * read finds the chip reset. Parallel charging, interrupts and OTG are not
 * supported yet.
 *
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2026 Adrian Nguyen <thenguyen1024@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

#define SMB1351_VFLOAT_REG		0x03
#define SMB1351_VFLOAT_MASK		GENMASK(5, 0)

#define SMB1351_CHG_PIN_EN_CTRL_REG	0x06
#define SMB1351_EN_PIN_CTRL_MASK	GENMASK(6, 5)
#define SMB1351_EN_BY_I2C_0_DISABLE	0x00

#define SMB1351_VERSION_REG		0x2e
#define SMB1351_VERSION_SMB1350		BIT(1)

#define SMB1351_CMD_I2C_REG		0x30
#define SMB1351_CMD_BQ_CFG_ACCESS	BIT(6)

#define SMB1351_CMD_CHG_REG		0x32
#define SMB1351_CMD_CHG_EN		BIT(1)

#define SMB1351_STATUS_4_REG		0x3a
#define SMB1351_STATUS_HOLD_OFF		BIT(3)
#define SMB1351_STATUS_CHG_MASK		GENMASK(2, 1)
#define SMB1351_STATUS_CHG_NONE		0
#define SMB1351_STATUS_CHG_PRE		1
#define SMB1351_STATUS_CHG_FAST		2
#define SMB1351_STATUS_CHG_TAPER	3

#define SMB1351_CHG_REVISION_REG	0x3f

#define SMB1351_VFLOAT_MIN_UV		3500000
#define SMB1351_VFLOAT_MAX_UV		4500000
#define SMB1351_VFLOAT_STEP_UV		20000

/* Let VBUS settle after the PMIC charger reports a change */
#define SMB1351_SETTLE_MS		300
/* Right after VBUS appears the chip may not answer yet */
#define SMB1351_RETRY_MS		200
#define SMB1351_RETRY_TIMEOUT_MS	2000

struct smb1351 {
	struct device *dev;
	struct regmap *regmap;
	struct power_supply *psy;
	struct delayed_work work;
	unsigned long retry_until;	/* jiffies, when to stop retrying */
	unsigned int vfloat_code;	/* set before the work is enabled */
	bool online;			/* only written by the work */
	bool identified;		/* only used by the work */
};

static const enum power_supply_property smb1351_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
};

static int smb1351_status(unsigned int stat)
{
	if (stat & SMB1351_STATUS_HOLD_OFF)
		return POWER_SUPPLY_STATUS_NOT_CHARGING;

	if (FIELD_GET(SMB1351_STATUS_CHG_MASK, stat) != SMB1351_STATUS_CHG_NONE)
		return POWER_SUPPLY_STATUS_CHARGING;

	return POWER_SUPPLY_STATUS_NOT_CHARGING;
}

static int smb1351_charge_type(unsigned int stat)
{
	switch (FIELD_GET(SMB1351_STATUS_CHG_MASK, stat)) {
	case SMB1351_STATUS_CHG_PRE:
		return POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
	case SMB1351_STATUS_CHG_FAST:
	case SMB1351_STATUS_CHG_TAPER:	/* constant voltage phase */
		return POWER_SUPPLY_CHARGE_TYPE_FAST;
	default:
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}
}

/*
 * Read the charger status, provided the chip is still configured. The driver
 * only ever selects the I2C enable control, so any other setting means that
 * the chip has been reset and is no longer kept from charging.
 */
static int smb1351_read_status(struct smb1351 *chip, unsigned int *stat)
{
	unsigned int en;
	int ret;

	ret = regmap_read(chip->regmap, SMB1351_CHG_PIN_EN_CTRL_REG, &en);
	if (ret)
		return ret;

	if ((en & SMB1351_EN_PIN_CTRL_MASK) != SMB1351_EN_BY_I2C_0_DISABLE)
		return -EIO;

	return regmap_read(chip->regmap, SMB1351_STATUS_4_REG, stat);
}

static int smb1351_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct smb1351 *chip = power_supply_get_drvdata(psy);
	bool online = READ_ONCE(chip->online);
	unsigned int stat, vfloat;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = online;
		return 0;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		/* -ENODATA leaves the property out of a uevent */
		if (!online)
			return -ENODATA;
		ret = regmap_read(chip->regmap, SMB1351_VFLOAT_REG, &vfloat);
		if (ret)
			return -ENODATA;
		val->intval = SMB1351_VFLOAT_MIN_UV +
			      FIELD_GET(SMB1351_VFLOAT_MASK, vfloat) *
			      SMB1351_VFLOAT_STEP_UV;
		return 0;
	case POWER_SUPPLY_PROP_STATUS:
		if (!online) {
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			return 0;
		}
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		if (!online) {
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
			return 0;
		}
		break;
	default:
		return -EINVAL;
	}

	/*
	 * The chip can lose VBUS, and with it its configuration, without the
	 * PMIC charger reporting a change. If it has been reset or does not
	 * answer, run the work again, unless a run with its own deadline is
	 * already pending. Report "Unknown" rather than an error, which would
	 * drop the whole uevent.
	 */
	ret = smb1351_read_status(chip, &stat);
	if (ret && !delayed_work_pending(&chip->work)) {
		WRITE_ONCE(chip->retry_until, jiffies +
			   msecs_to_jiffies(SMB1351_RETRY_TIMEOUT_MS));
		queue_delayed_work(system_freezable_wq, &chip->work, 0);
	}

	if (psp == POWER_SUPPLY_PROP_STATUS)
		val->intval = ret ? POWER_SUPPLY_STATUS_UNKNOWN :
				    smb1351_status(stat);
	else
		val->intval = ret ? POWER_SUPPLY_CHARGE_TYPE_UNKNOWN :
				    smb1351_charge_type(stat);

	return 0;
}

static int smb1351_configure(struct smb1351 *chip)
{
	struct regmap *map = chip->regmap;
	unsigned int cmd, en, vfloat;
	int ret;

	/* The configuration registers can only be written with this bit set */
	ret = regmap_set_bits(map, SMB1351_CMD_I2C_REG,
			      SMB1351_CMD_BQ_CFG_ACCESS);
	if (ret)
		return ret;

	/* Clear the command first, so that I2C control starts out disabled */
	ret = regmap_clear_bits(map, SMB1351_CMD_CHG_REG, SMB1351_CMD_CHG_EN);
	if (ret)
		return ret;

	ret = regmap_update_bits(map, SMB1351_CHG_PIN_EN_CTRL_REG,
				 SMB1351_EN_PIN_CTRL_MASK,
				 SMB1351_EN_BY_I2C_0_DISABLE);
	if (ret)
		return ret;

	ret = regmap_update_bits(map, SMB1351_VFLOAT_REG, SMB1351_VFLOAT_MASK,
				 FIELD_PREP(SMB1351_VFLOAT_MASK,
					    chip->vfloat_code));
	if (ret)
		return ret;

	ret = regmap_read(map, SMB1351_CMD_CHG_REG, &cmd);
	if (ret)
		return ret;
	ret = regmap_read(map, SMB1351_CHG_PIN_EN_CTRL_REG, &en);
	if (ret)
		return ret;
	ret = regmap_read(map, SMB1351_VFLOAT_REG, &vfloat);
	if (ret)
		return ret;

	if ((cmd & SMB1351_CMD_CHG_EN) ||
	    (en & SMB1351_EN_PIN_CTRL_MASK) != SMB1351_EN_BY_I2C_0_DISABLE ||
	    FIELD_GET(SMB1351_VFLOAT_MASK, vfloat) != chip->vfloat_code)
		return -EIO;

	return 0;
}

static void smb1351_work(struct work_struct *work)
{
	struct smb1351 *chip = container_of(to_delayed_work(work),
					    struct smb1351, work);
	unsigned int version;
	bool online;
	int ret;

	/* The chip only answers while it has VBUS */
	ret = regmap_read(chip->regmap, SMB1351_VERSION_REG, &version);
	online = !ret;

	if (online && !chip->identified) {
		bool smb1350 = version & SMB1351_VERSION_SMB1350;

		dev_info(chip->dev, "found %s (version %#04x)\n",
			 smb1350 ? "SMB1350" : "SMB1351", version);
		chip->identified = true;
	}

	if (online)
		ret = smb1351_configure(chip);

	if (ret && time_before(jiffies, READ_ONCE(chip->retry_until))) {
		queue_delayed_work(system_freezable_wq, &chip->work,
				   msecs_to_jiffies(SMB1351_RETRY_MS));
		return;
	}

	if (!online) {
		/* Without VBUS the chip does not acknowledge its address */
		if (ret != -ENXIO)
			dev_warn_ratelimited(chip->dev,
					     "failed to read version: %pe\n",
					     ERR_PTR(ret));
	} else if (ret) {
		dev_err_ratelimited(chip->dev,
				    "failed to disable charging: %pe\n",
				    ERR_PTR(ret));
	}

	if (online != chip->online) {
		dev_dbg(chip->dev, "VBUS %s\n", online ? "present" : "absent");
		WRITE_ONCE(chip->online, online);
		power_supply_changed(chip->psy);
	}
}

static void smb1351_external_power_changed(struct power_supply *psy)
{
	struct smb1351 *chip = power_supply_get_drvdata(psy);

	WRITE_ONCE(chip->retry_until,
		   jiffies + msecs_to_jiffies(SMB1351_SETTLE_MS +
					      SMB1351_RETRY_TIMEOUT_MS));
	mod_delayed_work(system_freezable_wq, &chip->work,
			 msecs_to_jiffies(SMB1351_SETTLE_MS));
}

static const struct power_supply_desc smb1351_psy_desc = {
	.name = "smb1351-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = smb1351_properties,
	.num_properties = ARRAY_SIZE(smb1351_properties),
	.get_property = smb1351_get_property,
	.external_power_changed = smb1351_external_power_changed,
};

static const struct regmap_config smb1351_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	/* 0x40-0x47 are interrupt status registers that clear on read */
	.max_register = SMB1351_CHG_REVISION_REG,
	/* The chip forgets its configuration whenever it loses VBUS */
	.cache_type = REGCACHE_NONE,
};

static void smb1351_disable_work(void *data)
{
	struct smb1351 *chip = data;

	disable_delayed_work_sync(&chip->work);
}

static int smb1351_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct power_supply_config psy_cfg = {};
	struct power_supply_battery_info *info;
	struct smb1351 *chip;
	int vfloat_uv;
	int ret;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = dev;

	chip->regmap = devm_regmap_init_i2c(client, &smb1351_regmap_config);
	if (IS_ERR(chip->regmap))
		return dev_err_probe(dev, PTR_ERR(chip->regmap),
				     "failed to initialize regmap\n");

	/*
	 * external_power_changed() can run as soon as the power supply is
	 * registered, before devm_power_supply_register() returns. Keep the
	 * work disabled until the rest of probe is done.
	 */
	INIT_DELAYED_WORK(&chip->work, smb1351_work);
	disable_delayed_work(&chip->work);

	psy_cfg.drv_data = chip;
	psy_cfg.fwnode = dev_fwnode(dev);

	chip->psy = devm_power_supply_register(dev, &smb1351_psy_desc,
					       &psy_cfg);
	if (IS_ERR(chip->psy))
		return dev_err_probe(dev, PTR_ERR(chip->psy),
				     "failed to register power supply\n");

	/*
	 * Added after the power supply, so this runs before the power supply
	 * is unregistered. Unlike devm_delayed_work_autocancel(), disabling
	 * the work also turns later requeues into no-ops.
	 */
	ret = devm_add_action_or_reset(dev, smb1351_disable_work, chip);
	if (ret)
		return ret;

	ret = power_supply_get_battery_info(chip->psy, &info);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get battery info\n");

	if (power_supply_battery_info_has_prop(info,
					       POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX))
		vfloat_uv = info->constant_charge_voltage_max_uv;
	else if (power_supply_battery_info_has_prop(info,
						    POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN))
		vfloat_uv = info->voltage_max_design_uv;
	else
		return dev_err_probe(dev, -EINVAL,
				     "battery has no maximum voltage\n");

	if (vfloat_uv < SMB1351_VFLOAT_MIN_UV ||
	    vfloat_uv > SMB1351_VFLOAT_MAX_UV)
		return dev_err_probe(dev, -EINVAL,
				     "unsupported float voltage %d uV\n",
				     vfloat_uv);

	chip->vfloat_code = (vfloat_uv - SMB1351_VFLOAT_MIN_UV) /
			    SMB1351_VFLOAT_STEP_UV;

	/* Leave the chip to the work, it does not answer without VBUS */
	WRITE_ONCE(chip->retry_until,
		   jiffies + msecs_to_jiffies(SMB1351_RETRY_TIMEOUT_MS));
	enable_delayed_work(&chip->work);
	mod_delayed_work(system_freezable_wq, &chip->work, 0);

	return 0;
}

static const struct of_device_id smb1351_of_match[] = {
	{ .compatible = "qcom,smb1351" },
	{ }
};
MODULE_DEVICE_TABLE(of, smb1351_of_match);

static const struct i2c_device_id smb1351_i2c_id[] = {
	{ .name = "smb1351" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, smb1351_i2c_id);

static struct i2c_driver smb1351_driver = {
	.driver = {
		.name = "qcom-smb1351",
		.of_match_table = smb1351_of_match,
	},
	.probe = smb1351_probe,
	.id_table = smb1351_i2c_id,
};
module_i2c_driver(smb1351_driver);

MODULE_AUTHOR("Adrian Nguyen <thenguyen1024@gmail.com>");
MODULE_DESCRIPTION("Qualcomm SMB1351 charger driver");
MODULE_LICENSE("GPL");
