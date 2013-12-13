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

/*! @file       ai2c_bus.h
 *  @brief      Constants, structs, and APIs used to communicate with the
 *              direct ACP I2C Hardware Layer registers
 */

#ifndef AI2C_BUS_H
#define AI2C_BUS_H

#include <generated/autoconf.h>

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/cdev.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
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
#include <linux/i2c-axxia.h>

#include <linux/version.h>

#include <linux/time.h>
#include <linux/fcntl.h>
#include <linux/unistd.h>
#include <linux/errno.h>
#include <linux/mman.h>
#include <linux/types.h>

#include <asm/byteorder.h>

/**************************************************************************
* Basic Type Definitions, Constants, #Defines, etc.
**************************************************************************/

    /**************************************************************************
    * Constants, #Defines, etc.
    **************************************************************************/

#ifndef NULL
#define NULL    0
#endif

#ifndef TRUE
#define TRUE    1
#endif

#ifndef FALSE
#define FALSE   0
#endif

    /**************************************************************************
     * ACP chip types
     *
     * These are the base silicon chip types. Each chip may have one
     * or more variants, but for the purpose of the chipType comparison
     * we only care about the base silicon version. For any variant the
     * driver will set the chipType in virtual register 0x301.0.0 to
     * one of the following.
     **************************************************************************/

#define AI2C_CHIP_ACP34xx	1
#define AI2C_CHIP_ACP32xx	2
#define AI2C_CHIP_ACP25xx	6
#define AI2C_CHIP_ACP25xx_V2    7

#define AI2C_CHIP_X3X7_HYBRID   7       /* TEMP HACK */

#define AI2C_CHIP_ACP55xx	9       /* AXM55xx, aka X7 */
#define AI2C_CHIP_ACP35xx       16       /* AXM35xx, aka X3 */


    /**************************************************************************
    * API Configuration Status Codes, Typedefs, etc.
    **************************************************************************/

#define AI2C_ST_SUCCESS	 (0)


    /**************************************************************************
    * Function Call Support Typedefs, Constants, Macros, etc.
    **************************************************************************/

#ifdef AI2C_ERR_DEBUG
#define AI2C_PRINT_LINE_FILE				          \
	AI2C_MSG(AI2C_MSG_INFO, "%s : %s, line = %d\n",	          \
		 ai2c_status_get(ai2cStatus), __FILE__, __LINE__)
#else
#define AI2C_PRINT_LINE_FILE
#endif			   /* AI2C_ERR_DEBUG */

#define AI2C_CALL(ai2cFunc)		\
	do {				   \
		ai2cStatus = (ai2cFunc);	   \
		if (ai2cStatus != AI2C_ST_SUCCESS) { \
			AI2C_PRINT_LINE_FILE;	  \
			goto ai2c_return;	      \
		}				  \
	} while (0);


/* BEGIN: Important forward type references */

struct ai2c_region_io;
struct ai2c_priv;

/* END:   Important forward type references */


/* --- Linux References --- */
#ifndef AI2C_MOD_NAME
#define AI2C_MOD_NAME        "ai2c"
#endif


/* --- Maximum version string length --- */
#define AI2C_DEV_MAX_VERSION_LENGTH  (41)


/* --- AI2C Trace Level Definitions --- */
#define AI2C_MSG_NONE                (0x00000000)
#define AI2C_MSG_INFO                (0x00000001)
#define AI2C_MSG_ERROR               (0x00000002)
#define AI2C_MSG_ENTRY               (0x00000004)
#define AI2C_MSG_EXIT                (0x00000008)
#define AI2C_MSG_CALL                (AI2C_MSG_ENTRY | AI2C_MSG_EXIT)
#define AI2C_MSG_IOR                 (0x00000010)
#define AI2C_MSG_IOW                 (0x00000020)
#define AI2C_MSG_IORW                (AI2C_MSG_IOR | AI2C_MSG_IOW)
#define AI2C_MSG_MEM                 (0x00000040)
#define AI2C_MSG_MDIO                (0x00000080)
#define AI2C_MSG_DEBUG_IO            (0x20000000)
#define AI2C_MSG_DEBUG               (0x40000000)
#define AI2C_MSG_INTR                (0x80000000)
#define AI2C_MSG_ALL                 (0xFFFFFFFF)


/* --- Device Target Access Map --- */
struct ai2c_access_map {
	u64    begin;
	u64    end;
	s32     word_size_in_bytes;
	s32     access_size_in_words;
};


#define AI2C_DUMMY_REGION_MAP_INIT                              \
	{                                                       \
	{ 0x00000000, 0x03000000, AI2C_DEV_ACCESS_RW   },     \
	{          0,          0, AI2C_DEV_ACCESS_NONE }      \
	}


/* --- Internal Types & Definitions --- */

#define AI2C_DEV_ACCESS_NONE            (0x00)
#define AI2C_DEV_ACCESS_READ            (0x01)
#define AI2C_DEV_ACCESS_WRITE           (0x02)
#define AI2C_DEV_ACCESS_RW              (0x03)
#define AI2C_DEV_ACCESS_BIG_ENDIAN      (0x04)
#define AI2C_DEV_ACCESS_LITTLE_ENDIAN   (0x08)


#define AI2C_DEV_SIZE_1KB                (1024*1)
#define AI2C_DEV_SIZE_4KB                (1024*4)
#define AI2C_DEV_SIZE_128KB              (1024*128)
#define AI2C_DEV_SIZE_256KB              (1024*256)
#define AI2C_DEV_SIZE_2MB                (1024*1024*2)
#define AI2C_DEV_SIZE_16MB               (1024*1024*16)
#define AI2C_DEV_SIZE_128MB              (1024*1024*128)
#define AI2C_DEV_SIZE_1GB                (1024*1024*1024)
#define AI2C_DEV_SIZE_NO_SIZE            (0)


/* read/write fn prototypes for region map function pointers */

typedef int (*_ai2c_dev_read_fn_t) (
		   struct ai2c_priv          *priv,
		   struct ai2c_region_io     *region,
		   u64	 offset,
		   u32	*buffer,
		   u32	 count,
		   u32	 flags,
		   u32	 cmdType,
		   u32	 xferWidth);

typedef int (*_ai2c_dev_write_fn_t) (
		   struct ai2c_priv          *priv,
		   struct ai2c_region_io     *region,
		   u64	 offset,
		   u32	*buffer,
		   u32	 count,
		   u32	 flags,
		   u32	 cmdType,
		   u32	 xferWidth);

/*
 * Structure definition(s) for the region map.
 * See above for typedef ai2c_region_io_t.
 */
struct ai2c_region_io {
	u32 regionId;
	struct ai2c_access_map     *accessMap;
	_ai2c_dev_read_fn_t    readFn;
	_ai2c_dev_write_fn_t   writeFn;
	u32          pageId;
};

/*
 * Sometimes it would be better to define a range of similar regions
 * with a single entry in the region map, especially, for regions
 * that are logical or virtual entities that involve interpretation,
 * calculated addresses based upon the regionId, or some other
 * transformation.  The alternate region map such definitions.
 */
struct ai2c_region_iorng {
	u32 startRegionId;
	u32 endRegionId;
	struct ai2c_access_map     *accessMap;
	_ai2c_dev_read_fn_t    readFn;
	_ai2c_dev_write_fn_t   writeFn;
	u32 pageId;
};


/*
 * Basic i/o methods
 */
#define AI2C_NCA_CMD_CRBR            (0x00000004)
#define AI2C_NCA_CMD_CRBW            (0x00000005)

#define AI2C_EDEV_BUS_READ32(dev, p, o, var) \
	ai2c_region_io_map[p].readFn(dev, &ai2c_region_io_map[p], \
	o, (var), 1, 0, AI2C_NCA_CMD_CRBR, 4)

#define AI2C_EDEV_BUS_BLOCK_READ32(dev, p, o, cnt, var) \
	ai2c_region_io_map[p].readFn(dev, &ai2c_region_io_map[p], \
	o, (var), cnt, 0, AI2C_NCA_CMD_CRBR, 4)

#define AI2C_EDEV_BUS_WRITE32(dev, p, o, var) \
	ai2c_region_io_map[p].writeFn(dev, &ai2c_region_io_map[p], \
	o, (var), 1, 0, AI2C_NCA_CMD_CRBW, 4)

#define AI2C_EDEV_BUS_BLOCK_WRITE32(dev, p, o, cnt, var) \
	ai2c_region_io_map[p].writeFn(dev, &ai2c_region_io_map[p], \
	o, (var), cnt, 0, AI2C_NCA_CMD_CRBW, 4)


#ifdef DEBUG_EDEV_IO
#define AI2C_WRITE_LOG(ctx, dev, pageId, offset, value) \
	AI2C_MSG(AI2C_MSG_DEBUG_IO, \
	    "%s: pageId=0x%x offset=0x%x addr=0x%x value=0x%02x\n", \
	    ctx, pageId, offset, AI2C_DEV_BUS_ADDR(dev, pageId, offset), value)
#else
#define AI2C_WRITE_LOG(ctx, dev, pageId, offset, value)
#endif

#define AI2C_DEV_BUS_READ8(dev, pageId, offset) \
	AI2C_BUS_READ8(AI2C_DEV_BUS_ADDR(dev, pageId, offset),\
		AI2C_DEV_PAGE_ENDIANNESS(pageId))

#define AI2C_DEV_BUS_READ16(dev, pageId, offset) \
	AI2C_BUS_READ16(AI2C_DEV_BUS_ADDR(dev, pageId, offset),\
		AI2C_DEV_PAGE_ENDIANNESS(pageId))

#define AI2C_DEV_BUS_READ32(dev, pageId, offset) \
	AI2C_BUS_READ32(AI2C_DEV_BUS_ADDR(dev, pageId, offset),\
		AI2C_DEV_PAGE_ENDIANNESS(pageId))

#define AI2C_DEV_BUS_WRITE8(dev, pageId, offset, value) \
	do { \
		AI2C_WRITE_LOG("edev_bus_write8", dev, pageId, offset, value); \
		AI2C_BUS_WRITE8( \
		AI2C_DEV_BUS_ADDR(dev, pageId, offset), value); \
		if (AI2C_DEV_PAGE_FLAGS(pageId) == AI2C_IO_SYNC) { \
			u32 ___val___; \
			___val___ = AI2C_BUS_READ32(AI2C_DEV_BUS_ADDR(dev, \
			AI2C_DEV_PAGE_PCIE0_PEI, AI2C_PEI_CONFIG), \
			AI2C_DEV_ACCESS_LITTLE_ENDIAN); \
		} \
	} while (0);

#define AI2C_DEV_BUS_WRITE16(dev, pageId, offset, value) \
	do { \
		AI2C_WRITE_LOG("edev_bus_write16", \
			dev, pageId, offset, value); \
		AI2C_BUS_WRITE16( \
			AI2C_DEV_BUS_ADDR(dev, pageId, offset), value); \
		if (AI2C_DEV_PAGE_FLAGS(pageId) == AI2C_IO_SYNC) { \
			u32 ___val___; \
			___val___ = AI2C_BUS_READ32(AI2C_DEV_BUS_ADDR(dev, \
			AI2C_DEV_PAGE_PCIE0_PEI, AI2C_PEI_CONFIG), \
			AI2C_DEV_ACCESS_LITTLE_ENDIAN); \
		} \
	} while (0);


    /**************************************************************************
    * Event/Error Logging
    **************************************************************************/

/*
* AI2C_MSG
*
*   Print a message to the system console.
*/
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


    /**************************************************************************
    * Endianness
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


    /*
    * Kernel memory allocation
    */

#define __ai2c_malloc(size)		kmalloc(size, GFP_KERNEL)
#define __ai2c_free(ptr)		   kfree(ptr)
#define __ai2c_realloc(ptr, size)	 (NULL)
#define __ai2c_calloc(no, size)	   kcalloc(no, size, GFP_KERNEL)


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

/*****************************************************************************
* Internal Data Structure Definitions                                        *
*****************************************************************************/

    /**********************************************************************
    * ACP I/O Mapped Functions Stuff                                      *
    **********************************************************************/

#define  __ai2c_dev_direct_read          ai2c_dev_direct_read
#define  __ai2c_dev_direct_write         ai2c_dev_direct_write
#define  __ai2c_dev_indirect_read        ai2c_dev_indirect_read
#define  __ai2c_dev_indirect_write       ai2c_dev_indirect_write
#define  __ai2c_dev_dcr_read             ai2c_dev_dcr_read
#define  __ai2c_dev_dcr_write            ai2c_dev_dcr_write

/*
 * Enumeration of pages/regions tracked by this driver.
 */
enum {
	AI2C_DEV_PAGE_AHB_BEGIN,             /* Placeholder (0/1) */
	AI2C_DEV_PAGE_I2C_0   = AI2C_DEV_PAGE_AHB_BEGIN,
	AI2C_DEV_PAGE_I2C_1,
	AI2C_DEV_PAGE_I2C_2,
	AI2C_DEV_PAGE_I2C_3                 /* aka SMB */,
	AI2C_DEV_PAGE_GPIO_0,
	AI2C_DEV_PAGE_RESET_CTRL,
	AI2C_DEV_PAGE_TIMER,
	AI2C_DEV_PAGE_GPREG,
	AI2C_DEV_PAGE_AHB_END = AI2C_DEV_PAGE_GPREG,

	AI2C_DEV_PAGE_END_MARKER,
};

#undef  AI2C_DEV_APB_PAGE_BASE
#define AI2C_DEV_APB_PAGE_BASE           AI2C_DEV_PAGE_AHB_BEGIN


    /**************************************************************************
    * Macros                                                                  *
    **************************************************************************/

#define DBGINFO(args...)
	/* General debugging */
#define XDBGINFO(args...)
	/* Miscellaneous debugging, commented out */
#define ADBGINFO(args...)
	/* Address debugging */
#define D1DBGINFO(args...)
	/* Track descriptor chain register modifications */
#define D2DBGINFO(args...)
	/* Track descriptor chain tracking modifications */
#define D3DBGINFO(args...)
	/* Track descriptor chain reset modifications */
#define D4DBGINFO(args...)
	/* Track dme+descriptor chain modifications */
#define ODBGINFO(args...)
	/* Track tx irq transaction */
#define O2DBGINFO(args...)
	/* Track tx foreground transaction */
#define O3DBGINFO(args...)
	/* Track numFree changes for tx transaction */
#define IDBGINFO(args...)
	/* Track rx irq transaction */
#define I2DBGINFO(args...)
	/* Track rx foreground transaction */
#define I3DBGINFO(args...)
	/* Track numFree changes for rx transaction */
#define SDBGINFO(args...)
	/* Track dme select/release */
#define DDBGINFO(args...)
	/* Track dbell irq transaction */
#define EIDBGINFO(args...)
	/* Track enable/disable irqs */
#define GSDBGINFO(args...)      printk(args)
	/* Dump lots of data to console during get_glob_stat */
#undef MDBG_SUPPORT
#ifdef MDBG_SUPPORT
	#define MDBGINFO(args...)   printk(args)
	/* Track maintenance accesses */
#else
	#define MDBGINFO(args...)
#endif

    /**********************************************************************
    * Macros for Paged Sysmem Access Methods                              *
    **********************************************************************/

#define AI2C_EDEV_BUS_PAGE_SHIFT 18
#define AI2C_EDEV_BUS_PAGE_SIZE ((u64) 1 << AI2C_EDEV_BUS_PAGE_SHIFT)

#define AI2C_EDEV_BUS_PAGE_MASK (AI2C_EDEV_BUS_PAGE_SIZE - 1)     /* ??? */
#define AI2C_EDEV_BUS_PAGE_OFFSET(x) \
	((u32) (((x) & (~AI2C_EDEV_BUS_PAGE_MASK)) >> \
	AI2C_EDEV_BUS_PAGE_SHIFT))  /* ??? */


    /**********************************************************************
    * Low-level I/O based upon 'page'                                     *
    **********************************************************************/

#define AI2C_DEV_BUS_ADDR(dev, pageId, offset) \
	((dev)->pageAddr[pageId] + offset)

#define AI2C_DEV_PAGE_ENDIANNESS(pageId) (priv->pages[pageId].endianness)


/**************************************************************************
* Driver State management                                                 *
**************************************************************************/

    /**********************************************************************
    * Support Memory Mappings for Driver State Structure                  *
    **********************************************************************/

#define AI2C_PAGE_FLAGS_NONE            (0x00000000)
#define AI2C_PAGE_FLAGS_I2CBUS          (0x00000001)

struct ai2c_dev_page_s {
	int    pageId;
	char   *busName;
	u32    bus_nr;
	u64    busAddr; /* 38-bit PCI address */
	u32    size;
	u32    endianness;
	u32    flags;
	struct axxia_i2c_bus_platform_data  *pdata;
};

struct ai2c_dev_chip_entry_s {
	u32	chipType;
	char	*chipName;
	u32	numActiveBusses;
	struct ai2c_i2c_access *api;
};


    /**********************************************************************
    * Driver State Structure                                              *
    **********************************************************************/

struct ai2c_priv {
	spinlock_t regLock;
	spinlock_t ioLock;

	struct ai2c_rev_id hw_rev;

	/* Static configuration describing selected ACP I2C bus region */
	struct ai2c_dev_chip_entry_s *busCfg;

	/* Memory Mapping/Management constructs */
	u32 numActiveBusses;
	struct ai2c_dev_page_s *pages;
	/* Per module memory pages */

	/* Memory indexing support to reach selected ACP regions */
	u32 *pageAddr;

	/* Diagnostics */
};

    /**************************************************************************
    * Exportable State                                                        *
    **************************************************************************/

extern int     AI2C_MSG_TRACE_LEVEL;

extern int     ai2c_chip_ver;


    /**************************************************************************
    * Shared Functions                                                        *
    **************************************************************************/

extern int ai2c_dev_read32(
	struct ai2c_priv         *dev,
	u32	regionId,
	u64        offset,
	u32       *buffer);

extern int ai2c_dev_write32(
	struct ai2c_priv         *dev,
	u32        regionId,
	u64        offset,
	u32        buffer);

int ai2c_dev_direct_read(
	struct ai2c_priv      *priv,
	struct ai2c_region_io *region,
	u64     offset,
	u32    *buffer,
	u32     count,
	u32     flags,
	u32     cmdType,
	u32     xferWidth);

int ai2c_dev_direct_write(
	struct ai2c_priv      *priv,
	struct ai2c_region_io *region,
	u64     offset,
	u32    *buffer,
	u32     count,
	u32     flags,
	u32     cmdType,
	u32     xferWidth);

int ai2c_dev_dcr_read(
	struct ai2c_priv      *priv,
	struct ai2c_region_io *region,
	u64     offset,
	u32    *buffer,
	u32     count,
	u32     flags,
	u32     cmdType,
	u32     xferWidth);

int ai2c_dev_dcr_write(
	struct ai2c_priv      *priv,
	struct ai2c_region_io *region,
	u64     offset,
	u32    *buffer,
	u32     count,
	u32     flags,
	u32     cmdType,
	u32     xferWidth);

/*! @fn u32 ai2c_page_to_region(struct ai2c_priv *priv,
 *                                           u32 pageId);
 *  @brief Map a memory page handle to a regionId handle.
    @param[in] inPriv Created device state structure
    @param[in] inPageId Original page id to be mapped
    @Returns mapped value
 */
extern u32 ai2c_page_to_region(struct ai2c_priv *priv, u32 pageId);

/*! @fn u32 *ai2c_region_lookup(struct ai2c_priv *priv,
 *                                           u32 regionId);
 *  @brief Map a memory region handle to a region description structure.
    @param[in] inPriv Created device state structure
    @param[in] inRegionId Original region id to be mapped
    @Returns mapped value
 */
extern struct ai2c_region_io *ai2c_region_lookup(
	struct ai2c_priv *priv,
	u32 regionId);

/*! @fn int ai2c_stateSetup(struct ai2c_priv **outPriv);
    @brief This is a one time initialization for the state linking all
	   of the I2C protocol layers to be called by the device
	   initialization step.
    @param[out] outPriv Created device state structure
    @Returns success/failure status of the operation
*/
extern int ai2c_stateSetup(struct ai2c_priv       **outPriv);

/*! @fn int ai2c_memSetup(struct platform_device *pdev,
			  struct ai2c_priv *priv);
    @brief This is a per-device to-be-mapped setup for the I2C protocol
	   layers to be called by the device initialization step.
    @param[in] inPDev Source platform device data strucure
    @param[in] inPriv Created device state structure
    @Returns success/failure status of the operation
*/
extern int ai2c_memSetup(struct platform_device *pdev,
			 struct ai2c_priv       *priv);

/*! @fn int ai2c_memDestroy(struct ai2c_priv  *inPriv);
    @brief This function will release resources acquired for the specified
	   I2C device driver.
    @param[in] inPriv Created device state structure
    @Returns success/failure status of the operation
*/
extern int ai2c_memDestroy(struct ai2c_priv *inPriv);


/*****************************************************************************
* Bus Device Protocol Definitions                                            *
*****************************************************************************/

    /*****************************
    * Common                     *
    *****************************/

/*! @def AI2C_I2CPROT_MAX_XFR_SIZE
    @brief Maximum number of bytes that may be moved at one time over the
	I2C bus.
*/
#define AI2C_I2CPROT_MAX_XFR_SIZE           8

/*! @def AI2C_I2CPROT_MAX_BUF_SIZE
    @brief Maximum number of bytes that may be stored at one time over
	the I2C bus i.e. size of TXD0+TXD1.
*/
#define AI2C_I2CPROT_MAX_BUF_SIZE           8

/* Max number of tries at looking for an I/O success */
#define AI2C_I2C_CHECK_COUNT				0xFFFFF

	/*****************************
	* ACP3400                    *
	*****************************/

/*! @def AI2C_I2CPROT_MAX_XFR_BOUND
    @brief Value mask that is anded with any I2C offset to determine a
	write transfer boundary.  If a transfer is going to cross this
	byte boundary, it should be broken into two smaller write
	transactions before and after the boundary.
*/
#define AI2C_I2CPROT_MAX_XFR_BOUND          (AI2C_I2CPROT_MAX_XFR_SIZE-1)

/*
 * Device-specific macros and tests for command manipulation
 */
#define AI2C_I2CPROT_MASK_TENBIT_ENABLE           (0x0001)
#define AI2C_I2CPROT_MASK_TENBIT_DISABLE          (0x0002)
#define AI2C_I2CPROT_MASK_TENBIT_CONSECUTIVE      (0x0004)

#define TENBIT_SETENABLED(ioc)       {(ioc)->tenBitMode = \
					AI2C_I2CPROT_MASK_TENBIT_ENABLE; }
#define TENBIT_SETDISABLED(ioc)      {(ioc)->tenBitMode = \
					AI2C_I2CPROT_MASK_TENBIT_DISABLE; }
#define TENBIT_SETCONSECUTIVE(ioc)   {(ioc)->tenBitMode |= \
					AI2C_I2CPROT_MASK_TENBIT_CONSECUTIVE; }
#define TENBIT_CLRCONSECUTIVE(ioc)   {(ioc)->tenBitMode &= \
					~AI2C_I2CPROT_MASK_TENBIT_CONSECUTIVE; }
#define TENBIT_IFENABLED(ioc)        ((ioc)->tenBitMode & \
					AI2C_I2CPROT_MASK_TENBIT_ENABLE)
#define TENBIT_IFDISABLED(ioc)       ((ioc)->tenBitMode & \
					AI2C_I2CPROT_MASK_TENBIT_DISABLE)
#define TENBIT_IFCONSECUTIVE(ioc)    ((ioc)->tenBitMode & \
					AI2C_I2CPROT_MASK_TENBIT_CONSECUTIVE)

#define DEV_10BIT_AUTO(ioc)          TENBIT_SETENABLED(ioc)


	/*******************************************
	* Common Protocol State & Callbacks
	********************************************/

/*! @typedef ai2c_bus_init_t
    @brief Signature for callback function that may be called from I2C
	protocol to initialize environment for an ACP device.
    @param[in] dev Device handle
    @param[in] regionId Reference to specific bus within device
    @returns success/failure of operation
*/
typedef int (*ai2c_bus_init_t)(
		struct ai2c_priv         *priv,
		u32     i2cRegion);

/*! @typedef ai2c_bus_block_write8_t
    @brief Signature for callback function that may be called from I2C
	protocol read/write operations to write 8-bit data to the
	target device.
    @param[in] dev      Device handle
    @param[in] regionId Bus reference handle
    @param[in] *adap    Ptr to I2C adapter
    @param[in] *msg     Ptr to next I2C message to process
    @param[in] stop     Op flag: append 'stop' to this msg
    @returns success/failure of operation
*/
typedef int (*ai2c_bus_block_write8_t)(
		struct ai2c_priv         *priv,
		u32     regionId,
		struct i2c_adapter  *adap,
		struct i2c_msg      *msg,
		int                  stop);

/*! @typedef ai2c_bus_block_read8_t
    @brief Signature for callback function that may be called from I2C
	protocol read/write operations to read 8-bit data from the
	target device.
    @param[in] dev      Device handle
    @param[in] regionId Bus reference handle
    @param[in] *adap    Ptr to I2C adapter
    @param[in] *msg     Ptr to next I2C message to process
    @param[in] stop     Op flag: append 'stop' to this msg
    @returns success/failure of operation
*/
typedef int (*ai2c_bus_block_read8_t)(
		struct ai2c_priv         *priv,
		u32     regionId,
		struct i2c_adapter  *adap,
		struct i2c_msg      *msg,
		int                  stop);

struct ai2c_i2c_access {
	u32            seekPos;
	u32            maxXfrSize;
	/*!< Maximum number of bytes for a single
	* data transfer */
	u32            deviceLen;
	/*!< Maximum number of bytes / seek location
	* where 0 means ignore this setting */
	ai2c_bus_init_t          initFn;
	ai2c_bus_block_write8_t  wrFn;
	ai2c_bus_block_read8_t   rdFn;
	void                    *extra;
};


    /*********************************************
     * AXM5500-like I2C Devices Definitions, etc.
     ********************************************/

extern struct ai2c_i2c_access       ai2c_axm5500_cfg;


    /************************************************************************
    * Externally Visible Function Prototypes                                *
    ************************************************************************/

/*! @fn int ai2c_bus_init(ai2c_priv_t * inDevHdl);
    @brief This is a one time initialization for the I2C protocol
	layers to be called by the chip device initialization step.
    @param[in] inDevHdl Reference to device handle structure
    @Returns success/failure status of the operation
*/
extern int ai2c_bus_init(struct ai2c_priv *priv);

/*! @fn int ai2c_bus_block_read8(ai2c_priv_t *inDev,
			ai2c_region_io_t *inRegion, u64 inOffset,
			u8 *inBuffer, u32 inCount,
			u32 inFlags);
  @brief Read num bytes from the offset and store it in buffer.
  @param[in] dev:    handle of device to access
  @param[in] region: region / slave address to access
  @param[in] offset: Offset into device to address
  @param[in] buffer: Read data will be stored this buffer
  @param[in] count:  Number of bytes to be read.
  @param[in] flags:  Extra flags to pass to low-level device I/O functions
  @Returns success/failure completion status
*/
extern int ai2c_bus_block_read8(
	struct ai2c_priv *priv,
	u64     inOffset,
	u8     *inBuffer,
	u32     inCount,
	u32     inFlags);

/*! @fn int ai2c_bus_block_write8(ai2c_priv_t *inDev,
			u64 inOffset,
			u8 *inBuffer, u32 inCount,
			u32 inFlags);
  @brief Write num bytes to the offset from buffer contents.
  @param[in] dev:    handle of device to access
  @param[in] offset: Offset into device to address
  @param[in] buffer: Read data will be stored this buffer
  @param[in] count:  Number of bytes to be read.
  @param[in] flags:  Extra flags to pass to low-level device I/O functions
  @Returns success/failure completion status
*/
extern int ai2c_bus_block_write8(
	struct ai2c_priv *priv,
	u64     inOffset,
	u8     *outBuffer,
	u32     inCount,
	u32     inFlags);

/*! @fn int ai2c_bus_destroy(ai2c_priv_t * inDevHdl);
    @brief This function will release resources acquired for the specified
	I2C region.
    @param[in] inDevHdl Reference to device handle structure
    @Returns success/failure status of the operation
*/
extern int ai2c_bus_destroy(struct ai2c_priv *priv);

extern int ai2c_dev_clock_mhz(
	struct ai2c_priv         *priv,          /* IN */
	u32       *clockMhz);     /* OUT: Calculated value */


#endif   /* defined(AI2C_BUS_H) */
