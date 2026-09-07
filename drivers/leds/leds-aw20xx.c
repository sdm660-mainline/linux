// SPDX-License-Identifier: GPL-2.0+
// Driver for Awinic AW20XX 3-channel LED drivers

#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regmap.h>

#define AW20XX_MAX_LEDS 3

/* Reset and ID register */
#define AW20XX_RSTR 0x00
#define AW20XX_RSTR_RESET 0x55
#define AW2013_RSTR_CHIP_ID 0x33
#define AW2027_RSTR_CHIP_ID 0x09

/* Global control register */
#define AW20XX_GCR 0x01
#define AW20XX_GCR_ENABLE BIT(0)

#define AW20XX_GCR2 0x04
#define AW20XX_IMAX_MASK (BIT(0) | BIT(1)) // Should be 0-3

/* LED channel enable register */
#define AW20XX_LCTR 0x30
#define AW20XX_LCTR_LE(x) BIT((x))

/* LED channel control registers */
#define AW20XX_LCFG(x) (0x31 + (x))
#define AW20XX_LCFG_CUR_MASK GENMASK(3, 0)
#define AW20XX_LCFG_MD BIT(4)
#define AW20XX_LCFG_FI BIT(5)
#define AW20XX_LCFG_FO BIT(6)

#define AW2027_IMAX_15MA (0)
#define AW2027_IMAX_30MA (1)
#define AW2027_IMAX_5MA  (2)
#define AW2027_IMAX_10MA (3)

/* LED channel PWM registers */
#define AW20XX_REG_PWM(x) (0x34 + (x))

/* LED channel timing registers */
#define AW20XX_LEDT0(x) (0x37 + (x) * 3)
#define AW20XX_LEDT0_T1(x) ((x) << 4) // Should be 0-7
#define AW20XX_LEDT0_T2(x) (x) // Should be 0-5

#define AW20XX_LEDT1(x) (0x38 + (x) * 3)
#define AW20XX_LEDT1_T3(x) ((x) << 4) // Should be 0-7
#define AW20XX_LEDT1_T4(x) (x) // Should be 0-7

#define AW20XX_LEDT2(x) (0x39 + (x) * 3)
#define AW20XX_LEDT2_T0(x) ((x) << 4) // Should be 0-8
#define AW20XX_LEDT2_REPEAT(x) (x) // Should be 0-15

#define AW2013_REG_MAX 0x77
#define AW2027_REG_MAX 0x7F /* copied from vendor driver, but only up to 0x3F is documented */

#define AW20XX_TIME_STEP 130 /* ms */

struct aw20xx;

struct aw20xx_led {
	struct aw20xx *chip;
	struct led_classdev cdev;
	u32 num;
	unsigned int imax;
};

struct aw20xx_chipdef {
	u8 chip_id;
	const struct regmap_config regmap_cfg;
	u32 default_imax;
	bool gcr2_imax;
	unsigned int current_levels;
	unsigned int current_max;
};

struct aw20xx {
	struct mutex mutex; /* held when writing to registers */
	struct regulator_bulk_data regulators[2];
	struct regmap *regmap;
	struct i2c_client *client;
	struct aw20xx_led leds[AW20XX_MAX_LEDS];
	const struct aw20xx_chipdef *cdef;
	int num_leds;
	bool enabled;
};

static const struct regmap_config aw2013_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = AW2013_REG_MAX,
};

static const struct regmap_config aw2027_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = AW2027_REG_MAX,
};

/* finds the closest current step to the given microamp
 * given the max current and number of levels
 */
inline u32 aw20xx_microamp_to_imax(u32 microamp, const struct aw20xx_chipdef *cdef)
{
	return min_t(u32, ((microamp * (cdef->current_levels - 1))
			  + (cdef->current_max / 2)) / cdef->current_max,
		     cdef->current_levels - 1);
}

static const struct aw20xx_chipdef aw2013_chipdef = {
	.chip_id = AW2013_RSTR_CHIP_ID,
	.regmap_cfg = aw2013_regmap_config,
	.default_imax = 0b01, // 5mA
	.gcr2_imax = false,
	.current_levels = 4,
	.current_max = 15000,
};

static const struct aw20xx_chipdef aw2027_chipdef = {
	.chip_id = AW2027_RSTR_CHIP_ID,
	.regmap_cfg = aw2027_regmap_config,
	.default_imax = AW2027_IMAX_15MA,
	.gcr2_imax = true,
	.current_levels = 16,
	.current_max = 30000,
};

static int aw20xx_chip_init(struct aw20xx *chip)
{
	int i, ret;

	ret = regmap_write(chip->regmap, AW20XX_GCR, AW20XX_GCR_ENABLE);
	if (ret) {
		dev_err(&chip->client->dev, "Failed to enable the chip: %d\n",
			ret);
		return ret;
	}

	if (chip->cdef->gcr2_imax) {
		/* AW2027 supports 4-step global imax, and also a 16-step control for limiting
		 * individual current per LED. This doesn't map to the single value the
		 * devicetree provides super well, so set global imax  to maximum, and
		 * local imax to whatever gets us closest to the value in the devicetree
		 */
		ret = regmap_update_bits(chip->regmap,
				AW20XX_GCR2,
				AW20XX_IMAX_MASK,
				AW2027_IMAX_30MA);

		for (i = 0; i < chip->num_leds; i++) {
			ret = regmap_update_bits(chip->regmap,
					AW20XX_LCFG(chip->leds[i].num),
					AW20XX_LCFG_CUR_MASK,
					chip->leds[i].imax);
			if (ret) {
				dev_err(&chip->client->dev,
					"Failed to set maximum current for led %d: %d\n",
					chip->leds[i].num, ret);
				return ret;
			}
		}
	} else {
		/* AW2013 only supports 4-step individual current per LED */
		for (i = 0; i < chip->num_leds; i++) {
			ret = regmap_update_bits(chip->regmap,
					AW20XX_LCFG(chip->leds[i].num),
					AW20XX_IMAX_MASK,
					chip->leds[i].imax);
			if (ret) {
				dev_err(&chip->client->dev,
					"Failed to set maximum current for led %d: %d\n",
					chip->leds[i].num, ret);
				return ret;
			}
		}
	}

	return ret;
}

static void aw20xx_chip_disable(struct aw20xx *chip)
{
	int ret;

	if (!chip->enabled)
		return;

	regmap_write(chip->regmap, AW20XX_GCR, 0);

	ret = regulator_bulk_disable(ARRAY_SIZE(chip->regulators),
					 chip->regulators);
	if (ret) {
		dev_err(&chip->client->dev,
			"Failed to disable regulators: %d\n", ret);
		return;
	}

	chip->enabled = false;
}

static int aw20xx_chip_enable(struct aw20xx *chip)
{
	int ret;

	if (chip->enabled)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(chip->regulators),
					chip->regulators);
	if (ret) {
		dev_err(&chip->client->dev,
			"Failed to enable regulators: %d\n", ret);
		return ret;
	}
	chip->enabled = true;

	ret = aw20xx_chip_init(chip);
	if (ret)
		aw20xx_chip_disable(chip);

	return ret;
}

static bool aw20xx_chip_in_use(struct aw20xx *chip)
{
	int i;

	for (i = 0; i < chip->num_leds; i++)
		if (chip->leds[i].cdev.brightness)
			return true;

	return false;
}

static int aw20xx_brightness_set(struct led_classdev *cdev,
				 enum led_brightness brightness)
{
	struct aw20xx_led *led = container_of(cdev, struct aw20xx_led, cdev);
	int ret, num;

	mutex_lock(&led->chip->mutex);

	if (aw20xx_chip_in_use(led->chip)) {
		ret = aw20xx_chip_enable(led->chip);
		if (ret)
			goto error;
	}

	num = led->num;

	ret = regmap_write(led->chip->regmap, AW20XX_REG_PWM(num), brightness);
	if (ret)
		goto error;

	if (brightness) {
		ret = regmap_update_bits(led->chip->regmap, AW20XX_LCTR,
					 AW20XX_LCTR_LE(num), 0xFF);
	} else {
		ret = regmap_update_bits(led->chip->regmap, AW20XX_LCTR,
					 AW20XX_LCTR_LE(num), 0);
		if (ret)
			goto error;
		ret = regmap_update_bits(led->chip->regmap, AW20XX_LCFG(num),
					 AW20XX_LCFG_MD, 0);
	}
	if (ret)
		goto error;

	if (!aw20xx_chip_in_use(led->chip))
		aw20xx_chip_disable(led->chip);

error:
	mutex_unlock(&led->chip->mutex);

	return ret;
}

static int aw20xx_blink_set(struct led_classdev *cdev,
				unsigned long *delay_on, unsigned long *delay_off)
{
	struct aw20xx_led *led = container_of(cdev, struct aw20xx_led, cdev);
	int ret, num = led->num;
	unsigned long off = 0, on = 0;

	/* If no blink specified, default to 1 Hz. */
	if (!*delay_off && !*delay_on) {
		*delay_off = 500;
		*delay_on = 500;
	}

	if (!led->cdev.brightness) {
		led->cdev.brightness = LED_FULL;
		ret = aw20xx_brightness_set(&led->cdev, led->cdev.brightness);
		if (ret)
			return ret;
	}

	/* Never on - just set to off */
	if (!*delay_on) {
		led->cdev.brightness = LED_OFF;
		return aw20xx_brightness_set(&led->cdev, LED_OFF);
	}

	mutex_lock(&led->chip->mutex);

	/* Never off - brightness is already set, disable blinking */
	if (!*delay_off) {
		ret = regmap_update_bits(led->chip->regmap, AW20XX_LCFG(num),
					 AW20XX_LCFG_MD, 0);
		goto out;
	}

	/* Convert into values the HW will understand. */
	off = min(5, ilog2((*delay_off - 1) / AW20XX_TIME_STEP) + 1);
	on = min(7, ilog2((*delay_on - 1) / AW20XX_TIME_STEP) + 1);

	*delay_off = BIT(off) * AW20XX_TIME_STEP;
	*delay_on = BIT(on) * AW20XX_TIME_STEP;

	/* Set timings */
	ret = regmap_write(led->chip->regmap,
			   AW20XX_LEDT0(num), AW20XX_LEDT0_T2(on));
	if (ret)
		goto out;
	ret = regmap_write(led->chip->regmap,
			   AW20XX_LEDT1(num), AW20XX_LEDT1_T4(off));
	if (ret)
		goto out;

	/* Finally, enable the LED */
	ret = regmap_update_bits(led->chip->regmap, AW20XX_LCFG(num),
				 AW20XX_LCFG_MD, 0xFF);
	if (ret)
		goto out;

	ret = regmap_update_bits(led->chip->regmap, AW20XX_LCTR,
				 AW20XX_LCTR_LE(num), 0xFF);

out:
	mutex_unlock(&led->chip->mutex);

	return ret;
}

static int aw20xx_probe_dt(struct aw20xx *chip)
{
	struct device_node *np = dev_of_node(&chip->client->dev);
	int count, ret = 0, i = 0;
	struct aw20xx_led *led;

	count = of_get_available_child_count(np);
	if (!count || count > AW20XX_MAX_LEDS)
		return -EINVAL;

	regmap_write(chip->regmap, AW20XX_RSTR, AW20XX_RSTR_RESET);

	for_each_available_child_of_node_scoped(np, child) {
		struct led_init_data init_data = {};
		u32 source;
		u32 imax;

		ret = of_property_read_u32(child, "reg", &source);
		if (ret != 0 || source >= AW20XX_MAX_LEDS) {
			dev_err(&chip->client->dev,
				"Couldn't read LED address: %d\n", ret);
			count--;
			continue;
		}

		led = &chip->leds[i];
		led->num = source;
		led->chip = chip;
		init_data.fwnode = of_fwnode_handle(child);

		if (!of_property_read_u32(child, "led-max-microamp", &imax)) {
			led->imax = aw20xx_microamp_to_imax(imax, chip->cdef);
		} else {
			led->imax = chip->cdef->default_imax;
			dev_info(&chip->client->dev,
				 "DT property led-max-microamp is missing\n");
		}

		led->cdev.brightness_set_blocking = aw20xx_brightness_set;
		led->cdev.blink_set = aw20xx_blink_set;

		ret = devm_led_classdev_register_ext(&chip->client->dev,
							 &led->cdev, &init_data);
		if (ret < 0)
			return ret;

		i++;
	}

	if (!count)
		return -EINVAL;

	chip->num_leds = i;

	return 0;
}

static void aw20xx_chip_disable_action(void *data)
{
	aw20xx_chip_disable(data);
}

static int aw20xx_probe(struct i2c_client *client)
{
	struct aw20xx *chip;
	const struct aw20xx_chipdef *cdef;
	int ret;
	unsigned int chipid;

	cdef = device_get_match_data(&client->dev);

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	ret = devm_mutex_init(&client->dev, &chip->mutex);
	if (ret)
		return ret;

	mutex_lock(&chip->mutex);

	chip->client = client;
	chip->cdef = cdef;
	i2c_set_clientdata(client, chip);

	chip->regmap = devm_regmap_init_i2c(client, &chip->cdef->regmap_cfg);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		goto error;
	}

	chip->regulators[0].supply = "vcc";
	chip->regulators[1].supply = "vio";
	ret = devm_regulator_bulk_get(&client->dev,
					  ARRAY_SIZE(chip->regulators),
					  chip->regulators);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(&client->dev,
				"Failed to request regulators: %d\n", ret);
		goto error;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(chip->regulators),
					chip->regulators);
	if (ret) {
		dev_err(&client->dev,
			"Failed to enable regulators: %d\n", ret);
		goto error;
	}

	ret = regmap_read(chip->regmap, AW20XX_RSTR, &chipid);
	if (ret) {
		dev_err(&client->dev, "Failed to read chip ID: %d\n",
			ret);
		goto error_reg;
	}
	if (chipid != chip->cdef->chip_id) {
		dev_err(&client->dev, "Chip reported wrong ID: %x\n",
			chipid);
		ret = -ENODEV;
		goto error_reg;
	}

	ret = devm_add_action(&client->dev, aw20xx_chip_disable_action, chip);
	if (ret)
		goto error_reg;

	ret = aw20xx_probe_dt(chip);
	if (ret < 0)
		goto error_reg;

	ret = regulator_bulk_disable(ARRAY_SIZE(chip->regulators),
					 chip->regulators);
	if (ret) {
		dev_err(&client->dev,
			"Failed to disable regulators: %d\n", ret);
		goto error;
	}

	mutex_unlock(&chip->mutex);

	return 0;

error_reg:
	regulator_bulk_disable(ARRAY_SIZE(chip->regulators),
				chip->regulators);

error:
	mutex_unlock(&chip->mutex);
	return ret;
}

static const struct of_device_id aw20xx_match_table[] = {
	{ .compatible = "awinic,aw2013", .data = &aw2013_chipdef },
	{ .compatible = "awinic,aw2027", .data = &aw2027_chipdef },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, aw20xx_match_table);

static struct i2c_driver aw20xx_driver = {
	.driver = {
		.name = "leds-aw20xx",
		.of_match_table = aw20xx_match_table,
	},
	.probe = aw20xx_probe,
};

module_i2c_driver(aw20xx_driver);

MODULE_AUTHOR("Nikita Travkin <nikitos.tr@gmail.com>");
MODULE_DESCRIPTION("AW20XX LED driver");
MODULE_LICENSE("GPL v2");
