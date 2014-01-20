/*
 * RapidIO device destination ID assingment support
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/rio_ids.h>
#include <linux/rio_regs.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/radix-tree.h>
#include <linux/hardirq.h>
#include <linux/err.h>

#include "rio.h"

struct rio_net_node {
	struct kref kref;
	u16 destid;
	u16 comptag;
	u8  lock_hw;
	u8  lut_update;
	u8  return_port;
	u8  pinned;
#if defined(CONFIG_RAPIDIO_STATIC_DESTID)
	struct radix_tree_root route_tree;
	atomic_t rio_route_num;
#endif
};

struct rio_net_id {
	struct list_head node;
	struct kref kref;
	u16 destid;
	u16 comptag;
	int net_id;
	int pinned;
};

static LIST_HEAD(rio_net);
static DEFINE_SPINLOCK(rio_net_lock);

#define RIO_DESTID_KEY(h, pp, pd) (		\
		(u8)((h) & 0xff) << 24 |	\
		(u8)((pp) & 0xff) << 16 |	\
		(u16)((pd) & 0xffff))

#define RIO_GET_PARENT_DEST(key) ((u16)((key) & 0xffff))
#define RIO_GET_PARENT_PORT(key) ((u8)(((key) & 0xff0000) >> 16))
#define RIO_GET_DEVICE_HOP(key) ((u8)(((key) & 0xff000000) >> 24))

#define RIO_DESTID_ONE_WAY_TAG (0)
#define RIO_DESTID_LEGACY_TAG (1)
#define RIO_DESTID_REDUNDANT_TAG (2)

struct rio_dest {
	u16 destid;
	u16 comptag;
	u16 flags;
	u16 return_port;
};

#define RIO_HW_LOCK_ENABLE    (1)
#define RIO_UPDATE_LUT_ENABLE (1 << 1)
#define RIO_ONE_WAY_ENABLE    (1 << 2)
#define RIO_LEGACY_ENABLE     (1 << 3)
#define RIO_REDUNDANT_ENABLE  (1 << 4)
#define RIO_DEFAULT_FLAGS     ((u16)(RIO_HW_LOCK_ENABLE | \
				RIO_UPDATE_LUT_ENABLE))

#define RIO_FLAG_GET(p, flag) (((p)->flags & flag) ? 1 : 0)
#define RIO_FLAG_ADD(p, flag) ((p)->flags |= (u16)(flag))
#define RIO_DEF_FLAGS_SET(p)  ((p)->flags = RIO_DEFAULT_FLAGS)

#define WARN_MSG \
	"Operation aborted - Node destid tables are only probed during boot\n"

/**
 * RIO destid internal
 */
static void rio_net_release(struct kref *kref)
{
	struct rio_net_id *net = container_of(kref, struct rio_net_id, kref);

	pr_info("RIO: kfree net id %d\n", net->net_id);
	kfree(net);
}

static struct rio_net_id *rio_net_get(struct rio_net_id *net)
{
	if (net)
		kref_get(&net->kref);

	return net;
}

static void rio_net_put(struct rio_net_id *net)
{
	if (net)
		kref_put(&net->kref, rio_net_release);
}

static int __rio_add_netid(u16 mport_destid, int net_id, u16 comptag)
{
	struct rio_net_id *net = kzalloc(sizeof(*net), GFP_KERNEL);

	if (!net)
		return -ENOMEM;

	net->destid = mport_destid;
	net->net_id = net_id;
	INIT_LIST_HEAD(&net->node);
	kref_init(&net->kref);
	net->pinned = 0;
	spin_lock(&rio_net_lock);
	list_add_tail(&net->node, &rio_net);
	spin_unlock(&rio_net_lock);

	return 0;
}
static int __rio_remove_netid(u16 mport_destid, int net_id)
{
	struct rio_net_id *net, *next;
	int rc = -ENODEV;

	spin_lock(&rio_net_lock);
	list_for_each_entry_safe(net, next, &rio_net, node) {
		if (net->destid == mport_destid && net->net_id == net_id) {
			if (!net->pinned) {
				pr_info("RIO: removing net id %d\n",
					net->net_id);
				list_del_init(&net->node);
				rio_net_put(net);
				rc = 0;
			} else {
				pr_warn("RIO: Not removing Net id %d -in use\n",
					net->net_id);
				rc = -EBUSY;
			}
			goto done;
		}
	}
done:
	spin_unlock(&rio_net_lock);
	return rc;
}

static struct rio_net_id *find_rio_net_id(u16 mport_destid,
					  struct rio_net_id *from)
{
	struct rio_net_id *net;
	struct list_head *n;

	spin_lock(&rio_net_lock);

	n = from ? from->node.next : rio_net.next;

	while (n && (n != &rio_net)) {
		net = list_entry(n, struct rio_net_id, node);
		if (net->destid == mport_destid)
			goto exit;
		n = n->next;
	}
	net = NULL;
exit:
	rio_net_put(from);
	net = rio_net_get(net);
	spin_unlock(&rio_net_lock);
	return net;
}

int rio_pin_netid(u16 host_deviceid, int net_id)
{
	struct rio_net_id *net, *next;

	spin_lock(&rio_net_lock);
	list_for_each_entry_safe(net, next, &rio_net, node) {
		if (net->destid == host_deviceid && net->net_id == net_id) {
			net->pinned++;
			pr_info("RIO: pinn net id %d\n", net->net_id);
			spin_unlock(&rio_net_lock);
			return 0;
		}
	}
	spin_unlock(&rio_net_lock);
	return -ENODEV;
}

int rio_unlock_netid(u16 host_deviceid, int net_id)
{
	struct rio_net_id *net, *next;

	spin_lock(&rio_net_lock);
	list_for_each_entry_safe(net, next, &rio_net, node) {
		if (net->destid == host_deviceid && net->net_id == net_id) {
			net->pinned--;
			pr_info("RIO: unlocknet id %d\n", net->net_id);
			spin_unlock(&rio_net_lock);
			return 0;
		}
	}
	spin_unlock(&rio_net_lock);
	return -ENODEV;
}

static void rio_node_release(struct kref *kref)
{
	struct rio_net_node *node = container_of(kref,
						 struct rio_net_node,
						 kref);
	kfree(node);
}

static struct rio_net_node *rio_node_get(struct rio_net_node *node)
{
	if (node)
		kref_get(&node->kref);

	return node;
}

static void rio_node_put(struct rio_net_node *node)
{
	if (node)
		kref_put(&node->kref, rio_node_release);
}

static int __rio_add_destid(struct rio_mport *mport,
			    u16 parent_destid, int parent_port,
			    int hopcount, struct rio_dest *dest)
{
	unsigned long key = RIO_DESTID_KEY(hopcount,
					   parent_port,
					   parent_destid);
	struct rio_net_node *node = NULL;
	int rc;

	rcu_read_lock();
	node = radix_tree_lookup(&mport->net.dst_tree, key);
	rcu_read_unlock();
	if (node)
		return -EBUSY;
	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	node->destid = dest->destid;
	node->comptag = dest->comptag;
	if (RIO_FLAG_GET(dest, RIO_ONE_WAY_ENABLE))
		node->return_port = dest->return_port;
	if (RIO_FLAG_GET(dest, RIO_HW_LOCK_ENABLE))
		node->lock_hw = 1;
	if (RIO_FLAG_GET(dest, RIO_UPDATE_LUT_ENABLE))
		node->lut_update = 1;
#if defined(CONFIG_RAPIDIO_STATIC_DESTID)
	INIT_RADIX_TREE(&node->route_tree, GFP_KERNEL);
	atomic_set(&node->rio_route_num, 0);
#endif
	kref_init(&node->kref);
	spin_lock(&mport->net.tree_lock);
	rc = radix_tree_insert(&mport->net.dst_tree, key, node);
	if (rc) {
		rio_node_put(node);
	} else {
		struct rio_net_node *tmp = NULL;

		if (RIO_FLAG_GET(dest, RIO_LEGACY_ENABLE)) {
			tmp = radix_tree_tag_set(&mport->net.dst_tree,
						 key, RIO_DESTID_LEGACY_TAG);
			BUG_ON(tmp != node);
		}
		if (RIO_FLAG_GET(dest, RIO_REDUNDANT_ENABLE)) {
			tmp = radix_tree_tag_set(&mport->net.dst_tree,
						 key, RIO_DESTID_REDUNDANT_TAG);
			BUG_ON(tmp != node);
		}
		if (RIO_FLAG_GET(dest, RIO_ONE_WAY_ENABLE)) {
			tmp = radix_tree_tag_set(&mport->net.dst_tree,
						 key, RIO_DESTID_ONE_WAY_TAG);
			BUG_ON(tmp != node);
		}
		atomic_inc(&mport->net.rio_dst_num);
		if (dest->destid > atomic_read(&mport->net.rio_max_dest))
			atomic_set(&mport->net.rio_max_dest, dest->destid);
	}
	spin_unlock(&mport->net.tree_lock);
	return rc;
}

static struct rio_net_node *rio_get_net_node(struct rio_mport *mport,
					     u16 parent_destid,
					     int parent_port,
					     int hopcount)
{
	struct rio_net_node *node = NULL;
	unsigned long key = RIO_DESTID_KEY(hopcount,
					   parent_port,
					   parent_destid);

	rcu_read_lock();
	node = radix_tree_lookup(&mport->net.dst_tree, key);
	if (node)
		node = rio_node_get(node);
	rcu_read_unlock();
	return node;
}

static int get_destid_tag(struct rio_mport *mport, u16 parent_destid,
			  int parent_port, int hopcount, unsigned int tag)
{
	unsigned long key = RIO_DESTID_KEY(hopcount,
					   parent_port,
					   parent_destid);
	int set;

	rcu_read_lock();
	set = radix_tree_tag_get(&mport->net.dst_tree, key, tag);
	rcu_read_unlock();

	return set;
}

#if defined(CONFIG_RAPIDIO_STATIC_DESTID)

static int __remove_static_routes_for_node(struct rio_mport *mport,
					   struct rio_net_node *node, int items)
{
	unsigned long *keys = NULL;
	void **nptr = NULL;
	int i, num, rc = -ENOMEM;

	if (items <= 0)
		return 0;

	keys = kmalloc(sizeof(*keys) * items, GFP_KERNEL);
	if (!keys)
		goto done_keys;

	nptr = kzalloc(sizeof(void *) * items, GFP_KERNEL);
	if (!nptr)
		goto done_nptr;

	spin_lock(&mport->net.tree_lock);

	num = radix_tree_gang_lookup_slot(&node->route_tree,
					  (void ***)nptr,
					  keys, 0, items);
	for (i = 0; i < num; i++) {
		u8 *curr_port = radix_tree_deref_slot((void **)nptr[i]);

		if (unlikely(!curr_port))
			continue;

		curr_port = radix_tree_delete(&node->route_tree, keys[i]);
		atomic_dec(&node->rio_route_num);
	}
	spin_unlock(&mport->net.tree_lock);

	synchronize_rcu();

	rc = (atomic_read(&node->rio_route_num) == 0 ? 0 : -EFAULT);

	kfree(nptr);
done_nptr:
	kfree(keys);
done_keys:
	if (rc)
		pr_warn("RIO: (%s) destid %hx rc %d\n",
			__func__, node->destid, rc);
	return rc;
}
#endif

static int __rio_release_destid(struct rio_mport *mport, u16 parent_destid,
				int parent_port, int hopcount)
{
	struct rio_net_node *node = NULL;
	unsigned long key = RIO_DESTID_KEY(hopcount,
					   parent_port,
					   parent_destid);

	spin_lock(&mport->net.tree_lock);
	node = radix_tree_lookup(&mport->net.dst_tree, key);
	if (node && node->pinned) {
		spin_unlock(&mport->net.tree_lock);
		return -EBUSY;
	}
	node = radix_tree_delete(&mport->net.dst_tree, key);
	spin_unlock(&mport->net.tree_lock);
	if (node) {
		synchronize_rcu();
#if defined(CONFIG_RAPIDIO_STATIC_DESTID)
		/* remove static routes if added */
		__remove_static_routes_for_node(mport, node,
					     atomic_read(&node->rio_route_num));
#endif
		rio_node_put(node);
		atomic_dec(&mport->net.rio_dst_num);
	}
	return 0;
}

int rio_pin_destid(struct rio_mport *mport, u16 parent_destid, int parent_port,
		   int hopcount)
{
	struct rio_net_node *node = NULL;
	unsigned long key = RIO_DESTID_KEY(hopcount,
					   parent_port,
					   parent_destid);
	int rc = 0;

	spin_lock(&mport->net.tree_lock);
	node = radix_tree_lookup(&mport->net.dst_tree, key);
	if (node)
		node->pinned++;
	else
		rc = -ENODEV;
	spin_unlock(&mport->net.tree_lock);
	return rc;
}

int rio_unlock_destid(struct rio_mport *mport, u16 parent_destid,
		      int parent_port, int hopcount)
{
	struct rio_net_node *node = NULL;
	unsigned long key = RIO_DESTID_KEY(hopcount,
					   parent_port,
					   parent_destid);
	int rc = 0;

	spin_lock(&mport->net.tree_lock);
	node = radix_tree_lookup(&mport->net.dst_tree, key);
	if (node && node->pinned)
		node->pinned--;
	else
		rc = -ENODEV;
	spin_unlock(&mport->net.tree_lock);
	return rc;
}

#if defined(CONFIG_RAPIDIO_STATIC_DESTID)

int __rio_release_node_table(struct rio_mport *mport)
{
	int parent_port, hopcount;
	u16 parent_destid;
	struct rio_net_node *mp_node;
	int rc = 0;

	if (!mport)
		return -EINVAL;

	mp_node = rio_get_net_node(mport, -1, -1, -1);
	if (mp_node) {
		rc = __rio_release_destid(mport, -1, -1, -1);
		rio_node_put(mp_node);
		if (rc)
			return rc;
	}
	for (hopcount = 0; hopcount < 256; hopcount++) {
		for (parent_port = -1;; parent_port++) {

			if (parent_port > 20)
				break;
			for (parent_destid = 0;
			     parent_destid <=
				atomic_read(&mport->net.rio_max_dest);
			     parent_destid++) {

				struct rio_net_node *node =
					rio_get_net_node(mport,
							 parent_destid,
							 parent_port,
							 hopcount);
				if (node) {
					rc = __rio_release_destid(mport,
								  parent_destid,
								  parent_port,
								  hopcount);
					rio_node_put(node);
					if (rc)
						return rc;
				}
			}
		}
	}
	return rc;
}

struct rio_route {
	unsigned long key;
	u8  *port;
};

static int __lookup_static_route(struct rio_mport *mport,
				 unsigned long node_key,
				 struct rio_static_route *sroute,
				 int num)
{
	int rc = 0;
	struct rio_net_node *node = NULL;

	rcu_read_lock();
	node = radix_tree_lookup(&mport->net.dst_tree, node_key);
	if (node) {
		int i;
		for (i = 0; i < num; i++) {
			u8 *curr_port = NULL;
			unsigned long key = sroute[i].sw_destid;
			curr_port = radix_tree_lookup(&node->route_tree, key);
			if (curr_port)
				sroute[i].sw_port = *curr_port;
		}
	} else {
		rc = -ENODEV;
	}
	rcu_read_unlock();
	return rc;
}

static int __static_route_table(struct rio_mport *mport, unsigned long node_key,
				struct rio_static_route *sroute, int items)
{
	int rc = 0;
	unsigned long *keys = kmalloc(sizeof(*keys) * items, GFP_KERNEL);
	void **nptr = NULL;
	struct rio_net_node *node = NULL;

	if (!keys)
		return -ENOMEM;

	nptr = kzalloc(sizeof(void *) * items, GFP_KERNEL);
	if (!nptr) {
		kfree(keys);
		return -ENOMEM;
	}

	rcu_read_lock();
	node = radix_tree_lookup(&mport->net.dst_tree, node_key);
	if (node) {
		int i;
		int num;
retry:
		num = radix_tree_gang_lookup_slot(&node->route_tree,
						  (void ***)nptr,
						  keys, 0, items);
		for (i = 0; i < num; i++) {
			u8 *curr_port = radix_tree_deref_slot((void **)nptr[i]);

			if (unlikely(!curr_port)) {
				sroute[i].sw_destid = RIO_INVALID_DESTID;
				sroute[i].sw_port = RIO_INVALID_ROUTE;
				continue;
			}
			if (radix_tree_deref_retry(curr_port))
				goto retry;

			sroute[i].sw_destid = keys[i];
			sroute[i].sw_port = *curr_port;
		}
		for (; i < items; i++) {
			sroute[i].sw_destid = RIO_INVALID_DESTID;
			sroute[i].sw_port = RIO_INVALID_ROUTE;
		}
	} else
		rc = -ENODEV;

	rcu_read_unlock();

	kfree(keys);
	kfree(nptr);

	return rc;
}

static int __remove_static_route(struct rio_mport *mport,
				 unsigned long node_key,
				 struct rio_route *route)
{
	struct rio_net_node *node = NULL;
	int rc = -ENODEV;

	spin_lock(&mport->net.tree_lock);
	node = radix_tree_lookup(&mport->net.dst_tree, node_key);
	if (node) {
		if (node->pinned) {
			rc = -EBUSY;
			goto done;
		}
		while (route->key != RIO_INVALID_DESTID) {
			u8 *curr_port = NULL;
			curr_port = radix_tree_lookup(&node->route_tree,
						      route->key);
			if (curr_port) {
				curr_port = radix_tree_delete(&node->route_tree,
							      route->key);
				route->port = curr_port;
				atomic_dec(&node->rio_route_num);
			} else {
				rc = -ENODEV;
				goto done;
			}
			route++;
		}
	}
done:
	spin_unlock(&mport->net.tree_lock);
	return rc;
}

static int __add_static_route(struct rio_mport *mport, unsigned long node_key,
			      struct rio_route *route, int update)
{
	struct rio_net_node *node = NULL;
	int rc = -ENODEV;

	spin_lock(&mport->net.tree_lock);
	node = radix_tree_lookup(&mport->net.dst_tree, node_key);
	if (node) {
		if (node->pinned) {
			rc = -EBUSY;
			goto done;
		}
		while (route->port) {
			void **rp = NULL;
			u8 *curr_port = NULL;
			rp = radix_tree_lookup_slot(&node->route_tree,
						    route->key);
			if (rp) {
				if (update) {
					curr_port = radix_tree_deref_slot(rp);
					if (unlikely(!curr_port))
						goto next;
					radix_tree_replace_slot(rp,
								route->port);
					route->port = curr_port;
				} else {
					rc = -EBUSY;
					goto done;
				}
			} else {
				rc = radix_tree_insert(&node->route_tree,
						       route->key, route->port);
				if (rc)
					goto done;
				atomic_inc(&node->rio_route_num);
				route->port = NULL;
			}
next:
			route++;
		}
	}
done:
	spin_unlock(&mport->net.tree_lock);
	return rc;
}

static struct rio_route *__alloc_route_table(struct rio_static_route *route,
					     int num_routes, int add)
{
	struct rio_route *rp;
	int i;

	rp = kzalloc(sizeof(*rp) * (num_routes + 1), GFP_KERNEL);
	if (!rp)
		return ERR_PTR(-ENOMEM);
	for (i = 0; i < num_routes; i++) {
		if (add) {
			rp[i].port = kmalloc(sizeof(u8), GFP_KERNEL);
			if (!rp[i].port)
				goto cleanup;

			*rp[i].port = route[i].sw_port;
		} else
			rp[i].port = NULL;

		rp[i].key = route[i].sw_destid;
	}
	rp[i].port = NULL;
	rp[i].key = RIO_INVALID_DESTID;
	return rp;

cleanup:
	for (i = 0; i < num_routes; i++) {
		if (rp[i].port)
			kfree(rp[i].port);
	}
	kfree(rp);
	return ERR_PTR(-ENOMEM);
}

static int add_static_route(struct rio_mport *mport, u16 parent_destid,
			    int parent_port, int hopcount,
			    struct rio_static_route *route,
			    int num_routes, int update)
{
	struct rio_route *rp;
	int i, rc = -ENOMEM;

#if !defined(CONFIG_RAPIDIO_HOTPLUG)
	if (WARN(system_state != SYSTEM_BOOTING, WARN_MSG))
		return -EINVAL;
#endif
	BUG_ON(!mport || !route || !num_routes);

	rp = __alloc_route_table(route, num_routes, 1);
	if (IS_ERR(rp))
		return PTR_ERR(rp);

	rc = __add_static_route(mport,
				RIO_DESTID_KEY(hopcount,
					       parent_port,
					       parent_destid),
				&rp[0],
				update);
	if (update)
		synchronize_rcu();

	for (i = 0; i < num_routes; i++) {
		if (rp[i].port)
			kfree(rp[i].port);
	}
	kfree(rp);

	return rc;
}

int rio_remove_static_route(struct rio_mport *mport, u16 parent_destid,
			int parent_port, int hopcount,
			struct rio_static_route *route, int num_routes)
{
	struct rio_route *rp;
	int i, rc = -ENOMEM;

#if !defined(CONFIG_RAPIDIO_HOTPLUG)
	if (WARN(system_state != SYSTEM_BOOTING, WARN_MSG))
		return -EINVAL;
#endif
	BUG_ON(!mport || !route || !num_routes);

	rp = __alloc_route_table(route, num_routes, 0);
	if (IS_ERR(rp))
		return PTR_ERR(rp);

	rc = __remove_static_route(mport,
				   RIO_DESTID_KEY(hopcount,
						  parent_port,
						  parent_destid),
				   &rp[0]);
	synchronize_rcu();

	for (i = 0; i < num_routes; i++) {
		if (rp[i].port) {
			pr_debug("RIO: removed %lx %hhu from static route table\n",
				 rp[i].key, *rp[i].port);
			kfree(rp[i].port);
		}
	}
	kfree(rp);

	return rc;
}

EXPORT_SYMBOL(rio_remove_static_route);

int rio_add_static_route(struct rio_mport *mport, u16 parent_destid,
			 int parent_port, int hopcount,
			 struct rio_static_route *route, int num_routes)
{
	return add_static_route(mport, parent_destid, parent_port,
				hopcount, route, num_routes, 0);
}

EXPORT_SYMBOL(rio_add_static_route);

int rio_update_static_route(struct rio_mport *mport, u16 parent_destid,
			    int parent_port, int hopcount,
			    struct rio_static_route *route, int num_routes)
{
	return add_static_route(mport, parent_destid, parent_port,
				hopcount, route, num_routes, 1);
}

EXPORT_SYMBOL(rio_update_static_route);

int rio_lookup_static_route(struct rio_dev *rdev, u16 sw_dest, u8 *route_port)
{
	struct rio_static_route sroute = { sw_dest, RIO_INVALID_ROUTE };
	int rc;

	rc = __lookup_static_route(rdev->hport,
				   RIO_DESTID_KEY(rdev->hopcount,
						  rdev->prev_port,
						  rdev->prev_destid),
				   &sroute, 1);

	*route_port = sroute.sw_port;
	return rc;
}

EXPORT_SYMBOL(rio_lookup_static_route);
int rio_lookup_static_routes(struct rio_mport *mport, u16 parent_destid,
			     int parent_port, int hopcount,
			     struct rio_static_route *sroute, int num_routes)
{
	return __lookup_static_route(mport,
				     RIO_DESTID_KEY(hopcount,
						    parent_port,
						    parent_destid),
				     sroute, num_routes);
}

EXPORT_SYMBOL(rio_lookup_static_routes);

struct rio_static_route *rio_static_route_table(struct rio_mport *mport,
						u16 parent_destid,
						int parent_port, int hopcount,
						u16 *destid,
						int *n)
{
	struct rio_net_node *node = rio_get_net_node(mport, parent_destid,
						     parent_port, hopcount);
	struct rio_static_route *sroute = NULL;
	int num = (node ? atomic_read(&node->rio_route_num) : 0);
	int rc;

	if (!node)
		return ERR_PTR(-ENODEV);

	*destid = node->destid;
	*n = num;
	rio_node_put(node);
	if (!num)
		return NULL;

	sroute = kzalloc(sizeof(*sroute) * num, GFP_KERNEL);
	if (!sroute)
		return ERR_PTR(-ENOMEM);

	rc = __static_route_table(mport,
				  RIO_DESTID_KEY(hopcount,
						 parent_port,
						 parent_destid),
				  sroute, num);
	if (rc) {
		kfree(sroute);
		return ERR_PTR(rc);
	}
	return sroute;
}

EXPORT_SYMBOL(rio_static_route_table);

/**
 * RIO static destid support - Exported KAPI
 */

int rio_get_next_destid(struct rio_mport *mport, u16 parent_destid,
			int port_num, u8 hopcount, u16 *id, int *comptag)
{
	struct rio_net_node *node = NULL;

	node = rio_get_net_node(mport, parent_destid, port_num, hopcount);

	if (node) {
		pr_debug("Assign destid %4.4x to device\n"
			 "At hopcount %u port %d parent destid %4.4x\n",
			 node->destid, hopcount,
			 port_num, parent_destid);
		*comptag = node->comptag;
		*id = node->destid;
		rio_node_put(node);
		return 0;
	}
	return -EINVAL;
}

int rio_get_next_netid(u16 host_deviceid, int *net_id, int *comptag)
{
	struct rio_net_id *net = NULL;

	net = find_rio_net_id(host_deviceid, net);

	if (net) {
		pr_debug("Assign DUS net_id %d to mport %4.4x net\n",
			 net->net_id, host_deviceid);
		*net_id = net->net_id;
		*comptag = net->comptag;
		rio_net_put(net);
		return 0;
	}
	return -EINVAL;
}

#else

/** Provide dynamic destid assignment **/

static int next_destid = 0;
static int next_netid = 0;

int rio_get_next_destid(struct rio_mport *mport, u16 parent_destid,
			int port_num, u8 hopcount, u16 *id, int *comptag)
{
	struct rio_net_node *node = NULL;
	int rc = 0;

	node = rio_get_net_node(mport, parent_destid, port_num, hopcount);

	if (node) {
		*comptag = node->comptag;
		*id = node->destid;
		rio_node_put(node);
	} else {
		struct rio_dest dest = {0};
		if (next_destid == mport->host_deviceid)
			next_destid++;
		RIO_DEF_FLAGS_SET(&dest);
		dest.destid = next_destid;
		dest.comptag = next_destid;

		rc = __rio_add_destid(mport, parent_destid, port_num,
				      hopcount, &dest);
		if (rc)
			goto done;
		*comptag = next_destid;
		*id = next_destid;
		next_destid++;
	}
	pr_debug("Assign destid %4.4x to device\n"
		 "At hopcount %u port %d parent destid %4.4x\n",
		 *id, hopcount, port_num, parent_destid);

done:
	return rc;
}

int rio_get_next_netid(u16 host_deviceid, int *net_id, int *comptag)
{
	struct rio_net_id *net = NULL;
	int rc = 0;

	net = find_rio_net_id(host_deviceid, net);

	if (net) {
		*net_id = net->net_id;
		*comptag = net->comptag;
		rio_net_put(net);
	} else {
		rc = __rio_add_netid(host_deviceid, next_netid, next_destid);
		if (rc)
			goto done;
		*net_id = next_netid;
		*comptag = next_destid;
		next_netid++;
		next_destid++;
	}
	pr_debug("Assign net_id %d to mport %4.4x net\n",
		 *net_id, host_deviceid);

done:
	return rc;
}
#endif


#if defined(CONFIG_RAPIDIO_HOTPLUG) || defined(CONFIG_RAPIDIO_STATIC_DESTID)

static int dump_node(char *buf, struct rio_mport *mport,
		     struct rio_net_node *node, unsigned long key)
{
	char *str = buf;
	int rd_tag, st_tag, ow_tag;

	rcu_read_lock();
	rd_tag = radix_tree_tag_get(&mport->net.dst_tree, key,
				    RIO_DESTID_REDUNDANT_TAG);
	st_tag = radix_tree_tag_get(&mport->net.dst_tree, key,
				    RIO_DESTID_LEGACY_TAG);
	ow_tag = radix_tree_tag_get(&mport->net.dst_tree, key,
				    RIO_DESTID_ONE_WAY_TAG);
	rcu_read_unlock();
	str += sprintf(str,
		       "%4.4x\t%d\t%d\t%4.4x\t%4.4x\t%s",
		       RIO_GET_PARENT_DEST(key),
		       RIO_GET_PARENT_PORT(key),
		       RIO_GET_DEVICE_HOP(key),
		       node->destid,
		       node->comptag,
		       (rd_tag ? "BLOCKED" : \
			(st_tag ? "LEGACY" : \
			 (ow_tag ? "ONE-WAY" : "DEFAULT"))));
	if (st_tag)
		str += sprintf(str,
			       "\t(lock=%d, lut_update=%d)",
			       node->lock_hw,
			       node->lut_update);
	if (ow_tag)
		str += sprintf(str,
			       "\t(ret_port=%d)",
			       node->return_port);
	str += sprintf(str, "\n");

	return str - buf;
}

static ssize_t __rio_net_nodes_show(struct rio_mport *mport, char *buf)
{
	char *str = buf;
	int parent_port, hopcount;
	u16 parent_destid;
	struct rio_net_node *mp_node;

	if (atomic_read(&mport->net.rio_dst_num))
		str += sprintf(str,
			       "parent\tparent\n"
			       "id\tport\thop\tdestid\tcomptag\ttag\n");

	mp_node = rio_get_net_node(mport, -1, -1, -1);
	if (mp_node) {
		str += dump_node(str, mport, mp_node,
				 RIO_DESTID_KEY(-1, -1, -1));
		rio_node_put(mp_node);
	}
	for (hopcount = 0; hopcount < 256; hopcount++) {
		for (parent_port = -1;; parent_port++) {

			if (parent_port > 20)
				break;
			for (parent_destid = 0;
			     parent_destid <=
				 atomic_read(&mport->net.rio_max_dest);
			     parent_destid++) {

				struct rio_net_node *node =
					rio_get_net_node(mport,
							 parent_destid,
							 parent_port,
							 hopcount);
				if (node) {
					str +=
					  dump_node(str, mport, node,
						 RIO_DESTID_KEY(hopcount,
								parent_port,
								parent_destid));
					rio_node_put(node);
				}
			}
		}
	}
	return str - buf;
}

#endif


int rio_destid_sysfs_init(struct rio_mport *mport)
{
/*	return sysfs_create_group(&mport->dev.kobj, &rio_attribute_group); */
	return 0;
}


int rio_lookup_next_destid(struct rio_mport *mport, u16 parent_destid,
			   int port_num, u8 hopcount, u16 *id)
{
	struct rio_net_node *node = NULL;

	node = rio_get_net_node(mport, parent_destid, port_num, hopcount);

	if (node) {
		*id = node->destid;
		rio_node_put(node);
		return 0;
	}
	return -ENODEV;
}
EXPORT_SYMBOL(rio_lookup_next_destid);

int rio_dest_is_redundant(struct rio_mport *mport, u16 parent_destid,
			  int parent_port, int hopcount)
{
	return get_destid_tag(mport, parent_destid, parent_port, hopcount,
			      RIO_DESTID_REDUNDANT_TAG);
}

int rio_dest_is_legacy(struct rio_mport *mport, u16 parent_destid,
		       int parent_port,
		       int hopcount)
{
	return get_destid_tag(mport, parent_destid, parent_port, hopcount,
			      RIO_DESTID_LEGACY_TAG);
}

int rio_dest_is_one_way(struct rio_mport *mport, u16 parent_destid,
			int parent_port,
		       int hopcount)
{
	return get_destid_tag(mport, parent_destid, parent_port, hopcount,
			      RIO_DESTID_ONE_WAY_TAG);
}

int rio_get_return_port(struct rio_mport *mport, u16 parent_destid,
			int parent_port,
			int hopcount, u8 *rport)
{
	struct rio_net_node *node = NULL;

	node = rio_get_net_node(mport, parent_destid, parent_port, hopcount);

	if (node) {
		*rport = node->return_port;
		rio_node_put(node);
		return 0;
	}
	return -ENODEV;
}

int rio_get_legacy_properties(struct rio_mport *mport, u16 parent_destid,
			      int parent_port, int hopcount, u8 *lock_hw,
			      u8 *lut_update)
{
	struct rio_net_node *node = NULL;

	node = rio_get_net_node(mport, parent_destid, parent_port, hopcount);

	if (node) {
		*lock_hw = node->lock_hw;
		*lut_update = node->lut_update;
		rio_node_put(node);
		return 0;
	}
	return -ENODEV;
}

int rio_eval_destid(struct rio_dev *new_rdev)
{
	struct rio_dev *rdev = NULL;
	int rc = 0;

	rcu_read_lock();
	rdev = radix_tree_lookup(&new_rdev->hport->net.dev_tree,
				 new_rdev->destid);
	if (rdev) {
		pr_warn("RIO: Duplicate DestID %hx found - New device is not added\n"
			"Possible causes:"
			"- You have found a BUG in the rio driver\n"
			"- You made a mistake when defining destid tables?\n",
			rdev->destid);
		rc = -EBUSY;
	}
	rcu_read_unlock();

	return rc;
}

/**
 * rio_get_destid - Get the base/extended device id for a device
 * @port: RIO master port
 * @destid: Destination ID of device
 * @hopcount: Hopcount to device
 *
 * Reads the base/extended device id from a device.
 * Returns success or failure.
 */
int rio_get_destid(struct rio_mport *mport, u16 destid,
		   u8 hopcount, u16 *res_destid)
{
	int rc = 0;
	u32 result;

	rc = rio_mport_read_config_32(mport, destid, hopcount,
				      RIO_DID_CSR, &result);
	*res_destid = RIO_GET_DID(mport->sys_size, result);
	return rc;
}

/**
 * rio_read_comptag - Get the comptag for a device
 * @port: RIO master port
 * @destid: Destination ID of device
 * @hopcount: Hopcount to device
 *
 * Reads the comptag from a device.
 * Returns success or failure.
 */
int rio_read_comptag(struct rio_mport *mport, u16 destid,
		     u8 hopcount, u32 *comptag)
{
	int rc = 0;
	u32 regval;

	rc = rio_mport_read_config_32(mport, destid, hopcount,
				      RIO_COMPONENT_TAG_CSR, &regval);
	*comptag = regval & 0xffff;

	return rc;
}

/**
 * rio_local_set_device_id - Set the base/extended device id for a port
 * @port: RIO master port
 * @did: Device ID value to be written
 *
 * Writes the base/extended device id from a device.
 */
int rio_set_master_destid(struct rio_mport *mport, u16 did)
{
	return rio_local_write_config_32(mport, RIO_DID_CSR,
					 RIO_SET_DID(mport->sys_size, did));
}

/**
 * rio_device_has_destid- Test if a device contains a destination ID register
 * @port: Master port to issue transaction
 * @src_ops: RIO device source operations
 * @dst_ops: RIO device destination operations
 *
 * Checks the provided @src_ops and @dst_ops for the necessary transaction
 * capabilities that indicate whether or not a device will implement a
 * destination ID register. Returns 1 if true or 0 if false.
 */
int rio_has_destid(int src_ops,
		   int dst_ops)
{
	u32 mask = RIO_OPS_READ | RIO_OPS_WRITE | RIO_OPS_ATOMIC_TST_SWP |
		   RIO_OPS_ATOMIC_INC | RIO_OPS_ATOMIC_DEC |
		   RIO_OPS_ATOMIC_SET | RIO_OPS_ATOMIC_CLR;

	return !!((src_ops | dst_ops) & mask);
}

/**
 * rio_set_device_id - Set the base/extended device id for a device
 * @port: RIO master port
 * @destid: Destination ID of device
 * @hopcount: Hopcount to device
 * @did: Device ID value to be written
 *
 * Writes the base/extended device id from a device.
 */
int rio_set_device_id(struct rio_mport *mport, u16 destid, u8 hopcount, u16 did)
{
	return rio_mport_write_config_32(mport, destid, hopcount, RIO_DID_CSR,
					 RIO_SET_DID(mport->sys_size, did));
}

int rio_assign_destid(struct rio_dev *rdev, struct rio_mport *mport, u16 destid,
		      int hopcount, u16 *id)
{
	int rc = 0;
	if (rio_is_switch(rdev)) {
		rdev->destid = rdev->comp_tag;
	} else {
		rc = rio_set_device_id(mport, destid, hopcount, *id);
		if (rc)
			goto done;
		rdev->destid = *id;
	}
done:
	return rc;
}

int rio_add_netid(u16 mport_destid, int net_id, int comptag)
{
#if !defined(CONFIG_RAPIDIO_HOTPLUG)
	if (WARN(system_state != SYSTEM_BOOTING, WARN_MSG))
		return -EINVAL;
#endif
	return __rio_add_netid(mport_destid, net_id, comptag);
}

#ifdef CONFIG_RAPIDIO_STATIC_DESTID
EXPORT_SYMBOL(rio_add_netid);
#endif

int rio_remove_netid(u16 mport_destid, int net_id)
{
#if !defined(CONFIG_RAPIDIO_HOTPLUG)
	if (WARN(system_state != SYSTEM_BOOTING, WARN_MSG))
		return -EINVAL;
#endif
	return __rio_remove_netid(mport_destid, net_id);
}

#ifdef CONFIG_RAPIDIO_STATIC_DESTID
EXPORT_SYMBOL(rio_remove_netid);
#endif

int rio_find_netid(u16 mport_destid, int *net_id)
{
	struct rio_net_id *net = NULL;

#if !defined(CONFIG_RAPIDIO_HOTPLUG)
	if (WARN(system_state != SYSTEM_BOOTING, WARN_MSG))
		return -EINVAL;
#endif
	net = find_rio_net_id(mport_destid, net);
	if (net) {
		*net_id = net->net_id;
		rio_net_put(net);
		return 0;
	} else {
		return -ENODEV;
	}
}

#ifdef CONFIG_RAPIDIO_STATIC_DESTID
EXPORT_SYMBOL(rio_find_netid);
#endif

/**
 * rio_add_destid - Add destid lookup entry with default properties
 *
 * @mport: Master port from which this device can be reached
 * @parent_destid: device ID of switch/master port that routes/connects
 *                 to this device
 * @parent_port: Switch port that shall be used in parent device to
 *               reach the device (-1 if parent is master port)
 * @hopcount: Number of hops, starting from @mport to reach this device
 * @destid: device ID that shall be assigned to this device
 * @comtag: device comptag that will be assigned to this device.
 *
 * This is a standard device meaning that:
 * - It is assumed that this device shall be used and that the same route
 *   shall be used for transmit to device as shall be used for transmit
 *   from device.
 * - If the device is a switch, the lut table will be configured with HW locks
 *   taken. The in-port will be used for return route setup. In single
 *   enumeration host mode, only the enum host will modify lut setup but
 *   discovery nodes will still claim the device HW lock while testing
 *   for enum completed flag. In multiple enumeration domain mode, discovery
 *   nodes will update the lut when adding domain return paths and they will
 *   do so with the HW lock taken.
 */

int rio_add_destid(struct rio_mport *mport,
		   u16 parent_destid, int parent_port,
		   int hopcount, u16 destid, u16 comptag)
{
	struct rio_dest dest = {0};

#if !defined(CONFIG_RAPIDIO_HOTPLUG)
	if (WARN(system_state != SYSTEM_BOOTING, WARN_MSG))
		return -EINVAL;
#endif
	RIO_DEF_FLAGS_SET(&dest);
	dest.destid = destid;
	dest.comptag = comptag;

	return __rio_add_destid(mport, parent_destid, parent_port,
				hopcount, &dest);
}

#ifdef CONFIG_RAPIDIO_STATIC_DESTID
EXPORT_SYMBOL(rio_add_destid);
#endif
/**
 * rio_block_destid_route - Add destid lookup entry which blocks routing
 *                          to device trough this path.
 *
 * @mport: Master port from which this device can be reached
 * @parent_destid: device ID of switch/master port that connects
 *                 to this device
 * @parent_port: Switch port that shall is used in parent device to
 *               reach the device.
 * @hopcount: Number of hops, starting from @mport to reach this device
 * @destid: device ID
 * @comtag: device comptag
 *
 * When blocking a route:
 *   It is assumed that this device shall not be used/or at least not
 *   be reachable trough this path. The device ID will not be added to
 *   the @parent_port in parent switch device lut.
 * NOTE:
 *   It is also assumed that the parent device is a switch and not the
 *   @mport itself.
 *   It is an error to add the first switch in a @mport net as a blocked
 *   device. If the master port shall not be used then the master port
 *   /first switch should not be enabled in the first place.
 */
int rio_block_destid_route(struct rio_mport *mport,
			   u16 parent_destid, int parent_port,
			   int hopcount, u16 destid, u16 comptag)
{
	struct rio_dest dest = {0};

#if !defined(CONFIG_RAPIDIO_HOTPLUG)
	if (WARN(system_state != SYSTEM_BOOTING, WARN_MSG))
		return -EINVAL;
#endif
	RIO_DEF_FLAGS_SET(&dest);
	RIO_FLAG_ADD(&dest, RIO_REDUNDANT_ENABLE);
	dest.destid = destid;
	dest.comptag = comptag;

	return __rio_add_destid(mport, parent_destid, parent_port,
				hopcount, &dest);
}

#ifdef CONFIG_RAPIDIO_STATIC_DESTID
EXPORT_SYMBOL(rio_block_destid_route);
#endif

/**
 * rio_split_destid_route - Add destid lookup entry with separat transmit
 *                          and return paths.
 *
 * @mport: Master port from which this device can be reached
 * @parent_destid: device ID of switch/master port that routes/connects
 *                 to this device
 * @parent_port: Switch port that shall be used in parent device to
 *               reach the device.
 * @hopcount: Number of hops, starting from @mport to reach this device
 * @destid: device ID that shall be assigned to this device
 * @comtag: device comptag that will be assigned to this device.
 * @return_port: port used in device for routing traffic back to parent device
 *
 * Split routes:
 *   If you have redundant connections between switches you may use
 *   use split routes to prevent network loops. In that case links are
 *   used in one-way mode and lut tables are setup so that one link,
 *   the @parent_port, is used for all traffic from the parent device
 *   to the @destid device, while all traffic from the @destid device to
 *   the parent device will use the @return_port.
 *
 *   All other properties, e.g. lut updates, hw_locking, etc. is handled
 *   in the same way as it is for standard devices.
 *
 * NOTE:
 * Adding a split route is only supported when both parent and destid
 * are switches.
 */
int rio_split_destid_route(struct rio_mport *mport,
			   u16 parent_destid, int parent_port,
			   int hopcount, u16 destid, u16 comptag,
			   u8 return_port)
{
	struct rio_dest dest = {0};

#if !defined(CONFIG_RAPIDIO_HOTPLUG)
	if (WARN(system_state != SYSTEM_BOOTING, WARN_MSG))
		return -EINVAL;
#endif
	RIO_DEF_FLAGS_SET(&dest);
	RIO_FLAG_ADD(&dest, RIO_ONE_WAY_ENABLE);
	dest.destid = destid;
	dest.comptag = comptag;
	dest.return_port = return_port;

	return __rio_add_destid(mport, parent_destid, parent_port,
				hopcount, &dest);
}

#ifdef CONFIG_RAPIDIO_STATIC_DESTID
EXPORT_SYMBOL(rio_split_destid_route);
#endif
/**
 * rio_legacy_destid_route - Add destid lookup entry for devices
 *                           reciding in domains handled by non-linux
 *                           hosts.
 *
 * @mport: Master port from which this device can be reached
 * @parent_destid: device ID of switch/master port that routes/connects
 *                 to this device
 * @parent_port: Switch port that shall be used in parent device to
 *               reach the device.
 * @hopcount: Number of hops, starting from @mport to reach this device
 * @destid: device ID
 * @comtag: device comptag
 * @lock_hw: HW locks may be taken on device
 * @lut_update: Device lut table may be updated
 *
 * Split routes:
 *   ???
 *
 * NOTE:
 *   ???
 */
int rio_legacy_destid_route(struct rio_mport *mport,
			    u16 parent_destid, int parent_port,
			    int hopcount, u16 destid, u16 comptag,
			    u8 lock_hw, u8 lut_update)
{
	struct rio_dest dest = {0};

#if !defined(CONFIG_RAPIDIO_HOTPLUG)
	if (WARN(system_state != SYSTEM_BOOTING, WARN_MSG))
		return -EINVAL;
#endif
	if (lock_hw)
		RIO_FLAG_ADD(&dest, RIO_HW_LOCK_ENABLE);
	if (lut_update)
		RIO_FLAG_ADD(&dest, RIO_UPDATE_LUT_ENABLE);

	RIO_FLAG_ADD(&dest, RIO_LEGACY_ENABLE);

	dest.destid = destid;
	dest.comptag = comptag;

	return __rio_add_destid(mport, parent_destid, parent_port,
				hopcount, &dest);
}

#ifdef CONFIG_RAPIDIO_STATIC_DESTID
EXPORT_SYMBOL(rio_legacy_destid_route);
#endif

void rio_release_destid(struct rio_mport *mport, u16 parent_destid,
			int parent_port, int hopcount)
{
#if !defined(CONFIG_RAPIDIO_HOTPLUG)
	if (WARN(system_state != SYSTEM_BOOTING, WARN_MSG))
		return;
#endif
	__rio_release_destid(mport, parent_destid, parent_port,
			     hopcount);
}

#ifdef CONFIG_RAPIDIO_STATIC_DESTID
EXPORT_SYMBOL(rio_release_destid);
#endif

#ifdef CONFIG_RAPIDIO_STATIC_DESTID
int rio_release_node_table(struct rio_mport *mport)
{
#if !defined(CONFIG_RAPIDIO_HOTPLUG)
	if (WARN(system_state != SYSTEM_BOOTING, WARN_MSG))
		return -EINVAL;
#endif

	return __rio_release_node_table(mport);
}

EXPORT_SYMBOL(rio_release_node_table);
#endif

#if defined(CONFIG_RAPIDIO_HOTPLUG) || defined(CONFIG_RAPIDIO_STATIC_DESTID)

/**
 * debug helper
 */
ssize_t rio_net_nodes_show(struct rio_mport *mport, char *buf)
{
	return __rio_net_nodes_show(mport, buf);
}
EXPORT_SYMBOL(rio_net_nodes_show);
#endif
