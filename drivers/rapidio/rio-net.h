#ifndef RIO_NET_H
#define RIO_NET_H

/*
 * RapidIO interconnect services
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/device.h>
#include <linux/list.h>
#include <linux/rio.h>

#define RIO_DEV_NOT_ADDED (0)
#define RIO_DEV_IS_SWITCH (1)
#define RIO_DEV_DISABLED (2)


extern int rio_device_enable(struct rio_dev *rdev);
extern int rio_device_disable(struct rio_dev *rdev);
extern int rio_switch_port_is_active(struct rio_dev *rdev, int sport);

#endif
