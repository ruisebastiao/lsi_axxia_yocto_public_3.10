/*
 *  Copyright (C) 2013 LSI Corporation
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/module.h>

#ifndef CONFIG_ARCH_AXXIA_SIM

#include <linux/cpu.h>
#include <linux/reboot.h>
#include <linux/syscore_ops.h>

#include <linux/proc_fs.h>

#include <asm/io.h>
#include <asm/cacheflush.h>
#include <../../../drivers/misc/lsi-ncr.h>

extern void flush_l3(void);
static void __iomem *nca_address;
static void __iomem *apb_base;

static inline void kill_time(int cnt)
{
	while (cnt--)
		;
}

unsigned long ncp_caal_regions_acp55xx[] = {
	NCP_REGION_ID(0x0b, 0x05),	/* SPPV2   */
	NCP_REGION_ID(0x0c, 0x05),	/* SED     */
	NCP_REGION_ID(0x0e, 0x05),	/* DPI_HFA */
	NCP_REGION_ID(0x14, 0x05),	/* MTM     */
	NCP_REGION_ID(0x14, 0x0a),	/* MTM2    */
	NCP_REGION_ID(0x15, 0x00),	/* MME     */
	NCP_REGION_ID(0x16, 0x05),	/* NCAV2   */
	NCP_REGION_ID(0x16, 0x10),	/* NCAV22  */
	NCP_REGION_ID(0x17, 0x05),	/* EIOAM1  */
	NCP_REGION_ID(0x19, 0x05),	/* TMGR    */
	NCP_REGION_ID(0x1a, 0x05),	/* MPPY    */
	NCP_REGION_ID(0x1a, 0x23),	/* MPPY2   */
	NCP_REGION_ID(0x1a, 0x21),	/* MPPY3   */
	NCP_REGION_ID(0x1b, 0x05),	/* PIC     */
	NCP_REGION_ID(0x1c, 0x05),	/* PAB     */
	NCP_REGION_ID(0x1f, 0x05),	/* EIOAM0  */
	NCP_REGION_ID(0x31, 0x05),	/* ISB     */
	NCP_REGION_ID(0x28, 0x05),	/* EIOASM0 */
	NCP_REGION_ID(0x29, 0x05),	/* EIOASM1 */
	NCP_REGION_ID(0x2a, 0x05),	/* EIOAS2  */
	NCP_REGION_ID(0x2b, 0x05),	/* EIOAS3  */
	NCP_REGION_ID(0x2c, 0x05),	/* EIOAS4  */
	NCP_REGION_ID(0x2d, 0x05),	/* EIOAS5  */
	NCP_REGION_ID(0x32, 0x05),	/* ISBS    */
	NCP_REGION_ID(0xff, 0xff)
};

static void quiesce_vp_engine(void)
{
	unsigned long *pCnalRegions = ncp_caal_regions_acp55xx;
	unsigned long *pRegion;
	unsigned ort, owt;
	unsigned long buf = 0;
	unsigned short node, target;
	int loop;

	printk(KERN_INFO "quiescing VP engines...\n");
	pRegion = pCnalRegions;
	while (*pRegion != NCP_REGION_ID(0xff, 0xff)) {

		/* set read/write transaction limits to zero */
		ncr_write(*pRegion, 0x8, 4, &buf);
		ncr_write(*pRegion, 0xc, 4, &buf);
		pRegion++;
	}

	pRegion = pCnalRegions;
	loop = 0;
	while (*pRegion != NCP_REGION_ID(0xff, 0xff)) {
		node = (*pRegion & 0xffff0000) >> 16;
		target = *pRegion & 0x0000ffff;
		/* read the number of outstanding read/write transactions */
		ncr_read(*pRegion, 0xf8, 4, &ort);
		ncr_read(*pRegion, 0xfc, 4, &owt);

		if ((ort == 0) && (owt == 0)) {
			/* this engine has been quiesced, move on to the next */
			printk(KERN_INFO "quiesced region 0x%02x.0x%02x\n",
				node, target);
			pRegion++;
		} else {
			if (loop++ > 10000) {
				printk(KERN_INFO
					"Unable to quiesce region 0x%02x.0x%02x ort=0x%x, owt=0x%x\n",
				     node, target, ort, owt);
				pRegion++;
				loop = 0;
				continue;
			}
		}
	}

	return;
}

static inline void ncp_ddr_shutdown(void)
{
	unsigned long value;
	int loop = 1;
	unsigned long cdr2[2] = { 0x00002200, 0x00000f00 };
	int smId;

	/*
	 * Most of the PIO command has already been set up.
	 * issue config ring write - enter DDR self-refresh mode
	 */

	for (smId = 0; smId < 2; smId++) {
		/* CDR2 - Node.target */
		ncr_register_write(cdr2[smId],
				   (unsigned *)(nca_address + 0xf8));
		/* CDR0 - */
		ncr_register_write(0x80050003,
				   (unsigned *)(nca_address + 0xf0));
		do {
			kill_time(100000);
			value =
			    ncr_register_read((unsigned *)(nca_address + 0xf0));
		} while ((0x80000000UL & value));
	}

	/* check interrupt status for completion */
	/* CDR1 - word offset 0x104 (byte offset 0x410) */
	ncr_register_write(0x00000104, (unsigned *)(nca_address + 0xf4));

	for (smId = 0; smId < 2; smId++) {
		/* CDR2 - Node.target */
		ncr_register_write(cdr2[smId],
				   (unsigned *)(nca_address + 0xf8));
		do {
			ncr_register_write(loop,
					   (unsigned *)(nca_address + 0x11f0));

			/* issue config ring read */
			ncr_register_write(0x80040003,
					   (unsigned *)(nca_address + 0xf0));
			do {
				kill_time(100000);
				value =
				    ncr_register_read((unsigned *)(nca_address +
								   0xf0));
			} while ((0x80000000UL & value));

			value =
			    ncr_register_read((unsigned *)(nca_address +
							   0x1000));
			ncr_register_write(value,
					   (unsigned *)(nca_address + 0x1200));

			loop++;
		} while ((value & 0x0200) == 0);
	}

	/* indicate DDR retention reset */
	writel(0x00000001, apb_base + 0x300dc);	/* set bit 0 of persist_scratch */

	/* issue chip reset */
	writel(0x00000040, apb_base + 0x31004);	/* Intrnl Boot, 0xffff0000 Target */
	writel(0x80000000, apb_base + 0x3180c);	/* Set ResetReadDone */
	writel(0x00080802, apb_base + 0x31008);	/* Chip Reset */

}

void initiate_retention_reset(void)
{
	unsigned long ctl_244 = 0;
	unsigned long value;

	if (NULL == nca_address)
		nca_address = ioremap(0x002020100000ULL, 0x20000);

	/* send stop message to other CPUs */
	local_irq_disable();
	asm volatile ("dsb":::"memory");
	asm volatile ("dmb":::"memory");
	system_state = SYSTEM_RESTART;
	smp_send_stop();

	kill_time(1000000);

	/* TODO - quiesce VP engines */
	quiesce_vp_engine();

	/* disable sysmem interrupts */
	printk("disabling sysmem interrupts\n");
	value = 0;
	ncr_write(NCP_REGION_ID(34, 0), 0x414, 4, &value);
	ncr_write(NCP_REGION_ID(15, 0), 0x414, 4, &value);

	/* flush L3 */
#if 0
	flush_cache_all();
	flush_l3();
#endif

	/* unlock reset register for later */
	apb_base = ioremap(0x2010000000, 0x40000);
	writel(0x000000ab, apb_base + 0x31000);	/* Access Key */

	/* prepare to put DDR in self refresh power-down mode */
	/* first read the CTL_244 register and OR in the LP_CMD value */
	ncr_read(NCP_REGION_ID(34, 0), 0x3d0, 4, &ctl_244);
	ctl_244 |= 0x000a0000;

	/*
	 * set up for CRBW operation
	 */
	/* write register value into CDAR[0] */
	ncr_register_write(ctl_244, (unsigned *)(nca_address + 0x1000));

	/* CDR2 - Node.target = 34.0 */
	ncr_register_write(0x00002200, (unsigned *)(nca_address + 0xf8));

	/* CDR1 - word offset 0xf4 (byte offset 0x3d0) */
	ncr_register_write(0x000000f4, (unsigned *)(nca_address + 0xf4));

	/*
	 * issue instruction barrier
	 * this should cause the next few instructions to be fetched
	 * into cache
	 */
	asm volatile ("dsb":::"memory");
	prefetch(ncp_ddr_shutdown);

	ncp_ddr_shutdown();

}

static ssize_t axxia_ddr_retention_trigger(struct file *file,
					   const char __user *buf,
					   size_t count, loff_t *ppos)
{
	initiate_retention_reset();
	return 0;
}

static const struct file_operations axxia_ddr_retention_proc_ops = {
	.write = axxia_ddr_retention_trigger,
	.llseek = noop_llseek,
};

void axxia_ddr_retention_init(void)
{
#ifndef CONFIG_ARCH_AXXIA_SIM
	if (!proc_create("driver/axxia_ddr_retention_reset", S_IWUSR, NULL,
			 &axxia_ddr_retention_proc_ops))
		printk(KERN_INFO
			"Failed to register DDR retention proc interface\n");
#endif
}

EXPORT_SYMBOL(initiate_retention_reset);

#else

void axxia_ddr_retention_init(void)
{
	return;
}

#endif