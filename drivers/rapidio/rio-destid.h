#ifndef RIO_DESTID_H
#define RIO_DESTID_H

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

extern int rio_pin_destid(struct rio_mport *mport, u16 parent_destid,
			  int parent_port, int hopcount);
extern int rio_unlock_destid(struct rio_mport *mport, u16 parent_destid,
			     int parent_port, int hopcount);

extern int rio_set_device_id(struct rio_mport *mport, u16 destid,
			     u8 hopcount, u16 did);
extern int rio_assign_destid(struct rio_dev *rdev, struct rio_mport *port,
			     u16 destid, int hopcount, u16 *id);
extern int rio_get_next_destid(struct rio_mport *mport, u16 parent_destid,
			       int port_num, u8 hopcount,
			       u16 *id, int *comptag);
extern int rio_get_next_netid(u16 host_deviceid, int *net_id, int *comptag);

extern int rio_pin_netid(u16 host_deviceid, int net_id);
extern int rio_unlock_netid(u16 host_deviceid, int net_id);

extern int rio_dest_is_redundant(struct rio_mport *mport, u16 parent_destid,
				 int parent_port, int hopcount);
extern int rio_dest_is_legacy(struct rio_mport *mport, u16 parent_destid,
			      int parent_port, int hopcount);
extern int rio_dest_is_one_way(struct rio_mport *mport, u16 parent_destid,
			       int parent_port, int hopcount);

extern int rio_eval_destid(struct rio_dev *new_rdev);
extern int rio_get_destid(struct rio_mport *mport, u16 destid, u8 hopcount,
			  u16 *res_destid);
extern int rio_read_comptag(struct rio_mport *mport, u16 destid, u8 hopcount,
			    u32 *comptag);
extern int rio_set_master_destid(struct rio_mport *mport, u16 did);
extern int rio_has_destid(int src_ops, int dst_ops);
extern int rio_destid_sysfs_init(struct rio_mport *mport);
extern int rio_get_return_port(struct rio_mport *mport, u16 parent_destid,
			       int parent_port, int hopcount, u8 *rport);
extern int rio_get_legacy_properties(struct rio_mport *mport,
				     u16 parent_destid, int parent_port,
				     int hopcount, u8 *lock_hw,
				     u8 *lut_update);
extern int rio_destid_get_parent_port(struct rio_mport *mport,
				      u16 destid, u8 *port);

#if !defined(CONFIG_RAPIDIO_STATIC_DESTID)

extern int rio_add_netid(u16 mport_destid, int net_id, int comptag);

extern int rio_add_destid(struct rio_mport *mport,
			  u16 parent_destid, int parent_port,
			  int hopcount, u16 destid, u16 comptag);
extern int rio_block_destid_route(struct rio_mport *mport,
				  u16 parent_destid, int parent_port,
				  int hopcount, u16 destid, u16 comptag);
extern int rio_split_destid_route(struct rio_mport *mport,
				  u16 parent_destid, int parent_port,
				  int hopcount, u16 destid, u16 comptag,
				  u8 return_port);
extern int rio_legacy_destid_route(struct rio_mport *mport,
				   u16 parent_destid, int parent_port,
				   int hopcount, u16 destid, u16 comptag,
				   u8 lock_hw, u8 lut_update);
extern void rio_release_destid(struct rio_mport *mport, u16 parent_destid,
			       int parent_port, int hopcount);
#else

extern int rio_lookup_static_route(struct rio_dev *rdev, u16 sw_dest,
				   u8 *route_port);

#endif

#endif
