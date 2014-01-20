/*
 * RapidIO architecture support
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef ASM_ARM_RIO_H
#define ASM_ARM_RIO_H

#include <asm/io.h>

extern void platform_rio_init(void);

#if defined(CONFIG_AXXIA_RIO)

#define rio_in_8(src, dst)      ({ insb((long unsigned int)src, dst, 8); 0; })
#define rio_in_be16(src, dst)   ({ insw((long unsigned int)src, dst, 16); 0; })
#define rio_in_be32(src, dst)   ({ insl((long unsigned int)src, dst, 32); 0; })

#define out_8(dst, val)         outb_p(val, (long unsigned int)(dst))
#define out_be16(dst, val)      outw_p(val, (long unsigned int)(dst))

#endif

#define iosync()

#endif /* ASM_ARM_RIO_H */

#if defined(CONFIG_AXXIA_RIO) && defined(DRIVERS_RAPIDIO_RIO_H)
#include <asm/axxia-rio.h>
#endif
