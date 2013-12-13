/*
 * Helper module for board specific I2C bus registration
 *
 * Copyright (C) 2013 LSI Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */
#ifndef __ASM__ARCH_AXXIA_I2C_H
#define __ASM__ARCH_AXXIA_I2C_H


/*
 * Default bus id to expect for an AXXIA platform.
 */
#define ARCH_AXXIA_MAX_I2C_BUSSES       1
#define ARCH_AXXIA_MAX_I2C_BUS_NR       2


extern int axxia_register_i2c_busses(void);


#endif /* __ASM__ARCH_AXXIA_I2C_H */
