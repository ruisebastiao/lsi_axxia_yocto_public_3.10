#ifndef _RIO_ROUTE_H
#define _RIO_ROUTE_H

/*
 * RapidIO job support
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/device.h>
#include <linux/rio.h>

extern int rio_route_add(struct rio_dev *rdev, u16 table, u16 route_destid,
			 u8 route_port, int lock);
extern int rio_route_get_port(struct rio_dev *rdev,
			      u16 destid, u8 *port, int lock);
extern int rio_add_route_for_destid(struct rio_mport *mport,
				    u16 parent_dest, u8 port_num,
				    u16 destid, int lock);
extern int rio_remove_route_for_destid(struct rio_mport *mport,
				       u16 destid, int lock);
extern int rio_update_route_for_destid(struct rio_mport *mport,
				       u16 destid, int lock);
extern int rio_init_lut(struct rio_dev *rdev, int lock);
extern int rio_update_routes(struct rio_mport *mport,
			     struct rio_dev **rptr, int rnum);

#endif
