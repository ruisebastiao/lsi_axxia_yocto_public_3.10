/*
 * RapidIO route support
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/rio_ids.h>
#include <linux/rio_regs.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/radix-tree.h>

#include "rio.h"

/**
 * rio_route_add - Add a route entry to a switch routing table
 * @rdev: RIO device
 * @table: Routing table ID
 * @route_destid: Destination ID to be routed
 * @route_port: Port number to be routed
 * @lock: lock switch device flag
 *
 * Calls the switch specific add_entry() method to add a route entry
 * on a switch. The route table can be specified using the @table
 * argument if a switch has per port routing tables or the normal
 * use is to specific all tables (or the global table) by passing
 * %RIO_GLOBAL_TABLE in @table. Returns %0 on success or %-EINVAL
 * on failure.
 */
int rio_route_add(struct rio_dev *rdev, u16 table, u16 route_destid,
u8 route_port, int lock)
{
	int err = 0;
	int rc = 0;

	if (!(rdev->rswitch->update_lut))
		return rc;

	if (lock && rdev->use_hw_lock) {
		rc = rio_hw_lock_wait(rdev->hport, rdev->destid,
				      rdev->hopcount, 1);
		if (rc)
			return rc;
	}

	err = rdev->rswitch->add_entry(rdev->hport, rdev->destid,
				       rdev->hopcount, table,
				       route_destid, route_port);

	if (lock && rdev->use_hw_lock)
		rc = rio_hw_unlock(rdev->hport, rdev->destid,
				   rdev->hopcount);

	return err ? err : rc;
}

int rio_route_get_port(struct rio_dev *rdev, u16 destid, u8 *port, int lock)
{
	int rc = 0;

	if (lock && rdev->use_hw_lock) {
		rc = rio_hw_lock_wait(rdev->hport, rdev->destid,
				      rdev->hopcount, 1);
		if (rc)
			return rc;
	}

	rdev->rswitch->get_entry(rdev->hport, rdev->destid,
				 rdev->hopcount, RIO_GLOBAL_TABLE,
				 destid, port);

	if (lock && rdev->use_hw_lock)
		rc = rio_hw_unlock(rdev->hport, rdev->destid,
				   rdev->hopcount);

	return rc;
}

int rio_add_route_for_destid(struct rio_mport *mport,
			     u16 parent_dest, u8 port_num,
			     u16 destid, int lock)
{
	struct rio_dev *curr;
	u8 sport = port_num;
	u16 curr_dest = parent_dest;
	int rc = 0;

	do {
		curr = lookup_rdev(mport, curr_dest);
		if (!curr)
			break;

		if (!rio_is_switch(curr) || !curr->rswitch->update_lut) {
			rio_dev_put(curr);
			break;
		}
		if (curr->local_domain) {

			pr_debug("RIO: add route for %hhu in %s at port %d\n",
				 destid, rio_name(curr), sport);

			rc = rio_route_add(curr, RIO_GLOBAL_TABLE,
					   destid,
					   sport, lock);
			if (rc < 0) {
				rio_dev_put(curr);
				break;
			}
		}
		curr_dest = curr->prev_destid;
		sport = curr->prev_port;
		rio_dev_put(curr);

	} while (curr_dest != RIO_INVALID_DESTID);

	return rc;
}
int rio_remove_route_for_destid(struct rio_mport *mport, u16 destid, int lock)
{
	int i, num = 0, rc = 0;
	struct rio_dev **dptr = rio_get_tagged_devices(mport,
						       RIO_DEV_IS_SWITCH,
						       &num);

	if (!dptr)
		return 0;
	if (IS_ERR(dptr))
		return PTR_ERR(dptr);

	for (i = 0; i < num; i++) {
		struct rio_dev *tmp = dptr[i];
		if (unlikely(!tmp))
			continue;
		if (!tmp->local_domain || !tmp->rswitch->update_lut) {
			rio_dev_put(tmp);
			continue;
		}
		pr_debug("RIO:%s remove route for %hu in %s\n",
			 __func__, destid, rio_name(tmp));

		rc = rio_route_add(tmp, RIO_GLOBAL_TABLE,
				   destid, RIO_INVALID_ROUTE, lock);
		if (rc)
			tmp->rswitch->update_lut = 0;
		rio_dev_put(tmp);
	}
	if (dptr)
		kfree(dptr);
	return rc;
}

int rio_update_routes(struct rio_mport *mport, struct rio_dev **rptr, int rnum)
{
	int d, i, num = 0, rio_fault = 0, rc = 0;
	struct rio_dev **dptr = rio_get_tagged_devices(mport,
						       RIO_DEV_IS_SWITCH,
						       &num);

	if (!dptr)
		return 0;
	if (IS_ERR(dptr))
		return PTR_ERR(dptr);

	for (d = 0; d < rnum; d++) {
		u16 destid;
		u32 result;
		if (rio_fault)
			break;
		if (!rptr[d])
			continue;
		destid = rptr[d]->destid;
		for (i = 0; i < num; i++) {
			u8 port, static_port = RIO_INVALID_ROUTE;
			int disabled;
			struct rio_dev *tmp = dptr[i];

			if (!tmp)
				continue;
			if (tmp->destid == destid ||
			    !tmp->local_domain ||
			    !tmp->rswitch->update_lut)
				continue;

			rcu_read_lock();
			disabled = radix_tree_tag_get(&mport->net.dev_tree,
						      tmp->destid,
						      RIO_DEV_DISABLED);
			rcu_read_unlock();

			rc = rio_route_get_port(tmp, destid, &port,
						(disabled ? 0 : 1));
			if (rc) {
				if (++rio_fault >
					CONFIG_RAPIDIO_ACCESS_ERR_LIMIT)
					break;
				continue;
			}
#ifdef CONFIG_RAPIDIO_STATIC_DESTID
			rio_lookup_static_route(rptr[d], tmp->destid,
						&static_port);
#endif
			pr_debug("RIO: Check route for destid %hx in %s static port %hhu\n",
				 destid, rio_name(tmp), static_port);
			if ((port == RIO_INVALID_ROUTE)  ||
			    ((static_port != RIO_INVALID_ROUTE) &&
			     (static_port != port))) {
				u8 sw_port = tmp->return_port;

				if (static_port != RIO_INVALID_ROUTE)
					sw_port = static_port;
				pr_debug("RIO: Add route for destid %hx at port %d in %s\n",
					 destid, sw_port, rio_name(tmp));
				rc = rio_route_add(tmp, RIO_GLOBAL_TABLE,
						   destid, sw_port,
						   (disabled ? 0 : 1));
				if (rc) {
					if (++rio_fault >
					    CONFIG_RAPIDIO_ACCESS_ERR_LIMIT)
						break;
				}
			}
		}
		/* verify dev access after route update */
		rc = rio_read_config_32(rptr[d],
					rptr[d]->phys_efptr +
					RIO_PORT_GEN_CTL_CSR,
					&result);
		if (rc != 0) {
			if (++rio_fault > CONFIG_RAPIDIO_ACCESS_ERR_LIMIT)
				break;
		}
	}
	for (i = 0; i < num; i++) {
		if (dptr[i])
			rio_dev_put(dptr[i]);
	}
	kfree(dptr);
	return (rio_fault > CONFIG_RAPIDIO_ACCESS_ERR_LIMIT ? rc : 0);
}

int rio_init_lut(struct rio_dev *rdev, int lock)
{
	struct rio_mport *mport = rdev->hport;
	int i, num = 0, rio_fault = 0, rc = 0;
	struct rio_dev **dptr = NULL;

	if (!rdev->rswitch->update_lut)
		return 0;

	dptr = rio_get_all_devices(mport, &num);
	if (!dptr)
		return 0;
	if (IS_ERR(dptr))
		return PTR_ERR(dptr);

	for (i = 0; i < num; i++) {
		struct rio_dev *tmp = dptr[i];

		if (unlikely(!tmp))
			continue;
		if (rio_fault || (!tmp->local_domain && !rdev->local_domain)) {
			rio_dev_put(tmp);
			continue;
		}
		pr_debug("RIO:%s add %hhu to %s at port %d\n",
			 __func__, tmp->destid, rio_name(rdev),
			 rdev->return_port);
		rc = rio_route_add(rdev, RIO_GLOBAL_TABLE,
				   tmp->destid, rdev->return_port, lock);
		rio_dev_put(tmp);
		if (rc)
			rio_fault++;
	}
	kfree(dptr);
	return rc;
}
