/*
 * Copyright (C) 2014 MSC Vertriebs GmbH, Design Center Aachen
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
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <asm/mach/map.h>

#include "mach-imx6q-nanoRISC.h"

static void imx6qdl_nanoRISC_enable_latches(void)
{
	struct device_node *np;
	int u0405_en_gpio;

	np = of_find_node_by_path("/latches");
	if (!np) {
		pr_warn("failed to find 'latches' node\n");
		return;
	}

	u0405_en_gpio = of_get_named_gpio(np, "u0405-en-gpio", 0);
	if (gpio_is_valid(u0405_en_gpio) &&
			!gpio_request_one(u0405_en_gpio, GPIOF_DIR_OUT, "u0405-en")) {
		gpio_set_value_cansleep(u0405_en_gpio, 1);
	}
	else {
		pr_warn("failed to enable 'u0405' latch\n");
	}
}

static void imx6qdl_nanoRISC_enable_lcd(void)
{
	struct device_node *np;
	int lcd_power_en;

	np = of_find_node_by_path("/lcd_power_en");
	if (!np) {
		pr_warn("failed to find 'lcd_power_en' node\n");
		return;
	}

	lcd_power_en = of_get_named_gpio(np, "gpios", 0);
	if (gpio_is_valid(lcd_power_en) &&
			!gpio_request_one(lcd_power_en, GPIOF_DIR_OUT, "lcd-power-en")) {
		gpio_set_value_cansleep(lcd_power_en, 0);
	}
	else {
		pr_warn("failed to enable 'lcd-power-en' gpio\n");
	}
}

static void imx6qdl_nanoRISC_enable_lvds(void)
{
	struct device_node *np;
	int lvds0_bl_en;

	np = of_find_node_by_path("/lvds_cabc_ctrl");
	if (!np) {
		pr_warn("failed to find 'lvds_cabc_ctrl' node\n");
		return;
	}

	lvds0_bl_en = of_get_named_gpio(np, "lvds0-gpios", 0);
	if (gpio_is_valid(lvds0_bl_en) &&
			!gpio_request_one(lvds0_bl_en, GPIOF_DIR_OUT, "lvds0-backlight")) {
		gpio_set_value_cansleep(lvds0_bl_en, 0);
	}
	else {
		pr_warn("failed to enable 'lvds0-backlight' gpio\n");
	}
}


#define SNVS_LPCR		0x38
#define SNVS_LP_DP_EN	(1<<5)
#define SNVS_LP_TOP		(1<<6)

static void imx6qdl_nanoRISC_poweroff(void)
{
	struct device_node *np;
	void __iomem *base;
	u32 value;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6q-caam-snvs");
	if (!np) {
		pr_warn("failed to find 'fsl,sec-v4.0-mon' compatible node\n");
		return;
	}

	base = of_iomap(np, 0);
	if (!base) {
		pr_warn("failed to map snvs\n");
		return;
	}

	value = readl(base + SNVS_LPCR);
	value |= (SNVS_LP_DP_EN | SNVS_LP_TOP);
	writel(value , base + SNVS_LPCR);
}

void imx6qdl_nanoRISC_board_init(void)
{
	imx6qdl_nanoRISC_enable_latches();
	imx6qdl_nanoRISC_enable_lcd();
	imx6qdl_nanoRISC_enable_lvds();
	pm_power_off = imx6qdl_nanoRISC_poweroff;
}

