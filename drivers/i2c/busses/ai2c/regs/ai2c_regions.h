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

/*! @file      ai2c_regions.h
    @brief     Defines to access induvidual regions within the NCP.
*/

#ifndef __AI2C_REGIONS_H__
#define __AI2C_REGIONS_H__


/*!
 * Node definitions for all nodes/engines in NCP/X1.
 * This needs to be kept in-sync with HW.
 */

#define AI2C_NODE_X1_ECID             0x0A


/* ---------------------------------- */
/* --- AI2C Region ID Support     --- */
/* ---------------------------------- */


#define AI2C_NODE_MASK             (0xFFFF)
#define AI2C_TARGET_MASK           (0xFFFF)

#define AI2C_NODE_ID(regionId)    (((regionId) >> 16) & AI2C_NODE_MASK)
#define AI2C_TARGET_ID(regionId)  ((regionId) & AI2C_TARGET_MASK)


#define AI2C_REGION_ID(node, target) \
	((((node) & AI2C_NODE_MASK) << 16) | ((target) & AI2C_TARGET_MASK))


/* ---------------------------------- */
/* --- AI2C Region ID Definitions --- */
/* ---------------------------------- */

/* Temporary dummy regions */
#define AI2C_REGION_NULL       (AI2C_REGION_ID(0xffff, 0xffff))


#define AI2C_REGION_NCA_CFG    (AI2C_REGION_ID(0x16,  0xff))


#define AI2C_REGION_GPIO_0     (AI2C_REGION_ID(0x140,  0)) /* 320.0 */
#define AI2C_REGION_I2C_0      (AI2C_REGION_ID(0x143,  0)) /* 323.0 */
#define AI2C_REGION_TIMER      (AI2C_REGION_ID(0x146,  0)) /* 326.0 */
#define AI2C_REGION_GPREG      (AI2C_REGION_ID(0x149,  0)) /* 329.0 */
#define AI2C_REGION_I2C_1      (AI2C_REGION_ID(0x14c,  0)) /* 332.0 */
#define AI2C_REGION_I2C_2      (AI2C_REGION_ID(0x152,  0)) /* 338.0 */

#define AI2C_REGION_SMB        (AI2C_REGION_ID(0x15c,  0)) /* 348.0 */
#define AI2C_REGION_I2C_3      AI2C_REGION_SMB

#define AI2C_REGION_CLK_CTRL   (AI2C_REGION_ID(0x18d,  0)) /* 397.0 */

#define AI2C_REGION_RESET_CTRL (AI2C_REGION_ID(0x18e,  0)) /* 398.0 */


#endif /* __AI2C_REGIONS_H__ */
