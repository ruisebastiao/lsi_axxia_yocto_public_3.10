/*
 * linux/arch/arm/mach-axxia/rapidio.c
 *
 * Helper module for board specific RAPIDIO bus registration
 *
 * Copyright (C) 2013 LSI Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <asm/io.h>

#include <../../../drivers/misc/lsi-ncr.h>


/**
 * axxia_rapidio_board_init -
 *   Perform board-specific initialization to support use of RapidIO busses
 *
 * @dev:     [IN] RIO platform device
 * @ndx:     [IN] Which instance of SRIOC driver needs support
 * @portNdx: [OUT] Which port to use for the specified controller
 *
 * Returns 0 on success or an error code.
 */
int
axxia_rapidio_board_init(
	struct platform_device *dev,
	int devNum,
	int *portNdx)
{
	/* Reset the RIO port id to zero for this device */
	void __iomem *gpregBase = ioremap(0x2010094000, 0x1000);
	unsigned long reg = 0;

	if (gpregBase == NULL)
		return -EFAULT;

	reg = inl((long unsigned int)(gpregBase + 0x60));

	reg &= ~(0xf << (devNum * 4));

	outl_p(reg, (long unsigned int)(gpregBase + 0x60));

	(*portNdx) = 0;

	iounmap(gpregBase);

	/* Verify that this device is actually enabled */
	ncr_read(NCP_REGION_ID(0x115,0), 0x23c, 4, &reg);
	if ((reg & (1 << (21+(devNum*4)))) == 0) {
		printk("%s: SRIO%d link not ready\n",
			dev->dev.of_node->full_name, devNum);
		return -ENXIO;
	}

	return 0;
}
