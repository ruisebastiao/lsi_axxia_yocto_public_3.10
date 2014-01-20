#ifndef __AXXIA_RIO_IRQ_H__
#define __AXXIA_RIO_IRQ_H__

/* forward declaration */
struct rio_priv;
struct mutex;

#if !defined(CONFIG_AXXIA_RIO_STAT)

#define __port_event_dbg(priv, escsr, iecsr)
#define __port_state_dbg(priv, escsr)
#define __irq_dbg(priv, irq)
#define __misc_fatal_dbg(priv, misc_state, amast)
#define __misc_info_dbg(priv, misc_state)
#define __ob_db_dbg(priv, mport)
#define __ob_dme_dbg(priv, dme_stat)
#define __ob_dme_dw_dbg(priv, dw0)
#define __ib_dme_dbg(priv, dme_stat)
#define __ib_dme_dw_dbg(priv, dw0)
#define __rpio_fail_dbg(priv, rpio_stat)
#define __apio_fail_dbg(priv, apio_state)
#define __ib_dme_event_dbg(priv, dme, ib_event)
#define __ob_dme_event_dbg(priv, dme, ob_event)
#endif

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
	RIO_PU,
	RIO_STATE_NUM
};

enum rio_irq_dbg {
	/**
	 * Axxi Error Events - really bad!
	 */
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
	RIO_LINKDOWN,
	/**
	 * Peripheral Bus bridge, RapidIO -> Peripheral bus events - mostly bad!
	 */
	RIO_PIO_COMPLETE,
	RIO_PIO_FAILED,
	RIO_PIO_RSP_ERR,
	RIO_PIO_ADDR_MAP,
	RIO_PIO_DISABLED,
	/**
	 * Peripheral Bus bridge, Peripheral bus -> RapidIO events - mostly bad!
	 */
	RIO_APIO_COMPLETE,
	RIO_APIO_RQ_ERR,
	RIO_APIO_TO_ERR,
	RIO_APIO_RSP_ERR,
	RIO_APIO_MAP_ERR,
	RIO_APIO_MAINT_DIS,
	RIO_APIO_MEM_DIS,
	RIO_APIO_DISABLED,
	/**
	 * Port Write - service irq
	 */
	RIO_MISC_PW,
	RIO_MISC_PW_SPURIOUS,
	RIO_MISC_PW_MSG,
	/**
	 * Doorbells - service irq
	 */
	RIO_MISC_OB_DB_DONE,
	RIO_MISC_OB_DB_RETRY,
	RIO_MISC_OB_DB_ERROR,
	RIO_MISC_OB_DB_TO,
	RIO_MISC_IB_DB,
	RIO_MISC_IB_DB_SPURIOUS,
	/**
	 * Outbound Messaging unit - service irq
	 */
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
	/**
	 * Inbound Messaging unit - service irq
	 */
	RIO_MISC_UNEXP,
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
	RIO_IRQ_NUM
};

enum rio_ib_dme_dbg {
	RIO_IB_DME_RX_PUSH,
	RIO_IB_DME_RX_POP,
	RIO_IB_DME_DESC_ERR,
	RIO_IB_DME_RX_VBUF_EMPTY,
	RIO_IB_DME_RX_RING_FULL,
	RIO_IB_DME_RX_PENDING_AT_SLEEP,
	RIO_IB_DME_RX_SLEEP,
	RIO_IB_DME_RX_WAKEUP,
	RIO_IB_DME_NUM
};

enum rio_ob_dme_dbg {
	RIO_OB_DME_TX_PUSH,
	RIO_OB_DME_TX_PUSH_RING_FULL,
	RIO_OB_DME_TX_POP,
	RIO_OB_DME_TX_DESC_READY,
	RIO_OB_DME_TX_PENDING,
	RIO_OB_DME_TX_SLEEP,
	RIO_OB_DME_TX_WAKEUP,
	RIO_OB_DME_NUM
};


#define RIO_MSG_MAX_OB_MBOX_MULTI_ENTRIES  15
#define RIO_MSG_MULTI_SIZE                 0x1000 /* 4Kb */
#define RIO_MSG_SEG_SIZE                   0x0100 /* 256B */
#define RIO_MSG_MAX_MSG_SIZE               RIO_MSG_MULTI_SIZE
#define RIO_MSG_MAX_ENTRIES                1024   /* Default Max descriptor
						     table entries for internal
						     descriptor builds */
#define	RIO_MBOX_TO_BUF_SIZE(mid)		\
	((mid <= RIO_MAX_RX_MBOX_4KB) ? RIO_MSG_MULTI_SIZE : RIO_MSG_SEG_SIZE)
#define	RIO_OUTB_DME_TO_BUF_SIZE(p,did)		\
	((did < p->numOutbDmes[0]) ? RIO_MSG_MULTI_SIZE : RIO_MSG_SEG_SIZE)

#define DME_MAX_IB_ENGINES          32
#define     RIO_MAX_IB_DME_MSEG		32
#define     RIO_MAX_IB_DME_SSEG	        0
#define DME_MAX_OB_ENGINES          3
#define     RIO_MAX_OB_DME_MSEG		2
#define     RIO_MAX_OB_DME_SSEG	        1

#ifdef	AXXIA_RIO_SMALL_SYSTEM
	#define RIO_MAX_TX_MBOX             8
	#define     RIO_MAX_TX_MBOX_4KB		3
	#define     RIO_MAX_TX_MBOX_256B	7
	#define RIO_MAX_RX_MBOX             8
	#define     RIO_MAX_RX_MBOX_4KB		3
	#define     RIO_MAX_RX_MBOX_256B	7
#else
	#define RIO_MAX_TX_MBOX             64
	#define     RIO_MAX_TX_MBOX_4KB		3
	#define     RIO_MAX_TX_MBOX_256B	63
	#define RIO_MAX_RX_MBOX             64
	#define     RIO_MAX_RX_MBOX_4KB		3
	#define     RIO_MAX_RX_MBOX_256B	63
#endif

#define RIO_MSG_MAX_LETTER          4


#define RIO_DESC_USED 0		/* Bit index for rio_msg_desc.state */

struct rio_msg_desc {
	unsigned long state;
	int desc_no;
	void __iomem *msg_virt;
	dma_addr_t msg_phys;
	int last;
	void *cookie;
};

struct rio_msg_dme {
	spinlock_t lock;
	char name[16];
	struct kref kref;
	struct rio_priv *priv;
	struct resource dres;
	int sz;
	int entries;
	int entries_in_use;
	int write_idx;
	int read_idx;
	atomic_t pending;
	int tx_dme_tmo;
	void *dev_id;
	int dme_no;
	struct rio_msg_desc *desc;
	struct rio_desc *descriptors;
#ifdef CONFIG_SRIO_IRQ_TIME
	u64 start_irq_tb;
	u64 start_thrd_tb;
	u64 stop_irq_tb;
	u64 stop_thrd_tb;
	u64 min_lat;
	u64 max_lat;
	u32 pkt;
	u32 bytes;
#endif
};

struct rio_rx_mbox {
	spinlock_t lock;
	int mbox_no;
	char name[16];
	struct kref kref;
	struct rio_mport *mport;
	/* void *virt_buffer[RIO_MAX_RX_RING_SIZE]; */
	void **virt_buffer;
	int last_rx_slot;
	int next_rx_slot;
	int ring_size;
	struct rio_msg_dme *me[RIO_MSG_MAX_LETTER];
};

#define PW_MSG_WORDS (RIO_PW_MSG_SIZE/sizeof(u32))

struct rio_pw_irq {
	/* Port Write */
	u32 discard_count;
	u32 msg_count;
	u32 msg_wc;
	u32 msg_buffer[PW_MSG_WORDS];
};

#define RIO_IRQ_ENABLED 0
#define RIO_IRQ_ACTIVE  1

struct rio_irq_handler {
	unsigned long state;
	struct rio_mport *mport;
	u32 irq_enab_reg_addr;
	u32 irq_state_reg_addr;
	u32 irq_state_mask;
	u32 irq_state;
	void (*thrd_irq_fn)(struct rio_irq_handler *h, u32 state);
	void (*release_fn)(struct rio_irq_handler *h);
	void *data;
#ifdef CONFIG_SRIO_IRQ_TIME
	u64 irq_tb;
	u64 thrd_tb;
	atomic_t start_time;
#endif
};


/**********************************************/
/* *********** External Functions *********** */
/**********************************************/

void axxia_rio_port_irq_init(struct rio_mport *mport);
void *axxia_get_inb_message(struct rio_mport *mport, int mbox, int letter,
			      int *sz, int *slot, u16 *destid);
int axxia_add_inb_buffer(struct rio_mport *mport, int mbox, void *buf);
void axxia_close_inb_mbox(struct rio_mport *mport, int mbox);
int axxia_open_inb_mbox(struct rio_mport *mport, void *dev_id,
			  int mbox, int entries);
int axxia_add_outb_message(struct rio_mport *mport, struct rio_dev *rdev,
			     int mbox_dest, int letter, int flags,
			     void *buffer, size_t len, void *cookie);
void axxia_close_outb_mbox(struct rio_mport *mport, int dmeMbox);
int axxia_open_outb_mbox(struct rio_mport *mport, void *dev_id, int mbox,
			 int entries, int prio);
int axxia_rio_doorbell_send(struct rio_mport *mport,
			      int index, u16 destid, u16 data);
int axxia_rio_pw_enable(struct rio_mport *mport, int enable);
void axxia_rio_port_get_state(struct rio_mport *mport, int cleanup);
int axxia_rio_port_irq_enable(struct rio_mport *mport);
void axxia_rio_port_irq_disable(struct rio_mport *mport);

/* Data_streaming - add function declaration as axxia-rio-ds.c
**                  calls this function as well */
int alloc_irq_handler(
	struct rio_irq_handler *h,
        void *data,
        const char *name);

void release_irq_handler(struct rio_irq_handler *h);

#ifdef CONFIG_AXXIA_RIO_STAT

extern int axxia_rio_init_sysfs(struct platform_device *dev);
extern void axxia_rio_release_sysfs(struct platform_device *dev);

#endif

#if defined(CONFIG_RAPIDIO_HOTPLUG)

int axxia_rio_port_notify_cb(struct rio_mport *mport,
			       int enable,
			       void (*cb)(struct rio_mport *mport));
int axxia_rio_port_op_state(struct rio_mport *mport);

#endif

#endif /* __AXXIA_RIO_IRQ_H__ */
