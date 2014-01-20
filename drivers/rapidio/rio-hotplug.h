#ifndef RIO_HOTPLUG_H
#define RIO_HOTPLUG_H

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


#if defined(CONFIG_RAPIDIO_HOTPLUG)

extern int rio_create_newid_file(struct rio_driver *drv);
extern void rio_remove_newid_file(struct rio_driver *drv);
extern int rio_create_removeid_file(struct rio_driver *drv);
extern void rio_remove_removeid_file(struct rio_driver *drv);

#else

static inline int rio_create_newid_file(struct rio_driver *drv) { return 0; }
static inline void rio_remove_newid_file(struct rio_driver *drv) {}
static inline int rio_create_removeid_file(struct rio_driver *drv) { return 0; }
static inline void rio_remove_removeid_file(struct rio_driver *drv) {}

#endif

#endif
