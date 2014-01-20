/*
 * RapidIO quirk support - heavily inspired by drivers/pci/quirks.c
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/init.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/rio_ids.h>
#include <linux/rio_regs.h>

#include "rio.h"

static void rio_dev_do_fixups(struct rio_dev *rdev,
			      u16 destid,
			      u8 hopcount,
			      struct rio_dev_fixup *f,
			      struct rio_dev_fixup *end)
{
	while (f < end) {
		if ((f->vid == rdev->vid) && (f->did == rdev->did)) {
			pr_debug("RIO: calling %pF\n", f->fixup_hook);
			f->fixup_hook(rdev, destid, hopcount);
		}
		f++;
	}
}

void rio_fixup_dev(enum rio_fixup_pass pass,
		   struct rio_dev *rdev,
		   u16 destid,
		   u8 hopcount)
{
	struct rio_dev_fixup *start, *end;

	switch (pass) {
	case rio_fixup_early:
		start = __start_rio_dev_fixup_early;
		end = __end_rio_dev_fixup_early;
		break;

	case rio_fixup_enable:
		start = __start_rio_dev_fixup_enable;
		end = __end_rio_dev_fixup_enable;
		break;

	default:
		return;
	}
	rio_dev_do_fixups(rdev, destid, hopcount, start, end);
}
EXPORT_SYMBOL(rio_fixup_dev);
