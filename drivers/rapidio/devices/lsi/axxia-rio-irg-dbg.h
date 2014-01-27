#ifndef __AXXIA_RIO_IRQ_DBG_H__
#define __AXXIA_RIO_IRQ_DBG_H__


enum rio_event_dbg {
	RIO_OPD,
	RIO_OFE,
	RIO_ODE,
	RIO_ORE,
	RIO_OEE,
	RIO_IEE,
	RIO_PE,
	EPC_RETE,
	RIO_EVENT_NUM
};

enum rio_state_dbg {
	RIO_ORS,
	RIO_OES,
	RIO_IRS,
	RIO_IES,
	RIO_PO,
	RIO_STATE_NUM
};


enum rio_irq_dbg {
	RIO_MISC_LOG,
	RIO_MISC_AMST,
	RIO_AMST_WRTO,
	RIO_AMST_RDTO,
	RIO_AMST_WRDE,
	RIO_AMST_WRSE,
	RIO_AMST_RDDE,
	RIO_AMST_RDSE,
	RIO_MISC_ASLV,
	RIO_MISC_TL,
	RIO_MISC_GRIO,
	RIO_MISC_UNSUP,
	RIO_MISC_UNEXP,
	RIO_MISC_PW,
	RIO_MISC_OB_DB,
	RIO_MISC_OB_DB_DONE,
	RIO_MISC_OB_DB_RETRY,
	RIO_MISC_OB_DB_ERROR,
	RIO_MISC_OB_DB_TO,
	RIO_MISC_OB_DB_PENDING,
	RIO_MISC_IB_DB,
	RIO_MISC_IB_DB_SPURIOUS,
	RIO_LINKDOWN,
	RIO_OB_DME_STAT_RESP_TO,
	RIO_OB_DME_STAT_RESP_ERR,
	RIO_OB_DME_STAT_DATA_TRANS_ERR,
	RIO_OB_DME_STAT_DESC_UPD_ERR,
	RIO_OB_DME_STAT_DESC_ERR,
	RIO_OB_DME_STAT_DESC_FETCH_ERR,
	RIO_OB_DME_STAT_SLEEPING,
	RIO_OB_DME_STAT_DESC_XFER_CPLT,
	RIO_OB_DME_STAT_DESC_CHAIN_XFER_CPLT,
	RIO_OB_DME_STAT_TRANS_PEND,
	RIO_OB_DME_DESC_DW0_RIO_ERR,
	RIO_OB_DME_DESC_DW0_AXI_ERR,
	RIO_OB_DME_DESC_DW0_TIMEOUT_ERR,
	RIO_OB_DME_DESC_DESC_DW0_DONE,
	RIO_IB_DME_STAT_MSG_TIMEOUT,
	RIO_IB_DME_STAT_MSG_ERR,
	RIO_IB_DME_STAT_DATA_TRANS_ERR,
	RIO_IB_DME_STAT_DESC_UPDATE_ERR,
	RIO_IB_DME_STAT_DESC_ERR,
	RIO_IB_DME_STAT_FETCH_ERR,
	RIO_IB_DME_STAT_SLEEPING,
	RIO_IB_DME_STAT_DESC_XFER_CPLT,
	RIO_IB_DME_STAT_DESC_CHAIN_XFER_CPLT,
	RIO_IB_DME_STAT_TRANS_PEND,
	RIO_IB_DME_DESC_DW0_RIO_ERR,
	RIO_IB_DME_DESC_DW0_AXI_ERR,
	RIO_IB_DME_DESC_DW0_TIMEOUT_ERR,
	RIO_IB_DME_DESC_DESC_DW0_DONE,
	RIO_PIO_COMPLETE,
	RIO_PIO_FAILED,
	RIO_PIO_RSP_ERR,
	RIO_PIO_ADDR_MAP,
	RIO_PIO_DISABLED,
	RIO_APIO,
	RIO_APIO_COMPLETE,
	RIO_APIO_FAILED,
	RIO_APIO_RQ_ERR,
	RIO_APIO_TO_ERR,
	RIO_APIO_RSP_ERR,
	RIO_APIO_MAP_ERR,
	RIO_APIO_MAINT_DIS,
	RIO_APIO_MEM_DIS,
	RIO_APIO_DISABLED,
	RIO_IRQ_NUM
};

#if defined(CONFIG_AXXIA_RIO_STAT)
/**
 * __add_event_dbg -- Update port event counters
 *
 * @priv: Master port private data
 * @escsr: PortN Error and Status Command State register
 * @iecsr: PortN Implementation Error Command and status register
 */
static inline void __port_event_dbg(struct rio_priv *priv, u32 escsr, u32 iecsr)
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
static inline void __port_state_dbg(struct rio_priv *priv, u32 escsr)
{
	/* update stats debug info */
	atomic_set(&priv->state[RIO_ORS], (escsr & RIO_ESCSR_ORS ? 1 : 0));
	atomic_set(&priv->state[RIO_OES], (escsr & RIO_ESCSR_OES ? 1 : 0));
	atomic_set(&priv->state[RIO_IRS], (escsr & RIO_ESCSR_IRS ? 1 : 0));
	atomic_set(&priv->state[RIO_IES], (escsr & RIO_ESCSR_IES ? 1 : 0));
	atomic_set(&priv->state[RIO_PO], (escsr & RIO_ESCSR_PO ? 1 : 0));
}
static inline void __irq_dbg(struct rio_priv *priv, enum rio_misc_dbg id)
{
	atomic_inc(&priv->irq[id]);
}
static inline void __misc_fatal_dbg(struct rio_priv *priv,
				    u32 misc_state, u32 amast)
{
	if (misc_stat & AMST_INT) {
		__irq_dbg(priv, RIO_MISC_AMST);
		if (amast & RAB_AMAST_STAT_WRTO)
			__irq_dbg(priv, RIO_AMST_WRTO);
		if (amast & RAB_AMAST_STAT_RDTO)
			__irq_dbg(priv, RIO_AMST_RDTO);
		if (amast & RAB_AMAST_STAT_WRDE)
			__irq_dbg(priv, RIO_AMST_WRDE);
		if (amast & RAB_AMAST_STAT_WRSE)
			__irq_dbg(priv, RIO_AMST_WRSE);
		if (amast & RAB_AMAST_STAT_RDDE)
			__add_irq_dbg(priv, RIO_AMST_RDDE);
		if (amast & RAB_AMAST_STAT_RDSE)
			__irq_dbg(priv, RIO_AMST_RDSE);
	}
	if (stat & ASLV_INT)
		__irq_dbg(priv, RIO_MISC_ASLV);
}
static inline void __misc_info_dbg(struct rio_priv *priv, u32 misc_state)
{
	/* Log only - no enable bit or state to clear */
	if (misc_state & (UNEXP_MSG_LOG |
			  LL_TL_INT | GRIO_INT |
			  UNSP_RIO_REQ_INT | UNEXP_MSG_INT)) {
		if (misc_state & UNEXP_MSG_LOG)
			__irq_dbg(priv, RIO_MISC_LOG);
		if (misc_state & LL_TL_INT)
			__irq_dbg(priv, RIO_MISC_TL);
		if (misc_state & GRIO_INT)
			__irq_dbg(priv, RIO_MISC_GRIO);
		if (misc_state & UNSP_RIO_REQ_INT)
			__irq_dbg(priv, RIO_MISC_UNSUP);
		if (stat & UNEXP_MSG_INT)
			__irq_dbg(priv, RIO_MISC_UNEXP);
	}
}

static inline void __ob_db_dbg(struct rio_priv *priv)
{
	int db;
	u32 csr;

	__irq_dbg(priv, RIO_MISC_OB_DB);

	for (db = 0; db < MAX_OB_DB; db++) {
		__rio_local_read_config_32(mport, RAB_OB_DB_CSR(db), &csr);

		if (OB_DB_STATUS(csr) == OB_DB_STATUS_RETRY)
			__irq_dbg(priv, RIO_MISC_OB_DB_RETRY);
		else if (OB_DB_STATUS(csr) == OB_DB_STATUS_ERROR)
			__irq_dbg(priv, RIO_MISC_OB_DB_ERROR);
		else if (OB_DB_STATUS(csr) == OB_DB_STATUS_TIMEOUT)
			__irq_dbg(priv, RIO_MISC_OB_DB_TO);
		else if (status == OB_DB_STATUS_PENDING)
			__add_irq_dbg(priv, RIO_MISC_OB_DB_PENDING);
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

#else

#define __port_event_dbg(priv, escsr, iecsr)
#define __port_state_dbg(priv, escsr)
#define __irq_dbg(priv, irq)
#define __misc_fatal_dbg(priv, misc_state, amast)
#define __misc_info_dbg(priv, misc_state)
#define __ob_db_dbg(priv)
#define __ob_dme_dbg(priv, dme_stat)
#define __ob_dme_dw_dbg(priv, dw0)

#endif

#endif
