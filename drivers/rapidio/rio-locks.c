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

/* #define DEBUG */
/* #define RIO_LOCK_DEBUG */

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

DECLARE_RWSEM(rio_tree_sem);
DEFINE_SPINLOCK(rio_lock_lock);

/* #define RIO_LOCK_DEBUG */

struct rio_dev *lookup_rdev(struct rio_mport *mport, u16 destid)
{
	struct rio_dev *rdev;

	rcu_read_lock();
	rdev = radix_tree_lookup(&mport->net.dev_tree, destid);
	if (rdev)
		rdev = rio_dev_get(rdev);
	rcu_read_unlock();
	return rdev;
}

struct rio_dev *lookup_rdev_next(struct rio_dev *rdev, int port_num)
{
	struct rio_dev *next = NULL;
	struct rio_mport *mport = rdev->hport;
	int rc;
	u16 device_destid;

	rc = rio_lookup_next_destid(mport, rdev->destid,
				    port_num, rdev->hopcount + 1,
				    &device_destid);
	if (rc)
		return ERR_PTR(rc);

	next = lookup_rdev(mport, device_destid);
	if (!next)
		return ERR_PTR(-ENODEV);

	return next;
}

/**
 * rio_set_host_lock - Sets the Host Device ID Lock CSR on a device
 * @port: Master port to send transaction
 * @destid: Destination ID of device
 * @hopcount: Number of hops to the device
 * @lock: Lock to be set on device
 *
 * Used during enumeration to set the Host Device ID Lock CSR on a
 * RIO device. Returns success of write.
 */
static int rio_set_host_lock(struct rio_mport *port, u16 destid, u8 hopcount)
{
	int rc;

	if (destid == port->host_deviceid)
        {
		rc = rio_local_write_config_32(port, RIO_HOST_DID_LOCK_CSR,
					       port->host_deviceid);
        }
	else
		rc = rio_mport_write_config_32(port, destid, hopcount,
					       RIO_HOST_DID_LOCK_CSR,
					       port->host_deviceid);
	if (rc)
		pr_debug("RIO:(%s) destid %hu hopcount %d - Write error\n",
			 __func__, destid, hopcount);
	return rc;
}

/**
 * rio_hw_lock - Acquires host device lock for specified device
 * @port: Master port to send transaction
 * @destid: Destination ID for device/switch
 * @hopcount: Hopcount to reach switch
 *
 * Attepts to acquire host device lock for specified device
 * Returns 0 if device lock acquired or ETIME if timeout expires.
 */
static int rio_hw_lock(struct rio_mport *port, u16 destid, u8 hopcount)
{
	u16 lock;
	int rc = 0;

	spin_lock(&rio_lock_lock);

	rc = rio_get_host_lock(port, destid, hopcount, &lock);
	if (rc)
		goto done;

	if (lock == port->host_deviceid)
		goto lock_err;

	/* Attempt to acquire device lock */
	rc = rio_set_host_lock(port, destid, hopcount);
	if (rc)
		goto done;
	rc = rio_get_host_lock(port, destid, hopcount, &lock);
	if (rc)
		goto done;

	if (lock != port->host_deviceid)
		goto to_err;

done:
#if defined(RIO_LOCK_DEBUG)
	pr_debug("RIO: Device destid 0x%x hopcount %hhu locked by 0x%x\n",
		 destid, hopcount, lock);
#endif
	spin_unlock(&rio_lock_lock);
	return rc;

to_err:
	pr_debug("RIO: timeout when locking device %hx:%hhu lock owned by %hu\n",
		 destid, hopcount, lock);
	rc = -ETIME;
	goto done;

lock_err:
	RAPIDIO_HW_LOCK_LOCK_ERR();
	pr_debug("RIO: Device destid %hx hopcount %hhu is already locked by %hx\n",
		 destid, hopcount, port->host_deviceid);
	rc = -EFAULT;
	goto done;
}


static int __rio_clear_locks(struct rio_dev *from, struct rio_dev *to)
{
	struct rio_dev *prev, *curr;
	int rc = 0;

	if (!from)
		return 0;

	if (!to)
		return 0;

	if (to->use_hw_lock) {
		rc = rio_hw_unlock(to->hport, to->destid, to->hopcount);
		if (rc)
			goto done;
	}

	pr_debug("RIO: Unlocked %s\n", rio_name(to));

	curr = to;

	rcu_read_lock();
	while (curr) {
		prev = radix_tree_lookup(&curr->hport->net.dev_tree,
					 curr->prev_destid);
		if (prev) {
			if (prev->use_hw_lock) {
				rc = rio_hw_unlock(prev->hport, prev->destid,
						   prev->hopcount);
				if (rc) {
					pr_debug("RIO: Failed to unlock %s\n",
						 rio_name(prev));
					rcu_read_unlock();
					goto done;
				}
			}
			pr_debug("RIO: Unlocked %s\n", rio_name(prev));
			if (prev == from) {
				rcu_read_unlock();
				goto done;
			}
		}
		curr = prev;
	}
	rcu_read_unlock();

done:
	return rc;
}
static int __rio_take_locks(struct rio_dev *from, struct rio_dev *to)
{
	struct rio_dev *prev, *curr;
	int rc = 0;

	if (!from)
		return 0;
	if (!to)
		return 0;

	if (to->use_hw_lock) {
		rc = rio_hw_lock(to->hport, to->destid, to->hopcount);
		if (rc)
			goto done;
	}

	pr_debug("RIO: Add lock to %s\n", rio_name(to));

	curr = to;

	rcu_read_lock();
	while (curr) {
		prev = radix_tree_lookup(&curr->hport->net.dev_tree,
					 curr->prev_destid);
		if (prev) {
			if (prev->use_hw_lock)
				rc = rio_hw_lock(prev->hport, prev->destid,
						 prev->hopcount);
				if (rc) {
					rcu_read_unlock();
					goto unlock;
				}
			pr_debug("RIO: Add lock to %s\n", rio_name(prev));
			if (prev == from) {
				rcu_read_unlock();
				goto done;
			}
		}
		curr = prev;
	}
	rcu_read_unlock();
done:
	return rc;

unlock:
	__rio_clear_locks(curr, to);
	goto done;
}
static int rio_hw_lock_devices(struct rio_mport *mport, struct rio_dev *from,
			       struct rio_dev *to)
{
	return __rio_take_locks(from, to);
}

static void rio_timeout(unsigned long data)
{
	/* timed out, set flag */
	*(int *)data = 1;
}

/**
 * rio_job_hw_lock_wait - Tries to take the @host_device_lock on a RIO device
 *			  or a group of RIO devices in a RIO network. A number
 *			  of attempts will be made to take the lock/locks.
 *			  Before each attempt, RIO job status GO is verified
 *			  and after each unsuccessful attempt the
 *			  rio_hw_lock() return status and timeout status is
 *			  checked. As long as the @tmo is not up and the
 *			  rio_hw_lock() return status indicates that
 *                        someone else owns the lock, the caller will be put
 *			  to sleep for a period of 10 ms.
 *
 * @job: RIO Job description
 * @from: Root Device to start locking from - set to @NULL if not applicable
 * @to: Last device to take lock on - set to @NULL if not applicable
 * @destid: Destination ID of device
 * @hopcount: Number of hops to the device
 * @tmo: Max time to wait in units of seconds
 *
 * If @from is defined it is assumed that the user have incremented the
 * @from reference count before calling rio_job_hw_lock_wait()
 *
 * Returns 0 at success and != 0 at failure
 *
 * In any case, the @from reference count is always consumed
 * if @from was defined.
 */
int rio_job_hw_lock_wait(struct rio_job *job, struct rio_dev *from,
			 struct rio_dev *to,
			 u16 destid, u8 hopcount, int tmo)
{
	int rc = 0;
	int timeout_flag = 0;
	struct timer_list rio_timer =
		TIMER_INITIALIZER(rio_timeout, 0, 0);

	rio_timer.expires = jiffies + tmo * HZ;
	rio_timer.data = (unsigned long)&timeout_flag;
	add_timer(&rio_timer);

	do {
		if (from)
			rc = rio_hw_lock_devices(job->mport, from, to);
		else
			rc = rio_hw_lock(job->mport, destid, hopcount);

		if (rc != -ETIME)
			goto done;

		msleep(10);

	} while (!timeout_flag);
done:
	del_timer_sync(&rio_timer);
	return rc;
}

/**
 * rio_job_hw_lock_wait_cond - Tries to take the @host_device_lock on a
 *			       RIO device. A number of attempts will be made
 *			       to take the lock. Before each attempt, RIO job
 *			       status GO is verified and after each
 *			       unsuccessful attempt the rio_hw_lock() return
 *			       status and timeout status is checked. As long
 *			       as the @tmo is not up and the rio_hw_lock()
 *			       return status indicates that someone else owns
 *			       the lock, the caller will be put to sleep
 *                             for a period of 50 ms. When the
 *			       @host_device_lock is set a user defined function
 *                             is invoked. A != 0 return value from this
 *			       function will terminate
*			       rio_job_hw_lock_wait_cond() otherwise the
 *                             @host_device_lock is relead and the wait
 *			       loop continues.
 *
 * @job: RIO Job description
 * @destid: Destination ID of device
 * @hopcount: Number of hops to the device
 * @tmo: Max time to wait in units of seconds
 *
 * Returns 0 at success and != 0 at failure
 */
int rio_job_hw_lock_wait_cond(struct rio_job *job, u16 destid,
			      u8 hopcount, int tmo, int lock,
			      int (*cond)(struct rio_mport *, u16, u8))
{
	int rc = 0;
	int timeout_flag = 0;
	struct timer_list rio_timer =
		TIMER_INITIALIZER(rio_timeout, 0, 0);

	rio_timer.expires = jiffies + tmo * HZ;
	rio_timer.data = (unsigned long)&timeout_flag;
	add_timer(&rio_timer);

	do {
		rc = rio_hw_lock(job->mport, destid, hopcount);
		if ((!lock) || (rc == 0)) {
			rc = cond(job->mport, destid, hopcount);
			if (rc == 0)
				goto done;
			rio_hw_unlock(job->mport, destid, hopcount);
		}
		if (rc != -ETIME)
			goto done;
		if (timeout_flag)
			break;
		msleep(50);

	} while (!timeout_flag);
done:
	del_timer_sync(&rio_timer);
	return rc;
}

int rio_job_hw_wait_cond(struct rio_job *job, u16 destid,
			 u8 hopcount, int tmo,
			 int (*cond)(struct rio_mport *, u16, u8))
{
	int rc = 0;
	int timeout_flag = 0;
	struct timer_list rio_timer =
		TIMER_INITIALIZER(rio_timeout, 0, 0);

	rio_timer.expires = jiffies + tmo * HZ;
	rio_timer.data = (unsigned long)&timeout_flag;
	add_timer(&rio_timer);

	do {
		rc = cond(job->mport, destid, hopcount);
		if (rc == 1)
			goto done;
		if (timeout_flag)
			break;
		msleep(100);

	} while (!timeout_flag);
done:
	del_timer_sync(&rio_timer);
	return rc;
}

/**
 * rio_hw_busy_lock_wait - Tries to take the @host_device_lock on a RIO device.
 *                         A number of attempts will be made to take the lock.
 *                         After each unsuccessful attempt the rio_hw_lock()
 *			   return status and timeout status is checked. As
			   long as the @wait_ms is not up and the
			   rio_hw_lock() return status indicates that
 *                         someone else owns the lock, the caller will be
 *			   delayed in a busy loop for a period of 1 ms.
 *
 * @mport: Master port to send transaction
 * @destid: Destination ID of device
 * @hopcount: Number of hops to the device
 * @wait_ms: Max time to wait in units of milli seconds
 *
 * Returns 0 at success and != 0 at failure
 */
int rio_hw_lock_busy_wait(struct rio_mport *mport, u16 destid,
			  u8 hopcount, int wait_ms)
{
	int tcnt = 0;
	int rc = rio_hw_lock(mport, destid, hopcount);

	while (rc == -ETIME) {
		if (tcnt >= wait_ms)
			break;
		mdelay(1);
		tcnt++;
		rc = rio_hw_lock(mport, destid, hopcount);
	}
	return rc;
}

/**
 * rio_hw_lock_wait - Tries to take the @host_device_lock on a RIO device.
 *                    A number of attempts will be made to take the lock.
 *                    After each unsuccessful attempt the rio_hw_lock() return
 *		      status and timeout status is checked. As long as the
 *		      @tmo is not up and the rio_hw_lock() return status
 *		      indicates that someone else owns the lock, the caller
 *		      will be put to sleep for a period of 10 ms.
 *
 * @mport: Master port to send transaction
 * @destid: Destination ID of device
 * @hopcount: Number of hops to the device
 * @tmo: Max time to wait in units of seconds
 *
 * Returns 0 at success and != 0 at failure
 */
int rio_hw_lock_wait(struct rio_mport *mport, u16 destid, u8 hopcount, int tmo)
{
	int rc = 0;
	int timeout_flag = 0;
	struct timer_list rio_timer =
		TIMER_INITIALIZER(rio_timeout, 0, 0);

	rio_timer.expires = jiffies + tmo * HZ;
	rio_timer.data = (unsigned long)&timeout_flag;
	add_timer(&rio_timer);

	do {
		rc = rio_hw_lock(mport, destid, hopcount);

		if (rc != -ETIME)
			goto done;

		msleep(10);

	} while (!timeout_flag);
done:
	del_timer_sync(&rio_timer);
	return rc;
}

/**
 * rio_get_by_ptr - search for a matching device in the global list
 *
 * @rdev: RIO device
 *
 * Increments rdev reference counter and returns rdev pointer
 * if a matching device pointer is found in the global list.
 * Users are responsible to decrement the reference count, i.e.
 * rio_dev_put() when done with the device.
 *
 * Returns NULL if RIO device doesn't exist in the global list.
 */
struct rio_dev *rio_get_by_ptr(struct rio_dev *rdev)
{
	struct rio_dev *tmp = NULL;

	if (!rdev)
		return NULL;

	while ((tmp = rio_get_device(RIO_ANY_ID, RIO_ANY_ID, tmp)) != NULL) {
		if (tmp == rdev)
			return tmp;
	}
	return NULL;
}

/**
 * rio_get_root_node - Find switch connected to local master port
 *                     in a RIO network.
 *
 * @mport: Local master port
 *
 * Increments switch reference counter and returns switch rdev pointer
 * if a matching switch device is found in the global list.
 * Users are responsible to decrement the reference count, i.e.
 * rio_dev_put() when done with the device.
 *
 * Returns NULL if a root switch doesn't exist in the global list.
 */
struct rio_dev *rio_get_root_node(struct rio_mport *mport)
{
	int rc;
	u16 device_destid;
	struct rio_dev *rdev = NULL;

	BUG_ON(!mport);

	rc = rio_lookup_next_destid(mport, mport->host_deviceid,
				    -1, 0, &device_destid);
	if (rc)
		return ERR_PTR(rc);

	rdev = lookup_rdev(mport, device_destid);
	if (!rdev)
		return ERR_PTR(-ENODEV);

	return rdev;
}

/**
 * rio_get_host_lock - Reads the Host Device ID Lock CSR on a device
 * @port: Master port to send transaction
 * @destid: Destination ID of device
 * @hopcount: Number of hops to the device
 * @result: Location where ID lock is stored
 *
 * Returns the RIO device access status.
 */
int rio_get_host_lock(struct rio_mport *port, u16 destid,
		      u8 hopcount, u16 *result)
{
	u32 regval;
	int rc;

	if (destid == port->host_deviceid)
        {
		rc = rio_local_read_config_32(port,
					      RIO_HOST_DID_LOCK_CSR, &regval);
        }
	else
        {
		rc = rio_mport_read_config_32(port, destid, hopcount,
					      RIO_HOST_DID_LOCK_CSR, &regval);
        }
	*result = regval & 0xffff;

	if (rc)
		pr_debug("RIO:(%s) destid %hx hopcount %d - Read error\n",
			 __func__, destid, hopcount);
	return rc;
}

/**
 * rio_clear_host_lock - Clear @lockid on a device
 * @port: Master port to send transaction
 * @destid: Destination ID of device
 * @hopcount: Number of hops to the device
 * @lockid: Device ID Lock to be cleared
 *
 * Returns 0 if the @lockid is cleared
 *
 * Returns < 0 if:
 * - At RIO device access faults
 * - If @lockid was not set on the device
 * - If the device was not unlocked
 */

int rio_clear_host_lock(struct rio_mport *port, u16 destid,
			u8 hopcount, u16 lockid)
{
	int rc;
	u16 result;

	spin_lock(&rio_lock_lock);
	rc = rio_get_host_lock(port, destid, hopcount, &result);
	if (rc)
		goto done;
	if (result != lockid) {
		pr_debug("RIO: device lock on %hx:%hhu is owned by %hu\n",
			 destid, hopcount, result);
		rc = -EINVAL;
		goto done;
	}
	if (destid == port->host_deviceid)
        {
		rc = rio_local_write_config_32(port, RIO_HOST_DID_LOCK_CSR,
					       lockid);
        }
	else
		rc = rio_mport_write_config_32(port, destid, hopcount,
					       RIO_HOST_DID_LOCK_CSR,
					       lockid);
	if (rc) {
		pr_debug("RIO: destid %hu hopcount %d - Write error\n",
			 destid, hopcount);
		goto done;
	}
	rc = rio_get_host_lock(port, destid, hopcount, &result);
	if (rc)
		goto done;

	if (result != 0xffff) {
		pr_debug("RIO: badness when releasing device lock %hx:%hhu\n",
			 destid, hopcount);
		rc = -EINVAL;
	}
done:
	spin_unlock(&rio_lock_lock);
	return rc;
}

/**
 * rio_hw_unlock - Releases @host_device_lock for specified device
 *
 * @port: Master port to send transaction
 * @destid: Destination ID for device/switch
 * @hopcount: Hopcount to reach switch
 *
 * Returns 0 if the @host_device_lock is cleared
 *
 * Returns < 0 if:
 * - At RIO device access faults
 * - If @host_device_lock was not set on the device
 * - If the device was not unlocked
 */
int rio_hw_unlock(struct rio_mport *port, u16 destid, u8 hopcount)
{
	u16 lock;
	int rc = 0;

	spin_lock(&rio_lock_lock);
	/* Release device lock */
	rc = rio_get_host_lock(port, destid, hopcount, &lock);
	if (rc)
		goto done;

	if (lock != port->host_deviceid)
		goto lock_err;

	rc = rio_set_host_lock(port, destid, hopcount);
	if (rc)
		goto done;

	rc = rio_get_host_lock(port, destid, hopcount, &lock);
	if (rc)
		goto done;

	if (lock != 0xffff)
		goto unlock_err;

done:
#if defined(RIO_LOCK_DEBUG)
	pr_debug("RIO: Device destid %hx hopcount %hhu is unlocked by %hx\n",
		 destid, hopcount, port->host_deviceid);
#endif
	spin_unlock(&rio_lock_lock);
	return rc;

lock_err:
	RAPIDIO_HW_UNLOCK_LOCK_ERR();
	pr_debug("RIO: release lock err - lock is not taken, destid %hx, hopcount %hhu, lock 0x%x",
		 destid, hopcount, lock);
	rc = -EINVAL;
	goto done;

unlock_err:
	pr_debug("RIO: badness when releasing device lock %hx:%hhu\n",
		 destid, hopcount);
	rc = -EINVAL;
	goto done;
}

/**
 * rio_job_hw_unlock_devices -	Releases @host_device_lock for a group
 *				of RIO devices. If the job description defines
 *				@rdev, locks are released on all devices
 *				between and including the switch connected
 *				to local master port and the job @rdev.
 *				The master port device ID Lock is
 *				always released.
 * @job: RIO Job description
 *
 */
void rio_job_hw_unlock_devices(struct rio_job *job)
{
	struct rio_dev *from = NULL;
	struct rio_dev *to = job->rdev;

	if (job->rdev)
		from = rio_get_root_node(job->mport);
	if (from && !IS_ERR(from)) {
		pr_debug("RIO: clear locks from %s to %s\n",
			 rio_name(from), rio_name(to));
		__rio_clear_locks(from, to);
		rio_dev_put(from);
	}
}
