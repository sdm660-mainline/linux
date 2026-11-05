// SPDX-License-Identifier: GPL
// Driver for SGMicro SGM31323 3-channel LED driver

#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regmap.h>

#define SGM31323_MAX_LEDS 3

#define SGM31323_REG_MAX 0x08

#define SGM31323_REG_ENABLE_RESET		(0)
#define SGM31323_REG_FLASH_PERIOD		(1)
#define SGM31323_REG_PWM1_TIMER			(2)
#define SGM31323_REG_PWM2_TIMER			(3)
#define SGM31323_REG_CHANNEL_CONTROL		(4)
#define SGM31323_REG_RAMP_RATE			(5)
#define SGM31323_REG_ILED(x)			(6+x)

#define SGM31323_REG0_RESET_ALL			(7)
#define SGM31323_REG0_EN_CTRL_ALWAYS_ON		(3)
#define SGM31323_REG0_EN_CTRL_SHUTDOWN_MODE	(0)
#define SGM31323_REG4_EN_TM_ALWAYS_OFF		(0)
#define SGM31323_REG4_EN_TM_ALWAYS_ON		(1)
#define SGM31323_REG4_EN_TM_PWM1		(2)
#define SGM31323_REG4_EN_TM_PWM2		(3)
#define SGM31323_REG_ILED_STEP			(125) /* microamps */
#define SGM31323_REG_ILED_MAX			(192) /* 24 milliamps */

#define SGM31323_TIME_STEP			128 /* ms */

enum {
	SGM31323_REG0_RESET_MODE,
	SGM31323_REG0_EN_CTRL,
	SGM31323_REG4_EN_TM_BITMASK_LED0,
	SGM31323_REG4_EN_TM_BITMASK_LED1,
	SGM31323_REG4_EN_TM_BITMASK_LED2,
};

static const struct reg_field sgm31323_fields[] = {
	[SGM31323_REG0_RESET_MODE] = REG_FIELD(SGM31323_REG_ENABLE_RESET, 0, 2),
	[SGM31323_REG0_EN_CTRL] = REG_FIELD(SGM31323_REG_ENABLE_RESET, 3, 4),
	[SGM31323_REG4_EN_TM_BITMASK_LED0] = REG_FIELD(SGM31323_REG_CHANNEL_CONTROL, 0, 1),
	[SGM31323_REG4_EN_TM_BITMASK_LED1] = REG_FIELD(SGM31323_REG_CHANNEL_CONTROL, 2, 3),
	[SGM31323_REG4_EN_TM_BITMASK_LED2] = REG_FIELD(SGM31323_REG_CHANNEL_CONTROL, 4, 5),
};

struct sgm31323;

struct sgm31323_led {
	struct sgm31323 *chip;
	struct led_classdev cdev;
	u32 num;
	unsigned int imax;
};

struct sgm31323 {
	struct mutex mutex; /* held when writing to registers */
	struct regulator_bulk_data regulators[1]; /* TODO change to non-array calls or add another one for vio */
	struct i2c_client *client;
	struct sgm31323_led leds[SGM31323_MAX_LEDS];
	struct regmap *regmap;
	struct regmap_field *fields[ARRAY_SIZE(sgm31323_fields)];
	int num_leds;
	bool enabled;
};

static int sgm31323_chip_init(struct sgm31323 *chip)
{
	int i, ret;

	ret = regmap_field_write(chip->fields[SGM31323_REG0_EN_CTRL], SGM31323_REG0_EN_CTRL_ALWAYS_ON);
	if (ret) {
		dev_err(&chip->client->dev, "Failed to enable the chip: %d\n",
			ret);
		return ret;
	}

	for (i = 0; i < chip->num_leds; i++) {
		ret = regmap_write(chip->regmap,
					 SGM31323_REG_ILED(chip->leds[i].num),
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

static void sgm31323_chip_disable(struct sgm31323 *chip)
{
	int ret;

	if (!chip->enabled)
		return;

	ret = regmap_field_write(chip->fields[SGM31323_REG0_EN_CTRL], SGM31323_REG0_EN_CTRL_SHUTDOWN_MODE);

	if (ret) {
		dev_err(&chip->client->dev, "Failed to disable the chip: %d\n",
			ret);
		return;
	}

	ret = regulator_bulk_disable(ARRAY_SIZE(chip->regulators),
				     chip->regulators);
	if (ret) {
		dev_err(&chip->client->dev,
			"Failed to disable regulators: %d\n", ret);
		return;
	}

	chip->enabled = false;
}

static int sgm31323_chip_enable(struct sgm31323 *chip)
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

	ret = sgm31323_chip_init(chip);
	if (ret)
		sgm31323_chip_disable(chip);

	return ret;
}

static bool sgm31323_chip_in_use(struct sgm31323 *chip)
{
	int i;

	for (i = 0; i < chip->num_leds; i++)
		if (chip->leds[i].cdev.brightness)
			return true;

	return false;
}

static int sgm31323_brightness_set(struct led_classdev *cdev,
				   enum led_brightness brightness)
{
	struct sgm31323_led *led = container_of(cdev, struct sgm31323_led, cdev);
	int ret, num;

	mutex_lock(&led->chip->mutex);

	if (sgm31323_chip_in_use(led->chip)) {
		ret = sgm31323_chip_enable(led->chip);
		if (ret)
			goto error;
	}

	num = led->num;

	if (brightness == LED_ON) {
		ret = regmap_field_write(led->chip->fields[SGM31323_REG4_EN_TM_BITMASK_LED0+num], SGM31323_REG4_EN_TM_ALWAYS_ON);
	} else {
		ret = regmap_field_write(led->chip->fields[SGM31323_REG4_EN_TM_BITMASK_LED0+num], SGM31323_REG4_EN_TM_ALWAYS_OFF);
	}

	if (ret)
		goto error;

	if (!sgm31323_chip_in_use(led->chip))
		sgm31323_chip_disable(led->chip);

error:
	mutex_unlock(&led->chip->mutex);

	return ret;
}

/**
 * SGM31323 doesn't have individual delay_on and delay_off registers.
 * Instead it has a period and a duty.
 * Also this doesn't work with different timers on a per-led basis but too bad.
 */
static int sgm31323_blink_set(struct led_classdev *cdev,
			    unsigned long *delay_on, unsigned long *delay_off)
{
	struct sgm31323_led *led = container_of(cdev, struct sgm31323_led, cdev);
	int ret, num = led->num;
	unsigned long off = 0, on = 0, bigger = 0, total = 0;
	u32 duty;

	/* If no blink specified, default to 0.9765625Hz. */
	if (!*delay_off && !*delay_on) {
		*delay_off = 512;
		*delay_on = 512;
	}

	if (!led->cdev.brightness) {
		led->cdev.brightness = LED_ON;
		ret = sgm31323_brightness_set(&led->cdev, led->cdev.brightness);
		if (ret)
			return ret;
	}

	/* Never on - just set to off */
	if (!*delay_on) {
		led->cdev.brightness = LED_OFF;
		return sgm31323_brightness_set(&led->cdev, led->cdev.brightness);
	}

	/* Never off - brightness is already set, disable blinking */
	if (!*delay_off) {
		led->cdev.brightness = LED_ON;
		return sgm31323_brightness_set(&led->cdev, led->cdev.brightness);
	}

	mutex_lock(&led->chip->mutex);

	/* Convert into values the HW will understand. */
	off = min(127, DIV_ROUND_CLOSEST(*delay_off, SGM31323_TIME_STEP));
	on = min(127, DIV_ROUND_CLOSEST(*delay_on, SGM31323_TIME_STEP));
	bigger = max(off, on);
	total = on + off;

	if (total == 0) {
			duty = 0;
	} else  {
		/* hacky fixed-point ratio of on/off
		 * 255 is 99.6% on
		 * 128 is 50% on
		 * 0 is always off
		 */
		duty = ((on << 8) + (total / 2)) / total;
		duty = clamp_val(duty, 0, 255);
		*delay_on = on * SGM31323_TIME_STEP;
		*delay_off = off * SGM31323_TIME_STEP;
	}


	/* Set timings */
	ret = regmap_write(led->chip->regmap,
			   SGM31323_REG_FLASH_PERIOD, total);
	if (ret)
		goto out;

	ret = regmap_write(led->chip->regmap,
			   SGM31323_REG_PWM1_TIMER, duty);
	if (ret)
		goto out;

	/* Finally, enable the LED */
	ret = regmap_field_write(led->chip->fields[SGM31323_REG4_EN_TM_BITMASK_LED0+num], SGM31323_REG4_EN_TM_PWM1);
out:
	mutex_unlock(&led->chip->mutex);

	return ret;
}

static int sgm31323_probe_dt(struct sgm31323 *chip)
{
	struct device_node *np = dev_of_node(&chip->client->dev);
	int count, ret = 0, i = 0;
	struct sgm31323_led *led;

	count = of_get_available_child_count(np);
	if (!count || count > SGM31323_MAX_LEDS)
		return -EINVAL;

	ret = regmap_field_write(chip->fields[SGM31323_REG0_RESET_MODE], SGM31323_REG0_RESET_ALL);

	for_each_available_child_of_node_scoped(np, child) {
		struct led_init_data init_data = {};
		u32 source;
		u32 imax;

		ret = of_property_read_u32(child, "reg", &source);
		if (ret != 0 || source >= SGM31323_MAX_LEDS) {
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
			led->imax = min_t(u32, imax / SGM31323_REG_ILED_STEP, SGM31323_REG_ILED_MAX);
		} else {
			led->imax = 40; // 5mA
			dev_info(&chip->client->dev,
				 "DT property led-max-microamp is missing\n");
		}

		led->cdev.brightness_set_blocking = sgm31323_brightness_set;
		led->cdev.blink_set = sgm31323_blink_set;
		led->cdev.max_brightness = 1;

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

static void sgm31323_chip_disable_action(void *data)
{
	sgm31323_chip_disable(data);
}

static const struct regmap_config sgm31323_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = SGM31323_REG_MAX,
	/* reads return all 0xff, use internal cache for rmw ops */
	.readable_reg = NULL,
	.cache_type = REGCACHE_FLAT,
};

static int sgm31323_probe(struct i2c_client *client)
{
	struct sgm31323 *chip;
	int ret;
	int i;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	ret = devm_mutex_init(&client->dev, &chip->mutex);
	if (ret)
		return ret;

	mutex_lock(&chip->mutex);

	chip->client = client;
	i2c_set_clientdata(client, chip);

	chip->regmap = devm_regmap_init_i2c(client, &sgm31323_regmap_config);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		goto error;
	}

	for (i = 0; i < ARRAY_SIZE(sgm31323_fields); i++) {
		chip->fields[i] = devm_regmap_field_alloc(&client->dev,
							  chip->regmap,
							  sgm31323_fields[i]);
		if (IS_ERR(chip->fields[i]))
			return PTR_ERR(chip->fields[i]);
	}

	chip->regulators[0].supply = "vdd";
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

	ret = devm_add_action(&client->dev, sgm31323_chip_disable_action, chip);
	if (ret)
		goto error_reg;

	ret = sgm31323_probe_dt(chip);
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

static const struct of_device_id sgm31323_match_table[] = {
	{ .compatible = "sgmicro,sgm31323", },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, sgm31323_match_table);

static struct i2c_driver sgm31323_driver = {
	.driver = {
		.name = "leds-sgm31323",
		.of_match_table = sgm31323_match_table,
	},
	.probe = sgm31323_probe,
};

module_i2c_driver(sgm31323_driver);

MODULE_AUTHOR("Paul Sajna <sajattack@postmarketos.org>");
MODULE_DESCRIPTION("SGMicro SGM31323 LED driver");
MODULE_LICENSE("GPL");
