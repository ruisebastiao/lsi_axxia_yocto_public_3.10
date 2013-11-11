/*
 *  linux/arch/arm/mach-axxia/platsmp.c
 *
 *  Copyright (C) 2012 LSI
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <asm/smp_plat.h>
#include <asm/cacheflush.h>
#include <linux/irqchip/arm-gic.h>
#include <asm/mach/map.h>
#include <asm/virt.h>

#include <mach/axxia-gic.h>

/*
 * Control for which core is the next to come out of the secondary
 * boot "holding pen".
 */
//volatile int __cpuinitdata pen_release = -1;

extern void axxia_secondary_startup(void);

#define APB2_SER3_PHY_ADDR    0x002010030000ULL
#define APB2_SER3_ADDR_SIZE   0x10000


/*
  flush_l3
*/
static void flush_l3(void)
{
#if 0
	unsigned long hnf_offsets[] = {
		 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27
	};
	int i;
	unsigned long status;
	int retries;
	void __iomem *dickens;

	dickens = ioremap(0x2000000000, 0x1000000);
#if 0
	for (i = 0; i < (sizeof(hnf_offsets) / sizeof(unsigned long)); ++i) {
		/* set state NOL3 */
		writel(0x0, dickens + (0x10000 * hnf_offsets[i]) + 0x10);
	}

	for (i = 0; i < (sizeof(hnf_offsets) / sizeof(unsigned long)); ++i) {
		retries = 10000;

		do {
			status = readl(dickens +
				       (0x10000 * hnf_offsets[i]) + 0x18);
			udelay(1);
		} while ((0 < --retries) && (0x0 != (status & 0xf)));

		if (0 == retries)
			BUG();
	}
#endif

	for (i=0;i < (sizeof(hnf_offsets) / sizeof(unsigned long)); ++i) {
		status = readl(dickens + (0x10000 * hnf_offsets[i]) + 0x18);

		/* If L3 not already set to FAM, set it to FAM*/
		if ((status & 0xf) != 0xc) {
			printk("JL: Switching L3s to FAM \n\r");

			for (i = 0; i < (sizeof(hnf_offsets) /
					 sizeof(unsigned long)); ++i) {
				/* set state FAM */
				writel(0x3,
				       (dickens +
					(0x10000 * hnf_offsets[i]) + 0x10));
			}

			for (i = 0; i < (sizeof(hnf_offsets) /
					 sizeof(unsigned long)); ++i) {
				retries = 10000;

				do {
					status = readl(dickens +
						       (0x10000 *
							hnf_offsets[i]) + 0x18);
					udelay(1);
				} while ((0 < --retries) &&
					 (0xc != (status & 0xf)));

				if (0 == retries)
					BUG();
			}
		}
	}

	iounmap(dickens);
#endif
	return;
}

/*
 * Write pen_release in a way that is guaranteed to be visible to all
 * observers, irrespective of whether they're taking part in coherency
 * or not.  This is necessary for the hotplug code to work reliably.
 */
static void __cpuinit write_pen_release(int val)
{
	pen_release = val;
	smp_wmb();
	__cpuc_flush_dcache_area((void *)&pen_release, sizeof(pen_release));
	outer_clean_range(__pa(&pen_release), __pa(&pen_release + 1));
	flush_l3();
}

static DEFINE_RAW_SPINLOCK(boot_lock);

void __cpuinit axxia_secondary_init(unsigned int cpu)
{
	/*
	 * If this isn't the first physical core in a secondary cluster
	 * then run the standard GIC secondary init routine. Otherwise,
	 * run the Axxia secondary cluster init routine.
	 */
	if (cpu_logical_map(cpu) % 4)
		axxia_gic_secondary_init();
	else
		axxia_gic_secondary_cluster_init();

	/*
	 * Let the primary processor know we're out of the
	 * pen, then head off into the C entry point.
	 */
	write_pen_release(-1);

	/*
	 * Synchronise with the boot thread.
	 */
	_raw_spin_lock(&boot_lock);
	_raw_spin_unlock(&boot_lock);
}

int __cpuinit axxia_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;
	int phys_cpu, cluster;

	/*
	 * Set synchronisation state between this boot processor
	 * and the secondary one.
	 */
	_raw_spin_lock(&boot_lock);

	/*
	 * In the Axxia, the bootloader does not put the secondary cores
	 * into a wait-for-event (wfe) or wait-for-interrupt (wfi) state
	 * because of the multi-cluster design (i.e., there's no way for
	 * the primary core in cluster 0 to send an event or interrupt
	 * to secondary cores in the other clusters).
	 *
	 * Instead, the secondary cores are immediately put into a loop
	 * that polls the "pen_release" global and MPIDR register. The two
	 * are compared and if they match, a secondary core then executes
	 * the Axxia secondary startup code.
	 *
	 * Here we convert the "cpu" variable to be compatible with the
	 * ARM MPIDR register format (CLUSTERID and CPUID):
	 *
	 * Bits:   |11 10 9 8|7 6 5 4 3 2|1 0
	 *         | CLUSTER | Reserved  |CPU
	 */
	phys_cpu = cpu_logical_map(cpu);
	cluster = (phys_cpu / 4) << 8;
	phys_cpu = cluster + (phys_cpu % 4);

	/* Release the specified core */
	write_pen_release(phys_cpu);

#ifdef CONFIG_HOTPLUG_CPU
	/* Send a wakeup IPI to get the idled cpu out of WFI state */
	axxia_gic_raise_softirq(cpumask_of(cpu), 1);
#endif

	/* Wait for so long, then give up if nothing happens ... */
#ifdef CONFIG_ARCH_AXXIA_SIM
	timeout = jiffies + (1 * HZ);
#else
	timeout = jiffies + (10 * HZ);
#endif
	while (time_before(jiffies, timeout)) {
		smp_rmb();
		if (pen_release == -1)
			break;

		udelay(10);
	}

	/*
	 * Now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish.
	 */
	_raw_spin_unlock(&boot_lock);

	return pen_release != -1 ? -ENOSYS : 0;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
static void __init axxia_smp_init_cpus(void)
{
	int ncores = 0;
	struct device_node *np;
	u32 cpu_num;

	for_each_node_by_name(np, "cpu") {
		if (++ncores > nr_cpu_ids) {
			pr_warn("SMP: More cores (%u) in DTB than max (%u)\n",
				ncores, nr_cpu_ids);
			break;
		}
		if (!of_property_read_u32(np, "reg", &cpu_num)) {
			if (cpu_num >= 0 && cpu_num < 16)
				set_cpu_possible(cpu_num, true);
			else
				pr_warn("SMP: Invalid cpu number (%u)\n",
					 cpu_num);
		}
	}

	//set_smp_cross_call(axxia_gic_raise_softirq);
}

static void __init axxia_smp_prepare_cpus(unsigned int max_cpus)
{
#ifdef CONFIG_ARCH_AXXIA_SIM
	int i;

	/*
	 * Initialise the present map, which describes the set of CPUs
	 * actually populated at the present time.
	 */
	for (i = 0; i < max_cpus; i++)
		set_cpu_present(i, true);

	/*
	 * This is the entry point of the routine that the secondary
	 * cores will execute once they are released from their
	 * "holding pen".
	 */
	*(u32 *)phys_to_virt(0x10000020) =
					  virt_to_phys(axxia_secondary_startup);
#else
	int i;
	int cpu_count = 0;
	u32 phys_cpu = 0;
	void __iomem *apb2_ser3_base;
	unsigned long resetVal;
	struct device_node *np;
	unsigned long release_addr[NR_CPUS] = {0};
	u32 release;

	if (of_find_compatible_node(NULL, NULL, "lsi,axm5516")) {
		for_each_node_by_name(np, "cpu") {
			if (of_property_read_u32(np, "reg", &phys_cpu))
				continue;

			if (0 == phys_cpu)
				continue;

			if (of_property_read_u32(np, "cpu-release-addr",
						 &release))
				continue;

			release_addr[phys_cpu] = release;
			printk(KERN_DEBUG
			       "%s:%d - set address for %d to 0x%08lx\n",
			       __FILE__, __LINE__,
			       phys_cpu, release_addr[phys_cpu]);
		}

		/*
		 * Initialise the present map, which describes the set of CPUs
		 * actually populated at the present time.
		 */

		apb2_ser3_base = ioremap(APB2_SER3_PHY_ADDR, APB2_SER3_ADDR_SIZE);

		for (i = 0; i < NR_CPUS; i++) {
			/* check if this is a possible CPU and
			 * it is within max_cpus range */
			if ((cpu_possible(i)) &&
				(cpu_count < max_cpus) &&
			    (0 != release_addr[i])) {
				set_cpu_present(cpu_count, true);
				cpu_count++;
			}

			if (!is_hyp_mode_available()) {
				/* Release all physical cpu:s when not in hyp mode
				 * since we might want to bring them online later.
				 * Also we need to get the execution into kernel
				 * code (it's currently executing in u-boot).
				 * u-boot releases the cores from reset in hyp mode.
				 */
				phys_cpu = cpu_logical_map(i);

				if (phys_cpu != 0) {
					resetVal = readl(apb2_ser3_base + 0x1010);
					writel(0xab, apb2_ser3_base+0x1000);
					resetVal &= ~(1 << phys_cpu);
					writel(resetVal, apb2_ser3_base+0x1010);
					udelay(1000);
				}
			}
		}

		iounmap(apb2_ser3_base);

		/*
		 * This is the entry point of the routine that the secondary
		 * cores will execute once they are released from their
		 * "holding pen".
		 */
		for(i = 0; i < NR_CPUS; i++) {
			if(release_addr[i] != 0) {
				u32* vrel_addr =
					(u32 *)phys_to_virt(release_addr[i]);
				*vrel_addr =
					virt_to_phys(axxia_secondary_startup);
				smp_wmb();
				__cpuc_flush_dcache_area(vrel_addr, sizeof(u32));
			}
		}
	} else if (of_find_compatible_node(NULL, NULL, "lsi,axm5516-sim")) {
		for (i = 0; i < max_cpus; i++)
			set_cpu_present(i, true);

		/*
		 * This is the entry point of the routine that the secondary
		 * cores will execute once they are released from their
		 * "holding pen".
		 */
		*(u32 *)phys_to_virt(0x10000020) =
			virt_to_phys(axxia_secondary_startup);
		smp_wmb();
		__cpuc_flush_dcache_area((void *)phys_to_virt(0x10000020), sizeof(u32));
	}
	return;
#endif
}

struct smp_operations __initdata axxia_smp_ops = {
	.smp_init_cpus		= axxia_smp_init_cpus,
	.smp_prepare_cpus	= axxia_smp_prepare_cpus,
	.smp_secondary_init	= axxia_secondary_init,
	.smp_boot_secondary	= axxia_boot_secondary,
};
