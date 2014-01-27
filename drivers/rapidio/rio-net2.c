/*
 * RapidIO enumeration and discovery support
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Copyright 2009 Integrated Device Technology, Inc.
 * Alex Bounine <alexandre.bounine@idt.com>
 * - Added Port-Write/Error Management initialization and handling
 *
 * Copyright 2009 Sysgo AG
 * Thomas Moll <thomas.moll@sysgo.com>
 * - Added Input- Output- enable functionality, to allow full communication
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
#include <linux/hardirq.h>

#include "rio.h"

DEFINE_SPINLOCK(rio_global_list_lock);

static int rio_mport_phys_table[] = {
	RIO_EFB_PAR_EP_ID,
	RIO_EFB_PAR_EP_REC_ID,
	RIO_EFB_SER_EP_ID,
	RIO_EFB_SER_EP_REC_ID,
	-1,
};

/**
 * rio_mport_active- Tests if master port link is active
 * @port: Master port to test
 *
 * Reads the port error status CSR for the master port to
 * determine if the port has an active link.  Returns
 * %RIO_PORT_N_ERR_STS_PORT_OK if the  master port is active
 * or %0 if it is inactive.
 */
static int rio_mport_active(struct rio_mport *port)
{
	u32 result = 0;
	u32 ext_ftr_ptr;
	int *entry = rio_mport_phys_table;
	int rc;

	do {
		rc = rio_mport_get_feature(port, 1, 0, 0, *entry, &ext_ftr_ptr);
		if (rc)
			return 0;
		if (ext_ftr_ptr)
			break;
	} while (*++entry >= 0);

	if (ext_ftr_ptr)
		rc = rio_local_read_config_32(port,
					      ext_ftr_ptr +
					       RIO_PORT_N_ERR_STS_CSR(port->index),
					      &result);

	return  rc ? 0 : result & RIO_PORT_N_ERR_STS_PORT_OK;
}

static int rio_update_dst_tree(struct rio_mport *mport, u16 parent_destid,
			       int parent_port, int hopcount,
			       u16 destid, u16 comptag, int redundant)
{
	u16 tmp_dst;

	int rc = rio_lookup_next_destid(mport, parent_destid, parent_port,
					hopcount, &tmp_dst);

	if (!rc) {
		pr_debug("RIO:(%s) Not adding new device dest %hx\n",
			 __func__, destid);
		pr_debug("RIO:(%s) found pdest %hx pport %d hop %d dest %hx\n",
			 __func__, parent_destid, parent_port, hopcount,
			 tmp_dst);
		return rc;
	}
	if (redundant)
		return rio_block_destid_route(mport, parent_destid,
					      parent_port, hopcount,
					      destid, comptag);
	else
		return rio_add_destid(mport, parent_destid,
				      parent_port, hopcount,
				      destid, comptag);
}

static int add_new_dev2tree(struct rio_dev *rdev,
			    struct rio_dev *prev,
			    u16 prev_dest,
			    int prev_port)
{
	int rc;
	struct rio_mport *mport = rdev->hport;
	struct rio_dev *tmp;

	rc = rio_pin_destid(mport, prev_dest, prev_port, rdev->hopcount);
	if (unlikely(rc))
		return rc;

	spin_lock(&mport->net.tree_lock);

	rc = radix_tree_insert(&mport->net.dev_tree, rdev->destid, rdev);
	if (unlikely(rc)) {
		spin_unlock(&mport->net.tree_lock);
		return rc;
	}
	tmp = radix_tree_tag_set(&mport->net.dev_tree,
				 rdev->destid,
				 RIO_DEV_NOT_ADDED);
	BUG_ON(tmp != rdev);
	atomic_inc(&mport->net.rio_dev_num);

	if (rio_is_switch(rdev)) {
		tmp = radix_tree_tag_set(&mport->net.dev_tree,
					 rdev->destid,
					 RIO_DEV_IS_SWITCH);
		BUG_ON(tmp != rdev);
	}
	if (rdev->local_domain) {
		tmp = radix_tree_tag_set(&mport->net.dev_tree,
					 rdev->destid,
					 RIO_DEV_DISABLED);
		BUG_ON(tmp != rdev);
	}
	/*
	 * Ensure that rdev is kept in circulation
	 * until enum/disc is done
	 */
	rdev = rio_dev_get(rdev);

	spin_unlock(&mport->net.tree_lock);

	return 0;
}

/**
 * rio_release_dev- Frees a RIO device struct
 * @dev: LDM device associated with a RIO device struct
 *
 * Gets the RIO device struct associated a RIO device struct.
 * The RIO device struct is freed.
 */
static void rio_release_dev(struct device *dev)
{
	struct rio_dev *rdev;

	rdev = to_rio_dev(dev);
	pr_debug("last call for %s\n", rio_name(rdev));

	kfree(rdev);
}

/**
 * rio_release_device - clear enumeration
 *		      complete flag
 * @rdev: rio_device
 *
 * Returns 0 on success or %-EINVAL on failure.
 */
static int rio_release_device(struct rio_dev *rdev)
{
	u32 result;
	int rc;

	/* Un-claim device */
	rc = rio_read_config_32(rdev,
				rdev->phys_efptr + RIO_PORT_GEN_CTL_CSR,
				&result);
	if (rc)
		return rc;

	result &= ~(RIO_PORT_GEN_DISCOVERED | RIO_PORT_GEN_MASTER);
	rc = rio_write_config_32(rdev,
				 rdev->phys_efptr + RIO_PORT_GEN_CTL_CSR,
				 result);
	return rc;
}

static inline int rdev_is_mport(struct rio_dev *rdev)
{
	return (rdev->hport->host_deviceid == rdev->destid);
}

static void __rio_remove_device(struct rio_dev *rdev,
				int dev_access,
				int srio_down)
{
	int rio_access = (srio_down ? 0 : rio_mport_active(rdev->hport));
	struct rio_mport *mport = rdev->hport;
	int device_lock = 0;
	int not_added;

	pr_debug("Removing rio device %s from lists\n", rio_name(rdev));

	rio_tree_write_lock();

	spin_lock(&mport->net.tree_lock);
	/*
	 * Remove device ref. from global list and mport net list
	 */
	not_added = radix_tree_tag_get(&mport->net.dev_tree, rdev->destid,
				       RIO_DEV_NOT_ADDED);
	rdev = radix_tree_delete(&mport->net.dev_tree, rdev->destid);
	if (!rdev) {
		pr_warn("device %s not found in glb tree\n", rio_name(rdev));
		spin_unlock(&mport->net.tree_lock);
		return;
	}
	atomic_dec(&mport->net.rio_dev_num);

	spin_unlock(&mport->net.tree_lock);
	synchronize_rcu();

	rio_tree_write_unlock();

	WARN(rio_unlock_destid(mport, rdev->prev_destid,
			       rdev->prev_port, rdev->hopcount),
	     "RIO: Destid table entry pinned after removing device %s",
	     rio_name(rdev));

	if (!rdev_is_mport(rdev) && rio_access &&
	 dev_access && rdev->local_domain) {
		/* try lock device */
		pr_debug("try lock %s\n", rio_name(rdev));
		if (rdev->use_hw_lock) {
			if (rio_hw_lock_busy_wait(rdev->hport, rdev->destid,
						  rdev->hopcount, 10)) {
				pr_warn("Can not claim device ID lock, HW release not possible");
			} else {
				pr_debug("Got %s device ID lock\n",
					 rio_name(rdev));
				device_lock = 1;
			}
		} else {
			device_lock = 1;
		}
	}
	if (not_added) {
		pr_warn("rio device %s is not added - skip remove!\n",
			rio_name(rdev));
	} else {
		pr_debug("rio device %s is added - remove!\n", rio_name(rdev));
		/*
		 * sysfs cleanup
		 */
		rio_remove_sysfs_dev_files(rdev);
		/*
		 * Remove device from kernel global device list
		 * If a driver has claimed the device its release
		 * function will be called.
		 */
		device_del(&rdev->dev);
	}
	rio_dev_put(rdev);
	if (device_lock) {
		/* Disable device and clear enum flag */
		pr_debug("Release %s HW\n", rio_name(rdev));
		rio_release_device(rdev);
		if (rdev->use_hw_lock)
			rio_hw_unlock(rdev->hport,
				      rdev->destid,
				      rdev->hopcount);
	}
	/*
	 * Cleanup-up routing tables
	 */
	if (!rdev_is_mport(rdev) && rio_access) {
		pr_debug("Remove route to %s\n", rio_name(rdev));
		rio_remove_route_for_destid(rdev->hport, rdev->destid, 1);
	}
	/*
	 * Give up last ref to device - the rio_release_dev
	 * function will do the final cleanup
	 */
	rio_dev_put(rdev);
}

/**
 * rio_remove_devices - remove device node and, if node is switch,
 *		      recursively remove all devices reached through
 *		      node
 *
 * @rdev: RIO device node
 * @rio_access: Cleanup RIO device HW
 *
 */
static void rio_remove_devices(struct rio_dev *rdev, int dev_access,
			       int srio_down)
{
	int i;
	dev_access = 0;
	if (rio_is_switch(rdev)) {
		pr_debug("found switch %s - check ports\n", rio_name(rdev));
		for (i = 0; i < RIO_GET_TOTAL_PORTS(rdev->swpinfo); i++) {
			struct rio_dev *next = lookup_rdev_next(rdev, i);
			int type = (IS_ERR(next) ?
			 RIO_PORT_UNUSED : rio_type_of_next(rdev, next));
			if (IS_ERR(next))
				next = NULL;

			switch (type) {
			case RIO_SWITCH:
				rio_remove_devices(next, dev_access, srio_down);
				break;
			case RIO_END_POINT:
				pr_debug("remove endpoint %s from switch %s\n",
					 rio_name(next), rio_name(rdev));
				__rio_remove_device(next,
						    dev_access,
						    srio_down);
				rio_dev_put(next);
				break;
			case RIO_PORT_UNUSED:
			default:
				rio_dev_put(next);
				break;
			}
		}
	}
	pr_debug("Remove %s %s\n",
		 (rio_is_switch(rdev) ? "switch" : "endpoint"),
		 rio_name(rdev));
	__rio_remove_device(rdev, dev_access, 0);
}

/**
 * rio_switch_init - Sets switch operations for a particular vendor switch
 * @rdev: RIO device
 * @do_enum: Enumeration/Discovery mode flag
 *
 * Searches the RIO switch ops table for known switch types. If the vid
 * and did match a switch table entry, then call switch initialization
 * routine to setup switch-specific routines.
 */
static void rio_switch_init(struct rio_dev *rdev, int do_enum)
{
	struct rio_switch_ops *cur = __start_rio_switch_ops;
	struct rio_switch_ops *end = __end_rio_switch_ops;

	while (cur < end) {
		if ((cur->vid == rdev->vid) && (cur->did == rdev->did)) {
			cur->init_hook(rdev, do_enum);
			break;
		}
		cur++;
	}

	if ((cur >= end) && (rdev->pef & RIO_PEF_STD_RT)) {
		pr_debug("RIO: adding STD routing ops for %s\n",
			 rio_name(rdev));
		rdev->rswitch->add_entry = rio_std_route_add_entry;
		rdev->rswitch->get_entry = rio_std_route_get_entry;
		rdev->rswitch->clr_table = rio_std_route_clr_table;
	}

	if (!rdev->rswitch->add_entry || !rdev->rswitch->get_entry)
		pr_warn("RIO: missing routing ops for %s\n",
			rio_name(rdev));
}

static int init_switch_pw(struct rio_dev *rdev)
{
	if ((rdev->src_ops & RIO_SRC_OPS_PORT_WRITE) &&
	    (rdev->em_efptr)) {
		return rio_write_config_32(rdev,
					   rdev->em_efptr + RIO_EM_PW_TGT_DEVID,
					   (rdev->hport->host_deviceid << 16) |
					   (rdev->hport->sys_size << 15));
	} else {
		return 0;
	}
}

/**
 * rio_add_device- Adds a RIO device to the device model
 * @rdev: RIO device
 *
 * Adds the RIO device to the global device list and adds the RIO
 * device to the RIO device list.  Creates the generic sysfs nodes
 * for an RIO device.
 */
static int rio_add_device(struct rio_dev *rdev)
{
	int err;
	pr_debug("RIO: %s %s\n", __func__, rio_name(rdev));
	err = device_add(&rdev->dev);
	if (err)
		return err;

	rio_create_sysfs_dev_files(rdev);

	return 0;
}

/**
 * rio_enable_rx_tx_port - enable input receiver and output transmitter of
 * given port
 * @port: Master port associated with the RIO network
 * @local: local=1 select local port otherwise a far device is reached
 * @destid: Destination ID of the device to check host bit
 * @hopcount: Number of hops to reach the target
 * @port_num: Port (-number on switch) to enable on a far end device
 *
 * Returns 0 or 1 from on General Control Command and Status Register
 * (EXT_PTR+0x3C)
 */
static int rio_enable_dio(struct rio_mport *port,
			  int local, u16 destid,
			  u8 hopcount, u8 port_num)
{
	int rc = 0;

#ifdef CONFIG_RAPIDIO_ENABLE_RX_TX_PORTS
	u32 regval;
	u32 ext_ftr_ptr;


	rc = rio_mport_get_physefb(port, local, destid, hopcount, &ext_ftr_ptr);
	if (rc)
		goto done;
	if (local)
		rc = rio_local_read_config_32(port, ext_ftr_ptr +
					      RIO_PORT_N_CTL_CSR(0),
					      &regval);
	else
		rc = rio_mport_read_config_32(port, destid, hopcount,
					      ext_ftr_ptr +
						RIO_PORT_N_CTL_CSR(port_num),
					      &regval);
	if (rc)
		goto done;

	if (regval & RIO_PORT_N_CTL_P_TYP_SER)
		/* serial */
		regval = regval | RIO_PORT_N_CTL_EN_RX_SER
			| RIO_PORT_N_CTL_EN_TX_SER;
	else
		/* parallel */
		regval = regval | RIO_PORT_N_CTL_EN_RX_PAR
			| RIO_PORT_N_CTL_EN_TX_PAR;

	if (local)
		rc = rio_local_write_config_32(port, ext_ftr_ptr +
					       RIO_PORT_N_CTL_CSR(0), regval);
	else
		rc = rio_mport_write_config_32(port, destid, hopcount,
					       ext_ftr_ptr +
						RIO_PORT_N_CTL_CSR(port_num),
					       regval);
done:
#endif
	return rc;
}

static int switch_port_is_active(struct rio_dev *rdev, int sport, u8 *active)
{
	u32 result = 0;
	u32 ext_ftr_ptr;
	int rc;

	rc = rio_mport_get_efb(rdev->hport, 0,
			       rdev->destid, rdev->hopcount,
			       0, &ext_ftr_ptr);
	if (rc)
		goto access_err;
	while (ext_ftr_ptr) {
		rc = rio_read_config_32(rdev, ext_ftr_ptr, &result);
		if (rc)
			goto access_err;

		result = RIO_GET_BLOCK_ID(result);
		if ((result == RIO_EFB_SER_EP_FREE_ID) ||
		    (result == RIO_EFB_SER_EP_FREE_ID_V13P) ||
		    (result == RIO_EFB_SER_EP_FREC_ID))
			break;

		rc = rio_mport_get_efb(rdev->hport, 0, rdev->destid,
				       rdev->hopcount, ext_ftr_ptr,
				       &ext_ftr_ptr);
		if (rc)
			goto access_err;
	}

	if (ext_ftr_ptr) {
		rc = rio_get_err_and_status(rdev, sport, &result);
		if (rc)
			goto access_err;
	}
	*active = (result & RIO_PORT_N_ERR_STS_PORT_OK ? 1 : 0);
done:
	return rc;
access_err:
	pr_warn("RIO:(%s) Access err at destid %hu hop %d\n",
		__func__, rdev->destid, rdev->hopcount);
	goto done;
}

static int rio_disc_switch_port(struct rio_job *job, struct rio_dev *rdev,
				int port_num, u16 *destid, int tmo)
{
	int rc;
	u8 route_port;
	u16 tmp_dst;

	rc = rio_job_hw_lock_wait(job, NULL, NULL, rdev->destid,
				  rdev->hopcount, tmo);
	if ((rdev->use_hw_lock) && (rc != 0))
		goto done;

	for (tmp_dst = 0;
	     tmp_dst < RIO_ANY_DESTID(job->mport->sys_size);
	     tmp_dst++) {
		rc = rdev->rswitch->get_entry(rdev->hport, rdev->destid,
					      rdev->hopcount, RIO_GLOBAL_TABLE,
					      tmp_dst, &route_port);
		if (rc)
			goto unlock;

		if (route_port == port_num) {
			if (tmp_dst != RIO_ANY_DESTID(job->mport->sys_size)) {
				*destid = tmp_dst;
				break;
			}
		}
	}
unlock:
	if (rdev->use_hw_lock)
		rio_hw_unlock(job->mport, rdev->destid, rdev->hopcount);
done:
	return rc;
}

static int rio_get_enum_boundary(struct rio_dev *rdev, int port_num, u8 *remote)
{
	u32 regval;
	int rc;

	rc = rio_mport_read_config_32(rdev->hport, rdev->destid,
				      rdev->hopcount,
				      rdev->phys_efptr +
				      RIO_PORT_N_CTL_CSR(port_num),
				      &regval);
	if (rc)
		return rc;

	*remote = (regval & RIO_PORT_N_CTL_ENUM_BOUNDARY ? 1 : 0);
	pr_debug("RIO: %s port %d (%8.8x) links to %s\n",
		 rio_name(rdev), port_num, regval,
		 (*remote ? "enum boundary" : "local domain"));
	return rc;
}

static int rio_is_remote_domain(struct rio_dev *rdev, int port_num, u8 *remote)
{
	int rc = 0;

	if (!rdev->local_domain)
		*remote = 1;
	else
		rc = rio_get_enum_boundary(rdev, port_num, remote);

	return rc;
}
static u8 __get_lock_mode(struct rio_mport *mport,
			  u16 parent_destid, int prev_port,
			  u8 hopcount)
{
	u8 lock_hw = 1, tmp;

	if (rio_dest_is_legacy(mport, parent_destid, prev_port, hopcount)) {
		if ((rio_get_legacy_properties(mport, parent_destid, prev_port,
					       hopcount, &lock_hw,
					       &tmp)))
			return 1;
	}
	return lock_hw;
}

static int rio_enum_start(struct rio_job *job)
{
	int rc = -EFAULT;
	struct rio_dev *from = NULL;
	struct rio_dev *to = job->rdev;

	if (job->rdev) {
		from = rio_get_root_node(job->mport);
		if (IS_ERR(from))
			return 0;
		pr_debug("RIO: lock net from %s to %s\n",
			 rio_name(from), rio_name(to));
		rc = rio_job_hw_lock_wait(job, from, to, 0, 0, 10);
		rio_dev_put(from);
	} else {
		u8 lock_mode = __get_lock_mode(job->mport, -1, -1, -1);

		if (lock_mode) {
			pr_debug("RIO: lock master port\n");
			rc = rio_job_hw_lock_wait(job, NULL, NULL,
						  job->mport->host_deviceid,
						  0, 10);
		} else {
			rc = 0;
		}
	}
	return rc;
}

static int rio_enum_complete(struct rio_mport *mport, u16 destid, u8 hopcount)
{
	u32 phys_efptr = 0;
	u32 result;
	int rc;

	rc = rio_mport_get_physefb(mport, 0, destid, hopcount, &phys_efptr);
	if (rc)
		return rc;

	rc = rio_mport_read_config_32(mport, destid, hopcount,
					   phys_efptr + RIO_PORT_GEN_CTL_CSR,
					   &result);
	if (rc < 0)
		return rc;

	if (result & RIO_PORT_GEN_DISCOVERED) {
		pr_debug("RIO: dest %hu hop %hhu Domain enumeration completed %8.8x\n",
			 destid, hopcount, result);
		return 0;
	} else {
		pr_debug("RIO: dest %hu hop %hhu Domain enumeration NOT completed %8.8x\n",
			 destid, hopcount, result);
		return -ETIME;
	}
}

/**
 * rio_mport_enum_complete- Tests if enumeration of a network is complete
 * @port: Master port to send transaction
 *
 * Tests the Component Tag CSR for non-zero value (enumeration
 * complete flag). Return %1 if enumeration is complete or %0 if
 * enumeration is incomplete.
 */
static int rio_mport_enum_complete(struct rio_mport *port, u16 destid,
				   u8 hopcount)
{
	u32 regval;

	rio_local_read_config_32(port, port->phys_efptr + RIO_PORT_GEN_CTL_CSR,
				 &regval);

	return (regval & RIO_PORT_GEN_DISCOVERED) ? 1 : 0;
}

static int rio_init_net(struct rio_mport *mport, int *comptag)
{
	int net_id = 0;
	int rc = 0;

	spin_lock(&mport->net.tree_lock);

	if (atomic_read(&mport->net.rio_dev_num) != 0)
		goto err_unlock;

	spin_unlock(&mport->net.tree_lock);

	rc = rio_get_next_netid(mport->host_deviceid, &net_id, comptag);
	if (rc) {
		pr_warn("RIO: Failed to get net id\n");
		goto done;
	}
	rc = rio_pin_netid(mport->host_deviceid, net_id);
	if (rc) {
		pr_warn("RIO: Failed to lock net id\n");
		goto done;
	}
	mport->net.id = net_id;

done:
	return rc;

err_unlock:
	spin_unlock(&mport->net.tree_lock);
	pr_warn("RIO: Master port net is not empty\n");
	rc = -EINVAL;
	goto done;
}

/**
 * rio_init_em - Initializes RIO Error Management (for switches)
 * @rdev: RIO device
 *
 * For each enumerated switch, call device-specific error management
 * initialization routine (if supplied by the switch driver).
 */
static void rio_init_em(struct rio_dev *rdev)
{
	if (rio_is_switch(rdev) && (rdev->em_efptr) && (rdev->rswitch->em_init))
		rdev->rswitch->em_init(rdev);
}

static void rio_net_register_devices(struct rio_mport *mport)
{
	int i, num_dev = 0;
	int cleanup_all = 0;
	struct rio_dev *tmp;
	struct rio_dev **dptr = rio_get_tagged_devices(mport,
						       RIO_DEV_NOT_ADDED,
						       &num_dev);

	if (!dptr) {
		pr_info("RIO: No new deviced detected when scanning net\n");
		return;
	}
	if (IS_ERR(dptr)) {
		pr_warn("RIO: Out of memory - detected devices are not added\n");
		return;
	}
	if (rio_update_routes(mport, dptr, num_dev)) {
		cleanup_all = 1;
		pr_warn("RIO: update routes failed\n");
	}
	for (i = 0; i < num_dev; i++) {
		struct rio_dev *rdev = dptr[i];
		int disabled, access = 1;

		if (unlikely(!rdev))
			continue;

		if (cleanup_all)
			goto cleanup;

		spin_lock(&mport->net.tree_lock);
		disabled = radix_tree_tag_get(&mport->net.dev_tree,
					      rdev->destid,
					      RIO_DEV_DISABLED);
		if (disabled) {
			if ((rio_device_enable(rdev))) {
				spin_unlock(&mport->net.tree_lock);
				pr_warn("RIO: Error when enabling device %s\n",
					rio_name(rdev));
				goto cleanup;
			}
			tmp = radix_tree_tag_clear(&mport->net.dev_tree,
						   rdev->destid,
						   RIO_DEV_DISABLED);
			if (tmp != rdev) {
				pr_warn("RIO: Error when clearing tag %i form device %s\n",
					RIO_DEV_DISABLED, rio_name(rdev));
				goto cleanup;
			}
		}
		spin_unlock(&mport->net.tree_lock);
		rio_tree_write_lock();
		if (rio_add_device(rdev)) {
			pr_warn("RIO: Error when adding device %s to kernel model\n",
				rio_name(rdev));
			rio_tree_write_unlock();
			goto cleanup;
		} else {
			spin_lock(&mport->net.tree_lock);
			tmp = radix_tree_tag_clear(&mport->net.dev_tree,
						   rdev->destid,
						   RIO_DEV_NOT_ADDED);
			spin_unlock(&mport->net.tree_lock);
			if (tmp != rdev) {
				pr_warn("RIO: Error when clearing tag %i form device %s\n",
					RIO_DEV_NOT_ADDED, rio_name(rdev));
				rio_tree_write_unlock();
				goto cleanup;
			}
		}
		rio_tree_write_unlock();
		rio_dev_put(rdev);
		continue;
cleanup:
		__rio_remove_device(rdev, access, 0);
		rio_dev_put(rdev);
	}
	kfree(dptr);
}

static int rio_redundant_path(struct rio_mport *mport,
			      struct rio_dev *prev, int prev_port,
			      u16 destid, u8 hopcount, u8 lock_mode,
			      u8 *redundant)
{
	u16 lock;
	u32 comptag;
	int rc = 0;

	BUG_ON(!prev);

	*redundant = rio_dest_is_redundant(mport, prev->destid,
					   prev_port, hopcount);
	if (*redundant)
		goto out;

	if (rdev_is_mport(prev))
		goto out;

	rc = rio_get_host_lock(prev->hport, destid, hopcount, &lock);
	if (rc)
		goto access_err;

	if (lock == prev->hport->host_deviceid || !lock_mode) {
		struct rio_dev *rdev;

		rc = rio_read_comptag(prev->hport, destid, hopcount, &comptag);
		if (rc)
			goto access_err;

		rdev = rio_get_comptag(mport, (u32)comptag);

		if (rdev) {
			rio_update_dst_tree(mport, prev->destid,
					    prev_port, hopcount,
					    rdev->destid,
					    rdev->comp_tag, 1);

			pr_debug("RIO: redundant path to %s\n",
				 rio_name(rdev));
			rio_dev_put(rdev);
			*redundant = 1;
		} else {
			if (lock_mode)
				goto lock_fault;
		}
	}
out:
	return rc;

access_err:
	pr_warn("RIO: Access fault at port %d destid %hu hopcount %d\n",
		prev_port, destid, hopcount);
	goto out;

lock_fault:
	RAPIDIO_REDUNDANT_PATH_LOCK_FAULT();
	pr_warn("RIO: %hu Unexpectedly owns lock on destid %hu hopcount %d\n",
		lock, destid, hopcount);
	rc = -EFAULT;
	goto out;
}

int rio_device_enable(struct rio_dev *rdev)
{
	u32 result;
	int rc = 0;

	BUG_ON(!rdev->local_domain);

	pr_debug("RIO: Enable device %s\n", rio_name(rdev));

	/* Mark device as discovered and enable master */
	rc = rio_read_config_32(rdev,
				rdev->phys_efptr + RIO_PORT_GEN_CTL_CSR,
				&result);
	if (rc != 0)
		goto abort;

	result |= RIO_PORT_GEN_DISCOVERED | RIO_PORT_GEN_MASTER;
	rc = rio_write_config_32(rdev,
				 rdev->phys_efptr + RIO_PORT_GEN_CTL_CSR,
				 result);
	if (rc != 0)
		goto abort;

	if (rdev->use_hw_lock)
		rc = rio_hw_unlock(rdev->hport, rdev->destid, rdev->hopcount);
done:
	return rc;
abort:
	pr_warn("RIO:(%s) RIO_PORT_GEN_CTL_CSR access fault\n",
		__func__);
	goto done;
}

int rio_device_disable(struct rio_dev *rdev)
{
	/* Mark device as undiscovered */
	return rio_write_config_32(rdev,
				   rdev->phys_efptr + RIO_PORT_GEN_CTL_CSR,
				   0);
}

static int rio_enum_unlock(struct rio_mport *mport, struct rio_dev *rdev,
			   u16 destid, u8 hopcount)
{
	u16 dest = (rdev ? rdev->destid : destid);
	u8 hop = (rdev ? rdev->hopcount : hopcount);
	int rc;

	rc = rio_mport_chk_dev_access(mport, dest, hop);
	if (rc)
		return rc;
	return rio_hw_unlock(mport, dest, hop);
}

static void do_port_cleanup(struct rio_dev *rdev, int port_num)
{
	struct rio_dev *next = lookup_rdev_next(rdev, port_num);

	if (!IS_ERR(next)) {
		rio_remove_devices(next, 0, 0);
		rio_dev_put(next);
	}
	return;
}

static int rio_add_enum_device(struct rio_dev *prev, int prev_port,
			       struct rio_dev *rdev, u16 tmp_destid)
{
	struct rio_mport *mport = rdev->hport;
	int rc = 0;

	BUG_ON(!prev);

	if ((lookup_rdev(mport, rdev->destid)) == rdev)
		return rc;

	rc = rio_add_route_for_destid(mport, prev->destid,
				      prev_port, rdev->destid, 0);
	if (rc)
		goto abort;

	if (rio_is_switch(rdev)) {
		rio_init_em(rdev);
		rc = init_switch_pw(rdev);
		if (rc)
			goto cleanup;

		rc = rio_init_lut(rdev, 0);
		if (rc)
			goto cleanup;

	} else {
		/* Enable Input Output Port (transmitter reviever) */
		rio_enable_dio(mport, 0, rdev->destid, rdev->hopcount, 0);
	}

	rc = add_new_dev2tree(rdev, prev, prev->destid, prev_port);
	if (rc)
		goto cleanup;

done:
	return rc;
cleanup:
	rio_remove_route_for_destid(mport, rdev->destid, 0);

abort:
	pr_warn("RIO: bail and release lock on destid %hu at hp %d\n",
		tmp_destid, rdev->hopcount);
	if (rdev->use_hw_lock)
		rio_enum_unlock(mport, NULL, tmp_destid, rdev->hopcount);
	goto done;
}

static struct rio_dev *rio_alloc_new_device(struct rio_mport *mport,
					    u16 destid, u8 hopcount,
					    u16 parent_destid, int prev_port)
{
	struct rio_dev *rdev = NULL;
	struct rio_switch *rswitch = NULL;
	int rc;
	size_t size;
	u32 result, swpinfo = 0;

	size = sizeof(struct rio_dev);

	rc = rio_mport_read_config_32(mport, destid, hopcount,
				      RIO_PEF_CAR, &result);
	if (rc)
		goto access_err;

	if (result & (RIO_PEF_SWITCH | RIO_PEF_MULTIPORT)) {
		rc = rio_mport_read_config_32(mport, destid, hopcount,
					      RIO_SWP_INFO_CAR, &swpinfo);
		if (rc)
			goto access_err;

		if (result & RIO_PEF_SWITCH)
			size += sizeof(*rswitch);
	}
	rdev = kzalloc(size, GFP_KERNEL);
	if (!rdev)
		return ERR_PTR(-ENOMEM);

	rdev->hport = mport;
	rdev->pef = result;
	rdev->swpinfo = swpinfo;
	rdev->prev_port = prev_port;
	rdev->prev_destid = parent_destid;

	rc = rio_mport_read_config_32(mport, destid, hopcount,
				      RIO_DEV_ID_CAR, &result);
	if (rc)
		goto access_err;

	rdev->did = result >> 16;
	rdev->vid = result & 0xffff;
	rc = rio_mport_read_config_32(mport, destid, hopcount,
				      RIO_DEV_INFO_CAR, &rdev->device_rev);
	if (rc)
		goto access_err;

	rc = rio_mport_read_config_32(mport, destid, hopcount,
				      RIO_ASM_ID_CAR, &result);
	if (rc)
		goto access_err;

	rdev->asm_did = result >> 16;
	rdev->asm_vid = result & 0xffff;
	rc = rio_mport_read_config_32(mport, destid, hopcount,
				      RIO_ASM_INFO_CAR, &result);
	if (rc)
		goto access_err;

	rdev->asm_rev = result >> 16;
	if (rdev->pef & RIO_PEF_EXT_FEATURES) {
		rdev->efptr = result & 0xffff;
		rc = rio_mport_get_physefb(mport, 0, destid,
					   hopcount, &rdev->phys_efptr);
		if (rc)
			goto access_err;

		rc = rio_mport_get_feature(mport, 0, destid,
					   hopcount, RIO_EFB_ERR_MGMNT,
					   &rdev->em_efptr);
		if (rc)
			goto access_err;
	}

	rc = rio_mport_read_config_32(mport, destid, hopcount,
				      RIO_SRC_OPS_CAR, &rdev->src_ops);
	if (rc)
		goto access_err;

	rc = rio_mport_read_config_32(mport, destid, hopcount,
				      RIO_DST_OPS_CAR, &rdev->dst_ops);
	if (rc)
		goto access_err;

	return rdev;

access_err:
	pr_warn("RIO: RIO:(%s) destid %hx hopcount %d - ACCESS ERROR\n",
		__func__, destid, hopcount);

	if (rdev && !IS_ERR(rdev))
		kfree(rdev);

	return ERR_PTR(rc);
}

static struct rio_dev *rio_alloc_mport_device(struct rio_mport *mport)
{
	struct rio_dev *rdev = NULL;
	int rc;
	size_t size;
	u32 result;

	size = sizeof(struct rio_dev);

	rc = rio_local_read_config_32(mport, RIO_PEF_CAR, &result);
	if (rc)
		goto access_err;

	rdev = kzalloc(size, GFP_KERNEL);
	if (!rdev)
		return ERR_PTR(-ENOMEM);

	rdev->hport = mport;
	rdev->pef = result;
	rdev->swpinfo = 0;
	rdev->prev_destid = RIO_INVALID_DESTID;
	rdev->prev_port = -1;

	rc = rio_local_read_config_32(mport, RIO_DEV_ID_CAR, &result);
	if (rc)
		goto access_err;

	rdev->did = result >> 16;
	rdev->vid = result & 0xffff;
	rc = rio_local_read_config_32(mport, RIO_DEV_INFO_CAR,
				      &rdev->device_rev);
	if (rc)
		goto access_err;

	rc = rio_local_read_config_32(mport, RIO_ASM_ID_CAR, &result);
	if (rc)
		goto access_err;

	rdev->asm_did = result >> 16;
	rdev->asm_vid = result & 0xffff;
	rc = rio_local_read_config_32(mport, RIO_ASM_INFO_CAR, &result);
	if (rc)
		goto access_err;

	rdev->asm_rev = result >> 16;
	if (rdev->pef & RIO_PEF_EXT_FEATURES) {
		rdev->efptr = result & 0xffff;
		rc = rio_mport_get_physefb(mport, 1, mport->host_deviceid,
					   0, &rdev->phys_efptr);
		if (rc)
			goto access_err;

		rc = rio_mport_get_feature(mport, 1, mport->host_deviceid,
					   0 , RIO_EFB_ERR_MGMNT,
					   &rdev->em_efptr);
		if (rc)
			goto access_err;
	}

	rc = rio_local_read_config_32(mport, RIO_SRC_OPS_CAR, &rdev->src_ops);
	if (rc)
		goto access_err;

	rc = rio_local_read_config_32(mport, RIO_DST_OPS_CAR, &rdev->dst_ops);
	if (rc)
		goto access_err;

	rc = rio_local_read_config_32(mport, RIO_COMPONENT_TAG_CSR,
				      &rdev->comp_tag);
	if (rc)
		goto access_err;

	return rdev;

access_err:
	pr_warn("RIO: RIO:(%s) destid %hx - ACCESS ERROR\n",
		__func__, mport->host_deviceid);
	kfree(rdev);

	return ERR_PTR(rc);
}

static void rio_dev_init(struct rio_dev *rdev)
{
	rdev->dev.bus = &rio_bus_type;
	rdev->dev.parent = &rio_bus;

	device_initialize(&rdev->dev);
	rdev->dev.release = rio_release_dev;
	rio_dev_get(rdev);

	rdev->dma_mask = DMA_BIT_MASK(32);
	rdev->dev.dma_mask = &rdev->dma_mask;
	rdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	if ((rdev->pef & RIO_PEF_INB_DOORBELL) &&
	    (rdev->dst_ops & RIO_DST_OPS_DOORBELL))
		rio_init_dbell_res(&rdev->riores[RIO_DOORBELL_RESOURCE],
				   0, 0xffff);
}

static struct rio_dev *rio_setup_disc_mport(struct rio_mport *mport,
					    int do_enum)
{
	struct rio_dev *rdev = rio_alloc_mport_device(mport);
	u8 lock_hw = 1, dummy;
	int rc;

	if (IS_ERR(rdev))
		return rdev;

	if (rio_dest_is_legacy(mport, -1, -1, -1)) {
		rc = rio_get_legacy_properties(mport, -1, -1, -1,
					       &lock_hw, &dummy);
		if (rc)
			goto cleanup;
	} else {
		pr_debug("RIO: Failed to lookup destid info for mport, using default flags\n");
		rc = rio_update_dst_tree(mport, -1, -1, -1,
					 mport->host_deviceid,
					 mport->host_deviceid, 0);
		if (rc)
			goto cleanup;
	}
	rdev->local_domain = do_enum;
	rdev->destid = mport->host_deviceid;
	rdev->hopcount = 0xff;
	rdev->use_hw_lock = lock_hw;

	if (rio_eval_destid(rdev)) {
		rc = -EINVAL;
		goto cleanup;
	}
	dev_set_name(&rdev->dev, "%02x:m:%04x", rdev->hport->net.id,
		     rdev->destid);
	rio_dev_init(rdev);
	return rdev;

cleanup:
	kfree(rdev);
	return ERR_PTR(rc);
}

static struct rio_dev *rio_setup_enum_device(struct rio_mport *mport,
					     u16 prev_destid, int prev_port,
					     u16 destid, u8 hopcount)
{
	struct rio_dev *rdev;
	int device_comptag;
	u16 device_destid;
	u8 lock_hw = 1, lut_update = 1;
	int rc;

	rc = rio_get_next_destid(mport, prev_destid,
				 prev_port, hopcount,
				 &device_destid, &device_comptag);
	if (rc) {
		pr_warn("RIO: Failed to get destid\n");
		return ERR_PTR(rc);
	}

	rdev = lookup_rdev(mport, device_destid);
	if (rdev)
		/* dev is already known and added - return ptr */
		return rdev;

	rdev = rio_alloc_new_device(mport, destid, hopcount,
				    prev_destid, prev_port);
	if (IS_ERR(rdev))
		return rdev;

	rdev->local_domain = 1;
	if (rio_dest_is_legacy(mport, prev_destid, prev_port, hopcount)) {
		rc = rio_get_legacy_properties(mport, prev_destid, prev_port,
					       hopcount, &lock_hw,
					       &lut_update);
		if (rc)
			goto cleanup;
	}
	rdev->use_hw_lock = lock_hw;

	rio_fixup_dev(rio_fixup_early, rdev, destid, hopcount);
	/* Assign component tag to device */
	if (device_comptag >= 0x10000) {
		pr_warn("RIO: Component Tag Counter Overflow\n");
		rc = -EFAULT;
		goto cleanup;
	}
	rio_mport_write_config_32(mport, destid, hopcount,
				  RIO_COMPONENT_TAG_CSR, device_comptag);
	rdev->comp_tag = device_comptag;
	rdev->hopcount = hopcount;
	rio_assign_destid(rdev, mport, destid, hopcount, &device_destid);
	if (rio_eval_destid(rdev)) {
		rc = -EINVAL;
		goto cleanup;
	}
	if (rio_is_switch(rdev)) {
		rdev->return_port = RIO_GET_PORT_NUM(rdev->swpinfo);

		if (rio_dest_is_one_way(mport, prev_destid,
					prev_port, hopcount)) {
			rc = rio_get_return_port(mport, prev_destid, prev_port,
						 hopcount, &rdev->return_port);
			if (rc)
				goto cleanup;
		}

		rdev->rswitch->switchid = rdev->comp_tag & RIO_CTAG_UDEVID;
		rdev->rswitch->update_lut = lut_update;

		dev_set_name(&rdev->dev, "%02x:s:%04x", rdev->hport->net.id,
			     rdev->rswitch->switchid);
		rio_switch_init(rdev, 1);
		if (rdev->rswitch->clr_table)
			rdev->rswitch->clr_table(mport, destid, hopcount,
						 RIO_GLOBAL_TABLE);
	} else {
		dev_set_name(&rdev->dev, "%02x:e:%04x", rdev->hport->net.id,
			     rdev->destid);
	}
	rio_dev_init(rdev);
	return rdev;

cleanup:
	kfree(rdev);

	return ERR_PTR(rc);
}

static int rio_enum(struct rio_job *job,
		    struct rio_dev *prev,
		    u16 prev_destid, int prev_port,
		    u8 hopcount)
{
	int rc = 0;
	struct rio_mport *mport = job->mport;
	struct rio_dev *rdev = NULL;
	u8 lock_mode = __get_lock_mode(mport, prev_destid, prev_port, hopcount);
	u8 redundant;

	pr_debug("RIO: rio_enum prev_dest %hx, prev_port %d hop %hhu\n",
		 prev_destid, prev_port, hopcount);

	rc = rio_redundant_path(mport, prev, prev_port,
				RIO_ANY_DESTID(job->mport->sys_size),
				hopcount, lock_mode, &redundant);
	if (rc || redundant)
		goto done;

	if (lock_mode) {
		rc = rio_job_hw_lock_wait(job, NULL, NULL,
					  RIO_ANY_DESTID(job->mport->sys_size),
					  hopcount, 1);
		if (unlikely(rc))
			goto done;
	}

	rdev = rio_setup_enum_device(mport, prev_destid, prev_port,
				     RIO_ANY_DESTID(job->mport->sys_size),
				     hopcount);

	if (unlikely(IS_ERR(rdev))) {
		rc = PTR_ERR(rdev);
		goto unlock;
	}
	rc = rio_add_enum_device(prev, prev_port, rdev,
				 RIO_ANY_DESTID(job->mport->sys_size));
	if (unlikely(rc))
		goto done;

	if (rio_is_switch(rdev)) {
		int port_num;
		int sw_inport;
		int num_ports;

		sw_inport = RIO_GET_PORT_NUM(rdev->swpinfo);
		num_ports = RIO_GET_TOTAL_PORTS(rdev->swpinfo);

		pr_debug("RIO: found %s (vid %4.4x did %4.4x) with %d ports\n",
			 rio_name(rdev), rdev->vid, rdev->did, num_ports);

		for (port_num = 0; port_num < num_ports; port_num++) {
			u8 remote = 0, active = 0;
			if (lock_mode)
				rc = rio_enable_dio(rdev->hport, 0,
						    rdev->destid,
						    rdev->hopcount,
						    port_num);
				if (rc < 0)
					goto unlock;
			if (sw_inport == port_num) {
				if (rdev_is_mport(prev)) {
					rio_update_dst_tree(job->mport,
							    rdev->destid,
							    port_num,
							    hopcount + 1,
							    prev->destid,
							    prev->comp_tag,
							    0);
				}
				continue;
			}
			rc = switch_port_is_active(rdev, port_num, &active);
			if (rc < 0)
				goto unlock;
			if (!active) {
				do_port_cleanup(rdev, port_num);
				continue;
			}

			rc = rio_get_enum_boundary(rdev, port_num, &remote);
			if (rc < 0)
				goto unlock;

			if (remote)
				continue;

			rc = rio_route_add(rdev, RIO_GLOBAL_TABLE,
					   RIO_ANY_DESTID(mport->sys_size),
					   port_num, 0);
			if (rc < 0)
				goto unlock;

			rc = rio_enum(job, rdev, rdev->destid, port_num,
				      hopcount + 1);
			if (rc)
				return rc;
		}
	} else {
		pr_debug("RIO: found ep %s (vid %4.4x did %4.4x)\n",
			 rio_name(rdev), rdev->vid, rdev->did);
	}

	/* final fixup pass for device */
	rio_fixup_dev(rio_fixup_enable, rdev, rdev->destid, rdev->hopcount);
done:
	if (rdev && !IS_ERR(rdev))
		rio_dev_put(rdev);
	if (rc != RIO_JOB_STATE_ABORT)
		return 0;
	return rc;
unlock:
	if (IS_ERR(rdev))
		rdev = NULL;
	if (lock_mode)
		rio_enum_unlock(mport, rdev,
				RIO_ANY_DESTID(job->mport->sys_size),
				hopcount);
	goto done;
}

static int __rio_mport_net_empty(struct rio_mport *mport)
{
	int empty = 0;

	spin_lock(&mport->net.tree_lock);
	if (atomic_read(&mport->net.rio_dev_num) != 0) {
		pr_debug("RIO: Master port Net devices - list not empty\n");
	} else {
		pr_debug("RIO: Master port Net devices - list empty\n");
		empty = 1;
	}
	spin_unlock(&mport->net.tree_lock);
	return empty;
}

static int rio_dev_local(struct rio_job *job, u8 *remote)
{
	int rc = 0;

	if (job->rdev) {
		rc = rio_is_remote_domain(job->rdev, job->port, remote);
		if (rc < 0)
			return rc; /* access fault */

		pr_debug("RIO: Insertion at port %d in %s domain\n",
			 job->port, (*remote ? "remote" : "local"));
	} else {
		*remote = (job->mport->enum_host ? 0 : 1);
		pr_debug("RIO: %s Master port - %s\n",
			 (*remote ? "Discover" : "Enum"),
			 (__rio_mport_net_empty(job->mport) ?
			  "net empty" : "net exists"));
	}

	return rc;
}

static struct rio_dev *rio_enum_master_port(struct rio_job *job)
{
	struct rio_dev *rdev = NULL;
	int comptag = 1;
	int rc;

	printk(KERN_INFO "RIO: enumerate master port %d, %s\n", job->mport->id,
	       job->mport->name);

	if (!rio_mport_active(job->mport)) {
		pr_warn("RIO: master port %d link inactive\n",
			job->mport->id);
		return ERR_PTR(-ENODEV);
	}
	/* If master port has an active link, allocate net and enum peers */
	rc = rio_init_net(job->mport, &comptag);
	if (rc < 0) {
		pr_warn("RIO: failed to init new net\n");
		return ERR_PTR(rc);
	}
	/* Set master port destid and init destid ctr */
	pr_debug("RIO:(%s) set master destid %hu\n",
		 __func__, job->mport->host_deviceid);
	rio_set_master_destid(job->mport, job->mport->host_deviceid);
	/* Set component tag for host */
	pr_debug("RIO:(%s) set master comptag %d\n", __func__,
		 job->mport->host_deviceid);
	rio_local_write_config_32(job->mport, RIO_COMPONENT_TAG_CSR,
				  job->mport->host_deviceid);
	/* Enable Input Output Port (transmitter reviever) */
	pr_debug("RIO:(%s) enable master DIO\n", __func__);
	rio_enable_dio(job->mport, 1, 0, 0, 0);
	rdev = rio_setup_disc_mport(job->mport, 1);

	return rdev;
}

static int rio_add_device_local(struct rio_job *job)
{
	int rc;
	struct rio_dev *rdev = job->rdev;
	struct rio_mport *mport = job->mport;
	int port_num;
	u8 hopcount;

	pr_debug("RIO: Handle job in local domain\n");
	rc = rio_enum_start(job);
	if (rc)
		return rc;

	if (!rdev) {
		rdev = rio_enum_master_port(job);

		if (unlikely(IS_ERR(rdev))) {
			rc = PTR_ERR(rdev);
			rdev = NULL;
			goto mport_unlock;
		}
		rc = add_new_dev2tree(rdev, NULL, RIO_INVALID_DESTID, -1);
		if (rc) {
			pr_warn("RIO: add master port to tree failed\n");
			goto mport_unlock;
		}
		if (job->mport->ops->pwenable)
			job->mport->ops->pwenable(job->mport, 1);

		port_num = -1;
		hopcount = 0;
	} else {
		u8 port_active;

		port_num = job->port;
		hopcount = rdev->hopcount + 1;

		rc = switch_port_is_active(rdev, port_num, &port_active);
		if (rc < 0)
			goto unlock;

		if (!port_active)
			goto unlock;

		rc = rio_add_route_for_destid(mport, rdev->destid, port_num,
					      RIO_ANY_DESTID(mport->sys_size),
					      0);
		if (rc < 0)
			goto unlock;

	}

	rc = rio_enum(job, rdev, rdev->destid, port_num, hopcount);

done:
	if (rdev && rdev_is_mport(rdev))
		rio_dev_put(rdev);
unlock:
	rio_job_hw_unlock_devices(job);
	return rc;
mport_unlock:
	rio_hw_unlock(job->mport, job->mport->host_deviceid, 0);
	goto done;
}

static int rio_add_disc_device(struct rio_dev *prev, int prev_port,
			       struct rio_dev *rdev, u16 tmp_destid)
{
	struct rio_mport *mport = rdev->hport;
	int rc = 0;

	BUG_ON(!prev);

	if ((lookup_rdev(mport, rdev->destid)) == rdev)
		return rc;

	if (rdev->destid != tmp_destid) {
		rc = rio_add_route_for_destid(mport, prev->destid, prev_port,
					      rdev->destid, 1);
		if (rc < 0)
			goto cleanup;
	}

	if (rio_is_switch(rdev)) {
		rc = rio_init_lut(rdev, 1);
		if (rc)
			goto cleanup;
	}

	rc = add_new_dev2tree(rdev, prev, prev->destid, prev_port);
	if (rc)
		goto cleanup;
done:
	return rc;
cleanup:
	rio_remove_route_for_destid(mport, rdev->destid, 0);
	pr_warn("RIO: bail on destid %hu at hp %d\n",
		tmp_destid, rdev->hopcount);
	goto done;
}

static struct rio_dev *rio_setup_disc_device(struct rio_mport *mport,
					     u16 parent_destid, int prev_port,
					     u16 destid, u8 hopcount)
{
	struct rio_dev *rdev = rio_alloc_new_device(mport, destid, hopcount,
						    parent_destid, prev_port);
	u8 lock_hw = 1, lut_update = 1;
	int rc;

	if (IS_ERR(rdev))
		return rdev;

	rdev->local_domain = 0;
	rc = rio_read_comptag(mport, destid, hopcount, &rdev->comp_tag);
	if (rc)
		goto access_err;

	if (rio_has_destid(rdev->src_ops, rdev->dst_ops)) {
		rc = rio_get_destid(mport, destid, hopcount, &rdev->destid);
		if (rc)
			goto access_err;
	} else {
		rdev->destid = rdev->comp_tag;
	}
	rdev->hopcount = hopcount;
	if (rio_eval_destid(rdev)) {
		rc = -EINVAL;
		goto cleanup;
	}
	rc = rio_update_dst_tree(mport, parent_destid, prev_port,
				 rdev->hopcount, rdev->destid,
				 rdev->comp_tag, 0);
	if (rc)
		goto cleanup;

	if (rio_dest_is_legacy(mport, parent_destid,
			       prev_port, rdev->hopcount)) {
		rc = rio_get_legacy_properties(mport, parent_destid, prev_port,
					       rdev->hopcount, &lock_hw,
					       &lut_update);
		if (rc)
			goto cleanup;
	}
	rdev->use_hw_lock = lock_hw;

	if (rio_is_switch(rdev)) {
		rdev->return_port = RIO_GET_PORT_NUM(rdev->swpinfo);

		if (rio_dest_is_one_way(mport, parent_destid,
					prev_port, hopcount)) {
			rc = rio_get_return_port(mport, parent_destid,
						 prev_port, hopcount,
						 &rdev->return_port);
			if (rc)
				goto cleanup;
		}

		rdev->rswitch->switchid = rdev->comp_tag & RIO_CTAG_UDEVID;
		rdev->rswitch->update_lut = lut_update;

		dev_set_name(&rdev->dev, "%02x:s:%04x", rdev->hport->net.id,
			     rdev->rswitch->switchid);

		rio_switch_init(rdev, 0);
	} else {
		dev_set_name(&rdev->dev, "%02x:e:%04x", rdev->hport->net.id,
			     rdev->destid);
	}
	rio_dev_init(rdev);
	return rdev;
access_err:
	pr_warn("RIO: RIO:(%s) destid %hx hopcount %d - ACCESS ERROR\n",
		__func__, destid, hopcount);
cleanup:
	kfree(rdev);
	return ERR_PTR(rc);
}

static int rio_disc(struct rio_job *job, struct rio_dev *prev,
		    int prev_port, u16 destid, u8 hopcount,
		    int tmo)
{
	int rc = 0;
	struct rio_mport *mport = job->mport;
	struct rio_dev *rdev = NULL;
	u8 lock_mode = __get_lock_mode(mport, prev->destid,
				       prev_port, hopcount);

	pr_debug("RIO: do_disc prev %s, prev port %d, destid 0x%x, hop %hhu\n",
		 rio_name(prev), prev_port, destid, hopcount);

	if (job->flags & RIO_JOB_FLAG_STATIC) {
		u16 tmp_dst;
		/* destid node required if discover static */
		if (rio_lookup_next_destid(mport, prev->destid, prev_port,
					   hopcount, &tmp_dst)) {
			pr_debug("RIO: No static destid found for device\n");
			goto done;
		}
	}
	rc = rio_job_hw_lock_wait_cond(job, destid, hopcount,
				       tmo, lock_mode,
				       rio_enum_complete);
	if (unlikely(rc)) {
		u32 tmp = 0;

		rio_read_comptag(prev->hport, destid,
				 hopcount, &tmp);
		pr_warn("RIO: lock fail comptag %x\n", tmp);
		goto done;
	}
	tmo = 1;
	rdev = rio_setup_disc_device(mport, prev->destid,
				     prev_port, destid, hopcount);
	if (lock_mode)
		rc = rio_hw_unlock(job->mport, destid, hopcount);

	if (unlikely(IS_ERR(rdev)))
		rc = PTR_ERR(rdev);

	if (rc)
		goto done;

	rc = rio_add_disc_device(prev, prev_port, rdev, destid);
	if (unlikely(rc))
		goto done;

	if (rio_is_switch(rdev)) {
		int port_num;
		int sw_inport;
		int num_ports;

		sw_inport = RIO_GET_PORT_NUM(rdev->swpinfo);
		num_ports = RIO_GET_TOTAL_PORTS(rdev->swpinfo);

		pr_debug("RIO: disc %s (vid %4.4x did %4.4x) with %d ports\n",
			 rio_name(rdev), rdev->vid, rdev->did, num_ports);

		for (port_num = 0;
		     port_num < num_ports;
		     port_num++) {
			u8 active = 0;
			u8 remote = 0;
			u16 ndestid = RIO_ANY_DESTID(job->mport->sys_size);
			u16 result = 0;

			if (sw_inport == port_num) {
				if (rdev_is_mport(prev)) {
					rio_update_dst_tree(job->mport,
							    rdev->destid,
							    port_num,
							    hopcount + 1,
							    prev->destid,
							    prev->comp_tag,
							    0);
				}
				continue;
			}
			rc = switch_port_is_active(rdev, port_num, &active);
			if (rc < 0)
				goto done; /* switch fault during disc ?*/

			if (!active) {
				do_port_cleanup(rdev, port_num);
				continue;
			}
			rc = rio_get_enum_boundary(rdev, port_num, &remote);
			if (rc < 0)
				goto done;

			if (remote)
				continue;

			rc = rio_disc_switch_port(job, rdev, port_num,
						  &ndestid, tmo);
			if (rc < 0)
				goto done; /* switch fault during disc ?*/

			result = RIO_ANY_DESTID(job->mport->sys_size);
			if (ndestid == result) {
				pr_debug("RIO: No destid setup at active port %d\n",
					 port_num);
				continue;
			}
			rc = rio_add_route_for_destid(mport, rdev->destid,
						      port_num, ndestid, 1);
			if (rc != 0)
				goto done; /* switch fault during disc ?*/

			rc = rio_disc(job, rdev, port_num,
				      ndestid, hopcount + 1, tmo);
			if (rc)
				return rc;
		}
	} else
		pr_debug("RIO: disc ep %s (vid %4.4x did %4.4x)\n",
			 rio_name(rdev), rdev->vid, rdev->did);
done:
	if (rdev && !IS_ERR(rdev))
		rio_dev_put(rdev);

	if (rc != RIO_JOB_STATE_ABORT)
		return 0;
	pr_debug("RIO: do_disc done rc %d\n", rc);
	return rc;
}

static struct rio_dev *rio_disc_master_port(struct rio_job *job)
{
	struct rio_dev *rdev = NULL;
	int comptag = 1;
	int rc;

	pr_debug("RIO: discover master port %d, %s\n", job->mport->id,
		job->mport->name);

	if (!rio_mport_active(job->mport))
		goto link_fault;

	if ((rio_job_hw_wait_cond(job, job->mport->host_deviceid, 0,
				  CONFIG_RAPIDIO_DISC_TIMEOUT,
				  rio_mport_enum_complete)) <= 0)
		goto enum_to;

	rio_local_read_config_32(job->mport, RIO_DID_CSR,
				 &job->mport->host_deviceid);

	job->mport->host_deviceid = RIO_GET_DID(job->mport->sys_size,
						job->mport->host_deviceid);

	if (rio_job_hw_lock_wait(job, NULL, NULL, job->mport->host_deviceid,
				 0, CONFIG_RAPIDIO_DISC_TIMEOUT))
		goto enum_to;

	rc = rio_init_net(job->mport, &comptag);
	if (rc < 0)
		goto err_net;

	rdev = rio_setup_disc_mport(job->mport, 0);
unlock:
	rio_hw_unlock(job->mport, job->mport->host_deviceid, 0);
done:
	return rdev;

link_fault:
	pr_warn("RIO: master port %d link inactive\n",
		job->mport->id);
	rdev = ERR_PTR(-ENODEV);
	goto done;
enum_to:
	pr_warn("RIO: timeout waiting for enumeration complete\n");
	job->mport->host_deviceid = RIO_ANY_ID;
	rdev = ERR_PTR(-ETIME);
	goto done;
err_net:
	pr_warn("RIO: failed to init new net\n");
	job->mport->host_deviceid = RIO_ANY_ID;
	rdev = ERR_PTR(rc);
	goto unlock;
}

static int rio_add_device_remote(struct rio_job *job)
{
	int rc = RIO_JOB_STATE_ABORT;
	struct rio_dev *rdev = job->rdev;
	struct rio_mport *mport = job->mport;
	int port_num;
	u8 hopcount;
	u16 destid = RIO_ANY_DESTID(job->mport->sys_size);
	int tmo;

	pr_debug("RIO: Insertion in remote domains\n");
	if (!rdev) {
		rdev = rio_disc_master_port(job);

		if (unlikely(IS_ERR(rdev)))
			return PTR_ERR(rdev);

		rc = add_new_dev2tree(rdev, NULL, RIO_INVALID_DESTID, -1);
		if (rc != 0)
			goto done;

		if (job->mport->ops->pwenable)
			job->mport->ops->pwenable(job->mport, 1);

		port_num = -1;
		hopcount = 0;
		tmo = 1;
	} else {
		u8 port_active;
		port_num = job->port;
		hopcount = rdev->hopcount + 1;
		tmo = CONFIG_RAPIDIO_DISC_TIMEOUT;

		rc = switch_port_is_active(rdev, port_num, &port_active);
		if (rc < 0)
			goto done;

		if (!port_active)
			goto done;
		if (rdev->local_domain) {
			rc = rio_add_route_for_destid(mport,
						      rdev->destid,
						      port_num,
						      destid, 1);
			if (rc != 0)
				goto done;
		} else {
			rc = rio_disc_switch_port(job, rdev,
						  port_num,
						  &destid, tmo);
			if (rc < 0)
				goto done;
			if (destid == RIO_ANY_DESTID(job->mport->sys_size))
				goto done;
			rc = rio_add_route_for_destid(mport,
						      rdev->destid,
						      port_num,
						      destid, 1);
			if (rc != 0)
				goto done;
		}
	}
	rc = rio_disc(job, rdev, port_num, destid, hopcount, tmo);
done:
	if (rdev_is_mport(rdev))
		rio_dev_put(rdev);
	return rc;
}

static int rio_remove(struct rio_job *job, struct rio_dev *rdev)
{
	int dev_access = (job->rdev ? 0 : 1);
	int rc = 0;

	rio_remove_devices(rdev, dev_access, job->srio_down);

	return rc;
}

struct rio_dev *rio_remove_device_root(struct rio_job *job)
{
	struct rio_mport *mport = job->mport;
	struct rio_dev *rdev = NULL;

	BUG_ON(!mport);

	if (!job->rdev)
		rdev = rio_get_root_node(job->mport);
	else
		rdev = lookup_rdev_next(job->rdev, job->port);

	if (!rdev || IS_ERR(rdev))
		return ERR_PTR(-ENODEV);

	return rdev;
}

static int rio_job_add_device(struct rio_job *job)
{
	int rc;
	u8 remote = 0;

	rc = rio_dev_local(job, &remote);
	if (rc < 0) {
		pr_warn("RIO: Can not handle job request\n");
		return rc;
	}
	if (!remote) {
		pr_debug("Handle local job\n");
		rc = rio_add_device_local(job);
	} else {
		pr_debug("Handle remote job\n");
		rc = rio_add_device_remote(job);
	}
	if (rc)
		pr_warn("RIO: Job aborted rc %d\n", rc);

	rio_net_register_devices(job->mport);
	return rc;
}

static int rio_job_remove_device(struct rio_job *job)
{
	int rc;
	struct rio_dev *rdev = NULL;

	pr_debug("RIO: remove job\n");

	rdev = rio_remove_device_root(job);
	if (IS_ERR(rdev)) {
		rc = PTR_ERR(rdev);
		goto done;
	}

	rc = rio_remove(job, rdev);
	rio_dev_put(rdev);

	if (!rc) {
		if (__rio_mport_net_empty(job->mport)) {
			if (job->mport->ops->pwenable)
				job->mport->ops->pwenable(job->mport, 0);
			iosync();
			if (rio_unlock_netid(job->mport->host_deviceid,
					     job->mport->net.id))
				pr_warn("RIO: Fail to unlock mport net_id %d\n",
					job->mport->net.id);
		}
	}
done:
	if (rc)
		pr_warn("RIO: remove job aborted rc %d\n", rc);
	return rc;
}

int rio_job_init(struct rio_mport *mport, struct rio_dev *rdev,
		 int port, u32 flags, int hw_access, int event)
{
	struct rio_job job;

	job.rdev = rdev;
	job.mport = mport;
	job.flags = flags;
	job.srio_down = (hw_access ? 0 : 1);
	job.port = port;

	if (event == RIO_DEVICE_INSERTION)
		return rio_job_add_device(&job);
	else if (event == RIO_DEVICE_EXTRACTION)
		return rio_job_remove_device(&job);
	else
		return -EINVAL;
}
