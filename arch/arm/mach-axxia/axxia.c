/*
 * arch/arm/mach-axxia/axxia.c
 *
 * Support for the LSI Axxia boards based on ARM cores.
 *
 * Copyright (C) 2012 LSI
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/amba/bus.h>
#include <linux/amba/mmci.h>
#include <linux/amba/pl022.h>
#include <linux/amba/pl061.h>
#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/smsc911x.h>
#include <linux/spi/spi.h>
#include <linux/clkdev.h>
#ifdef CONFIG_ARM_ARCH_TIMER
#include <asm/arch_timer.h>
#endif
#include <asm/mach-types.h>
#include <asm/sizes.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/gic.h>
#include <mach/timers.h>
#include <mach/axxia-gic.h>
#include "axxia.h"
#include "pci.h"
#include "i2c.h"

static const char *axxia_dt_match[] __initconst = {
	"lsi,axm5516",		/* AXM5516 */
	NULL
};


static void __iomem *ssp_base;

void __init axxia_dt_map_io(void)
{
}

void __init axxia_dt_init_early(void)
{
	 init_dma_coherent_pool_size(SZ_1M);
}

static struct of_device_id axxia_irq_match[] __initdata = {
	{
		.compatible = "arm,cortex-a15-gic",
		.data = gic_of_init,
	},
	{ }
};

static void __init axxia_dt_init_irq(void)
{
	of_irq_init(axxia_irq_match);
}

void __init axxia_dt_timer_init(void)
{
	const char *path;
	struct device_node *node;
	void __iomem *base;

	axxia_init_clocks();

#ifdef CONFIG_ARM_ARCH_TIMER
	{
		int err = arch_timer_of_register();
		if (err == 0)
			err = arch_timer_sched_clock_init();
		WARN_ON(err);
	}
#endif

	if (of_property_read_string(of_aliases, "timer", &path)) {
		WARN_ON(1);
		return;
	}

	node = of_find_node_by_path(path);
	if (WARN_ON(node == NULL))
		return;

	base = of_iomap(node, 0);
	if (WARN_ON(base == NULL))
		return;

	__sp804_clocksource_and_sched_clock_init(base, "axxia-timer0", 0);
	sp804_clockevents_init(base + 0x20,
			       irq_of_parse_and_map(node, 1),
			       "axxia-timer1");
}


static struct sys_timer axxia_dt_timer = {
	.init = axxia_dt_timer_init,
};

static struct mmci_platform_data mmc_plat_data = {
	.ocr_mask = MMC_VDD_32_33 | MMC_VDD_33_34,
	.status	  = NULL,
	.gpio_wp  = -ENOSYS,
	.gpio_cd  = -ENOSYS
};

struct pl061_platform_data gpio0_plat_data = {
	.gpio_base  = 0,
	.irq_base   = 0,
	.directions = 0,	/* startup directions, 1: out, 0: in */
	.values     = 0		/* startup values */
};

struct pl061_platform_data gpio1_plat_data = {
	.gpio_base  = 8,
	.irq_base   = 0,
	.directions = 0,	/* startup directions, 1: out, 0: in */
	.values     = 0		/* startup values */
};

static struct pl022_ssp_controller ssp_plat_data = {
	.bus_id         = 0,
	.num_chipselect = 5,
	.enable_dma     = 0
};

static struct of_dev_auxdata axxia_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,primecell", 0x20101E0000ULL,
		       "mmci",  &mmc_plat_data),
	OF_DEV_AUXDATA("arm,primecell", 0x2010088000ULL,
		       "ssp",   &ssp_plat_data),
	OF_DEV_AUXDATA("arm,primecell", 0x2010092000ULL,
		       "gpio0", &gpio0_plat_data),
	OF_DEV_AUXDATA("arm,primecell", 0x2010093000ULL,
		       "gpio1", &gpio1_plat_data),
	{}
};

static inline void
spidev_chip_select(u32 control, unsigned n)
{
	if (control == SSP_CHIP_SELECT)
		writel(~(1<<n) & 0x1F, ssp_base+0x30);
	else
		writel(0x1F, ssp_base+0x30);
}

static void spi_cs_eeprom0(u32 control) { spidev_chip_select(control, 0); }
static void spi_cs_eeprom1(u32 control) { spidev_chip_select(control, 1); }
static void spi_cs_eeprom2(u32 control) { spidev_chip_select(control, 2); }

struct pl022_config_chip spi_eeprom0 = {
	.iface      = SSP_INTERFACE_MOTOROLA_SPI,
	.com_mode   = POLLING_TRANSFER,
	.cs_control = spi_cs_eeprom0
};

struct pl022_config_chip spi_eeprom1 = {
	.iface      = SSP_INTERFACE_MOTOROLA_SPI,
	.com_mode   = POLLING_TRANSFER,
	.cs_control = spi_cs_eeprom1
};

struct pl022_config_chip spi_eeprom2 = {
	.iface      = SSP_INTERFACE_MOTOROLA_SPI,
	.com_mode   = POLLING_TRANSFER,
	.cs_control = spi_cs_eeprom2
};

static struct spi_board_info spi_devs[] __initdata = {
	{
		.modalias               = "spidev",
		.controller_data        = &spi_eeprom0,
		.bus_num                = 0,
		.chip_select            = 0,
		.max_speed_hz           = 12000000,
		.mode                   = SPI_MODE_0,
	},
	{
		.modalias               = "spidev",
		.controller_data        = &spi_eeprom1,
		.bus_num                = 0,
		.chip_select            = 1,
		.max_speed_hz           = 12000000,
		.mode                   = SPI_MODE_0,
	},
	{
		.modalias               = "spidev",
		.controller_data        = &spi_eeprom2,
		.bus_num                = 0,
		.chip_select            = 2,
		.max_speed_hz           = 12000000,
		.mode                   = SPI_MODE_0,
	},
};

void __init axxia_dt_init(void)
{
	l2x0_of_init(0x00400000, 0xfe0fffff);
	of_platform_populate(NULL, of_default_bus_match_table,
			     axxia_auxdata_lookup, NULL);
	pm_power_off = NULL; /* TBD */

	spi_register_board_info(spi_devs, ARRAY_SIZE(spi_devs));

	/*
	 * Setup PL022 to handle chip-select signal automatically
	 */
	ssp_base = of_iomap(of_find_compatible_node(NULL, NULL, "arm,pl022"),
			    0);
	if (!WARN_ON(ssp_base == NULL)) {
		/* Use legacy mode, bits 0..4 control nCS[0..4] pins */
		writel(0x1F, ssp_base+0x30);
	}

	/*axxia_pcie_init();*/
}

static void axxia_restart(char str, const char *cmd)
{
	void __iomem *base;

	base = ioremap(0x2010000000, 0x40000);

	writel(0x000000ab, base + 0x31000); /* Access Key */
	writel(0x00000040, base + 0x31004); /* Intrnl Boot, 0xffff0000 Target */
	writel(0x80000000, base + 0x3180c); /* Set ResetReadDone */
	writel(0x00080802, base + 0x31008); /* Chip Reset */
}

DT_MACHINE_START(AXXIA_DT, "LSI Axxia")
	.dt_compat	= axxia_dt_match,
	.map_io		= axxia_dt_map_io,
	.init_early	= axxia_dt_init_early,
	.init_irq	= axxia_dt_init_irq,
	.timer		= &axxia_dt_timer,
	.init_machine	= axxia_dt_init,
	.handle_irq	= axxia_gic_handle_irq,
	.restart	= axxia_restart,
#if defined(CONFIG_ZONE_DMA) && defined(CONFIG_ARM_LPAE)
	.dma_zone_size	= (4ULL * SZ_1G),
#endif
MACHINE_END
