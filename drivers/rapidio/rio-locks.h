#ifndef _RIO_LOCK_H
#define _RIO_LOCK_H

#include <linux/sched.h>

/*
 * RapidIO job support
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

extern struct rw_semaphore rio_tree_sem;

static inline void rio_tree_read_lock(void)
{
	down_read(&rio_tree_sem);
#if defined(RIO_LOCK_DEBUG)
	pr_debug("RIO: PID %u got the rio_tree_read_lock\n", current->pid);
#endif
}
static inline void rio_tree_read_unlock(void)
{
#if defined(RIO_LOCK_DEBUG)
	pr_debug("RIO: PID %u release the rio_tree_read_lock\n", current->pid);
#endif
	up_read(&rio_tree_sem);
}
static inline void rio_tree_write_lock(void)
{
	down_write(&rio_tree_sem);
#if defined(RIO_LOCK_DEBUG)
	pr_debug("RIO: PID %u got the rio_tree_write_lock\n", current->pid);
#endif
}
static inline void rio_tree_write_unlock(void)
{
#if defined(RIO_LOCK_DEBUG)
	pr_debug("RIO: PID %u release the rio_tree_write_lock\n",
		 current->pid);
#endif
	up_write(&rio_tree_sem);
}
extern struct rio_dev *lookup_rdev_next(struct rio_dev *rdev, int port_num);
extern struct rio_dev *rio_get_by_ptr(struct rio_dev *rdev);
extern int rio_get_host_lock(struct rio_mport *port, u16 destid, u8 hopcount,
			     u16 *result);
extern int rio_clear_host_lock(struct rio_mport *port, u16 destid,
			       u8 hopcount, u16 lockid);
extern int rio_hw_unlock(struct rio_mport *port, u16 destid, u8 hopcount);
extern void rio_job_hw_unlock_devices(struct rio_job *job);
extern int rio_hw_lock_busy_wait(struct rio_mport *port, u16 destid,
				 u8 hopcount, int wait_ms);
extern int rio_job_hw_lock_wait(struct rio_job *job, struct rio_dev *from,
				struct rio_dev *to,
				u16 destid, u8 hopcount, int tmo);
extern int rio_job_hw_lock_wait_cond(struct rio_job *job, u16 destid,
				     u8 hopcount, int tmo, int lock,
				     int (*unlock)(struct rio_mport *,
						   u16, u8));

extern int rio_hw_lock_wait(struct rio_mport *mport, u16 destid,
			    u8 hopcount, int tmo);
extern int rio_job_hw_wait_cond(struct rio_job *job, u16 destid,
				u8 hopcount, int tmo,
				int (*cond)(struct rio_mport *, u16, u8));
#endif
