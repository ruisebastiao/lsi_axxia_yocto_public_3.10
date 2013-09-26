/*
 * drivers/net/ethernet/lsi/lsi_acp_mdio.c
 *
 * Copyright (C) 2010 LSI
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307	 USA
 */

#include <linux/module.h>
#include <linux/of.h>
#include <asm/irq.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/irqdomain.h>
#include <linux/skbuff.h>
#include <linux/platform_device.h>

/*
  ==============================================================================
  ==============================================================================
  MDIO Access
  ==============================================================================
  ==============================================================================
*/

#ifndef CONFIG_ACPISS

#define BZ33327_WA

static unsigned long mdio_base;
static DEFINE_SPINLOCK(mdio_lock);

#define MDIO_CONTROL_RD_DATA ((void *)(mdio_base + 0x0))
#define MDIO_STATUS_RD_DATA  ((void *)(mdio_base + 0x4))
#define MDIO_CLK_OFFSET      ((void *)(mdio_base + 0x8))
#define MDIO_CLK_PERIOD      ((void *)(mdio_base + 0xc))

#ifdef CONFIG_ARM
#define READ(a) readl((a))
#define WRITE(a, v) writel((v), (a))
#else
#define READ(a) in_le32((a))
#define WRITE(a, v) out_le32((a), (v))
#endif

/*
  ------------------------------------------------------------------------------
  acp_mdio_read
*/

int
acp_mdio_read(unsigned long address, unsigned long offset,
	      unsigned short *value, int clause45)
{
	unsigned long command = 0;
	unsigned long status;
	unsigned long flags;

	spin_lock_irqsave(&mdio_lock, flags);
#if defined(BZ33327_WA)
	/* Set the mdio_busy (status) bit. */
	status = READ(MDIO_STATUS_RD_DATA);
	status |= 0x40000000;
	WRITE(MDIO_STATUS_RD_DATA, status);
#endif /* BZ33327_WA */

	if (clause45 == 0) {
		/* Write the command. */
		command = 0x10000000;              /* op_code: read */
		command |= (address & 0x1f) << 16; /* port_addr (tgt device) */
		command |= (offset & 0x1f) << 21;  /* device_addr (tgt reg) */
		WRITE(MDIO_CONTROL_RD_DATA, command);
	} else {
		/*
		 * Step 1: Write the address.
		 */

		/* Write the address */
		command = 0x20000000;                    /* clause_45 = 1 */
		command |= 0x00000000;                   /* op_code: 0 */
		command |= 0x04000000;                   /* interface_sel = 1 */
		command |= ((offset & 0x1f000000) >> 3); /* device_addr (target
							    device_type) */
		command |= (address & 0x1f) << 16;       /* port_addr (target
							    device) */
		command |= (offset & 0xffff);            /* addr_or_data (target
							    register) */
		WRITE(MDIO_CONTROL_RD_DATA, command);

		/* Wait for the mdio_busy (status) bit to clear. */
		do {
			status = READ(MDIO_STATUS_RD_DATA);
		} while (0 != (status & 0x40000000));

		/* Wait for the mdio_busy (control) bit to clear. */
		do {
			command = READ(MDIO_CONTROL_RD_DATA);
		} while (0 != (command & 0x80000000));

		/*
		 * Step 2: Read the value.
		 */

		/* Set the mdio_busy (status) bit. */
		status = READ(MDIO_STATUS_RD_DATA);
		status |= 0x40000000;
		WRITE(MDIO_STATUS_RD_DATA, status);

		command = 0x20000000;                    /* clause_45 = 1 */
		command |= 0x10000000;                   /* op_code: read */
		command |= 0x04000000;                   /* interface_sel = 1 */
		command |= ((offset & 0x1f000000) >> 3); /* device_addr (target
							    device_type) */
		command |= (address & 0x1f) << 16;       /* port_addr (target
							    device) */
		WRITE(MDIO_CONTROL_RD_DATA, command);
	}

#if defined(BZ33327_WA)
	/* Wait for the mdio_busy (status) bit to clear. */
	do {
		status = READ(MDIO_STATUS_RD_DATA);
	} while (0 != (status & 0x40000000));
#endif				/* BZ33327_WA */

	/* Wait for the mdio_busy (control) bit to clear. */
	do {
		command = READ(MDIO_CONTROL_RD_DATA);
	} while (0 != (command & 0x80000000));

	*value = (unsigned short)(command & 0xffff);
	spin_unlock_irqrestore(&mdio_lock, flags);

	return 0;
}
EXPORT_SYMBOL(acp_mdio_read);

/*
  ------------------------------------------------------------------------------
  acp_mdio_write
*/

int
acp_mdio_write(unsigned long address, unsigned long offset,
	       unsigned short value, int clause45)
{
	unsigned long command = 0;
	unsigned long status;
	unsigned long flags;

	spin_lock_irqsave(&mdio_lock, flags);

	/* Wait for mdio_busy (control) to be clear. */
	do {
		command = READ(MDIO_CONTROL_RD_DATA);
	} while (0 != (command & 0x80000000));

#if defined(BZ33327_WA)
	/* Set the mdio_busy (status) bit. */
	status = READ(MDIO_STATUS_RD_DATA);
	status |= 0x40000000;
	WRITE(MDIO_STATUS_RD_DATA, status);
#endif /* BZ33327_WA */

	if (clause45 == 0) {
		/* Write the command. */
		command = 0x08000000;              /* op_code: write */
		command |= (address & 0x1f) << 16; /* port_addr (tgt device) */
		command |= (offset & 0x1f) << 21;  /* device_addr (tgt reg) */
		command |= (value & 0xffff);       /* value */
		WRITE(MDIO_CONTROL_RD_DATA, command);
	} else {
		/*
		 * Step 1: Write the address.
		 */

		/* Write the address */
		command = 0x20000000;                    /* clause_45 = 1 */
		command |= 0x00000000;                   /* op_code: 0 */
		command |= 0x04000000;                   /* interface_sel = 1 */
		command |= ((offset & 0x1f000000) >> 3); /* device_addr (target
							    device_type) */
		command |= (address & 0x1f) << 16;       /* port_addr (target
							    device) */
		command |= (offset & 0xffff);            /* addr_or_data (target
							    register) */
		WRITE(MDIO_CONTROL_RD_DATA, command);

		/* Wait for the mdio_busy (status) bit to clear. */
		do {
			status = READ(MDIO_STATUS_RD_DATA);
		} while (0 != (status & 0x40000000));

		/* Wait for the mdio_busy (control) bit to clear. */
		do {
			command = READ(MDIO_CONTROL_RD_DATA);
		} while (0 != (command & 0x80000000));

		/*
		 * Step 2: Write the value.
		 */

		/* Set the mdio_busy (status) bit. */
		status = READ(MDIO_STATUS_RD_DATA);
		status |= 0x40000000;
		WRITE(MDIO_STATUS_RD_DATA, status);

		command = 0x20000000;                    /* clause_45 = 1 */
		command |= 0x08000000;                   /* op_code: write */
		command |= 0x04000000;                   /* interface_sel = 1 */
		command |= ((offset & 0x1f000000) >> 3); /* device_addr (target
							    device_type) */
		command |= (address & 0x1f) << 16;       /* port_addr (target
							    device) */
		command |= (value & 0xffff);             /* addr_or_data=value*/
		WRITE(MDIO_CONTROL_RD_DATA, command);
	}

#if defined(BZ33327_WA)
	/* Wait for the mdio_busy (status) bit to clear. */
	do {
		status = READ(MDIO_STATUS_RD_DATA);
	} while (0 != (status & 0x40000000));
#endif	/* BZ33327_WA */

	/* Wait for the mdio_busy (control) bit to clear. */
	do {
		command = READ(MDIO_CONTROL_RD_DATA);
	} while (0 != (command & 0x80000000));

	spin_unlock_irqrestore(&mdio_lock, flags);

	return 0;
}
EXPORT_SYMBOL(acp_mdio_write);

/*
  ------------------------------------------------------------------------------
  acp_mdio_initialize
*/

static int
acp_mdio_initialize(void)
{
#ifdef CONFIG_ARM
	WRITE(MDIO_CLK_OFFSET, 0x1c);
	WRITE(MDIO_CLK_PERIOD, 0xf0);
#else
	WRITE(MDIO_CLK_OFFSET, 0x10);
	WRITE(MDIO_CLK_PERIOD, 0x2c);
#endif

	return 0;
}

#endif /* ! CONFIG_ACPISS */

/*
  ==============================================================================
  ==============================================================================
  Linux Stuff
  ==============================================================================
  ==============================================================================
*/

/*
  ------------------------------------------------------------------------------
  acp_wrappers_init
*/

int __init
acp_mdio_init(void)
{
	int rc = -ENODEV;
	struct device_node *np = NULL;
	const u32 *field;
	u64 mdio_address;
	u32 mdio_size;

	pr_info("Initializing Axxia Wrappers.\n");

#ifndef CONFIG_ACPISS
	np = of_find_node_by_type(np, "network");

	while (np &&
	       !of_device_is_compatible(np, "lsi,acp-femac") &&
	       !of_device_is_compatible(np, "acp-femac"))
		np = of_find_node_by_type(np, "network");

	if (!np)
		goto error;

	field = of_get_property(np, "mdio-reg", NULL);

	if (!field)
		goto error;

	mdio_address = of_translate_address(np, field);
	mdio_size = field[1];
	mdio_base = (unsigned long)ioremap(mdio_address, mdio_size);
	rc = acp_mdio_initialize();
#else
	rc = 0;
#endif

error:
	return rc;
}

module_init(acp_mdio_init);

MODULE_AUTHOR("LSI Corporation");
MODULE_DESCRIPTION("Timing Test");
MODULE_LICENSE("GPL");
