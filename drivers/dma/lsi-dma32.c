/*
 * Driver for the LSI DMA controller DMA-32.
 *
 * The driver is based on:
 *
 * lsi-dma.c - Copyright 2011 Mentor Graphics
 * acp_gpdma.c - Copyright (c) 2011, Ericsson AB
 *               Niclas Bengtsson <niklas.x.bengtsson@ericsson.com>
 *               Kerstin Jonsson <kerstin.jonsson@ericsson.com>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/export.h>
#include <linux/stat.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kref.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <asm/page.h>
#include <linux/bitops.h>
#include <linux/atomic.h>
#include "lsi-dma32.h"

#define rd32(_addr)         readl((_addr))
#define wr32(_value, _addr) writel((_value), (_addr))

#ifdef DEBUG
#define engine_dbg(engine, fmt, ...) \
	do { \
		struct gpdma_engine *_e = engine; \
		(void)_e; \
		printk(KERN_DEBUG "dma0: " fmt, ##__VA_ARGS__); \
	} while (0)

#define ch_dbg(dmac, fmt, ...) \
	do { \
		struct gpdma_channel *_c = dmac; \
		(void)_c; \
		printk(KERN_DEBUG "dma0ch%d: [%s] " fmt, \
			dmac->channel, __func__, ##__VA_ARGS__); \
	} while (0)
#else
#define engine_dbg(engine, fmt, ...) do {} while (0)
#define ch_dbg(dmac, fmt, ...)       do {} while (0)
#endif


static dma_cookie_t gpdma_tx_submit(struct dma_async_tx_descriptor *txd);


static void reset_channel(struct gpdma_channel *dmac)
{
	const int WAIT = 1024;
	int i;

	/* Pause channel */
	wr32(DMA_STATUS_CH_PAUS_WR_EN | DMA_STATUS_CH_PAUSE,
	     dmac->base+DMA_STATUS);
	wmb();

	/* Disable channel */
	wr32(0, dmac->base+DMA_CHANNEL_CONFIG);
	for (i = 0; rd32(dmac->base+DMA_CHANNEL_CONFIG) && i < WAIT; i++)
		cpu_relax();
	if (i == WAIT)
		ch_dbg(dmac, "Failed to DISABLE channel\n");

	/* Clear FIFO */
	wr32(DMA_CONFIG_CLEAR_FIFO, dmac->base+DMA_CHANNEL_CONFIG);
	for (i = 0; rd32(dmac->base+DMA_CHANNEL_CONFIG) && i < WAIT; i++)
		cpu_relax();
	if (i == WAIT)
		ch_dbg(dmac, "Failed to clear FIFO\n");
}

static void soft_reset(struct gpdma_engine *engine)
{
	int i;
	u32 cfg;

	/* Reset all channels */
	for (i = 0; i < engine->chip->num_channels; i++)
		reset_channel(&engine->channel[i]);

	/* Reset GPDMA by writing Magic Number to reset reg */
	wr32(GPDMA_MAGIC, engine->gbase + SOFT_RESET);
	wmb();

	cfg = (engine->pool.phys & 0xfff00000) | GEN_CONFIG_EXT_MEM;

	if (engine->chip->flags & LSIDMA_EDGE_INT) {
		for (i = 0; i < engine->chip->num_channels; i++)
			cfg |= GEN_CONFIG_INT_EDGE(i);
		engine_dbg(engine, "Using edge-triggered interrupts\n");
	}
	wr32(cfg, engine->gbase + GEN_CONFIG);
	engine_dbg(engine, "engine->desc.phys & 0xfff00000 == %llx\n",
		   (engine->pool.phys & 0xfff00000));

	engine->ch_busy = 0;
}

static int alloc_desc_table(struct gpdma_engine *engine)
{
	/*
	 * For controllers that doesn't support full descriptor addresses, all
	 * descriptors must be in the same 1 MB page, i.e address bits 31..20
	 * must be the same for all descriptors.
	 */
	u32 order = 20 - PAGE_SHIFT;
	int i;

	if (engine->chip->flags & LSIDMA_NEXT_FULL) {
		/*
		 * Controller can do full descriptor addresses, then we need no
		 * special alignment on the descriptor block.
		 */
		order = ilog2((ALIGN(GPDMA_MAX_DESCRIPTORS *
				     sizeof(struct gpdma_desc),
				     PAGE_SIZE)) >> PAGE_SHIFT);
	}

	engine->pool.va = (struct gpdma_desc *)
			  __get_free_pages(GFP_KERNEL|GFP_DMA, order);
	if (!engine->pool.va)
		return -ENOMEM;
	engine->pool.order = order;
	engine->pool.phys = virt_to_phys(engine->pool.va);
	engine_dbg(engine, "order=%d pa=%#llx va=%p\n",
		   engine->pool.order, engine->pool.phys, engine->pool.va);

	for (i = 0; i < GPDMA_MAX_DESCRIPTORS; i++)
		engine->pool.free[i] = &engine->pool.va[i];
	engine->pool.next = 0;

	return 0;
}

static void free_desc_table(struct gpdma_engine *engine)
{
	if (engine->pool.va)
		free_pages((unsigned long)engine->pool.va, engine->pool.order);
}

static struct gpdma_desc *get_descriptor(struct gpdma_engine *engine)
{
	unsigned long flags;
	struct gpdma_desc *desc = NULL;

	raw_spin_lock_irqsave(&engine->lock, flags);
	if (engine->pool.next < GPDMA_MAX_DESCRIPTORS)
		desc = engine->pool.free[engine->pool.next++];
	raw_spin_unlock_irqrestore(&engine->lock, flags);

	return desc;
}

static void
free_descriptor(struct gpdma_engine *engine, struct gpdma_desc *desc)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&engine->lock, flags);
	BUG_ON(engine->pool.next == 0);
	engine->pool.free[--engine->pool.next] = desc;
	raw_spin_unlock_irqrestore(&engine->lock, flags);
}

static int segment_match(struct gpdma_engine *engine, struct gpdma_desc *desc)
{
	unsigned int gpreg_dma = rd32(engine->gpreg);
	unsigned int seg_src = (gpreg_dma >> 0) & 0x3f;
	unsigned int seg_dst = (gpreg_dma >> 8) & 0x3f;

	return (seg_src == ((desc->src >> 32) & 0x3f) &&
		seg_dst == ((desc->dst >> 32) & 0x3f));
}

static int gpdma_start_job(struct gpdma_channel *dmac)
{
	void __iomem      *base = BASE(dmac);
	struct gpdma_desc *desc;
	struct descriptor *d;
	phys_addr_t        paddr;

	desc = list_first_entry(&dmac->waiting, struct gpdma_desc, node);
	d = &desc->hw;
	paddr = virt_to_phys(d);
	WARN_ON(paddr & 0xf);

	if (!(dmac->engine->chip->flags & LSIDMA_SEG_REGS)) {
		/*
		 * No segment registers -> descriptor address bits must match
		 * running descriptor on any other channel.
		 */
		if (dmac->engine->ch_busy && !segment_match(dmac->engine, desc))
			return -EBUSY;
	}

	/* Remove from 'waiting' list and mark as active */
	list_del(&desc->node);
	desc->dma_status = DMA_IN_PROGRESS;
	dmac->active = desc;
	set_bit(dmac->channel, &dmac->engine->ch_busy);
	ch_dbg(dmac, "Load desc va=%p pa=%llx\n", d, paddr);

	if (dmac->engine->chip->flags & LSIDMA_NEXT_FULL) {
		/* Physical address of descriptor to load */
		wr32((u32)paddr | 0x8, base+DMA_NXT_DESCR);
	} else {
		wr32((u32)paddr & 0xfffff, base+DMA_NXT_DESCR);
	}

	if (dmac->engine->chip->flags & LSIDMA_SEG_REGS) {
		/* Segment bits [39..32] of descriptor, src and dst addresses */
		wr32(paddr >> 32, base+DMA_DESCR_ADDR_SEG);
		wr32(desc->src >> 32, base+DMA_SRC_ADDR_SEG);
		wr32(desc->dst >> 32, base+DMA_DST_ADDR_SEG);
	} else {
		unsigned int seg_src = (desc->src >> 32) & 0x3f;
		unsigned int seg_dst = (desc->dst >> 32) & 0x3f;
		wr32((seg_dst << 8) | seg_src, dmac->engine->gpreg);
	}
	wmb();
	wr32(DMA_CONFIG_DSC_LOAD, base+DMA_CHANNEL_CONFIG);

	return 0;
}

static inline void
gpdma_job_complete(struct gpdma_channel *dmac, struct gpdma_desc *desc)
{
	dmac->last_completed = desc->txd.cookie;

	if (desc->txd.callback)
		desc->txd.callback(desc->txd.callback_param);
	ch_dbg(dmac, "cookie %d status %d\n",
		desc->txd.cookie, desc->dma_status);
	free_descriptor(dmac->engine, desc);
	clear_bit(dmac->channel, &dmac->engine->ch_busy);
}

static void flush_channel(struct gpdma_channel *dmac)
{
	struct gpdma_desc *desc, *tmp;

	ch_dbg(dmac, "active=%p\n", dmac->active);
	reset_channel(dmac);
	if (dmac->active) {
		gpdma_job_complete(dmac, dmac->active);
		dmac->active = NULL;
	}
	list_for_each_entry_safe(desc, tmp, &dmac->waiting, node) {
		ch_dbg(dmac, "del wating %p\n", desc);
		list_del(&desc->node);
		gpdma_job_complete(dmac, desc);
	}
}

static inline void gpdma_sched_job_handler(struct gpdma_engine *engine)
{
	if (likely(test_bit(GPDMA_INIT, &engine->state)))
		tasklet_schedule(&engine->job_task);
}

static void job_tasklet(unsigned long data)
{
	struct gpdma_engine *engine = (struct gpdma_engine *)data;
	unsigned long        flags;
	int i;

	/* Handle completed jobs */
	for (i = 0; i < engine->chip->num_channels; i++) {
		struct gpdma_channel *dmac = &engine->channel[i];
		struct gpdma_desc    *desc = dmac->active;

		desc = dmac->active;
		if (desc != NULL) {
			/* Check if job completed */
			if (desc->dma_status != DMA_IN_PROGRESS) {
				dmac->active = NULL;
				gpdma_job_complete(dmac, desc);
			}
		}
	}

	/* Start new jobs */
	for (i = 0; i < engine->chip->num_channels; i++) {
		struct gpdma_channel *dmac = &engine->channel[i];

		raw_spin_lock_irqsave(&dmac->lock, flags);

		if (dmac->active == NULL && !list_empty(&dmac->waiting)) {
			/* Start next job */
			if (gpdma_start_job(dmac) != 0) {
				raw_spin_unlock_irqrestore(&dmac->lock, flags);
				break;
			}
		}

		raw_spin_unlock_irqrestore(&dmac->lock, flags);
	}
}

static irqreturn_t gpdma_isr_err(int irqno, void *_engine)
{
	struct gpdma_engine *engine = _engine;
	u32 status = rd32(engine->gbase + GEN_STAT);
	u32 ch = (status & GEN_STAT_CH0_ERROR) ? 0 : 1;
	struct gpdma_channel *dmac = &engine->channel[ch];

	if (0 == (status & (GEN_STAT_CH0_ERROR | GEN_STAT_CH1_ERROR)))
		return IRQ_NONE;

	/* Read the channel status bits and dump the error */
	status = rd32(dmac->base + DMA_STATUS);
	pr_err("dma: channel%d error %08x\n", dmac->channel, status);
	/* Clear the error indication */
	wr32(DMA_STATUS_ERROR, dmac->base+DMA_STATUS);

	return IRQ_HANDLED;
}

static irqreturn_t gpdma_isr(int irqno, void *_dmac)
{
	struct gpdma_channel *dmac = _dmac;
	struct gpdma_desc    *desc = dmac->active;
	u32                  status;
	u32	             error;

	WARN_ON(desc == NULL);

	status = rd32(dmac->base+DMA_STATUS);
	error = status & DMA_STATUS_ERROR;
	wr32(DMA_STATUS_CLEAR, dmac->base+DMA_STATUS);

	ch_dbg(dmac, "irq%u channel status %08x, error %08x\n",
		irqno, status, error);

	WARN_ON((status & DMA_STATUS_CH_ACTIVE) != 0);

	if (error) {
		if (error & DMA_STATUS_UNALIGNED_ERR) {
			dev_warn(dmac->engine->dev,
				 "Unaligned transaction on ch%d (status=%#x)\n",
				 dmac->channel, status);
			reset_channel(dmac);
		} else {
			dev_warn(dmac->engine->dev,
				 "DMA transaction error on ch%d (status=%#x)\n",
				 dmac->channel, status);
		}
	}

	dmac->active->dma_status = (error ? DMA_ERROR : DMA_SUCCESS);

	wr32(0, dmac->base+DMA_CHANNEL_CONFIG);
	wr32(DMA_CONFIG_CLEAR_FIFO, dmac->base+DMA_CHANNEL_CONFIG);

	gpdma_sched_job_handler(dmac->engine);

	return IRQ_HANDLED;
}

/*
 * Perform soft reset procedure on DMA Engine.  Needed occasionally to work
 * around nasty bug ACP3400 sRIO HW.
 */
static ssize_t __ref
reset_engine(struct device *dev,
	     struct device_attribute *attr,
	     const char *buf, size_t count)
{
	struct gpdma_engine *engine = dev_get_drvdata(dev);
	int i;

	if (!engine)
		return -EINVAL;

	/* Disable interrupts and acquire each channel lock */
	for (i = 0; i < engine->chip->num_channels; i++)
		disable_irq(engine->channel[i].irq);

	/* Disable tasklet (synchronized) */
	tasklet_disable(&engine->job_task);

	soft_reset(engine);

	/* Re-queue any active jobs */
	for (i = 0; i < engine->chip->num_channels; i++) {
		struct gpdma_channel *dmac = &engine->channel[i];
		struct gpdma_desc  *active;
		raw_spin_lock(&engine->channel[i].lock);
		active = dmac->active;
		if (active && active->dma_status == DMA_IN_PROGRESS) {
			/* Restart active job after soft reset */
			list_add(&active->node, &dmac->waiting);
			dmac->active = NULL;
		}
		raw_spin_unlock(&engine->channel[i].lock);
	}

	tasklet_enable(&engine->job_task);

	for (i = 0; i < engine->chip->num_channels; i++)
		enable_irq(engine->channel[i].irq);

	gpdma_sched_job_handler(engine);

	return count;
}

static DEVICE_ATTR(soft_reset, S_IWUSR, NULL, reset_engine);

/*
 *===========================================================================
 *
 *                       DMA DEVICE INTERFACE
 *
 *===========================================================================
 *
 */

/**
 * gpdma_alloc_chan_resources - Allocate resources and return the number of
 * allocated descriptors.
 *
 */
static int gpdma_alloc_chan_resources(struct dma_chan *chan)
{
	struct gpdma_channel *dmac = dchan_to_gchan(chan);

	(void) dmac;
	return 1;
}

/**
 * gpdma_free_chan_resources - Release DMA channel's resources.
 *
 */
static void gpdma_free_chan_resources(struct dma_chan *chan)
{
	struct gpdma_channel *dmac = dchan_to_gchan(chan);

	(void) dmac;
}

/**
 * gpdma_prep_memcpy - Prepares a memcpy operation.
 *
 */
static struct dma_async_tx_descriptor *
gpdma_prep_memcpy(struct dma_chan *chan,
		 dma_addr_t dst,
		 dma_addr_t src,
		 size_t size,
		 unsigned long dma_flags)
{
	struct gpdma_channel *dmac = dchan_to_gchan(chan);
	struct gpdma_desc *desc;
	u16 rot_len, x_count, src_size, access_size;

	desc = get_descriptor(dmac->engine);
	if (desc == NULL) {
		ch_dbg(dmac, "ERROR: No descriptor\n");
		return NULL;
	}

	/* Maximize memory access width based on job src, dst and length */
	switch (ffs((u32)dst | (u32)src | size)) {
	case 1:
		src_size = 1;
		access_size = (0 << 3);
		break;
	case 2:
		src_size = 2;
		access_size = (1 << 3);
		break;
	case 3:
		src_size = 4;
		access_size = (2 << 3);
		break;
	case 4:
		src_size = 8;
		access_size = (3 << 3);
		break;
	default:
		src_size = 16;
		access_size = (4 << 3);
		break;
	}

	ch_dbg(dmac, "dst=%#llx src=%#llx size=%u mod=%d\n",
		dst, src, size, src_size);
	x_count = (size/src_size) - 1;
	rot_len = (2 * src_size) - 1;

	/*
	 * Fill in descriptor in memory.
	 */
	desc->hw.src_x_ctr     = cpu_to_le16(x_count);
	desc->hw.src_y_ctr     = 0;
	desc->hw.src_x_mod     = cpu_to_le32(src_size);
	desc->hw.src_y_mod     = 0;
	desc->hw.src_addr      = cpu_to_le32(src & 0xffffffff);
	desc->hw.src_data_mask = ~0;
	desc->hw.src_access    = cpu_to_le16((rot_len << 6) | access_size);
	desc->hw.dst_access    = cpu_to_le16(access_size);
	desc->hw.ch_config     = cpu_to_le32(DMA_CONFIG_ONE_SHOT(1));
	desc->hw.next_ptr      = 0;
	desc->hw.dst_x_ctr     = cpu_to_le16(x_count);
	desc->hw.dst_y_ctr     = 0;
	desc->hw.dst_x_mod     = cpu_to_le32(src_size);
	desc->hw.dst_y_mod     = 0;
	desc->hw.dst_addr      = cpu_to_le32(dst & 0xffffffff);

	/* Setup sw descriptor */
	INIT_LIST_HEAD(&desc->node);
	desc->src        = src;
	desc->dst        = dst;
	desc->dma_status = DMA_IN_PROGRESS;
	dma_async_tx_descriptor_init(&desc->txd, chan);
	desc->txd.tx_submit = gpdma_tx_submit;
	desc->txd.flags     = DMA_CTRL_ACK;

	return &desc->txd;
}

/**
 * gpdma_tx_submit
 *
 */
static dma_cookie_t gpdma_tx_submit(struct dma_async_tx_descriptor *txd)
{
	struct gpdma_channel *dmac = dchan_to_gchan(txd->chan);
	struct gpdma_desc    *desc = txd_to_desc(txd);
	unsigned long         flags;

	raw_spin_lock_irqsave(&dmac->lock, flags);

	/* Update last used cookie */
	if (++dmac->chan.cookie < 0)
		dmac->chan.cookie = DMA_MIN_COOKIE;

	txd->cookie = dmac->chan.cookie;
	list_add_tail(&desc->node, &dmac->waiting);

	raw_spin_unlock_irqrestore(&dmac->lock, flags);

	ch_dbg(dmac, "cookie=%d last_completed=%d\n",
		txd->cookie, dmac->last_completed);

	gpdma_sched_job_handler(dmac->engine);

	return txd->cookie;
}

/**
 * gpdma_issue_pending - Push pending transactions to hardware.
 *
 */
static void gpdma_issue_pending(struct dma_chan *chan)
{
	struct gpdma_channel *dmac = dchan_to_gchan(chan);

	gpdma_sched_job_handler(dmac->engine);
}

/**
 * gpdma_tx_status - Poll for transaction completion, the optional txstate
 * parameter can be supplied with a pointer to get a struct with auxiliary
 * transfer status information, otherwise the call will just return a simple
 * status code.
 */
static enum dma_status gpdma_tx_status(struct dma_chan *chan,
				       dma_cookie_t cookie,
				       struct dma_tx_state *txstate)
{
	struct gpdma_channel *dmac = dchan_to_gchan(chan);
	enum dma_status ret = 0;

	ret = dma_async_is_complete(cookie, dmac->last_completed, chan->cookie);
	dma_set_tx_state(txstate, dmac->last_completed, chan->cookie, 0);

	return ret;
}


/**
 * gpdma_device_control - Manipulate all pending operations on a channel,
 * returns zero or error code.
 *
 */
static int gpdma_device_control(struct dma_chan *dchan,
				enum dma_ctrl_cmd cmd,
				unsigned long arg)
{
	struct gpdma_channel *dmac = dchan_to_gchan(dchan);

	if (!dmac)
		return -EINVAL;

	switch (cmd) {
	case DMA_TERMINATE_ALL:
		tasklet_disable(&dmac->engine->job_task);
		flush_channel(dmac);
		tasklet_enable(&dmac->engine->job_task);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static void gpdma_engine_release(struct kref *kref)
{
	struct gpdma_engine *engine =
		container_of(kref, struct gpdma_engine, kref);

	kfree(engine);
}

/**
 * gpdma_of_remove
 *
 *
 */
static int gpdma_of_remove(struct platform_device *op)
{
	struct gpdma_engine *engine = dev_get_drvdata(&op->dev);
	int i;

	dev_dbg(&op->dev, "%s\n", __func__);

	if (engine) {
		if (test_and_clear_bit(GPDMA_INIT, &engine->state)) {
			device_remove_file(&op->dev, &dev_attr_soft_reset);
			dma_async_device_unregister(&engine->dma_device);
		}

		tasklet_disable(&engine->job_task);
		tasklet_kill(&engine->job_task);

		if (engine->err_irq)
			free_irq(engine->err_irq, engine);

		for (i = 0; i < engine->chip->num_channels; i++) {
			struct gpdma_channel *dmac = &engine->channel[i];

			if (dmac->irq)
				free_irq(dmac->irq, dmac);
		}

		free_desc_table(engine);
		iounmap(engine->iobase);
		kref_put(&engine->kref, gpdma_engine_release);
		dev_set_drvdata(&op->dev, NULL);
	}

	return 0;
}

static int
setup_channel(struct gpdma_engine *engine, struct device_node *child, int id)
{
	struct gpdma_channel *dmac;
	int rc = -ENODEV;

	if (id >= engine->chip->num_channels) {
		dev_dbg(engine->dev, "Too many channels (%d)\n", id);
		goto err_init;
	}

	dmac = &engine->channel[id];
	dmac->channel = id;
	dmac->engine = engine;
	raw_spin_lock_init(&dmac->lock);
	INIT_LIST_HEAD(&dmac->waiting);
	dmac->chan.device = &engine->dma_device;

	dmac->base = engine->iobase + id*engine->chip->chregs_offset;
	dev_dbg(engine->dev, "channel%d base @ %p\n", id, dmac->base);

	/* Find the IRQ line, if it exists in the device tree */
	dmac->irq = irq_of_parse_and_map(child, 0);
	dev_dbg(engine->dev, "channel %d, irq %d\n", id, dmac->irq);
	rc = request_irq(dmac->irq, gpdma_isr, IRQF_SHARED, "lsi-dma", dmac);
	if (rc) {
		dev_err(engine->dev, "failed to request_irq, error = %d\n", rc);
		goto err_init;
	}
	/* Add the channel to the DMAC list */
	list_add_tail(&dmac->chan.device_node, &engine->dma_device.channels);

err_init:
	return rc;
}

static struct lsidma_hw lsi_dma32 = {
	.num_channels   = 2,
	.chregs_offset  = 0x80,
	.genregs_offset = 0xF00,
	.flags          = (LSIDMA_NEXT_FULL |
			   LSIDMA_SEG_REGS)
};

static struct lsidma_hw lsi_dma31 = {
	.num_channels   = 4,
	.chregs_offset  = 0x40,
	.genregs_offset = 0x400,
	.flags          = 0
};

static const struct of_device_id gpdma_of_ids[] = {
	{
		.compatible = "lsi,dma32",
		.data       = &lsi_dma32
	},
	{
		.compatible = "lsi,dma31",
		.data       = &lsi_dma31
	},
	{
		.compatible = "gp-dma,acp-dma",
		.data       = &lsi_dma31
	},
	{
		.compatible = "gp-dma,acp-gpdma",
		.data       = &lsi_dma31
	},
	{ }
};

/**
 * gpdma_of_probe
 *
 *
 */
static int __devinit gpdma_of_probe(struct platform_device *op)
{
	struct gpdma_engine *engine;
	struct dma_device   *dma;
	struct device_node *child;
	const struct of_device_id *match;
	int rc = -ENOMEM;
	int id = 0;

	match = of_match_device(gpdma_of_ids, &op->dev);
	if (!match)
		return -EINVAL;

	engine = kzalloc(sizeof *engine, GFP_KERNEL);
	if (!engine)
		return -ENOMEM;

	dev_set_drvdata(&op->dev, engine);

	kref_init(&engine->kref);
	raw_spin_lock_init(&engine->lock);
	engine->dev = &op->dev;
	engine->chip = match->data;

	/* Initialize dma_device struct */
	dma = &engine->dma_device;
	dma->dev = &op->dev;
	dma_cap_zero(dma->cap_mask);
	dma_cap_set(DMA_MEMCPY, dma->cap_mask);
	dma->copy_align = 2;
	dma->chancnt = engine->chip->num_channels;
	dma->device_alloc_chan_resources = gpdma_alloc_chan_resources;
	dma->device_free_chan_resources = gpdma_free_chan_resources;
	dma->device_tx_status = gpdma_tx_status;
	dma->device_prep_dma_memcpy = gpdma_prep_memcpy;
	dma->device_issue_pending = gpdma_issue_pending;
	dma->device_control = gpdma_device_control;
	INIT_LIST_HEAD(&dma->channels);

	/*
	 * Map device I/O memory
	 */
	engine->iobase = of_iomap(op->dev.of_node, 0);
	if (!engine->iobase) {
		rc = -EINVAL;
		goto err_init;
	}
	dev_dbg(&op->dev, "mapped base @ %p\n", engine->iobase);

	engine->err_irq = irq_of_parse_and_map(op->dev.of_node, 1);
	if (engine->err_irq) {
		rc = request_irq(engine->err_irq, gpdma_isr_err,
				 IRQF_SHARED, "lsi-dma-err", engine);
		if (rc) {
			dev_err(engine->dev,
				"failed to request irq%d\n",
				engine->err_irq);
			engine->err_irq = 0;
		}
	}

	if (!(engine->chip->flags & LSIDMA_SEG_REGS)) {
		struct device_node *gp_node;
		gp_node = of_find_compatible_node(NULL, NULL, "lsi,gpreg");
		if (!gp_node) {
			dev_err(engine->dev, "FDT is missing node 'gpreg'\n");
			rc = -EINVAL;
			goto err_init;
		}
		engine->gpreg = of_iomap(gp_node, 0);
	}

	/* General registes at device specific offset */
	engine->gbase = engine->iobase + engine->chip->genregs_offset;

	if (alloc_desc_table(engine))
		goto err_init;

	/* Setup channels */
	for_each_child_of_node(op->dev.of_node, child) {
		rc = setup_channel(engine, child, id++);
		if (rc != 0)
			goto err_init;
	}

	soft_reset(engine);

	rc = dma_async_device_register(&engine->dma_device);
	if (rc) {
		dev_err(engine->dev, "unable to register\n");
		goto err_init;
	}
	tasklet_init(&engine->job_task, job_tasklet, (unsigned long)engine);
	device_create_file(&op->dev, &dev_attr_soft_reset);

	set_bit(GPDMA_INIT, &engine->state);

	return 0;

err_init:
	gpdma_of_remove(op);
	dev_set_drvdata(&op->dev, NULL);
	return rc;
}

static struct platform_driver gpdma_of_driver = {
	.driver = {
		.name           = "lsi-dma32",
		.owner          = THIS_MODULE,
		.of_match_table = gpdma_of_ids,
	},
	.probe  = gpdma_of_probe,
	.remove = gpdma_of_remove,
};

static __init int gpdma_init(void)
{
	return platform_driver_register(&gpdma_of_driver);
}

static void __exit gpdma_exit(void)
{
	platform_driver_unregister(&gpdma_of_driver);
}

subsys_initcall(gpdma_init);
module_exit(gpdma_exit);

MODULE_DESCRIPTION("LSI DMA driver");
MODULE_LICENSE("GPL");
