/*
 * arch/powerpc/sysdev/lsi_acp_wrappers.c
 *
 * Copyright (C) 2013 LSI
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
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/of.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/lsi/acp_ncr.h>

MODULE_AUTHOR("LSI Corporation");
MODULE_DESCRIPTION("ACP Wrappers");
MODULE_LICENSE("GPL");

/*
  ==============================================================================
  ==============================================================================
  Platform Device Registration
  ==============================================================================
  ==============================================================================
*/

/*
  ------------------------------------------------------------------------------
  acp_platform_device_register
*/

int
acp_platform_device_register(struct platform_device *pdev)
{
	return platform_device_register(pdev);
}

EXPORT_SYMBOL(acp_platform_device_register);

/*
  ------------------------------------------------------------------------------
  acp_platform_device_unregister
*/

void
acp_platform_device_unregister(struct platform_device *pdev)
{
	platform_device_unregister(pdev);

	return;
}

EXPORT_SYMBOL(acp_platform_device_unregister);

/*
  ============================================================================
  ============================================================================
  MDIO Access
  ============================================================================
  ============================================================================
*/


static unsigned long mdio_base;
DEFINE_SPINLOCK(mdio_lock);

#define MDIO_CONTROL_RD_DATA ((void *)(mdio_base + 0x0))
#define MDIO_STATUS_RD_DATA  ((void *)(mdio_base + 0x4))
#define MDIO_CLK_OFFSET      ((void *)(mdio_base + 0x8))
#define MDIO_CLK_PERIOD      ((void *)(mdio_base + 0xc))

/*
 * -------------------------------------------------------------------------
 * acp_mdio_read
 */

int acp_mdio_read(unsigned long address, unsigned long offset,
		  unsigned short *value)
{
	unsigned long command = 0;
	unsigned long status;
	unsigned long flags;

	spin_lock_irqsave(&mdio_lock, flags);

	/* Set the mdio_busy (status) bit. */
	status = in_le32(MDIO_STATUS_RD_DATA);
	status |= 0x40000000;
	out_le32(MDIO_STATUS_RD_DATA, status);

	/* Write the command.*/
	command |= 0x10000000;	/* op_code: read */
	command |= (address & 0x1f) << 16; /* port_addr (target device) */
	command |= (offset & 0x1f) << 21; /* device_addr (target register) */
	out_le32(MDIO_CONTROL_RD_DATA, command);

	/* Wait for the mdio_busy (status) bit to clear. */
	do {
		status = in_le32(MDIO_STATUS_RD_DATA);
	} while (0 != (status & 0x40000000));

	/* Wait for the mdio_busy (control) bit to clear. */
	do {
		command = in_le32(MDIO_CONTROL_RD_DATA);
	} while (0 != (command & 0x80000000));

	*value = (unsigned short)(command & 0xffff);

	spin_unlock_irqrestore(&mdio_lock, flags);

	return 0;
}
EXPORT_SYMBOL(acp_mdio_read);

/*
 * -------------------------------------------------------------------------
 * acp_mdio_write
 */

int acp_mdio_write(unsigned long address, unsigned long offset,
		   unsigned short value)
{
	unsigned long command = 0;
	unsigned long status;
	unsigned long flags;

	spin_lock_irqsave(&mdio_lock, flags);

	/* Wait for mdio_busy (control) to be clear. */
	do {
		command = in_le32(MDIO_CONTROL_RD_DATA);
	} while (0 != (command & 0x80000000));

	/* Set the mdio_busy (status) bit. */
	status = in_le32(MDIO_STATUS_RD_DATA);
	status |= 0x40000000;
	out_le32(MDIO_STATUS_RD_DATA, status);

	/* Write the command. */
	command = 0x08000000;	/* op_code: write */
	command |= (address & 0x1f) << 16; /* port_addr (target device) */
	command |= (offset & 0x1f) << 21; /* device_addr (target register) */
	command |= (value & 0xffff); /* value */
	out_le32(MDIO_CONTROL_RD_DATA, command);

	/* Wait for the mdio_busy (status) bit to clear. */
	do {
		status = in_le32(MDIO_STATUS_RD_DATA);
	} while (0 != (status & 0x40000000));

	/* Wait for the mdio_busy (control) bit to clear. */
	do {
		command = in_le32(MDIO_CONTROL_RD_DATA);
	} while (0 != (command & 0x80000000));

	spin_unlock_irqrestore(&mdio_lock, flags);

	return 0;
}
EXPORT_SYMBOL(acp_mdio_write);

/*
 * -------------------------------------------------------------------------
 * acp_mdio_initialize
 */

static int acp_mdio_initialize(void)
{
	if (is_asic()) {
		out_le32(MDIO_CLK_OFFSET, 0x10);
		out_le32(MDIO_CLK_PERIOD, 0x2c);
	} else {
		out_le32(MDIO_CLK_OFFSET, 0x05);
		out_le32(MDIO_CLK_PERIOD, 0x0c);
	}

	return 0;
}


/*
  ============================================================================
  ============================================================================
  Interrupts
  ============================================================================
  ============================================================================
*/

/*
 * -------------------------------------------------------------------------
 * acp_irq_create_mapping
 */
unsigned int acp_irq_create_mapping(struct irq_domain *host,
				    irq_hw_number_t hwirq)
{
	unsigned int mapped_irq;

	preempt_disable();
	mapped_irq = irq_create_mapping(host, hwirq);
	preempt_enable();

	return mapped_irq;
}
EXPORT_SYMBOL(acp_irq_create_mapping);

/*
  ============================================================================
  ============================================================================
  Spin Locks
  ============================================================================
  ============================================================================
*/

/*
 * -------------------------------------------------------------------------
 * acp_spin_lock_init
 */

void acp_spin_lock_init(spinlock_t *lock)
{
	spin_lock_init(lock);
}
EXPORT_SYMBOL(acp_spin_lock_init);

/*
 * -------------------------------------------------------------------------
 * acp_spin_lock
 */

void acp_spin_lock(spinlock_t *lock)
{
	spin_lock(lock);
}
EXPORT_SYMBOL(acp_spin_lock);

/*
 * -------------------------------------------------------------------------
 * acp_spin_unlock
 */

void acp_spin_unlock(spinlock_t *lock)
{
	spin_unlock(lock);
}
EXPORT_SYMBOL(acp_spin_unlock);

/*
 * -------------------------------------------------------------------------
 * acp_spin_lock_bh
 */

void acp_spin_lock_bh(spinlock_t *lock)
{
	spin_lock_bh(lock);
}
EXPORT_SYMBOL(acp_spin_lock_bh);

/*
 * -------------------------------------------------------------------------
 * acp_spin_unlock_bh
 */

void acp_spin_unlock_bh(spinlock_t *lock)
{
	spin_unlock_bh(lock);
}
EXPORT_SYMBOL(acp_spin_unlock_bh);

/*
 * -------------------------------------------------------------------------
 * acp_spin_lock_irqsave
 */

void acp_spin_lock_irqsave(spinlock_t *lock, unsigned long flags)
{
	spin_lock_irqsave(lock, flags);
}
EXPORT_SYMBOL(acp_spin_lock_irqsave);

/*
 * -------------------------------------------------------------------------
 * acp_spin_unlock_irqrestore
 */

void acp_spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
	spin_unlock_irqrestore(lock, flags);
}
EXPORT_SYMBOL(acp_spin_unlock_irqrestore);

/*
 * -------------------------------------------------------------------------
 * acp_wrappers_init
 */

int __init acp_wrappers_init(void)
{
	int rc = -1;
	struct device_node *np = NULL;
	const u32 *field;
	u64 mdio_phys_address;
	u32 mdio_size;

	pr_info("Initializing ACP Wrappers.\n");

	np = of_find_node_by_type(np, "network");

	while (np && !of_device_is_compatible(np, "acp-femac"))
		np = of_find_node_by_type(np, "network");

	if (np) {
		field = of_get_property(np, "enabled", NULL);

		if (!field || (field && (0 == *field))) {
			pr_warn("Networking is Not Enabled.\n");
			goto acp_wrappers_init_done;
		}

		field = of_get_property(np, "mdio-reg", NULL);

		if (!field) {
			pr_err("Couldn't get \"mdio-reg\" property.\n");
		} else {
			mdio_phys_address = of_translate_address(np, field);
			mdio_size = field[1];
			rc = 0;
		}
	}

	if (0 != rc) {
		mdio_phys_address = 0x002000409000ULL;
		mdio_size = 0x1000;
		pr_warn("** MDIO Address Not Specified in Device Tree.\n");
	}

	mdio_base = (unsigned long)ioremap(mdio_phys_address, mdio_size);
	rc = acp_mdio_initialize();

	if (0 != rc)
		pr_err("MDIO Initiailzation Failed!\n");

acp_wrappers_init_done:

	return 0;
}

module_init(acp_wrappers_init);