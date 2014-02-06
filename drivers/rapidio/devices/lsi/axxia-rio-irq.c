/*
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/dmapool.h>

#include <linux/io.h>
#include <linux/uaccess.h>

#include "axxia-rio.h"

/*
** Debug Build Flags
**/
/* #define STRICT_INB_ORDERING	0 */
/* #define AXM55XX_OUTB_DME_BBS 1 */


/*
** Local State
*/

atomic_t thrd_handler_calls = ATOMIC_INIT(0);
atomic_t hw_handler_calls = ATOMIC_INIT(0);
atomic_t port_irq_disabled = ATOMIC_INIT(0);
atomic_t port_irq_enabled = ATOMIC_INIT(0);

#if defined(CONFIG_AXXIA_RIO_STAT)

/**
 * __add_event_dbg -- Update port event counters
 *
 * @priv: Master port private data
 * @escsr: PortN Error and Status Command State register
 * @iecsr: PortN Implementation Error Command and status register
 */
static inline void __add_event_dbg(struct rio_priv *priv, u32 escsr, u32 iecsr)
{
	/* update stats debug info */
	if (escsr & RIO_ESCSR_OPD)
		atomic_inc(&priv->event[RIO_OPD]);
	if (escsr & RIO_ESCSR_OFE)
		atomic_inc(&priv->event[RIO_OFE]);
	if (escsr & RIO_ESCSR_ODE)
		atomic_inc(&priv->event[RIO_ODE]);
	if (escsr & RIO_ESCSR_ORE)
		atomic_inc(&priv->event[RIO_ORE]);
	if (escsr & RIO_ESCSR_OEE)
		atomic_inc(&priv->event[RIO_OEE]);
	if (escsr & RIO_ESCSR_IEE)
		atomic_inc(&priv->event[RIO_IEE]);
	if (escsr & RIO_ESCSR_PE)
		atomic_inc(&priv->event[RIO_PE]);
	if (iecsr & EPC_IECSR_RETE)
		atomic_inc(&priv->event[EPC_RETE]);
}

/**
 * __add_state_dbg -- Update port state
 *
 * @priv: Master port private data
 * @escsr: PortN Error and Status Command State register
 */
static inline void __add_state_dbg(struct rio_priv *priv, u32 escsr)
{
	/* update stats debug info */
	atomic_set(&priv->state[RIO_ORS], (escsr & RIO_ESCSR_ORS ? 1 : 0));
	atomic_set(&priv->state[RIO_OES], (escsr & RIO_ESCSR_OES ? 1 : 0));
	atomic_set(&priv->state[RIO_IRS], (escsr & RIO_ESCSR_IRS ? 1 : 0));
	atomic_set(&priv->state[RIO_IES], (escsr & RIO_ESCSR_IES ? 1 : 0));
	atomic_set(&priv->state[RIO_PO], (escsr & RIO_ESCSR_PO ? 1 : 0));
	atomic_set(&priv->state[RIO_PU], (escsr & RIO_ESCSR_PU ? 1 : 0));
}

static inline void __irq_dbg(struct rio_priv *priv, enum rio_irq_dbg id)
{
	atomic_inc(&priv->irq[id]);
}

static inline void __ib_dme_debug(struct rio_priv *priv,
				  int dme_no,
				  enum rio_ib_dme_dbg id)
{
	atomic_inc(&priv->ib_dme[dme_no][id]);
}

static inline void __ob_dme_debug(struct rio_priv *priv,
				  int dme_no,
				  enum rio_ob_dme_dbg id)
{
	atomic_inc(&priv->ob_dme[dme_no][id]);
}

static inline void __misc_fatal_dbg(struct rio_priv *priv, u32 misc_state,
				    u32 amast)
{
	if (misc_state & AMST_INT) {
		if (amast & RAB_AMAST_STAT_WRTO)
			__irq_dbg(priv, RIO_AMST_WRTO);
		if (amast & RAB_AMAST_STAT_RDTO)
			__irq_dbg(priv, RIO_AMST_RDTO);
		if (amast & RAB_AMAST_STAT_WRDE)
			__irq_dbg(priv, RIO_AMST_WRDE);
		if (amast & RAB_AMAST_STAT_WRSE)
			__irq_dbg(priv, RIO_AMST_WRSE);
		if (amast & RAB_AMAST_STAT_RDDE)
			__irq_dbg(priv, RIO_AMST_RDDE);
		if (amast & RAB_AMAST_STAT_RDSE)
			__irq_dbg(priv, RIO_AMST_RDSE);
	}
	if (misc_state & ASLV_INT)
		__irq_dbg(priv, RIO_MISC_ASLV);
}

static inline void __misc_info_dbg(struct rio_priv *priv, u32 misc_state)
{
	/* Log only - no enable bit or state to clear */
	if (misc_state & (UNEXP_MSG_LOG | UNEXP_MSG_INT |
			  LL_TL_INT | GRIO_INT |
			  UNSP_RIO_REQ_INT | LINK_REQ_INT)) {
		if (misc_state & UNEXP_MSG_INT)
			__irq_dbg(priv, RIO_MISC_UNEXP);
		if (misc_state & LL_TL_INT)
			__irq_dbg(priv, RIO_MISC_TL);
		if (misc_state & GRIO_INT)
			__irq_dbg(priv, RIO_MISC_GRIO);
		if (misc_state & UNSP_RIO_REQ_INT)
			__irq_dbg(priv, RIO_MISC_UNSUP);
		if (misc_state & LINK_REQ_INT)
			__irq_dbg(priv, RIO_MISC_LINK_REQ);
	}
}

static inline void __linkdown_dbg(struct rio_priv *priv, u32 misc_state)
{
	__irq_dbg(priv, RIO_LINKDOWN);
}

static inline void __ob_db_dbg(struct rio_priv *priv, struct rio_mport *mport)
{
	int db;
	u32 csr;

	for (db = 0; db < MAX_OB_DB; db++) {
		__rio_local_read_config_32(mport, RAB_OB_DB_CSR(db), &csr);

		if (OB_DB_STATUS(csr) == OB_DB_STATUS_DONE)
			__irq_dbg(priv, RIO_MISC_OB_DB_DONE);
		else if (OB_DB_STATUS(csr) == OB_DB_STATUS_RETRY)
			__irq_dbg(priv, RIO_MISC_OB_DB_RETRY);
		else if (OB_DB_STATUS(csr) == OB_DB_STATUS_ERROR)
			__irq_dbg(priv, RIO_MISC_OB_DB_ERROR);
		else if (OB_DB_STATUS(csr) == OB_DB_STATUS_TIMEOUT)
			__irq_dbg(priv, RIO_MISC_OB_DB_TO);
	}
}

static inline void __ob_dme_dbg(struct rio_priv *priv, u32 dme_stat)
{
	if (dme_stat & OB_DME_STAT_ERROR_MASK) {
		if (dme_stat & OB_DME_STAT_RESP_TO)
			__irq_dbg(priv, RIO_OB_DME_STAT_RESP_TO);
		if (dme_stat & OB_DME_STAT_RESP_ERR)
			__irq_dbg(priv, RIO_OB_DME_STAT_RESP_ERR);
		if (dme_stat & OB_DME_STAT_DATA_TRANS_ERR)
			__irq_dbg(priv, RIO_OB_DME_STAT_DATA_TRANS_ERR);
		if (dme_stat & OB_DME_STAT_DESC_UPD_ERR)
			__irq_dbg(priv, RIO_OB_DME_STAT_DESC_UPD_ERR);
		if (dme_stat & OB_DME_STAT_DESC_ERR)
			__irq_dbg(priv, RIO_OB_DME_STAT_DESC_ERR);
		if (dme_stat & OB_DME_STAT_DESC_FETCH_ERR)
			__irq_dbg(priv, RIO_OB_DME_STAT_DESC_FETCH_ERR);
	}
	if (dme_stat & OB_DME_STAT_SLEEPING)
		__irq_dbg(priv, RIO_OB_DME_STAT_SLEEPING);
	if (dme_stat & OB_DME_STAT_DESC_XFER_CPLT)
		__irq_dbg(priv, RIO_OB_DME_STAT_DESC_XFER_CPLT);
	if (dme_stat & OB_DME_STAT_DESC_CHAIN_XFER_CPLT)
		__irq_dbg(priv, RIO_OB_DME_STAT_DESC_CHAIN_XFER_CPLT);
	if (dme_stat & OB_DME_STAT_TRANS_PEND)
		__irq_dbg(priv, RIO_OB_DME_STAT_TRANS_PEND);

}

static inline void __ob_dme_dw_dbg(struct rio_priv *priv, u32 dw0)
{
	if (dw0 & DME_DESC_DW0_ERROR_MASK) {
		if (dw0 & DME_DESC_DW0_RIO_ERR)
			__irq_dbg(priv, RIO_OB_DME_DESC_DW0_RIO_ERR);
		if (dw0 & DME_DESC_DW0_AXI_ERR)
			__irq_dbg(priv, RIO_OB_DME_DESC_DW0_AXI_ERR);
		if (dw0 & DME_DESC_DW0_TIMEOUT_ERR)
			__irq_dbg(priv, RIO_OB_DME_DESC_DW0_TIMEOUT_ERR);
	}
	if (dw0 & DME_DESC_DW0_DONE)
		__irq_dbg(priv, RIO_OB_DME_DESC_DESC_DW0_DONE);
}

static inline void __ib_dme_dbg(struct rio_priv *priv, u32 dme_stat)
{
	if (dme_stat & IB_DME_STAT_ERROR_MASK) {
		if (dme_stat & IB_DME_STAT_MSG_TIMEOUT)
			__irq_dbg(priv, RIO_IB_DME_STAT_MSG_TIMEOUT);
		if (dme_stat & IB_DME_STAT_MSG_ERR)
			__irq_dbg(priv, RIO_IB_DME_STAT_MSG_ERR);
		if (dme_stat & IB_DME_STAT_DATA_TRANS_ERR)
			__irq_dbg(priv, RIO_IB_DME_STAT_DATA_TRANS_ERR);
		if (dme_stat & IB_DME_STAT_DESC_UPDATE_ERR)
			__irq_dbg(priv, RIO_IB_DME_STAT_DESC_UPDATE_ERR);
		if (dme_stat & IB_DME_STAT_DESC_ERR)
			__irq_dbg(priv, RIO_IB_DME_STAT_DESC_ERR);
		if (dme_stat & IB_DME_STAT_DESC_FETCH_ERR)
			__irq_dbg(priv, RIO_IB_DME_STAT_FETCH_ERR);
	}
	if (dme_stat & IB_DME_STAT_SLEEPING)
		__irq_dbg(priv, RIO_IB_DME_STAT_SLEEPING);
	if (dme_stat & IB_DME_STAT_DESC_XFER_CPLT)
		__irq_dbg(priv, RIO_IB_DME_STAT_DESC_XFER_CPLT);
	if (dme_stat & IB_DME_STAT_DESC_CHAIN_XFER_CPLT)
		__irq_dbg(priv, RIO_IB_DME_STAT_DESC_CHAIN_XFER_CPLT);
	if (dme_stat & IB_DME_STAT_TRANS_PEND)
		__irq_dbg(priv, RIO_IB_DME_STAT_TRANS_PEND);
}

static inline void __ib_dme_dw_dbg(struct rio_priv *priv, u32 dw0)
{
	if (dw0 & DME_DESC_DW0_ERROR_MASK) {
		if (dw0 & DME_DESC_DW0_RIO_ERR)
			__irq_dbg(priv, RIO_IB_DME_DESC_DW0_RIO_ERR);
		if (dw0 & DME_DESC_DW0_AXI_ERR)
			__irq_dbg(priv, RIO_IB_DME_DESC_DW0_AXI_ERR);
		if (dw0 & DME_DESC_DW0_TIMEOUT_ERR)
			__irq_dbg(priv, RIO_IB_DME_DESC_DW0_TIMEOUT_ERR);
	}
	if (dw0 & DME_DESC_DW0_DONE)
		__irq_dbg(priv, RIO_IB_DME_DESC_DESC_DW0_DONE);
}

static inline void __rpio_fail_dbg(struct rio_priv *priv, u32 rpio_stat)
{
	if (rpio_stat & RAB_RPIO_STAT_RSP_ERR)
		__irq_dbg(priv, RIO_PIO_RSP_ERR);
	if (rpio_stat & RAB_RPIO_STAT_ADDR_MAP)
		__irq_dbg(priv, RIO_PIO_ADDR_MAP);
	if (rpio_stat & RAB_RPIO_STAT_DISABLED)
		__irq_dbg(priv, RIO_PIO_DISABLED);
}

static inline void __apio_fail_dbg(struct rio_priv *priv, u32 apio_stat)
{
	if (apio_stat & RAB_APIO_STAT_RQ_ERR)
		__irq_dbg(priv, RIO_APIO_RQ_ERR);
	if (apio_stat & RAB_APIO_STAT_TO_ERR)
		__irq_dbg(priv, RIO_APIO_TO_ERR);
	if (apio_stat & RAB_APIO_STAT_RSP_ERR)
		__irq_dbg(priv, RIO_APIO_RSP_ERR);
	if (apio_stat & RAB_APIO_STAT_MAP_ERR)
		__irq_dbg(priv, RIO_APIO_MAP_ERR);
	if (apio_stat & RAB_APIO_STAT_MAINT_DIS)
		__irq_dbg(priv, RIO_APIO_MAINT_DIS);
	if (apio_stat & RAB_APIO_STAT_MEM_DIS)
		__irq_dbg(priv, RIO_APIO_MEM_DIS);
	if (apio_stat & RAB_APIO_STAT_DISABLED)
		__irq_dbg(priv, RIO_APIO_DISABLED);
}

static inline void __ib_dme_event_dbg(struct rio_priv *priv,
				      int dme, u32 ib_event)
{
	if (ib_event & (1 << RIO_IB_DME_RX_PUSH))
		__ib_dme_debug(priv, dme, RIO_IB_DME_RX_PUSH);
	if (ib_event & (1 << RIO_IB_DME_RX_POP))
		__ib_dme_debug(priv, dme, RIO_IB_DME_RX_POP);
	if (ib_event & (1 << RIO_IB_DME_DESC_ERR))
		__ib_dme_debug(priv, dme, RIO_IB_DME_DESC_ERR);
	if (ib_event & (1 << RIO_IB_DME_RX_VBUF_EMPTY))
		__ib_dme_debug(priv, dme, RIO_IB_DME_RX_VBUF_EMPTY);
	if (ib_event & (1 << RIO_IB_DME_RX_RING_FULL))
		__ib_dme_debug(priv, dme, RIO_IB_DME_RX_RING_FULL);
	if (ib_event & (1 << RIO_IB_DME_RX_PENDING_AT_SLEEP))
		__ib_dme_debug(priv, dme, RIO_IB_DME_RX_PENDING_AT_SLEEP);
	if (ib_event & (1 << RIO_IB_DME_RX_SLEEP))
		__ib_dme_debug(priv, dme, RIO_IB_DME_RX_SLEEP);
	if (ib_event & (1 << RIO_IB_DME_RX_WAKEUP))
		__ib_dme_debug(priv, dme, RIO_IB_DME_RX_WAKEUP);
}

static inline void __ob_dme_event_dbg(struct rio_priv *priv,
				      int dme, u32 ob_event)
{
	if (ob_event & (1 << RIO_OB_DME_TX_PUSH))
		__ob_dme_debug(priv, dme, RIO_OB_DME_TX_PUSH);
	if (ob_event & (1 << RIO_OB_DME_TX_POP))
		__ob_dme_debug(priv, dme, RIO_OB_DME_TX_POP);
	if (ob_event & (1 << RIO_OB_DME_TX_DESC_READY))
		__ob_dme_debug(priv, dme, RIO_OB_DME_TX_DESC_READY);
	if (ob_event & (1 << RIO_OB_DME_TX_PENDING))
		__ob_dme_debug(priv, dme, RIO_OB_DME_TX_PENDING);
	if (ob_event & (1 << RIO_OB_DME_TX_SLEEP))
		__ob_dme_debug(priv, dme, RIO_OB_DME_TX_SLEEP);
	if (ob_event & (1 << RIO_OB_DME_TX_WAKEUP))
		__ob_dme_debug(priv, dme, RIO_OB_DME_TX_WAKEUP);
	if (ob_event & (1 << RIO_OB_DME_TX_PUSH))
		__ob_dme_debug(priv, dme, RIO_OB_DME_TX_PUSH);
}

static void reset_state_counters(struct rio_priv *priv)
{
	int i;

	for (i = 0; i < RIO_STATE_NUM; i++)
		atomic_set(&priv->state[i], 0);
	for (i = 0; i < RIO_EVENT_NUM; i++)
		atomic_set(&priv->event[i], 0);
	for (i = 0; i < RIO_IRQ_NUM; i++)
		atomic_set(&priv->irq[i], 0);
}
#endif /* defined(CONFIG_AXXIA_RIO_STAT) */

/**
 * thrd_irq_handler - Threaded interrupt handler
 * @irq: Linux interrupt number
 * @data: Pointer to interrupt-specific data
 *
 */
static irqreturn_t thrd_irq_handler(int irq, void *data)
{
	struct rio_irq_handler *h = data;

	atomic_inc(&thrd_handler_calls);

#ifdef CONFIG_SRIO_IRQ_TIME
	if (atomic_read(&h->start_time))
		h->thrd_tb = get_tb();
#endif

	/**
	 * Invoke handler callback
	 */
	test_and_set_bit(RIO_IRQ_ACTIVE, &h->state);
	h->thrd_irq_fn(h, h->irq_state);
	clear_bit(RIO_IRQ_ACTIVE, &h->state);

	return IRQ_HANDLED;
}

/**
 * hw_irq_handler - RIO HW interrupt handler
 * @irq: Linux interrupt number
 * @data: Pointer to interrupt-specific data
 *
 */
static irqreturn_t hw_irq_handler(int irq, void *data)
{
	struct rio_irq_handler *h = data;
	struct rio_mport *mport = h->mport;

	atomic_inc(&hw_handler_calls);

	/**
	 * Get current interrupt state and clear latched state
	 * for interrupts handled by current thread.
	 */
	__rio_local_read_config_32(mport, h->irq_state_reg_addr, &h->irq_state);
	h->irq_state &= h->irq_state_mask;
	__rio_local_write_config_32(mport, h->irq_state_reg_addr, h->irq_state);

	if (h->irq_state & h->irq_state_mask) {
#ifdef CONFIG_SRIO_IRQ_TIME
		if (atomic_read(&h->start_time))
			h->irq_tb = get_tb();
#endif
		return IRQ_WAKE_THREAD;
	}

	return IRQ_NONE;
}

/**
 * Caller must hold RAB lock
 */
int alloc_irq_handler(struct rio_irq_handler *h,
		     void *data,
		     const char *name)
{
	struct rio_mport *mport = h->mport;
	struct rio_priv *priv = mport->priv;
	u32 mask;
	int rc;

	if (test_and_set_bit(RIO_IRQ_ENABLED,  &h->state))
		return -EBUSY;

#ifdef CONFIG_SRIO_IRQ_TIME
	atomic_set(&h->start_time, 0);
#endif
	h->data = data;
	rc = request_threaded_irq(priv->irq_line,
				  hw_irq_handler,
				  thrd_irq_handler,
				  IRQF_TRIGGER_NONE | IRQF_SHARED |
				   IRQF_ONESHOT,
				  name,
				  (void *)h);
	if (rc) {
		clear_bit(RIO_IRQ_ENABLED,  &h->state);
		h->data = NULL;
		return rc;
	}
	if (h->irq_enab_reg_addr) {
		__rio_local_read_config_32(mport, h->irq_enab_reg_addr, &mask);
		mask |= h->irq_state_mask;
		__rio_local_write_config_32(mport, h->irq_enab_reg_addr, mask);
	}
	if (h->irq_enab_reg_addr)
		__rio_local_write_config_32(mport, h->irq_state_reg_addr, mask);

	return rc;
}

/**
 * Caller must hold RAB lock
 */

void release_irq_handler(struct rio_irq_handler *h)
{
	struct rio_mport *mport = h->mport;
	struct rio_priv *priv = mport->priv;
	u32 mask;

	if (test_and_clear_bit(RIO_IRQ_ENABLED,  &h->state)) {
		__rio_local_read_config_32(mport, h->irq_enab_reg_addr, &mask);
		mask &= ~h->irq_state_mask;
		__rio_local_write_config_32(mport, h->irq_enab_reg_addr, mask);

		free_irq(priv->irq_line, h);
		if (h->release_fn)
			h->release_fn(h);
	}
}

/**
 * MISC Indications
 */
#if defined(CONFIG_RAPIDIO_HOTPLUG)
static void rio_port_down_notify(struct rio_mport *mport)
{
	unsigned long flags;
	struct rio_priv *priv = mport->priv;

	spin_lock_irqsave(&priv->port_lock, flags);
	if (priv->port_notify_cb)
		priv->port_notify_cb(mport);

	spin_unlock_irqrestore(&priv->port_lock, flags);
}
#else
#define rio_port_down_notify(mport)
#endif

/**
 * __port_fatal_err - Check port error state and clear latched
 *                    error state to enable detection of new events.
 *
 * @mport: Master port
 *
 * Returns:
 * 1 -- port fatal error state is detected
 * 0 -- port ok
 */
static inline void __misc_fatal(struct rio_mport *mport,
				u32 misc_state)
{
#if defined(CONFIG_AXXIA_RIO_STAT)
	struct rio_priv *priv = mport->priv;
#endif
	u32 amast = 0;
	u32 aslv_state = 0;
	u32 escsr, iecsr;

	__rio_local_read_config_32(mport, RIO_ESCSR(priv->portNdx), &escsr);
	__rio_local_read_config_32(mport, EPC_IECSR(priv->portNdx), &iecsr);

	/* clear latched state indications */
	__rio_local_write_config_32(mport, RIO_ESCSR(priv->portNdx),
					(escsr & RIO_EXCSR_WOLR));
	__rio_local_write_config_32(mport, EPC_IECSR(priv->portNdx),
					(iecsr & EPC_IECSR_RETE));

#if defined(CONFIG_AXXIA_RIO_STAT)
	__add_event_dbg(priv, escsr, iecsr);
	__add_state_dbg(priv, escsr);
#endif

	if (misc_state & MISC_FATAL) {

		__rio_local_read_config_32(mport, RAB_AMAST_STAT, &amast);
		__rio_local_read_config_32(mport, RAB_ASLV_STAT_CMD,
					   &aslv_state);
		/* clear latched state */
		__rio_local_write_config_32(mport, RAB_AMAST_STAT, amast);
		__rio_local_write_config_32(mport, RAB_ASLV_STAT_CMD,
					    aslv_state);

		__misc_fatal_dbg(priv, misc_state, amast);

	}
	if ((escsr & ESCSR_FATAL) ||
	    (iecsr & EPC_IECSR_RETE) ||
	    (misc_state & MISC_FATAL))
		rio_port_down_notify(mport);
}

/**
 * srio_sw_reset - Reset the SRIO (GRIO) module when it reaches a fatal
 *                 lockup state
 * @mport: Master port with triggered interrupt
 */
static void srio_sw_reset(struct rio_mport *mport)
{
	struct rio_priv *priv = mport->priv;

	/**
	 * Reset platform if port is broken
	 */
	if (priv->linkdown_reset.win) {
		u32 r0, r00, r1, r2;

		__rio_local_read_config_32(mport, RIO_DID_CSR, &r1);
		__rio_local_read_config_32(mport, RIO_COMPONENT_TAG_CSR, &r2);

		r0 = *((u32 *)priv->linkdown_reset.win+
				priv->linkdown_reset.reg_addr);
		*((u32 *)priv->linkdown_reset.win+
				priv->linkdown_reset.reg_addr) =
				r0 | priv->linkdown_reset.reg_mask;

		r00 = *((u32 *)priv->linkdown_reset.win+
				priv->linkdown_reset.reg_addr);
			/* Verify that the bit was set? */

		*((u32 *)priv->linkdown_reset.win+
			priv->linkdown_reset.reg_addr) = r0;

		__rio_local_write_config_32(mport, RIO_DID_CSR, r1);
		__rio_local_write_config_32(mport, RIO_COMPONENT_TAG_CSR, r2);
	}
}

/**
 * misc_irq_handler - MISC interrupt handler
 * @h: handler specific data
 * @state: Interrupt state
 */
static void misc_irq_handler(struct rio_irq_handler *h, u32 state)
{
	struct rio_mport *mport = h->mport;
#if defined(CONFIG_AXXIA_RIO_STAT)
	struct rio_priv *priv = mport->priv;
#endif

	/*
	 * Handle miscellaneous 'Link (IPG) Reset Request'
	 */
	if (state & LINK_REQ_INT)
		srio_sw_reset(mport);

	/**
	 * Notify platform if port is broken
	 */
	__misc_fatal(mport, state);

#if defined(CONFIG_AXXIA_RIO_STAT)
	/**
	 * update event stats
	 */
	__misc_info_dbg(priv, state);
#endif
}

/**
 * linkdown_irq_handler - Link Down interrupt Status interrupt handler
 * @h: handler specific data
 * @state: Interrupt state
 */
static void linkdown_irq_handler(struct rio_irq_handler *h, u32 state)
{
	struct rio_mport *mport = h->mport;
#if defined(CONFIG_AXXIA_RIO_STAT)
	struct rio_priv *priv = mport->priv;
#endif

	/**
	 * Reset platform if port is broken
	 */
	if (state & RAB_SRDS_STAT1_LINKDOWN_INT)
		srio_sw_reset(mport);

#if defined(CONFIG_AXXIA_RIO_STAT)
	/**
	 * Update event stats
	 */
	__linkdown_dbg(priv, state);
#endif
}

/**
 * rpio_irq_handler - RPIO interrupt handler.
 * Service Peripheral Bus bridge, RapidIO -> Peripheral bus interrupt
 *
 * @h: handler specific data
 * @state: Interrupt state
 *
 */
static void rpio_irq_handler(struct rio_irq_handler *h, u32 state)
{
	struct rio_mport *mport = h->mport;
#if defined(CONFIG_AXXIA_RIO_STAT)
	struct rio_priv *priv = mport->priv;
#endif

	if (state & RPIO_TRANS_COMPLETE)
		__irq_dbg(priv, RIO_PIO_COMPLETE);

	if (state & RIO_PIO_FAILED) {
		u32 rpio_stat;

		__rio_local_read_config_32(mport, RAB_RPIO_STAT, &rpio_stat);
		__rio_local_write_config_32(mport, RAB_RPIO_STAT, rpio_stat);
#if defined(CONFIG_AXXIA_RIO_STAT)
		__rpio_fail_dbg(priv, rpio_stat);
#endif
	}
}

/**
 * enable_rpio - Turn on RPIO (only for debug purposes)
 * @h: Interrupt handler specific data
 *
 * Caller must hold RAB lock
 */
static int enable_rpio(struct rio_irq_handler *h, u32 mask, u32 bits)
{
	int rc;

	if (test_bit(RIO_IRQ_ENABLED, &h->state))
		return -EBUSY;

	h->irq_state_mask &= ~mask;
	h->irq_state_mask |= bits;
	rc = alloc_irq_handler(h, NULL, "rio-rpio");

	return rc;
}

/**
 * APIO
 */

/**
 * apio_irq_handler - APIO interrupt handler.
 * Service Peripheral Bus bridge, Peripheral bus -> RapidIO interrupt
 *
 * @h: handler specific data
 * @state: Interrupt state
 *
 */
static void apio_irq_handler(struct rio_irq_handler *h, u32 state)
{
	struct rio_mport *mport = h->mport;
#if defined(CONFIG_AXXIA_RIO_STAT)
	struct rio_priv *priv = mport->priv;
#endif

	if (state & APIO_TRANS_COMPLETE)
		__irq_dbg(priv, RIO_APIO_COMPLETE);

	if (state & APIO_TRANS_FAILED) {
		u32 apio_stat;

		__rio_local_read_config_32(mport, RAB_APIO_STAT, &apio_stat);
		__rio_local_write_config_32(mport, RAB_APIO_STAT, apio_stat);
#if defined(CONFIG_AXXIA_RIO_STAT)
		__apio_fail_dbg(priv, apio_stat);
#endif
	}
}

/**
 * enable_apio - Turn on APIO (only for debug purposes)
 * @h: Interrupt handler specific data
 *
 * Caller must hold RAB lock
 */
static int enable_apio(struct rio_irq_handler *h, u32 mask, u32 bits)
{
	int rc;

	if (test_bit(RIO_IRQ_ENABLED, &h->state))
		return -EBUSY;

	h->irq_state_mask &= ~mask;
	h->irq_state_mask |= bits;
	rc = alloc_irq_handler(h, NULL, "rio-apio");

	return rc;
}

/**
 * PORT WRITE events
 */

/**
 * pw_irq_handler - AXXIA port write interrupt handler
 * @h: handler specific data
 * @state: PW Interrupt state
 *
 * Handles port write interrupts.
 */
static void pw_irq_handler(struct rio_irq_handler *h, u32 state)
{
	struct rio_mport *mport = h->mport;
	struct rio_priv *priv = mport->priv;
	struct rio_pw_irq *pw = h->data;
	u32 csr;
	int noofpw;
	u32 msg_word;

	__rio_local_read_config_32(mport, RAB_IB_PW_CSR, &csr);
	noofpw = RAB_IB_PW_NUMWORDS(csr);
	dev_dbg(priv->dev, "%s: noofpw %d\n", __func__, noofpw);
	if (!(noofpw)) {
		__irq_dbg(priv, RIO_MISC_PW_SPURIOUS);
		return;
	}
	__irq_dbg(priv, RIO_MISC_PW);

	while (noofpw) {

read_buff:
		__rio_local_read_config_32(mport, RAB_IB_PW_DATA, &msg_word);
		pw->msg_buffer[pw->msg_wc++] = BSWAP(msg_word);
		if (pw->msg_wc == 4) {
			__irq_dbg(priv, RIO_MISC_PW_MSG);
			/*
			 * Pass the port-write message to RIO
			 * core for processing
			 */
			rio_inb_pwrite_handler(mport,
					 (union rio_pw_msg *)pw->msg_buffer);
			pw->msg_wc = 0;
		}
		noofpw--;
		if (noofpw)
			goto read_buff;

		__rio_local_read_config_32(mport, RAB_IB_PW_CSR, &csr);
		noofpw = RAB_IB_PW_NUMWORDS(csr);
	}
}

static void axxia_rio_flush_pw(struct rio_mport *mport, int noofpw,
			     struct rio_pw_irq *pw_data)
{
	struct rio_priv *priv = mport->priv;
	u32 dummy;
	int x;

	dev_dbg(priv->dev, "(%s): flush %d words from pwbuff\n",
		__func__, noofpw);
	for (x = 0; x < noofpw; x++) {
		__rio_local_read_config_32(mport, RAB_IB_PW_DATA, &dummy);
		pw_data->discard_count++;
	}
	pw_data->msg_wc = 0;
}

/**
 * enable_pw - enable port-write interface unit
 * @h: Interrupt handler specific data
 *
 * Caller must hold RAB lock
 */
static int enable_pw(struct rio_irq_handler *h)
{
	struct rio_mport *mport = h->mport;
	struct rio_priv *priv = mport->priv;
	struct rio_pw_irq *pw_data;
	u32 rval;
	int rc;

	if (test_bit(RIO_IRQ_ENABLED, &h->state))
		return -EBUSY;

	pw_data = kzalloc(sizeof(struct rio_pw_irq), GFP_KERNEL);
	if (!pw_data)
		return -ENOMEM;

	__rio_local_read_config_32(mport, RAB_IB_PW_CSR, &rval);
	rval |= RAB_IB_PW_EN;
	axxia_rio_flush_pw(mport, RAB_IB_PW_NUMWORDS(rval), pw_data);
	__rio_local_write_config_32(mport, RAB_IB_PW_CSR, rval);

	rc = alloc_irq_handler(h, pw_data, "rio-pw");
	if (rc)
		goto err;
	atomic_inc(&priv->api_user);
	return rc;

err:
	rval &= ~RAB_IB_PW_EN;
	__rio_local_write_config_32(mport, RAB_IB_PW_CSR, rval);
	kfree(pw_data);
	return rc;
}

/**
 * disable_pw - Disable port-write interface unit
 * @h: Interrupt handler specific data
 *
 * Caller must hold RAB lock
 */
static void disable_pw(struct rio_irq_handler *h)
{
	struct rio_mport *mport = h->mport;
	struct rio_priv *priv = mport->priv;
	struct rio_pw_irq *pw_data = h->data;
	u32 rval;

	__rio_local_read_config_32(mport, RAB_IB_PW_CSR, &rval);
	rval &= ~RAB_IB_PW_EN;
	__rio_local_write_config_32(mport, RAB_IB_PW_CSR, rval);
	kfree(pw_data);
	h->data = NULL;
	atomic_dec(&priv->api_user);
}

/**
 * DOORBELL events
 */

/**
 * axxia_rio_rx_db_int_handler - AXXIA inbound doorbell interrupt handler
 * @mport: Master port with triggered interrupt
 * @mask: Interrupt register data
 *
 * Handles inbound doorbell interrupts.  Executes a callback on received
 * doorbell.
 */
void rx_db_handler(struct rio_mport *mport)
{
	struct rio_priv *priv = mport->priv;
	struct rio_dbell *dbell;
	u32 csr, info;
	u8 num_msg;
	u16 src_id, db_info;
	int found;

	__rio_local_read_config_32(mport, RAB_IB_DB_CSR, &csr);
	num_msg = IB_DB_CSR_NUM_MSG(csr);

	for (; num_msg; num_msg--) {
		__rio_local_read_config_32(mport, RAB_IB_DB_INFO, &info);
		src_id = DBELL_SID(info);
		db_info = DBELL_INF(info);

		found = 0;
		dev_dbg(priv->dev,
			 "Processing doorbell, sid %4.4x info %4.4x\n",
			src_id, db_info);

		list_for_each_entry(dbell, &mport->dbells, node) {
			if (dbell->res->start <= db_info &&
			    (dbell->res->end >= db_info)) {
				found = 1;
				break;
			}
		}
		if (found) {
			/**
			 * NOTE: dst is set to 0 since we don't have
			 *       that value in the ACP
			 */
			__irq_dbg(priv, RIO_MISC_IB_DB);
			if (dbell->dinb)
				dbell->dinb(mport, dbell->dev_id, src_id,
						0, db_info);
		} else {
			__irq_dbg(priv, RIO_MISC_IB_DB_SPURIOUS);
			dev_dbg(priv->dev,
				"Spurious doorbell, sid %4.4x info %4.4x\n",
				src_id, db_info);
		}
	}
}

void db_irq_handler(struct rio_irq_handler *h, u32 state)
{
	struct rio_mport *mport = h->mport;
#if defined(CONFIG_AXXIA_RIO_STAT)
	struct rio_priv *priv = mport->priv;
#endif

	/**
	 * Handle RX doorbell events
	 */
	if (state & IB_DB_RCV_INT)
		rx_db_handler(mport);

#if defined(CONFIG_AXXIA_RIO_STAT)
	/**
	 * Update outbound doorbell stats
	 */
	if (state & OB_DB_DONE_INT)
		__ob_db_dbg(priv, mport);
#endif
}

/**
 * OBDME Events/Outbound Messages
 */

static void release_dme(struct kref *kref)
{
	struct rio_msg_dme *me = container_of(kref, struct rio_msg_dme, kref);
	struct rio_priv *priv = me->priv;
	struct rio_msg_desc *desc;
	int i;

	if (me->desc) {
		for (i = 0, desc = me->desc; i < me->entries; i++, desc++) {
			if (desc->msg_virt)
				kfree(desc->msg_virt);
		}
		kfree(me->desc);
	}

	if (me->descriptors)
		kfree(me->descriptors);

	if (!priv->internalDesc) {
		if (me->dres.parent)
			release_resource(&me->dres);
	}

	kfree(me);
}

static inline struct rio_msg_dme *dme_get(struct rio_msg_dme *me)
{
	if (me)
		kref_get(&me->kref);
	return me;
}

static inline void dme_put(struct rio_msg_dme *me)
{
	if (me)
		kref_put(&me->kref, release_dme);
}

static inline int check_dme(int dme_no,
			    int *numDmes,
			    int *dmesInUse,
			    int *dmes)
{
	int i;
	for (i = 0; i < 2; i++) {
		if (dme_no < numDmes[i]) {
			if (dmes[i] & (1 << dme_no)) {
				if (dmesInUse[i] & (1 << dme_no))
					return -EBUSY;	/* Already allocated */
				return 0;
			}
		} else {
			dme_no -= numDmes[i];
		}
	}

	return -ENXIO;	/* Not available */
}

static inline int select_dme(int dme_no,
			     int *numDmes,
			     int *dmesInUse,
			     int *dmes,
			     int value)
{
	int i;
	for (i = 0; i < 2; i++) {
		if (dme_no < numDmes[i]) {
			dmesInUse[i] &= ~(1 << dme_no);
			dmesInUse[i] |= (value << dme_no);
			return 0;
		} else {
			dme_no -= numDmes[i];
		}
	}

	return -ENXIO;	/* Not available */
}

static inline int choose_ob_dme(
	struct rio_priv	*priv,
	int len,
	struct rio_msg_dme **ob_dme,
	int *buf_sz)
{
	int i, j, ret = 0;

	/* Find an OB DME that is enabled and which has empty slots */
	for (j = 0; j < 2; j++) {
		for (i = 0; i < priv->numOutbDmes[j]; i++) {
			int sz = RIO_OUTB_DME_TO_BUF_SIZE(priv, i);
			struct rio_irq_handler *h = &priv->ob_dme_irq[i];
			struct rio_msg_dme *dme = h->data;

			if (!test_bit(RIO_IRQ_ENABLED, &h->state))
				continue;

			if (len > sz)
				continue;

			if (dme->entries > (dme->entries_in_use+1)) {
				(*ob_dme) = dme;
				(*buf_sz) = sz;
				return ret + i;
			}
		}
		ret += priv->numOutbDmes[j];
	}

	return -EBUSY;
}

static void release_mbox(struct kref *kref)
{
	struct rio_rx_mbox *mb = container_of(kref, struct rio_rx_mbox, kref);
	struct rio_priv *priv = mb->mport->priv;
	int letter;

	/* Quickly disable the engines */
	for (letter = 0; letter < RIO_MSG_MAX_LETTER; letter++) {
		if (mb->me[letter])
			__rio_local_write_config_32(mb->mport,
				   RAB_IB_DME_CTRL(mb->me[letter]->dme_no), 0);
	}

	/* And then release the remaining resources */
	for (letter = 0; letter < RIO_MSG_MAX_LETTER; letter++) {
		if (mb->me[letter]) {
			dme_put(mb->me[letter]);
			select_dme(mb->me[letter]->dme_no,
					&priv->numInbDmes[0],
					&priv->inbDmesInUse[0],
					&priv->inbDmes[0], 0);
		}
	}

	priv->ib_dme_irq[mb->mbox_no].irq_state_mask = 0;

	if (mb->virt_buffer)
		kfree(mb->virt_buffer);

	kfree(mb);
}

static inline struct rio_rx_mbox *mbox_get(struct rio_rx_mbox *mb)
{
	if (mb)
		kref_get(&mb->kref);
	return mb;
}

static inline void mbox_put(struct rio_rx_mbox *mb)
{
	if (mb)
		kref_put(&mb->kref, release_mbox);
}

static struct rio_msg_dme *alloc_message_engine(struct rio_mport *mport,
						int dme_no, void *dev_id,
						int buf_sz, int entries,
						int ack_buf)
{
	struct rio_priv *priv = mport->priv;
	struct rio_msg_dme *me = kzalloc(sizeof(struct rio_msg_dme),
					 GFP_KERNEL);
	struct resource *dres;
	struct rio_msg_desc *desc;
	int i;

	if (!me)
		return ERR_PTR(-ENOMEM);
	memset(me, 0, sizeof(struct rio_msg_dme));

	kref_init(&me->kref);
	spin_lock_init(&me->lock);
	me->priv = priv;
	me->sz = buf_sz;
	dres = &me->dres;

	if (priv->internalDesc) {
		dres->name = "DME_DESC";
		dres->flags = ACP_RESOURCE_HW_DESC;
		if (allocate_resource(&priv->acpres[ACP_HW_DESC_RESOURCE],
				dres, entries,
				priv->acpres[ACP_HW_DESC_RESOURCE].start,
				priv->acpres[ACP_HW_DESC_RESOURCE].end,
				0x1, NULL, NULL)) {
			memset(dres, 0, sizeof(*dres));
			goto err;
		}
	} else {
		dres->start = 0;
	}
	me->desc = kzalloc(sizeof(struct rio_msg_desc) * entries, GFP_KERNEL);
	if (!me->desc)
		goto err;
	me->descriptors = kzalloc(sizeof(struct rio_desc) * entries,
				GFP_KERNEL);
	if (!me->descriptors)
		goto err;
	me->entries = entries;
	me->dev_id = dev_id;
	me->entries_in_use = 0;
	me->write_idx = 0;
	me->read_idx = 0;
	me->last_invalid_desc = 0;
	me->last_compl_idx = 0;
	me->tx_dme_tmo = 0;
	me->dme_no = dme_no;

	for (i = 0, desc = me->desc; i < entries; i++, desc++) {
		desc->msg_virt = kzalloc(buf_sz, GFP_KERNEL);
		if (!desc->msg_virt)
			goto err;
		desc->msg_phys = virt_to_phys(desc->msg_virt);
		clear_bit(RIO_DESC_USED, &desc->state);
		desc->desc_no = dres->start + i;
	}
	desc--;
	desc->last = 1;

	return me;
err:
	dme_put(me);
	return ERR_PTR(-ENOMEM);
}

/**
 * ob_dme_irq_handler - Outbound message interrupt handler
 * --- Called in threaded irq handler ---
 * @h: Pointer to interrupt-specific data
 *
 * Handles outbound message interrupts. Executes a callback,
 * if available, on each successfully sent message.
 *
 * @note:
 * HW descriptor fetch and update may be out of order
 * Check state of all used descriptors and take care to not fall into
 * any of the traps that come with this design:
 *
 * Due to this (possibly) out of order execution in the HW, SW ack of
 * descriptors must be done atomically, re-enabling descriptors with
 * completed transactions while processing finished transactions may
 * break the ring and leave the DMA engine in a state where it doesn't
 * process new inserted requests.
 *
 * Neither is it an option to process only finished transactions and
 * leave the unfinished pending until the next ready interrupt arrives.
 * Doing so may also break the ring.
 *
 * The net core transmit ack callback must not be called with a spinlock,
 * because if the net device has packets queued, the cb will trigger
 * high priority jobs that will push more packets for the DMA descriptor
 * ring and compete for the same spinlock that is used in ring processing.
 *
 * In earlier versions the callback was called for each processed descriptor,
 * releasing and re-claiming the lock before and after, but since we don't
 * want the net core interfering in ring processing anyway, this was a bad
 * idea altogether.  TX ack to net core is now deferred until after ring
 * maintenance is completed.
 *
 * If you allow the ring to get full, i.e. you actually have a ring, not
 * a "invalid-descriptor- * terminated" list, then the HW doesn't always
 * respond reliably.  It seems to go into a real bad state, enabling and
 * firing interrupts constantly, but not completing the pending transactions.
 * Looking at the link partner, it doesn't even seem that transactions
 * are even generated.  This is more likely to happen in stress situations,
 * where the TX side is overloading the RX side temporarily so that
 * transactions take longer to complete.
 *
 * I have no idea what happens, just looks as the HW loses track of read
 * and write pointers for the ring or something... I'm working around this
 * for now by never using up the the last descriptor in the ring.
 *
 * The HW interrupts provided for this thing are, hm.., something that I
 * fail to understand here.  Sometimes you get too many interrupts and
 * sometimes you don't get them at all, even though you should have them.
 * The most reliable approach seems to be: never leave the irq service
 * routine unless all current transactions are completed.  And always
 * re-enable the DMA engine when a new descriptor is enabled in the ring.
 */
static void ob_dme_irq_handler(struct rio_irq_handler *h, u32 state)
{
	struct rio_mport *mport = h->mport;
	struct rio_priv *priv = mport->priv;
	struct rio_msg_dme *mbox = h->data;
	u32 dme_stat, dw0, dme_no = 31 - CNTLZW(state);
	u32 dme_ctrl;
	unsigned long flags;
	int i;

	/**
	 * Clear latched state
	 */
	__rio_local_read_config_32(mport, RAB_OB_DME_STAT(dme_no), &dme_stat);
	__rio_local_write_config_32(mport, RAB_OB_DME_STAT(dme_no), dme_stat);
	__ob_dme_dbg(priv, dme_stat);

	spin_lock_irqsave(&mbox->lock, flags);

	/**
	 * Try to kick back some life in the HW if it is un-responsive
	 */
	__rio_local_read_config_32(mport, RAB_OB_DME_CTRL(dme_no), &dme_ctrl);
	dme_ctrl |= DME_WAKEUP | DME_ENABLE;
	__rio_local_write_config_32(mport, RAB_OB_DME_CTRL(dme_no), dme_ctrl);

	/**
	 * Process all completed transactions
	 */
	for (i = 0; i < mbox->entries; i++) {
		struct rio_msg_desc *desc = &mbox->desc[i];

		if (mbox->last_compl_idx != desc->desc_no)
			continue;

		if (!priv->internalDesc) {
			dw0 = *((u32 *)DESC_TABLE_W0_MEM(mbox, desc->desc_no));
		} else {
			__rio_local_read_config_32(mport,
					DESC_TABLE_W0(desc->desc_no), &dw0);
		}

		if ((dw0 & DME_DESC_DW0_VALID) &&
		    (dw0 & DME_DESC_DW0_READY_MASK)) {
			if (!priv->internalDesc) {
				*((u32 *)DESC_TABLE_W0_MEM(mbox, desc->desc_no))
					= dw0 & DME_DESC_DW0_NXT_DESC_VALID;
			} else {
				__rio_local_write_config_32(mport,
					DESC_TABLE_W0(desc->desc_no),
					dw0 & DME_DESC_DW0_NXT_DESC_VALID);
			}
			__ob_dme_dw_dbg(priv, dw0);

			mbox->entries_in_use--;
			mbox->last_compl_idx = (mbox->last_compl_idx + 1) %
						mbox->entries;

			/**
			* UP-call to net device handler
			*/
			if (mport->outb_msg[dme_no].mcback) {
				__ob_dme_event_dbg(priv, dme_no,
						1 << RIO_OB_DME_TX_DESC_READY);
				mport->outb_msg[dme_no].mcback(mport,
							mbox->dev_id,
							dme_no,
							i,
							desc->cookie);
			}
		}
	}
	spin_unlock_irqrestore(&mbox->lock, flags);
}

/**
 * open_outb_mbox - Initialize AXXIA outbound mailbox
 * @mport: Master port implementing the outbound message unit
 * @dev_id: Device specific pointer to pass on event
 * @dme_no: Mailbox DME engine to open
 * @entries: Number of entries in the outbound mailbox ring for each letter
 * @prio: 0..3, higher number -> lower priority.
 *
 * Caller must hold RAB lock
 * If the specified mbox DME has already been opened/reserved, then we just
 * abort out of this operation with "busy", and without changing resource
 * allocation for the mbox DME.
 */
static int open_outb_mbox(struct rio_mport *mport, void *dev_id, int dme_no,
			  int entries, int prio)
{
	int i, rc = -ENOMEM;
	struct rio_priv *priv = mport->priv;
	struct rio_irq_handler *h = &priv->ob_dme_irq[dme_no];
	struct rio_msg_desc *desc;
	struct rio_msg_dme *me = NULL;
	u32 dme_stat, dme_ctrl, dw0, dw1, dw2, dw3, wait = 0;
	u64 descChainStart, descAddr;
	int buf_sz = 0;

	if ((entries < 2) || (entries > priv->desc_max_entries))
		return -EINVAL;

	if (test_bit(RIO_IRQ_ENABLED, &h->state))
		return -EBUSY;

	/* Is the requested DME present & available? */
	rc = check_dme(dme_no, &priv->numOutbDmes[0],
			&priv->outbDmesInUse[0], &priv->outbDmes[0]);
	if (rc < 0)
		return rc;

	buf_sz = RIO_OUTB_DME_TO_BUF_SIZE(priv, dme_no);

	me = alloc_message_engine(mport, dme_no, dev_id, buf_sz, entries, 1);
	if (IS_ERR(me))
		return -ENOMEM;

	do {
		__rio_local_read_config_32(mport,
					   RAB_OB_DME_STAT(dme_no), &dme_stat);
		if (wait++ > 100) {
			rc = -EBUSY;
			goto err;
		}
	} while (dme_stat & OB_DME_STAT_TRANS_PEND);

	for (i = 0, desc = me->desc; i < entries; i++, desc++) {
		dw0 = 0;
		if (!priv->internalDesc) {
#ifdef AXM55XX_OUTB_DME_BBS
			dw1 = (u32)(desc->msg_phys >> 11) & 0x1fe00000;
			dw2 = (u32)(desc->msg_phys >>  0) & 0x3fffffff;
#else
			dw1 = 0;
			dw2 = (u32)(desc->msg_phys >>  8) & 0x3fffffff;
#endif
			*((u32 *)DESC_TABLE_W0_MEM(me, desc->desc_no)) = dw0;
			*((u32 *)DESC_TABLE_W1_MEM(me, desc->desc_no)) = dw1;
			*((u32 *)DESC_TABLE_W2_MEM(me, desc->desc_no)) = dw2;
			*((u32 *)DESC_TABLE_W3_MEM(me, desc->desc_no)) = 0;
		} else {
			dw1 = 0;
			dw2 = (u32)(desc->msg_phys >> 8) & 0x3fffffff;
			__rio_local_write_config_32(mport,
					    DESC_TABLE_W0(desc->desc_no), dw0);
			__rio_local_write_config_32(mport,
					    DESC_TABLE_W1(desc->desc_no), dw1);
			__rio_local_write_config_32(mport,
					    DESC_TABLE_W2(desc->desc_no), dw2);
			__rio_local_write_config_32(mport,
					    DESC_TABLE_W3(desc->desc_no), 0);
		}
	}

	/**
	 * Last descriptor - make ring.
	 * Next desc table entry -> dw2.First desc address[37:36]
	 *                       -> dw3.First desc address[35:4].
	 * (desc_base + 0x10 * nr)
	 */
	desc--;
	dw0 |= DME_DESC_DW0_NXT_DESC_VALID;
	if (!priv->internalDesc) {
		descChainStart = (uintptr_t)virt_to_phys(me->descriptors);

		dw2  = *((u32 *)DESC_TABLE_W2_MEM(me, desc->desc_no));
		dw2 |= (descChainStart >> 4) & 0xc0000000;
		dw3  = descChainStart >> 4;
		*((u32 *)DESC_TABLE_W0_MEM(me, desc->desc_no)) = dw0;
		*((u32 *)DESC_TABLE_W2_MEM(me, desc->desc_no)) = dw2;
		*((u32 *)DESC_TABLE_W3_MEM(me, desc->desc_no)) = dw3;
	} else {
		descChainStart = DESC_TABLE_W0(me->dres.start);

		__rio_local_read_config_32(mport,
					DESC_TABLE_W2(desc->desc_no), &dw2);
		dw2 |= ((descChainStart >> 8) & 0xc0000000);
		dw3  = 0;
		__rio_local_write_config_32(mport,
					DESC_TABLE_W0(desc->desc_no), dw0);
		__rio_local_write_config_32(mport,
					DESC_TABLE_W2(desc->desc_no), dw2);
		__rio_local_write_config_32(mport,
					DESC_TABLE_W3(desc->desc_no), dw3);
	}

	/**
	 * Set TID mask to restrict RAB to only send
	 * one letter to each destination at a time.
	 * NB! masking only effective per group - multi/single segment.
	 */
	__rio_local_write_config_32(mport, RAB_OB_DME_TID_MASK,
				    OB_DME_TID_MASK);

	/**
	 * And setup the DME chain control and chain start address
	 */
	dme_ctrl  = (prio & 0x3) << 4;
	dme_ctrl |= (u32)((descChainStart >> 6) & 0xc0000000);
	descAddr  = (u32)descChainStart >> 4;
	__rio_local_write_config_32(mport, RAB_OB_DME_DESC_ADDR(dme_no),
				    descAddr);
	__rio_local_write_config_32(mport, RAB_OB_DME_CTRL(dme_no), dme_ctrl);

	/**
	 * Create irq handler and enable MBOX DME Engine irq
	 */
	sprintf(me->name, "obmb-%d", dme_no);
	rc = alloc_irq_handler(h, me, me->name);
	if (rc)
		goto err;

	/**
	 * And finally update the state to reflect the DME is in use
	 */
	rc = select_dme(dme_no, &priv->numOutbDmes[0],
			&priv->outbDmesInUse[0], &priv->outbDmes[0], 1);

	atomic_inc(&priv->api_user);
	return 0;

err:
	dme_put(me);
	return rc;
}

/**
 * release_outb_mbox - Close AXXIA outbound mailbox
 * @mport: Master port implementing the outbound message unit
 * @mbox: Mailbox to close
 *
 * Caller must hold RAB lock
 * Release all resources i.e. DMEs, descriptors, buffers, and so on.
 */

static void release_outb_mbox(struct rio_irq_handler *h)
{
	struct rio_mport *mport = h->mport;
	struct rio_priv *priv = mport->priv;
	struct rio_msg_dme *me = h->data;

	__rio_local_write_config_32(mport, RAB_OB_DME_CTRL(me->dme_no), 0);

	select_dme(me->dme_no, &priv->numOutbDmes[0],
		   &priv->outbDmesInUse[0], &priv->outbDmes[0], 0);

	if (me->entries_in_use) {
		dev_warn(priv->dev,
			"RIO: MBOX DME %d had %d messages unread at release\n",
			me->dme_no,
			me->entries_in_use);
	}

	h->data = NULL;
	dme_put(me);
	atomic_dec(&priv->api_user);
}

/**
 * ib_dme_irq_handler - AXXIA inbound message interrupt handler
 * @mport: Master port with triggered interrupt
 * @mask: Interrupt register data
 *
 * Handles inbound message interrupts.  Executes a callback, if available,
 * on received message.
 */
static void ib_dme_irq_handler(struct rio_irq_handler *h, u32 state)
{
	struct rio_mport *mport = h->mport;
	struct rio_priv *priv = mport->priv;
	struct rio_rx_mbox *mb = h->data;
	int mbox_no = mb->mbox_no;
	int letter = RIO_MSG_MAX_LETTER - 1;
	u32 dme_mask = state;

	/**
	 * Inbound mbox has 4 engines, 1 per letter.
	 * For each message engine that contributes to IRQ state,
	 * go through all descriptors in queue that have been
	 * written but not handled.
	 */
	while (dme_mask) {
		struct rio_msg_dme *me;
		u32 dme_stat;
		u32 dw0;
		int dme_no = 31 - CNTLZW(dme_mask);
		int num_new;
		dme_mask ^= (1 << dme_no);

		while (mb->me[letter]->dme_no != dme_no)
			letter--;

		if (letter < 0)
			return;

		me = mb->me[letter];

		/**
		 * Get and clear latched state
		 */
		__rio_local_read_config_32(mport,
					   RAB_IB_DME_STAT(dme_no), &dme_stat);
		__rio_local_write_config_32(mport,
					    RAB_IB_DME_STAT(dme_no), dme_stat);
		__ib_dme_dbg(priv, dme_stat);
#ifdef CONFIG_SRIO_IRQ_TIME
		{
			struct rio_irq_handler *hN;
			hN = &priv->ib_dme_irq[mbox_no];

			if (atomic_read(&hN->start_time)) {
				if (me->pkt == 0) {
					me->start_irq_tb = hN->irq_tb;
					me->start_thrd_tb = hN->thrd_tb;
				}
				me->stop_irq_tb = hN->irq_tb;
				me->stop_thrd_tb = hN->thrd_tb;
				if ((hN->thrd_tb - hN->irq_tb) > me->max_lat)
					me->max_lat = hN->thrd_tb - hN->irq_tb;
				if ((hN->thrd_tb - hN->irq_tb) < me->min_lat)
					me->min_lat = hN->thrd_tb - hN->irq_tb;
			}
		}
#endif
		/**
		 * Set Valid flag to 0 on each desc with a new message.
		 * Flag is reset when the message belonging to the desc
		 * is fetched in get_inb_message().
		 * HW descriptor update and fetch is in order.
		 */
		num_new = 0;
		do {
			struct rio_msg_desc *desc = &me->desc[me->write_idx];

			if (!priv->internalDesc) {
				dw0 = *((u32 *)DESC_TABLE_W0_MEM(me,
							 desc->desc_no));
			} else {
				__rio_local_read_config_32(mport,
					 DESC_TABLE_W0(desc->desc_no), &dw0);
			}

			if ((dw0 & DME_DESC_DW0_READY_MASK) &&
			    (dw0 & DME_DESC_DW0_VALID)) {

#ifdef OBSOLETE_BZ47185
				/* Some chips clear this bit, some don't.
				** Make sure in any event. */
				if (!priv->internalDesc) {
					*((u32 *)DESC_TABLE_W0_MEM(me,
							 desc->desc_no)) =
						 dw0 & ~DME_DESC_DW0_VALID;
				} else {
					__rio_local_write_config_32(mport,
						DESC_TABLE_W0(desc->desc_no),
						dw0 & ~DME_DESC_DW0_VALID);
				}
#endif /* OBSOLETE_BZ47185 */

				if (mport->inb_msg[mbox_no].mcback)
					mport->inb_msg[mbox_no].mcback(mport,
								me->dev_id,
								mbox_no,
								desc->desc_no);

				__ib_dme_dw_dbg(priv, dw0);
				__ib_dme_event_dbg(priv, dme_no,
						   1 << RIO_IB_DME_RX_PUSH);
				me->write_idx = (me->write_idx + 1) %
						 me->entries;
				num_new++;
				if (num_new == me->entries)
					break;
			}
		} while ((dw0 & DME_DESC_DW0_READY_MASK) &&
			 (dw0 & DME_DESC_DW0_VALID));

		/**
		 * Wakeup owner
		 */
		if (num_new == me->entries)
			__ib_dme_event_dbg(priv, dme_no,
					   1 << RIO_IB_DME_RX_RING_FULL);

#ifdef OBSOLETE_BZ47185
		if (dme_stat & IB_DME_STAT_SLEEPING) {
			struct rio_msg_desc *desc;
			u32 dme_ctrl;
			int i;
			int inval = 0;  /* see #ifdef STRICT_INB_ORDERING */

			__ib_dme_event_dbg(priv, dme_no,
					   1 << RIO_IB_DME_RX_SLEEP);
			for (i = 0, desc = me->desc; i < me->entries;
				 i++, desc++) {

				if (!priv->internalDesc) {
					dw0 = *((u32 *)DESC_TABLE_W0_MEM(me,
							      desc->desc_no));
				} else {
					__rio_local_read_config_32(mport,
						DESC_TABLE_W0(desc->desc_no),
						&dw0);
				}
				if (!(dw0 & DME_DESC_DW0_VALID)) {
					__ib_dme_event_dbg(priv, dme_no,
						1 << RIO_IB_DME_RX_PENDING_AT_SLEEP);
#ifdef STRICT_INB_ORDERING
					inval++;
					pr_warn("RIO: Inbound Message engine "
						"%d disabled\n RIO: Receive "
						"buffer ring is corrupt\n",
						dme_no);
#endif
				}
			}
			if (inval == 0) {
				__ib_dme_event_dbg(priv, dme_no,
						   1 << RIO_IB_DME_RX_WAKEUP);
				dme_ctrl = (mbox_no & 0x3f) << 6;
				dme_ctrl |= letter << 4;
				dme_ctrl |= DME_WAKEUP;
				dme_ctrl |= DME_ENABLE;
				__rio_local_write_config_32(mport,
					 RAB_IB_DME_CTRL(dme_no), dme_ctrl);
			}
		}
#endif /* OBSOLETE_BZ47185 */
	}
}

/**
 * open_inb_mbox - Initialize AXXIA inbound mailbox
 * @mport: Master port implementing the inbound message unit
 * @dev_id: Device specific pointer to pass on event
 * @mbox: Mailbox to open 0..(MID-1),
 *            0..3 multi segment,
 *            4..(MID-1) single segment
 * @entries: Number of entries in the inbound mailbox ring
 *
 * Initializes buffer ring.  Sets up desciptor ring and memory
 * for messages for all 4 letters in the mailbox.  [This means
 * that the actual descriptor requirements are "4 * entries".]
 *
 * Returns %0 on success and %-EINVAL or %-ENOMEM on failure.
 */
static int open_inb_mbox(struct rio_mport *mport, void *dev_id,
			 int mbox, int entries)
{
	struct rio_priv *priv = mport->priv;
	struct rio_irq_handler *h = NULL;
	int i, letter;
	int irq_state_mask = 0;
	u32 dme_ctrl;
	struct rio_rx_mbox *mb;
	int rc, buf_sz;

	if ((mbox < 0) || (mbox >= RIO_MAX_RX_MBOX))
		return -EINVAL;

	if ((entries < 2) || (entries > priv->desc_max_entries))
		return -EINVAL;

	h = &priv->ib_dme_irq[mbox];

	if (test_bit(RIO_IRQ_ENABLED, &h->state))
		return -EBUSY;

	buf_sz = RIO_MBOX_TO_BUF_SIZE(mbox);

	mb = kzalloc(sizeof(*mb), GFP_KERNEL);
	if (!mb)
		return -ENOMEM;
	mb->mbox_no = mbox;

	kref_init(&mb->kref);

	/**
	 *  Initialize rx buffer ring
	 */
	mb->mport = mport;
	mb->ring_size = entries * RIO_MSG_MAX_LETTER;
	mb->virt_buffer = kzalloc(mb->ring_size * sizeof(void *), GFP_KERNEL);
	if (!mb->virt_buffer) {
		kfree(mb);
		return -ENOMEM;
	}
	mb->last_rx_slot = 0;
	mb->next_rx_slot = 0;
	for (i = 0; i < mb->ring_size; i++)
		mb->virt_buffer[i] = NULL;

	/**
	 * Since we don't have the definition of letter in the generic
	 * RIO layer, we set up IB mailboxes for all letters for each
	 * mailbox.
	 */
	for (letter = 0; letter < RIO_MSG_MAX_LETTER; ++letter) {
		int dme_no = (mbox * RIO_MSG_MAX_LETTER) + letter;
		struct rio_msg_dme *me = NULL;
		struct rio_msg_desc *desc;
		u32 dw0, dw1, dw2, dw3;
		u64 descChainStart, descAddr;
		u32 dme_stat, wait = 0;
		u32 buffer_size = (buf_sz > 256 ? 3 : 0);

		rc = check_dme(dme_no, &priv->numInbDmes[0],
				&priv->inbDmesInUse[0], &priv->inbDmes[0]);
		if (rc < 0)
			return rc;

		me = alloc_message_engine(mport,
					  dme_no,
					  dev_id,
					  buf_sz,
					  entries,
					  0);
		if (IS_ERR(me)) {
			rc = PTR_ERR(me);
			goto err;
		}

		irq_state_mask |= (1 << dme_no);

		do {
			__rio_local_read_config_32(mport,
						   RAB_IB_DME_STAT(me->dme_no),
						   &dme_stat);
			if (wait++ > 100) {
				rc = -EBUSY;
				goto err;
			}
		} while (dme_stat & IB_DME_STAT_TRANS_PEND);

		mb->me[letter] = me;
		dw0 = ((buffer_size & 0x3) << 4) |
		      DME_DESC_DW0_EN_INT |
		      DME_DESC_DW0_VALID;
		dw1 = ((mbox & 0x3f) << 2) |
		      (letter & 0x3);
		dw3 = 0;		/* 0 means, next contiguous addr
					 * Also next desc valid bit in dw0
					 * must be zero. */
		for (i = 0, desc = me->desc; i < entries; i++, desc++) {
			if (!priv->internalDesc) {
				/* Reference AXX5500 Peripheral Subsystem
				 * Multicore Reference Manual, January 2013,
				 * Chapter 5, p. 584 */
				dw1 |= 0;
				dw2  = (u32)(desc->msg_phys >> 8) & 0x3fffffff;
				*((u32 *)DESC_TABLE_W0_MEM(me,
						 desc->desc_no)) = dw0;
				*((u32 *)DESC_TABLE_W1_MEM(me,
						 desc->desc_no)) = dw1;
				*((u32 *)DESC_TABLE_W2_MEM(me,
						 desc->desc_no)) = dw2;
				*((u32 *)DESC_TABLE_W3_MEM(me,
						 desc->desc_no)) = dw3;
			} else {
				dw1 |= 0;
				dw2  = (u32)(desc->msg_phys >> 8) & 0x3fffffff;
				__rio_local_write_config_32(mport,
					DESC_TABLE_W0(desc->desc_no), dw0);
				__rio_local_write_config_32(mport,
					DESC_TABLE_W1(desc->desc_no), dw1);
				__rio_local_write_config_32(mport,
					DESC_TABLE_W2(desc->desc_no), dw2);
				__rio_local_write_config_32(mport,
					DESC_TABLE_W3(desc->desc_no), dw3);
			}
		}

		/**
		 * Last descriptor - make ring.
		 * Next desc table entry -> dw2.First desc address[37:36].
		 *                       -> dw3.First desc address[35:4].
		 * (desc_base + 0x10 * nr)
		 */
		desc--;
		dw0 |= DME_DESC_DW0_NXT_DESC_VALID;
		dw0 &= ~DME_DESC_DW0_VALID;
		me->last_invalid_desc = desc->desc_no;
		if (!priv->internalDesc) {
			descChainStart =
				(uintptr_t)virt_to_phys(me->descriptors);

			dw2  = *((u32 *)DESC_TABLE_W2_MEM(me, desc->desc_no));
			dw2 |= (descChainStart >> 4) & 0xc0000000;
			dw3  = descChainStart >> 4;
			*((u32 *)DESC_TABLE_W0_MEM(me, desc->desc_no)) = dw0;
			*((u32 *)DESC_TABLE_W2_MEM(me, desc->desc_no)) = dw2;
			*((u32 *)DESC_TABLE_W3_MEM(me, desc->desc_no)) = dw3;
		} else {
			descChainStart = DESC_TABLE_W0(me->dres.start);

			__rio_local_read_config_32(mport,
					    DESC_TABLE_W2(desc->desc_no),
					    &dw2);
			dw3  = 0;
			dw2 |= ((descChainStart >> 8) & 0xc0000000);
			__rio_local_write_config_32(mport,
						DESC_TABLE_W0(desc->desc_no),
						dw0);
			__rio_local_write_config_32(mport,
						DESC_TABLE_W2(desc->desc_no),
						dw2);
			__rio_local_write_config_32(mport,
						DESC_TABLE_W3(desc->desc_no),
						dw3);
		}

		/**
		 * Setup the DME including descriptor chain start address
		 */
		dme_ctrl = (mbox & 0x3f) << 6;
		dme_ctrl |= letter << 4;
		dme_ctrl |= DME_WAKEUP;
		dme_ctrl |= DME_ENABLE;
		dme_ctrl |= (u32)((descChainStart >> 6) & 0xc0000000);
		descAddr  = (u32)descChainStart >> 4;
		__rio_local_write_config_32(mport,
					RAB_IB_DME_DESC_ADDR(dme_no), descAddr);
		__rio_local_write_config_32(mport,
					RAB_IB_DME_CTRL(dme_no), dme_ctrl);

		select_dme(dme_no, &priv->numInbDmes[0],
			&priv->inbDmesInUse[0], &priv->inbDmes[0], 1);
	}

	/**
	* Create irq handler and enable MBOX irq
	*/
	sprintf(mb->name, "ibmb-%d", mbox);
	priv->ib_dme_irq[mbox].irq_state_mask = irq_state_mask;
	rc = alloc_irq_handler(h, mb, mb->name);
	if (rc)
		goto err;

	atomic_inc(&priv->api_user);
	return 0;

err:
	priv->ib_dme_irq[mbox].irq_state_mask = 0;
	mbox_put(mb);
	return rc;
}

/**
 * release_inb_mbox - Close AXXIA inbound mailbox
 * @mport: Master port implementing the inbound message unit
 * @mbox: Mailbox to close
 *
 * Caller must hold RAB lock
 * Release all resources i.e. DMEs, descriptors, buffers, and so on.
 */

static void release_inb_mbox(struct rio_irq_handler *h)
{
	struct rio_mport *mport = h->mport;
	struct rio_priv *priv = mport->priv;
	struct rio_rx_mbox *mb = h->data;

	h->data = NULL;
	mbox_put(mb);
	atomic_dec(&priv->api_user);
}

void axxia_rio_port_get_state(struct rio_mport *mport, int cleanup)
{
#if defined(CONFIG_AXXIA_RIO_STAT)
	struct rio_priv *priv = mport->priv;
#endif
	u32 escsr, iecsr, state;

	if (cleanup) {
#if defined(CONFIG_AXXIA_RIO_STAT)
		reset_state_counters(priv);
#endif
		/**
		 * Clear latched state indications
		 */
		/* Miscellaneous Events */
		__rio_local_read_config_32(mport, RAB_INTR_STAT_MISC, &state);
		__rio_local_write_config_32(mport, RAB_INTR_STAT_MISC, state);
		/* Outbound Message Engine */
		__rio_local_read_config_32(mport, RAB_INTR_STAT_ODME, &state);
		__rio_local_write_config_32(mport, RAB_INTR_STAT_ODME , state);
		/* Inbound Message Engine */
		__rio_local_read_config_32(mport, RAB_INTR_STAT_IDME, &state);
		__rio_local_write_config_32(mport, RAB_INTR_STAT_IDME, state);
		/* Axxi Bus to RIO Events */
		__rio_local_read_config_32(mport, RAB_INTR_STAT_APIO, &state);
		__rio_local_write_config_32(mport, RAB_INTR_STAT_APIO, state);
		/* RIO to Axxi Bus Events */
		__rio_local_read_config_32(mport, RAB_INTR_STAT_RPIO, &state);
		__rio_local_write_config_32(mport, RAB_INTR_STAT_RPIO, state);
	}

	/* Master Port state */
	__rio_local_read_config_32(mport, RIO_ESCSR(priv->portNdx), &escsr);
	__rio_local_read_config_32(mport, EPC_IECSR(priv->portNdx), &iecsr);

	__rio_local_write_config_32(mport, RIO_ESCSR(priv->portNdx),
				(escsr & RIO_EXCSR_WOLR));
#if defined(CONFIG_AXXIA_RIO_STAT)
	__add_state_dbg(priv, escsr);
	if (!(escsr & RIO_ESCSR_PO)) /* Port is down */
		__add_event_dbg(priv, escsr, iecsr);
#endif
}

/**
 * RIO MPORT Driver API
 */

/**
 * axxia_rio_port_irq_enable - Register RIO interrupt handler
 *
 * @mport: master port
 * @irq: IRQ mapping from DTB
 *
 * Caller must hold RAB lock
 *
 * Returns:
 * 0        Success
 * <0       Failure
 */
int axxia_rio_port_irq_enable(struct rio_mport *mport)
{
	struct rio_priv *priv = mport->priv;
	int rc;

	atomic_inc(&port_irq_enabled);
	/**
	 * Clean up history
	 * from port reset/restart
	 */
	axxia_rio_port_get_state(mport, 1);
	rc = alloc_irq_handler(&priv->misc_irq, NULL, "rio-misc");
	if (rc)
		goto out;
	rc = alloc_irq_handler(&priv->db_irq, NULL, "rio-doorbell");
	if (rc)
		goto err1;

#if defined(CONFIG_AXXIA_RIO_STAT)
	rc = alloc_irq_handler(&priv->apio_irq, NULL, "rio-apio");
	if (rc)
		goto err2;
	rc = alloc_irq_handler(&priv->rpio_irq, NULL, "rio-rpio");
	if (rc)
		goto err3;
#endif
	__rio_local_write_config_32(mport, RAB_INTR_ENAB_GNRL,
				    RAB_INTR_ENAB_GNRL_SET);
out:
	return rc;
err0:
	dev_warn(priv->dev, "RIO: unable to request irq.\n");
	goto out;

#if defined(CONFIG_AXXIA_RIO_STAT)
err3:
	release_irq_handler(&priv->apio_irq);
err2:
	release_irq_handler(&priv->db_irq);
#endif

err1:
	release_irq_handler(&priv->misc_irq);
	goto err0;
}

void axxia_rio_port_irq_disable(struct rio_mport *mport)
{
	struct rio_priv *priv = mport->priv;
	int i;

	atomic_inc(&port_irq_disabled);
	/**
	 * Mask top level IRQs
	 */
	__rio_local_write_config_32(mport, RAB_INTR_ENAB_GNRL, 0);
	/**
	 * free registered handlers
	 */
	release_irq_handler(&priv->misc_irq);
	release_irq_handler(&priv->pw_irq);
	release_irq_handler(&priv->db_irq);
	for (i = 0; i < DME_MAX_OB_ENGINES; i++)
		release_irq_handler(&priv->ob_dme_irq[i]);
	for (i = 0; i < RIO_MAX_RX_MBOX; i++)
		release_irq_handler(&priv->ib_dme_irq[i]);
	release_irq_handler(&priv->apio_irq);
	release_irq_handler(&priv->rpio_irq);
}

int axxia_rio_pw_enable(struct rio_mport *mport, int enable)
{
	struct rio_priv *priv = mport->priv;
	int rc = 0;

	axxia_api_lock(priv);
	if (enable)
		rc = enable_pw(&priv->pw_irq);
	else
		release_irq_handler(&priv->pw_irq);
	axxia_api_unlock(priv);

	return rc;
}

/**
 * axxia_rio_doorbell_send - Send a doorbell message
 *
 * @mport: RapidIO master port info
 * @index: ID of RapidIO interface
 * @destid: Destination ID of target device
 * @data: 16-bit info field of RapidIO doorbell message
 *
 * Sends a doorbell message.
 *
 * Returns %0 on success or %-EINVAL on failure.
 *
 * API protected by spin lock in generic rio driver.
 */
int axxia_rio_doorbell_send(struct rio_mport *mport,
			    int index, u16 destid, u16 data)
{
	int db;
	u32 csr;

	for (db = 0; db < MAX_OB_DB; db++) {
		__rio_local_read_config_32(mport, RAB_OB_DB_CSR(db), &csr);
		if (OB_DB_STATUS(csr) == OB_DB_STATUS_DONE &&
		    OB_DB_STATUS(csr) != OB_DB_STATUS_RETRY) {

			csr = 0;
			csr |= OB_DB_DEST_ID(destid);
			csr |= OB_DB_PRIO(0x2); /* Good prio? */
			csr |= OB_DB_SEND;

			__rio_local_write_config_32(mport, RAB_OB_DB_INFO(db),
						    OB_DB_INFO(data));
			__rio_local_write_config_32(mport, RAB_OB_DB_CSR(db),
						    csr);
			break;
		}
	}
	if (db == MAX_OB_DB)
		return -EBUSY;

	return 0;
}

/************/
/* OUTBOUND */
/************/
/**
 * axxia_open_outb_mbox - Initialize AXXIA outbound mailbox
 * @mport: Master port implementing the outbound message unit
 * @dev_id: Device specific pointer to pass on event
 * @mboxDme: Mailbox to open
 * @entries: Number of entries in the outbound DME/mailbox ring for
 *           each letter
 *
 * Allocates and initializes descriptors.
 * We have N (e.g. 3) outbound mailboxes and M (e.g. 1024) message
 * descriptors.  The message descriptors are usable by inbound and
 * outbound message queues, at least until the point of binding.
 * Allocation/Distribution of message descriptors is flexible and
 * not restricted in any way other than that they must be uniquely
 * assigned/coherent to each mailbox/DME.
 *
 * Allocate memory for messages.
 * Each descriptor can hold a message of up to 4kB, though certain
 * DMEs or mailboxes may impose further limits on the size of the
 * messages.
 *
 * Returns %0 on success and %-EINVAL or %-ENOMEM on failure.
 */
int axxia_open_outb_mbox(
	struct rio_mport *mport,
	void *dev_id,
	int mboxDme,
	int entries,
	int prio)
{
	struct rio_priv *priv = mport->priv;
	int rc = 0;

	axxia_api_lock(priv);
	rc = open_outb_mbox(mport, dev_id, mboxDme, entries, prio);
	axxia_api_unlock(priv);

	return rc;
}

/**
 * axxia_close_outb_mbox - Shut down AXXIA outbound mailbox
 * @mport: Master port implementing the outbound message unit
 * @mboxDme: Mailbox to close
 *
 * Disables the outbound message unit, frees all buffers, and
 * frees any other resources.
 */
void axxia_close_outb_mbox(struct rio_mport *mport, int mboxDme)
{
	struct rio_priv *priv = mport->priv;

	if ((mboxDme < 0) ||
	    (mboxDme > (priv->numOutbDmes[0]+priv->numOutbDmes[0])))
		return;

	axxia_api_lock(priv);
	release_irq_handler(&priv->ob_dme_irq[mboxDme]);
	axxia_api_unlock(priv);

	return;
}

static struct rio_msg_desc *get_ob_desc(struct rio_mport *mport,
					struct rio_msg_dme *mb)
{
	int desc_num = mb->write_idx;
	struct rio_priv *priv = mport->priv;
	struct rio_msg_desc *desc = &mb->desc[desc_num];
	int nxt_write_idx = (mb->write_idx + 1) % mb->entries;
	u32 dw0;

	if ((nxt_write_idx != mb->last_compl_idx) &&
	    (mb->entries > (mb->entries_in_use + 1))) {
		if (!priv->internalDesc) {
			dw0 = *((u32 *)DESC_TABLE_W0_MEM(mb, desc->desc_no));
		} else {
			__rio_local_read_config_32(mport,
					   DESC_TABLE_W0(desc->desc_no),
					   &dw0);
		}
		if (!(dw0 & DME_DESC_DW0_VALID)) {
			mb->write_idx = nxt_write_idx;
			return desc;
		}
	}

	return NULL;
}

/**
 * axxia_add_outb_message - Add message to the AXXIA outbound message queue
 * --- Called in net core soft IRQ with local interrupts masked ---
 * --- And spin locked in master port net device handler        ---
 *
 * @mport: Master port with outbound message queue
 * @rdev: Target of outbound message
 * @mbox_dest: Destination mailbox
 * @letter: TID letter
 * @flags: 3 bit field,Critical Request Field[2] | Prio[1:0]
 * @buffer: Message to add to outbound queue
 * @len: Length of message
 *
 * Adds the @buffer message to the AXXIA outbound message queue.
 * Returns %0 on success or %-EINVAL on failure.
 */
int axxia_add_outb_message(struct rio_mport *mport, struct rio_dev *rdev,
			     int mbox_dest, int letter, int flags,
			     void *buffer, size_t len, void *cookie)
{
	int rc = 0, dme;
	u32 dw0, dw1, dme_ctrl;
	u16 destid = (rdev ? rdev->destid : mport->host_deviceid);
	struct rio_priv *priv = mport->priv;
	struct rio_msg_dme *mb = NULL;
	int buf_sz = 0;
	struct rio_msg_desc *desc;
	unsigned long iflags;

	if ((mbox_dest < 0)          ||
	    (mbox_dest >= RIO_MAX_TX_MBOX))
		return -EINVAL;

	dme = choose_ob_dme(priv, len, &mb, &buf_sz);
	if (dme < 0) {
		rc = dme;
		return rc;
	}

	if ((len < 8) || (len > buf_sz))
		return -EINVAL;

	mb = dme_get(mb);
	if (!mb)
		return -EINVAL;

	spin_lock_irqsave(&mb->lock, iflags);
	desc = get_ob_desc(mport, mb);
	if (!desc) {
		dev_dbg(priv->dev,
			"RIO: TX DMA descriptor ring exhausted\n");
		__ob_dme_event_dbg(priv, mb->dme_no,
				   1 << RIO_OB_DME_TX_PUSH_RING_FULL);
		rc = -EAGAIN;
		goto done;
	}
	__ob_dme_event_dbg(priv, mb->dme_no, 1 << RIO_OB_DME_TX_PUSH);
	desc->cookie = cookie;

	/* Copy and clear rest of buffer */
	memcpy(desc->msg_virt, buffer, len);
	if (len < (buf_sz - 4))
		memset(desc->msg_virt + len, 0, buf_sz - len);

	dw0 = DME_DESC_DW0_SRC_DST_ID(destid) |
		DME_DESC_DW0_EN_INT |
		DME_DESC_DW0_VALID;

	if (desc->last) /* (Re-)Make ring of descriptors */
		dw0 |= DME_DESC_DW0_NXT_DESC_VALID;

	dw1 = DME_DESC_DW1_PRIO(flags) |
		DME_DESC_DW1_CRF(flags) |
		DME_DESC_DW1_SEG_SIZE_256 |
		DME_DESC_DW1_SIZE(len) |
		DME_DESC_DW1_XMBOX(mbox_dest) |
		DME_DESC_DW1_MBOX(mbox_dest) |
		DME_DESC_DW1_LETTER(letter);

	if (!priv->internalDesc) {
		*((u32 *)DESC_TABLE_W1_MEM(mb, desc->desc_no)) = dw1;
		*((u32 *)DESC_TABLE_W0_MEM(mb, desc->desc_no)) = dw0;
	} else {
		__rio_local_write_config_32(mport,
					 DESC_TABLE_W1(desc->desc_no), dw1);
		__rio_local_write_config_32(mport,
					 DESC_TABLE_W0(desc->desc_no), dw0);
	}
	mb->entries_in_use++;

	/* Start / Wake up */
	__rio_local_read_config_32(mport, RAB_OB_DME_CTRL(mb->dme_no),
				   &dme_ctrl);
	dme_ctrl |= DME_WAKEUP | DME_ENABLE;
	__rio_local_write_config_32(mport, RAB_OB_DME_CTRL(mb->dme_no),
				    dme_ctrl);

done:
	spin_unlock_irqrestore(&mb->lock, iflags);
	dme_put(mb);
	return rc;
}

/**
 * axxia_open_inb_mbox - Initialize AXXIA inbound mailbox
 * @mport: Master port implementing the inbound message unit
 * @dev_id: Device specific pointer to pass on event
 * @mbox: Mailbox to open
 * @entries: Number of entries in the inbound mailbox ring
 *
 * Initializes buffer ring.  Set up descriptor ring and memory
 * for messages for all letters in the mailbox.
 * Returns %0 on success and %-EINVAL or %-ENOMEM on failure.
 */
int axxia_open_inb_mbox(struct rio_mport *mport, void *dev_id,
			int mbox, int entries)
{
	struct rio_priv *priv = mport->priv;
	int rc = 0;

	axxia_api_lock(priv);
	rc = open_inb_mbox(mport, dev_id, mbox, entries);
	axxia_api_unlock(priv);

	return rc;
}

/**
 * axxia_close_inb_mbox - Shut down AXXIA inbound mailbox
 * @mport: Master port implementing the inbound message unit
 * @mbox: Mailbox to close
 *
 * Disables the inbound message unit, free all buffers, and
 * frees resources.
 */
void axxia_close_inb_mbox(struct rio_mport *mport, int mbox)
{
	struct rio_priv *priv = mport->priv;

	if ((mbox < 0) || (mbox >= RIO_MAX_RX_MBOX))
		return;

	axxia_api_lock(priv);
	release_irq_handler(&priv->ib_dme_irq[mbox]);
	axxia_api_unlock(priv);

	return;
}

/**
 * axxia_add_inb_buffer - Add buffer to the AXXIA inbound message queue
 * @mport: Master port implementing the inbound message unit
 * @mbox: Inbound mailbox number
 * @buf: Buffer to add to inbound queue
 *
 * Adds the @buf buffer to the AXXIA inbound message queue.
 * Returns %0 on success or %-EINVAL on failure.
 */
int axxia_add_inb_buffer(struct rio_mport *mport, int mbox, void *buf)
{
	struct rio_priv *priv = mport->priv;
	struct rio_rx_mbox *mb;
	unsigned long iflags;
	int rc = 0;

	if ((mbox < 0) || (mbox >= RIO_MAX_RX_MBOX))
		return -EINVAL;

	mb = mbox_get(priv->ib_dme_irq[mbox].data);
	if (!mb)
		return -EINVAL;

	spin_lock_irqsave(&mb->lock, iflags);
	if (mb->virt_buffer[mb->last_rx_slot])
		goto busy;

	mb->virt_buffer[mb->last_rx_slot] = buf;
	mb->last_rx_slot = (mb->last_rx_slot + 1) % mb->ring_size;
done:
	spin_unlock_irqrestore(&mb->lock, iflags);
	mbox_put(mb);
	return rc;
busy:
	rc = -EBUSY;
	goto done;
}

/**
 * axxia_get_inb_message - Fetch an inbound message from the AXXIA
 *                         message unit
 * @mport: Master port implementing the inbound message unit
 * @mbox: Inbound mailbox number
 * @letter: Inbound mailbox letter
 * @sz: size of returned buffer
 *
 * Gets the next available inbound message from the inbound message queue.
 * A pointer to the message is returned on success or NULL on failure.
 */
void *axxia_get_inb_message(struct rio_mport *mport, int mbox, int letter,
			    int *sz, int *slot, u16 *destid)
{
	struct rio_priv *priv = mport->priv;
	struct rio_rx_mbox *mb;
	struct rio_msg_dme *me;
	unsigned long iflags;
	int numProc = 0;
	void *buf = NULL;

	if ((mbox < 0) || (mbox >= RIO_MAX_RX_MBOX))
		return ERR_PTR(-EINVAL);
	if (letter >= RIO_MSG_MAX_LETTER)
		return ERR_PTR(-EINVAL);

	mb = mbox_get(priv->ib_dme_irq[mbox].data);
	if (!mb)
		return ERR_PTR(-EINVAL);

	me = dme_get(mb->me[letter]);
	if (!me)
		return ERR_PTR(-EINVAL);

	spin_lock_irqsave(&mb->lock, iflags);

#ifdef OBSOLETE_BZ47185
	/* Make this conditional for AXM55xx??? */
	if (!in_interrupt() &&
	    !test_bit(RIO_IRQ_ACTIVE, &priv->ib_dme_irq[mbox].state)) {
		u32	intr;
		__rio_local_read_config_32(mport, RAB_INTR_ENAB_IDME, &intr);
		__rio_local_write_config_32(mport, RAB_INTR_ENAB_IDME,
					    intr & ~(1 << me->dme_no));
		ib_dme_irq_handler(&priv->ib_dme_irq[mbox], (1 << me->dme_no));
		__rio_local_write_config_32(mport, RAB_INTR_ENAB_IDME, intr);
	}
#endif /* OBSOLETE_BZ47185 */

	while (1) {
		struct rio_msg_desc *desc = &me->desc[me->read_idx];
		u32 dw0, dw1;

		buf = NULL;
		*sz = 0;
		if (!priv->internalDesc) {
			dw0 = *((u32 *)DESC_TABLE_W0_MEM(me, desc->desc_no));
			dw1 = *((u32 *)DESC_TABLE_W1_MEM(me, desc->desc_no));
		} else {
			__rio_local_read_config_32(mport,
					   DESC_TABLE_W0(desc->desc_no),
					   &dw0);
			__rio_local_read_config_32(mport,
					   DESC_TABLE_W1(desc->desc_no),
					   &dw1);
		}
		if ((dw0 & DME_DESC_DW0_ERROR_MASK) &&
		    (dw0 & DME_DESC_DW0_VALID)) {
			if (!priv->internalDesc) {
				*((u32 *)DESC_TABLE_W0_MEM(me,
					desc->desc_no)) =
					(dw0 & 0xff) | DME_DESC_DW0_VALID;
			} else {
				__rio_local_write_config_32(mport,
					DESC_TABLE_W0(desc->desc_no),
					(dw0 & 0xff) | DME_DESC_DW0_VALID);
			}
			me->read_idx = (me->read_idx + 1) % me->entries;
			__ib_dme_event_dbg(priv, me->dme_no,
					   1 << RIO_IB_DME_DESC_ERR);
			numProc++;
		} else if ((dw0 & DME_DESC_DW0_DONE) &&
			   (dw0 & DME_DESC_DW0_VALID)) {
			int seg = DME_DESC_DW1_SIZE_F(dw1);
			int buf_sz = DME_DESC_DW1_SIZE_SENT(seg);
			buf = mb->virt_buffer[mb->next_rx_slot];
			if (!buf)
				goto err;
			memcpy(buf, desc->msg_virt, buf_sz);
			mb->virt_buffer[mb->next_rx_slot] = NULL;
			if (!priv->internalDesc) {
				*((u32 *)DESC_TABLE_W0_MEM(me,
					desc->desc_no)) =
					(dw0 & 0xff) | DME_DESC_DW0_VALID;
			} else {
				__rio_local_write_config_32(mport,
					DESC_TABLE_W0(desc->desc_no),
					(dw0 & 0xff) | DME_DESC_DW0_VALID);
			}
			__ib_dme_event_dbg(priv, me->dme_no,
					   1 << RIO_IB_DME_RX_POP);
			*sz = buf_sz;
			*slot = me->read_idx;
			*destid = DME_DESC_DW0_GET_DST_ID(dw0);

#ifdef CONFIG_SRIO_IRQ_TIME
			if (atomic_read(&priv->ib_dme_irq[mbox].start_time)) {
				int add_time = 0;
				u32 *w1 = (u32 *)((char *)buf + 28);
				u32 *w2 = (u32 *)((char *)buf + 32);
				u32 magic = 0xc0cac01a;

				if (*w1 == magic && *w2 == magic)
					add_time = 1;

				if (add_time) {
					u64 *mp = (u64 *)((char *)buf + 40);
					*mp++ = me->stop_irq_tb;
					*mp++ = me->stop_thrd_tb;
					*mp++ = get_tb();
				}
				me->pkt++;
				me->bytes += buf_sz;
			}
#endif

			mb->next_rx_slot = (mb->next_rx_slot + 1) %
					    mb->ring_size;
			me->read_idx = (me->read_idx + 1) % me->entries;
			numProc++;
			goto done;
		} else {
			goto done;
		}
	}

done:
	/* Advance VALID bit to next entry */
	if (numProc > 0) {
		u32 dw0;
		int nxt_inval_desc = (me->last_invalid_desc + numProc) %
				     me->entries;
		if (!priv->internalDesc) {
			dw0 = *((u32 *)DESC_TABLE_W0_MEM(me, nxt_inval_desc));
			dw0 &= ~DME_DESC_DW0_VALID;
			*((u32 *)DESC_TABLE_W0_MEM(me, nxt_inval_desc)) = dw0;

			dw0 = *((u32 *)DESC_TABLE_W0_MEM(me,
						me->last_invalid_desc));
			dw0 |= DME_DESC_DW0_VALID;
			*((u32 *)DESC_TABLE_W0_MEM(me,
						me->last_invalid_desc)) = dw0;
		} else {
			__rio_local_read_config_32(mport,
					DESC_TABLE_W0(nxt_inval_desc),
					&dw0);
			dw0 &= ~DME_DESC_DW0_VALID;
			__rio_local_write_config_32(mport,
					DESC_TABLE_W0(nxt_inval_desc),
					dw0);
			__rio_local_read_config_32(mport,
					DESC_TABLE_W0(me->last_invalid_desc),
					&dw0);
			dw0 |= DME_DESC_DW0_VALID;
			__rio_local_write_config_32(mport,
					DESC_TABLE_W0(me->last_invalid_desc),
					dw0);
		}

		/* And re-awaken the DME */
		me->last_invalid_desc = nxt_inval_desc;
		__rio_local_read_config_32(mport, RAB_IB_DME_CTRL(me->dme_no),
					   &dw0);
		dw0 |= DME_WAKEUP | DME_ENABLE;
		__rio_local_write_config_32(mport, RAB_IB_DME_CTRL(me->dme_no),
					    dw0);
	}

	spin_unlock_irqrestore(&mb->lock, iflags);
	dme_put(me);
	mbox_put(mb);
	return buf;
err:
	__ib_dme_event_dbg(priv, me->dme_no, 1 << RIO_IB_DME_RX_VBUF_EMPTY);
	buf = ERR_PTR(-ENOMEM);
	goto done;
}

void axxia_rio_port_irq_init(struct rio_mport *mport)
{
	struct rio_priv *priv = mport->priv;
	int i;

	/**
	 * Port general error indications
	 */
	clear_bit(RIO_IRQ_ENABLED, &priv->misc_irq.state);
	priv->misc_irq.mport = mport;
	priv->misc_irq.irq_enab_reg_addr = RAB_INTR_ENAB_MISC;
	priv->misc_irq.irq_state_reg_addr = RAB_INTR_STAT_MISC;
	priv->misc_irq.irq_state_mask = AMST_INT | ASLV_INT |
					LINK_REQ_INT;
#if defined(CONFIG_AXXIA_RIO_STAT)
	priv->misc_irq.irq_state_mask |=
		GRIO_INT | LL_TL_INT | UNEXP_MSG_LOG |
		UNSP_RIO_REQ_INT | UNEXP_MSG_INT;
#endif
	priv->misc_irq.irq_state = 0;
	priv->misc_irq.thrd_irq_fn = misc_irq_handler;
	priv->misc_irq.data = NULL;
	priv->misc_irq.release_fn = NULL;

	/**
	 * Deadman Monitor status interrupt
	 */
	clear_bit(RIO_IRQ_ENABLED, &priv->linkdown_irq.state);
	priv->linkdown_irq.mport = mport;
	priv->linkdown_irq.irq_enab_reg_addr = 0;
	priv->linkdown_irq.irq_state_reg_addr = RAB_SRDS_STAT1;
	priv->linkdown_irq.irq_state_mask = RAB_SRDS_STAT1_LINKDOWN_INT;
	priv->linkdown_irq.irq_state = 0;
	priv->linkdown_irq.thrd_irq_fn = linkdown_irq_handler;
	priv->linkdown_irq.data = NULL;
	priv->linkdown_irq.release_fn = NULL;

	/**
	 * Port Write messages
	 */
	clear_bit(RIO_IRQ_ENABLED, &priv->pw_irq.state);
	priv->pw_irq.mport = mport;
	priv->pw_irq.irq_enab_reg_addr = RAB_INTR_ENAB_MISC;
	priv->pw_irq.irq_state_reg_addr = RAB_INTR_STAT_MISC;
	priv->pw_irq.irq_state_mask = PORT_WRITE_INT;
	priv->pw_irq.irq_state = 0;
	priv->pw_irq.thrd_irq_fn = pw_irq_handler;
	priv->pw_irq.data = NULL;
	priv->pw_irq.release_fn = disable_pw;

	/**
	 * Doorbells
	 */
	clear_bit(RIO_IRQ_ENABLED, &priv->db_irq.state);
	priv->db_irq.mport = mport;
	priv->db_irq.irq_enab_reg_addr = RAB_INTR_ENAB_MISC;
	priv->db_irq.irq_state_reg_addr = RAB_INTR_STAT_MISC;
	priv->db_irq.irq_state_mask = IB_DB_RCV_INT;
#if defined(CONFIG_AXXIA_RIO_STAT)
	priv->db_irq.irq_state_mask |= OB_DB_DONE_INT;
#endif
	priv->db_irq.irq_state = 0;
	priv->db_irq.thrd_irq_fn = db_irq_handler;
	priv->db_irq.data = NULL;
	priv->db_irq.release_fn = NULL;

	/**
	 * Outbound messages
	 */
	for (i = 0; i < DME_MAX_OB_ENGINES; i++) {
		clear_bit(RIO_IRQ_ENABLED, &priv->ob_dme_irq[i].state);
		priv->ob_dme_irq[i].mport = mport;
		priv->ob_dme_irq[i].irq_enab_reg_addr = RAB_INTR_ENAB_ODME;
		priv->ob_dme_irq[i].irq_state_reg_addr = RAB_INTR_STAT_ODME;
		priv->ob_dme_irq[i].irq_state_mask = (1 << i);
		priv->ob_dme_irq[i].irq_state = 0;
		priv->ob_dme_irq[i].thrd_irq_fn = ob_dme_irq_handler;
		priv->ob_dme_irq[i].data = NULL;
		priv->ob_dme_irq[i].release_fn = release_outb_mbox;
	}

	/**
	 * Inbound messages
	 */
	for (i = 0; i < RIO_MAX_RX_MBOX; i++) {
		clear_bit(RIO_IRQ_ENABLED, &priv->ib_dme_irq[i].state);
		priv->ib_dme_irq[i].mport = mport;
		priv->ib_dme_irq[i].irq_enab_reg_addr = RAB_INTR_ENAB_IDME;
		priv->ib_dme_irq[i].irq_state_reg_addr = RAB_INTR_STAT_IDME;
		priv->ib_dme_irq[i].irq_state_mask = 0; /*(0xf << (i * 4));*/
		priv->ib_dme_irq[i].irq_state = 0;
		priv->ib_dme_irq[i].thrd_irq_fn = ib_dme_irq_handler;
		priv->ib_dme_irq[i].data = NULL;
		priv->ib_dme_irq[i].release_fn = release_inb_mbox;
	}

	/**
	 * PIO
	 * Only when debug config
	 */
	clear_bit(RIO_IRQ_ENABLED, &priv->apio_irq.state);
	priv->apio_irq.mport = mport;
	priv->apio_irq.irq_enab_reg_addr = RAB_INTR_ENAB_APIO;
	priv->apio_irq.irq_state_reg_addr = RAB_INTR_STAT_APIO;
#if defined(CONFIG_AXXIA_RIO_STAT)
	priv->apio_irq.irq_state_mask = APIO_TRANS_FAILED;
#else
	priv->apio_irq.irq_state_mask = 0;
#endif
	priv->apio_irq.irq_state = 0;
	priv->apio_irq.thrd_irq_fn = apio_irq_handler;
	priv->apio_irq.data = NULL;
	priv->apio_irq.release_fn = NULL;

	clear_bit(RIO_IRQ_ENABLED, &priv->rpio_irq.state);
	priv->rpio_irq.mport = mport;
	priv->rpio_irq.irq_enab_reg_addr = RAB_INTR_ENAB_RPIO;
	priv->rpio_irq.irq_state_reg_addr = RAB_INTR_STAT_RPIO;
#if defined(CONFIG_AXXIA_RIO_STAT)
	priv->rpio_irq.irq_state_mask = RPIO_TRANS_FAILED;
#else
	priv->rpio_irq.irq_state_mask = 0;
#endif
	priv->rpio_irq.irq_state = 0;
	priv->rpio_irq.thrd_irq_fn = rpio_irq_handler;
	priv->rpio_irq.data = NULL;
	priv->rpio_irq.release_fn = NULL;
}

#if defined(CONFIG_RAPIDIO_HOTPLUG)
int axxia_rio_port_notify_cb(struct rio_mport *mport,
			       int enable,
			       void (*cb)(struct rio_mport *mport))
{
	struct rio_priv *priv = mport->priv;
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&priv->port_lock, flags);
	if (enable) {
		if (priv->port_notify_cb)
			rc = -EBUSY;
		else
			priv->port_notify_cb = cb;
	} else {
		if (priv->port_notify_cb != cb)
			rc = -EINVAL;
		else
			priv->port_notify_cb = NULL;
	}
	spin_unlock_irqrestore(&priv->port_lock, flags);

	return rc;
}

int axxia_rio_port_op_state(struct rio_mport *mport)
{
	u32 escsr;

	__rio_local_read_config_32(mport, RIO_ESCSR(priv->portNdx), &escsr);

	if (escsr & RIO_ESCSR_PO)
		return MPORT_STATE_OPERATIONAL;
	else
		return MPORT_STATE_DOWN;
}
#endif

int axxia_rio_apio_enable(struct rio_mport *mport, u32 mask, u32 bits)
{
	struct rio_priv *priv = mport->priv;
	int rc;

	axxia_api_lock(priv);
	rc = enable_apio(&priv->apio_irq, mask, bits);
	axxia_api_unlock(priv);

	return rc;
}
EXPORT_SYMBOL_GPL(axxia_rio_apio_enable);

int axxia_rio_apio_disable(struct rio_mport *mport)
{
	struct rio_priv *priv = mport->priv;

	axxia_api_lock(priv);
	release_irq_handler(&priv->apio_irq);
	axxia_api_unlock(priv);

	return 0;
}
EXPORT_SYMBOL_GPL(axxia_rio_apio_disable);

int axxia_rio_rpio_enable(struct rio_mport *mport, u32 mask, u32 bits)
{
	struct rio_priv *priv = mport->priv;
	int rc = 0;

	axxia_api_lock(priv);
	rc = enable_rpio(&priv->rpio_irq, mask, bits);
	axxia_api_unlock(priv);

	return rc;
}
EXPORT_SYMBOL_GPL(axxia_rio_rpio_enable);

int axxia_rio_rpio_disable(struct rio_mport *mport)
{
	struct rio_priv *priv = mport->priv;

	axxia_api_lock(priv);
	release_irq_handler(&priv->rpio_irq);
	axxia_api_unlock(priv);

	return 0;
}
EXPORT_SYMBOL_GPL(axxia_rio_rpio_disable);
