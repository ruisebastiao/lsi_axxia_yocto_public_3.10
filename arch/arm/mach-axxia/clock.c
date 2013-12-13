/*
 *  linux/arch/arm/mach-axxia/clock.c
 *
 *  Copyright (C) 2012 LSI
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#define AXXIA_CPU_CLOCK 1400000000
#define AXXIA_SYS_CLOCK  450000000
#define AXXIA_DDR_CLOCK 1866000000

#define clk_register_clkdev(_clk, _conid, _devfmt, ...) \
	do { \
		struct clk_lookup *cl; \
		cl = clkdev_alloc(_clk, _conid, _devfmt, ## __VA_ARGS__); \
		clkdev_add(cl); \
	} while (0)

/*
  ------------------------------------------------------------------------------
  axxia_init_clocks

  Clock setup for Emulation/ASIC systems.
*/

#ifdef CONFIG_ARCH_AXXIA_SIM
void __init
axxia_init_clocks(void)
{
	struct clk *clk;
	int i;

	/* APB clock dummy */
	clk = clk_register_fixed_rate(NULL, "apb_pclk", NULL,
				      CLK_IS_ROOT, AXXIA_SYS_CLOCK/2);
	clk_register_clkdev(clk, "apb_pclk", NULL);

	/* CPU core clock (1400MHz) from CPU_PLL */
	clk = clk_register_fixed_rate(NULL, "clk_cpu", NULL,
				      CLK_IS_ROOT, AXXIA_CPU_CLOCK);

	/* APB and System AXI clock from CPU_PLL */
	clk = clk_register_fixed_rate(NULL, "clk_pclk", NULL,
				      CLK_IS_ROOT, AXXIA_CPU_CLOCK/9);

	/* DDR3 (interface 1) clock from SMEM1_PLL */
	clk = clk_register_fixed_rate(NULL, "clk_smem1_2x", NULL,
				      CLK_IS_ROOT, AXXIA_DDR_CLOCK);

	/* AXIS slow peripheral clock from SMEM1_PLL. */
	clk = clk_register_fixed_rate(NULL, "clk_per", NULL,
				      CLK_IS_ROOT, 24000000);
	/* PL011 UART0 */
	clk_register_clkdev(clk, NULL, "2010080000.uart");
	/* PL011 UART1 */
	clk_register_clkdev(clk, NULL, "2010081000.uart");
	/* PL011 UART2 */
	clk_register_clkdev(clk, NULL, "2010082000.uart");
	/* PL011 UART3 */
	clk_register_clkdev(clk, NULL, "2010083000.uart");
	/* PL022 SSP */
	clk_register_clkdev(clk, NULL, "ssp");

	/* Timers 1MHz clock */
	clk = clk_register_fixed_rate(NULL, "clk_1mhz", NULL,
				      CLK_IS_ROOT, 1000000);
	/* SP804 timers */
	clk_register_clkdev(clk, NULL, "sp804");
	for (i = 0; i < 8; i++)
		clk_register_clkdev(clk, NULL, "axxia-timer%d", i);

	/* Dummy MMC clk */
	clk = clk_register_fixed_rate(NULL, "clk_mmci", NULL,
				      CLK_IS_ROOT, 25000000);
	/* PL180 MMCI */
	clk_register_clkdev(clk, NULL, "mmci");
}

#else /* !CONFIG_ARCH_AXXIA_SIM */

void __init
axxia_init_clocks(void)
{
	struct clk *clk;
	int i;
	struct device_node *np;
	u32 frequency;

	np = of_find_node_by_path("/clocks/cpu");

	if (np) {
		if (of_property_read_u32(np, "frequency", &frequency))
			pr_err("%d - Error!", __LINE__);
	}

	clk = clk_register_fixed_rate(NULL, "clk_cpu", NULL,
				      CLK_IS_ROOT, frequency);

	np = of_find_node_by_path("/clocks/peripheral");

	if (np) {
		if (of_property_read_u32(np, "frequency", &frequency))
			pr_err("%d - Error!", __LINE__);
	}

	clk = clk_register_fixed_rate(NULL, "clk_per", NULL,
				      CLK_IS_ROOT, frequency);

	/* PL011 UART0 */
	clk_register_clkdev(clk, NULL, "2010080000.uart");
	/* PL011 UART1 */
	clk_register_clkdev(clk, NULL, "2010081000.uart");
	/* PL011 UART2 */
	clk_register_clkdev(clk, NULL, "2010082000.uart");
	/* PL011 UART3 */
	clk_register_clkdev(clk, NULL, "2010083000.uart");
	/* PL022 SSP */
	clk_register_clkdev(clk, NULL, "ssp");
	/* I2C */
	clk_register_clkdev(clk, NULL, "2010084000.i2c");
	clk_register_clkdev(clk, NULL, "2010085000.i2c");
	clk_register_clkdev(clk, NULL, "2010086000.i2c");
	clk_register_clkdev(clk, NULL, "2010087000.i2c");
	/* SP804 timers */
	clk_register_clkdev(clk, NULL, "sp804");
	for (i = 0; i < 8; i++)
		clk_register_clkdev(clk, NULL, "axxia-timer%d", i);

	np = of_find_node_by_path("/clocks/emmc");

	if (np) {
		if (of_property_read_u32(np, "frequency", &frequency))
			pr_err("%d - Error!", __LINE__);
	}

	clk = clk_register_fixed_rate(NULL, "clk_mmci", NULL,
				      CLK_IS_ROOT, frequency);

	/* PL180 MMCI */
	clk_register_clkdev(clk, NULL, "mmci");

	/* APB clock dummy */
	clk = clk_register_fixed_rate(NULL, "apb_pclk", NULL,
				      CLK_IS_ROOT, AXXIA_SYS_CLOCK/2);
	clk_register_clkdev(clk, "apb_pclk", NULL);

	/* APB and System AXI clock from CPU_PLL */
	clk = clk_register_fixed_rate(NULL, "clk_pclk", NULL,
				      CLK_IS_ROOT, AXXIA_CPU_CLOCK/9);

	/* DDR3 (interface 1) clock from SMEM1_PLL */
	clk = clk_register_fixed_rate(NULL, "clk_smem1_2x", NULL,
				      CLK_IS_ROOT, AXXIA_DDR_CLOCK);

	return;
}

#endif /* CONFIG_ARCH_AXXIA_SIM */
