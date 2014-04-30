/*
 * arch/arm/mach-axxia/ev.c
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

#include <linux/module.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <asm/page.h>
#include <asm/io.h>

static int initialized;
static int wfe_available;

inline void
__axxia_arch_wfe(void)
{
	if (0 == initialized) {
		if (of_find_compatible_node(NULL, NULL,
					    "lsi,axm5516-sim") != NULL ||
		    of_find_compatible_node(NULL, NULL,
					    "lsi,axm5516-emu") != NULL)
			wfe_available = 0;
		else
			wfe_available = 1;

		initialized = 1;
	}

	if (0 != wfe_available)
		wfe();

	return;
}
EXPORT_SYMBOL(__axxia_arch_wfe);
