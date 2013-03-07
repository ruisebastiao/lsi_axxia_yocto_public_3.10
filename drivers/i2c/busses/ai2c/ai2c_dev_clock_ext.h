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

/*! @file       ai2c_dev_clock_pvt.h
    @brief      Low-level (Device) APIs for clock-related calculations
*/

#ifndef _AI2C_DEV_CLOCK_EXT_H_
#define _AI2C_DEV_CLOCK_EXT_H_

#include "ai2c_dev.h"

/**************************************************************************
* Support Functions APIs                                                  *
**************************************************************************/

extern int ai2c_dev_clock_mhz(
	struct ai2c_priv         *priv,          /* IN */
	u32       *clockMhz);     /* OUT: Calculated value */


#endif /* _AI2C_DEV_CLOCK_EXT_H_ */
