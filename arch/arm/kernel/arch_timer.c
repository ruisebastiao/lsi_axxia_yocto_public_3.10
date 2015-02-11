/*
 *  linux/arch/arm/kernel/arch_timer.c
 *
 *  Copyright (C) 2011 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>

#include <asm/delay.h>
#include <asm/sched_clock.h>

#include <clocksource/arm_arch_timer.h>

static unsigned long arch_timer_read_counter_long(void)
{
	return arch_timer_read_counter();
}

static u32 sched_clock_mult __read_mostly;
static u32 sched_clock_shift __read_mostly;

static unsigned long long notrace arch_timer_sched_clock(void)
{
	unsigned long long tmp = arch_timer_read_counter();
	return (tmp * sched_clock_mult) >> sched_clock_shift;
}

static struct delay_timer arch_delay_timer;

static void __init arch_timer_delay_timer_register(void)
{
	/* Use the architected timer for the delay loop. */
	arch_delay_timer.read_current_timer = arch_timer_read_counter_long;
	arch_delay_timer.freq = arch_timer_get_rate();
	register_current_timer_delay(&arch_delay_timer);
}

int __init arch_timer_arch_init(void)
{
	u32 arch_timer_rate = arch_timer_get_rate();
	u64 maxsec;

	if (arch_timer_rate == 0)
		return -ENXIO;

	arch_timer_delay_timer_register();

	/* Select maxsec so we get a large conversion range (56 bits) for the
	 * mult/shift while making sure the resulting maxsec fits in 32 bits.
	 */
	maxsec = 1ULL << 56;
	do_div(maxsec, arch_timer_rate);
	if ((maxsec >> 32) != 0)
		maxsec = 0xffffffff;

	/* Cache the multiplier and shift to save a divide in the hot path. */
	clocks_calc_mult_shift(&sched_clock_mult, &sched_clock_shift,
			       arch_timer_rate, NSEC_PER_SEC, (u32)maxsec);
	sched_clock_func = arch_timer_sched_clock;
	pr_info("sched_clock: ARM arch timer >56 bits at %ukHz, resolution %uns\n",
		arch_timer_rate / 1000, sched_clock_mult);

	return 0;
}
