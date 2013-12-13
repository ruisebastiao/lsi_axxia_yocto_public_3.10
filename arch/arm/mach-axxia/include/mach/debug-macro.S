/* arch/arm/mach-realview/include/mach/debug-macro.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * -- NOTE --
 *
*/

#if !defined(CONFIG_ARM_LPAE)
#error "Axxia Peripherals Are Only Accessible Using the LPAE!"
#endif

#define UART0_PHYSICAL_ADDRESS 0x0000002010080000ULL
#define UART1_PHYSICAL_ADDRESS 0x0000002010081000ULL
#define UART2_PHYSICAL_ADDRESS 0x0000002010082000ULL
#define UART3_PHYSICAL_ADDRESS 0x0000002010083000ULL

#define UART0_VIRTUAL_ADDRESS 0xf0080000
#define UART1_VIRTUAL_ADDRESS 0xf0081000
#define UART2_VIRTUAL_ADDRESS 0xf0082000
#define UART3_VIRTUAL_ADDRESS 0xf0083000

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =0x10080000
	ldr	\rv, =0xf0080000
	.endm

#include <asm/hardware/debug-pl01x.S>
