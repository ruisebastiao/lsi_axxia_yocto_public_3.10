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

#ifndef ASM_PPC_RIO_H
#define ASM_PPC_RIO_H

extern void platform_rio_init(void);

#ifdef CONFIG_FSL_RIO
extern int fsl_rio_mcheck_exception(struct pt_regs *);
#else
static inline int fsl_rio_mcheck_exception(struct pt_regs *regs) {return 0; }
#endif

extern int axxia_rio_mcheck_exception(struct pt_regs *regs);

#define DEF_RIO_IN_BE(name, size, op)					\
	static inline int name(u##size * dst,				\
		       const volatile u##size __iomem *addr)		\
	{								\
	int err = 0;							\
	__asm__ __volatile__(						\
		"msync" "\n"						\
		"0:"    op "%U2%X2 %1,%2\n"				\
		"1:     sync\n"						\
		"2:     nop\n"						\
		"3:\n"							\
		".section .fixup,\"ax\"\n"				\
		"4:	li %0,%3\n"					\
		"	b 3b\n"						\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		PPC_LONG_ALIGN "\n"					\
		PPC_LONG "0b,4b\n"					\
		PPC_LONG "1b,4b\n"					\
		PPC_LONG "2b,4b\n"					\
		".previous"						\
		: "=r" (err), "=r" (*dst)				\
		: "m" (*addr), "i" (-EFAULT), "0" (err)			\
		: "memory");						\
	return err;							\
	}

DEF_RIO_IN_BE(rio_in_8, 8, "lbz")
DEF_RIO_IN_BE(rio_in_be16, 16, "lhz")
DEF_RIO_IN_BE(rio_in_be32, 32, "lwz")


/* Bit definitions for the MCSR. */
#define PPC47x_MCSR_L2  0x00200000 /* Error or system error reported through
                                      the L2 cache */
#define PPC47x_MCSR_DCR 0x00100000 /* DCR timeout */


#endif				/* ASM_PPC_RIO_H */


#if defined(CONFIG_AXXIA_RIO) && defined(DRIVERS_RAPIDIO_RIO_H)
#include <asm/axxia-rio.h>
#endif
