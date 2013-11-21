/*
 * linux/arch/arm/mach-axxia/i2c.c
 *
 * Helper module for board specific I2C bus registration
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
#include <linux/i2c.h>
#include <linux/i2c-axxia.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>

#include <mach/irqs.h>

#include "i2c.h"

/*****************************************************************************
* Local Definitions & State
*****************************************************************************/

static const char name[] = "axxia_ai2c";


static struct axxia_i2c_bus_platform_data       *axxia_i2cx_ports;
static unsigned int                              axxia_i2cx_port_count;
static struct platform_device                   *axxia_i2cx_devices;
static struct platform_device                  **axxia_i2cx_device_ptrs;


static inline
int
axxia_add_i2c_bus(
	struct device_node          *np,
	struct platform_device      *pdev,
	int                          ndx,
	int                          bus_id)
{
	struct axxia_i2c_bus_platform_data  *pdata;
	const u32                            pval;
	const char                          *val;
	int                                  portno;

	/* Get the port number from the device-tree */
	if (!of_property_read_u32(np, "port", (u32 *)&pval)) {
		portno = pval;
	} else {
		pr_err("I2C: Can't find port number for %s\n",
			np->full_name);
		return -ENXIO;
	}
	if (portno > axxia_i2cx_port_count) {
		pr_err("I2C: port number out of range for %s\n",
			np->full_name);
		return -ENXIO;
	}

	pdata = &axxia_i2cx_ports[ndx];
	pdata->node  = of_node_get(np);

	pdata->index = portno;

	/* Verify device type */
	val = of_get_property(np, "device_type", NULL);
	if (strcmp(val, "i2c")) {
		pr_err("I2C%d: missing or incorrect device_type for %s\n",
			portno, np->full_name);
		return -ENXIO;
	}

	/* Get or insert bus name */
	val = of_get_property(np, "bus_name", NULL);
	if (val)
		strncpy(pdata->name, val, MAX_AXXIA_I2C_HWMOD_NAME_LEN);
	else
		sprintf(pdata->name, "i2c%d", portno);

	pdata->rev = AXXIA_I2C_IP_VERSION_2;        /* AXM55xx */

	pdata->flags = AXXIA_I2C_FLAGS_NONE;

	/* Get the bus number from the device-tree */
	if (!of_property_read_u32(np, "bus", (u32 *)&pval))
		pdata->bus_nr = pval;
	else
		pdata->bus_nr = ~0;

	/* Fetch config space registers address */
	if (of_address_to_resource(np, 0, &pdata->dev_space)) {
		pr_err("%s: Can't get I2C device space !",
			np->full_name);
		return -ENXIO;
	}
	pdata->dev_space.flags = IORESOURCE_MEM;

	/* Hookup an interrupt handler -- TBD, maybe later */
	pdata->int_space.start = irq_of_parse_and_map(np, 0);
	pdata->int_space.flags = IORESOURCE_IRQ;

	if (pdata->bus_nr == ~0) {
		pr_info("I2C Port %d found; bus#=<auto> '%s'\n",
			portno, pdata->name);
	} else {
		pr_info("I2C Port %d found; bus#=i%d '%s'\n",
			portno, pdata->bus_nr, pdata->name);
	}
	pr_info("  dev_space start = 0x%012llx, end = 0x%012llx\n",
		pdata->dev_space.start, pdata->dev_space.end);
	pr_info("  mappedIrq#=%x\n", (unsigned int)pdata->int_space.start);

	/* Fill in the device */
	pdev->id = ndx;
	pdev->name = name;
	pdev->num_resources = 2;
	pdev->resource = &pdata->dev_space;
	pdev->dev.platform_data = pdata;

	return 0;
}


/**
 * axxia_register_i2c_busses - register I2C busses with device descriptors
 *
 * Returns 0 on success or an error code.
 */
int __init
axxia_register_i2c_busses(
	void)
{
	int                 i;
	int                 err;
	struct device_node *np;

	/* How many of these devices will be needed? */
	axxia_i2cx_port_count = 0;
	for_each_compatible_node(np, NULL, "lsi,api2c")
		axxia_i2cx_port_count++;

	if (axxia_i2cx_port_count == 0)
		return -ENXIO;

	/* Allocate memory */
	axxia_i2cx_ports = kzalloc(axxia_i2cx_port_count*
				   sizeof(struct axxia_i2c_bus_platform_data),
				   GFP_KERNEL);
	if (!axxia_i2cx_ports) {
		pr_warn("I2C: failed to allocate ports array\n");
		return -ENOMEM;
	}
	memset(axxia_i2cx_ports, 0,
	       axxia_i2cx_port_count*
	       sizeof(struct axxia_i2c_bus_platform_data));

	axxia_i2cx_devices = kzalloc(axxia_i2cx_port_count*
				     sizeof(struct platform_device),
				     GFP_KERNEL);
	if (!axxia_i2cx_devices) {
		pr_warn("I2C: failed to allocate devices array\n");
		return -ENOMEM;
	}
	memset(axxia_i2cx_devices, 0,
	       axxia_i2cx_port_count*sizeof(struct platform_device));

	axxia_i2cx_device_ptrs = kzalloc(axxia_i2cx_port_count*
					 sizeof(struct platform_device *),
					 GFP_KERNEL);
	if (!axxia_i2cx_device_ptrs) {
		pr_warn("I2C: failed to allocate device ptrs array\n");
		return -ENOMEM;
	}
	memset(axxia_i2cx_device_ptrs, 0,
	       axxia_i2cx_port_count*sizeof(struct platform_device *));

	/* Now parse and fill in the device entries */
	i = 0;
	for_each_compatible_node(np, NULL, "lsi,api2c")
	{
	    axxia_i2cx_device_ptrs[i] = &axxia_i2cx_devices[i];

	    err = axxia_add_i2c_bus(np, axxia_i2cx_device_ptrs[i],
				    i, i+ARCH_AXXIA_MAX_I2C_BUS_NR);
	    if (err == 0)
		i++;
	}

	return platform_add_devices(axxia_i2cx_device_ptrs, i);
}
