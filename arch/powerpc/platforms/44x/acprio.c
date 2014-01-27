/*
 * arch/powerpc/platforms/44x/acprio.c
 *
 * Support for the RIO module on LSI Axxia boards
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/dmapool.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#include <asm/axxia-rio.h>


int axxia_rio_mcheck_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *entry;
	u32 mcsr = mfspr(SPRN_MCSR);

	if (mcsr & (PPC47x_MCSR_IPR | PPC47x_MCSR_L2)) {
		entry = search_exception_tables(regs->nip);
		if (entry) {
			pr_debug("(%s): Recoverable exception %lx\n",
				 __func__, regs->nip);
			regs->msr |= MSR_RI;
			regs->nip = entry->fixup;
			mcsr &= ~(PPC47x_MCSR_IPR | PPC47x_MCSR_L2);
			if (mcsr == MCSR_MCS)
				mcsr &= ~MCSR_MCS;
			mtspr(SPRN_MCSR, mcsr);
			return 1;
		}
	}
	return 0;
}

EXPORT_SYMBOL_GPL(axxia_rio_mcheck_exception);
