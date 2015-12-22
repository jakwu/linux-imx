/*
 * Copyright (C) 2014 MSC Technologies GmbH,
 *          www.msc-technologies.eu
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/mfd/da9063/pdata.h>
#include <linux/mfd/da9063/registers.h>
#include <linux/mfd/da9063/core.h>
#include <linux/regmap.h>

#define BCHG_ISET(R) (((R)>>4) & 0xf)
#define BCHG_VSET(R) (((R)) & 0xf)

struct da9063_battery {
	struct device *dev;
	struct da9063 *da9063;
	u16 charging_uA;
	u16 charging_mV;
};

struct mV_reg_record {
	u16 mV;
	u16 reg;
};

struct mV_reg_record mV_map[16] =
{
	{ .mV = 0,    .reg = 0 },
	{ .mV = 1100, .reg = 1 },
	{ .mV = 1200, .reg = 2 },
	{ .mV = 1400, .reg = 3 },
	{ .mV = 1600, .reg = 4 },
	{ .mV = 1800, .reg = 5 },
	{ .mV = 2000, .reg = 6 },
	{ .mV = 2200, .reg = 7 },
	{ .mV = 2400, .reg = 8 },
	{ .mV = 2500, .reg = 9 },
	{ .mV = 2600, .reg = 10 },
	{ .mV = 2700, .reg = 11 },
	{ .mV = 2800, .reg = 12 },
	{ .mV = 2900, .reg = 13 },
	{ .mV = 3000, .reg = 14 },
	{ .mV = 3100, .reg = 15 },
};

static int mV_to_reg(u16 mV) {
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(mV_map); idx++) {
		struct mV_reg_record *rec = &mV_map[idx];
		if (rec->mV == mV) {
			return rec->reg;
		}
	}
	return -1;
}

static int reg_to_mV(u16 reg) {
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(mV_map); idx++) {
		struct mV_reg_record *rec = &mV_map[idx];
		if (rec->reg == reg) {
			return rec->mV;
		}
	}
	return -1;
}

struct uA_reg_record {
	u16 uA;
	u16 reg;
};

struct uA_reg_record uA_map[16] =
{
	{ .uA = 0,    .reg = 0 },
	{ .uA = 100, .reg = 1 },
	{ .uA = 200, .reg = 2 },
	{ .uA = 300, .reg = 3 },
	{ .uA = 400, .reg = 4 },
	{ .uA = 500, .reg = 5 },
	{ .uA = 600, .reg = 6 },
	{ .uA = 700, .reg = 7 },
	{ .uA = 800, .reg = 8 },
	{ .uA = 900, .reg = 9 },
	{ .uA = 1000, .reg = 10 },
	{ .uA = 2000, .reg = 11 },
	{ .uA = 3000, .reg = 12 },
	{ .uA = 4000, .reg = 13 },
	{ .uA = 5000, .reg = 14 },
	{ .uA = 6000, .reg = 15 },
};

static int uA_to_reg(u16 uA) {
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(uA_map); idx++) {
		struct uA_reg_record *rec = &uA_map[idx];
		if (rec->uA == uA) {
			return rec->reg;
		}
	}
	return -1;
}

static int reg_to_uA(u16 reg) {
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(uA_map); idx++) {
		struct uA_reg_record *rec = &uA_map[idx];
		if (rec->reg == reg) {
			return rec->uA;
		}
	}
	return -1;
}

static int set_charging_parameters(struct da9063_battery *bat)
{
	struct da9063 *da9063 = bat->da9063;
	int charging_i;
	int charging_u;
	u32 bbat_cont;
	int ret = 0;

	charging_i = uA_to_reg(bat->charging_uA);
	if ( charging_i < 0 ) {
		dev_err(bat->dev, "Not practicable charging current value!\n");
		ret = -EINVAL;
		goto exit;
	}

	charging_u = mV_to_reg(bat->charging_mV);
	if ( charging_u < 0 ) {
		dev_err(bat->dev, "Not practicable charging voltage value!\n");
		ret = -EINVAL;
		goto exit;
	}

	bbat_cont = ((charging_i & 0x0f) << 4) | (charging_u & 0x0f);

	ret = regmap_write(da9063->regmap, DA9063_REG_BBAT_CONT, bbat_cont);
	if (ret < 0) {
		dev_err(bat->dev, "Cannot set battery charger parameters.\n");
		ret = -EIO;
		goto exit;
	}

	ret = regmap_read(da9063->regmap, DA9063_REG_BBAT_CONT, &bbat_cont);
	if (ret < 0) {
		dev_err(bat->dev, "Cannot read battery charger parameters.\n");
		ret = -EIO;
		goto exit;
	}

	dev_info(bat->dev, "Iset=%duA, Vset=%dmV\n",
			reg_to_uA(BCHG_ISET(bbat_cont)), reg_to_mV(BCHG_VSET(bbat_cont)));

exit:
	return ret;
}

static ssize_t da9063_bat_charging_uA_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct da9063_battery *bat = dev_get_drvdata(dev);
	return sprintf(buf, "%duA\n", bat->charging_uA);
}

static ssize_t da9063_bat_charging_uA_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct da9063_battery *bat = dev_get_drvdata(dev);
	int ret;
	unsigned long value;


	ret = strict_strtoul(buf, 10, &value);
	if (ret < 0) {
		goto exit;
	}

	bat->charging_uA = value;
	ret = set_charging_parameters(bat);
	if (ret < 0) {
		goto exit;
	}
exit:
	return count;
}

DEVICE_ATTR(charging_uA, S_IRUSR | S_IWUSR, \
	da9063_bat_charging_uA_show, da9063_bat_charging_uA_store);

static ssize_t da9063_bat_charging_mV_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct da9063_battery *bat = dev_get_drvdata(dev);
	return sprintf(buf, "%dmV\n", bat->charging_mV);
}

static ssize_t da9063_bat_charging_mV_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct da9063_battery *bat = dev_get_drvdata(dev);
	int ret;
	unsigned long value;

	ret = strict_strtoul(buf, 10, &value);
	if (ret < 0) {
		goto exit;
	}

	bat->charging_mV = value;
	ret = set_charging_parameters(bat);
	if (ret < 0) {
		goto exit;
	}
exit:
	return count;
}

DEVICE_ATTR(charging_mV, S_IRUSR | S_IWUSR, \
	da9063_bat_charging_mV_show, da9063_bat_charging_mV_store);

static struct attribute *da9063_bat_sysfs_attributes[] = {
	&dev_attr_charging_uA.attr,
	&dev_attr_charging_mV.attr,
	NULL,
};

static const struct attribute_group da9063_bat_sysfs_attr_group = {
	.attrs = da9063_bat_sysfs_attributes,
};

static int da9063_bat_sysfs_init(struct da9063_battery *bat)
{
	return sysfs_create_group(&bat->dev->kobj, &da9063_bat_sysfs_attr_group);
}

static void da9063_bat_sysfs_exit(struct da9063_battery *bat)
{
	sysfs_remove_group(&bat->dev->kobj, &da9063_bat_sysfs_attr_group);
}

#if defined(CONFIG_OF)
static struct da9063_battery_pdata *da9063_bat_parse_dt(struct platform_device *pdev)
{
	struct da9063_battery_pdata *pdata;
	struct device_node *node;

	node = of_find_node_by_name(pdev->dev.parent->of_node, "battery");
	if (!node) {
		dev_err(&pdev->dev, "Battery device node not found\n");
		return ERR_PTR(-ENODEV);
	}

	pdata = devm_kzalloc(&pdev->dev, sizeof(struct da9063_battery_pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev, "Can't allocate battery device platfrom data\n");
		return ERR_PTR(-ENOMEM);
	}

	if (of_property_read_u32(node, "charging-uA", &pdata->charging_uA)) {
		dev_err(&pdev->dev, "Missing mandatory property <charging-uA>!\n");
		goto error;
	}

	if (of_property_read_u32(node, "charging-mV", &pdata->charging_mV)) {
		dev_err(&pdev->dev, "Missing mandatory property <charging-mV>!\n");
		goto error;
	}

	return pdata;

error:
	// TODO, wgle, Oct 27, 2014, 1:18:06 PM: free memory?
	return NULL;
}
#else /* defined(CONFIG_OF) */
static struct da9063_battery_pdata *da9063_bat_parse_dt(struct platform_device *pdev)
{
	return PTR_ERR(-ENODEV);
}
#endif /* defined(CONFIG_OF) */

static s32 da9063_bat_probe(struct platform_device *pdev)
{
	struct da9063 *da9063 = dev_get_drvdata(pdev->dev.parent);
	struct da9063_pdata *da9063_pdata = dev_get_platdata(da9063->dev);
	struct da9063_battery_pdata *bat_pdata;
	struct da9063_battery *bat;
	int ret = 0;

	bat = devm_kzalloc(&pdev->dev, sizeof(struct da9063_battery),
				GFP_KERNEL);
	if (!bat)
		return -ENOMEM;

	bat_pdata = da9063_pdata ? da9063_pdata->battery_pdata : NULL;

	if (!bat_pdata)
		bat_pdata = da9063_bat_parse_dt(pdev);

	if (!bat_pdata || IS_ERR(bat_pdata)){
		dev_err(&pdev->dev, "No battery defined for the platform\n");
		return PTR_ERR(bat_pdata);
	}

	bat->dev = &pdev->dev;
	bat->da9063 = da9063;
	bat->charging_uA = bat_pdata->charging_uA;
	bat->charging_mV = bat_pdata->charging_mV;

	ret = set_charging_parameters(bat);
	if (ret < 0)
		goto error;

	ret = da9063_bat_sysfs_init(bat);
	if (ret < 0) {
		dev_err(&pdev->dev, "Couldn't initialize sysfs.\n");
		goto error;
	}

	platform_set_drvdata(pdev, bat);

	return ret;

error:
	return ret;
}
static int da9063_bat_remove(struct platform_device *pdev)
{
	struct da9063_battery *bat = platform_get_drvdata(pdev);

	da9063_bat_sysfs_exit(bat);

	bat->dev = NULL,
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id da9063_battery_ids[] = {
	{ .compatible = "dialog,da9063-battery" },
	{}
};
MODULE_DEVICE_TABLE(of, da9063_battery_ids);

static struct platform_driver da9063_bat_driver = {
	.driver = {
		.name = DA9063_DRVNAME_BATTERY,
		.owner = THIS_MODULE,
		.of_match_table = da9063_battery_ids,
	},
	.probe = da9063_bat_probe,
	.remove = da9063_bat_remove,

};
module_platform_driver(da9063_bat_driver);

MODULE_DESCRIPTION("DA9063 battery device driver");
MODULE_AUTHOR("Waldemar Glensk <waldemar.glensk@msc-technologies.eu>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9063-bat");
