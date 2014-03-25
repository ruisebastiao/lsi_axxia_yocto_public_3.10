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

/*
** Debug Build Flags
**/
/* #define AXM55XX_OUTB_DME_BBS 1 */


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

/****************************************************************************
**
** Implementation Note:
**
** The Message DME registers lie within the fixed page block in the RAB SRIO
** Configuration memory.  Thus, all or almost all of its register accesses
** do not require use of the RAB memory paging register.  On the other hand,
** the Message descriptor registers for the ACP34xx platform do lie outside
** of the fixed page block.  For safety, we will direct all of the accesses
** to the Message descriptor registers (on the ACP34xx platform and the like),
** through the RIO mport's lcread and lcwrite interfaces which use a software
** spin lock to control access.
**
*****************************************************************************/


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
	if (misc_state & (UNEXP_MSG_INT |
			  LL_TL_INT |
			  GRIO_INT |
			  UNSP_RIO_REQ_INT |
			  LINK_REQ_INT)) {
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
		axxia_local_config_read(priv, RAB_OB_DB_CSR(db), &csr);

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
	if (ib_event & (1 << RIO_IB_DME_RX_PEND_SLEEP))
		__ib_dme_debug(priv, dme, RIO_IB_DME_RX_PEND_SLEEP);
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
	struct rio_priv *priv = mport->priv;

	atomic_inc(&hw_handler_calls);

	/**
	 * Get current interrupt state and clear latched state
	 * for interrupts handled by current thread.
	 */
	axxia_local_config_read(priv, h->irq_state_reg_addr, &h->irq_state);
	h->irq_state &= h->irq_state_mask;
	axxia_local_config_write(priv, h->irq_state_reg_addr, h->irq_state);

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

	if (test_and_set_bit(RIO_IRQ_ENABLED, &h->state))
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
		axxia_local_config_read(priv, h->irq_enab_reg_addr, &mask);
		mask |= h->irq_state_mask;
		axxia_local_config_write(priv, h->irq_state_reg_addr, mask);
		axxia_local_config_write(priv, h->irq_enab_reg_addr, mask);
	}

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

	if (test_and_clear_bit(RIO_IRQ_ENABLED, &h->state)) {
		axxia_local_config_read(priv, h->irq_enab_reg_addr, &mask);
		mask &= ~h->irq_state_mask;
		axxia_local_config_write(priv, h->irq_enab_reg_addr, mask);

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
	struct rio_priv *priv = mport->priv;
	u32 amast = 0;
	u32 aslv_state = 0;
	u32 escsr, iecsr;

	axxia_local_config_read(priv, RIO_ESCSR(priv->port_ndx), &escsr);
	axxia_local_config_read(priv, EPC_IECSR(priv->port_ndx), &iecsr);

	/* clear latched state indications */
	axxia_local_config_write(priv, RIO_ESCSR(priv->port_ndx),
				(escsr & RIO_EXCSR_WOLR));
	axxia_local_config_write(priv, EPC_IECSR(priv->port_ndx),
				(iecsr & EPC_IECSR_RETE));

#if defined(CONFIG_AXXIA_RIO_STAT)
	__add_event_dbg(priv, escsr, iecsr);
	__add_state_dbg(priv, escsr);
#endif

	if (misc_state & MISC_FATAL) {

		axxia_local_config_read(priv, RAB_AMAST_STAT, &amast);
		axxia_local_config_read(priv, RAB_ASLV_STAT_CMD,
					   &aslv_state);
		/* clear latched state */
		axxia_local_config_write(priv, RAB_AMAST_STAT, amast);
		axxia_local_config_write(priv, RAB_ASLV_STAT_CMD,
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
		u32 r0, r00, r1, r2, r3;

		__rio_local_read_config_32(mport, RIO_DID_CSR, &r1);
		__rio_local_read_config_32(mport, RIO_COMPONENT_TAG_CSR, &r2);
		__rio_local_read_config_32(mport, RIO_GCCSR, &r3);


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
		__rio_local_write_config_32(mport, RIO_GCCSR, r3);
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

		axxia_local_config_read(priv, RAB_RPIO_STAT, &rpio_stat);
		axxia_local_config_write(priv, RAB_RPIO_STAT, rpio_stat);
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

		axxia_local_config_read(priv, RAB_APIO_STAT, &apio_stat);
		axxia_local_config_write(priv, RAB_APIO_STAT, apio_stat);
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

	axxia_local_config_read(priv, RAB_IB_PW_CSR, &csr);
	noofpw = RAB_IB_PW_NUMWORDS(csr);
	dev_dbg(priv->dev, "%s: noofpw %d\n", __func__, noofpw);
	if (!(noofpw)) {
		__irq_dbg(priv, RIO_MISC_PW_SPURIOUS);
		return;
	}
	__irq_dbg(priv, RIO_MISC_PW);

	while (noofpw) {

read_buff:
		axxia_local_config_read(priv, RAB_IB_PW_DATA, &msg_word);
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

		axxia_local_config_read(priv, RAB_IB_PW_CSR, &csr);
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
		axxia_local_config_read(priv, RAB_IB_PW_DATA, &dummy);
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

	axxia_local_config_read(priv, RAB_IB_PW_CSR, &rval);
	rval |= RAB_IB_PW_EN;
	axxia_rio_flush_pw(mport, RAB_IB_PW_NUMWORDS(rval), pw_data);
	axxia_local_config_write(priv, RAB_IB_PW_CSR, rval);

	rc = alloc_irq_handler(h, pw_data, "rio-pw");
	if (rc)
		goto err;
	atomic_inc(&priv->api_user);
	return rc;

err:
	rval &= ~RAB_IB_PW_EN;
	axxia_local_config_write(priv, RAB_IB_PW_CSR, rval);
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

	axxia_local_config_read(priv, RAB_IB_PW_CSR, &rval);
	rval &= ~RAB_IB_PW_EN;
	axxia_local_config_write(priv, RAB_IB_PW_CSR, rval);
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

	axxia_local_config_read(priv, RAB_IB_DB_CSR, &csr);
	num_msg = IB_DB_CSR_NUM_MSG(csr);

	for (; num_msg; num_msg--) {
		axxia_local_config_read(priv, RAB_IB_DB_INFO, &info);
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
		for (i = 0, desc = me->desc; i < me->entries; i++, desc++)
			kfree(desc->msg_virt);
		kfree(me->desc);
	}

	kfree(me->descriptors);

	if (priv->intern_msg_desc) {
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
			    int *num_dmes,
			    int *dmes_in_use,
			    int *dmes)
{
	int i;
	for (i = 0; i < 2; i++) {
		if (dme_no < num_dmes[i]) {
			if (dmes[i] & (1 << dme_no)) {
				if (dmes_in_use[i] & (1 << dme_no))
					return -EBUSY;	/* Already allocated */
				return 0;
			}
		} else {
			dme_no -= num_dmes[i];
		}
	}

	return -ENXIO;	/* Not available */
}

/*
 * Enforce a DME 'choice' previously made
 */
static inline int select_dme(int dme_no,
			     int *num_dmes,
			     int *dmes_in_use,
			     int *dmes,
			     int value)
{
	int i;
	for (i = 0; i < 2; i++) {
		if (dme_no < num_dmes[i]) {
			dmes_in_use[i] &= ~(1 << dme_no);
			dmes_in_use[i] |= (value << dme_no);
			return 0;
		} else {
			dme_no -= num_dmes[i];
		}
	}

	return -ENXIO;	/* Not available */
}

static inline int choose_ob_dme(
	struct rio_priv	*priv,
	int mbox_dest,
	int buf_sz,
	struct rio_msg_dme **ob_dme)
{
	int  i, ndx, sz, min_entries = 0;
	int  dme_no = 0, ret_dme_no = -ENXIO;
	struct rio_irq_handler *h = NULL;
	struct rio_msg_dme *dme = NULL, *ret_dme = NULL;

	/* Multi-segment vs single-segment DMEs */
	ndx = RIO_MBOX_TO_IDX(mbox_dest);
	switch (ndx) {
	case 0:
		if ((priv->num_outb_dmes[0] == 0) || (priv->outb_dmes[0] == 0))
			return -ENXIO;
		break;
	case 1:
		if ((priv->num_outb_dmes[1] == 0) || (priv->outb_dmes[1] == 0))
			return -ENXIO;
		dme_no += priv->num_outb_dmes[0];
		break;
	default:
		dev_err(priv->dev, "Attempt to select unknown OB DME type!\n");
		return -ENXIO;
	}

	/* Find one with fewest entries, or sufficient free entries */
	for (i = 0; i < priv->num_outb_dmes[ndx]; i++, dme_no++) {
		sz = RIO_OUTB_DME_TO_BUF_SIZE(priv, dme_no);

		if (sz > buf_sz)
			continue;

		h = &priv->ob_dme_irq[dme_no];
		dme = h->data;

		if (dme == NULL) {
			(*ob_dme) = NULL;
			return dme_no;
		} else if (dme->entries < min_entries) {
			min_entries = dme->entries;
			ret_dme = dme;
			ret_dme_no = dme_no;
		} else if (min_entries == 0) {
			min_entries = dme->entries;
			ret_dme = dme;
			ret_dme_no = dme_no;
		}
	}

	(*ob_dme) = ret_dme;
	return ret_dme_no;
}

static void release_mbox(struct kref *kref)
{
	struct rio_rx_mbox *mb = container_of(kref, struct rio_rx_mbox, kref);
	struct rio_priv *priv = mb->mport->priv;
	int letter;

	/* Quickly disable the engines */
	for (letter = 0; letter < RIO_MSG_MAX_LETTER; letter++) {
		if (mb->me[letter])
			axxia_local_config_write(priv,
				   RAB_IB_DME_CTRL(mb->me[letter]->dme_no), 0);
	}

	/* And then release the remaining resources */
	for (letter = 0; letter < RIO_MSG_MAX_LETTER; letter++) {
		if (mb->me[letter]) {
			dme_put(mb->me[letter]);
			select_dme(mb->me[letter]->dme_no,
					&priv->num_inb_dmes[0],
					&priv->inb_dmes_in_use[0],
					&priv->inb_dmes[0], 0);
		}
	}

	priv->ib_dme_irq[mb->mbox_no].irq_state_mask = 0;

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

static int alloc_msg_descriptors(struct rio_mport *mport,
				  struct resource *dres,
				  int buf_sz,
				  int entries,
				  int need_to_init,
				  struct rio_msg_desc **desc,
				  struct rio_desc **descriptors)
{
	struct rio_priv *priv = mport->priv;
	struct rio_msg_desc *rdesc = NULL, *idesc;
	struct rio_desc *rdescriptors = NULL;
	int i;

	if (priv->intern_msg_desc) {
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

	rdesc = kzalloc(sizeof(struct rio_msg_desc) * entries, GFP_KERNEL);
	if (rdesc == NULL)
		goto err;
	rdescriptors = kzalloc(sizeof(struct rio_desc) * entries, GFP_KERNEL);
	if (rdescriptors == NULL)
		goto err;

	for (i = 0, idesc = rdesc; i < need_to_init; i++, idesc++) {
		idesc->msg_virt = kzalloc(buf_sz, GFP_KERNEL);
		if (!idesc->msg_virt)
			goto err;
		idesc->msg_phys = virt_to_phys(idesc->msg_virt);
		clear_bit(RIO_DESC_USED, &idesc->state);
		idesc->desc_no = dres->start + i;
	}

	for (; i < entries; i++, idesc++) {
		clear_bit(RIO_DESC_USED, &idesc->state);
		idesc->desc_no = dres->start + i;
	}

	idesc--;
	idesc->last = 1;

	(*desc) = rdesc;
	(*descriptors) = rdescriptors;

	return 0;

err:
	kfree(rdesc);
	kfree(rdescriptors);
	return -ENOMEM;
}

static struct rio_msg_dme *alloc_message_engine(struct rio_mport *mport,
						int dme_no, void *dev_id,
						int buf_sz, int entries)
{
	struct rio_priv *priv = mport->priv;
	struct rio_msg_dme *me = kzalloc(sizeof(struct rio_msg_dme),
					 GFP_KERNEL);
	int rc = 0;

	if (!me)
		return ERR_PTR(-ENOMEM);

	memset(me, 0, sizeof(struct rio_msg_dme));

	kref_init(&me->kref);
	spin_lock_init(&me->lock);
	me->priv = priv;
	me->sz = buf_sz;

	rc = alloc_msg_descriptors(mport, &me->dres, buf_sz, entries,
				entries, &me->desc, &me->descriptors);
	if (rc < 0)
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

	return me;

err:
	dme_put(me);
	return ERR_PTR(rc);
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
 * HW descriptor fetch and update may be out of order.
 * Check state of all used descriptors and take care to not fall into
 * any of the traps that come with this design:
 *
 * Due to this (possibly) out of order execution in the HW, SW ack of
 * descriptors must be done atomically, re-enabling descriptors with
 * completed transactions while processing finished transactions may
 * break the ring and leave the DMA engine in a state where it doesn't
 * process new inserted requests.
 */
static void ob_dme_irq_handler(struct rio_irq_handler *h, u32 state)
{
	struct rio_mport *mport = h->mport;
	struct rio_priv *priv = mport->priv;
	struct rio_msg_dme *dme = h->data;
	u32 dme_stat, dw0, dme_no = 31 - CNTLZW(state);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&dme->lock, flags);

	/**
	 * Clear latched state
	 */
	axxia_local_config_read(priv, RAB_OB_DME_STAT(dme_no), &dme_stat);
	axxia_local_config_write(priv, RAB_OB_DME_STAT(dme_no), dme_stat);
	__ob_dme_dbg(priv, dme_stat);

#ifdef OBSOLETE_47417
	/**
	 * Try to kick back some life in the HW if it is un-responsive
	 */
	axxia_local_config_read(priv, RAB_OB_DME_CTRL(dme_no), &dme_stat);
	dme_stat |= DME_WAKEUP | DME_ENABLE;
	axxia_local_config_write(priv, RAB_OB_DME_CTRL(dme_no), dme_stat);
#endif /* OBSOLETE_47417 */

	/**
	 * Process all completed transactions
	 */
	for (i = 0; i < dme->entries; i++) {
		struct rio_msg_desc *desc = &dme->desc[i];

		if (dme->last_compl_idx != desc->desc_no)
			continue;

		if (!priv->intern_msg_desc) {
			dw0 = *((u32 *)DESC_TABLE_W0_MEM(dme, desc->desc_no));
		} else {
			__rio_local_read_config_32(mport,
					DESC_TABLE_W0(desc->desc_no), &dw0);
		}

		if ((dw0 & DME_DESC_DW0_VALID) &&
		    (dw0 & DME_DESC_DW0_READY_MASK)) {
			if (!priv->intern_msg_desc) {
				*((u32 *)DESC_TABLE_W0_MEM(dme, desc->desc_no))
					= dw0 & DME_DESC_DW0_NXT_DESC_VALID;
			} else {
				__rio_local_write_config_32(mport,
					DESC_TABLE_W0(desc->desc_no),
					dw0 & DME_DESC_DW0_NXT_DESC_VALID);
			}
			__ob_dme_dw_dbg(priv, dw0);

			dme->entries_in_use--;
			dme->last_compl_idx = (dme->last_compl_idx + 1) %
						dme->entries;

			/**
			* UP-call to net device handler
			*/
			if (mport->outb_msg[dme_no].mcback) {
				__ob_dme_event_dbg(priv, dme_no,
						1 << RIO_OB_DME_TX_DESC_READY);
				mport->outb_msg[dme_no].mcback(mport,
							dme->dev_id,
							dme_no,
							i,
							desc->cookie);
			}
		}
	}

	spin_unlock_irqrestore(&dme->lock, flags);
}

/**
 * open_outb_mbox - Initialize AXXIA outbound mailbox
 *
 * @mport: Master port implementing the outbound message unit
 * @dev_id: Device specific pointer to pass on event
 * @mbox_id: Mailbox to open
 * @entries: Number of entries in the outbound mailbox ring for each letter
 * @prio: 0..3, higher number -> lower priority.
 *
 * Caller must hold RAB lock
 * If the specified mbox DME has already been opened/reserved, then we just
 * abort out of this operation with "busy", and without changing resource
 * allocation for the mbox DME.
 *
 * To reduce down time (and some programming effort) for the outbound DMEs,
 * we keep and resuse the descriptors and buffers allocated to previously
 * opened and closed outbound mailboxes.
 *
 * Returns:
 * %0 if successful
 * %-EINVAL if an argument is invalid
 * %-ENOMEM if unable to allocate sufficient memory
 * %-ENODEV if unable to find a DME matching the input arguments
 */
static int open_outb_mbox(struct rio_mport *mport, void *dev_id, int mbox_id,
			  int entries, int prio)
{
	int  rc = 0;
	int  dme_no, buf_sz = 0;
	int  have = 0, need = 0, to_init = 0;
	int  to_ring = 0, from_free = 0;
	int  know_need = 0, new_desc = 0;
	struct rio_priv *priv = mport->priv;
	struct rio_tx_mbox *mb = &priv->ob_mbox[mbox_id];
	struct rio_msg_dme *me = NULL;
	struct rio_msg_desc *desc = NULL;
	struct rio_desc *descriptors = NULL;
	struct rio_irq_handler *h = NULL;
	unsigned long iflags0, iflags1;

	if ((mbox_id < 0) || (mbox_id > RIO_MAX_TX_MBOX) ||
	    (entries < 2) || (entries > priv->desc_max_entries))
		return -EINVAL;

	/*
	** Spin lock versus the ob_dme_irq_handler ???
	*/
	spin_lock_irqsave(&mb->lock, iflags0);

	if (test_bit(RIO_DME_OPEN, &mb->state)) {
		spin_unlock_irqrestore(&mb->lock, iflags0);
		return -EINVAL;
	}

	/*
	** Pick the OB DME that we will use for this mailbox
	*/
	if (!test_bit(RIO_DME_MAPPED, &mb->state)) {
		buf_sz = RIO_MBOX_TO_BUF_SIZE(mbox_id);

		dme_no = choose_ob_dme(priv, mbox_id, buf_sz, &me);
		if (dme_no < 0) {
			spin_unlock_irqrestore(&mb->lock, iflags0);
			rc = dme_no;
			goto err2;
		}

		h = &priv->ob_dme_irq[dme_no];

		if (!test_bit(RIO_IRQ_ENABLED, &h->state)) {
			me = alloc_message_engine(mport,
						dme_no,
						dev_id,
						buf_sz,
						entries);
			if (IS_ERR(me)) {
				spin_unlock_irqrestore(&mb->lock, iflags0);
				rc = PTR_ERR(me);
				goto err2;
			}

			need = 0;
			have = entries;
			to_init = entries;
			know_need = 1;
			new_desc = 1;
			to_ring = entries;
			from_free = 0;

		} else {
			test_and_set_bit(RIO_IRQ_ENABLED, &mb->state);
		}
	} else {
		dme_no = mb->dme_no;
		h = &priv->ob_dme_irq[dme_no];
		me = mb->me;
	}

	spin_lock_irqsave(&me->lock, iflags1);

	mb->mport = mport;
	mb->mbox_no = mbox_id;
	mb->dme_no = dme_no;
	mb->me = me;
	mb->ring_size = entries;
	test_and_set_bit(RIO_DME_MAPPED, &mb->state);

	/**
	 * Figure out whether we can reuse the 'virtual' descriptors
	 * from a previously closed mailbox.
	 */
	if (!know_need) {
		if (priv->ob_dme_shared[dme_no].ring_size_free >= entries) {
			have = entries;
			need = 0;
			to_init = 0;
			to_ring = 0;
			from_free = entries;
		} else {
			have = priv->ob_dme_shared[dme_no].ring_size_free;
			need = entries - have;
			to_init = need;
			to_ring = need;
			from_free =
				priv->ob_dme_shared[dme_no].ring_size_free;
		}
	}

	/**
	 * Allocate and initialize new 'virtual' descriptors that we need
	 */
	if (need) {
		rc = alloc_msg_descriptors(mport, &me->dres, buf_sz,
					me->entries + need, need,
					&desc, &descriptors);
		if (rc < 0) {
			rc = -ENOMEM;
			goto err;
		}

		new_desc = 1;

		memcpy(&desc[need], me->desc,
			sizeof(struct rio_msg_desc) * me->entries);
		memcpy(&descriptors[need], me->descriptors,
			sizeof(struct rio_desc) * me->entries);

		me->entries += need;

		kfree(me->desc);
		me->desc = desc;

		kfree(me->descriptors);
		me->descriptors = descriptors;
	} else {
		desc = me->desc;
		descriptors = me->descriptors;
	}

	if (new_desc) {
		u32  dme_stat, dme_ctrl, wait = 0;
		u32 dw0, dw1, dw2, dw3, desc_addr;
		u64  desc_chn_start = 0;
		int  i;

		do {
			axxia_local_config_read(priv,
						RAB_OB_DME_STAT(dme_no),
						&dme_stat);
			if (wait++ > 100) {
				rc = -EBUSY;
				goto err;
			}
		} while (dme_stat & OB_DME_STAT_TRANS_PEND);

		for (i = 0, desc = me->desc; i < to_init; i++, desc++) {
			dw0 = 0;
			if (!priv->intern_msg_desc) {
#ifdef AXM55XX_OUTB_DME_BBS
				dw1 = (u32)(desc->msg_phys >> 11) & 0x1fe00000;
				dw2 = (u32)(desc->msg_phys >>  0) & 0x3fffffff;
#else
				dw1 = 0;
				dw2 = (u32)(desc->msg_phys >>  8) & 0x3fffffff;
#endif
				*((u32 *)DESC_TABLE_W0_MEM(me, desc->desc_no))
					= dw0;
				*((u32 *)DESC_TABLE_W1_MEM(me, desc->desc_no))
					= dw1;
				*((u32 *)DESC_TABLE_W2_MEM(me, desc->desc_no))
					= dw2;
				*((u32 *)DESC_TABLE_W3_MEM(me, desc->desc_no))
					= 0;
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

		for (; i < me->entries; i++, desc++)
			desc->desc_no = me->dres.start + i;

		/**
		* Last descriptor - make ring.
		* Next desc table entry -> dw2.First desc address[37:36]
		*                       -> dw3.First desc address[35:4].
		* (desc_base + 0x10 * nr)
		*/
		desc--;
		dw0 |= DME_DESC_DW0_NXT_DESC_VALID;
		if (!priv->intern_msg_desc) {
			desc_chn_start =
				(uintptr_t)virt_to_phys(me->descriptors);

			dw2  = *((u32 *)DESC_TABLE_W2_MEM(me, desc->desc_no));
			dw2 |= (desc_chn_start >> 4) & 0xc0000000;
			dw3  = desc_chn_start >> 4;
			*((u32 *)DESC_TABLE_W0_MEM(me, desc->desc_no)) = dw0;
			*((u32 *)DESC_TABLE_W2_MEM(me, desc->desc_no)) = dw2;
			*((u32 *)DESC_TABLE_W3_MEM(me, desc->desc_no)) = dw3;
		} else {
			desc_chn_start = DESC_TABLE_W0(me->dres.start);

			__rio_local_read_config_32(mport,
					DESC_TABLE_W2(desc->desc_no), &dw2);
			dw2 |= ((desc_chn_start >> 8) & 0xc0000000);
			dw3  = 0;
			__rio_local_write_config_32(mport,
					DESC_TABLE_W0(desc->desc_no), dw0);
			__rio_local_write_config_32(mport,
					DESC_TABLE_W2(desc->desc_no), dw2);
			__rio_local_write_config_32(mport,
					DESC_TABLE_W3(desc->desc_no), dw3);
		}

		/**
		 * (Re-)Setup the DME chain control and chain start address
		 */
		dme_ctrl  = (prio & 0x3) << 4;
		dme_ctrl |= (u32)((desc_chn_start >> 6) & 0xc0000000);
		desc_addr  = (u32)desc_chn_start >> 4;
		axxia_local_config_write(priv, RAB_OB_DME_DESC_ADDR(dme_no),
					desc_addr);
		axxia_local_config_write(priv, RAB_OB_DME_CTRL(dme_no),
					dme_ctrl);
	}

	/**
	 * Create irq handler and enable MBOX DME Engine irq
	 */
	if (!test_bit(RIO_IRQ_ENABLED, &mb->state)) {
		struct rio_irq_handler *h = NULL;

		h = &priv->ob_dme_irq[dme_no];

		sprintf(me->name, "obmb-%d", dme_no);
		rc = alloc_irq_handler(h, me, me->name);
		if (rc)
			goto err;

		select_dme(dme_no, &priv->num_outb_dmes[0],
			&priv->outb_dmes_in_use[0], &priv->outb_dmes[0], 1);

		test_and_set_bit(RIO_IRQ_ENABLED, &mb->state);
	}

	/**
	 * Finish updating the mailbox and DME state before we go
	 */
	test_and_set_bit(RIO_DME_OPEN, &mb->state);

	priv->ob_dme_shared[dme_no].ring_size += to_ring;
	priv->ob_dme_shared[dme_no].ring_size_free -= from_free;

	/**
	 * Endit
	 */
	spin_unlock_irqrestore(&me->lock, iflags1);
	spin_unlock_irqrestore(&mb->lock, iflags0);
	atomic_inc(&priv->api_user);
	return 0;

err:
	spin_unlock_irqrestore(&me->lock, iflags1);
err2:
	spin_unlock_irqrestore(&mb->lock, iflags0);
	/* dme_put(me); */
	return rc;
}


/**
 * release_outb_dme - Close AXXIA outbound DME engine structures
 * @mport: Master port implementing the outbound message unit
 * @mbox: Mailbox to close
 *
 * Caller must hold RAB lock
 * Release all resources i.e. DMEs, descriptors, buffers, and so on.
 */

static void release_outb_dme(struct rio_irq_handler *h)
{
	struct rio_mport *mport = h->mport;
	struct rio_priv *priv = mport->priv;
	struct rio_msg_dme *me = h->data;

	axxia_local_config_write(priv, RAB_OB_DME_CTRL(me->dme_no), 0);

	select_dme(me->dme_no, &priv->num_outb_dmes[0],
		   &priv->outb_dmes_in_use[0], &priv->outb_dmes[0], 0);

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
	unsigned long iflags;

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

		spin_lock_irqsave(&me->lock, iflags);

		/**
		 * Get and clear latched state
		 */
		axxia_local_config_read(priv,
					   RAB_IB_DME_STAT(dme_no), &dme_stat);
		axxia_local_config_write(priv,
					    RAB_IB_DME_STAT(dme_no), dme_stat);
		__ib_dme_dbg(priv, dme_stat);
#ifdef CONFIG_SRIO_IRQ_TIME
		{
			struct rio_irq_handler *h_n;
			h_n = &priv->ib_dme_irq[mbox_no];

			if (atomic_read(&h_n->start_time)) {
				if (me->pkt == 0) {
					me->start_irq_tb = h_n->irq_tb;
					me->start_thrd_tb = h_n->thrd_tb;
				}
				me->stop_irq_tb = h_n->irq_tb;
				me->stop_thrd_tb = h_n->thrd_tb;
				if ((h_n->thrd_tb - h_n->irq_tb) > me->max_lat)
					me->max_lat = h_n->thrd_tb -
							h_n->irq_tb;
				if ((h_n->thrd_tb - h_n->irq_tb) < me->min_lat)
					me->min_lat = h_n->thrd_tb -
							h_n->irq_tb;
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

			if (!priv->intern_msg_desc) {
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
				if (!priv->intern_msg_desc) {
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

			__ib_dme_event_dbg(priv, dme_no,
					   1 << RIO_IB_DME_RX_SLEEP);
			for (i = 0, desc = me->desc; i < me->entries;
				 i++, desc++) {

				if (!priv->intern_msg_desc) {
					dw0 = *((u32 *)DESC_TABLE_W0_MEM(me,
							      desc->desc_no));
				} else {
					__rio_local_read_config_32(mport,
						DESC_TABLE_W0(desc->desc_no),
						&dw0);
				}
				if (!(dw0 & DME_DESC_DW0_VALID)) {
					__ib_dme_event_dbg(priv, dme_no,
						1 << RIO_IB_DME_RX_PEND_SLEEP);
				}
			}

			__ib_dme_event_dbg(priv, dme_no,
					   1 << RIO_IB_DME_RX_WAKEUP);
			dme_ctrl = (mbox_no & 0x3f) << 6;
			dme_ctrl |= letter << 4;
			dme_ctrl |= DME_WAKEUP;
			dme_ctrl |= DME_ENABLE;
			axxia_local_config_write(priv,
				 RAB_IB_DME_CTRL(dme_no), dme_ctrl);
		}
#endif /* OBSOLETE_BZ47185 */

		spin_unlock_irqrestore(&me->lock, iflags);
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
	int rc, buf_sz;
	u32 irq_state_mask = 0;
	u32 dme_ctrl;
	struct rio_rx_mbox *mb;

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
		int dme_no = 0;
		struct rio_msg_dme *me = NULL;
		struct rio_msg_desc *desc;
		u32 dw0, dw1, dw2, dw3;
		u64 desc_chn_start, desc_addr;
		u32 dme_stat, wait = 0;
		u32 buffer_size = (buf_sz > 256 ? 3 : 0);

		/* Search for a free DME, so we can more efficiently map
		 * them to the all of the mbox||letter combinations. */
		for (i = 0, rc = -1;
		     i < (priv->num_inb_dmes[0]+priv->num_inb_dmes[1]);
		     i++) {
			rc = check_dme(i, &priv->num_inb_dmes[0],
				&priv->inb_dmes_in_use[0], &priv->inb_dmes[0]);
			if (rc == 0) {
				dme_no = i;
				break;
			}
		}
		if (rc < 0)
			return rc;

		me = alloc_message_engine(mport,
					  dme_no,
					  dev_id,
					  buf_sz,
					  entries);
		if (IS_ERR(me)) {
			rc = PTR_ERR(me);
			goto err;
		}

		irq_state_mask |= (1 << dme_no);

		do {
			axxia_local_config_read(priv,
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
		dw1 = DME_DESC_DW1_XMBOX(mbox) |
		      DME_DESC_DW1_MBOX(mbox)  |
		      DME_DESC_DW1_LETTER(letter);
		dw3 = 0;		/* 0 means, next contiguous addr
					 * Also next desc valid bit in dw0
					 * must be zero. */
		for (i = 0, desc = me->desc; i < entries; i++, desc++) {
			if (!priv->intern_msg_desc) {
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
		if (!priv->intern_msg_desc) {
			desc_chn_start =
				(uintptr_t)virt_to_phys(me->descriptors);

			dw2  = *((u32 *)DESC_TABLE_W2_MEM(me, desc->desc_no));
			dw2 |= (desc_chn_start >> 4) & 0xc0000000;
			dw3  = desc_chn_start >> 4;
			*((u32 *)DESC_TABLE_W0_MEM(me, desc->desc_no)) = dw0;
			*((u32 *)DESC_TABLE_W2_MEM(me, desc->desc_no)) = dw2;
			*((u32 *)DESC_TABLE_W3_MEM(me, desc->desc_no)) = dw3;
		} else {
			desc_chn_start = DESC_TABLE_W0(me->dres.start);

			__rio_local_read_config_32(mport,
					    DESC_TABLE_W2(desc->desc_no),
					    &dw2);
			dw3  = 0;
			dw2 |= ((desc_chn_start >> 8) & 0xc0000000);
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
		dme_ctrl = RAB_IB_DME_CTRL_XMBOX(mbox)    |
			   RAB_IB_DME_CTRL_MBOX(mbox)     |
			   RAB_IB_DME_CTRL_LETTER(letter) |
			   DME_WAKEUP                     |
			   DME_ENABLE;
		dme_ctrl |= (u32)((desc_chn_start >> 6) & 0xc0000000);
		desc_addr  = (u32)desc_chn_start >> 4;
		axxia_local_config_write(priv,
					RAB_IB_DME_DESC_ADDR(dme_no),
					desc_addr);
		axxia_local_config_write(priv,
					RAB_IB_DME_CTRL(dme_no), dme_ctrl);

		select_dme(dme_no, &priv->num_inb_dmes[0],
			&priv->inb_dmes_in_use[0], &priv->inb_dmes[0], 1);
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
		axxia_local_config_read(priv, RAB_INTR_STAT_MISC, &state);
		axxia_local_config_write(priv, RAB_INTR_STAT_MISC, state);
		/* Outbound Message Engine */
		axxia_local_config_read(priv, RAB_INTR_STAT_ODME, &state);
		axxia_local_config_write(priv, RAB_INTR_STAT_ODME , state);
		/* Inbound Message Engine */
		axxia_local_config_read(priv, RAB_INTR_STAT_IDME, &state);
		axxia_local_config_write(priv, RAB_INTR_STAT_IDME, state);
		/* Axxi Bus to RIO Events */
		axxia_local_config_read(priv, RAB_INTR_STAT_APIO, &state);
		axxia_local_config_write(priv, RAB_INTR_STAT_APIO, state);
		/* RIO to Axxia Bus Events */
		axxia_local_config_read(priv, RAB_INTR_STAT_RPIO, &state);
		axxia_local_config_write(priv, RAB_INTR_STAT_RPIO, state);
	}

	/* Master Port state */
	axxia_local_config_read(priv, RIO_ESCSR(priv->port_ndx), &escsr);
	axxia_local_config_read(priv, EPC_IECSR(priv->port_ndx), &iecsr);

	axxia_local_config_write(priv, RIO_ESCSR(priv->port_ndx),
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
	axxia_local_config_write(priv, RAB_INTR_ENAB_GNRL,
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
	axxia_local_config_write(priv, RAB_INTR_ENAB_GNRL, 0);
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
	unsigned long lflags;
	int rc = 0;

	spin_lock_irqsave(&priv->api_lock, lflags);
	if (enable)
		rc = enable_pw(&priv->pw_irq);
	else
		release_irq_handler(&priv->pw_irq);
	spin_unlock_irqrestore(&priv->api_lock, lflags);

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
	struct rio_priv *priv = mport->priv;
	int db;
	u32 csr;

	for (db = 0; db < MAX_OB_DB; db++) {
		axxia_local_config_read(priv, RAB_OB_DB_CSR(db), &csr);
		if (OB_DB_STATUS(csr) == OB_DB_STATUS_DONE &&
		    OB_DB_STATUS(csr) != OB_DB_STATUS_RETRY) {

			csr = 0;
			csr |= OB_DB_DEST_ID(destid);
			csr |= OB_DB_PRIO(0x2); /* Good prio? */
			csr |= OB_DB_SEND;

			axxia_local_config_write(priv, RAB_OB_DB_INFO(db),
						    OB_DB_INFO(data));
			axxia_local_config_write(priv, RAB_OB_DB_CSR(db),
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
 * @mbox_dme: Mailbox to open
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
	int mbox_dme,
	int entries,
	int prio)
{
	struct rio_priv *priv = mport->priv;
	unsigned long lflags;
	int rc = 0;

	spin_lock_irqsave(&priv->api_lock, lflags);
	rc = open_outb_mbox(mport, dev_id, mbox_dme, entries, prio);
	spin_unlock_irqrestore(&priv->api_lock, lflags);

	return rc;
}

/**
 * axxia_close_outb_mbox - Shut down AXXIA outbound mailbox
 *
 * @mport: Master port implementing the outbound message unit
 * @mbox_id: Mailbox to close
 *
 * Disables the outbound message unit, frees all buffers, and
 * frees any other resources.
 */
void axxia_close_outb_mbox(struct rio_mport *mport, int mbox_id)
{
	struct rio_priv *priv = mport->priv;
	struct rio_tx_mbox *mb = &priv->ob_mbox[mbox_id];
	unsigned long iflags0, iflags1;
	int dme_no;

	if ((mbox_id < 0) ||
	    (mbox_id > RIO_MAX_TX_MBOX) ||
	    (!test_bit(RIO_DME_OPEN, &priv->ob_mbox[mbox_id].state)))
		return;

	spin_lock_irqsave(&priv->api_lock, iflags0);
	spin_lock_irqsave(&mb->lock, iflags1);

	/* release_irq_handler(&priv->ob_dme_irq[mboxDme]); */

	dme_no = priv->ob_mbox[mbox_id].dme_no;
	priv->ob_dme_shared[dme_no].ring_size_free +=
		priv->ob_mbox[mbox_id].ring_size;
	priv->ob_mbox[mbox_id].ring_size = 0;
	clear_bit(RIO_DME_OPEN, &priv->ob_mbox[mbox_id].state);

	spin_unlock_irqrestore(&mb->lock, iflags1);
	spin_unlock_irqrestore(&priv->api_lock, iflags0);

	return;
}

static inline struct rio_msg_desc *get_ob_desc(struct rio_mport *mport,
						struct rio_msg_dme *mb)
{
	int desc_num = mb->write_idx;
	struct rio_priv *priv = mport->priv;
	struct rio_msg_desc *desc = &mb->desc[desc_num];
	int nxt_write_idx = (mb->write_idx + 1) % mb->entries;
	u32 dw0;

	if ((nxt_write_idx != mb->last_compl_idx) &&
	    (mb->entries > (mb->entries_in_use + 1))) {
		if (!priv->intern_msg_desc) {
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
 * Returns %0 on success
 *         %-EBUSY  on temporarily unavailable resource failure e.g. such
 *                     as waiting for an open entry in the outbound DME
 *                     descriptor chain
 *         %-EAGAIN on another kind of temporarily unavailable resource
 *                     failure
 *         %-EINVAL on invalid argument failure.
 *         %-ENODEV on unavailable resource failure e.g. no outbound DME
 *                     open that matches the kind of destination mailbox
 *         %-ENXIO  on incompatible argument failure e.g. trying to open
 *                     a single-segment mbox when none are available on
 *                     the platform
 */
int axxia_add_outb_message(struct rio_mport *mport, struct rio_dev *rdev,
			     int mbox_dest, int letter, int flags,
			     void *buffer, size_t len, void *cookie)
{
	int rc = 0;
	int buf_sz = 0;
	u32 dw0, dw1, dme_ctrl;
	u16 destid = (rdev ? rdev->destid : mport->host_deviceid);
	struct rio_priv *priv = mport->priv;
	struct rio_msg_dme *me = priv->ob_mbox[mbox_dest].me;
	struct rio_msg_desc *desc;
	unsigned long iflags;

	if ((mbox_dest < 0)                   ||
	    (mbox_dest >= RIO_MAX_TX_MBOX)    ||
	    (letter    <  0)                  ||
	    (letter    >= RIO_MSG_MAX_LETTER) ||
	    (buffer    == NULL))
		return -EINVAL;

	/* Should we have another spin lock nesting for the mailbox here?
	** Depends upon the frequency of mailbox open/close.
	*/

	if (!test_bit(RIO_DME_OPEN, &priv->ob_mbox[mbox_dest].state))
		return -EINVAL;

	buf_sz = me->sz;
	if ((len < 8) || (len > buf_sz))
		return -EINVAL;

	me = dme_get(me);
	if (!me)
		return -EINVAL;

	/* Choose a free descriptor in a critical section */
	spin_lock_irqsave(&me->lock, iflags);
	desc = get_ob_desc(mport, me);
	if (!desc) {
		spin_unlock_irqrestore(&me->lock, iflags);
		__ob_dme_event_dbg(priv, me->dme_no,
				   1 << RIO_OB_DME_TX_PUSH_RING_FULL);
		rc = -EBUSY;
		goto done;
	}
	me->entries_in_use++;
	spin_unlock_irqrestore(&me->lock, iflags);

	__ob_dme_event_dbg(priv, me->dme_no, 1 << RIO_OB_DME_TX_PUSH);
	desc->cookie = cookie;

	/* Copy and clear rest of buffer */
	if (desc->msg_virt == NULL) {
		rc = -ENXIO;
		goto done;
	}
	memcpy(desc->msg_virt, buffer, len);

	dw0 = DME_DESC_DW0_SRC_DST_ID(destid) |
		DME_DESC_DW0_EN_INT |
		DME_DESC_DW0_VALID;

	if (desc->last) /* (Re-)Make ring of descriptors */
		dw0 |= DME_DESC_DW0_NXT_DESC_VALID;

	dw1 = DME_DESC_DW1_PRIO(flags) |
		DME_DESC_DW1_CRF(flags) |
		DME_DESC_DW1_SEG_SIZE_256 |
		DME_DESC_DW1_MSGLEN(len) |
		DME_DESC_DW1_XMBOX(mbox_dest) |
		DME_DESC_DW1_MBOX(mbox_dest) |
		DME_DESC_DW1_LETTER(letter);

	if (!priv->intern_msg_desc) {
		*((u32 *)DESC_TABLE_W1_MEM(me, desc->desc_no)) = dw1;
		*((u32 *)DESC_TABLE_W0_MEM(me, desc->desc_no)) = dw0;
	} else {
		__rio_local_write_config_32(mport,
					 DESC_TABLE_W1(desc->desc_no), dw1);
		__rio_local_write_config_32(mport,
					 DESC_TABLE_W0(desc->desc_no), dw0);
	}

	/* Start / Wake up */
	axxia_local_config_read(priv, RAB_OB_DME_CTRL(me->dme_no), &dme_ctrl);
	dme_ctrl |= DME_WAKEUP | DME_ENABLE;
	axxia_local_config_write(priv, RAB_OB_DME_CTRL(me->dme_no), dme_ctrl);

done:
	dme_put(me);
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
	unsigned long lflags;
	int rc = 0;

	spin_lock_irqsave(&priv->api_lock, lflags);
	rc = open_inb_mbox(mport, dev_id, mbox, entries);
	spin_unlock_irqrestore(&priv->api_lock, lflags);

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
	unsigned long lflags;

	if ((mbox < 0) || (mbox >= RIO_MAX_RX_MBOX))
		return;

	spin_lock_irqsave(&priv->api_lock, lflags);
	release_irq_handler(&priv->ib_dme_irq[mbox]);
	spin_unlock_irqrestore(&priv->api_lock, lflags);

	return;
}

/**
 * axxia_add_inb_buffer - Add buffer to the AXXIA inbound message queue
 * @mport: Master port implementing the inbound message unit
 * @mbox: Inbound mailbox number
 * @buf: Buffer to add to inbound queue
 *
 * Adds the @buf buffer to the AXXIA inbound message queue.
 *
 * Returns %0 on success
 *         %-EINVAL on invalid argument failure.
 *         %-EBUSY  on temporarily unavailable resource failure e.g. such
 *                     as waiting for a filled entry in the inbound DME
 *                     descriptor chain
 */
int axxia_add_inb_buffer(struct rio_mport *mport, int mbox, void *buf)
{
	struct rio_priv *priv = mport->priv;
	struct rio_rx_mbox *mb;
	unsigned long iflags;
	int rc = 0;

	if ((mbox < 0) || (mbox >= RIO_MAX_RX_MBOX))
		return -EINVAL;

	if (!test_bit(RIO_IRQ_ENABLED, &priv->ib_dme_irq[mbox].state))
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
 *
 * Returns pointer to the message on success
 *         NULL on nothing available
 *         IS_ERR(ptr) on failure with extra information
 */
void *axxia_get_inb_message(struct rio_mport *mport, int mbox, int letter,
			    int *sz, int *slot, u16 *destid)
{
	struct rio_priv *priv = mport->priv;
	struct rio_rx_mbox *mb;
	struct rio_msg_dme *me;
	unsigned long iflags;
	int num_proc = 0;
	void *buf = NULL;

	if ((mbox < 0) || (mbox >= RIO_MAX_RX_MBOX))
		return ERR_PTR(-EINVAL);
	if ((letter < 0) || (letter >= RIO_MSG_MAX_LETTER))
		return ERR_PTR(-EINVAL);
	if (!test_bit(RIO_IRQ_ENABLED, &priv->ib_dme_irq[mbox].state))
		return ERR_PTR(-EINVAL);

	mb = mbox_get(priv->ib_dme_irq[mbox].data);
	if (!mb)
		return ERR_PTR(-EINVAL);

	me = dme_get(mb->me[letter]);
	if (!me)
		return ERR_PTR(-EINVAL);

	spin_lock_irqsave(&me->lock, iflags);

#ifdef OBSOLETE_BZ47185
	/* Make this conditional for AXM55xx??? */
	if (!in_interrupt() &&
	    !test_bit(RIO_IRQ_ACTIVE, &priv->ib_dme_irq[mbox].state)) {
		u32	intr;
		axxia_local_config_read(priv, RAB_INTR_ENAB_IDME, &intr);
		axxia_local_config_write(priv, RAB_INTR_ENAB_IDME,
					    intr & ~(1 << me->dme_no));
		ib_dme_irq_handler(&priv->ib_dme_irq[mbox], (1 << me->dme_no));
		axxia_local_config_write(priv, RAB_INTR_ENAB_IDME, intr);
	}
#endif /* OBSOLETE_BZ47185 */

	while (1) {
		struct rio_msg_desc *desc = &me->desc[me->read_idx];
		u32 dw0, dw1;

		buf = NULL;
		*sz = 0;
		if (!priv->intern_msg_desc) {
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
			if (!priv->intern_msg_desc) {
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
			num_proc++;
		} else if ((dw0 & DME_DESC_DW0_DONE) &&
			   (dw0 & DME_DESC_DW0_VALID)) {
			int seg = DME_DESC_DW1_MSGLEN_F(dw1);
			int buf_sz = DME_DESC_DW1_MSGLEN_B(seg);
			buf = mb->virt_buffer[mb->next_rx_slot];
			if (!buf)
				goto err;

			AXXIA_RIO_SYSMEM_BARRIER();

			memcpy(buf, desc->msg_virt, buf_sz);
			mb->virt_buffer[mb->next_rx_slot] = NULL;
			if (!priv->intern_msg_desc) {
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
			num_proc++;
			goto done;
		} else {
			goto done;
		}
	}

done:
	/* Advance VALID bit to next entry */
	if (num_proc > 0) {
		u32 dw0;
		int nxt_inval_desc = (me->last_invalid_desc + num_proc) %
				     me->entries;
		if (!priv->intern_msg_desc) {
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
		axxia_local_config_read(priv, RAB_IB_DME_CTRL(me->dme_no),
					&dw0);
		dw0 |= DME_WAKEUP | DME_ENABLE;
		axxia_local_config_write(priv, RAB_IB_DME_CTRL(me->dme_no),
					 dw0);
	}

	spin_unlock_irqrestore(&me->lock, iflags);
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
		GRIO_INT | LL_TL_INT |
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
		priv->ob_dme_irq[i].release_fn = release_outb_dme;
	}

	for (i = 0; i < RIO_MAX_TX_MBOX; i++)
		spin_lock_init(&priv->ob_mbox[i].lock);

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

	axxia_local_config_read(priv, RIO_ESCSR(priv->port_ndx), &escsr);

	if (escsr & RIO_ESCSR_PO)
		return MPORT_STATE_OPERATIONAL;
	else
		return MPORT_STATE_DOWN;
}
#endif

int axxia_rio_apio_enable(struct rio_mport *mport, u32 mask, u32 bits)
{
	struct rio_priv *priv = mport->priv;
	unsigned long lflags;
	int rc;

	spin_lock_irqsave(&priv->api_lock, lflags);
	rc = enable_apio(&priv->apio_irq, mask, bits);
	spin_unlock_irqrestore(&priv->api_lock, lflags);

	return rc;
}
EXPORT_SYMBOL_GPL(axxia_rio_apio_enable);

int axxia_rio_apio_disable(struct rio_mport *mport)
{
	struct rio_priv *priv = mport->priv;
	unsigned long lflags;

	spin_lock_irqsave(&priv->api_lock, lflags);
	release_irq_handler(&priv->apio_irq);
	spin_unlock_irqrestore(&priv->api_lock, lflags);

	return 0;
}
EXPORT_SYMBOL_GPL(axxia_rio_apio_disable);

int axxia_rio_rpio_enable(struct rio_mport *mport, u32 mask, u32 bits)
{
	struct rio_priv *priv = mport->priv;
	unsigned long lflags;
	int rc = 0;

	spin_lock_irqsave(&priv->api_lock, lflags);
	rc = enable_rpio(&priv->rpio_irq, mask, bits);
	spin_unlock_irqrestore(&priv->api_lock, lflags);

	return rc;
}
EXPORT_SYMBOL_GPL(axxia_rio_rpio_enable);

int axxia_rio_rpio_disable(struct rio_mport *mport)
{
	struct rio_priv *priv = mport->priv;
	unsigned long lflags;

	spin_lock_irqsave(&priv->api_lock, lflags);
	release_irq_handler(&priv->rpio_irq);
	spin_unlock_irqrestore(&priv->api_lock, lflags);

	return 0;
}
EXPORT_SYMBOL_GPL(axxia_rio_rpio_disable);
