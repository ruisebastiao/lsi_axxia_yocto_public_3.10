/*
 *  linux/arch/arm/mach-axxia/axxia-gic.c
 *
 *  Cloned from linux/arch/arm/common/gic.c
 *
 *  Copyright (C) 2002 ARM Limited, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Interrupt architecture for the Axxia:
 *
 * o The Axxia chip can have up to four clusters, and each cluster has
 *   an ARM GIC interrupt controller.
 *
 * o In each GIC, there is one Interrupt Distributor, which receives
 *   interrupts from system devices and sends them to the Interrupt
 *   Controllers.
 *
 * o There is one CPU Interface per CPU, which sends interrupts sent
 *   by the Distributor, and interrupts generated locally, to the
 *   associated CPU. The base address of the CPU interface is usually
 *   aliased so that the same address points to different chips depending
 *   on the CPU it is accessed from.
 *
 * o The Axxia chip uses a distributed interrupt interface that's used
 *   for IPI messaging between clusters. Therefore, this design does not
 *   use the GIC software generated interrupts (0 - 16).
 *
 * Note that IRQs 0-31 are special - they are local to each CPU.
 * As such, the enable set/clear, pending set/clear and active bit
 * registers are banked per-cpu for these sources.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/smp.h>
#include <linux/cpu_pm.h>
#include <linux/cpumask.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/slab.h>

#include <asm/irq.h>
#include <asm/exception.h>
#include <asm/smp_plat.h>
#include <asm/mach/irq.h>
#include <asm/hardware/gic.h>

#include <mach/axxia-gic.h>

static u32 irq_cpuid[1020];
static void __iomem *ipi_mask_reg_base;
static void __iomem *ipi_send_reg_base;

/* AXM IPI numbers */
enum axxia_ext_ipi_num {
	IPI0_CPU0 = 227,	/* Axm IPI 195 */
	IPI0_CPU1,
	IPI0_CPU2,
	IPI0_CPU3,
	IPI1_CPU0,		/* Axm IPI 199 */
	IPI1_CPU1,
	IPI1_CPU2,
	IPI1_CPU3,
	IPI2_CPU0,		/* Axm IPI 203 */
	IPI2_CPU1,
	IPI2_CPU2,
	IPI2_CPU3,
	IPI3_CPU0,		/* Axm IPI 207 */
	IPI3_CPU1,
	IPI3_CPU2,
	IPI3_CPU3,
	MAX_AXM_IPI_NUM
};
static u32 mplx_ipi_num_45;
static u32 mplx_ipi_num_61;

union gic_base {
	void __iomem *common_base;
	void __percpu __iomem **percpu_base;
};

struct gic_chip_data {
	union gic_base dist_base;
	union gic_base cpu_base;
#ifdef CONFIG_CPU_PM
	u32 saved_spi_enable[DIV_ROUND_UP(1020, 32)];
	u32 saved_spi_conf[DIV_ROUND_UP(1020, 16)];
	u32 saved_spi_target[DIV_ROUND_UP(1020, 4)];
	u32 __percpu *saved_ppi_enable;
	u32 __percpu *saved_ppi_conf;
#endif
	struct irq_domain *domain;
	unsigned int gic_irqs;
};

static DEFINE_RAW_SPINLOCK(irq_controller_lock);

static struct gic_chip_data gic_data __read_mostly;

#define gic_data_dist_base(d)	((d)->dist_base.common_base)
#define gic_data_cpu_base(d)	((d)->cpu_base.common_base)
#define gic_set_base_accessor(d, f)

static inline void __iomem *gic_dist_base(struct irq_data *d)
{
	struct gic_chip_data *gic_data = irq_data_get_irq_chip_data(d);
	return gic_data_dist_base(gic_data);
}

static inline void __iomem *gic_cpu_base(struct irq_data *d)
{
	struct gic_chip_data *gic_data = irq_data_get_irq_chip_data(d);
	return gic_data_cpu_base(gic_data);
}

static inline unsigned int gic_irq(struct irq_data *d)
{
	return d->hwirq;
}

/*
 * Routines to acknowledge, disable and enable interrupts.
 */
struct gic_mask_irq_wrapper_struct {
	struct irq_data *d;
};

static void _gic_mask_irq(void *arg)
{
	struct irq_data *d = (struct irq_data *)arg;
	u32 mask = 1 << (gic_irq(d) % 32);

	raw_spin_lock(&irq_controller_lock);
	writel_relaxed(mask, gic_dist_base(d) + GIC_DIST_ENABLE_CLEAR
				+ (gic_irq(d) / 32) * 4);
	raw_spin_unlock(&irq_controller_lock);
}

static void gic_mask_irq(struct irq_data *d)
{
	u32 pcpu = cpu_logical_map(smp_processor_id());
	u32 irqid = gic_irq(d);

	if (irqid >= 1020)
		return;

	/* Don't mess with the AXM IPIs. */
	if ((irqid >= IPI0_CPU0) && (irqid < MAX_AXM_IPI_NUM))
		return;

	/* Deal with PPI interrupts directly. */
	if ((irqid > 16) && (irqid < 32)) {
		_gic_mask_irq(d);
		return;
	}

	/*
	 * If the cpu that this interrupt is assigned to falls within
	 * the same cluster as the cpu we're currently running on, do
	 * the IRQ masking directly. Otherwise, use the IPI mechanism
	 * to remotely do the masking.
	 */
	if ((cpu_logical_map(irq_cpuid[irqid]) / 4) == (pcpu / 4)) {
		_gic_mask_irq(d);
	} else {
		/*
		 * We are running here with local interrupts
		 * disabled. Temporarily re-enable them to
		 * avoid possible deadlock when calling
		 * smp_call_function_single().
		 */
		local_irq_enable();
		smp_call_function_single(irq_cpuid[irqid],
					 _gic_mask_irq,
					 d, 1);
		local_irq_disable();
	}
}

static void _gic_unmask_irq(void *arg)
{
	struct irq_data *d = (struct irq_data *)arg;
	u32 mask = 1 << (gic_irq(d) % 32);

	raw_spin_lock(&irq_controller_lock);
	writel_relaxed(mask, gic_dist_base(d) + GIC_DIST_ENABLE_SET
				+ (gic_irq(d) / 32) * 4);
	raw_spin_unlock(&irq_controller_lock);
}

static void gic_unmask_irq(struct irq_data *d)
{
	u32 pcpu = cpu_logical_map(smp_processor_id());
	u32 irqid = gic_irq(d);

	if (irqid >= 1020)
		return;

	/* Don't mess with the AXM IPIs. */
	if ((irqid >= IPI0_CPU0) && (irqid < MAX_AXM_IPI_NUM))
		return;

	/* Deal with PPI interrupts directly. */
	if ((irqid > 15) && (irqid < 32)) {
		_gic_unmask_irq(d);
		return;
	}

	/*
	 * If the cpu that this interrupt is assigned to falls within
	 * the same cluster as the cpu we're currently running on, do
	 * the IRQ masking directly. Otherwise, use the IPI mechanism
	 * to remotely do the masking.
	 */
	if ((cpu_logical_map(irq_cpuid[irqid]) / 4) == (pcpu / 4)) {
		_gic_unmask_irq(d);
	} else {
		/*
		 * We are running here with local interrupts
		 * disabled. Temporarily re-enable them to
		 * avoid possible deadlock when calling
		 * smp_call_function_single().
		 */
		local_irq_enable();
		smp_call_function_single(irq_cpuid[irqid],
					 _gic_unmask_irq,
					 d, 1);
		local_irq_disable();
	}
}

static void gic_eoi_irq(struct irq_data *d)
{
	/*
	 * This always runs on the same cpu that is handling
	 * an IRQ, so no need to worry about running this on
	 * remote clusters.
	 */
	writel_relaxed(gic_irq(d), gic_cpu_base(d) + GIC_CPU_EOI);
}

static int _gic_set_type(struct irq_data *d, unsigned int type)
{
	void __iomem *base = gic_dist_base(d);
	unsigned int gicirq = gic_irq(d);
	u32 enablemask = 1 << (gicirq % 32);
	u32 enableoff = (gicirq / 32) * 4;
	u32 confmask = 0x2 << ((gicirq % 16) * 2);
	u32 confoff = (gicirq / 16) * 4;
	bool enabled = false;
	u32 val;

	raw_spin_lock(&irq_controller_lock);

	val = readl_relaxed(base + GIC_DIST_CONFIG + confoff);
	if (type == IRQ_TYPE_LEVEL_HIGH)
		val &= ~confmask;
	else if (type == IRQ_TYPE_EDGE_RISING)
		val |= confmask;

	/*
	 * As recommended by the spec, disable the interrupt before changing
	 * the configuration.
	 */
	if (readl_relaxed(base + GIC_DIST_ENABLE_SET + enableoff)
			  & enablemask) {
		writel_relaxed(enablemask,
			       base + GIC_DIST_ENABLE_CLEAR + enableoff);
		enabled = true;
	}

	writel_relaxed(val, base + GIC_DIST_CONFIG + confoff);

	if (enabled)
		writel_relaxed(enablemask,
			       base + GIC_DIST_ENABLE_SET + enableoff);

	raw_spin_unlock(&irq_controller_lock);

	return 0;
}

#ifdef CONFIG_SMP
struct gic_set_type_wrapper_struct {
	struct irq_data *d;
	unsigned int type;
	int status;
};

static void gic_set_type_wrapper(void *data)
{
	struct gic_set_type_wrapper_struct *pArgs =
		(struct gic_set_type_wrapper_struct *)data;

	pArgs->status = _gic_set_type(pArgs->d, pArgs->type);
}
#endif

static int gic_set_type(struct irq_data *d, unsigned int type)
{
#ifdef CONFIG_SMP
	int i, cpu, nr_cluster_ids = ((nr_cpu_ids-1) / 4) + 1;
	unsigned int gicirq = gic_irq(d);
	u32 pcpu = cpu_logical_map(smp_processor_id());
	struct gic_set_type_wrapper_struct data;

	/* Interrupt configuration for SGIs can't be changed. */
	if (gicirq < 16)
		return -EINVAL;

	/* Interrupt configuration for the AXM IPIs can't be changed. */
	if ((gicirq >= IPI0_CPU0) && (gicirq < MAX_AXM_IPI_NUM))
		return -EINVAL;

	/* We only support two interrupt trigger types. */
	if (type != IRQ_TYPE_LEVEL_HIGH && type != IRQ_TYPE_EDGE_RISING)
		return -EINVAL;

	/*
	 * Duplicate IRQ type settings across all clusters. Run
	 * directly for this cluster, use IPI for all others.
	 */
	data.d = d;
	data.type = type;
	for (i = 0; i < nr_cluster_ids; i++) {
		if (i == (pcpu/4))
			continue;

		/* Have the first cpu in each cluster execute this. */
		cpu = i * 4;
		if (cpu_online(cpu)) {
			/*
			 * We are running here with local interrupts
			 * disabled. Temporarily re-enable them to
			 * avoid possible deadlock when calling
			 * smp_call_function_single().
			 */
			local_irq_enable();
			smp_call_function_single(cpu, gic_set_type_wrapper,
						 &data, 1);
			local_irq_disable();
			if (data.status != 0)
				pr_err("Failed to set IRQ type for cpu%d\n",
				       cpu);
		}
	}
#endif
	return _gic_set_type(d, type);
}

static int gic_retrigger(struct irq_data *d)
{
	return -ENXIO;
}

#ifdef CONFIG_SMP

/* Mechanism for forwarding IRQ affinity requests to other clusters. */
struct gic_set_affinity_wrapper_struct {
	struct irq_data *d;
	const struct cpumask *mask_val;
	bool disable;
};

static void _gic_set_affinity(void *data)
{
	struct gic_set_affinity_wrapper_struct *pArgs =
		(struct gic_set_affinity_wrapper_struct *)data;
	void __iomem *reg  = gic_dist_base(pArgs->d) +
			     GIC_DIST_TARGET + (gic_irq(pArgs->d) & ~3);
	unsigned int shift = (gic_irq(pArgs->d) % 4) * 8;
	unsigned int cpu = cpumask_any_and(pArgs->mask_val, cpu_online_mask);
	u32 val, affinity_mask, affinity_bit;
	u32 enable_mask, enable_offset;

	/*
	 * Normalize the cpu number as seen by Linux (0-15) to a
	 * number as seen by a cluster (0-3).
	 */
	affinity_bit = 1 << ((cpu_logical_map(cpu) % 4) + shift);
	affinity_mask = 0xff << shift;

	enable_mask = 1 << (gic_irq(pArgs->d) % 32);
	enable_offset = 4 * (gic_irq(pArgs->d) / 32);

	raw_spin_lock(&irq_controller_lock);
	val = readl_relaxed(reg) & ~affinity_mask;
	if (pArgs->disable == true) {
		writel_relaxed(val, reg);
		writel_relaxed(enable_mask, gic_data_dist_base(&gic_data)
				+ GIC_DIST_ENABLE_CLEAR + enable_offset);
	} else {
		writel_relaxed(val | affinity_bit, reg);
		writel_relaxed(enable_mask, gic_data_dist_base(&gic_data)
				+ GIC_DIST_ENABLE_SET + enable_offset);
	}
	raw_spin_unlock(&irq_controller_lock);
}

static int gic_set_affinity(struct irq_data *d,
			    const struct cpumask *mask_val,
			    bool force)
{
	unsigned int cpu = cpumask_any_and(mask_val, cpu_online_mask);
	u32 pcpu = cpu_logical_map(smp_processor_id());
	unsigned int irqid = gic_irq(d);
	struct gic_set_affinity_wrapper_struct data;

	if (cpu >= nr_cpu_ids)
		return -EINVAL;

	if (irqid >= 1020)
		return -EINVAL;

	/* Interrupt affinity for the AXM IPIs can't be changed. */
	if ((irqid >= IPI0_CPU0) && (irqid < MAX_AXM_IPI_NUM))
		return IRQ_SET_MASK_OK;

	/*
	 * If the new IRQ affinity is the same as current, then
	 * there's no need to update anything.
	 */
	if (cpu == irq_cpuid[irqid])
		return IRQ_SET_MASK_OK;

	/*
	 * If the new physical cpu assignment falls within the same
	 * cluster as the cpu we're currently running on, set the IRQ
	 * affinity directly. Otherwise, use the IPI mechanism.
	 */
	data.d = d;
	data.mask_val = mask_val;
	data.disable = false;

	if ((cpu_logical_map(cpu) / 4) == (pcpu / 4)) {
		_gic_set_affinity(&data);
	} else {
		/* Temporarily re-enable local interrupts. */
		local_irq_enable();
		smp_call_function_single(cpu, _gic_set_affinity, &data, 1);
		local_irq_disable();
	}

	/*
	 * If the new physical cpu assignment is on a cluster that's
	 * different than the prior cluster, remove the IRQ affinity
	 * on the old cluster.
	 */
	if ((cpu_logical_map(cpu) / 4) !=
		(cpu_logical_map(irq_cpuid[irqid]) / 4)) {
		/*
		 * If old cpu assignment falls within the same cluster as
		 * the cpu we're currently running on, set the IRQ affinity
		 * directly. Otherwise, use IPI mechanism.
		 */
		data.disable = true;
		if ((cpu_logical_map(irq_cpuid[irqid]) / 4) == (pcpu / 4)) {
			_gic_set_affinity(&data);
		} else {
			/* Temporarily re-enable local interrupts. */
			local_irq_enable();
			smp_call_function_single(irq_cpuid[irqid],
						 _gic_set_affinity, &data, 1);
			local_irq_disable();
		}
	}

	/* Update Axxia IRQ affinity table with the new logical CPU number. */
	irq_cpuid[irqid] = cpu;

	return IRQ_SET_MASK_OK;
}
#endif /* SMP */

#ifdef CONFIG_PM
static int gic_set_wake(struct irq_data *d, unsigned int on)
{
	int ret = -ENXIO;

	return ret;
}

#else
#define gic_set_wake	NULL
#endif

asmlinkage void __exception_irq_entry axxia_gic_handle_irq(struct pt_regs *regs)
{
	u32 irqstat, irqnr;
	u32 ipinum = 0;
	struct gic_chip_data *gic = &gic_data;
	void __iomem *cpu_base = gic_data_cpu_base(gic);

	do {
		irqstat = readl_relaxed(cpu_base + GIC_CPU_INTACK);
		irqnr = irqstat & ~0x1c00;

		if (likely(irqnr > 15 && irqnr < 1021)) {
			irqnr = irq_find_mapping(gic->domain, irqnr);

			/*
			 * Check if this is an external Axxia IPI interrupt.
			 * Translate to a standard ARM internal IPI number.
			 * The Axxia only has 4 IPI interrupts, so we
			 * multiplex IPI_CALL_FUNC and IPI_CALL_FUNC_SINGLE
			 * as one IPI. We also multiplex IPI_CPU_STOP and
			 * IPI_WAKEUP as one IPI.
			 *
			 * IPI0_CPUx = IPI_TIMER (2)
			 * IPI1_CPUx = IPI_RESCHEDULE (3)
			 * IPI2_CPUx = IPI_CALL_FUNC (4) /
			 *             IPI_CALL_FUNC_SINGLE (5)
			 * IPI3_CPUx = IPI_CPU_STOP (6) /
			 *             IPI_WAKEUP (1)
			 *
			 * Note that if the ipi_msg_type enum changes in
			 * arch/arm/kernel/smp.c then this will have to be
			 * updated as well.
			 */
			switch (irqnr) {
			case IPI0_CPU0:
			case IPI0_CPU1:
			case IPI0_CPU2:
			case IPI0_CPU3:
				ipinum = 2;
				break;

			case IPI1_CPU0:
			case IPI1_CPU1:
			case IPI1_CPU2:
			case IPI1_CPU3:
				ipinum = 3;
				break;

			case IPI2_CPU0:
			case IPI2_CPU1:
			case IPI2_CPU2:
			case IPI2_CPU3:
				ipinum = mplx_ipi_num_45; /* 4 or 5 */
				break;

			case IPI3_CPU0:
			case IPI3_CPU1:
			case IPI3_CPU2:
			case IPI3_CPU3:
				ipinum = mplx_ipi_num_61; /* 6 or 1 */
				break;

			default:
				/* Not an Axxia IPI */
				ipinum = 0;
				break;
			}

			if (ipinum > 1) { /* Ignore IPI_WAKEUP (1) */
				/*
				 * Write the original irq number to the
				 * EOI register to acknowledge the IRQ.
				 * No need to write CPUID field, since this
				 * is really a SPI interrupt, not a SGI.
				 */
				writel_relaxed(irqnr, cpu_base + GIC_CPU_EOI);
#ifdef CONFIG_SMP
				/* Do the normal IPI handling. */
				handle_IPI(ipinum, regs);
#endif

			} else {
				handle_IRQ(irqnr, regs);
			}
			continue;
		}
		if (irqnr < 16) {
			writel_relaxed(irqstat, cpu_base + GIC_CPU_EOI);
#ifdef CONFIG_SMP
			handle_IPI(irqnr, regs);
#endif
			continue;
		}
		break;
	} while (1);
}

static struct irq_chip gic_chip = {
	.name			= "GIC",
	.irq_mask		= gic_mask_irq,
	.irq_unmask		= gic_unmask_irq,
	.irq_eoi		= gic_eoi_irq,
	.irq_set_type		= gic_set_type,
	.irq_retrigger		= gic_retrigger,
#ifdef CONFIG_SMP
	.irq_set_affinity	= gic_set_affinity,
#endif
	.irq_set_wake		= gic_set_wake,
};

static void __init gic_axxia_init(struct gic_chip_data *gic)
{
	int i;
	u32 cpumask;

	/*
	 * Initialize the Axxia IRQ affinity table. All non-IPI
	 * interrupts are initially assigned to logical cpu 0.
	 */
	for (i = 0; i < 1020; i++)
		irq_cpuid[i] = 0;

	/* Unmask all Axxia IPI interrupts */
	cpumask = 0;
	for (i = 0; i < nr_cpu_ids; i++)
		cpumask |= 1 << i;
	for (i = 0; i < nr_cpu_ids; i++)
		writel_relaxed(cpumask, ipi_mask_reg_base + 0x40 + i * 4);
}

static void __cpuinit gic_dist_init(struct gic_chip_data *gic)
{
	unsigned int i;
	u32 cpumask;
	unsigned int gic_irqs = gic->gic_irqs;
	void __iomem *base = gic_data_dist_base(gic);
	u32 cpu = cpu_logical_map(smp_processor_id());
	u8 cpumask_8;
	u32 confmask;
	u32 confoff;
	u32 enablemask;
	u32 enableoff;
	u32 val;

	cpumask = 1 << cpu;
	cpumask |= cpumask << 8;
	cpumask |= cpumask << 16;

	writel_relaxed(0, base + GIC_DIST_CTRL);

	/*
	 * Set all global interrupts to be level triggered, active low.
	 */
	for (i = 32; i < gic_irqs; i += 16)
		writel_relaxed(0, base + GIC_DIST_CONFIG + i * 4 / 16);

	/*
	 * Set all global interrupts to this CPU only.
	 * (Only do this for the first core on cluster 0).
	 */
	if (cpu == 0)
		for (i = 32; i < gic_irqs; i += 4)
			writel_relaxed(cpumask,
				       base + GIC_DIST_TARGET + i * 4 / 4);

	/*
	 * Set priority on all global interrupts.
	 */
	for (i = 32; i < gic_irqs; i += 4)
		writel_relaxed(0xa0a0a0a0, base + GIC_DIST_PRI + i * 4 / 4);

	/*
	 * Disable all interrupts.  Leave the PPI and SGIs alone
	 * as these enables are banked registers.
	 */
	for (i = 32; i < gic_irqs; i += 32)
		writel_relaxed(0xffffffff,
			       base + GIC_DIST_ENABLE_CLEAR + i * 4 / 32);

	/*
	 * Set Axxia IPI interrupts for all CPUs in this cluster.
	 */
	for (i = IPI0_CPU0; i < MAX_AXM_IPI_NUM; i++) {
		cpumask_8 = 1 << ((i - IPI0_CPU0) % 4);
		writeb_relaxed(cpumask_8, base + GIC_DIST_TARGET + i);
	}

	/*
	 * Set Axxia IPI interrupts to be edge triggered.
	 */
	for (i = IPI0_CPU0; i < MAX_AXM_IPI_NUM; i++) {
		confmask = 0x2 << ((i % 16) * 2);
		confoff = (i / 16) * 4;
		val = readl_relaxed(base + GIC_DIST_CONFIG + confoff);
		val |= confmask;
		writel_relaxed(val, base + GIC_DIST_CONFIG + confoff);
	}

	/*
	 * Do the initial enable of the Axxia IPI interrupts here.
	 * NOTE: Writing a 0 to this register has no effect, so
	 * no need to read and OR in bits, just writing is OK.
	 */
	for (i = IPI0_CPU0; i < MAX_AXM_IPI_NUM; i++) {
		enablemask = 1 << (i % 32);
		enableoff = (i / 32) * 4;
		writel_relaxed(enablemask,
			       base + GIC_DIST_ENABLE_SET + enableoff);
	}

	writel_relaxed(1, base + GIC_DIST_CTRL);
}

static void __cpuinit gic_cpu_init(struct gic_chip_data *gic)
{
	void __iomem *dist_base = gic_data_dist_base(gic);
	void __iomem *base = gic_data_cpu_base(gic);
	int i;

	/*
	 * Deal with the banked PPI and SGI interrupts - disable all
	 * PPI interrupts, and also all SGI interrupts (we don't use
	 * SGIs in the Axxia).
	 */
	writel_relaxed(0xffffffff, dist_base + GIC_DIST_ENABLE_CLEAR);

	/*
	 * Set priority on PPI and SGI interrupts
	 */
	for (i = 0; i < 32; i += 4)
		writel_relaxed(0xa0a0a0a0,
			       dist_base + GIC_DIST_PRI + i * 4 / 4);

	writel_relaxed(0xf0, base + GIC_CPU_PRIMASK);
	writel_relaxed(1, base + GIC_CPU_CTRL);
}

#ifdef CONFIG_CPU_PM
/*
 * Saves the GIC distributor registers during suspend or idle.  Must be called
 * with interrupts disabled but before powering down the GIC.  After calling
 * this function, no interrupts will be delivered by the GIC, and another
 * platform-specific wakeup source must be enabled.
 */
static void gic_dist_save(void)
{
	unsigned int gic_irqs;
	void __iomem *dist_base;
	int i;

	gic_irqs = gic_data.gic_irqs;
	dist_base = gic_data_dist_base(&gic_data);

	if (!dist_base)
		return;

	for (i = 0; i < DIV_ROUND_UP(gic_irqs, 16); i++)
		gic_data.saved_spi_conf[i] =
			readl_relaxed(dist_base + GIC_DIST_CONFIG + i * 4);

	for (i = 0; i < DIV_ROUND_UP(gic_irqs, 4); i++)
		gic_data.saved_spi_target[i] =
			readl_relaxed(dist_base + GIC_DIST_TARGET + i * 4);

	for (i = 0; i < DIV_ROUND_UP(gic_irqs, 32); i++)
		gic_data.saved_spi_enable[i] =
			readl_relaxed(dist_base + GIC_DIST_ENABLE_SET + i * 4);
}

/*
 * Restores the GIC distributor registers during resume or when coming out of
 * idle.  Must be called before enabling interrupts.  If a level interrupt
 * that occured while the GIC was suspended is still present, it will be
 * handled normally, but any edge interrupts that occured will not be seen by
 * the GIC and need to be handled by the platform-specific wakeup source.
 */
static void gic_dist_restore(void)
{
	unsigned int gic_irqs;
	unsigned int i;
	void __iomem *dist_base;

	gic_irqs = gic_data.gic_irqs;
	dist_base = gic_data_dist_base(&gic_data);

	if (!dist_base)
		return;

	writel_relaxed(0, dist_base + GIC_DIST_CTRL);

	for (i = 0; i < DIV_ROUND_UP(gic_irqs, 16); i++)
		writel_relaxed(gic_data.saved_spi_conf[i],
			dist_base + GIC_DIST_CONFIG + i * 4);

	for (i = 0; i < DIV_ROUND_UP(gic_irqs, 4); i++)
		writel_relaxed(0xa0a0a0a0,
			dist_base + GIC_DIST_PRI + i * 4);

	for (i = 0; i < DIV_ROUND_UP(gic_irqs, 4); i++)
		writel_relaxed(gic_data.saved_spi_target[i],
			dist_base + GIC_DIST_TARGET + i * 4);

	for (i = 0; i < DIV_ROUND_UP(gic_irqs, 32); i++)
		writel_relaxed(gic_data.saved_spi_enable[i],
			dist_base + GIC_DIST_ENABLE_SET + i * 4);

	writel_relaxed(1, dist_base + GIC_DIST_CTRL);
}

static void gic_cpu_save(void)
{
	int i;
	u32 *ptr;
	void __iomem *dist_base;
	void __iomem *cpu_base;

	dist_base = gic_data_dist_base(&gic_data);
	cpu_base = gic_data_cpu_base(&gic_data);

	if (!dist_base || !cpu_base)
		return;

	ptr = __this_cpu_ptr(gic_data.saved_ppi_enable);
	for (i = 0; i < DIV_ROUND_UP(32, 32); i++)
		ptr[i] = readl_relaxed(dist_base + GIC_DIST_ENABLE_SET + i * 4);

	ptr = __this_cpu_ptr(gic_data.saved_ppi_conf);
	for (i = 0; i < DIV_ROUND_UP(32, 16); i++)
		ptr[i] = readl_relaxed(dist_base + GIC_DIST_CONFIG + i * 4);

}

static void gic_cpu_restore(void)
{
	int i;
	u32 *ptr;
	void __iomem *dist_base;
	void __iomem *cpu_base;

	dist_base = gic_data_dist_base(&gic_data);
	cpu_base = gic_data_cpu_base(&gic_data);

	if (!dist_base || !cpu_base)
		return;

	ptr = __this_cpu_ptr(gic_data.saved_ppi_enable);
	for (i = 0; i < DIV_ROUND_UP(32, 32); i++)
		writel_relaxed(ptr[i], dist_base + GIC_DIST_ENABLE_SET + i * 4);

	ptr = __this_cpu_ptr(gic_data.saved_ppi_conf);
	for (i = 0; i < DIV_ROUND_UP(32, 16); i++)
		writel_relaxed(ptr[i], dist_base + GIC_DIST_CONFIG + i * 4);

	for (i = 0; i < DIV_ROUND_UP(32, 4); i++)
		writel_relaxed(0xa0a0a0a0, dist_base + GIC_DIST_PRI + i * 4);

	writel_relaxed(0xf0, cpu_base + GIC_CPU_PRIMASK);
	writel_relaxed(1, cpu_base + GIC_CPU_CTRL);
}

static int _gic_notifier(struct notifier_block *self,
			 unsigned long cmd, void *v)
{
	int i;
	switch (cmd) {
	case CPU_PM_ENTER:
		gic_cpu_save();
		break;
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		gic_cpu_restore();
		break;
	case CPU_CLUSTER_PM_ENTER:
		gic_dist_save();
		break;
	case CPU_CLUSTER_PM_ENTER_FAILED:
	case CPU_CLUSTER_PM_EXIT:
		gic_dist_restore();
		break;
	}

	return NOTIFY_OK;
}

/* Mechanism for forwarding PM events to other clusters. */
struct gic_notifier_wrapper_struct {
	struct notifier_block *self;
	unsigned long cmd;
	void *v;
};

static void gic_notifier_wrapper(void *data)
{
	struct gic_notifier_wrapper_struct *pArgs =
		(struct gic_notifier_wrapper_struct *)data;

	_gic_notifier(pArgs->self, pArgs->cmd, pArgs->v);
}

static int gic_notifier(struct notifier_block *self, unsigned long cmd,	void *v)
{
	int i, cpu;
	struct gic_notifier_wrapper_struct data;
	int nr_cluster_ids = ((nr_cpu_ids-1) / 4) + 1;
	u32 pcpu = cpu_logical_map(smp_processor_id());

	/* Use IPI mechanism to execute this at other clusters. */
	data.self = self;
	data.cmd = cmd;
	data.v = v;
	for (i = 0; i < nr_cluster_ids; i++) {
		/* Skip the cluster we're already executing on - do last. */
		if ((pcpu/4) == i)
			continue;

		/* Have the first cpu in each cluster execute this. */
		cpu = i * 4;
		if (cpu_online(cpu)) {
			local_irq_enable();
			smp_call_function_single(cpu,
						 gic_notifier_wrapper,
						 &data, 0);
			local_irq_disable();
		}
	}

	/* Execute on this cluster. */
	_gic_notifier(self, cmd, v);

	return NOTIFY_OK;
	}

static struct notifier_block gic_notifier_block = {
	.notifier_call = gic_notifier,
};

static void __init gic_pm_init(struct gic_chip_data *gic)
{
	gic->saved_ppi_enable = __alloc_percpu(DIV_ROUND_UP(32, 32) * 4,
		sizeof(u32));
	BUG_ON(!gic->saved_ppi_enable);

	gic->saved_ppi_conf = __alloc_percpu(DIV_ROUND_UP(32, 16) * 4,
		sizeof(u32));
	BUG_ON(!gic->saved_ppi_conf);

	if (gic == &gic_data)
		cpu_pm_register_notifier(&gic_notifier_block);
}
#else
static void __init gic_pm_init(struct gic_chip_data *gic)
{
}
#endif /* CONFIG_CPU_PM */

static int gic_irq_domain_map(struct irq_domain *d, unsigned int irq,
				irq_hw_number_t hw)
{
	if (hw < 32) {
		irq_set_percpu_devid(irq);
		irq_set_chip_and_handler(irq, &gic_chip,
					 handle_percpu_devid_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_NOAUTOEN);
	} else {
		irq_set_chip_and_handler(irq, &gic_chip,
					 handle_fasteoi_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}
	irq_set_chip_data(irq, d->host_data);
	return 0;
}

static int gic_irq_domain_xlate(struct irq_domain *d,
				struct device_node *controller,
				const u32 *intspec,
				unsigned int intsize,
				unsigned long *out_hwirq,
				unsigned int *out_type)
{
	if (d->of_node != controller)
		return -EINVAL;
	if (intsize < 3)
		return -EINVAL;

	/* Get the interrupt number and add 16 to skip over SGIs */
	*out_hwirq = intspec[1] + 16;

	/* For SPIs, we need to add 16 more to get the GIC irq ID number */
	if (!intspec[0])
		*out_hwirq += 16;

	*out_type = intspec[2] & IRQ_TYPE_SENSE_MASK;
	return 0;
}

const struct irq_domain_ops gic_irq_domain_ops = {
	.map = gic_irq_domain_map,
	.xlate = gic_irq_domain_xlate,
};

void __init gic_init_bases(unsigned int gic_nr, int irq_start,
			   void __iomem *dist_base, void __iomem *cpu_base,
			   u32 percpu_offset, struct device_node *node)
{
	irq_hw_number_t hwirq_base;
	struct gic_chip_data *gic;
	int gic_irqs, irq_base;

	gic = &gic_data;

	/* Normal, sane GIC... */
	gic->dist_base.common_base = dist_base;
	gic->cpu_base.common_base = cpu_base;
	gic_set_base_accessor(gic, gic_get_common_base);

	/*
	 * For primary GICs, skip over SGIs.
	 * For secondary GICs, skip over PPIs, too.
	 */
	if ((irq_start & 31) > 0) {
		hwirq_base = 16;
		if (irq_start != -1)
			irq_start = (irq_start & ~31) + 16;
	} else {
		hwirq_base = 32;
	}

	/*
	 * Find out how many interrupts are supported.
	 * The GIC only supports up to 1020 interrupt sources.
	 */
	gic_irqs = readl_relaxed(gic_data_dist_base(gic) + GIC_DIST_CTR) & 0x1f;
	gic_irqs = (gic_irqs + 1) * 32;
	if (gic_irqs > 1020)
		gic_irqs = 1020;
	gic->gic_irqs = gic_irqs;

	gic_irqs -= hwirq_base; /* calculate # of irqs to allocate */
	irq_base = irq_alloc_descs(irq_start, 16, gic_irqs, numa_node_id());
	if (IS_ERR_VALUE(irq_base)) {
		WARN(1,
		 "Cannot allocate irq_descs @ IRQ%d, assuming pre-allocated\n",
		     irq_start);
		irq_base = irq_start;
	}
	gic->domain = irq_domain_add_legacy(node, gic_irqs, irq_base,
				    hwirq_base, &gic_irq_domain_ops, gic);
	if (WARN_ON(!gic->domain))
		return;

	gic_axxia_init(gic);
	gic_dist_init(gic);
	gic_cpu_init(gic);
	gic_pm_init(gic);
}

void __cpuinit axxia_gic_secondary_init(void)
{
	gic_cpu_init(&gic_data);
}


void __cpuinit axxia_gic_secondary_cluster_init(void)
{
	struct gic_chip_data *gic = &gic_data;

	/*
	 * Initialize the GIC distributor and cpu interfaces
	 * for secondary clusters in the Axxia SoC.
	 */

	gic_dist_init(gic);
	gic_cpu_init(gic);
}

#ifdef CONFIG_SMP
void axxia_gic_raise_softirq(const struct cpumask *mask, unsigned int irq)
{
	int cpu;
	unsigned long map = 0;
	unsigned int regoffset;
	u32 phys_cpu = cpu_logical_map(smp_processor_id());

	/* Sanity check the physical cpu number */
	if (phys_cpu >= nr_cpu_ids) {
		printk(KERN_ERR "Invalid cpu num (%d) >= max (%d)\n",
			phys_cpu, nr_cpu_ids);
		return;
	}

	/* Convert our logical CPU mask into a physical one. */
	for_each_cpu(cpu, mask)
		map |= 1 << cpu_logical_map(cpu);

	/*
	 * Convert the standard ARM IPI number (as defined in
	 * arch/arm/kernel/smp.c) to an Axxia IPI interrupt.
	 * The Axxia sends IPI interrupts to other cores via
	 * the use of "IPI send" registers. Each register is
	 * specific to a sending CPU and IPI number. For example:
	 * regoffset 0x0 = CPU0 uses to send IPI0 to other CPUs
	 * regoffset 0x4 = CPU0 uses to send IPI1 to other CPUs
	 * ...
	 * regoffset 0x1000 = CPU1 uses to send IPI0 to other CPUs
	 * regoffset 0x1004 = CPU1 uses to send IPI1 to other CPUs
	 * ...
	 */

	if (phys_cpu < 8)
		regoffset = phys_cpu * 0x1000;
	else
		regoffset = (phys_cpu - 8) * 0x1000 + 0x10000;

	switch (irq) {
	case 2: /* IPI_TIMER */
		regoffset += 0x0; /* Axxia IPI0 */
		break;

	case 3: /* IPI_RESCHEDULE */
		regoffset += 0x4; /* Axxia IPI1 */
		break;

	case 4: /* IPI_CALL_FUNC */
	case 5: /* IPI_CALL_FUNC_SINGLE */
		regoffset += 0x8; /* Axxia IPI2 */
		mplx_ipi_num_45 = irq;
		break;

	case 6: /* IPI_CPU_STOP */
	case 1: /* IPI_WAKEUP */
		regoffset += 0xC; /* Axxia IPI3 */
		mplx_ipi_num_61 = irq;
		break;

	default:
		/* Unknown ARM IPI */
		printk(KERN_ERR "Unknown ARM IPI num (%d)!\n", irq);
		return;
	}

	/*
	 * Ensure that stores to Normal memory are visible to the
	 * other CPUs before issuing the IPI.
	 */
	dsb();

	/* Axxia chip uses external SPI interrupts for IPI functionality. */
	writel_relaxed(map, ipi_send_reg_base + regoffset);
}
#endif /* SMP */

#ifdef CONFIG_OF

int __init gic_of_init(struct device_node *node, struct device_node *parent)
{
	void __iomem *cpu_base;
	void __iomem *dist_base;
	u32 percpu_offset;

	if (WARN_ON(!node))
		return -ENODEV;

	dist_base = of_iomap(node, 0);
	WARN(!dist_base, "unable to map gic dist registers\n");

	cpu_base = of_iomap(node, 1);
	WARN(!cpu_base, "unable to map gic cpu registers\n");

	ipi_mask_reg_base = of_iomap(node, 2);
	WARN(!ipi_mask_reg_base, "unable to map Axxia IPI mask registers\n");

	ipi_send_reg_base = of_iomap(node, 3);
	WARN(!ipi_send_reg_base, "unable to map Axxia IPI send registers\n");

	if (of_property_read_u32(node, "cpu-offset", &percpu_offset))
		percpu_offset = 0;

	gic_init_bases(0, -1, dist_base, cpu_base, percpu_offset, node);

	return 0;
}
#endif