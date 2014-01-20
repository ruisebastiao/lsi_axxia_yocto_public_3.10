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

extern void axxia_secondary_startup(void);

#define APB2_SER3_PHY_ADDR    0x002010030000ULL
#define APB2_SER3_ADDR_SIZE   0x10000

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

	/* Send a wakeup IPI to get the idled cpu out of WFI state */
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));

	/* Wait for so long, then give up if nothing happens ... */
	timeout = jiffies + (1 * HZ);
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

static __init struct device_node *get_cpu_node(int cpu)
{
	struct device_node *np;

	for_each_node_by_type(np, "cpu") {
		u32 reg;
		if (of_property_read_u32(np, "reg", &reg))
			continue;
		if (reg == cpu_logical_map(cpu))
			return np;
	}

	return NULL;
}

static void __init axxia_smp_prepare_cpus(unsigned int max_cpus)
{
	void __iomem *apb2_ser3_base;
	int cpu_count = 0;
	int cpu;

	apb2_ser3_base = ioremap(APB2_SER3_PHY_ADDR, APB2_SER3_ADDR_SIZE);
	if (WARN_ON(!apb2_ser3_base))
		return;

	/*
	 * Initialise the present map, which describes the set of CPUs actually
	 * populated at the present time.
	 */
	for_each_possible_cpu(cpu) {
		struct device_node *np;
		u32 release_phys;
		u32 *release_virt;

		np = get_cpu_node(cpu);
		if (!np)
			continue;
		if (of_property_read_u32(np, "cpu-release-addr", &release_phys))
			continue;

		/*
		 * Release all physical cpus when not in hyp mode since we
		 * might want to bring them online later.
		 *
		 * Also we need to get the execution into kernel code (it's
		 * currently executing in u-boot).  u-boot releases the cores
		 * from reset in hyp mode.
		 */
		if (!is_hyp_mode_available()) {
			u32 phys_cpu = cpu_logical_map(cpu);
			if (phys_cpu != 0) {
				u32 tmp = readl(apb2_ser3_base + 0x1010);
				writel(0xab, apb2_ser3_base+0x1000);
				tmp &= ~(1 << phys_cpu);
				writel(tmp, apb2_ser3_base+0x1010);
			}
		}

		if (cpu_count < max_cpus) {
			set_cpu_present(cpu, true);
			cpu_count++;
		}

		/*
		 * This is the entry point of the routine that the secondary
		 * cores will execute once they are released from their
		 * "holding pen".
		 */
		if (release_phys != 0) {
			release_virt = (u32 *)phys_to_virt(release_phys);
			*release_virt = virt_to_phys(axxia_secondary_startup);
			smp_wmb();
			__cpuc_flush_dcache_area(release_virt, sizeof(u32));
		}
	}

	iounmap(apb2_ser3_base);
}

struct smp_operations axxia_smp_ops __initdata = {
	.smp_prepare_cpus	= axxia_smp_prepare_cpus,
	.smp_secondary_init	= axxia_secondary_init,
	.smp_boot_secondary	= axxia_boot_secondary,
};
