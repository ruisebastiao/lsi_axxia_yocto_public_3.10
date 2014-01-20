/*
 * RapidIO Direct I/O driver
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/gfp.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/rio_dio.h>

static LIST_HEAD(io_wins);
static LIST_HEAD(dio_channels);
static DEFINE_SPINLOCK(rio_dio_lock);

#if defined(CONFIG_RAPIDIO_DIO_DMA)

struct rio_dio_dma {
	struct list_head node;
	struct dma_chan *txdmachan;
	enum dma_ctrl_flags flags;
	struct dma_device *txdmadev;
	struct kref kref;
};

/**
 * __dma_register
 *
 * Allocate and initialize a direct I/O channel for
 * DMA transfers. Initialize reference counting
 * and ad  channel to rio_dio global channel
 * list.
 *
 * Returns: On success - Pointer to I/O channel data
 *          On failure - NULL
 */
static struct rio_dio_dma *__dma_register(void)
{
	dma_cap_mask_t mask;
	unsigned long flags;
	struct rio_dio_dma *dma = kzalloc(sizeof(*dma),
					  GFP_KERNEL);
	if (!dma)
		return NULL;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);
	dma->txdmachan = dma_request_channel(mask, NULL, NULL);
	if (!dma->txdmachan) {
		kfree(dma);
		return NULL;
	}
	dma->flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	dma->txdmadev = dma->txdmachan->device;
	kref_init(&dma->kref);
	spin_lock_irqsave(&rio_dio_lock, flags);
	list_add_tail(&dma->node, &dio_channels);
	spin_unlock_irqrestore(&rio_dio_lock, flags);
	dev_dbg(dma->txdmadev->dev,
		"rio_dio Register DMA channel\n");
	return dma;
}
/**
 * __rio_dio_method_setup
 *
 * @dio_channel: RIO direct I/O method data.
 *
 * DMA version of generic method
 *
 * Returns: On success - 0
 *          On failure != 0
 */
static int __rio_dio_method_setup(void **dio_channel)
{
	*dio_channel = __dma_register();
	if (!*dio_channel)
		return -EFAULT;
	return 0;
}

/**
 * __dma_get
 *
 * @dio_dma: RIO direct I/O method data.
 *
 * Increment reference count if @dio_dma is a valid
 * method data pointer, i.e. if it is found in the
 * rio_dio global channel list.
 *
 * Returns: On success - @dio_dma
 *          On failure - NULL
 */
static inline struct rio_dio_dma *__dma_get(struct rio_dio_dma *dio_dma)
{
	struct rio_dio_dma *entry, *next, *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&rio_dio_lock, flags);
	list_for_each_entry_safe(entry, next, &dio_channels, node) {
		if (dio_dma && dio_dma == entry) {
			kref_get(&dio_dma->kref);
			ret = entry;
			break;
		}
	}
	spin_unlock_irqrestore(&rio_dio_lock, flags);
	return ret;
}

/**
 * __rio_dio_method_get
 *
 * @dio_channel: RIO direct I/O method data.
 *
 * DMA version of generic method
 *
 * Returns: On success - @dio_channel
 *          On failure - NULL
 */
static void *__rio_dio_method_get(void *dio_channel)
{
	struct rio_dio_dma *dio_dma = (struct rio_dio_dma *)dio_channel;

	return __dma_get(dio_dma);
}
/**
 * __dma_release
 *
 * @kref:
 *
 * Remove dio_channel from the rio_dio global channel list.
 * Release DMA channel and free direct I/O channel data
 *
 */

static void __dma_release(struct kref *kref)
{
	struct rio_dio_dma *dio_dma = container_of(kref,
						   struct rio_dio_dma, kref);
	unsigned long flags;

	spin_lock_irqsave(&rio_dio_lock, flags);
	list_del(&dio_dma->node);
	spin_unlock_irqrestore(&rio_dio_lock, flags);

	dev_dbg(dio_dma->txdmadev->dev,
		"rio_dio Release DMA channel\n");
	dma_release_channel(dio_dma->txdmachan);
	kfree(dio_dma);
}

/**
 * __rio_dio_method_put
 *
 * @dio_channel: RIO direct I/O method data.
 *
 * DMA version of generic method
 *
 */

static void __rio_dio_method_put(void *dio_channel)
{
	struct rio_dio_dma *dio_dma = (struct rio_dio_dma *)dio_channel;

	if (dio_dma)
		kref_put(&dio_dma->kref, __dma_release);
}

/**
 * __dma_callback
 *
 * @completion: DMA semaphore
 *
 * Used by the DMA driver to signal that a transfer is done
 *
 */

static void __dma_callback(void *completion)
{
	complete(completion);
}
/**
 * __dma_cpy
 *
 * @dio_dma: RIO direct I/O method data.
 * @phys: Physical (SRIO device) read/write address
 * @buf: Pointer from/to where data shall be read/written
 * @len: Number of bytes to read/write
 * @dir: Transfer direction to/from SRIO device
 *
 * Setup DMA transfer and wait for it to finish
 * There may be multiple threads running this function
 * in parallel for a single channel, and multiple channels
 * may be used in parallel.
 *
 * Returns: On success - 0
 *          On failure != 0
 */

static int __dma_cpy(struct rio_dio_dma *dio_dma,
		     resource_size_t phys,
		     void *buf, u32 len,
		     enum dma_data_direction dir)
{
	struct dma_async_tx_descriptor *tx;
	struct dma_slave_config config;
	dma_addr_t dma_src, dma_dst, dma_map;
	dma_cookie_t cookie;
	unsigned long tmo = msecs_to_jiffies(3000);
	struct completion cmp;
	enum dma_status status = DMA_ERROR;
	enum dma_ctrl_flags eflag;
	u8 align;
	char *message = NULL;

	if (!__rio_dio_method_get(dio_dma))
		return -EFAULT;

	align = dio_dma->txdmadev->copy_align;
	if (1 << align > len) {
		message = "DMA Buffer alignment error";
		goto out_err;
	}
	config.direction = dir;

	if (dir == DMA_TO_DEVICE) {
		config.dst_addr_width = 16;
		config.dst_maxburst = 16;
		dma_map = dma_src = dma_map_single(dio_dma->txdmadev->dev,
						   buf, len, dir);
		dma_dst = (dma_addr_t)phys;
		eflag = DMA_COMPL_SKIP_DEST_UNMAP;
	} else {
		config.src_addr_width = 16;
		config.src_maxburst = 16;
		dma_src = (dma_addr_t)phys;
		dma_map = dma_dst = dma_map_single(dio_dma->txdmadev->dev,
						   buf, len, dir);
		eflag = DMA_COMPL_SKIP_SRC_UNMAP;
	}
	dmaengine_slave_config(dio_dma->txdmachan, &config);

	tx = dio_dma->txdmadev->device_prep_dma_memcpy(dio_dma->txdmachan,
						       dma_dst,
						       dma_src,
						       len,
						       dio_dma->flags | eflag);
	if (!tx) {
		message = "DMA channel prepare error";
		goto out_prep;
	}
	init_completion(&cmp);
	tx->callback = __dma_callback;
	tx->callback_param = &cmp;
	cookie = tx->tx_submit(tx);
	if (dma_submit_error(cookie)) {
		message = "DMA channel submit error";
		goto out_submit;
	}
	dma_async_issue_pending(dio_dma->txdmachan);
	tmo = wait_for_completion_timeout(&cmp, tmo);
	status = dma_async_is_tx_complete(dio_dma->txdmachan,
					  cookie, NULL, NULL);
	if (!tmo) {
		message = "DMA transfer timeout";
		status = -ETIME;
		dmaengine_terminate_all(dio_dma->txdmachan);
		goto out_err;
	}
	goto out;

out_prep:
out_submit:
	dma_unmap_single(dio_dma->txdmadev->dev, dma_map, len, dir);

out_err:
	dev_info(dio_dma->txdmadev->dev, "%s\n", message);

out:
	__rio_dio_method_put(dio_dma);
	if (status != DMA_SUCCESS)
		return status == -ETIME ? status : -EFAULT;
	return 0;

}
/**
 * __rio_dio
 *
 * @dio_channel: RIO direct I/O method data.
 * @res: RapidIO mapping region phys and virt start address
 * @buf: Pointer from/to where data shall be read/written
 * @len: Number of bytes to read/write
 * @dir: Transfer direction to/from SRIO device
 *
 * DMA version of generic method
 *
 */

static inline int __rio_dio(void *dio_channel,
			    struct rio_map_addr *res,
			    void *buf,
			    u32 len,
			    enum dma_data_direction dir)
{
	struct rio_dio_dma *dio_dma = (struct rio_dio_dma *)dio_channel;

	return __dma_cpy(dio_dma, (resource_size_t)res->phys,
			 buf, len, dir);
}

#else

/**
 * __rio_dio_method_setup
 *
 * @dio_channel: NULL.
 *
 * memcpy version of generic method
 *
 * Returns: 0
 */

static int __rio_dio_method_setup(void **dio_channel)
{
	*dio_channel = NULL;
	return 0;
}
/**
 * __rio_dio_method_get
 *
 * @dio_channel: NULL
 *
 * memcpy version of generic method
 *
 * Returns: NULL
 */

static void *__rio_dio_method_get(void *dio_channel)
{
	return NULL;
}
/**
 * __rio_dio_method_put
 *
 * @dio_channel: NULL
 *
 * memcpy version of generic method
 *
 */

static void __rio_dio_method_put(void *dio_channel)
{
}
/**
 * __rio_dio
 *
 * @dio_channel: RIO direct I/O method data.
 * @res: RapidIO mapping region phys and virt start address
 * @buf: Pointer from/to where data shall be read/written
 * @len: Number of bytes to read/write
 * @dir: Transfer direction to/from SRIO device
 *
 * memcpy version of generic method
 *
 */

static inline int __rio_dio(void *dio_channel,
			    struct rio_map_addr *res,
			    void *buf,
			    u32 len,
			    enum dma_data_direction dir)
{
	u8 *src, *dst;
	int rc = 0;

	if (dir == DMA_FROM_DEVICE) {
		int size = len;

		dst = (u8 *)buf;
		src = (u8 *)res->va;

		while (size > 0 && !rc) {
			int tsize = (size > 3 ? 4 : (size < 2 ? 1 : 2));
			switch (tsize) {
			case 1:
				rc = rio_in_8(dst, (u8 __iomem *)src);
				break;
			case 2:
				rc = rio_in_be16((u16 *)dst,
						 (u16 __iomem *)src);
				break;
			case 4:
				rc = rio_in_be32((u32 *)dst,
						 (u32 __iomem *)src);
				break;
			default:
				pr_warn("(%s): illegal read tsize %d\n",
					__func__, tsize);
				rc = -EINVAL;
			}
			src += tsize;
			dst += tsize;
			size -= tsize;
		}
	} else {
		dst = (u8 *)res->va;
		src = (u8 *)buf;
		memcpy(dst, src, len);
	}

	return rc;
}

#endif
/**
 * __rio_dio_release_region
 *
 * @kref:
 *
 * Remove @io_win from the rio_dio global io window list.
 * Give up reference to @dio_channel
 * Free outbound RapidIO bus region and io_win data.
 *
 */

static void __rio_dio_release_region(struct kref *kref)
{
	struct rio_dio_win *io_win = container_of(kref,
						 struct rio_dio_win,
						 kref);
	unsigned long flags;

	spin_lock_irqsave(&rio_dio_lock, flags);
	list_del(&io_win->node);
	spin_unlock_irqrestore(&rio_dio_lock, flags);

	pr_debug("RIO: Return ATMU window %u\n", io_win->outb_win);
	rio_release_outb_region(io_win->rdev->hport, io_win->outb_win);
	__rio_dio_method_put(io_win->dio_channel);

	kfree(io_win);
}
/**
 * __rio_dio_region_get
 *
 * @io_win: RIO I/O window data.
 *
 * Increment reference count if @io_win is a valid
 * pointer, i.e. if it is found in the
 * rio_dio global I/O window list.
 *
 * Returns: On success - @io_win
 *          On failure - NULL
 */

static inline
struct rio_dio_win *__rio_dio_region_get(struct rio_dio_win *io_win)
{
	struct rio_dio_win *entry, *next, *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&rio_dio_lock, flags);
	list_for_each_entry_safe(entry, next, &io_wins, node) {
		if (io_win == entry) {
			kref_get(&io_win->kref);
			ret = entry;
			break;
		}
	}
	spin_unlock_irqrestore(&rio_dio_lock, flags);
	return ret;
}

/**
 * __rio_dio_write
 *
 * @io_win: RIO I/O window data.
 * @offset: Offset into device direct I/O memory space
 * @buf: Pointer from where data shall be read
 * @len: Number of bytes to write
 *
 * Create mapping from local iomem to rio space
 * and write data to device.
 *
 * Returns: On success - 0
 *          On failure != 0
 *
 */

static int __rio_dio_write(struct rio_dio_win *io_win,
			   u32 offset, void *buf, u32 mflags, u32 len)
{
	struct rio_map_addr res;
	u8 *dst;
	int rc;

	if (!__rio_dio_region_get(io_win))
		return -ENODEV;

	rc = rio_map_outb_mem(io_win->rdev->hport,
			      io_win->outb_win,
			      io_win->rdev->destid,
			      offset,
			      mflags,
			      &res);
	if (rc)
		goto out;

	dst = (u8 *)res.va;
	switch (len) {
	case 1:
		out_8(dst, *((u8 *)buf));
		break;
	case 2:
		out_be16((u16 *)dst, *((u16 *)buf));
		break;
	default:
			rc = __rio_dio(io_win->dio_channel,
				       &res,
				       buf,
				       len,
				       DMA_TO_DEVICE);
	}
out:
	rio_dio_region_put(io_win);
	return rc;
}


/**
 * __rio_dio_read
 *
 * @io_win: RIO I/O window data.
 * @offset: Offset into device direct I/O memory space
 * @buf: Pointer to where data shall be stored
 * @len: Number of bytes to read
 *
 * Create mapping from local iomem to rio space
 * and read data from device.
 *
 * Returns: On success - 0
 *          On failure != 0
 *
 */

static int __rio_dio_read(struct rio_dio_win *io_win,
			  u32 offset, void *buf, u32 mflags, u32 len)
{
	struct rio_map_addr res;
	int rc;

	if (!__rio_dio_region_get(io_win))
		return -ENODEV;

	rc = rio_map_outb_mem(io_win->rdev->hport,
			      io_win->outb_win,
			      io_win->rdev->destid,
			      offset,
			      mflags,
			      &res);

	if (rc)
		goto out;

	switch (len) {
	case 1:
		rc = rio_in_8((u8 *)buf, res.va);
		break;
	case 2:
		rc = rio_in_be16((u16 *)buf, res.va);
		break;
	default:
		rc = __rio_dio(io_win->dio_channel,
			       &res,
			       buf,
			       len,
			       DMA_FROM_DEVICE);
	}
out:
	rio_dio_region_put(io_win);
	return rc;
}
#define RIO_DIO_8_BAD 0
#define RIO_DIO_16_BAD (offset & 1)
#define RIO_DIO_32_BAD (offset & 3)
#define RIO_DIO_BUFF_BAD (len > io_win->win_size)

/**
 * RIO_DIO_READ - Generate rio_dio_read_* functions
 *
 * @size: Size of direct I/O space read (8, 16, 32 bit)
 * @type: C type of value argument
 * @len: Length of direct I/O space read in bytes
 *
 * Generates rio_dio_read_* functions used for direct I/O access
 * to end-point device.
 *
 * Returns: 0 on success and != 0 on failure
 *
 * NOTE: With regard to @io_win, these functions are not re-entrant.
 * It is assumed that the user provides proper synchronization
 * methods assuring that calls, pertaining to the same @io_win,
 * are serialized.
 *
 */
#define RIO_DIO_READ(size, type, len)					\
	int rio_dio_read_##size						\
	(struct rio_dio_win *io_win, u32 offset, u32 mflags, type *value) \
	{								\
		int rc;							\
		u32 dst;						\
									\
		if (RIO_DIO_##size##_BAD)				\
			return RIO_BAD_SIZE;				\
									\
		rc = __rio_dio_read(io_win, offset, &dst, mflags, len);	\
		*value = (type)dst;					\
		return rc;						\
	}

/**
 * RIO_DIO_WRITE - Generate rio_dio_write_* functions
 *
 * @size: Size of direct I/O space write (8, 16, 32 bits)
 * @type: C type of value argument
 * @len: Length of direct I/O space write in bytes
 *
 * Generates rio_dio_write_* functions used for direct I/O access
 * to end-point device.
 *
 * Returns: 0 on success and != 0 on failure
 *
 * NOTE: With regard to @io_win, these function are not re-entrant.
 * It is assumed that the user provides proper synchronization
 * methods assuring that calls, pertaining to the same @io_win,
 * are serialized.
 *
 */
#define RIO_DIO_WRITE(size, type, len)					\
	int rio_dio_write_##size					\
	(struct rio_dio_win *io_win, u32 offset, u32 mflags, type *src)	\
	{								\
		int rc;							\
									\
		if (RIO_DIO_##size##_BAD)				\
			return RIO_BAD_SIZE;				\
									\
		rc = __rio_dio_write(io_win, offset, src, mflags, len);	\
		return rc;						\
	}

RIO_DIO_READ(8, u8, 1)
EXPORT_SYMBOL_GPL(rio_dio_read_8);
RIO_DIO_READ(16, u16, 2)
EXPORT_SYMBOL_GPL(rio_dio_read_16);
RIO_DIO_READ(32, u32, 4)
EXPORT_SYMBOL_GPL(rio_dio_read_32);
RIO_DIO_WRITE(8, u8, 1)
EXPORT_SYMBOL_GPL(rio_dio_write_8);
RIO_DIO_WRITE(16, u16, 2)
EXPORT_SYMBOL_GPL(rio_dio_write_16);
RIO_DIO_WRITE(32, u32, 4)
EXPORT_SYMBOL_GPL(rio_dio_write_32);

/**
 * rio_dio_read_buff
 *
 * @io_win: RIO I/O window data.
 * @offset: Offset into device direct I/O memory space
 * @dst: Pointer where read data will be stored
 * @len: Length of direct I/O space read in bytes
 *
 * used for direct I/O access
 * to end-point device.
 *
 * Returns: 0 on success and != 0 on failure
 *
 * NOTE: With regard to @io_win, this function is not re-entrant.
 * It is assumed that the user provides proper synchronization
 * methods assuring that calls, pertaining to the same @io_win,
 * are serialized.
 *
 */
int rio_dio_read_buff(struct rio_dio_win *io_win, u32 offset,
		      u8 *dst, u32 mflags, u32 len)
{
	if (RIO_DIO_BUFF_BAD)
		return RIO_BAD_SIZE;

	return __rio_dio_read(io_win, offset, dst, mflags, len);
}
EXPORT_SYMBOL_GPL(rio_dio_read_buff);
/**
 * rio_dio_write_buff
 *
 * @io_win: RIO I/O window data.
 * @offset: Offset into device direct I/O memory space
 * @dst: Data to be written
 * @len: Length of direct I/O space read in bytes
 *
 * used for direct I/O access
 * to end-point device.
 *
 * Returns: 0 on success and != 0 on failure
 *
 * NOTE: With regard to @io_win, this function is not re-entrant.
 * It is assumed that the user provides proper synchronization
 * methods assuring that calls, pertaining to the same @io_win,
 * are serialized.
 *
 */
int rio_dio_write_buff(struct rio_dio_win *io_win, u32 offset,
		       u8 *src, u32 mflags, u32 len)
{
	if (RIO_DIO_BUFF_BAD)
		return RIO_BAD_SIZE;

	return __rio_dio_write(io_win, offset, src, mflags, len);
}
EXPORT_SYMBOL_GPL(rio_dio_write_buff);

int rio_dio_const_win(struct rio_dio_win *io_win,
		      void *buf, u32 len,
		      enum dma_data_direction dir,
		      struct rio_map_addr *res)
{
	int rc;

	if (!__rio_dio_region_get(io_win))
		return -ENODEV;

	rc = __rio_dio(io_win->dio_channel,
		       res,
		       buf,
		       len,
		       dir);

	rio_dio_region_put(io_win);
	return rc;
}
EXPORT_SYMBOL_GPL(rio_dio_const_win);

/**
 * rio_dio_req_region
 *
 * @dio_channel: Direct I/O mode, must be allocated prior to
 *            this call using rio_dio_method_setup().
 * @rdev: RIO device
 * @size: Size of ATMU window
 * @transaction_type: Flags for mapping. 0 for using default
 *                     (NREAD/NWRITE) transaction type.
 *
 * Allocate and initialize a RIO direct I/O ATMU window.
 * Reserves an outbound region in the RapidIO bus address space.
 * The reserved region may be used to create mappings from local
 * iomem to rio space.
 *
 * Returns: On success - Pointer to RIO I/O window data.
 *          On failure - NULL
 *
 * NOTE: Once you have called this function, use rio_dio_region_put()
 *       to give up your reference instead of freeing the
 *       RIO I/O window data structure directly.
 */
struct rio_dio_win *rio_dio_req_region(void *dio_channel,
				       struct rio_dev *rdev,
				       resource_size_t size,
				       u32 transaction_type)
{
	unsigned long flags;
	struct rio_dio_win *io_win = kzalloc(sizeof(*io_win), GFP_KERNEL);
	char *message = NULL;

	if (!io_win) {
		message = "Out of memory";
		goto out_err;
	}
	io_win->rdev = rdev;
	io_win->dio_channel = __rio_dio_method_get(dio_channel);
	io_win->win_size = size;
	/* Setup outbound ATMU I/O window
	 */
	if (rio_req_outb_region(rdev->hport, size, rdev->dev.init_name,
				transaction_type, &io_win->outb_win)) {
		message = "Mapping outbound ATMU window failed";
		goto out_map;
	}
	kref_init(&io_win->kref);
	spin_lock_irqsave(&rio_dio_lock, flags);
	list_add_tail(&io_win->node, &io_wins);
	spin_unlock_irqrestore(&rio_dio_lock, flags);
	pr_debug("RIO: Alloc ATMU window %u\n", io_win->outb_win);
	goto out;

out_map:
	__rio_dio_method_put(dio_channel);
	kfree(io_win);
	io_win = NULL;
out_err:
	pr_warn("RIO: %s\n", message);
out:
	return io_win;
}
EXPORT_SYMBOL_GPL(rio_dio_req_region);

/**
 * rio_dio_region_put - decrement reference count.
 *
 * @io_win: RIO I/O window in question.
 *
 * Frees outbound RapidIO bus region and io_win data
 * when the reference count drops to zero
 */

void rio_dio_region_put(struct rio_dio_win *io_win)
{
	if (io_win)
		kref_put(&io_win->kref, __rio_dio_release_region);
}
EXPORT_SYMBOL_GPL(rio_dio_region_put);

/**
 * rio_dio_method_setup
 *
 * @dio_channel: RIO direct I/O method data.
 *
 * Allocate and initialize a direct I/O channel.
 * The I/O method used for RapidIO transfers > 4 bytes
 * is configurable at kernel build time. If DMA is choosen
 * a successful method setup implies that a DMA channel has
 * been allocated. The default option is to use memcpy, in
 * which case not particular setup is required and the method
 * data ptr will be set to NULL.
 * It is, regardless of selected method, entirely possible to
 * use the same @dio_channel in multiple I/O windows.
 *
 * RapidIO transfers <= 4 bytes will use the standar io.h in_*
 * and out_* access functions, this is not configurable.
 *
 * Returns: On success - 0
 *          On failure != 0
 *
 * NOTE: Once you have called this function, use rio_dio_method_put()
 *       to give up your reference instead of freeing the
 *       RIO direct I/O method data structure directly.
 */

int rio_dio_method_setup(void **dio_channel)
{
	return __rio_dio_method_setup(dio_channel);
}
EXPORT_SYMBOL_GPL(rio_dio_method_setup);

/**
 * rio_dio_method_put - decrement reference count.
 *
 * @dio_channel: RIO direct I/O channel in question.
 *
 * Frees direct I/O channel when the reference count
 * drops to zero
 */

void rio_dio_method_put(void *dio_channel)
{
	__rio_dio_method_put(dio_channel);
}
EXPORT_SYMBOL_GPL(rio_dio_method_put);
