/*
 * RapidIO hotplug support
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

/* #define DEBUG */

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

#include "rio.h"
#include "rio-hotplug.h"


#ifdef CONFIG_RAPIDIO_HOTPLUG
/**
 * Hotplug API internal to RIO driver - pw-event driven.
 */
/**
 * store_new_id - sysfs frontend to rio_add_dynid()
 * @driver: target device driver
 * @buf: buffer for scanning device ID data
 * @count: input size
 *
 * Allow RIO IDs to be added to an existing driver via sysfs.
 */
static ssize_t
store_new_id(struct device_driver *driver, const char *buf, size_t count)
{
	return count;
}
static DRIVER_ATTR(new_id, S_IWUSR, NULL, store_new_id);

/**
 * store_remove_id - remove a RIO device ID from this driver
 * @driver: target device driver
 * @buf: buffer for scanning device ID data
 * @count: input size
 *
 * Removes a dynamic RIO device ID to this driver.
 */
static ssize_t
store_remove_id(struct device_driver *driver, const char *buf, size_t count)
{
	return count;
}
static DRIVER_ATTR(remove_id, S_IWUSR, NULL, store_remove_id);

int rio_create_newid_file(struct rio_driver *drv)
{
	int error = 0;
	if (drv->probe != NULL)
		error = driver_create_file(&drv->driver, &driver_attr_new_id);
	return error;
}

void rio_remove_newid_file(struct rio_driver *drv)
{
	driver_remove_file(&drv->driver, &driver_attr_new_id);
}

int rio_create_removeid_file(struct rio_driver *drv)
{
	int error = 0;
	if (drv->probe != NULL)
		error = driver_create_file(&drv->driver,
					   &driver_attr_remove_id);
	return error;
}

void rio_remove_removeid_file(struct rio_driver *drv)
{
	driver_remove_file(&drv->driver, &driver_attr_remove_id);
}

/**
 * Hotplug API exported
 */

void rio_remove_mport_net(struct rio_mport *port, int hw_access)
{
	rio_job_init(port, NULL, -1, 0, hw_access, RIO_DEVICE_EXTRACTION);
}
EXPORT_SYMBOL_GPL(rio_remove_mport_net);
void rio_rescan_mport(struct rio_mport *port)
{
	rio_job_init(port, NULL, -1, 0, 1, RIO_DEVICE_INSERTION);
}
EXPORT_SYMBOL_GPL(rio_rescan_mport);

#endif

/*
 * FIXME!!! - remove as soon as hp is fixed on DUL
 */

/**
 * rio_init_device - sets up a RIO device
 * @port: Master port to send transactions
 * @rdev:
 * @hopcount: Current hopcount
 *
 */
int rio_init_device(struct rio_dev *rdev)
{
	struct rio_mport *port = rdev->hport;
	u8 hopcount = rdev->hopcount;
	u16 destid = rdev->destid;

	if (rio_has_destid(rdev->src_ops, rdev->dst_ops)) {
		rio_assign_destid(rdev, port, rdev->destid, rdev->hopcount,
				  &rdev->destid);
	}
	rio_fixup_dev(rio_fixup_early, rdev, destid, hopcount);
	/* Assign component tag to device */
	rio_mport_write_config_32(port, destid, hopcount,
				  RIO_COMPONENT_TAG_CSR, rdev->comp_tag);

	return 0;
}
EXPORT_SYMBOL(rio_init_device);
