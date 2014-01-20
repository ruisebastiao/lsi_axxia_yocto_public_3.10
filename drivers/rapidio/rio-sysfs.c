/*
 * RapidIO sysfs attributes and support
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/slab.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/stat.h>
#include <linux/capability.h>
#ifdef NEW_STYLE
#include <linux/radix-tree.h>
#endif

#include "rio.h"

/* Sysfs support */
#define rio_config_attr(field, format_string)				\
static ssize_t								\
field##_show(struct device *dev, struct device_attribute *attr, char *buf) \
{									\
	struct rio_dev *rdev = to_rio_dev(dev);				\
									\
	return sprintf(buf, format_string, rdev->field);		\
}									\

rio_config_attr(did, "0x%04x\n");
rio_config_attr(vid, "0x%04x\n");
rio_config_attr(device_rev, "0x%08x\n");
rio_config_attr(asm_did, "0x%04x\n");
rio_config_attr(asm_vid, "0x%04x\n");
rio_config_attr(asm_rev, "0x%04x\n");
rio_config_attr(destid, "0x%04x\n");
rio_config_attr(hopcount, "0x%02x\n");

static ssize_t routes_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct rio_dev *rdev = to_rio_dev(dev);
	char *str = buf;
	int i;
	u8 port;

	if (rio_hw_lock_wait(rdev->hport, rdev->destid, rdev->hopcount, 1))
		goto done;

	for (i = 0; i < RIO_MAX_ROUTE_ENTRIES(rdev->hport->sys_size);
			i++) {
		if (rio_route_get_port(rdev, i, &port, 0))
			continue;

		if (port == RIO_INVALID_ROUTE)
			continue;
		str +=
		    sprintf(str, "%04x %02x\n", i, port);
	}

	rio_hw_unlock(rdev->hport, rdev->destid, rdev->hopcount);

done:
	return str - buf;
}

static ssize_t port_status_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct rio_dev *rdev = to_rio_dev(dev);
	char *str = buf;
	int no_ports, port, rc, link_ok;
	u32 value, status_reg, ctrl_reg;

	rc = rio_read_config_32(rdev, RIO_SWP_INFO_CAR, &value);
	if (rc) {
		pr_debug(
		   "RIO: (%s) Failed to read status reg addr=%08x result=%d\n",
		   __func__, RIO_SWP_INFO_CAR, rc);
		goto done;
	}
	no_ports = (value & RIO_SWP_INFO_PORT_TOTAL_MASK) >> 8;

	str += sprintf(str, "\nPort\tLink\tPORT_N_ERR\tPORT_N_CTL\n");
	str += sprintf(str, "------------------------------------------\n");

	for (port = 0; port < no_ports; port++) {
		rc = rio_read_config_32(rdev,
					rdev->phys_efptr +
					RIO_PORT_N_ERR_STS_CSR(port),
					&status_reg);
		if (rc) {
			pr_debug("RIO: (%s) Failed to read status reg addr=%08x result=%d\n",
				 __func__, rdev->phys_efptr +
				 RIO_PORT_N_ERR_STS_CSR(port), rc);
			goto done;
		}
		rc = rio_read_config_32(rdev,
					rdev->phys_efptr +
					RIO_PORT_N_CTL_CSR(port),
					&ctrl_reg);
		if (rc) {
			pr_debug("RIO: (%s) Failed to read control reg addr=%08x result=%d\n",
				 __func__, rdev->phys_efptr +
				 RIO_PORT_N_CTL_CSR(port), rc);
			goto done;
		}

		link_ok = ((status_reg & RIO_PORT_N_ERR_STS_PORT_OK) != 0);

		str += sprintf(str, "%3d\t%s\t0x%08x\t0x%08x\n",
			       port,
			       (link_ok) ? "OK" : "NOK",
			       status_reg,
			       ctrl_reg);
	}
	str += sprintf(str, "------------------------------------------\n\n");

done:
	return str - buf;
}

static ssize_t lut_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct rio_dev *rdev = to_rio_dev(dev);
	char *str = buf;
	u16 destid;

	for (destid = 0;
	     destid < RIO_ANY_DESTID(rdev->hport->sys_size); destid++) {
		u8 route_port;
		rdev->rswitch->get_entry(rdev->hport, rdev->destid,
					 rdev->hopcount, RIO_GLOBAL_TABLE,
					 destid, &route_port);
		if (route_port != RIO_INVALID_ROUTE)
			str += sprintf(str, "%04x %02x\n", destid, route_port);
	}
	return str - buf;
}

#ifdef NEW_STYLE
static ssize_t lprev_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct rio_dev *rdev = to_rio_dev(dev);
	struct rio_dev *prev;
	ssize_t count;

	rcu_read_lock();
	prev = radix_tree_lookup(&rdev->hport->net.dev_tree,
				 rdev->prev_destid);
	count = sprintf(buf, "%s\n",
			(prev) ? rio_name(prev) : "root");
	rcu_read_unlock();
	return count;
}

static ssize_t lnext_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct rio_dev *rdev = to_rio_dev(dev);
	char *str = buf;
	int i;

	if (rdev->pef & RIO_PEF_SWITCH) {
		for (i = 0; i < RIO_GET_TOTAL_PORTS(rdev->swpinfo); i++) {
			struct rio_dev *next = lookup_rdev_next(rdev, i);

			if (!IS_ERR(next)) {
				str += sprintf(str, "%s\n",
					       rio_name(next));
				rio_dev_put(next);
			} else
				str += sprintf(str, "null\n");
		}
	}

	return str - buf;
}

#else

static ssize_t lprev_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct rio_dev *rdev = to_rio_dev(dev);

	return sprintf(buf, "%s\n",
			(rdev->prev) ? rio_name(rdev->prev) : "root");
}

static ssize_t lnext_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct rio_dev *rdev = to_rio_dev(dev);
	char *str = buf;
	int i;

	if (rdev->pef & RIO_PEF_SWITCH) {
		for (i = 0; i < RIO_GET_TOTAL_PORTS(rdev->swpinfo); i++) {
			if (rdev->rswitch->port[i].rdev)
				str += sprintf(str, "%s\n",
					rio_name(rdev->rswitch->port[i].rdev));
			else
				str += sprintf(str, "null\n");
		}
	}

	return str - buf;
}
#endif

struct device_attribute rio_dev_attrs[] = {
	__ATTR_RO(did),
	__ATTR_RO(vid),
	__ATTR_RO(device_rev),
	__ATTR_RO(asm_did),
	__ATTR_RO(asm_vid),
	__ATTR_RO(asm_rev),
	__ATTR_RO(lprev),
	__ATTR_RO(destid),
	__ATTR_NULL,
};

static DEVICE_ATTR(routes, S_IRUGO, routes_show, NULL);
static DEVICE_ATTR(lnext, S_IRUGO, lnext_show, NULL);
static DEVICE_ATTR(hopcount, S_IRUGO, hopcount_show, NULL);
static DEVICE_ATTR(port_status, S_IRUGO, port_status_show, NULL);
static DEVICE_ATTR(lut, S_IRUGO, lut_show, NULL);


static ssize_t
rio_read_config(struct file *filp, struct kobject *kobj,
		struct bin_attribute *bin_attr,
		char *buf, loff_t off, size_t count)
{
	struct rio_dev *dev =
	    to_rio_dev(container_of(kobj, struct device, kobj));
	unsigned int size = 0x100;
	loff_t init_off = off;
	u8 *data = (u8 *) buf;

	/* Several chips lock up trying to read undefined config space */
	if (capable(CAP_SYS_ADMIN))
		size = RIO_MAINT_SPACE_SZ;

	if (off >= size)
		return 0;
	if (off + count > size) {
		size -= off;
		count = size;
	} else {
		size = count;
	}

	if ((off & 1) && size) {
		u8 val;
		rio_read_config_8(dev, off, &val);
		data[off - init_off] = val;
		off++;
		size--;
	}

	if ((off & 3) && size > 2) {
		u16 val;
		rio_read_config_16(dev, off, &val);
		data[off - init_off] = (val >> 8) & 0xff;
		data[off - init_off + 1] = val & 0xff;
		off += 2;
		size -= 2;
	}

	while (size > 3) {
		u32 val;
		rio_read_config_32(dev, off, &val);
		data[off - init_off] = (val >> 24) & 0xff;
		data[off - init_off + 1] = (val >> 16) & 0xff;
		data[off - init_off + 2] = (val >> 8) & 0xff;
		data[off - init_off + 3] = val & 0xff;
		off += 4;
		size -= 4;
	}

	if (size >= 2) {
		u16 val;
		rio_read_config_16(dev, off, &val);
		data[off - init_off] = (val >> 8) & 0xff;
		data[off - init_off + 1] = val & 0xff;
		off += 2;
		size -= 2;
	}

	if (size > 0) {
		u8 val;
		rio_read_config_8(dev, off, &val);
		data[off - init_off] = val;
		off++;
		--size;
	}

	return count;
}

static ssize_t
rio_write_config(struct file *filp, struct kobject *kobj,
		 struct bin_attribute *bin_attr,
		 char *buf, loff_t off, size_t count)
{
	struct rio_dev *dev =
	    to_rio_dev(container_of(kobj, struct device, kobj));
	unsigned int size = count;
	loff_t init_off = off;
	u8 *data = (u8 *) buf;

	if (off >= RIO_MAINT_SPACE_SZ)
		return 0;
	if (off + count > RIO_MAINT_SPACE_SZ) {
		size = RIO_MAINT_SPACE_SZ - off;
		count = size;
	}

	if ((off & 1) && size) {
		rio_write_config_8(dev, off, data[off - init_off]);
		off++;
		size--;
	}

	if ((off & 3) && (size > 2)) {
		u16 val = data[off - init_off + 1];
		val |= (u16) data[off - init_off] << 8;
		rio_write_config_16(dev, off, val);
		off += 2;
		size -= 2;
	}

	while (size > 3) {
		u32 val = data[off - init_off + 3];
		val |= (u32) data[off - init_off + 2] << 8;
		val |= (u32) data[off - init_off + 1] << 16;
		val |= (u32) data[off - init_off] << 24;
		rio_write_config_32(dev, off, val);
		off += 4;
		size -= 4;
	}

	if (size >= 2) {
		u16 val = data[off - init_off + 1];
		val |= (u16) data[off - init_off] << 8;
		rio_write_config_16(dev, off, val);
		off += 2;
		size -= 2;
	}

	if (size) {
		rio_write_config_8(dev, off, data[off - init_off]);
		off++;
		--size;
	}

	return count;
}

static struct bin_attribute rio_config_attr = {
	.attr = {
		 .name = "config",
		 .mode = S_IRUGO | S_IWUSR,
		 },
	.size = RIO_MAINT_SPACE_SZ,
	.read = rio_read_config,
	.write = rio_write_config,
};

/**
 * rio_create_sysfs_dev_files - create RIO specific sysfs files
 * @rdev: device whose entries should be created
 *
 * Create files when @rdev is added to sysfs.
 */
int rio_create_sysfs_dev_files(struct rio_dev *rdev)
{
	int err = 0;

	err = device_create_bin_file(&rdev->dev, &rio_config_attr);

	if (!err && (rdev->pef & RIO_PEF_SWITCH)) {
		err |= device_create_file(&rdev->dev, &dev_attr_routes);
		err |= device_create_file(&rdev->dev, &dev_attr_lnext);
		err |= device_create_file(&rdev->dev, &dev_attr_hopcount);
		err |= device_create_file(&rdev->dev, &dev_attr_port_status);
		err |= device_create_file(&rdev->dev, &dev_attr_lut);

		if (!err && rdev->rswitch->sw_sysfs)
			err = rdev->rswitch->sw_sysfs(rdev,
						      RIO_SW_SYSFS_CREATE);
	}

	if (err)
		pr_warning("RIO: Failed to create attribute file(s) for %s\n",
			   rio_name(rdev));

	return err;
}

/**
 * rio_remove_sysfs_dev_files - cleanup RIO specific sysfs files
 * @rdev: device whose entries we should free
 *
 * Cleanup when @rdev is removed from sysfs.
 */
void rio_remove_sysfs_dev_files(struct rio_dev *rdev)
{
	device_remove_bin_file(&rdev->dev, &rio_config_attr);
	if (rdev->pef & RIO_PEF_SWITCH) {
		device_remove_file(&rdev->dev, &dev_attr_routes);
		device_remove_file(&rdev->dev, &dev_attr_lnext);
		device_remove_file(&rdev->dev, &dev_attr_hopcount);

		if (rdev->rswitch->sw_sysfs)
			rdev->rswitch->sw_sysfs(rdev, RIO_SW_SYSFS_REMOVE);
	}
}

#if defined(CONFIG_RAPIDIO_HOTPLUG) || defined(CONFIG_RAPIDIO_STATIC_DESTID)
static ssize_t rio_devices_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct rio_mport *mport = container_of(dev, struct rio_mport, dev);
	char *str = buf;
	int i, num = 0;
	struct rio_dev **dptr = NULL;

	if (!mport)
		return -EINVAL;

	dptr = rio_get_all_devices(mport, &num);
	if (!dptr)
		return 0;

	if (IS_ERR(dptr))
		return PTR_ERR(dptr);

	str += sprintf(str, "Device table:\n");
	str += sprintf(str,
		       "name\t\tdid\tvid\tswpinfo\t\thops\thwlock\tlocal\n");
	for (i = 0; i < num; i++) {
		struct rio_dev *node = dptr[i];

		if (unlikely(!node))
			continue;

		str += sprintf(str,
			       "%s\t%4.4x\t%4.4x\t%8.8x\t%d\t%d\t%d\n",
			       rio_name(node),
			       node->did,
			       node->vid,
			       node->swpinfo,
			       node->hopcount,
			       node->use_hw_lock,
			       node->local_domain);
		rio_dev_put(node);
	}
	kfree(dptr);
	return str - buf;
}
static DEVICE_ATTR(devices, S_IRUGO, rio_devices_show, NULL);

int rio_sysfs_init(struct rio_mport *mport)
{
	int rc = 0;

	rc |= device_create_file(&mport->dev, &dev_attr_devices);

	return rc;
}
#endif
