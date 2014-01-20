#ifndef _RIO_DIO_H_
#define _RIO_DIO_H_


#define RIO_IO_READ_HOME        0x00
#define RIO_MAINT_READ		0x01
#define RIO_MAINT_WRITE		0x10
#define RIO_NREAD		0x02
#define RIO_NWRITE		0x20
#define RIO_NWRITE_R		0x40
#define RIO_SWRITE		0x80

#ifdef __KERNEL__

#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>

struct rio_dio_win {
	struct list_head node;
	void *dio_channel;
	struct rio_dev *rdev;
	unsigned outb_win;
	resource_size_t win_size;
	struct kref kref;
};

/*
 Setup / release methods
*/
struct rio_dio_win *rio_dio_req_region(void *dio_channel,
				       struct rio_dev *rdev,
				       resource_size_t size,
				       u32 flags);
void rio_dio_region_put(struct rio_dio_win *io_win);
int rio_dio_method_setup(void **dio_channel);
void rio_dio_method_put(void *dio_channel);

/*
  Access methods
*/
int rio_dio_read_8(struct rio_dio_win *io_win, u32 offset,
		   u32 mflags, u8 *value);
int rio_dio_read_16(struct rio_dio_win *io_win, u32 offset,
		    u32 mflags, u16 *value);
int rio_dio_read_32(struct rio_dio_win *io_win, u32 offset,
		    u32 mflags, u32 *value);
int rio_dio_read_buff(struct rio_dio_win *io_win, u32 offset,
		      u8 *dst, u32 mflags, u32 len);
int rio_dio_write_8(struct rio_dio_win *io_win,
		    u32 offset, u32 mflags, u8 *src);
int rio_dio_write_16(struct rio_dio_win *io_win,
		     u32 offset, u32 mflags, u16 *src);
int rio_dio_write_32(struct rio_dio_win *io_win, u32 offset,
		     u32 mflags, u32 *src);
int rio_dio_write_buff(struct rio_dio_win *io_win, u32 offset,
		       u8 *src, u32 mflags, u32 len);

int rio_dio_const_win(struct rio_dio_win *io_win, void *buf, u32 len,
		      enum dma_data_direction dir, struct rio_map_addr *res);

#endif /* __KERNEL__ */

#endif
