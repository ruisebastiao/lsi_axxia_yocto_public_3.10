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
#ifndef _AXXIA_RIO_DS_H_
#define _AXXIA_RIO_DS_H_

#include <linux/device.h>
#include <linux/of_platform.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/rio_dio.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>

/* #define DS_DEBUG 1  */
#define USE_IOCTRL 1

/******************************************************************************
    #defines
******************************************************************************/
#define RIO_IBDS_DATA_BUF_1K                  (1<<10)
#define RIO_IBDS_DATA_BUF_2K                  (1<<11)
#define RIO_IBDS_DATA_BUF_4K                  (1<<12)
#define RIO_IBDS_DATA_BUF_8K                  (1<<13)
#define RIO_IBDS_DATA_BUF_16K                 (1<<14)
#define RIO_IBDS_DATA_BUF_32K                 (1<<15)
#define RIO_IBDS_DATA_BUF_64K                 (1<<16)


#define RIO_DS_DATA_BUF_64K                 (0) /* HW uses 0 for 64K */

#define RIO_MAX_NUM_OBDS_DSE                (16)
#define RIO_MAX_NUM_IBDS_DSE				(16)
#define RIO_MAX_NUM_IBDS_VSID_M             (32)

#define RC_TBD                              (-1)

#define RIO_DS_TRUE                         (1)
#define RIO_DS_FALSE                        (0)

#define IOCTL_BUF_SIZE                      (4096)
#define MAX_NUM_PROC_IBDS_DESC				(10)

#define DS_DBUF_FREED					    (0)
#define DS_DBUF_ALLOC						(1)
#define DS_DBUF_RETRIEVED					(2)

#define     AXXIA_SRIO_RAB_VER_VAL_55xx           (0x00211021)
#define     AXXIA_SRIO_RAB_VER_VAL_55xx_X7v1P1    (0x00221013)


/*
** Data Streaming registers
*/
#define IB_DS_INT_EN					(1<<7)
#define OB_DS_INT_EN					(1<<8)
#define OB_DSE_WAKEUP                   (2)
#define OB_DSE_ENABLE                   (1)
#define OB_DSE_PREFETCH				 (4)

#define IB_VSID_M_PREFETCH_ENABLE		(2)
#define IB_VSID_M_PREFETCH_WAKEUP		(4)

#define RAB_OBDSE_CTRL(n)            (RAB_REG_BASE + (0x2d28 + (0xC*(n))))
#define RAB_OBDSE_STAT(n)            (RAB_REG_BASE + (0x2d28 + (0xC*(n)))+0x4)
#define RAB_OBDSE_DESC_ADDR(n)       (RAB_REG_BASE + (0x2d28 + (0xC*(n)))+0x8)

#define RAB_IBVIRT_M_STAT(n)         (RAB_REG_BASE + (0x2ef0 + (0x4*(n))))

#define RAB_IBDSE_CTRL(n) \
	(RAB_REG_BASE + (0x2a20 + (0x8 * (n))))
#define RAB_IBDSE_STAT(n) \
	(RAB_REG_BASE + (0x2a20 + (0x8 * (n))) + 0x4)

#define RAB_IBDS_VSID_ADDR_LOW(n)    (RAB_REG_BASE + (0x2b28 + (0x8*(n))))
#define RAB_IBDS_VSID_ADDR_HI(n)     (RAB_REG_BASE + (0x2b28 + (0x8*(n)))+0x4)

#define RAB_IBDS_VSID_ALIAS          (RAB_REG_BASE + 0x2a1c)
#define GRIO_DSI_CAR				 (0x3c)
#define GRIO_DSLL_CCSR				 (0x48)

#define RIO_DS_DESC_ALIGNMENT        (1 << 5)

/* stats */
#define IB_VIRT_M_STAT_ERROR_MASK           0x3FC
#define IB_VIRT_M_STAT_SLEEPING			(1<<11)
#define IB_VIRT_M_STAT_TRAN_PENDING		(1<<10)
#define IB_VIRT_M_STAT_PDU_DROPPED	    (1 << 9)
#define IB_VIRT_M_STAT_SEG_LOSS		    (1 << 8)
#define IB_VIRT_M_STAT_MTU_LEN_MIS_ERR	    (1 << 7)
#define IB_VIRT_M_STAT_PDU_LEN_MIS_ERR      (1 << 6)
#define IB_VIRT_M_STAT_TRANS_ERR	    (1 << 5)
#define IB_VIRT_M_STAT_UPDATE_ERR	    (1 << 4)
#define IB_VIRT_M_STAT_TIMEOUT_ERR	    (1 << 3)
#define IB_VIRT_M_STAT_FETCH_ERR            (1 << 2)

#define IB_DSE_DESC_ERROR_MASK          0xC00
#define IB_DSE_DESC_AXI_ERR             (1 << 11)
#define IB_DSE_DESC_DS_ERR              (1 << 10)
#define IB_DSE_DESC_DONE                (1 << 9)

#define IB_DSE_VSID_IN_USED             0x3F
#define IB_DSE_STAT_TRANS_PENDING       (1 << 6)
#define IB_DSE_STAT_TIMEOUT             (1 << 7)

#define OB_DSE_STAT_ERROR_MASK		    0x3C
#define OB_DSE_STAT_TRANS_ERR	  	    (1 << 5)
#define OB_DSE_STAT_UPDATE_ERR		    (1 << 4)
#define OB_DSE_STAT_DESC_ERR                (1 << 3)
#define OB_DSE_STAT_FETCH_ERR               (1 << 2)
#define OB_DSE_STAT_SLEEPING			(1<<8)

#define OB_DSE_DESC_ERROR_MASK          0x400
#define OB_HDR_DESC_AXI_ERR             (1 << 10)
#define OB_HDR_DESC_DONE                (1 << 8)


/* data streaming dtb related information */
struct rio_ds_dtb_info{
	int ds_enabled;
	#if 0
	int num_inb_virtaul_m;      /* number of inbound virtual M */
	int num_outb_dses;          /* number of outbound DSEs */
	int inb_num_data_descs;     /* number of inbound data descriptors */
	int outb_num_hdr_descs;     /* number of outbound header descriptors */
	int outb_num_data_descs;    /* number of outbound data descriptors */
	#endif
};

/*
** The following data structure defines data streaming descriptors
**
**  The HW requires that the desc_addr has to be 8 words alignment, thus
**      additional words are added for SW usage
*/
/* outbound data descriptor */
struct rio_ods_data_desc{
	u32 dw0;
	u32 dw1;
	u32 dw2;
	u32 dw3;

	/* SW usage */
	u32 virt_data_buf;
	u32 sw1;
	u32 sw2;
	u32 sw3;
};

/* inbound data descriptor */
struct rio_ids_data_desc{
	u32 dw0;
	u32 dw1;
	u32 dw2;
	u32 dw3;
	u32 dw4;

	/* SW usage */
	u32 virt_data_buf;
	u32 buf_status;
	u32 sw2;
};

/*
** The following data structure defines data streaming header descriptors
**  only used in outbound
*/
struct rio_ds_hdr_desc{
	u32 dw0;
	u32 dw1;
	u32 dw2;
	u32 dw3;
	u32 dw4;

	/* SW usage */
	u32     virt_data_buf;
	u32		buf_status;
	u32	sw2;
};

/*
** OBDS DSE configuration
*/
struct rio_obds_dse_cfg {
	u8         in_use;     /* if the DSE is in_use */
	u8         cos;
	u16        dest_id;
	u16        stream_id;
	spinlock_t lock;
	u8         irqEnabled;
	char       name[16];

	/* header descriptor */
	u16        num_hdr_desc_free;
	u16        max_num_hdr_desc;

	/* data descriptor */
	u16        num_data_desc_free;
	u16        max_num_data_desc;
	u16		hdr_read_ptr;
	u16		hdr_write_ptr;
	u8		first_hdr_desc;

	u16		data_read_ptr;
	u16		data_write_ptr;
	u8		first_data_desc;

	struct rio_ds_hdr_desc		*ptr_obds_hdr_desc;
	struct rio_ods_data_desc	*ptr_obds_data_desc;
};

/*
** IBDS configuration
*/
struct ibds_virt_m_cfg {
	spinlock_t lock;
	u32        in_use;     /* if the DSE is in_use */
	u8         cos;
	u16        dest_id;
	u16        stream_id;
	u16        source_id;
	char       name[16];

	u16        num_desc_free;
	u16        max_num_data_desc;
	u16        data_read_ptr;
	u16        data_write_ptr;

	struct rio_ids_data_desc    *ptr_ibds_data_desc;
	u32			desc_dbuf_size;
	u32			buf_add_ptr;
	u32			num_hw_written_bufs;

	u32			alias_reg_value;
	u16			virt_vsid;
	u16			num_data_streams;

};

/* Outbound data stream stats */
struct rio_ds_obds_dse_stats {
	u32	   num_desc_chain_transferred;
	u32	   num_desc_transferred;
	u32        num_desc_err;
	u32        num_desc_fetch_err;
	u32        num_desc_data_transaction_err;
	u32        num_desc_update_err;
	u32        num_desc_axi_err;
};

/* Inbound data stream stats */
struct rio_ds_ibds_vsid_m_stats {
	u32	   num_desc_chain_transferred;
	u32        num_desc_transferred;
	u32        num_desc_fetch_err;
	u32        num_timeout_err;
	u32        num_desc_update_err;
	u32	   num_data_transaction_err;
	u32        num_mtu_len_mismatch_err;
	u32        num_pdu_dropped;
	u32        num_pdu_len_mismatch_err;
	u32        num_dropped_pdu;
	u32        num_segment_loss;
	u32        num_desc_axi_err;
	u32        num_desc_ds_err;
};

/*
** The following data structure defines private data used by data streaming
** feature
*/
struct rio_ds_priv {
	/* IBDS */
	u16			mtu;
	u32			ibds_avsid_mapping;
	u16                     num_ibds_dses;/* TBR */
	u16                     num_ibds_virtual_m;/* TBR */
	struct ibds_virt_m_cfg	ibds_vsid_m_cfg[RIO_MAX_NUM_IBDS_VSID_M];

	/* OBDS */
	u16                     num_obds_dses; /* TBR */
	struct rio_obds_dse_cfg obds_dse_cfg[RIO_MAX_NUM_OBDS_DSE];

	struct rio_irq_handler  ob_dse_irq[RIO_MAX_NUM_OBDS_DSE];
	struct rio_irq_handler  ib_dse_vsid_irq[RIO_MAX_NUM_IBDS_VSID_M];

struct rio_ds_ibds_vsid_m_stats ib_vsid_m_stats[RIO_MAX_NUM_IBDS_VSID_M];
	struct rio_ds_obds_dse_stats        ob_dse_stats[RIO_MAX_NUM_OBDS_DSE];

	u8		is_use_ds_feature;
};

/* Platform driver initialization */
extern int axxia_parse_dtb_ds(
	struct platform_device *dev,
	struct rio_ds_dtb_info *ptr_ds_dtb_info);

extern int axxia_cfg_ds(
    struct rio_mport        *mport,
    struct rio_ds_dtb_info  *ptr_ds_dtb_info);

extern void axxia_rio_ds_port_irq_init(
    struct rio_mport        *mport);

/* open an OBDS data stream */
extern int axxia_open_ob_data_stream(
	struct rio_mport    *mport,
	void                *dev_id,
	int                  dse_id,
	int                  num_header_entries,
	int                  num_data_entries);

extern int open_ob_data_stream(
	struct rio_mport    *mport,
	void                *dev_id,
	int                  dse_id,
	int                  num_header_entries,
	int                  num_data_entries);

/* add user's data */
extern int axxia_add_ob_data_stream(
	struct rio_mport	*mport,
	int		     	dest_id,
	int		     	stream_id,
	int		     	cos,
	int                     priority,
	int                     is_hdr_desc,
	void		    	*buffer,
	int		     	data_len);

/* handle outbound data streaming DSE interrupt */
void ob_dse_irq_handler(struct rio_irq_handler *h, u32 state);

/* handle inbound VSID interrupt */
void ib_dse_vsid_m_irq_handler(struct rio_irq_handler *h, u32 state);

/* data streaming global configuration */
extern int axxia_data_stream_global_cfg(
	struct rio_mport       *mport,
	int			mtu,
	int			ibds_avsid_mapping);

/* open IBDS data stream */
extern int axxia_open_ib_data_stream(
	struct rio_mport    	*mport,
	void			*dev_id,
	int			source_id,
	int 		    	cos,
	int			desc_dbuf_size,
	int			num_entries);

int open_ib_data_stream(
	struct rio_mport    	*mport,
	void			*dev_id,
	int			source_id,
	int 		    	cos,
	int			desc_dbuf_size,
	int			num_entries);

/* add IBDS data buffer */
extern int axxia_add_ibds_buffer(
	struct rio_mport       *mport,
	int                     source_id,
	int                     cos,
	void                   *buf,
	int                     buf_size);

/* get IBDS data */
extern void *axxia_get_ibds_data(
    struct rio_mport    *mport,
    int                 source_id,
    int                 cos,
    int                 *ptr_pdu_length,
    int                 *ptr_stream_id);

/* convert VSID to internal VSID */
int axxio_virt_vsid_convert(
    u32     vsid,
    u32     alias_reg,
    u16     *ptr_virt_vsid);

/* close IBDS data streaming */
extern int axxia_close_ib_data_stream(
    struct rio_mport    *mport,
    int                 source_id,
    int                 cos);

/* close OBDS data streaming */
extern int axxia_close_ob_data_stream(
	struct rio_mport    	*mport,
	int		 	dse_id);

void release_ob_ds(struct rio_irq_handler *h);

void release_ib_ds(struct rio_irq_handler *h);

#endif
