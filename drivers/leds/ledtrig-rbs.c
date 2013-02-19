/*
 * LED RBS Trigger
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
  *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include <linux/timer.h>
#include <linux/slab.h>

#define RBS_LED_MAXPHASES  6

struct rbs_led_phase {
	int led;
	int delay; /* us */
};

struct rbs_led_mode {
	char *name;
	int nphases;
	struct rbs_led_phase phases[RBS_LED_MAXPHASES];
};

struct rbs_led {
	struct timer_list delay_timer;
	struct mutex mode_lock;
	int cphase;
	struct rbs_led_mode *mode;
};

/* RBS LED modes */
static struct rbs_led_mode mode_steady_on = {
	.name = "on",
	.nphases = 1,
	.phases = {
		{1, -1 /* infinitive */}
	},
};

static struct rbs_led_mode mode_steady_off = {
	.name = "off",
	.nphases = 1,
	.phases = {
		{0, -1 /* infinitive */}
	},
};

static struct rbs_led_mode mode_slow_blink = {
	.name = "slow",
	.nphases = 2,
	.phases = {
		{0, 1000000},
		{1, 1000000}
	},
};

static struct rbs_led_mode mode_fast_blink = {
	.name = "fast",
	.nphases = 2,
	.phases = {
		{0, 31250},
		{1, 31250}
	},
};

static struct rbs_led_mode mode_double_flash_on = {
	.name = "double-on",
	.nphases = 6,
	.phases = {
		{0, 300000},
		{1, 350000},
		{0, 300000},
		{1, 350000},
		{0, 300000},
		{1, 2400000},
	},
};

static struct rbs_led_mode mode_double_flash_off = {
	.name = "double-off",
	.nphases = 6,
	.phases = {
		{0, 300000},
		{1, 350000},
		{0, 300000},
		{1, 350000},
		{0, 300000},
		{0, 2400000},
	},
};
static struct rbs_led_mode *rbs_led_modes[] = {
	&mode_steady_on,
	&mode_steady_off,
	&mode_slow_blink,
	&mode_fast_blink,
	&mode_double_flash_on,
	&mode_double_flash_off,
	NULL
};

static void rbs_led_timer(unsigned long data)
{
	struct led_classdev *led_cdev = (void *)data;
	struct rbs_led *led = led_cdev->trigger_data;
	int delay;

	/* toggle led */
	if (led->mode->phases[led->cphase].led)
		led_brightness_set(led_cdev, LED_FULL);
	else
		led_brightness_set(led_cdev, LED_OFF);

	delay = led->mode->phases[led->cphase].delay;

	if (++led->cphase >= led->mode->nphases)
		led->cphase = 0;

	if (delay != -1)
		mod_timer(&led->delay_timer,
				jiffies + usecs_to_jiffies(delay));
}

static ssize_t led_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct rbs_led *led = led_cdev->trigger_data;

	return sprintf(buf, "%s\n", led->mode->name);
}

static ssize_t led_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct rbs_led *led = led_cdev->trigger_data;
	int i;

	if (!led)
		return -EINVAL;

	for (i = 0; rbs_led_modes[i]; i++) {
		if (!strncmp(rbs_led_modes[i]->name, buf,
					strlen(rbs_led_modes[i]->name)))
			break;
	}
	if (!rbs_led_modes[i])
		return -EINVAL;

	mutex_lock(&led->mode_lock);

	/* replace led mode */
	del_timer_sync(&led->delay_timer);
	led_brightness_set(led_cdev, LED_OFF);

	led->cphase = 0;
	led->mode = rbs_led_modes[i];

	mod_timer(&led->delay_timer, jiffies + 1);

	mutex_unlock(&led->mode_lock);

	return size;
}

static DEVICE_ATTR(mode, 0644, led_mode_show, led_mode_store);

static void rbs_trig_activate(struct led_classdev *led_cdev)
{
	int ret;
	struct rbs_led *led;

	led_cdev->trigger_data = NULL;

	led = kzalloc(sizeof(*led), GFP_KERNEL);
	if (led == NULL)
		return;

	mutex_init(&led->mode_lock);

	/* default mode - steady off */
	led->mode = &mode_steady_off;

	init_timer(&led->delay_timer);
	led->delay_timer.function = rbs_led_timer;
	led->delay_timer.data = (unsigned long) led_cdev;

	ret = device_create_file(led_cdev->dev, &dev_attr_mode);
	if (ret)
		goto fail;

	led_cdev->trigger_data = led;

	return;

fail:
	kfree(led);
}

static void rbs_trig_deactivate(struct led_classdev *led_cdev)
{
	struct rbs_led *led = led_cdev->trigger_data;

	if (led) {
		device_remove_file(led_cdev->dev, &dev_attr_mode);
		del_timer_sync(&led->delay_timer);
		kfree(led);
		led_cdev->trigger_data = NULL;
	}

	led_brightness_set(led_cdev, LED_OFF);
}

static struct led_trigger rbs_led_trigger = {
	.name     = "rbs",
	.activate = rbs_trig_activate,
	.deactivate = rbs_trig_deactivate,
};

static int __init rbs_led_trig_init(void)
{
	return led_trigger_register(&rbs_led_trigger);
}

static void __exit rbs_led_trig_exit(void)
{
	led_trigger_unregister(&rbs_led_trigger);
}

module_init(rbs_led_trig_init);
module_exit(rbs_led_trig_exit);

MODULE_AUTHOR("Andrey Panteleev <andrey.xx.panteleev@ericsson.com");
MODULE_DESCRIPTION("RBS LED trigger");
MODULE_LICENSE("GPL");
