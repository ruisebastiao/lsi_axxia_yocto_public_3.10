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

/*! @file      ai2c_sal.h
    @brief     OS Specific definitions are located here.
*/

#ifndef __AI2C_SAL_H__
#define __AI2C_SAL_H__

#include <generated/autoconf.h>

#ifdef __KERNEL__

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/cdev.h>
#include <linux/of_platform.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/kthread.h>
#include <linux/sched.h>

#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <asm/pgtable.h>

#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/spinlock.h>
#include <linux/signal.h>

#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <linux/version.h>

#include <linux/time.h>
#include <linux/fcntl.h>
#include <linux/unistd.h>
#include <linux/errno.h>
#include <linux/mman.h>

#include <asm/byteorder.h>

#else

#include <stdio.h>
#include <stdlib.h>

#endif

#include "ai2c_types.h"
#include "ai2c_dev.h"


/**************************************************************************
* Some Constants
**************************************************************************/

#ifdef __BIG_ENDIAN
#undef  AI2C_BIG_ENDIAN
#define AI2C_BIG_ENDIAN	  9999
#undef  AI2C_LITTLE_ENDIAN
#endif

#ifdef __LITTLE_ENDIAN
#undef  AI2C_BIG_ENDIAN
#undef  AI2C_LITTLE_ENDIAN
#define AI2C_LITTLE_ENDIAN      9998
#endif

/**************************************************************************
* Macros
**************************************************************************/

/*
* AI2C_MSG
*
*   Print a message to the system console.
*/
#ifdef __KERNEL__

#define AI2C_MSG(type, fmt, args...)				         \
	do {							         \
		if ((type) & AI2C_MSG_TRACE_LEVEL) {			 \
			if ((type) == AI2C_MSG_ERROR)			 \
				printk(KERN_ERR AI2C_MOD_NAME ": ERROR: "); \
			else						 \
				printk(KERN_WARNING AI2C_MOD_NAME ": "); \
			printk(fmt, ## args);				 \
		}							 \
	} while (0)

#else

#define AI2C_MSG(type, fmt, args...)				         \
	do {							         \
		if ((type) & AI2C_MSG_TRACE_LEVEL) {			 \
			if ((type) == AI2C_MSG_ERROR)			 \
				printf("[Error] " AI2C_MOD_NAME ": ERROR: "); \
			else						 \
				printf("[Warning] " AI2C_MOD_NAME ": "); \
			printf(fmt, ## args);				 \
		}							 \
	} while (0)

#endif

    /*
     * AI2C_LOG
     *
     *   Print a message to the system log device and/or console. This
     *   interface is callable from interrupt level.
     */
#define AI2C_LOG \
	AI2C_MSG

#ifndef AI2C_MSG_TRACE_LEVEL
#define AI2C_MSG_TRACE_LEVEL     ai2c_trace_level
#endif

extern int AI2C_MSG_TRACE_LEVEL;


/*
* Endian-ness Conversion
*/

#define AI2C_SWAP16m(n) \
	((((u16)(n) >>  8) & 0x00ff) |  \
	(((u16)(n) <<  8) & 0xff00))

#define AI2C_SWAP32m(n) \
	((((u32)(n) >> 24) & 0x000000ff) |  \
	(((u32)(n) >>  8) & 0x0000ff00) |  \
	(((u32)(n) <<  8) & 0x00ff0000) |  \
	(((u32)(n) << 24) & 0xff000000))

#define SWAP16(x)	\
	{ { \
		u16 val = x; \
		AI2C_SWAP16m(val); \
	} }

#define SWAP32(x)	\
	{ { \
		u32 val = x; \
		AI2C_SWAP32m(val); \
	} }


/*
* Endian-ness I/O
*/

#ifdef CONFIG_ARM

#define in_be8(x)		(*x)
#define in_be16(x)		AI2C_SWAP16m(*x)
#define in_be32(x)		AI2C_SWAP32m(*x)

#define in_le8(x)		(*x)
#define in_le16(x)		(*x)
#define in_le32(x)		(*x)

#define out_be8(a, v)	        (*a) = (v)
#define out_be16(a, v)	        (*a) = AI2C_SWAP16m(v)
#define out_be32(a, v)	        (*a) = AI2C_SWAP32m(v)

#define out_le8(a, v)	        (*a) = (v)
#define out_le16(a, v)	        (*a) = (v)
#define out_le32(a, v)	        (*a) = (v)

#endif  /* CONFIG_ARM */


#define AI2C_EDEV_BUS_ENFORCE_ORDERING()

#define AI2C_BUS_READ8(addr) \
	readb((u8 *) (addr))

#define AI2C_BUS_READ16_ENDIAN(endian, addr) \
	in_##endian##16((u16 __iomem *) (addr))


#define AI2C_BUS_READ16_LE(addr) AI2C_BUS_READ16_ENDIAN(le, addr)

#define AI2C_BUS_READ16_BE(addr) AI2C_BUS_READ16_ENDIAN(be, addr)

#define AI2C_BUS_READ16(addr, endian) \
	(endian == AI2C_DEV_ACCESS_BIG_ENDIAN) ?   \
		AI2C_BUS_READ16_BE(addr) : AI2C_BUS_READ16_LE(addr)

#define AI2C_BUS_READ32_ENDIAN(endian, addr) \
	in_##endian##32((u32 __iomem *) (addr))


#define AI2C_BUS_READ32_LE(addr) AI2C_BUS_READ32_ENDIAN(le, addr)

#define AI2C_BUS_READ32_BE(addr) AI2C_BUS_READ32_ENDIAN(be, addr)

#define AI2C_BUS_READ32(addr, endian) \
	(endian == AI2C_DEV_ACCESS_BIG_ENDIAN) ?   \
	AI2C_BUS_READ32_BE(addr) : AI2C_BUS_READ32_LE(addr)


#define AI2C_BUS_WRITE8(addr, data) \
	writeb((data), (u8 *) (addr))

#define AI2C_BUS_WRITE16_ENDIAN(endian, addr, data) \
	do { \
		u16 *__a__ = (u16 *) addr; \
		u16 __d__ = data; \
		out_##endian##16((u16 __iomem *) __a__, __d__); \
		AI2C_EDEV_BUS_ENFORCE_ORDERING(); \
	} while (0);

#define AI2C_BUS_WRITE16_LE(addr, data) \
	AI2C_BUS_WRITE16_ENDIAN(le, addr, data)

#define AI2C_BUS_WRITE16_BE(addr, data) \
	AI2C_BUS_WRITE16_ENDIAN(be, addr, data)

#define AI2C_BUS_WRITE16(addr, data, endian) \
	do { \
		if (endian == AI2C_DEV_ACCESS_BIG_ENDIAN) {  \
			AI2C_BUS_WRITE16_BE(addr, data);    \
		} else { \
			AI2C_BUS_WRITE16_LE(addr, data);    \
		} \
	} while (0);

#define AI2C_BUS_WRITE32_ENDIAN(endian, addr, data) \
	do { \
		u32 *__a__ = (u32 *) addr; \
		u32 __d__ = data; \
		out_##endian##32((u32 __iomem *) __a__, __d__); \
		AI2C_EDEV_BUS_ENFORCE_ORDERING(); \
	} while (0);

#define AI2C_BUS_WRITE32_LE(addr, data) \
	AI2C_BUS_WRITE32_ENDIAN(le, addr, data)

#define AI2C_BUS_WRITE32_BE(addr, data) \
	AI2C_BUS_WRITE32_ENDIAN(be, addr, data)

#define AI2C_BUS_WRITE32(addr, data, endian) \
	do { \
		if (endian == AI2C_DEV_ACCESS_BIG_ENDIAN) {  \
			AI2C_BUS_WRITE32_BE(addr, data);    \
		} else {			            \
			AI2C_BUS_WRITE32_LE(addr, data);    \
		} \
	} while (0);

    /*
    * Spinlock mutex stuff
    */

#define AI2C_SPINLOCK_INIT(pSpinlock) \
	spin_lock_init(pSpinlock)

#define AI2C_SPINLOCK_LOCK(pSpinlock) \
	spin_lock(pSpinlock)

#define AI2C_SPINLOCK_TRYLOCK(pSpinlock) \
	spin_trylock(pSpinlock)

#define AI2C_SPINLOCK_UNLOCK(pSpinlock) \
	spin_unlock(pSpinlock)

#define AI2C_SPINLOCK_INTERRUPT_DISABLE(pSem, flags) \
	spin_lock_irqsave(pSem, flags)

#define AI2C_SPINLOCK_INTERRUPT_ENABLE(pSem, flags) \
	spin_unlock_irqrestore(pSem, flags)

#define AI2C_SPINLOCK_SW_INTERRUPT_DISABLE(pSem, flags) \
	spin_lock_bh(pSem)

#define AI2C_SPINLOCK_SW_INTERRUPT_ENABLE(pSem, flags) \
	spin_unlock_bh(pSem)


#ifdef __KERNEL__
    /*
    * Kernel memory allocation
    */

#define __ai2c_malloc(size)		kmalloc(size, GFP_KERNEL)
#define __ai2c_free(ptr)		   kfree(ptr)
#define __ai2c_realloc(ptr, size)	 (NULL)
#define __ai2c_calloc(no, size)	   kcalloc(no, size, GFP_KERNEL)

#else

    /*
    * User space memory allocation
    */

#define __ai2c_malloc(size)		malloc(size)
#define __ai2c_free(ptr)		free(ptr)
#define __ai2c_realloc(ptr, size)	(NULL)
#define __ai2c_calloc(no, size)	        calloc(no, size)

#endif


    /*
    * Miscellaneous externs not provided by other headers reliably
    */

extern int snprintf(char *s, size_t n, const char *format, ...);

struct ai2c_rev_id {

#ifdef NCP_BIG_ENDIAN
	unsigned isAsic:1;
	unsigned isFpga:1;
	unsigned isSim:1;
	unsigned:2;
	unsigned secDisable:1;
	unsigned sppDisable:1;
	unsigned cpuDisable:4;
	unsigned ecidChipType:5;
	unsigned:1;
	unsigned packageType:4;
	unsigned chipVersion:6;
	unsigned chipTyp:5;
#else
	unsigned chipType:5;
	unsigned chipVersion:6;
	unsigned packageType:4;
	unsigned:1;
	unsigned ecidChipType:5;
	unsigned cpuDisable:4;
	unsigned sppDisable:1;
	unsigned secDisable:1;
	unsigned:2;
	unsigned isSim:1;
	unsigned isFpga:1;
	unsigned isAsic:1;
#endif
};


/**************************************************************************
* More Macros
**************************************************************************/

/* Should this be in sal? */
#ifndef MIN
#define MIN(a, b)	((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b)	((a) > (b) ? (a) : (b))
#endif

/**************************************************************************
* Function Prototypes
**************************************************************************/

extern void *ai2c_malloc(size_t size);
extern void *ai2c_realloc(void *ptr, size_t size);
extern void *ai2c_calloc(size_t no, size_t size);
extern void  ai2c_free(void *ptr);

#endif /* __AI2C_SAL_H__ */
