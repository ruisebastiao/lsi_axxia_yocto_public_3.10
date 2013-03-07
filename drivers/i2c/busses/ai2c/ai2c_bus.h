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

#include "ai2c_types.h"
#include "regs/ai2c_i2c_regs.h"
#include "ai2c_plat.h"

/*****************************************************************************
* Constants                                                                  *
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


/*****************************************************************************
* Type definitions                                                           *
*****************************************************************************/

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


/*****************************************************************************
* Externally Visible Function Prototypes                                     *
*****************************************************************************/

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

#endif   /* defined(AI2C_BUS_H) */
