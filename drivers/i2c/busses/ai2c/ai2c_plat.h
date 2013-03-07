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

#ifndef _AI2C_LINUX_H_
#define _AI2C_LINUX_H_

#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/i2c-axxia.h>

#include "ai2c_sal.h"
#include "ai2c_dev.h"

/**************************************************************************
* Constants                                                               *
**************************************************************************/

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

    /*************************************************************************
     * I/O Macros
     *************************************************************************/

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

    /*************************************************************************
     * Debug Macros
     *************************************************************************/

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
* Type Definitions                                                        *
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
* Exportable Functions                                                    *
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

/*****************************************************************************
* Externally Visible Function Prototypes				     *
*****************************************************************************/

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


#endif /* _AI2C_PLAT_H_ */
