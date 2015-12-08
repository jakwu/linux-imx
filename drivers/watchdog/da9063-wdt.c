/*
 * System monitoring driver for da9063 PMICs.
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/mfd/da9063/core.h>
#include <linux/mfd/da9063/registers.h>

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

#define DA9063_DEF_TIMEOUT	4
#define DA9063_TWDMIN		256

struct da9063_wdt_data {
	struct watchdog_device wdt;
	struct da9063 *da9063;
	struct kref kref;
};

static const struct {
	u8 reg_val;
	int user_time;  /* In seconds */
} da9063_wdt_maps[] = {
	{ 0, 0 },
	{ 1, 2 },
	{ 2, 4 },
	{ 3, 8 },
	{ 4, 16 },
	{ 5, 32 },
	{ 5, 33 },  /* Actual time  32.768s so included both 32s and 33s */
	{ 6, 65 },
	{ 6, 66 },  /* Actual time 65.536s so include both, 65s and 66s */
	{ 7, 131 },
};

static int da9063_wdt_set_timeout(struct watchdog_device *wdt_dev,
				  unsigned int timeout)
{
	struct da9063_wdt_data *driver_data = watchdog_get_drvdata(wdt_dev);
	struct da9063 *da9063 = driver_data->da9063;
	int ret, i,val;

	for (i = 0; i < ARRAY_SIZE(da9063_wdt_maps); i++)
		if (da9063_wdt_maps[i].user_time == timeout)
			break;

	if (i == ARRAY_SIZE(da9063_wdt_maps))
		ret = -EINVAL;
	else {
		ret = regmap_write(da9063->regmap, DA9063_REG_CONTROL_D,
					(da9063_wdt_maps[i].reg_val & DA9063_TWDSCALE_MASK) <<
					DA9063_TWDSCALE_SHIFT);
	}
	ret = regmap_read(da9063->regmap, DA9063_REG_CONTROL_D, &val);
	if (ret < 0) {
		dev_err(da9063->dev,
			"Failed to update timescale bit, %d\n", ret);
		return ret;
	}

	wdt_dev->timeout = timeout;

	return 0;
}

static int da9063_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct da9063_wdt_data *driver_data = watchdog_get_drvdata(wdt_dev);
	struct da9063 *da9063 = driver_data->da9063;
	
	/*
	 * We have a minimum time for watchdog window called TWDMIN. A write
	 * to the watchdog before this elapsed time will cause an error.
	 */
	mdelay(DA9063_TWDMIN);

	/* Reset the watchdog timer */
	return regmap_write(da9063->regmap, DA9063_REG_CONTROL_F,
					(1&DA9063_WATCHDOG_MASK));
}

static void da9063_wdt_release_resources(struct kref *r)
{
}

static void da9063_wdt_ref(struct watchdog_device *wdt_dev)
{
	struct da9063_wdt_data *driver_data = watchdog_get_drvdata(wdt_dev);

	kref_get(&driver_data->kref);
}

static void da9063_wdt_unref(struct watchdog_device *wdt_dev)
{
	struct da9063_wdt_data *driver_data = watchdog_get_drvdata(wdt_dev);

	kref_put(&driver_data->kref, da9063_wdt_release_resources);
}

static int da9063_wdt_start(struct watchdog_device *wdt_dev)
{
	return da9063_wdt_set_timeout(wdt_dev, wdt_dev->timeout);
}

static int da9063_wdt_stop(struct watchdog_device *wdt_dev)
{
	return da9063_wdt_set_timeout(wdt_dev, 0);
}

static struct watchdog_info da9063_wdt_info = {
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity	= "DA9063 Watchdog",
};

static const struct watchdog_ops da9063_wdt_ops = {
	.owner = THIS_MODULE,
	.start = da9063_wdt_start,
	.stop = da9063_wdt_stop,
	.ping = da9063_wdt_ping,
	.set_timeout = da9063_wdt_set_timeout,
	.ref = da9063_wdt_ref,
	.unref = da9063_wdt_unref,
};

static int da9063_wdt_probe(struct platform_device *pdev)
{
	struct da9063 *da9063 = dev_get_drvdata(pdev->dev.parent);
	struct da9063_wdt_data *driver_data;
	struct watchdog_device *da9063_wdt;
	int ret;

	driver_data = devm_kzalloc(&pdev->dev, sizeof(*driver_data),
				   GFP_KERNEL);
	if (!driver_data) {
		dev_err(da9063->dev, "Failed to allocate watchdog device\n");
		return -ENOMEM;
	}

	driver_data->da9063 = da9063;

	da9063_wdt = &driver_data->wdt;

	da9063_wdt->timeout = DA9063_DEF_TIMEOUT;
	da9063_wdt->info = &da9063_wdt_info;
	da9063_wdt->ops = &da9063_wdt_ops;
	watchdog_set_nowayout(da9063_wdt, nowayout);
	watchdog_set_drvdata(da9063_wdt, driver_data);

	kref_init(&driver_data->kref);

	

	dev_set_drvdata(&pdev->dev, driver_data);

	ret = watchdog_register_device(&driver_data->wdt);
	if (ret != 0)
		dev_err(da9063->dev, "watchdog_register_device() failed: %d\n",
			ret);
	dev_info(da9063->dev, "DA9063 Watchdog probed!\n");
	
	return ret;
}

static int da9063_wdt_remove(struct platform_device *pdev)
{
	struct da9063_wdt_data *driver_data = dev_get_drvdata(&pdev->dev);

	watchdog_unregister_device(&driver_data->wdt);
	kref_put(&driver_data->kref, da9063_wdt_release_resources);

	return 0;
}

static struct platform_driver da9063_wdt_driver = {
	.probe = da9063_wdt_probe,
	.remove = da9063_wdt_remove,
	.driver = {
		.name	= "da9063-watchdog",
	},
};

module_platform_driver(da9063_wdt_driver);

MODULE_AUTHOR("Achim Kanert <acka@msc-ge.com>");
MODULE_DESCRIPTION("DA9063 watchdog");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9063-watchdog");
