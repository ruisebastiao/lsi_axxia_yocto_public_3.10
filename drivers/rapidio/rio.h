/*
 * RapidIO interconnect services
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef DRIVERS_RAPIDIO_RIO_H
#define DRIVERS_RAPIDIO_RIO_H

#include <linux/device.h>
#include <linux/export.h>
#include <linux/rwsem.h>
#include <linux/list.h>
#include <linux/rio.h>
#include <asm/rio.h>

#include "rio-hotplug.h"
#include "rio-multicast.h"
#include "rio-destid.h"
#include "rio-job.h"
#include "rio-locks.h"
#include "rio-net.h"
#include "rio-route.h"

#define RIO_MAX_CHK_RETRY	3

#define RIO_PORT_UNUSED 0
#define RIO_REDUNDANT_PATH 1
#define RIO_END_POINT 2
#define RIO_SWITCH 3

/* Functions internal to the RIO core code */
extern struct rio_dev **rio_get_tagged_devices(struct rio_mport *mport,
					       int tag, int *n);

extern int rio_type_of_next(struct rio_dev *sw_curr, struct rio_dev *sw_next);
extern int rio_is_switch(struct rio_dev *rdev);
extern int rio_get_err_and_status(struct rio_dev *rdev,
				  int portnum, u32 *err_status);
extern int rio_mport_get_feature(struct rio_mport *port,
				 int local, u16 destid,
				 u8 hopcount, int ftr, u32 *feature);
extern int rio_mport_get_physefb(struct rio_mport *port, int local,
				 u16 destid, u8 hopcount, u32 *physefb);
extern int rio_mport_get_efb(struct rio_mport *port, int local, u16 destid,
			     u8 hopcount, u32 from, u32 *ext_ftr_ptr);
extern int rio_mport_chk_dev_access(struct rio_mport *mport, u16 destid,
				    u8 hopcount);
extern int rio_create_sysfs_dev_files(struct rio_dev *rdev);
extern void rio_remove_sysfs_dev_files(struct rio_dev *rdev);
extern int rio_sysfs_init(struct rio_mport *mport);
extern int rio_std_route_add_entry(struct rio_mport *mport, u16 destid,
				   u8 hopcount, u16 table, u16 route_destid,
				   u8 route_port);
extern int rio_std_route_get_entry(struct rio_mport *mport, u16 destid,
				   u8 hopcount, u16 table, u16 route_destid,
				   u8 *route_port);
extern int rio_std_route_clr_table(struct rio_mport *mport, u16 destid,
				   u8 hopcount, u16 table);
extern struct rio_dev *rio_get_comptag(struct rio_mport *mport, u32 comp_tag);
extern int rio_clr_err_stopped(struct rio_dev *rdev, u32 pnum,
			       u32 err_status, int *success);
extern struct rio_net *rio_get_mport_net(struct rio_mport *port);
/* extern int rio_setup_event(struct rio_dev *rdev, int portnum, int event);*/

/* Structures internal to the RIO core code */
extern struct list_head rio_switches;
extern struct list_head rio_mports;
extern struct device_attribute rio_dev_attrs[];
extern spinlock_t rio_global_list_lock;

extern struct rio_switch_ops __start_rio_switch_ops[];
extern struct rio_switch_ops __end_rio_switch_ops[];

extern struct rio_dev_fixup __start_rio_dev_fixup_early[];
extern struct rio_dev_fixup __end_rio_dev_fixup_early[];
extern struct rio_dev_fixup __start_rio_dev_fixup_enable[];
extern struct rio_dev_fixup __end_rio_dev_fixup_enable[];


/* Helpers internal to the RIO core code */
#define DECLARE_RIO_SWITCH_SECTION(section, name, vid, did, init_hook) \
	static const struct rio_switch_ops __rio_switch_##name __used \
	__section(section) = { vid, did, init_hook };

/**
 * DECLARE_RIO_SWITCH_INIT - Registers switch initialization routine
 * @vid: RIO vendor ID
 * @did: RIO device ID
 * @init_hook: Callback that performs switch-specific initialization
 *
 * Manipulating switch route tables and error management in RIO
 * is switch specific. This registers a switch by vendor and device ID with
 * initialization callback for setting up switch operations and (if required)
 * hardware initialization. A &struct rio_switch_ops is initialized with
 * pointer to the init routine and placed into a RIO-specific kernel section.
 */
#define DECLARE_RIO_SWITCH_INIT(vid, did, init_hook)		\
	DECLARE_RIO_SWITCH_SECTION(.rio_switch_ops, vid##did, \
			vid, did, init_hook)

#define RIO_GET_DID(size, x)	(size ? (x & 0xffff) : \
					((x & 0x00ff0000) >> 16))
#define RIO_SET_DID(size, x)	(size ? (x & 0xffff) : \
					((x & 0x000000ff) << 16))

/* ------------------------------------------------------------------------- */
/*
 * Patch Mechanism
 *
 * The following macros may be defined in the processor- or board-specific
 * patch files to modify the operation of the generic RapidIO driver
 * software.  If they are not defined, then the default operation will be
 * performed.
 */

#ifndef RAPIDIO_REDUNDANT_PATH_LOCK_FAULT
	#define RAPIDIO_REDUNDANT_PATH_LOCK_FAULT()
#endif
#ifndef RAPIDIO_HW_LOCK_LOCK_ERR
	#define RAPIDIO_HW_LOCK_LOCK_ERR()
#endif
#ifndef RAPIDIO_HW_UNLOCK_LOCK_ERR
	#define RAPIDIO_HW_UNLOCK_LOCK_ERR()
#endif

/* ------------------------------------------------------------------------- */

#endif /* DRIVERS_RAPIDIO_RIO_H */
