#ifndef RIO_MULTICAST_H
#define RIO_MULTICAST_H

/*
 * RapidIO multicast
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/rio.h>

extern int rio_multicast_del_all_ports(struct rio_dev *rdev, int maskId);
extern int rio_multicast_add_port(struct rio_dev *rdev,
				  int maskId, int port_num);
extern int rio_multicast_add_assoc(struct rio_dev *rdev,
				   int maskId, int destId);
extern int rio_multicast_del_port(struct rio_dev *rdev,
				  int maskId, int port_num);

extern int rio_init_switch_pw(struct rio_dev *rdev);
extern int rio_update_pw(struct rio_dev *rdev, int sw_port,
			 int add, int lock);


#endif
