#ifndef _RIO_JOB_H
#define _RIO_JOB_H

/*
 * RapidIO job support
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/device.h>
#include <linux/list.h>
#include <linux/rio.h>

#define RIO_JOB_STATE_GO      0
#define RIO_JOB_STATE_INIT    1
#define RIO_JOB_STATE_PENDING 2
#define RIO_JOB_STATE_ABORT   3

struct rio_job {
	struct rio_mport *mport;
	struct rio_dev *rdev;
	int port;
	int event;
	u32 flags;
	int srio_down;
};

#endif
