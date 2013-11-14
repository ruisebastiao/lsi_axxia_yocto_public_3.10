/*
 *  Copyright (C) 2013 LSI Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Error monitor for system memory.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#include "lsi-ncr.h"

#ifndef CONFIG_ARCH_AXXIA
#error "Only AXM55xx is Supported At Present!"
#endif

static int log = 1;
module_param(log, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(log, "Log each error on the console.");

/*
  AXM55xx Interrupt Status Bits

  Bit [24] = The software-initiated control word write has completed.
  Bit [23] = The user-initiated DLL resync has completed.
  Bit [22] = A state change has been detected on the dfi_init_complete signal
	     after initialization.
  Bit [21] = The assertion of the INHIBIT_DRAM_CMD parameter has successfully
	     inhibited the command queue.
  Bit [20] = The register interface-initiated mode register write has completed
	     and another mode register write may be issued.
  Bit [19] = A parity error has been detected on the address/control bus on a
	     registered DIMM.
  Bit [18] = The leveling operation has completed.
  Bit [17] = A leveling operation has been requested.
  Bit [16] = A DFI update error has occurred. Error information can be found in
	     the UPDATE_ERROR_STATUS parameter.
  Bit [15] = A write leveling error has occurred. Error information can be found
	     in the WRLVL_ERROR_STATUS parameter.
  Bit [14] = A read leveling gate training error has occurred. Error information
	     can be found in the RDLVL_ERROR_STATUS parameter.
  Bit [13] = A read leveling error has occurred. Error information can be found
	     in the RDLVL_ERROR_STATUS parameter.
  Bit [12] = The user has programmed an invalid setting associated with user
	     words per burst. Examples: Setting param_reduc when burst
	     length = 2. A 1:2 MC:PHY clock ratio with burst length = 2.
  Bit [11] = A wrap cycle crossing a DRAM page has been detected. This is
	     unsupported & may result in memory data corruption.
  Bit [10] = The BIST operation has been completed.
  Bit [09] = The low power operation has been completed.
  Bit [08] = The MC initialization has been completed.
  Bit [07] = An error occurred on the port command channel.
  Bit [06] = Multiple uncorrectable ECC events have been detected.
  Bit [05] = An uncorrectable ECC event has been detected.
  Bit [04] = Multiple correctable ECC events have been detected.
  Bit [03] = A correctable ECC event has been detected.
  Bit [02] = Multiple accesses outside the defined PHYSICAL memory space have
	     occurred.
  Bit [01] = A memory access outside the defined PHYSICAL memory space has
	     occurred.
  Bit [00] = The memory reset is valid on the DFI bus.

  Of these, 1, 2, 3, 4, 5, 6, 7, 11, and 19 are of interest.
*/

struct smmon_counts {
	unsigned long illegal_access[2];
	unsigned long multiple_illegal_access[2];
	unsigned long correctable_ecc[2];
	unsigned long multiple_correctable_ecc[2];
	unsigned long uncorrectable_ecc[2];
	unsigned long multiple_uncorrectable_ecc[2];
	unsigned long port_error[2];
	unsigned long wrap_error[2];
	unsigned long parity_error[2];
};

static struct smmon_counts counts;

DEFINE_RAW_SPINLOCK(counts_lock);

#define SUMMARY_SIZE 512
static char *summary;
module_param(summary, charp, S_IRUGO);
MODULE_PARM_DESC(summary, "A Summary of the Current Error Counts.");

/*
  ------------------------------------------------------------------------------
  update_summary
*/

static void
update_summary(void)
{
	memset(summary, 0, SUMMARY_SIZE);
	sprintf(summary,
		"------------ Counts for SM0/SM1 ----------\n"
		"                   Illegal Access: %lu/%lu\n"
		"        Multiple Illegal Accesses: %lu/%lu\n"
		"            Correctable ECC Error: %lu/%lu\n"
		"  Multiple Correctable ECC Errors: %lu/%lu\n"
		"          Uncorrectable ECC Error: %lu/%lu\n"
		"Multiple Uncorrectable ECC Errors: %lu/%lu\n"
		"                      Port Errors: %lu/%lu\n"
		"                      Wrap Errors: %lu/%lu\n"
		"                    Parity Errors: %lu/%lu\n",
		counts.illegal_access[0],
		counts.illegal_access[1],
		counts.multiple_illegal_access[0],
		counts.multiple_illegal_access[1],
		counts.correctable_ecc[0],
		counts.correctable_ecc[1],
		counts.multiple_correctable_ecc[0],
		counts.multiple_correctable_ecc[1],
		counts.uncorrectable_ecc[0],
		counts.uncorrectable_ecc[1],
		counts.multiple_uncorrectable_ecc[0],
		counts.multiple_uncorrectable_ecc[1],
		counts.port_error[0],
		counts.port_error[1],
		counts.wrap_error[0],
		counts.wrap_error[1],
		counts.parity_error[0],
		counts.parity_error[1]);

	return;
}

/*
  ------------------------------------------------------------------------------
  smmon_isr
*/

static irqreturn_t
smmon_isr(int interrupt, void *device)
{
	unsigned long status;
	unsigned long region;
	int rc;
	int sm;

	if ((32 + 161) == interrupt) {
		region = NCP_REGION_ID(0x22, 0);
		sm = 1;
	} else if ((32 + 160) == interrupt) {
		region = NCP_REGION_ID(0xf, 0);
		sm = 0;
	} else {
		return IRQ_NONE;
	}

	rc = ncr_read(region, 0x410, 4, &status);

	if (0 != rc) {
		printk(KERN_ERR
		       "smmon(%d): Error reading interrupt status!\n", sm);

		return IRQ_NONE;
	}

	raw_spin_lock(&counts_lock);

	if (0 != (0x00000002 & status) || 0 != (0x00000004 & status))
		printk(KERN_ERR
		       "smmon(%d): Illegal Access!\n", sm);

	if (0 != (0x00000002 & status))
		++counts.illegal_access[sm];

	if (0 != (0x00000004 & status))
		++counts.multiple_illegal_access[sm];

	if ((0 != (0x00000008 & status) ||
	     0 != (0x00000010 & status)) &&
	    0 != log)
		printk(KERN_NOTICE
		       "smmon(%d): Correctable ECC Error!\n", sm);

	if (0 != (0x00000008 & status))
		++counts.correctable_ecc[sm];

	if (0 != (0x00000010 & status))
		++counts.multiple_correctable_ecc[sm];

	if ((0 != (0x00000020 & status) ||
	     0 != (0x00000040 & status)) &&
	    0 != log)
		printk(KERN_CRIT
		       "smmon(%d): Uncorrectable ECC Error!\n", sm);

	if (0 != (0x00000020 & status))
		++counts.uncorrectable_ecc[sm];

	if (0 != (0x00000040 & status))
		++counts.multiple_uncorrectable_ecc[sm];

	if (0 != (0x00000080 & status)) {
		++counts.port_error[sm];

		if (0 != log)
			printk(KERN_CRIT
			       "smmon(%d): Port Error!\n", sm);
	}

	if (0 != (0x00000800 & status)) {
		++counts.wrap_error[sm];

		if (0 != log)
			printk(KERN_CRIT
			       "smmon(%d): Wrap Error!\n", sm);
	}

	if (0 != (0x00080000 & status)) {
		++counts.parity_error[sm];

		if (0 != log)
			printk(KERN_CRIT
			       "smmon(%d): Parity Error!\n", sm);
	}

	update_summary();

	raw_spin_unlock(&counts_lock);

	ncr_write(region, 0x548, 4, &status);

	return IRQ_HANDLED;
}

/*
  ==============================================================================
  ==============================================================================
  Linux Interface
  ==============================================================================
  ==============================================================================
*/

/*
  ------------------------------------------------------------------------------
  smmon_init
*/

static int __init
smmon_init(void)
{
	int rc;
	int mask;

	summary = kmalloc(SUMMARY_SIZE, GFP_KERNEL);

	if (NULL == summary)
		return -ENOMEM;

	update_summary();

	memset(&counts, 0, sizeof(struct smmon_counts));

	/*
	  Set the interrupt mask for each controller.
	*/

	mask = 0x1f7f701;
	ncr_write(NCP_REGION_ID(0x22, 0), 0x414, 4, &mask);
	ncr_write(NCP_REGION_ID(0xf, 0), 0x414, 4, &mask);

	rc = request_irq(32 + 161, smmon_isr, IRQF_ONESHOT,
			 "smmon_0", NULL);
	rc |= request_irq(32 + 160, smmon_isr, IRQF_ONESHOT,
			  "smmon_1", NULL);

	if (0 != rc)
		return -EBUSY;

	printk(KERN_INFO
	       "lsi_smmon: Monitoring System Memory\n");

	return 0;
}

module_init(smmon_init);

/*
  ------------------------------------------------------------------------------
  smmon_exit
*/

static void __exit
smmon_exit(void)
{
	free_irq(32 + 161, NULL);
	free_irq(32 + 160, NULL);

	kfree(summary);

	printk(KERN_INFO
	       "lsi_smmon: Not Monitoring System Memory\n");

	return;
}

module_exit(smmon_exit);
