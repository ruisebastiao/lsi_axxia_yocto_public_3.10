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

#ifndef _AI2C_CFG_NODE_REG_DEFINES_H_
#define _AI2C_CFG_NODE_REG_DEFINES_H_

    /* NODE 0x1a , TARGET 0xff*/


#define     AI2C_CFG_NODE_NODE_INFO_0                            (0x00000000)
#define     AI2C_CFG_NODE_NODE_INFO_1                            (0x00000004)
#define     AI2C_CFG_NODE_NODE_INFO_2                            (0x00000008)
#define     AI2C_CFG_NODE_NODE_INFO_3                            (0x0000000c)
#define     AI2C_CFG_NODE_NODE_CFG                               (0x00000010)
#define     AI2C_CFG_NODE_WRITE_ERR_ADDR                         (0x00000014)
#define     AI2C_CFG_NODE_NODE_ERROR                             (0x00000018)
#define     AI2C_CFG_NODE_NODE_ERROR_DATA_R                      (0x0000001c)
#define     AI2C_CFG_NODE_NODE_SCRATCH                           (0x00000020)

#endif /* _AI2C_CFG_NODE_REG_DEFINES_H_ */
