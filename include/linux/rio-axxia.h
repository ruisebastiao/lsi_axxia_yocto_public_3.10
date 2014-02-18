#ifndef __RIO_AXXIA_H__
#define __RIO_AXXIA_H__

#ifdef __KERNEL__

#include <linux/device.h>
#include <linux/of_platform.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/rio_dio.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>

#endif /* __KERNEL__ */

/******************************************************************************
**  Buffer sizes, Device limits, and other limits
******************************************************************************/

#define RIO_IBDS_DATA_BUF_1K		(1<<10)
#define RIO_IBDS_DATA_BUF_2K		(1<<11)
#define RIO_IBDS_DATA_BUF_4K		(1<<12)
#define RIO_IBDS_DATA_BUF_8K		(1<<13)
#define RIO_IBDS_DATA_BUF_16K		(1<<14)
#define RIO_IBDS_DATA_BUF_32K		(1<<15)
#define RIO_IBDS_DATA_BUF_64K		(1<<16)

#define RIO_OBDS_DATA_BUF_64K		(1<<16)

#define RIO_DS_DATA_BUF_64K		(0) /* HW uses 0 for 64K */

#define RIO_MAX_NUM_OBDS_DSE		(16)
#define RIO_MAX_NUM_IBDS_DSE		(16)
#define RIO_MAX_NUM_IBDS_VSID_M		(32)

#define RC_TBD				(-1)

#define RIO_DS_TRUE			(1)
#define RIO_DS_FALSE			(0)

#define MAX_NUM_PROC_IBDS_DESC		(10)

#define DS_DBUF_FREED			(0)
#define DS_DBUF_ALLOC			(1)
#define DS_DBUF_RETRIEVED		(2)

/******************************************************************************
**  Data Streaming registers
******************************************************************************/

#define IB_DS_INT_EN			(1<<7)
#define OB_DS_INT_EN			(1<<8)
#define OB_DSE_WAKEUP			(2)
#define OB_DSE_ENABLE			(1)
#define OB_DSE_PREFETCH			(4)

#define IB_VSID_M_PREFETCH_ENABLE	(2)
#define IB_VSID_M_PREFETCH_WAKEUP	(4)

#define RAB_OBDSE_CTRL(n)		(RAB_REG_BASE + (0x2d28 + (0xC*(n))))
#define RAB_OBDSE_STAT(n)		(RAB_REG_BASE + (0x2d28 + (0xC*(n)))+0x4)
#define RAB_OBDSE_DESC_ADDR(n)		(RAB_REG_BASE + (0x2d28 + (0xC*(n)))+0x8)

#define RAB_IBVIRT_M_STAT(n)		(RAB_REG_BASE + (0x2ef0 + (0x4*(n))))

#define RAB_IBDSE_CTRL(n) \
	(RAB_REG_BASE + (0x2a20 + (0x8 * (n))))
#define RAB_IBDSE_STAT(n) \
	(RAB_REG_BASE + (0x2a20 + (0x8 * (n))) + 0x4)

#define RAB_IBDS_VSID_ADDR_LOW(n)	(RAB_REG_BASE + (0x2b28 + (0x8*(n))))
#define RAB_IBDS_VSID_ADDR_HI(n)	(RAB_REG_BASE + (0x2b28 + (0x8*(n)))+0x4)

#define RAB_IBDS_VSID_ALIAS		(RAB_REG_BASE + 0x2a1c)
#define GRIO_DSI_CAR			(0x3c)
#define GRIO_DSLL_CCSR			(0x48)

#define RIO_DS_DESC_ALIGNMENT		(1 << 5)

/* stats */
#define IB_VIRT_M_STAT_ERROR_MASK	0x3FC
#define IB_VIRT_M_STAT_SLEEPING		(1<<11)
#define IB_VIRT_M_STAT_TRAN_PENDING	(1<<10)
#define IB_VIRT_M_STAT_PDU_DROPPED	(1 << 9)
#define IB_VIRT_M_STAT_SEG_LOSS		(1 << 8)
#define IB_VIRT_M_STAT_MTU_LEN_MIS_ERR	(1 << 7)
#define IB_VIRT_M_STAT_PDU_LEN_MIS_ERR	(1 << 6)
#define IB_VIRT_M_STAT_TRANS_ERR	(1 << 5)
#define IB_VIRT_M_STAT_UPDATE_ERR	(1 << 4)
#define IB_VIRT_M_STAT_TIMEOUT_ERR	(1 << 3)
#define IB_VIRT_M_STAT_FETCH_ERR	(1 << 2)

#define IB_DSE_DESC_ERROR_MASK		0xC00
#define IB_DSE_DESC_AXI_ERR		(1 << 11)
#define IB_DSE_DESC_DS_ERR		(1 << 10)
#define IB_DSE_DESC_DONE		(1 << 9)

#define IB_DSE_VSID_IN_USE		0x3F
#define IB_DSE_STAT_TRANS_PENDING	(1 << 6)
#define IB_DSE_STAT_TIMEOUT		(1 << 7)

#define OB_DSE_STAT_ERROR_MASK		0x3C
#define OB_DSE_STAT_TRANS_ERR		(1 << 5)
#define OB_DSE_STAT_UPDATE_ERR		(1 << 4)
#define OB_DSE_STAT_DESC_ERR		(1 << 3)
#define OB_DSE_STAT_FETCH_ERR		(1 << 2)
#define OB_DSE_STAT_SLEEPING		(1<<8)

#define OB_DSE_DESC_ERROR_MASK		0x400
#define OB_HDR_DESC_AXI_ERR		(1 << 10)
#define OB_HDR_DESC_DONE		(1 << 8)

/******************************************************************************
**  Type Definitions for registers & APIs
******************************************************************************/

#ifdef __KERNEL__

/* Outbound data descriptor */
struct axxia_rio_ods_data_desc {
	u32     dw0;
	u32     dw1;
	u32     dw2;
	u32     dw3;

	/* SW usage */
	void    *virt_data_buf;
	u32     sw1;
	u32     sw2;
	u32     sw3;
};

/* Inbound data descriptor */
struct axxia_rio_ids_data_desc {
	u32     dw0;
	u32     dw1;
	u32     dw2;
	u32     dw3;
	u32     dw4;

	/* SW usage */
	void    *virt_data_buf;
	u32     buf_status;
	u32     sw2;
};

/*
** The following data structure defines data streaming header descriptors
**  only used in outbound
*/
struct axxia_rio_ds_hdr_desc {
	u32     dw0;
	u32     dw1;
	u32     dw2;
	u32     dw3;
	u32     dw4;

	/* SW usage */
	void    *virt_data_buf;
	u32     buf_status;
	u32     sw2;
};

/*
** OBDS DSE configuration
*/
struct axxia_rio_obds_dse_cfg {
	spinlock_t	lock;
	u8		in_use;     /* if the DSE is in_use */
	u8		cos;
	u16		dest_id;
	u16		stream_id;
	u8		irqEnabled;
	char		name[16];

	/* header descriptor */
	u16		num_hdr_desc_free;
	u16		max_num_hdr_desc;

	/* data descriptor */
	u16		num_data_desc_free;
	u16		max_num_data_desc;
	u16		hdr_read_ptr;
	u16		hdr_write_ptr;
	u8		first_hdr_desc;

	u16		data_read_ptr;
	u16		data_write_ptr;
	u8		first_data_desc;

	struct axxia_rio_ds_hdr_desc    *ptr_obds_hdr_desc;
	struct axxia_rio_ods_data_desc  *ptr_obds_data_desc;
};

/*
** IBDS configuration
*/
struct axxia_ibds_virt_m_cfg {
	spinlock_t	lock;
	u32		in_use;     /* if the DSE is in_use */
	u8		cos;
	u16		dest_id;
	u16		stream_id;
	u16		source_id;
	char		name[16];

	u16		num_desc_free;
	u16		max_num_data_desc;
	u16		data_read_ptr;
	u16		data_write_ptr;

	struct axxia_rio_ids_data_desc    *ptr_ibds_data_desc;
	u32		desc_dbuf_size;
	u32		buf_add_ptr;
	u32		num_hw_written_bufs;

	u32		alias_reg_value;
	u16		virt_vsid;
	u16		num_data_streams;
};

/*
** The following data structure defines the configuration of the AXXIA
** data streaming feature used by the device-specific code of the
** current mport.
*/
struct axxia_rio_ds_cfg {
	/* IBDS */
	u16	     mtu;
	u32	     ibds_avsid_mapping;
	u16	     num_ibds_dses;
	u16	     num_ibds_virtual_m;
	struct axxia_ibds_virt_m_cfg ibds_vsid_m_cfg[RIO_MAX_NUM_IBDS_VSID_M];

	/* OBDS */
	u16	     num_obds_dses;
	struct axxia_rio_obds_dse_cfg obds_dse_cfg[RIO_MAX_NUM_OBDS_DSE];
};

/******************************************************************************
**  APIs exported to other kernel modules
******************************************************************************/

extern struct axxia_rio_ds_cfg*
	axxia_mport_to_ds_cfg(struct rio_mport *mport);

/* Open an OBDS data stream */
extern int axxia_open_ob_data_stream(
	struct rio_mport	*mport,
	void			*dev_id,
	int			dse_id,
	int			num_header_entries,
	int			num_data_entries);

/* Add user's data */
extern int axxia_add_ob_data_stream(
	struct rio_mport	*mport,
	int			dest_id,
	int			stream_id,
	int			cos,
	int			priority,
	int			is_hdr_desc,
	void			*buffer,
	int			data_len);

/* Data streaming global configuration */
extern int axxia_data_stream_global_cfg(
	struct rio_mport	*mport,
	int			mtu,
	int			ibds_avsid_mapping);

/* Open IBDS data stream */
extern int axxia_open_ib_data_stream(
	struct rio_mport	*mport,
	void			*dev_id,
	int			source_id,
	int			cos,
	int			desc_dbuf_size,
	int			num_entries);

/* Add IBDS data buffer */
extern int axxia_add_ibds_buffer(
	struct rio_mport	*mport,
	int			source_id,
	int			cos,
	void			*buf,
	int			buf_size);

/* Get IBDS data */
extern void *axxia_get_ibds_data(
	struct rio_mport	*mport,
	int			source_id,
	int			cos,
	int			*ptr_pdu_length,
	int			*ptr_stream_id);

/* Convert VSID to internal VSID */
int axxio_virt_vsid_convert(
	u32			vsid,
	u32			alias_reg,
	u16			*ptr_virt_vsid);

/* Close IBDS data streaming */
extern int axxia_close_ib_data_stream(
	struct rio_mport	*mport,
	int			source_id,
	int			cos);

/* Close OBDS data streaming */
extern int axxia_close_ob_data_stream(
	struct rio_mport	*mport,
	int			dse_id);

#endif /* __KERNEL__ */

#endif /* __RIO_AXXIA_H__ */
