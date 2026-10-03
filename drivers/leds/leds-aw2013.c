// SPDX-License-Identifier: GPL-2.0+
// Driver for Awinic AW2013 3-channel LED driver

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

/* Global control register */
#define AW20XX_GCR 0x01
#define AW20XX_GCR_ENABLE BIT(0)

/* LED channel enable register */
#define AW20XX_LCTR 0x30
#define AW20XX_LCTR_LE(x) BIT((x))

/* LED channel control registers */
#define AW20XX_LCFG(x) (0x31 + (x))
#define AW20XX_LCFG_IMAX_MASK (BIT(0) | BIT(1)) // Should be 0-3
#define AW20XX_LCFG_MD BIT(4)
#define AW20XX_LCFG_FI BIT(5)
#define AW20XX_LCFG_FO BIT(6)

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

#define AW20XX_TIME_STEP 130 /* ms */

#define AW2013_RSTR_CHIP_ID 0x33
#define AW2013_REG_MAX 0x77

struct aw20xx;

struct aw20xx_led {
	struct aw20xx *chip;
	struct led_classdev cdev;
	u32 num;
	unsigned int imax;
};

struct aw20xx {
	struct mutex mutex; /* held when writing to registers */
	struct regulator_bulk_data regulators[2];
	struct i2c_client *client;
	struct aw20xx_led leds[AW20XX_MAX_LEDS];
	struct regmap *regmap;
	int num_leds;
	bool enabled;
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

	for (i = 0; i < chip->num_leds; i++) {
		ret = regmap_update_bits(chip->regmap,
					 AW20XX_LCFG(chip->leds[i].num),
					 AW20XX_LCFG_IMAX_MASK,
					 chip->leds[i].imax);
		if (ret) {
			dev_err(&chip->client->dev,
				"Failed to set maximum current for led %d: %d\n",
				chip->leds[i].num, ret);
			return ret;
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
			led->imax = min_t(u32, imax / 5000, 3);
		} else {
			led->imax = 1; // 5mA
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

static const struct regmap_config aw20xx_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = AW2013_REG_MAX,
};

static int aw20xx_probe(struct i2c_client *client)
{
	struct aw20xx *chip;
	int ret;
	unsigned int chipid;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	ret = devm_mutex_init(&client->dev, &chip->mutex);
	if (ret)
		return ret;

	mutex_lock(&chip->mutex);

	chip->client = client;
	i2c_set_clientdata(client, chip);

	chip->regmap = devm_regmap_init_i2c(client, &aw20xx_regmap_config);
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

	if (chipid != AW2013_RSTR_CHIP_ID) {
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
	{ .compatible = "awinic,aw2013", },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, aw20xx_match_table);

static struct i2c_driver aw20xx_driver = {
	.driver = {
		.name = "leds-aw2013",
		.of_match_table = aw20xx_match_table,
	},
	.probe = aw20xx_probe,
};

module_i2c_driver(aw20xx_driver);

MODULE_AUTHOR("Nikita Travkin <nikitos.tr@gmail.com>");
MODULE_DESCRIPTION("AW2013 LED driver");
MODULE_LICENSE("GPL v2");
