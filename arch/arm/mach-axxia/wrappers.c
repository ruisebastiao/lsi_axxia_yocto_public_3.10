/*
 * arch/arm/mach-axxia/wrappers.c
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
#include <asm/irq.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/skbuff.h>
#include <linux/platform_device.h>

/*
  ==============================================================================
  ==============================================================================
  MDIO
  ==============================================================================
  ==============================================================================
*/

/*
  These are not wrappers, but are required when loading the RTE in simulation.
*/

#ifdef CONFIG_ARCH_AXXIA_SIM

/*
  ------------------------------------------------------------------------------
  acp_mdio_read
*/

int
acp_mdio_read(unsigned long address, unsigned long offset,
	      unsigned short *value, int clause45)
{
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
	return 0;
}

EXPORT_SYMBOL(acp_mdio_write);

#endif	/* CONFIG_ARCH_AXXIA_SIM */

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
  SKB
  ============================================================================
  ============================================================================
*/

/*
  ----------------------------------------------------------------------------
  acp_skb_tstamp_tx
*/

void
acp_skb_tstamp_tx(struct sk_buff *orig_skb,
		  struct skb_shared_hwtstamps *hwtstamps) {
	skb_tstamp_tx(orig_skb, hwtstamps);
}

EXPORT_SYMBOL(acp_skb_tstamp_tx);

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
acp_wrappers_init(void)
{
	printk(KERN_INFO "Initializing Axxia Wrappers.\n");

	return 0;
}

module_init(acp_wrappers_init);

MODULE_AUTHOR("LSI Corporation");
MODULE_DESCRIPTION("Timing Test");
MODULE_LICENSE("GPL");
