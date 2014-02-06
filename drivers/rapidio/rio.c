/*
 * RapidIO interconnect services
 * (RapidIO Interconnect Specification, http://www.rapidio.org)
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Copyright 2009 Integrated Device Technology, Inc.
 * Alex Bounine <alexandre.bounine@idt.com>
 * - Added Port-Write/Error Management initialization and handling
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/rio_ids.h>
#include <linux/rio_regs.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/hardirq.h>
#include <linux/semaphore.h>

#include "rio.h"
#include "rio-job.h"

LIST_HEAD(rio_mports);
static unsigned char next_portid;

struct rio_dev **rio_get_tagged_devices(struct rio_mport *mport, int tag, int *n)
{
	int items = atomic_read(&mport->net.rio_dev_num);
	void **nptr = NULL;
	struct rio_dev **dptr = NULL;
	int i, num = 0;

	if (!items)
		goto done;

	nptr = kzalloc(sizeof(void *) * items, GFP_KERNEL);
	if (!nptr)
		goto err;
	dptr = kzalloc(sizeof(struct rio_dev *) * items, GFP_KERNEL);
	if (!dptr)
		goto err;

	rcu_read_lock();
retry:
	num = radix_tree_gang_lookup_tag_slot(&mport->net.dev_tree, (void ***)nptr,
					      0, items, tag);
	if (num > 0) {
		for (i = 0; i < num; i++) {
			struct rio_dev *rdev = radix_tree_deref_slot((void **)nptr[i]);

			if (unlikely(!rdev)) {
				dptr[i] = NULL;
				continue;
			}
			if (radix_tree_deref_retry(rdev))
				goto retry;
			dptr[i] = rio_dev_get(rdev);
		}
	}
	rcu_read_unlock();
done:
	if (nptr)
		kfree(nptr);
	*n = num;
	return dptr;
err:
	if (dptr)
		kfree(dptr);
	dptr = ERR_PTR(-ENOMEM);
	goto done;
}

struct rio_dev **rio_get_all_devices(struct rio_mport *mport, int *n)
{
	int items = atomic_read(&mport->net.rio_dev_num);
	void **nptr = NULL;
	struct rio_dev **dptr = NULL;
	int i, num = 0;

	if (!items)
		goto done;

	nptr = kzalloc(sizeof(void *) * items, GFP_KERNEL);
	if (!nptr)
		goto err;
	dptr = kzalloc(sizeof(struct rio_dev *) * items, GFP_KERNEL);
	if (!dptr)
		goto err;

	rcu_read_lock();
retry:
	num = radix_tree_gang_lookup_slot(&mport->net.dev_tree, (void ***)nptr,
					  NULL, 0, items);
	if (num > 0) {
		for (i = 0; i < num; i++) {
			struct rio_dev *rdev = radix_tree_deref_slot((void **)nptr[i]);

			if (unlikely(!rdev)) {
				dptr[i] = NULL;
				continue;
			}
			if (radix_tree_deref_retry(rdev))
				goto retry;

			dptr[i] = rio_dev_get(rdev);
		}
	}
	rcu_read_unlock();
done:
	if (nptr)
		kfree(nptr);
	*n = num;
	return dptr;
err:
	if (dptr)
		kfree(dptr);
	dptr = ERR_PTR(-ENOMEM);
	goto done;
}
EXPORT_SYMBOL_GPL(rio_get_all_devices);

int rio_get_err_and_status(struct rio_dev *rdev, int portnum, u32 *err_status)
{
        int rc;
	if ((rc = rio_read_config_32(rdev,
                                     rdev->phys_efptr + RIO_PORT_N_ERR_STS_CSR(portnum),
                                     err_status))) {
                pr_debug("RIO: Failed to read RIO_PORT_N_ERR_STS_CSR from device\n");
        }
        return rc;
}

/**
 * rio_is_switch- Tests if a RIO device has switch capabilities
 * @rdev: RIO device
 *
 * Gets the RIO device Processing Element Features register
 * contents and tests for switch capabilities. Returns 1 if
 * the device is a switch or 0 if it is not a switch.
 * The RIO device struct is freed.
 */
int rio_is_switch(struct rio_dev *rdev)
{
	if (rdev->pef & RIO_PEF_SWITCH) {
		return 1;
	}
	return 0;
}
#ifdef NEW_STYLE
int rio_type_of_next(struct rio_dev *sw_curr, struct rio_dev *sw_next)
{
	if (!sw_next)
		return RIO_PORT_UNUSED;

	if (rio_is_switch(sw_next)) {
		rcu_read_lock();
		if (sw_next->prev_destid != sw_curr->destid
		    || sw_curr == sw_next) {
			pr_debug("skip redundant path to %s\n", rio_name(sw_next));
			rcu_read_unlock();
			return RIO_REDUNDANT_PATH;
		}
		rcu_read_unlock();
		return RIO_SWITCH;
	}
	return RIO_END_POINT;
}
#else
int rio_type_of_next(struct rio_dev *sw_curr, struct rio_dev *sw_next)
{
	if (!sw_next)
		return RIO_PORT_UNUSED;

	if (rio_is_switch(sw_next)) {
		if (sw_next->prev != sw_curr || sw_curr == sw_next) {
			pr_debug("skip redundant path to %s\n", rio_name(sw_next));
			return RIO_REDUNDANT_PATH;
		}
		return RIO_SWITCH;
	}
	return RIO_END_POINT;
}
#endif
/**
 * rio_map_outb_mem -- Mapping outbound memory.
 * @mport:  RapidIO master port
 * @win:    Outbound ATMU window for this access
 *          - obtained by calling fsl_rio_req_outb_region.
 * @destid: Destination ID of transaction
 * @offset: RapidIO space start address.
 * @res:    Mapping region phys and virt start address
 *
 * Return: 0 -- Success.
 *
 */
int rio_map_outb_mem(struct rio_mport *mport, u32 win,
		     u16 destid, u32 offset, u32 mflags,
		     struct rio_map_addr *res)
{
	int rc = 0;

	if (!mport->mops)
		return -1;
	rc = mport->mops->map_outb(mport, win, destid, offset, mflags, res);
	return rc;
}
EXPORT_SYMBOL_GPL(rio_map_outb_mem);

/**
 * rio_req_outb_region -- Request outbound region in the
 * RapidIO bus address space.
 * @mport:  RapidIO master port
 * @size:   The mapping region size.
 * @name:   Resource name
 * @flags:  Flags for mapping. 0 for using default flags.
 * @id:     Allocated outbound ATMU window id
 *
 * Return: 0 -- Success.
 *
 * This function will reserve a memory region that may
 * be used to create mappings from local iomem to rio space.
 */
int rio_req_outb_region(struct rio_mport *mport,
			resource_size_t size,
			const char *name,
			u32 mflags, u32 *win)
{
	int rc = 0;

	if (!mport->mops)
		return -1;
	rc = mport->mops->req_outb(mport, size, name, mflags, win);
	return rc;
}
EXPORT_SYMBOL_GPL(rio_req_outb_region);

/**
 * rio_release_outb_region -- Unreserve outbound memory region.
 * @mport: RapidIO master port
 * @win:   Allocated outbound ATMU window id
 */
void rio_release_outb_region(struct rio_mport *mport, u32 win)
{
	if (!mport->mops)
		return;
	mport->mops->release_outb(mport, win);
}
EXPORT_SYMBOL_GPL(rio_release_outb_region);

/**
 * rio_local_get_device_id - Get the base/extended device id for a port
 * @port: RIO master port from which to get the deviceid
 *
 * Reads the base/extended device id from the local device
 * implementing the master port. Returns the 8/16-bit device
 * id.
 */
u16 rio_local_get_device_id(struct rio_mport *port)
{
	u32 result;

	if (rio_local_read_config_32(port, RIO_DID_CSR, &result))
		pr_debug("RIO: Failed to read RIO_DID_CSR\n");

	return (RIO_GET_DID(port->sys_size, result));
}

/**
 * rio_request_inb_mbox - request inbound mailbox service
 * @mport: RIO master port from which to allocate the mailbox resource
 * @dev_id: Device specific pointer to pass on event
 * @mbox: Mailbox number to claim
 * @entries: Number of entries in inbound mailbox queue
 * @minb: Callback to execute when inbound message is received
 *
 * Requests ownership of an inbound mailbox resource and binds
 * a callback function to the resource. Returns %0 on success.
 */
int rio_request_inb_mbox(struct rio_mport *mport,
			 void *dev_id,
			 int mbox,
			 int entries,
			 void (*minb) (struct rio_mport * mport, void *dev_id, int mbox,
				       int slot))
{
	int rc = -ENOSYS;
	struct resource *res;

	if (mport->ops->open_inb_mbox == NULL)
		goto out;

	res = kmalloc(sizeof(struct resource), GFP_KERNEL);

	if (res) {
		rio_init_mbox_res(res, mbox, mbox);

		/* Make sure this mailbox isn't in use */
		if ((rc =
		     request_resource(&mport->riores[RIO_INB_MBOX_RESOURCE],
				      res)) < 0) {
			kfree(res);
			goto out;
		}

		mport->inb_msg[mbox].res = res;

		/* Hook the inbound message callback */
		mport->inb_msg[mbox].mcback = minb;

		rc = mport->ops->open_inb_mbox(mport, dev_id, mbox, entries);
	} else
		rc = -ENOMEM;

out:
	return rc;
}

/**
 * rio_release_inb_mbox - release inbound mailbox message service
 * @mport: RIO master port from which to release the mailbox resource
 * @mbox: Mailbox number to release
 *
 * Releases ownership of an inbound mailbox resource. Returns 0
 * if the request has been satisfied.
 */
int rio_release_inb_mbox(struct rio_mport *mport, int mbox)
{
	if (mport->ops->close_inb_mbox) {
		mport->ops->close_inb_mbox(mport, mbox);

		/* Release the mailbox resource */
		if (mport->inb_msg[mbox].res)
			return release_resource(mport->inb_msg[mbox].res);
		else
			return -ENOMEM;
	} else
		return -ENOSYS;
}

/**
 * rio_request_outb_mbox - request outbound mailbox service
 * @mport: RIO master port from which to allocate the mailbox resource
 * @dev_id: Device specific pointer to pass on event
 * @mbox: Mailbox number to claim
 * @entries: Number of entries in outbound mailbox queue
 * @moutb: Callback to execute when outbound message is sent
 *
 * Requests ownership of an outbound mailbox resource and binds
 * a callback function to the resource. Returns 0 on success.
 */
int rio_request_outb_mbox(struct rio_mport *mport,
			  void *dev_id,
			  int mbox,
			  int entries,
			  int prio,
			  void (*moutb) (struct rio_mport * mport, void *dev_id,
					 int mbox, int slot, void *cookie))
{
	int rc = -ENOSYS;
	struct resource *res;

	if (mport->ops->open_outb_mbox == NULL)
		goto out;

	res = kmalloc(sizeof(struct resource), GFP_KERNEL);

	if (res) {
		rio_init_mbox_res(res, mbox, mbox);

		/* Make sure this outbound mailbox isn't in use */
		if ((rc =
		     request_resource(&mport->riores[RIO_OUTB_MBOX_RESOURCE],
				      res)) < 0) {
			kfree(res);
			goto out;
		}

		mport->outb_msg[mbox].res = res;

		/* Hook the inbound message callback */
		mport->outb_msg[mbox].mcback = moutb;

		rc = mport->ops->open_outb_mbox(mport, dev_id, mbox, entries, prio);
	} else
		rc = -ENOMEM;

out:
	return rc;
}

/**
 * rio_release_outb_mbox - release outbound mailbox message service
 * @mport: RIO master port from which to release the mailbox resource
 * @mbox: Mailbox number to release
 *
 * Releases ownership of an inbound mailbox resource. Returns 0
 * if the request has been satisfied.
 */
int rio_release_outb_mbox(struct rio_mport *mport, int mbox)
{
	if (mport->ops->close_outb_mbox) {
		mport->ops->close_outb_mbox(mport, mbox);

		/* Release the mailbox resource */
		if (mport->outb_msg[mbox].res)
			return release_resource(mport->outb_msg[mbox].res);
		else
			return -ENOMEM;
	} else
		return -ENOSYS;
}

/**
 * rio_setup_inb_dbell - bind inbound doorbell callback
 * @mport: RIO master port to bind the doorbell callback
 * @dev_id: Device specific pointer to pass on event
 * @res: Doorbell message resource
 * @dinb: Callback to execute when doorbell is received
 *
 * Adds a doorbell resource/callback pair into a port's
 * doorbell event list. Returns 0 if the request has been
 * satisfied.
 */
static int
rio_setup_inb_dbell(struct rio_mport *mport, void *dev_id, struct resource *res,
		    void (*dinb) (struct rio_mport * mport, void *dev_id, u16 src, u16 dst,
				  u16 info))
{
	int rc = 0;
	struct rio_dbell *dbell;

	if (!(dbell = kmalloc(sizeof(struct rio_dbell), GFP_KERNEL))) {
		rc = -ENOMEM;
		goto out;
	}

	dbell->res = res;
	dbell->dinb = dinb;
	dbell->dev_id = dev_id;

	list_add_tail(&dbell->node, &mport->dbells);

out:
	return rc;
}

/**
 * rio_request_inb_dbell - request inbound doorbell message service
 * @mport: RIO master port from which to allocate the doorbell resource
 * @dev_id: Device specific pointer to pass on event
 * @start: Doorbell info range start
 * @end: Doorbell info range end
 * @dinb: Callback to execute when doorbell is received
 *
 * Requests ownership of an inbound doorbell resource and binds
 * a callback function to the resource. Returns 0 if the request
 * has been satisfied.
 */
int rio_request_inb_dbell(struct rio_mport *mport,
			  void *dev_id,
			  u16 start,
			  u16 end,
			  void (*dinb) (struct rio_mport * mport, void *dev_id, u16 src,
					u16 dst, u16 info))
{
	int rc = 0;

	struct resource *res = kmalloc(sizeof(struct resource), GFP_KERNEL);

	if (res) {
		rio_init_dbell_res(res, start, end);

		/* Make sure these doorbells aren't in use */
		if ((rc =
		     request_resource(&mport->riores[RIO_DOORBELL_RESOURCE],
				      res)) < 0) {
			kfree(res);
			goto out;
		}

		/* Hook the doorbell callback */
		rc = rio_setup_inb_dbell(mport, dev_id, res, dinb);
	} else
		rc = -ENOMEM;

out:
	return rc;
}

/**
 * rio_release_inb_dbell - release inbound doorbell message service
 * @mport: RIO master port from which to release the doorbell resource
 * @start: Doorbell info range start
 * @end: Doorbell info range end
 *
 * Releases ownership of an inbound doorbell resource and removes
 * callback from the doorbell event list. Returns 0 if the request
 * has been satisfied.
 */
int rio_release_inb_dbell(struct rio_mport *mport, u16 start, u16 end)
{
	int rc = 0, found = 0;
	struct rio_dbell *dbell;

	list_for_each_entry(dbell, &mport->dbells, node) {
		if ((dbell->res->start == start) && (dbell->res->end == end)) {
			found = 1;
			break;
		}
	}

	/* If we can't find an exact match, fail */
	if (!found) {
		rc = -EINVAL;
		goto out;
	}

	/* Delete from list */
	list_del(&dbell->node);

	/* Release the doorbell resource */
	rc = release_resource(dbell->res);

	/* Free the doorbell event */
	kfree(dbell);

out:
	return rc;
}

/**
 * rio_request_outb_dbell - request outbound doorbell message range
 * @rdev: RIO device from which to allocate the doorbell resource
 * @start: Doorbell message range start
 * @end: Doorbell message range end
 *
 * Requests ownership of a doorbell message range. Returns a resource
 * if the request has been satisfied or %NULL on failure.
 */
struct resource *rio_request_outb_dbell(struct rio_dev *rdev, u16 start,
					u16 end)
{
	struct resource *res = kmalloc(sizeof(struct resource), GFP_KERNEL);

	if (res) {
		rio_init_dbell_res(res, start, end);

		/* Make sure these doorbells aren't in use */
		if (request_resource(&rdev->riores[RIO_DOORBELL_RESOURCE], res)
		    < 0) {
			kfree(res);
			res = NULL;
		}
	}

	return res;
}

/**
 * rio_release_outb_dbell - release outbound doorbell message range
 * @rdev: RIO device from which to release the doorbell resource
 * @res: Doorbell resource to be freed
 *
 * Releases ownership of a doorbell message range. Returns 0 if the
 * request has been satisfied.
 */
int rio_release_outb_dbell(struct rio_dev *rdev, struct resource *res)
{
	int rc = release_resource(res);

	kfree(res);

	return rc;
}

/**
 * rio_request_inb_pwrite - request inbound port-write message service
 * @rdev: RIO device to which register inbound port-write callback routine
 * @pwcback: Callback routine to execute when port-write is received
 *
 * Binds a port-write callback function to the RapidIO device.
 * Returns 0 if the request has been satisfied.
 */
int rio_request_inb_pwrite(struct rio_dev *rdev,
                           int (*pwcback)(struct rio_dev *rdev, union rio_pw_msg *msg, int step))
{
	int rc = 0;

	spin_lock(&rio_global_list_lock);
	if (rdev->pwcback != NULL)
		rc = -ENOMEM;
	else
		rdev->pwcback = pwcback;

	spin_unlock(&rio_global_list_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(rio_request_inb_pwrite);

/**
 * rio_release_inb_pwrite - release inbound port-write message service
 * @rdev: RIO device which registered for inbound port-write callback
 *
 * Removes callback from the rio_dev structure. Returns 0 if the request
 * has been satisfied.
 */
int rio_release_inb_pwrite(struct rio_dev *rdev)
{
	int rc = -ENOMEM;

	spin_lock(&rio_global_list_lock);
	if (rdev->pwcback) {
		rdev->pwcback = NULL;
		rc = 0;
	}

	spin_unlock(&rio_global_list_lock);
	return rc;
}
EXPORT_SYMBOL_GPL(rio_release_inb_pwrite);

/**
 * rio_mport_get_physefb - Helper function that returns register offset
 *                      for Physical Layer Extended Features Block.
 * @port: Master port to issue transaction
 * @local: Indicate a local master port or remote device access
 * @destid: Destination ID of the device
 * @hopcount: Number of switch hops to the device
 */
int
rio_mport_get_physefb(struct rio_mport *port, int local,
		      u16 destid, u8 hopcount, u32 *physefb)
{
	u32 ext_ftr_ptr;
	u32 ftr_header;
        int rc = 0;

	if ((rc = rio_mport_get_efb(port, local, destid, hopcount, 0, &ext_ftr_ptr)))
                goto done;

	while (ext_ftr_ptr)  {
		if (local) {
			if ((rc = rio_local_read_config_32(port, ext_ftr_ptr,
                                                           &ftr_header)))
                                goto done;
                } else {
			if ((rc = rio_mport_read_config_32(port, destid, hopcount,
                                                           ext_ftr_ptr, &ftr_header)))
                                goto done;
                }

		ftr_header = RIO_GET_BLOCK_ID(ftr_header);
		switch (ftr_header) {

		case RIO_EFB_SER_EP_ID_V13P:
		case RIO_EFB_SER_EP_REC_ID_V13P:
		case RIO_EFB_SER_EP_FREE_ID_V13P:
		case RIO_EFB_SER_EP_ID:
		case RIO_EFB_SER_EP_REC_ID:
		case RIO_EFB_SER_EP_FREE_ID:
		case RIO_EFB_SER_EP_FREC_ID:

                        goto done;

		default:
			break;
		}

		if ((rc = rio_mport_get_efb(port, local, destid,
                                            hopcount, ext_ftr_ptr, &ext_ftr_ptr)))
                        goto done;
	}

done:
        *physefb = ext_ftr_ptr;
	return rc;
}
EXPORT_SYMBOL_GPL(rio_mport_get_physefb);

/**
 * rio_get_comptag - Begin or continue searching for a RIO device by component tag
 * @comp_tag: RIO component tag to match
 * @from: Previous RIO device found in search, or %NULL for new search
 *
 * Iterates through the list of known RIO devices. If a RIO device is
 * found with a matching @comp_tag, a pointer to its device
 * structure is returned. Otherwise, %NULL is returned. A new search
 * is initiated by passing %NULL to the @from argument. Otherwise, if
 * @from is not %NULL, searches continue from next device on the global
 * list.
 */
struct rio_dev *rio_get_comptag(struct rio_mport *mport, u32 comp_tag)
{
	int items = atomic_read(&mport->net.rio_dev_num);
	void **nptr = kzalloc(sizeof(void *) * items, GFP_KERNEL);
	int i, num;
	struct rio_dev *rdev = NULL;

	if (!nptr)
		return NULL;

	rcu_read_lock();
retry:
	num = radix_tree_gang_lookup_slot(&mport->net.dev_tree, (void ***)nptr,
					  NULL, 0, items);
	if (num > 0) {
		for (i = 0; i < num; i++) {
			struct rio_dev *tmp = radix_tree_deref_slot((void **)nptr[i]);

			if (unlikely(!tmp))
				continue;

			if (radix_tree_deref_retry(tmp))
				goto retry;

			if (tmp->comp_tag == comp_tag) {
				rdev = rio_dev_get(tmp);
				goto done;
			}
		}
	}
done:
	rcu_read_unlock();
	kfree(nptr);
	return rdev;
}

/**
 * rio_set_port_lockout - Sets/clears LOCKOUT bit (RIO EM 1.3) for a switch port.
 * @rdev: Pointer to RIO device control structure
 * @pnum: Switch port number to set LOCKOUT bit
 * @lock: Operation : set (=1) or clear (=0)
 */
static int
rio_set_port_lockout(struct rio_dev *rdev, u32 pnum, int lock, int do_lock)
{
        int rc = 0;
	u32 regval;

        if (do_lock)
                if ((rc = rio_hw_lock_wait(rdev->hport, rdev->destid, rdev->hopcount, 10)))
                        goto done;

	if ((rc = rio_read_config_32(rdev,
                                     rdev->phys_efptr + RIO_PORT_N_CTL_CSR(pnum),
                                     &regval)))
                goto rel_lock;

	if (lock)
		regval |= RIO_PORT_N_CTL_LOCKOUT;
	else
		regval &= ~RIO_PORT_N_CTL_LOCKOUT;

	rc = rio_write_config_32(rdev,
                                 rdev->phys_efptr + RIO_PORT_N_CTL_CSR(pnum),
                                 regval);

	pr_debug("RIO_EM: %s_port_lockout %s port %d addr %8.8x == %8.8x\n",
		 (lock ? "set":"clear"), rio_name(rdev), pnum, rdev->phys_efptr + RIO_PORT_N_CTL_CSR(pnum), regval);

rel_lock:
        if (do_lock) {
                if (rc)
                        rio_hw_unlock(rdev->hport, rdev->destid, rdev->hopcount);
                else
                        rc = rio_hw_unlock(rdev->hport, rdev->destid, rdev->hopcount);
        }
done:
	return rc;
}


/**
 * rio_mport_chk_dev_access - Validate access to the specified device.
 * @mport: Master port to send transactions
 * @destid: Device destination ID in network
 * @hopcount: Number of hops into the network
 */
int
rio_mport_chk_dev_access(struct rio_mport *mport, u16 destid, u8 hopcount)
{
	u32 tmp;

	return rio_mport_read_config_32(mport, destid, hopcount,
					RIO_DEV_ID_CAR, &tmp);
}

/**
 * rio_chk_dev_access - Validate access to the specified device.
 * @rdev: Pointer to RIO device control structure
 */
static int rio_chk_dev_access(struct rio_dev *rdev)
{
	return rio_mport_chk_dev_access(rdev->hport,
					rdev->destid, rdev->hopcount);
}

/**
 * rio_get_input_status - Sends a Link-Request/Input-Status control symbol and
 *                        returns link-response (if requested).
 * @rdev: RIO devive to issue Input-status command
 * @pnum: Device port number to issue the command
 * @lnkresp: Response from a link partner
 */
static int
rio_get_input_status(struct rio_dev *rdev, int pnum, u32 request, u32 *lnkresp)
{
	u32 regval;
	int checkcount;
        int rc = 0;

	if (lnkresp) {
		/* Read from link maintenance response register
		 * to clear valid bit */
		if ((rc = rio_read_config_32(rdev,
                                             rdev->phys_efptr + RIO_PORT_N_MNT_RSP_CSR(pnum),
                                             &regval)))
                        goto done;
		udelay(50);
	}

	/* Issue Input-status command */
	if ((rc = rio_write_config_32(rdev,
                                      rdev->phys_efptr + RIO_PORT_N_MNT_REQ_CSR(pnum),
                                      request)))
                goto done;

	/* Exit if the response is not expected */
	if (lnkresp == NULL)
                goto done;

	checkcount = 3;
	while (checkcount--) {
		udelay(50);
		if ((rc = rio_read_config_32(rdev,
                                             rdev->phys_efptr + RIO_PORT_N_MNT_RSP_CSR(pnum),
                                             &regval)))
                        goto done;
		if (regval & RIO_PORT_N_MNT_RSP_RVAL) {
			*lnkresp = regval;
                        goto done;
		}
	}

	rc = -EIO;
done:
        return rc;
}

/**
 * rio_clr_err_stopped - Clears port Error-stopped states.
 * @rdev: Pointer to RIO device control structure
 * @pnum: Switch port number to clear errors
 * @err_status: port error status (if 0 reads register from device)
 */
int
rio_clr_err_stopped(struct rio_dev *rdev, u32 pnum, u32 err_status, int *success)
{
	struct rio_dev *nextdev;
	u32 regval;
	u32 far_ackid, far_linkstat, near_ackid;
        int rc = 0;

	nextdev = lookup_rdev_next(rdev, pnum);
	if (IS_ERR(nextdev))
		nextdev = NULL;

        if (rdev->use_hw_lock &&
            ((rc = rio_hw_lock_wait(rdev->hport, rdev->destid, rdev->hopcount, 10))))
                goto done;

        if (err_status == 0) {
		if ((rc = rio_get_err_and_status(rdev, pnum, &err_status)))
                        goto err;
        }

	if (err_status & RIO_PORT_N_ERR_STS_PW_OUT_ES) {
		pr_debug("RIO_EM: servicing Output Error-Stopped state\n");
		/*
		 * Send a Link-Request/Input-Status control symbol
		 */
                rc = rio_get_input_status(rdev, pnum, RIO_MNT_REQ_CMD_IS, &regval);
                if (rc == -EIO) {
			pr_debug("RIO_EM: Input-status response timeout\n");
			goto rd_err;
                } else if (rc) {
			goto err;
                }

		pr_debug("RIO_EM: SP%d Input-status response=0x%08x\n",
			 pnum, regval);
		far_ackid = (regval & RIO_PORT_N_MNT_RSP_ASTAT) >> 5;
		far_linkstat = regval & RIO_PORT_N_MNT_RSP_LSTAT;
		if ((rc = rio_read_config_32(rdev,
                                             rdev->phys_efptr + RIO_PORT_N_ACK_STS_CSR(pnum),
                                             &regval)))
                        goto err;
		pr_debug("RIO_EM: SP%d_ACK_STS_CSR=0x%08x\n", pnum, regval);
		near_ackid = (regval & RIO_PORT_N_ACK_INBOUND) >> 24;
		pr_debug("RIO_EM: SP%d far_ackID=0x%02x far_linkstat=0x%02x" \
			 " near_ackID=0x%02x\n",
                         pnum, far_ackid, far_linkstat, near_ackid);

		/*
		 * If required, synchronize ackIDs of near and
		 * far sides.
		 */
		if ((far_ackid != ((regval & RIO_PORT_N_ACK_OUTSTAND) >> 8)) ||
		    (far_ackid != (regval & RIO_PORT_N_ACK_OUTBOUND))) {
			/* Align near outstanding/outbound ackIDs with
			 * far inbound.
			 */
			if ((rc = rio_write_config_32(rdev,
                                                      rdev->phys_efptr + RIO_PORT_N_ACK_STS_CSR(pnum),
                                                      (near_ackid << 24) |
                                                      (far_ackid << 8) | far_ackid)))
                                goto err;
			/* Align far outstanding/outbound ackIDs with
			 * near inbound.
			 */
			far_ackid++;
			if (nextdev) {
				if ((rc = rio_write_config_32(nextdev,
                                                              nextdev->phys_efptr +
                                                              RIO_PORT_N_ACK_STS_CSR(
                                                                      RIO_GET_PORT_NUM(nextdev->swpinfo)),
                                                              (far_ackid << 24) |
                                                              (near_ackid << 8) | near_ackid)))
                                        goto err;
                        } else {
				pr_debug("RIO_EM: Invalid nextdev pointer (NULL)\n");
                        }
		}
        rd_err:
		if ((rc = rio_get_err_and_status(rdev, pnum, &err_status)))
                        goto err;
		pr_debug("RIO_EM: SP%d_ERR_STS_CSR=0x%08x\n", pnum, err_status);
	}

	if ((err_status & RIO_PORT_N_ERR_STS_PW_INP_ES) && nextdev) {
		pr_debug("RIO_EM: servicing Input Error-Stopped state\n");
		rio_get_input_status(nextdev,
				     RIO_GET_PORT_NUM(nextdev->swpinfo), RIO_MNT_REQ_CMD_IS, NULL);
		udelay(50);

		if ((rc = rio_get_err_and_status(rdev, pnum, &err_status)))
                        goto err;
		pr_debug("RIO_EM: SP%d_ERR_STS_CSR=0x%08x\n", pnum, err_status);
	}

        *success = (err_status & (RIO_PORT_N_ERR_STS_PW_OUT_ES |
                                  RIO_PORT_N_ERR_STS_PW_INP_ES)) ? 0 : 1;
        goto rel_lock;
err:
        pr_debug("RIO_EM: Read/Write Error\n");
        *success = 0;
rel_lock:
        if (rdev->use_hw_lock) {
                if (rc)
                        rio_hw_unlock(rdev->hport, rdev->destid, rdev->hopcount);
                else
                        rc = rio_hw_unlock(rdev->hport, rdev->destid, rdev->hopcount);
        }
done:
	if (nextdev)
		rio_dev_put(nextdev);
        return rc;
}
/**
 * @brief Brute force sync ackid method
 *
 * Send link maint request to LP then configure
 * near end so that inbound, outstanding and outbound
 * ackid corresponds to far end next expected ackid.
 *
 * This will work as long as far end next expected
 * inbound == outbound (which is usually the case)
 *
 * @note The proper is probably to first sync near
 * end to far end next expected and then do maint
 * request to LP to sync far en outbound to near end
 * inbound. To do that, however, you'd need to know
 * the phys extended feature ptr offset and the port
 * number of the LP, and that information is hard to
 * come by as long as near and far end ackid are not
 * in sync
 */
static int rio_ackid_sync(struct rio_dev *rdev, u32 pnum)
{
	struct rio_dev *nextdev;
	u32 regval;
	u32 far_ackid, far_linkstat, near_ackid;
        int rc = 0;

	nextdev = lookup_rdev_next(rdev, pnum);
	if (IS_ERR(nextdev))
		nextdev = NULL;

        if (rdev->use_hw_lock &&
            ((rc = rio_hw_lock_wait(rdev->hport, rdev->destid, rdev->hopcount, 10))))
                goto done;

	pr_debug("RIO_EM: Send link request\n");
	/*
	 * Send a Link-Request/Input-Status control symbol
	 */
	rc = rio_get_input_status(rdev, pnum, RIO_MNT_REQ_CMD_IS, &regval);
	if (rc == -EIO) {
		pr_warn("RIO_EM: Input-status response timeout\n");
		goto rd_err;
	} else if (rc) {
		pr_warn("RIO_EM: Input-status response other err %d\n", rc);
		goto err;
	}

	pr_debug("RIO_EM: SP%d Input-status response=0x%08x\n",
		 pnum, regval);
	far_ackid = (regval & RIO_PORT_N_MNT_RSP_ASTAT) >> 5;
	far_linkstat = regval & RIO_PORT_N_MNT_RSP_LSTAT;
	if ((rc = rio_read_config_32(rdev,
				     rdev->phys_efptr + RIO_PORT_N_ACK_STS_CSR(pnum),
				     &regval)))
		goto err;

	pr_debug("RIO_EM: SP%d_ACK_STS_CSR=0x%08x\n", pnum, regval);
	near_ackid = (regval & RIO_PORT_N_ACK_INBOUND) >> 24;
	pr_debug("RIO_EM: SP%d far_ackID=0x%02x far_linkstat=0x%02x"	\
		 " near_ackID=0x%02x\n",
		 pnum, far_ackid, far_linkstat, near_ackid);

	if (far_linkstat != 0x10) {
		pr_warn("RIO_EM: LP link state 0x%08x\n", far_linkstat);
		rc = -EIO;
		goto err;
	}
	/*
	 * If required, synchronize ackIDs of near and
	 * far sides.
	 */
	if ((far_ackid != ((regval & RIO_PORT_N_ACK_OUTSTAND) >> 8)) ||
	    (far_ackid != (regval & RIO_PORT_N_ACK_OUTBOUND))) {
		/* Align near outstanding/outbound ackIDs with
		 * far inbound.
		 */
		if ((rc = rio_write_config_32(rdev,
					      rdev->phys_efptr + RIO_PORT_N_ACK_STS_CSR(pnum),
					      (far_ackid << 24) |
					      (far_ackid << 8) | far_ackid)))
			goto err;
	}
	goto rel_lock;
rd_err:
	if ((rc = rio_get_err_and_status(rdev, pnum, &regval)))
		goto err;
	pr_warn("RIO_EM: SP%d_ERR_STS_CSR=0x%08x\n", pnum, regval);
        goto rel_lock;
err:
        pr_warn("RIO_EM: Read/Write Error\n");
rel_lock:
        if (rdev->use_hw_lock) {
                if (rc)
                        rio_hw_unlock(rdev->hport, rdev->destid, rdev->hopcount);
                else
                        rc = rio_hw_unlock(rdev->hport, rdev->destid, rdev->hopcount);
        }
done:
	if (nextdev)
		rio_dev_put(nextdev);
        return rc;
}

static int
rio_pw_dump_msg(union rio_pw_msg *pw_msg)
{
#ifdef DEBUG_PW
        u32 i;
        for (i = 0; i < RIO_PW_MSG_SIZE/sizeof(u32);) {
                pr_debug("0x%02x: %08x %08x %08x %08x\n",
                         i * 4, pw_msg->raw[i], pw_msg->raw[i + 1],
                         pw_msg->raw[i + 2], pw_msg->raw[i + 3]);
                i += 4;
        }
#endif
        return 0;
}

static int
rio_handle_local_domain(struct rio_dev *rdev, int portnum, u32 err_status)
{
        int rc = 0;
	u32 em_perrdet, em_ltlerrdet;

	if (rdev->local_domain) {
                if (rdev->use_hw_lock &&
                    ((rc = rio_hw_lock_wait(rdev->hport, rdev->destid, rdev->hopcount, 10))))
                        goto done;

		if ((rc = rio_read_config_32(rdev,
                                             rdev->em_efptr + RIO_EM_PN_ERR_DETECT(portnum),
                                             &em_perrdet))) {
                        pr_debug("RIO_PW: Failed to read RIO_EM_PN_ERR_DETECT from device\n");
                        goto rel_lock;
                }
		if (em_perrdet) {
			/* Clear EM Port N Error Detect CSR */
			if ((rc = rio_write_config_32(rdev,
                                                      rdev->em_efptr + RIO_EM_PN_ERR_DETECT(portnum),
                                                      0))) {
                                pr_debug("RIO_PW: Failed to write RIO_EM_PN_ERR_DETECT to device\n");
                                goto rel_lock;
                        }
		}
		if ((rc = rio_read_config_32(rdev,
                                             rdev->em_efptr + RIO_EM_LTL_ERR_DETECT,
                                             &em_ltlerrdet))) {
                        pr_debug("RIO_PW: Failed to read RIO_EM_LTL_ERR_DETECT from device\n");
                        goto rel_lock;
                }
		if (em_ltlerrdet) {
			/* Clear EM L/T Layer Error Detect CSR */
			if ((rc = rio_write_config_32(rdev,
                                                      rdev->em_efptr + RIO_EM_LTL_ERR_DETECT, 0))) {
                                pr_debug("RIO_PW: Failed to write RIO_EM_LTL_ERR_DETECT to device\n");
                                goto rel_lock;
                        }
		}
		/* Clear remaining error bits and Port-Write Pending bit */
		if ((rc = rio_write_config_32(rdev,
                                              rdev->phys_efptr + RIO_PORT_N_ERR_STS_CSR(portnum),
                                              err_status))) {
                        pr_debug("RIO_PW: Failed to write RIO_PORT_N_ERR_STS_CSR to device\n");
                        goto rel_lock;
                }
        rel_lock:
                if (rdev->use_hw_lock) {
                        if (rc)
                                rio_hw_unlock(rdev->hport, rdev->destid, rdev->hopcount);
                        else
                                rc = rio_hw_unlock(rdev->hport, rdev->destid, rdev->hopcount);
                }
	}
done:
        return rc;
}

static int
rio_handle_events(struct rio_dev *rdev, int portnum, u32 err_status, int *event)
{
        int rc = 0;
        int success;

	rio_tree_write_lock();

	if (err_status & RIO_PORT_N_ERR_STS_PORT_OK) {
		pr_debug("RIO: port OK\n");
                if ((!(rdev->rswitch->port_init & (1 << portnum))) ||
                    (!(rdev->rswitch->port_ok & (1 << portnum)))) {
			rdev->rswitch->port_init |= (1 << portnum);
			rdev->rswitch->port_ok |= (1 << portnum);
			if (rdev->local_domain) {
				if ((rc = rio_set_port_lockout(rdev, portnum, 0, 1))) {
                                        pr_warn("RIO: Failed to set port lockout bit.\n");
                                        goto done;
                                }
                        }
		}
                /* Schedule Insertion Service */
                pr_debug("RIO: Device Insertion on [%s]-P%d\n",
                         rio_name(rdev), portnum);
                *event = RIO_DEVICE_INSERTION;

		if (rdev->local_domain) {
			/* Clear error-stopped states (if reported).
			 * Depending on the link partner state, two attempts
			 * may be needed for successful recovery.
			 */
			if (err_status & (RIO_PORT_N_ERR_STS_PW_OUT_ES |
					  RIO_PORT_N_ERR_STS_PW_INP_ES)) {
                                if ((rc = rio_clr_err_stopped(rdev, portnum, err_status, &success))) {
                                        goto done;
				}
				if (!success) {
					rc = rio_clr_err_stopped(rdev, portnum, 0, &success);
                                        if (rc) {
                                                goto done;
					}
                                }
			}
		}
	}  else { /* if (err_status & RIO_PORT_N_ERR_STS_PORT_UNINIT) */
                if ((!(rdev->rswitch->port_init & (1 << portnum))) ||
                    (rdev->rswitch->port_ok & (1 << portnum))) {
			rdev->rswitch->port_init |= (1 << portnum);
			rdev->rswitch->port_ok &= ~(1 << portnum);
			if (rdev->local_domain) {
                                if (rdev->use_hw_lock &&
                                    ((rc = rio_hw_lock_wait(rdev->hport,
                                                            rdev->destid,
                                                            rdev->hopcount, 10))))
                                        goto done;
				if ((rc = rio_set_port_lockout(rdev, portnum, 1, 0))) {
                                        pr_warn("RIO: Failed to set port lockout bit.\n");
                                        if (rdev->use_hw_lock)
                                                rio_hw_unlock(rdev->hport,
                                                              rdev->destid,
                                                              rdev->hopcount);
                                        goto done;
                                }
				if ((rc = rio_write_config_32(rdev,
                                                              rdev->phys_efptr +
                                                              RIO_PORT_N_ACK_STS_CSR(portnum),
                                                              RIO_PORT_N_ACK_CLEAR))) {
                                        pr_warn("RIO: Failed to write to RIO_PORT_N_ACK_STS_CSR.\n");
                                        if (rdev->use_hw_lock)
                                                rio_hw_unlock(rdev->hport,
                                                              rdev->destid,
                                                              rdev->hopcount);
                                        goto done;
                                }
                                if (rdev->use_hw_lock &&
                                    ((rc = rio_hw_unlock(rdev->hport,
                                                         rdev->destid,
                                                         rdev->hopcount))))
                                        goto done;
			}
		}
                /* Schedule Extraction Service */
                pr_debug("RIO: Device Extraction on [%s]-P%d\n",
                         rio_name(rdev), portnum);
                *event = RIO_DEVICE_EXTRACTION;
	}
done:
        rio_tree_write_unlock();
        return rc;
}
static int __rio_setup_event(struct rio_dev *rdev, int portnum, int event)
{
        int rc = 0;
	int tmp_event = 0;
	u32 err_status;

        rdev = rio_get_by_ptr(rdev);
        if (!rdev)
                goto done;

	if (rdev->local_domain) {
		if (rdev->rswitch->em_handle) {
                        if ((!rdev->use_hw_lock) ||
                            (!rio_hw_lock_wait(rdev->hport, rdev->destid, rdev->hopcount, 10))) {
                                rdev->rswitch->em_handle(rdev, portnum);
                                if (rdev->use_hw_lock)
                                        rio_hw_unlock(rdev->hport, rdev->destid, rdev->hopcount);
                        }
                }
	}
	if ((rc = rio_get_err_and_status(rdev, portnum, &err_status)))
                goto rel_dev;
	pr_debug("port %d RIO_PORT_N_ERR_STS_CSR 0x%x\n", portnum, err_status);
        if ((rc = rio_handle_events(rdev, portnum, err_status, &tmp_event)))
                goto rel_dev;

        if (tmp_event != event) {
                rc = 1;
                if (event == RIO_DEVICE_EXTRACTION) {
                        pr_err("RIO: Removal of not reseted device is not allowed.\n");
                } else {
                        pr_err("RIO: Insertion of device on not ok port is not allowed.\n");
		}
                goto rel_dev;
        }

        if ((rc = rio_handle_local_domain(rdev, portnum, err_status)))
                goto rel_dev;

rel_dev:
        rio_dev_put(rdev);
done:
	return rc;
}
int rio_setup_event(struct rio_dev *rdev, int portnum, int event)
{
	return __rio_setup_event(rdev, portnum, event);
}
int rio_setup_event_force(struct rio_dev *rdev, int portnum, int event)
{
	int rc =  __rio_setup_event(rdev, portnum, event);
	if (!rc && (event == RIO_DEVICE_INSERTION))
		rio_ackid_sync(rdev, portnum);
	return rc;
}

/**
 * rio_inb_pwrite_handler - process inbound port-write message
 * @pw_msg: pointer to inbound port-write message
 *
 * Processes an inbound port-write message. Returns 0 if the request
 * has been satisfied.
 */
int
rio_inb_pwrite_handler(struct rio_mport *mport, union rio_pw_msg *pw_msg)
{
	struct rio_dev *rdev;
        int rc = 0;
        int portnum;

	pr_debug("RIO-EM: --- %s ---\n", __func__);

	rdev = rio_get_comptag(mport, (pw_msg->em.comptag & RIO_CTAG_UDEVID));
	if (rdev == NULL) {
		/* Device removed or enumeration error */
		pr_debug("RIO_PW: %s No matching device for CTag 0x%08x\n",
                         __func__, pw_msg->em.comptag);
                rc = -EIO;
                goto done;
	}
	pr_debug("RIO_PW: Port-Write message from %s\n", rio_name(rdev));

        rio_pw_dump_msg(pw_msg);

	/* Call an external service function (if such is registered
	 * for this device). This may be the service for endpoints that send
	 * device-specific port-write messages. End-point messages expected
	 * to be handled completely by EP specific device driver.
	 * For switches rc==0 signals that no standard processing required.
	 */
	if (rdev->pwcback != NULL) {
		rc = rdev->pwcback(rdev, pw_msg, 0);
		if (rc == 0)
                        goto rel_dev;
	}

	portnum = pw_msg->em.is_port & 0xFF;

	/* Check if device and route to it are functional:
	 * Sometimes devices may send PW message(s) just before being
	 * powered down (or link being lost).
	 */
	if (rio_chk_dev_access(rdev)) {
		pr_debug("RIO_PW: device access failed\n");
                rc = -EIO;
                goto rel_dev;
	}

	/* For End-point devices processing stops here */
	if (!(rdev->pef & RIO_PEF_SWITCH))
                goto rel_dev;

	if (!rdev->phys_efptr) {
		pr_err("RIO_PW: Bad switch initialization for %s\n",
                       rio_name(rdev));
                goto rel_dev;
	}

	if (rdev->local_domain) {
		/* Process the port-write notification from switch
		 */
		if (rdev->rswitch->em_handle) {
			rdev->rswitch->em_handle(rdev, portnum);
                }
	}

#if !defined(CONFIG_RAPIDIO_HOTPLUG)
/**
 * FIXME!
 * Temporary fix to make ulma booting on DUL work
 * with the old version of the ulma driver for DUL
 * which is not adopted to hotplug
 * Maybe we should do this anyway if user don't define
 * hotplug - to keep the same level of PW handling as
 * in original RIO driver - i.e. call rio_setup_event,
 * not the rest obviously.
 */
	{
		u32 err_status;
		int event = 0;
		if ((rc = rio_get_err_and_status(rdev, portnum, &err_status)))
			goto rel_dev;

		if (err_status & RIO_PORT_N_ERR_STS_PORT_OK)
			event = RIO_DEVICE_INSERTION;
		else
			event = RIO_DEVICE_EXTRACTION;

		rc = rio_setup_event(rdev, portnum, event);

		if (rdev->pwcback != NULL && rc == 0)
			(void)rdev->pwcback(rdev, pw_msg, event);
	}
#endif

rel_dev:
        rio_dev_put(rdev);
done:
	return rc;
}
EXPORT_SYMBOL_GPL(rio_inb_pwrite_handler);

/**
 * rio_mport_get_efb - get pointer to next extended features block
 * @port: Master port to issue transaction
 * @local: Indicate a local master port or remote device access
 * @destid: Destination ID of the device
 * @hopcount: Number of switch hops to the device
 * @from: Offset of  current Extended Feature block header (if 0 starts
 * from	ExtFeaturePtr)
 */
int
rio_mport_get_efb(struct rio_mport *port, int local, u16 destid,
                  u8 hopcount, u32 from, u32 *ext_ftr_ptr)
{
	u32 reg_val;
        int rc = 0;

	if (from == 0) {
		if (local)
			rc = rio_local_read_config_32(port, RIO_ASM_INFO_CAR,
                                                      &reg_val);
		else
			rc = rio_mport_read_config_32(port, destid, hopcount,
                                                      RIO_ASM_INFO_CAR, &reg_val);
                if (!rc)
                        *ext_ftr_ptr = reg_val & RIO_EXT_FTR_PTR_MASK;
	} else {
		if (local)
			rc = rio_local_read_config_32(port, from, &reg_val);
		else
			rc = rio_mport_read_config_32(port, destid, hopcount,
                                                      from, &reg_val);
                if (!rc)
                        *ext_ftr_ptr = RIO_GET_BLOCK_ID(reg_val);
	}
        if (rc)
                *ext_ftr_ptr = 0;
        return rc;
}

/**
 * rio_mport_get_feature - query for devices' extended features
 * @port: Master port to issue transaction
 * @local: Indicate a local master port or remote device access
 * @destid: Destination ID of the device
 * @hopcount: Number of switch hops to the device
 * @ftr: Extended feature code
 *
 * Tell if a device supports a given RapidIO capability.
 * Returns the offset of the requested extended feature
 * block within the device's RIO configuration space or
 * 0 in case the device does not support it.  Possible
 * values for @ftr:
 *
 * %RIO_EFB_PAR_EP_ID		LP/LVDS EP Devices
 *
 * %RIO_EFB_PAR_EP_REC_ID	LP/LVDS EP Recovery Devices
 *
 * %RIO_EFB_PAR_EP_FREE_ID	LP/LVDS EP Free Devices
 *
 * %RIO_EFB_SER_EP_ID		LP/Serial EP Devices
 *
 * %RIO_EFB_SER_EP_REC_ID	LP/Serial EP Recovery Devices
 *
 * %RIO_EFB_SER_EP_FREE_ID	LP/Serial EP Free Devices
 */
int
rio_mport_get_feature(struct rio_mport * port, int local, u16 destid,
		      u8 hopcount, int ftr, u32 *feature)
{
	u32 asm_info, ext_ftr_ptr = 0, ftr_header;
        int rc = 0;

	if (local) {
		if ((rc = rio_local_read_config_32(port, RIO_ASM_INFO_CAR, &asm_info)))
                        goto done;
        } else {
		if ((rc = rio_mport_read_config_32(port, destid, hopcount,
                                                   RIO_ASM_INFO_CAR, &asm_info)))
                        goto done;
        }

	ext_ftr_ptr = asm_info & RIO_EXT_FTR_PTR_MASK;

	while (ext_ftr_ptr) {
		if (local) {
			if ((rc = rio_local_read_config_32(port, ext_ftr_ptr,
                                                           &ftr_header)))
                                goto done;
		} else {
			if ((rc = rio_mport_read_config_32(port, destid, hopcount,
                                                           ext_ftr_ptr, &ftr_header)))
                                goto done;
                }
		if (RIO_GET_BLOCK_ID(ftr_header) == ftr)
                        goto done;
		if (!(ext_ftr_ptr = RIO_GET_BLOCK_PTR(ftr_header)))
			break;
	}
done:
        *feature = ext_ftr_ptr;
	return rc;
}
/**
 * rio_get_asm - Begin or continue searching for a RIO device by vid/did/asm_vid/asm_did
 * @vid: RIO vid to match or %RIO_ANY_ID to match all vids
 * @did: RIO did to match or %RIO_ANY_ID to match all dids
 * @asm_vid: RIO asm_vid to match or %RIO_ANY_ID to match all asm_vids
 * @asm_did: RIO asm_did to match or %RIO_ANY_ID to match all asm_dids
 * @from: Previous RIO device found in search, or %NULL for new search
 *
 * Iterates through the list of known RIO devices. If a RIO device is
 * found with a matching @vid, @did, @asm_vid, @asm_did, the reference
 * count to the device is incrememted and a pointer to its device
 * structure is returned. Otherwise, %NULL is returned. A new search
 * is initiated by passing %NULL to the @from argument. Otherwise, if
 * @from is not %NULL, searches continue from next device on the global
 * list. The reference count for @from is always decremented if it is
 * not %NULL.
 */
struct rio_dev *rio_get_asm(u16 vid, u16 did,
			    u16 asm_vid, u16 asm_did, struct rio_dev *from)
{
	struct rio_mport *mport = (from ? from->hport : rio_get_mport(RIO_ANY_ID, NULL));
	int items;
	void **nptr;
	int i, num;
	struct rio_dev *rdev = NULL;
	unsigned long key = (from ? from->destid : 0);

	rcu_read_lock();
	while (mport) {
		items = atomic_read(&mport->net.rio_dev_num);
		if (!items) {
			rcu_read_unlock();
			return NULL;
		}
		nptr = kzalloc(sizeof(void *) * items, GFP_KERNEL);
		if (!nptr) {
			rcu_read_unlock();
			return NULL;
		}
	retry:
		num = radix_tree_gang_lookup_slot(&mport->net.dev_tree, (void ***)nptr,
						  NULL, key, items);
		if (num > 0) {
			for (i = 0; i < num; i++) {
				rdev = radix_tree_deref_slot((void **)nptr[i]);
				if (unlikely(!rdev))
					continue;
				if (radix_tree_deref_retry(rdev))
					goto retry;
				if (rdev == from)
					continue;
				if ((vid == RIO_ANY_ID || rdev->vid == vid) &&
				    (did == RIO_ANY_ID || rdev->did == did) &&
				    (asm_vid == RIO_ANY_ID || rdev->asm_vid == asm_vid) &&
				    (asm_did == RIO_ANY_ID || rdev->asm_did == asm_did)) {
					kfree(nptr);
					goto done;
				}
			}
		}
		kfree(nptr);
		mport = rio_get_mport(RIO_ANY_ID, mport);
		key = 0;
	}
	rdev = NULL;
done:
	rio_dev_put(from);
	rdev = rio_dev_get(rdev);
	rcu_read_unlock();
	return rdev;
}

/**
 * rio_get_device - Begin or continue searching for a RIO device by vid/did
 * @vid: RIO vid to match or %RIO_ANY_ID to match all vids
 * @did: RIO did to match or %RIO_ANY_ID to match all dids
 * @from: Previous RIO device found in search, or %NULL for new search
 *
 * Iterates through the list of known RIO devices. If a RIO device is
 * found with a matching @vid and @did, the reference count to the
 * device is incrememted and a pointer to its device structure is returned.
 * Otherwise, %NULL is returned. A new search is initiated by passing %NULL
 * to the @from argument. Otherwise, if @from is not %NULL, searches
 * continue from next device on the global list. The reference count for
 * @from is always decremented if it is not %NULL.
 */
struct rio_dev *rio_get_device(u16 vid, u16 did, struct rio_dev *from)
{
	return rio_get_asm(vid, did, RIO_ANY_ID, RIO_ANY_ID, from);
}

/**
 * rio_std_route_add_entry - Add switch route table entry using standard
 *   registers defined in RIO specification rev.1.3
 * @mport: Master port to issue transaction
 * @destid: Destination ID of the device
 * @hopcount: Number of switch hops to the device
 * @table: routing table ID (global or port-specific)
 * @route_destid: destID entry in the RT
 * @route_port: destination port for specified destID
 */
int
rio_std_route_add_entry(struct rio_mport *mport, u16 destid, u8 hopcount,
                            u16 table, u16 route_destid, u8 route_port)
{
        int rc = 0;
	if (table == RIO_GLOBAL_TABLE) {
		if ((rc = rio_mport_write_config_32(mport, destid, hopcount,
                                                    RIO_STD_RTE_CONF_DESTID_SEL_CSR,
                                                    (u32)route_destid)))
                        goto done;
		if ((rc = rio_mport_write_config_32(mport, destid, hopcount,
                                                    RIO_STD_RTE_CONF_PORT_SEL_CSR,
                                                    (u32)route_port)))
                        goto done;
	}

	udelay(10);
done:
	return rc;
}

/**
 * rio_std_route_get_entry - Read switch route table entry (port number)
 *   associated with specified destID using standard registers defined in RIO
 *   specification rev.1.3
 * @mport: Master port to issue transaction
 * @destid: Destination ID of the device
 * @hopcount: Number of switch hops to the device
 * @table: routing table ID (global or port-specific)
 * @route_destid: destID entry in the RT
 * @route_port: returned destination port for specified destID
 */
int
rio_std_route_get_entry(struct rio_mport *mport, u16 destid, u8 hopcount,
                        u16 table, u16 route_destid, u8 *route_port)
{
	u32 result = 0;
        int rc = 0;

	if (table == RIO_GLOBAL_TABLE) {
		if ((rc = rio_mport_write_config_32(mport, destid, hopcount,
                                                    RIO_STD_RTE_CONF_DESTID_SEL_CSR,
                                                    route_destid)))
                        goto done;
		if ((rc = rio_mport_read_config_32(mport, destid, hopcount,
                                                   RIO_STD_RTE_CONF_PORT_SEL_CSR,
                                                   &result)))
                        goto done;

		*route_port = (u8)result;
	}
done:
	return rc;
}

/**
 * rio_std_route_clr_table - Clear swotch route table using standard registers
 *   defined in RIO specification rev.1.3.
 * @mport: Master port to issue transaction
 * @destid: Destination ID of the device
 * @hopcount: Number of switch hops to the device
 * @table: routing table ID (global or port-specific)
 */
int
rio_std_route_clr_table(struct rio_mport *mport, u16 destid,
                        u8 hopcount, u16 table)
{
	u32 max_destid = 0xff;
	u32 i, pef, id_inc = 1, ext_cfg = 0;
	u32 port_sel = RIO_INVALID_ROUTE;
        int rc = 0;

	if (table == RIO_GLOBAL_TABLE) {
		if ((rc = rio_mport_read_config_32(mport, destid, hopcount,
                                                   RIO_PEF_CAR, &pef)))
                        goto done;

		if (mport->sys_size) {
			if ((rc = rio_mport_read_config_32(mport, destid, hopcount,
                                                           RIO_SWITCH_RT_LIMIT,
                                                           &max_destid)))
                                goto done;
			max_destid &= RIO_RT_MAX_DESTID;
		}

		if (pef & RIO_PEF_EXT_RT) {
			ext_cfg = 0x80000000;
			id_inc = 4;
			port_sel = (RIO_INVALID_ROUTE << 24) |
                                (RIO_INVALID_ROUTE << 16) |
                                (RIO_INVALID_ROUTE << 8) |
                                RIO_INVALID_ROUTE;
		}

		for (i = 0; i <= max_destid;) {
			if ((rc = rio_mport_write_config_32(mport, destid, hopcount,
                                                            RIO_STD_RTE_CONF_DESTID_SEL_CSR,
                                                            ext_cfg | i)))
                                goto done;
			if ((rc = rio_mport_write_config_32(mport, destid, hopcount,
                                                            RIO_STD_RTE_CONF_PORT_SEL_CSR,
                                                            port_sel)))
                                goto done;
			i += id_inc;
		}
	}

done:
	udelay(10);
	return rc;
}

static void rio_fixup_device(struct rio_dev *dev)
{
}

static int rio_init(void)
{
	struct rio_dev *dev = NULL;

	while ((dev = rio_get_device(RIO_ANY_ID, RIO_ANY_ID, dev)) != NULL) {
		rio_fixup_device(dev);
	}
	return 0;
}
/**
 * @note No lock; Assuming this is used at boot time only,
 *       before start of user space
 */
int rio_init_mports(void)
{
	struct rio_mport *port;

	list_for_each_entry(port, &rio_mports, node) {
		rio_job_init(port, NULL, -1, 0, 1, RIO_DEVICE_INSERTION);
	}

	rio_init();

	return 0;
}

device_initcall_sync(rio_init_mports);

static int hdids[RIO_MAX_MPORTS + 1];

static int rio_get_hdid(int index)
{
	if (!hdids[0] || hdids[0] <= index || index >= RIO_MAX_MPORTS)
		return -1;

	return hdids[index + 1];
}

static int rio_hdid_setup(char *str)
{
	(void)get_options(str, ARRAY_SIZE(hdids), hdids);
	return 1;
}

__setup("riohdid=", rio_hdid_setup);

#if defined(CONFIG_RAPIDIO_STATIC_DESTID) || defined(CONFIG_RAPIDIO_HOTPLUG)

static void rio_add_mport_device(struct rio_mport *mport)
{
	dev_set_name(&mport->dev, "mport:%04x", mport->id);
	mport->dev.bus = NULL;
	mport->dev.parent = &rio_bus;

	if (device_register(&mport->dev))
		pr_warn("RIO: mport device register failure\n");
	else {
		rio_destid_sysfs_init(mport);
		rio_sysfs_init(mport);
	}
}
#else
static inline void rio_add_mport_device(struct rio_mport *port) {}
#endif

struct rio_net *rio_get_mport_net(struct rio_mport *port)
{
	return &port->net;
}

struct rio_mport *rio_get_mport(int hostid, struct rio_mport *from)
{
	struct rio_mport *port;
	struct list_head *n;

	if (list_empty(&rio_mports))
        {
		return NULL;
        }

	n = from ? from->node.next : rio_mports.next;
	while (n && (n != &rio_mports)) {
		port = list_entry(n, struct rio_mport, node);
		if (hostid == RIO_ANY_ID || hostid == port->host_deviceid)
                {
			return port;
                }
		n = n->next;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(rio_get_mport);


int rio_register_mport(struct rio_mport *port)
{
	if (next_portid >= RIO_MAX_MPORTS) {
		pr_err("RIO: reached specified max number of mports\n");
		return 1;
	}
	port->id = next_portid++;
	port->host_deviceid = rio_get_hdid(port->id);
#ifndef CONFIG_RAPIDIO_STATIC_DESTID
	if (port->host_deviceid == 0xb)
        {
		port->host_deviceid = RIO_ANY_ID;
        }
#endif

#ifdef NEW_STYLE
	INIT_RADIX_TREE(&port->net.dev_tree, GFP_KERNEL);
	INIT_RADIX_TREE(&port->net.dst_tree, GFP_KERNEL);
	atomic_set(&port->net.rio_dev_num, 0);
	atomic_set(&port->net.rio_dst_num, 0);
	atomic_set(&port->net.rio_max_dest, 0);
	spin_lock_init(&port->net.tree_lock);
#else
	INIT_LIST_HEAD(&port->net.devices);
#endif
	rio_add_mport_device(port);
	list_add_tail(&port->node, &rio_mports);
	return 0;
}
EXPORT_SYMBOL_GPL(rio_local_get_device_id);
EXPORT_SYMBOL_GPL(rio_get_device);
EXPORT_SYMBOL_GPL(rio_get_asm);
EXPORT_SYMBOL_GPL(rio_request_inb_dbell);
EXPORT_SYMBOL_GPL(rio_release_inb_dbell);
EXPORT_SYMBOL_GPL(rio_request_outb_dbell);
EXPORT_SYMBOL_GPL(rio_release_outb_dbell);
EXPORT_SYMBOL_GPL(rio_request_inb_mbox);
EXPORT_SYMBOL_GPL(rio_release_inb_mbox);
EXPORT_SYMBOL_GPL(rio_request_outb_mbox);
EXPORT_SYMBOL_GPL(rio_release_outb_mbox);
EXPORT_SYMBOL_GPL(rio_register_mport);
