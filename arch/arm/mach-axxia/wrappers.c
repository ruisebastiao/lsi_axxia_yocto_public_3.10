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

/*
  ==============================================================================
  ==============================================================================
  Platform Device Registration
  ==============================================================================
  ==============================================================================
*/

#include <linux/platform_device.h>

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
  ==============================================================================
  ==============================================================================
  Interrupts
  ==============================================================================
  ==============================================================================
*/

#include <linux/irqdomain.h>

/*
  ------------------------------------------------------------------------------
  acp_irq_create_mapping
*/

unsigned int
acp_irq_create_mapping(struct irq_domain *host, irq_hw_number_t hwirq)
{
        return irq_create_mapping(host, hwirq);
}

EXPORT_SYMBOL(acp_irq_create_mapping);
