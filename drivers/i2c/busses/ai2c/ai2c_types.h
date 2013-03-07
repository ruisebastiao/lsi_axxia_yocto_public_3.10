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

#ifndef AI2C_TYPES_H
#define AI2C_TYPES_H

#ifdef __KERNEL__

#include <linux/types.h>

#else

#define u64     unsigned long long
#define u32     unsigned long
#define s32     signed long
#define size_t  int

#define AI2C_U32        unsigned long

#define ai2c_uint8_t    unsigned char
#define ai2c_uint16_t   unsigned short
#define ai2c_uint32_t   unsigned long
#define ai2c_uint64_t   unsigned long long
#define ai2c_int8_t     signed char
#define ai2c_int16_t    signed short
#define ai2c_int32_t    signed long
#define ai2c_int64_t    signed long long
#define ai2c_bool_t     unsigned short
#define ai2c_size_t     signed long

#endif

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


#endif  /* AI2C_TYPES_H */
