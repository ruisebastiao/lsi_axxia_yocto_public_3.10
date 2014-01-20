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

/* #define DEBUG */

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
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/io.h>
/* #include <asm/machdep.h> */
#include <linux/uaccess.h>

#include "axxia-rio.h"
#include "axxia-rio-irq.h"
#include "axxia-rio-ds.h"

/* #define DS_DEBUG 1 */

/* #define ALLOC_BUF_BY_KERNEL 1 */

static inline void __ib_virt_m_dbg(
	struct rio_ds_ibds_vsid_m_stats *ptr_ib_stats,
	u32 virt_m_stat);

static inline void __ob_dse_dbg(
	struct rio_ds_obds_dse_stats *ptr_ob_stats,
	u32 dse_stat);

static inline void __ob_dse_dw_dbg(
	struct rio_ds_obds_dse_stats *ptr_ob_stats,
	u32 dw0);

static inline void __ib_dse_dw_dbg(
	struct rio_ds_ibds_vsid_m_stats *ptr_ib_stats,
	u32 dw0);


static inline void __ib_virt_m_dbg(
	struct rio_ds_ibds_vsid_m_stats *ptr_ib_stats,
	u32 virt_m_stat)
{
	if (virt_m_stat & IB_VIRT_M_STAT_ERROR_MASK) {
		if (virt_m_stat & IB_VIRT_M_STAT_PDU_DROPPED)
			ptr_ib_stats->num_dropped_pdu++;

		if (virt_m_stat & IB_VIRT_M_STAT_SEG_LOSS)
			ptr_ib_stats->num_segment_loss++;

		if (virt_m_stat & IB_VIRT_M_STAT_MTU_LEN_MIS_ERR)
			ptr_ib_stats->num_mtu_len_mismatch_err++;

		if (virt_m_stat & IB_VIRT_M_STAT_PDU_LEN_MIS_ERR)
			ptr_ib_stats->num_pdu_len_mismatch_err++;

		if (virt_m_stat & IB_VIRT_M_STAT_TRANS_ERR)
			ptr_ib_stats->num_data_transaction_err++;

		if (virt_m_stat & IB_VIRT_M_STAT_UPDATE_ERR)
			ptr_ib_stats->num_desc_update_err++;

		if (virt_m_stat & IB_VIRT_M_STAT_TIMEOUT_ERR)
			ptr_ib_stats->num_timeout_err++;

		if (virt_m_stat & IB_VIRT_M_STAT_FETCH_ERR)
			ptr_ib_stats->num_desc_fetch_err++;
	}
}

static inline void __ob_dse_dbg(
	struct rio_ds_obds_dse_stats *ptr_ob_stats,
	u32 dse_stat)
{
	if (dse_stat & OB_DSE_STAT_ERROR_MASK) {
		if (dse_stat & OB_DSE_STAT_TRANS_ERR)
			ptr_ob_stats->num_desc_data_transaction_err++;

		if (dse_stat & OB_DSE_STAT_UPDATE_ERR)
			ptr_ob_stats->num_desc_update_err++;

		if (dse_stat & OB_DSE_STAT_DESC_ERR)
			ptr_ob_stats->num_desc_err++;

		if (dse_stat & OB_DSE_STAT_FETCH_ERR)
			ptr_ob_stats->num_desc_fetch_err++;
	}
}

static inline void __ob_dse_dw_dbg(
	struct rio_ds_obds_dse_stats *ptr_ob_stats,
	u32 dw0)
{
	if (dw0 & OB_DSE_DESC_ERROR_MASK) {
		if (dw0 & OB_HDR_DESC_AXI_ERR)
			ptr_ob_stats->num_desc_axi_err++;
	}
	if (dw0 & OB_HDR_DESC_DONE)
		ptr_ob_stats->num_desc_transferred++;
}

static inline void __ib_dse_dw_dbg(
	struct rio_ds_ibds_vsid_m_stats *ptr_ib_stats,
	u32 dw0)
{
	if (dw0 & IB_DSE_DESC_ERROR_MASK) {
		if (dw0 & IB_DSE_DESC_AXI_ERR)
			ptr_ib_stats->num_desc_axi_err++;

		if (dw0 & IB_DSE_DESC_DS_ERR)
			ptr_ib_stats->num_desc_ds_err++;
	}

	if (dw0 & IB_DSE_DESC_DONE)
		ptr_ib_stats->num_desc_transferred++;
}

/*****************************************************************************
 * axxia_data_stream_global_cfg -
 *
 *  This function sets global configuration of data streaming related
 *	registers.
 *
 * @mport:	pointer to the master port
 * @mtu:	Maximum Transmission Unit - controls the data payload size for
 *			segments of an encapsulated PDU.
 * @ibds_avsid_mapping:	this field definesmapping from incoming VSID
 *			(Combination of Source ID and COS) to internal
 *			(aliased) VSID.
 *
 * Returns %0 on success
 ****************************************************************************/
int axxia_data_stream_global_cfg(
	struct rio_mport    *mport,
	int			mtu,
	int			ibds_avsid_mapping)
{
	struct rio_priv *priv = mport->priv;
	struct rio_ds_priv  *ptr_ds_priv = &(priv->ds_priv_data);
	struct ibds_virt_m_cfg  *ptr_virt_m_cfg;
	struct rio_obds_dse_cfg *ptr_dse_cfg;
	int		reg_val;
	u32		mtu_value = 0;
	int	i;

	/* sanity check */
	if ((mtu < 32)		||
		(mtu > 256)) {
		return -EINVAL;
	}

	/* The global configuration can only be done if there is no OBDSE or
	**	IBDS ALIAS M is used.
	*/
	for (i = 0; i < RIO_MAX_NUM_IBDS_VSID_M; i++) {
		ptr_virt_m_cfg = &(ptr_ds_priv->ibds_vsid_m_cfg[i]);
		if (ptr_virt_m_cfg->in_use == RIO_DS_TRUE)
			return -EINVAL;
	}

	for (i = 0; i < RIO_MAX_NUM_OBDS_DSE; i++) {
		ptr_dse_cfg = &(ptr_ds_priv->obds_dse_cfg[i]);
		if (ptr_dse_cfg->in_use == RIO_DS_TRUE)
			return -EINVAL;
	}

	/*
	** Data Streaming Logical Layer Control Command and Status Register
	**	MTU bits [31:24] 8 - 32 bytes, 9 - 36 bytes etc.
	*/
	reg_val = 0;
	mtu_value = mtu / 4;
	reg_val |= (mtu_value  & 0xFF);

	__rio_local_write_config_32(mport, GRIO_DSLL_CCSR, reg_val);

	/* IBDS alias mapping register */
	reg_val = 0;
	reg_val |= (ibds_avsid_mapping & 0xFFFFF);
	__rio_local_write_config_32(mport, RAB_IBDS_VSID_ALIAS, reg_val);

	/* save information in the system */
	ptr_ds_priv->mtu = mtu;
	ptr_ds_priv->ibds_avsid_mapping = ibds_avsid_mapping;

	return 0;
}
EXPORT_SYMBOL(axxia_data_stream_global_cfg);

/*****************************************************************************
 * axxia_open_ob_data_stream -
 *
 *  This function sets up a descriptor chain to an outbound data streaming
 *  engine (DSE).
 *
 *	There are two types of descriptor usage:
 *	1. Single header descriptors. Each descriptor points to a data buffer
 *	   that is a full PDU length.
 *	2. Header and data descriptor combination. Each header descriptor
 *	   points to a data descriptor, each data descriptor points to a
 *	   4KB data buffer.
 *
 *  Under current implementation, only single descriptor is supported.
 *
 * @mport:		Pointer to the master port
 * @dev_id: 		Device specific pointer to pass on event
 * @dse_id:		DSE ID in the range of [0, 15]
 * @num_header_entries:	Number of header descriptors in the descriptor chain
 * @num_data_entries:	Number of data descriptors in the descriptor chain
 *
 * Returns %0 on success
 ****************************************************************************/
int axxia_open_ob_data_stream(
	struct rio_mport    	*mport,
	void			*dev_id,
	int			dse_id,
	int			num_header_entries,
	int			num_data_entries)
{
	int		 	rc = 0;
	struct rio_priv *priv = mport->priv;

	axxia_api_lock(priv);

	rc = open_ob_data_stream(mport,
				dev_id,
				dse_id,
				num_header_entries,
				num_data_entries);

	axxia_api_unlock(priv);

	return rc;
}
EXPORT_SYMBOL(axxia_open_ob_data_stream);

/*****************************************************************************
 * open_ob_data_stream -
 *
 *  This function sets up a descriptor chain to an outbound data streaming
 *  engine (DSE). It is called by axxia_open_ob_data_stream( ).
 *
 * @mport:		Pointer to the master port
 * @dev_id: 		Device specific pointer to pass on event
 * @dse_id:		DSE ID in the range of [0, 15]
 * @num_header_entries:	Number of header descriptors in the descriptor chain
 * @num_data_entries:	Number of data descriptors in the descriptor chain
 *
 * Returns %0 on success
 ****************************************************************************/
int open_ob_data_stream(
	struct rio_mport    	*mport,
	void		    	*dev_id,
	int			dse_id,
	int			num_header_entries,
	int			num_data_entries)
{
	struct rio_priv *priv = mport->priv;
	struct rio_ds_priv  *ptr_ds_priv = &(priv->ds_priv_data);
	struct rio_obds_dse_cfg *ptr_dse_cfg;
	u32		    temp;
	void		*ptr;
	struct rio_irq_handler *h;
	u32 des_chain_start_addr_phy_low, des_chain_start_addr_phy_hi;
	u32 dse_ctrl;
	unsigned long dse_chain_start_addr_phy;

	int rc = 0;

	/* Check if the dse_id is in use */
	ptr_dse_cfg = &(ptr_ds_priv->obds_dse_cfg[dse_id]);

	if (ptr_dse_cfg->in_use)
		return -EINVAL;

	/* allocate header descriptor buffers */
	if (!num_header_entries) {
		return -EINVAL;
	} else {
		ptr = kzalloc((num_header_entries *
					  sizeof(struct rio_ds_hdr_desc) +
					  RIO_DS_DESC_ALIGNMENT),
					  GFP_KERNEL);
		if (!ptr) {
			return -ENOMEM;
		} else {
			temp = (u32)(ptr) % (RIO_DS_DESC_ALIGNMENT);
			if (temp)
				ptr = (char *)ptr +
					(RIO_DS_DESC_ALIGNMENT -
					temp);

			/* check if the ptr is 8 word alignment */
			if (((unsigned int)ptr & 0x1F) != 0) {
				/* free the buffer */
				kfree(ptr);

				return -ENOMEM;
			}

			ptr_dse_cfg->ptr_obds_hdr_desc = ptr;
		}
	}

	/* allocate data descriptors */
	if (num_data_entries) {
		ptr = kzalloc((num_data_entries *
					   sizeof(struct rio_ods_data_desc) +
					   RIO_DS_DESC_ALIGNMENT),
					   GFP_KERNEL);

		if (!ptr)
			return -ENOMEM;
		else{
			temp = (u32)(ptr) % (RIO_DS_DESC_ALIGNMENT);
			if (temp)
				ptr = (char *)ptr +
					(RIO_DS_DESC_ALIGNMENT -
					temp);

			/* check if the ptr is 8 word alignment */
			if (((unsigned int)ptr & 0x1F) != 0) {
				/* free the buffer */
				kfree(ptr);
				return -ENOMEM;
			}

			ptr_dse_cfg->ptr_obds_data_desc = ptr;
		}
	}

	ptr_dse_cfg->in_use = RIO_DS_TRUE;

	/* get the chain start address */
	dse_chain_start_addr_phy =
		virt_to_phys((void *)(ptr_dse_cfg->ptr_obds_hdr_desc));

	/*
	** program chain start address
	**
	**  The OBDSE_DESC_ADDR reg holds lower 32 bits of the descriptor
	**  chain address
	**  The RAB_OBDSE_CTRL register holds the upper bit
	**
	**  There is also a requirement for the start of the descriptor chain
	**  address, it has to be 8 words aligned. TBD
	*/
	des_chain_start_addr_phy_low =
		(dse_chain_start_addr_phy >> 5) & 0xFFFFFFFF;
	des_chain_start_addr_phy_hi =
		(((u64)dse_chain_start_addr_phy >> 37)) & 0x1;

	__rio_local_read_config_32(mport, RAB_OBDSE_CTRL(dse_id), &dse_ctrl);
	dse_ctrl |= ((des_chain_start_addr_phy_hi << 31) & 0x80000000);
	__rio_local_write_config_32(mport, RAB_OBDSE_CTRL(dse_id), dse_ctrl);
	__rio_local_write_config_32(mport, RAB_OBDSE_DESC_ADDR(dse_id),
				des_chain_start_addr_phy_low);

	h = &(ptr_ds_priv->ob_dse_irq[dse_id]);

	sprintf(ptr_dse_cfg->name, "obds-%d", dse_id);
	rc = alloc_irq_handler(h, (void *)ptr_dse_cfg, ptr_dse_cfg->name);

	if (rc == 0) {
		ptr_dse_cfg->max_num_hdr_desc = num_header_entries;
		ptr_dse_cfg->num_hdr_desc_free = num_header_entries;
		ptr_dse_cfg->hdr_read_ptr = 0;
		ptr_dse_cfg->hdr_write_ptr = 0;

		ptr_dse_cfg->max_num_data_desc = num_data_entries;
		ptr_dse_cfg->num_data_desc_free = num_data_entries;
		ptr_dse_cfg->data_read_ptr = 0;
		ptr_dse_cfg->data_write_ptr = 0;

#if 0
		/* enable the DSE */
		/* start, wake up the engine */
		__rio_local_read_config_32(mport,
						RAB_OBDSE_CTRL(dse_id),
						&dse_ctrl);
		dse_ctrl |= DSE_ENABLE;
		__rio_local_write_config_32(mport,
						RAB_OBDSE_CTRL(dse_id),
						dse_ctrl);
#endif
	}

	return rc;
}

/*****************************************************************************
 * axxia_add_ob_data_stream -
 *
 *  This function adds a descriptor and a data buffer to a descriptor chain.
 *
 *	To keep the correct order of a data stream, data descripors of the same
 *	stream ID goes to the same DSE descriptor chain. However, each DSE can
 *	handle multiple data streams. To make it simple, a data stream with
 *	stream ID goes to (stream ID % (totoal number of DSEs)) descriptor
 *	chain.
 *
 *	Under the current implementation, only header descriptor is supported.
 *
 *	The buffer will be freed when the associated descriptor is processed
 *	in the ob_dse_irq_handler( ) routine.
 *
 * @mport:	  	Pointer to the master port
 * @dest_id:	 	Destination ID of the data stream
 * @stream_id:		Data stream ID
 * @cos:	    	Class of service of the stream
 * @priority:		Priority of the data stream
 * @is_hdr_desc:	Indicate if the descriptor a header descriptor
 *			or data descriptor
 * @buffer:	 	Pointer to where the data is stored
 * @data_len:     	Data buffer length associated with the descriptor
 *
 * Returns %0 on success
 ****************************************************************************/
int axxia_add_ob_data_stream(
	struct rio_mport	*mport,
	int		     	dest_id,
	int		     	stream_id,
	int		     	cos,
	int			priority,
	int			is_hdr_desc,
	void		    	*buffer,
	int		     	data_len)
{
	struct rio_priv *priv = mport->priv;
	struct rio_ds_priv      *ptr_ds_priv;
	struct rio_obds_dse_cfg *ptr_dse_cfg;
	struct rio_ds_hdr_desc  *ptr_hdr_desc;
	u16		hdr_write_ptr, next_desc_index;
	u16     dse_id;
	u32     dse_ctrl, dse_stat;
	u32		next_desc_high, data_buf_high;
	unsigned long	next_desc_ptr_phy, data_buf_phy;
	int rc = 0;

	/* sanity check - TBD */
	ptr_ds_priv = &(priv->ds_priv_data);

	/*
	** There are maximum of 16 DSEs, each DSE can handle one
	**	descriptor chain, however
	**	different stream_id can be chained in the same
	**	descriptor chain.
	*/
	dse_id = (stream_id % (ptr_ds_priv->num_obds_dses));
	ptr_dse_cfg = &(ptr_ds_priv->obds_dse_cfg[dse_id]);

	/* if the DSE has not been configured, return an error */
	if (ptr_dse_cfg->in_use == RIO_DS_FALSE)
		return -EINVAL;

	/* check if there is a space for the new data */
	if (is_hdr_desc) {
		if (ptr_dse_cfg->num_hdr_desc_free == 0)
			return -ENOMEM;

		/* get the header descriptor */
		hdr_write_ptr = ptr_dse_cfg->hdr_write_ptr;
		ptr_hdr_desc = &(ptr_dse_cfg->ptr_obds_hdr_desc[hdr_write_ptr]);

		/*
		** program the header descriptor word0
		** The int_enable and valid bits are programmed when data
		** is ready to be sent.
		*/
		ptr_hdr_desc->dw0 = 0;
		/* dest_id - [31:16]  */
		ptr_hdr_desc->dw0 |= ((dest_id << 16) & 0xFFFF0000);
		/* descriptor type - bit 2 (1 - header descriptor) */
		ptr_hdr_desc->dw0 |= ((1 << 2) & 0x4);
		/* single_desc - bit 3 (1 - single descriptor) */
		ptr_hdr_desc->dw0 |= ((1 << 3) & 0x8);

		/*
		** end of descriptor chain
		**
		**If the end of descriptor chain bit is set to 1, next time
		**a new descriptor is added, the start address of the chain
		**needs to be reprogrammed.
		**
		**If the end of descriptor chain bit is always set to 0,
		**the dreiver needs to make sure that the valid bit of next
		**descriptors must be set as invalid.
		**
		**ptr_hdr_desc->dw0 |= ((1<<4) & 0x10);
		*/

		/* next descriptor valid bit */
		ptr_hdr_desc->dw0 |= 2;

		/* program the header descriptor word1 */
		ptr_hdr_desc->dw1 = 0;
		/* stream_id - bits [0:16] */
		ptr_hdr_desc->dw1 |= (stream_id & 0xFFFF);
		/*
		** pdu_len - [17:31] 000 - 64KB
		** if it is 64KB,the dw1 field is programmed as 0
		*/
		if (data_len != RIO_DS_DATA_BUF_64K)
			ptr_hdr_desc->dw1 |= ((data_len << 16) & 0xFFFF0000);

		/* program the header descritpor word2 */
		ptr_hdr_desc->dw2 = 0;
		/* cos - bits [2:9] */
		ptr_hdr_desc->dw2 |= ((cos << 2) & 0x3FC);
		/* priority - bits [1:0] */
		ptr_hdr_desc->dw2 |= (priority & 0x3);

		if (ptr_dse_cfg->hdr_write_ptr ==
			(ptr_dse_cfg->max_num_hdr_desc - 1)) {
			next_desc_index = 0;
			ptr_dse_cfg->hdr_write_ptr = 0;
		} else {
			next_desc_index = ptr_dse_cfg->hdr_write_ptr + 1;
			ptr_dse_cfg->hdr_write_ptr++;
		}

		next_desc_ptr_phy = virt_to_phys((void *)
		(&(ptr_dse_cfg->ptr_obds_hdr_desc[next_desc_index])));

		/*
		** The next_desc_addr - 38-bit AXI addressing
		**  next_desc_addr[37] - h_dw2 [28]
		**  next_desc_addr[36:5] - h_dw3[0:31] - next_desc_address has
		**                                       to be 8 words aligned
		*/
		ptr_hdr_desc->dw3 =
			(u32)((next_desc_ptr_phy >> 5) & 0xFFFFFFFF);
		next_desc_high = ((u64)next_desc_ptr_phy >> 37) & 0x1;
		ptr_hdr_desc->dw2 |= ((next_desc_high << 28) & 0x10000000);

		/* program the data buffer in the descriptor */
		data_buf_phy = virt_to_phys(buffer);

		/*
		** data_address - 38-bit AXI addressing
		** data_address[0:31] - h_dw4 [0:31]
		** data_address[32:37] - h_dw2[22:27]
		*/
		ptr_hdr_desc->dw4 = (u32)((data_buf_phy) & 0xFFFFFFFF);

		data_buf_high = (((u64)data_buf_phy >> 32) & 0x3F);
		ptr_hdr_desc->dw2 |= (((data_buf_high) << 22) & 0xFC00000);

		/* set the en_int and valid bit of the header descriptor */
		ptr_hdr_desc->dw0 |= 0x1;
		ptr_hdr_desc->dw0 |= ((1 << 5) & 0x20);

		ptr_hdr_desc->virt_data_buf = (u32)buffer;

		ptr_hdr_desc->buf_status = DS_DBUF_ALLOC;

		ptr_dse_cfg->num_hdr_desc_free--;

	} else {
		/* header and data descriptor combination support TBD */
		return -EINVAL;
	}

	/* check if the DSE is in sleep mode, if it is, wake up */
	/* find out DSE stats */
	__rio_local_read_config_32(mport, RAB_OBDSE_STAT(dse_id), &dse_stat);


  /*  if (dse_stat & OB_DSE_STAT_SLEEPING) TBD */ {
		/* start, wake up the engine */
		__rio_local_read_config_32(mport,
						RAB_OBDSE_CTRL(dse_id),
						&dse_ctrl);

		/* check if the DSE is enabled */
		if (!(dse_ctrl & OB_DSE_PREFETCH))
			dse_ctrl |= OB_DSE_PREFETCH;

		/* check if the DSE preftech is enabled */
		if (!(dse_ctrl & OB_DSE_ENABLE))
			dse_ctrl |= OB_DSE_ENABLE;

		/*
		** the wakeup bit is self-clear bit, thus, it
		**	needs to be enabled each time
		*/
		dse_ctrl |= OB_DSE_WAKEUP;

		__rio_local_write_config_32(mport,
						RAB_OBDSE_CTRL(dse_id),
						dse_ctrl);
	}

	return rc;
}
EXPORT_SYMBOL(axxia_add_ob_data_stream);

/**
 * ob_dse_irq_handler - Outbound data streaming interrupt handler
 * --- Called in threaded irq handler ---
 * @h: Pointer to interrupt-specific data
 *
 * Handles outbound data streaming interrupts.  Executes a callback,
 * if available, on each successfully sent data stream.
 *
*/
void ob_dse_irq_handler(struct rio_irq_handler *h, u32 state)
{
	struct rio_mport *mport = h->mport;
	struct rio_priv *priv = mport->priv;
	struct rio_ds_priv      *ptr_ds_priv;
	struct rio_obds_dse_cfg *ptr_dse_cfg;
	struct rio_ds_hdr_desc  *ptr_hdr_desc = h->data;
	u32 dse_stat, dse_id;
	u16 hdr_read_ptr;
	u32	is_hdr_desc_done = 1;
	unsigned long flags;
	u8 	i;

	u32 num_desc_processed = 0;

	/* find the DSE that gets interrupted, CNTLZW found the upper
	** bit first */
	for (i = 0; i < 32; i++) {
		/* if the corresponding interrupt bit is set */
		if ((state >> i) & 0x1)
			break;
	}

	if (i == 32)
		return;

	dse_id = i;

	/* find out DSE stats */
	__rio_local_read_config_32(mport, RAB_OBDSE_STAT(dse_id), &dse_stat);


	/*
	** The ARM could also got interrupted with dse_stat sticky status
	**	bits not being set. TBD
	*/
	if (!(dse_stat & 0x3F))
		return;

	ptr_ds_priv = &(priv->ds_priv_data);

	/**
	 * Wait for all pending transactions to finish before doing descriptor
	 * updates
	 */
	ptr_dse_cfg = &(ptr_ds_priv->obds_dse_cfg[dse_id]);
	spin_lock_irqsave(&ptr_dse_cfg->lock, flags);

	/*
	** It is possible that one DSE handles multiple data streams,
	** thus the error condition does not reflect a specific descriptor
	** condition. We log the DSE stats but report per descriptor error
	** condition.
	*/
	/* check DSE registers for error reports */
	__ob_dse_dbg(&(ptr_ds_priv->ob_dse_stats[dse_id]), dse_stat);


	/* process all completed transactions - bit 1 - descriptor transaction
	** completed */
	hdr_read_ptr = ptr_dse_cfg->hdr_read_ptr;

	ptr_hdr_desc =
		&(ptr_dse_cfg->ptr_obds_hdr_desc[hdr_read_ptr]);

	if (dse_stat & 0x2) {

		is_hdr_desc_done = (ptr_hdr_desc->dw0 & OB_HDR_DESC_DONE);

		while (is_hdr_desc_done) {
			num_desc_processed++;
			__ob_dse_dw_dbg(&(ptr_ds_priv->ob_dse_stats[dse_id]),
				ptr_hdr_desc->dw0);

			/* free the buffer */
			if (ptr_hdr_desc->buf_status != DS_DBUF_FREED) {
				kfree((void *)ptr_hdr_desc->virt_data_buf);
				ptr_hdr_desc->buf_status = DS_DBUF_FREED;
			}

			if (ptr_dse_cfg->hdr_read_ptr ==
				(ptr_dse_cfg->max_num_hdr_desc - 1)) {
				ptr_dse_cfg->hdr_read_ptr = 0;
			} else {
				ptr_dse_cfg->hdr_read_ptr++;
			}
			/* free the buffer */
			ptr_dse_cfg->num_hdr_desc_free++;

			/* set the valid bit, done bit to be zero */
			ptr_hdr_desc->dw0 &= 0xFFFFFFFE;

			hdr_read_ptr = ptr_dse_cfg->hdr_read_ptr;

			ptr_hdr_desc =
				&(ptr_dse_cfg->ptr_obds_hdr_desc[hdr_read_ptr]);

			is_hdr_desc_done =
				(ptr_hdr_desc->dw0 & OB_HDR_DESC_DONE);
		}
	} else {
		/* TBD when HW gets AXI error, it will stop and not go further*/
	}
	/* clear the interrupt bit */
	__rio_local_write_config_32(mport,
					RAB_OBDSE_STAT(dse_id),
					(dse_stat & 0x3F));

	spin_unlock_irqrestore(&ptr_dse_cfg->lock, flags);

	return;
}

/*****************************************************************************
 * axxia_close_ob_data_stream -
 *
 *  This function closes an outbound data streaming associated with the DSE.
 *
 * @mport:	 Pointer to the master port
 * @dse_id:	 DSE ID
 *
 * Returns %0 on success
 ****************************************************************************/
int axxia_close_ob_data_stream(
	struct rio_mport    	*mport,
	int		 	dse_id)
{
	struct rio_priv *priv = mport->priv;
	struct rio_ds_priv      *ptr_ds_priv = &(priv->ds_priv_data);
	struct rio_obds_dse_cfg *ptr_dse_cfg;
	struct rio_ds_hdr_desc  *ptr_hdr_desc;
	u32    dse_ctrl, i;

	axxia_api_lock(priv);

	ptr_dse_cfg = &(ptr_ds_priv->obds_dse_cfg[dse_id]);

	if (ptr_dse_cfg->in_use == RIO_DS_FALSE) {
		axxia_api_unlock(priv);
		return 0;
	}

	/* reset variables */
	ptr_dse_cfg->in_use = RIO_DS_FALSE;
	ptr_dse_cfg->data_read_ptr = 0;
	ptr_dse_cfg->data_write_ptr = 0;
	ptr_dse_cfg->hdr_read_ptr = 0;
	ptr_dse_cfg->hdr_write_ptr = 0;
	ptr_dse_cfg->num_hdr_desc_free = 0;
	ptr_dse_cfg->num_data_desc_free = 0;

	/* clear OBDS data buffers */
	for (i = 0; i < (ptr_dse_cfg->max_num_data_desc); i++) {
		ptr_hdr_desc = ptr_dse_cfg->ptr_obds_hdr_desc;

		/* if an application has not yet retrieve the data */
		if (((ptr_hdr_desc->buf_status == DS_DBUF_ALLOC)) &&
			(ptr_hdr_desc->virt_data_buf)) {
			kfree((void *)ptr_hdr_desc->virt_data_buf);
		}
	}

	/* free header and data descriptor */
	if (ptr_dse_cfg->ptr_obds_hdr_desc != NULL)
		kfree(ptr_dse_cfg->ptr_obds_hdr_desc);

	if (ptr_dse_cfg->ptr_obds_data_desc != NULL)
		kfree(ptr_dse_cfg->ptr_obds_data_desc);

	/* Disable the corresponding DSE */
	__rio_local_read_config_32(mport, RAB_OBDSE_CTRL(dse_id), &dse_ctrl);
	dse_ctrl &= 0xFFFFFFFE;
	__rio_local_write_config_32(mport, RAB_OBDSE_CTRL(dse_id), dse_ctrl);

	/* release the IRQ handler */
	release_irq_handler(&(ptr_ds_priv->ob_dse_irq[dse_id]));

	axxia_api_unlock(priv);

	return 0;
}
EXPORT_SYMBOL(axxia_close_ob_data_stream);

/*****************************************************************************
 * axxia_open_ib_data_stream -
 *
 *  This function sets up a descriptor chain to an internal alias VSID (AVSID).
 *  The internal VSID is calculated through source_id, class of service (cos)
 *  and the RAB_IBDS_VSID_ALIAS register.
 *
 *	Please refer to the data sheet of RAB_IBDS_VSID_ALIAS register for
 *	detail mapping information.
 *
 *	In the IBDS, there are only 7 buffer sizes
 *	(1KB, 2KB, 4KB, 8KB, 16KB, 32KB, 64KB) can be programmed in the
 *	hardware.
 *	If the incoming PDU length is larger than the programmed buffer size,
 *	data error will occur. Thus, an application must program desc_dbuf_size
 *	larger than or equal to the expected PDU.
 *
 * @mport:			Pointer to the master port
 * @source_id:   	Source ID of the data stream
 * @cos:			Class of service of the stream
 * @desc_dbuf_size: Data buffer size the descriptor can handle
 * @num_entries:	Number of descriptors in this descriptor chain
 *
 * Returns %0 on success
 ****************************************************************************/
int axxia_open_ib_data_stream(
	struct rio_mport    	*mport,
	void			*dev_id,
	int			source_id,
	int 		    	cos,
	int			desc_dbuf_size,
	int			num_entries)
{
	int rc = 0;
	struct rio_priv *priv = mport->priv;

	axxia_api_lock(priv);

	rc = open_ib_data_stream(mport,
				 dev_id,
				 source_id,
				 cos,
				 desc_dbuf_size,
				 num_entries);
	axxia_api_unlock(priv);

	return rc;
}
EXPORT_SYMBOL(axxia_open_ib_data_stream);

/*****************************************************************************
 * open_ib_data_stream -
 *
 *  This function sets up a descriptor chain to an internal alias VSID (AVSID).
 *  It is called by axxia_open_ib_data_stream( ).
 *
 * @mport:		Pointer to the master port
 * @source_id:   	Source ID of the data stream
 * @cos:		Class of service of the stream
 * @desc_dbuf_size: Data buffer size the descriptor can handle
 * @num_entries:	Number of descriptors in this descriptor chain
 *
 * Returns %0 on success
 ****************************************************************************/
int open_ib_data_stream(
	struct rio_mport    	*mport,
	void			*dev_id,
	int			source_id,
	int 		    	cos,
	int			desc_dbuf_size,
	int			num_entries)
{
	struct rio_priv *priv = mport->priv;
	struct rio_ds_priv  *ptr_ds_priv = &(priv->ds_priv_data);
	struct ibds_virt_m_cfg  *ptr_virt_m_cfg;
	struct rio_ids_data_desc *ptr_data_desc;
	struct rio_irq_handler *h;
	void	*ptr;
	u32	temp;
	u32     alias_reg;
	u32     vsid, i, next_desc_offset;
	u16     virt_vsid;
	u8	hw_desc_size;
	unsigned long     desc_chain_start_addr_phy, next_desc_addr_phy;

	u32     next_desc_addr_hi, vsid_addr_reg;
	u32	num_int_entries;
	int rc = 0;

	/*
	** the desc_size is not actual size, it is numbered value
	**	0 - 64KB, 1 - 1K, 2 - 2K, 3 - 4k, 4 - 8K, 5 - 16K
	**	6 - 32K, 7 - 64K
	*/
	switch (desc_dbuf_size) {
	case RIO_IBDS_DATA_BUF_1K:
		hw_desc_size = 1;
	break;

	case RIO_IBDS_DATA_BUF_2K:
		hw_desc_size = 2;
	break;

	case RIO_IBDS_DATA_BUF_4K:
		hw_desc_size = 3;
	break;

	case RIO_IBDS_DATA_BUF_8K:
		hw_desc_size = 4;
	break;

	case RIO_IBDS_DATA_BUF_16K:
		hw_desc_size = 5;
	break;

	case RIO_IBDS_DATA_BUF_32K:
		hw_desc_size = 6;
	break;

	case RIO_IBDS_DATA_BUF_64K:
		hw_desc_size = 0;
	break;

	default:
		return -EINVAL;
	}

	/* TBD ASR_SPINLOCK_INTERRUPT_DISABLE(&priv->ioLock, lflags); */

	/* find the mapping between incoming VSID and internal VSID */
	__rio_local_read_config_32(mport, RAB_IBDS_VSID_ALIAS, &alias_reg);

	/* VSID = {16'b SourceID, 8'bCOS} */
	vsid = ((source_id & 0xFFFF) << 16) | (cos & 0xFF);

	/* calculate the virtual M index */
	(void)axxio_virt_vsid_convert(vsid, alias_reg, &virt_vsid);

	if (virt_vsid >= RIO_MAX_NUM_IBDS_VSID_M)
		return -EINVAL;

	/*
	** In the IBDS, the descriptor size allocated must be greater than
	** or equal to the PDU length.  The descriptor size can be 1K, 2K,
	** 4K, 8K, 16K, 32K, or 64K.
	*/
	/* get a internal VSID M based on virt_vsid */
	ptr_virt_m_cfg = &(ptr_ds_priv->ibds_vsid_m_cfg[virt_vsid]);

	/*
	** If the descriptor chain is already opened, return OK
	*/
	if (ptr_virt_m_cfg->in_use == RIO_DS_TRUE)
		return 0;

	ptr_virt_m_cfg->alias_reg_value = alias_reg;

	/*
	** If the end of chain bit is not set, it is required that there is
	** 	one invalid entry left in the system.
	*/
	num_int_entries = num_entries + 1;

	/* allocate data descriptor buffers */
	ptr = kzalloc((num_int_entries) *
			sizeof(struct rio_ids_data_desc) +
			sizeof(struct rio_ids_data_desc) +
			RIO_DS_DESC_ALIGNMENT,
			GFP_KERNEL);
	if (ptr == NULL) {
		return -ENOMEM;
	} else {
		temp = (u32)(ptr) % (RIO_DS_DESC_ALIGNMENT);
		if (temp)
			ptr = (char *)ptr + (RIO_DS_DESC_ALIGNMENT - temp);

		/* check if the ptr is 8 word alignment */
		if (((unsigned int)ptr & 0x1F) != 0) {
			kfree(ptr);
			return -ENOMEM;
		}

		ptr_virt_m_cfg->ptr_ibds_data_desc =
			(struct rio_ids_data_desc *)ptr;
	}

	ptr_virt_m_cfg->in_use = RIO_DS_TRUE;

	/* chain the data descriptors */
	for (i = 0; i < num_int_entries; i++) {
		ptr_data_desc = &(ptr_virt_m_cfg->ptr_ibds_data_desc[i]);

		/* init the data descriptor */
		memset((void *)ptr_data_desc,
			0,
			sizeof(struct rio_ids_data_desc));

		/* dw0 - desc_size, bits [4:6]
		**	the desc_size is not actual size, it is numbered value
		**	0 - 64KB, 1 - 1K, 2 - 2K, 3 - 4k, 4 - 8K, 5 - 16K
		**	6 - 32K, 7 - 64K
		*/
		ptr_data_desc->dw0 |= ((hw_desc_size << 4) & 0x70);
		/* dw0 - source_id, bits [16:31] */
		ptr_data_desc->dw0 |= ((source_id << 16) & 0xFFFF0000);

		/* next descriptor valid */
		ptr_data_desc->dw0 |= 0x2;
		/* enable interrupt bit */
		ptr_data_desc->dw0 |= 0x8;

		/*
		** end of descriptor chain
		**
		**If the end of descriptor chain bit is set to 1, next time
		**a new descriptor is added, the start address of the chain
		**needs to be reprogrammed.
		**
		**If the end of descriptor chain bit is always set to 0,
		**the dreiver needs to make sure that the valid bit of next
		**descriptors must be set as invalid. There also should always
		**have one more empty descriptor in the chain. TBD
		**
		**ptr_data_desc->dw0 |= 4;
		*/

		if (i == (num_int_entries-1))
			next_desc_offset = 0;
		else
			next_desc_offset = i + 1;

		/* cos - bit[16:23] */
		ptr_data_desc->dw2 |= ((cos << 16) & 0xFF0000);
		/*
		** next_desc_addr - 38-bit AXI addressing
		**  next_desc_addr[37] - dw2[24]
		**  next_desc_addr[36:5] - dw4[31:0]
		*/
		next_desc_addr_phy =
		virt_to_phys((void *)
			&ptr_virt_m_cfg->ptr_ibds_data_desc[next_desc_offset]);
		next_desc_addr_hi = ((u64)next_desc_addr_phy >> 37) & 0x1;

		ptr_data_desc->dw4 =
		((u64)next_desc_addr_phy >> 5) & 0xFFFFFFFF;

		ptr_data_desc->dw2 |= (next_desc_addr_hi << 24) & 0x1000000;
	}

	ptr_virt_m_cfg->cos = cos;
	ptr_virt_m_cfg->source_id = source_id;
	ptr_virt_m_cfg->desc_dbuf_size = desc_dbuf_size;
	ptr_virt_m_cfg->virt_vsid = virt_vsid;

	desc_chain_start_addr_phy =
		virt_to_phys((void *)
			&(ptr_virt_m_cfg->ptr_ibds_data_desc[0]));

	/*
	** desc_chain_start_addr - 38-bit AXI address
	**  M_LOW_ADDR[31:0] - chain_addr[36:5] - has to be 8 bytes
	**                                        alignment
	**  M_HIGH_ADDR[0] - chain_addr[37]
	*/
	/* program the start address of the descriptor chain */
	vsid_addr_reg = (desc_chain_start_addr_phy >> 5) & 0xFFFFFFFF;
	__rio_local_write_config_32(mport,
		RAB_IBDS_VSID_ADDR_LOW(virt_vsid), vsid_addr_reg);

	__rio_local_read_config_32(mport,
		RAB_IBDS_VSID_ADDR_HI(virt_vsid), &vsid_addr_reg);
	vsid_addr_reg |= (((u64)desc_chain_start_addr_phy >> 37) & 0x1);

	__rio_local_write_config_32(mport,
		RAB_IBDS_VSID_ADDR_HI(virt_vsid), vsid_addr_reg);

	/* register IRQ */
	h = &(ptr_ds_priv->ib_dse_vsid_irq[virt_vsid]);

	sprintf(ptr_virt_m_cfg->name, "ibds-%d", virt_vsid);

	rc = alloc_irq_handler(h, (void *)ptr_virt_m_cfg, ptr_virt_m_cfg->name);

	if (rc == 0) {
		ptr_virt_m_cfg->data_read_ptr = 0;
		ptr_virt_m_cfg->data_write_ptr = 0;
		ptr_virt_m_cfg->buf_add_ptr = 0;

		ptr_virt_m_cfg->num_desc_free = num_int_entries;

		ptr_virt_m_cfg->max_num_data_desc = num_int_entries;

	}

	return rc;
}

/*****************************************************************************
 * axxia_add_ibds_buffer -
 *
 *  This function adds a data buffer to a descriptor chain determined by
 *  source ID, class of service and RAB_IBDS_VSID_ALIAS register.
 *  When the hardware receives a PDU, it writes into the data buffer.
 *  Since we don't know the incoming PDU length, the buf_size must be
 *  large enough.
 *
 * @mport:	Pointer to the master port
 * @source_id:	Source ID of the data stream
 * @cos:	Class of service of the stream
 * @buf:	Pointer to where the data is stored
 * @buf_size: 	Size of the buffer
 *
 * Returns %0 on success
 ****************************************************************************/
int axxia_add_ibds_buffer(
	struct rio_mport   *mport,
	int		   source_id,
	int		   cos,
	void		  *buf,
	int		   buf_size)
{
	struct rio_priv *priv = mport->priv;
	struct rio_ds_priv      *ptr_ds_priv = &(priv->ds_priv_data);
	struct ibds_virt_m_cfg  *ptr_virt_m_cfg;
	struct rio_ids_data_desc *ptr_data_desc;
	u32		     	m_id;
	u8		      	found_one = RIO_DS_FALSE;
	u32			vsid_addr_reg;
	u32     vsid;
	u16		virt_vsid;
	u32     alias_reg;
	u32		vsid_m_stats;

	unsigned long   data_addr_phy;
	u32 data_addr_hi;

	unsigned long iflags;

	if (buf == NULL)
		return -EINVAL;

	/* search through the virtual M table to find the one that has
	**  the same source_id and cos */
	/* find the mapping between incoming VSID and internal VSID */
	__rio_local_read_config_32(mport, RAB_IBDS_VSID_ALIAS, &alias_reg);

	/* VSID = {16'b SourceID, 8'bCOS} */
	vsid = ((source_id & 0xFFFF) << 16) | (cos & 0xFF);

	/* calculate the virtual M index */
	(void)axxio_virt_vsid_convert(vsid, alias_reg, &virt_vsid);


	for (m_id = 0; m_id < RIO_MAX_NUM_IBDS_VSID_M; m_id++) {
		ptr_virt_m_cfg = &(ptr_ds_priv->ibds_vsid_m_cfg[m_id]);

		if ((ptr_virt_m_cfg->virt_vsid == virt_vsid)    &&
		    (ptr_virt_m_cfg->in_use == RIO_DS_TRUE)) {
			found_one = RIO_DS_TRUE;
			break;
		}
	}

	if (found_one == RIO_DS_FALSE)
		return RC_TBD;


#if 0
	/* check if the buf_size is smaller than the desc_dbuf_size - TBD */
	if (buf_size > ptr_virt_m_cfg->desc_dbuf_size)
		return -EINVAL;
#endif

	/*
	** check if there are descriptors left
	**
	**	Since the driver uses circular ring and linked the data
	**	descriptor during open_data_stream( ) function.
	**   If end_of_chain bit is not used, SW needs to always keep
	**		one data descriptor invalid, so that the HW will
	**		not overwrite the data buffer when SW can not
	**		keep up with. When SW can not keep up with,
	**		the HW will drop the newly received data.
	*/
	if (ptr_virt_m_cfg->num_desc_free == 1)
		return -ENOMEM;

     spin_lock_irqsave(&ptr_virt_m_cfg->lock, iflags);

	/* put user's buffer into the corresponding descriptors */
	ptr_data_desc =
	&(ptr_virt_m_cfg->ptr_ibds_data_desc[ptr_virt_m_cfg->buf_add_ptr]);

	ptr_data_desc->virt_data_buf = (u32)buf;

	data_addr_phy =
		virt_to_phys((void *)ptr_data_desc->virt_data_buf);

	ptr_data_desc->dw3 = ((u64)data_addr_phy & 0xFFFFFFFF);
	data_addr_hi = ((u64)data_addr_phy >> 32) & 0x3F;
	ptr_data_desc->dw2 |= (data_addr_hi << 26) & 0xFC000000;

	/* clear all the status bits that may be set before */
	ptr_data_desc->dw0 &= ~(IB_DSE_DESC_DONE);
	ptr_data_desc->dw0 &= ~(IB_DSE_DESC_AXI_ERR);
	ptr_data_desc->dw0 &= ~(IB_DSE_DESC_DS_ERR);

	/*
	**	set the valid bit to be 1
	** 	The valid bit has to be set prior to setting VSID_ADDR_HI reg
	*/
	ptr_data_desc->dw0 |= 0x1;

	ptr_data_desc->buf_status = DS_DBUF_ALLOC;

	/*
	** For the first descriptor, the VSID M Descriptor Chain Prefetch Enable
	**needs to be set to 1. After that, only VSID M Descriptor Chain
	**  Wakeup Enable needs to be set
	*/
	__rio_local_read_config_32(mport,
				RAB_IBDS_VSID_ADDR_HI(m_id),
				&vsid_addr_reg);

	/* if the prefetch enable is not set */
	if (!(vsid_addr_reg & IB_VSID_M_PREFETCH_ENABLE))
		vsid_addr_reg |= IB_VSID_M_PREFETCH_ENABLE;

	/* wakeup bit is alway set each time a new buffer is added */
	__rio_local_read_config_32(mport,
				RAB_IBVIRT_M_STAT(m_id),
				&vsid_m_stats);

	if (vsid_m_stats & IB_VIRT_M_STAT_SLEEPING)
		vsid_addr_reg |= IB_VSID_M_PREFETCH_WAKEUP;

	__rio_local_write_config_32(mport,
				RAB_IBDS_VSID_ADDR_HI(m_id),
				vsid_addr_reg);

	/* the buf_add_ptr is determined by number of free descriptors */
	if (ptr_virt_m_cfg->buf_add_ptr ==
		(ptr_virt_m_cfg->max_num_data_desc - 1)) {
		ptr_virt_m_cfg->buf_add_ptr = 0;
	} else {
		ptr_virt_m_cfg->buf_add_ptr++;
	}

	ptr_virt_m_cfg->num_desc_free--;

    spin_unlock_irqrestore(&ptr_virt_m_cfg->lock, iflags);

	return 0;
}
EXPORT_SYMBOL(axxia_add_ibds_buffer);

/**
 * ib_dse_vsid_m_irq_handler - Inbound data streaming interrupt handler
 * --- Called in threaded irq handler ---
 * @h: Pointer to interrupt-specific data
 *
 * Handles inbound data streaming interrupts.  Executes a callback,
 * if available, on each successfully received data stream
 *
*/
void ib_dse_vsid_m_irq_handler(struct rio_irq_handler *h, u32 state)
{
	struct rio_mport *mport = h->mport;
	struct rio_priv *priv = mport->priv;
	struct rio_ds_priv      *ptr_ds_priv = &(priv->ds_priv_data);
	struct ibds_virt_m_cfg  *ptr_virt_m_cfg;
	struct rio_ids_data_desc    *ptr_data_desc;
	u32 dse_stat, vsid_m_stats;
	u8  virt_vsid, dse_id;
	u16 data_write_ptr;
	unsigned long flags;
	u32	is_desc_done = 1;
	u8	i;

	for (i = 0; i < 32; i++) {
		/* if the corresponding interrupt bit is set */
		if ((state >> i) & 0x1) {
			virt_vsid = i;

			__rio_local_read_config_32(mport,
					RAB_IBVIRT_M_STAT(virt_vsid),
					&vsid_m_stats);

			/*
			** The ARM could also got interrupted with
			** vsid_m_stats sticky status
			**	bits not being set. TBD should be 3FF?
			*/
			if ((vsid_m_stats & 0x1FF)) {

				/* check if the chain transfer complete */
				ptr_virt_m_cfg =
				&(ptr_ds_priv->ibds_vsid_m_cfg[virt_vsid]);

				spin_lock_irqsave(&ptr_virt_m_cfg->lock, flags);

				/* check errors */
		__ib_virt_m_dbg(&(ptr_ds_priv->ib_vsid_m_stats[virt_vsid]),
				vsid_m_stats);


		if (vsid_m_stats & IB_VIRT_M_STAT_FETCH_ERR) {
			/*
			** If transaction pending bit is not set an timeout
			** 	is also not set,
			** 	that means that PDU was successfully written
			** 	into AXI memory
			**      and nothing needs to be done.
			** If transaction pending bit is set or timeout is set,
			**	engine needs
			** 	to be reset. After disabling engine, when
			**	transaction pending
			**	gets reset, engine is ready to be enabled again.
			*/

			/* check if there is a corresponding DSE
			 ** that handles this vsid */
			for (dse_id = 0;
			     dse_id < RIO_MAX_NUM_IBDS_DSE;
			     dse_id++) {
				__rio_local_read_config_32(mport,
						RAB_IBDSE_STAT(dse_id),
						&dse_stat);

			if (((dse_stat & IB_DSE_STAT_TRANS_PENDING)  ||
			     (dse_stat & IB_DSE_STAT_TIMEOUT))	&&
			     ((dse_stat & IB_DSE_VSID_IN_USED) == virt_vsid)) {
				/*
				** BZ43821 - SW workaround for the IBDS
				** descriptor fetch error
				** When S/W sees the descriptor fetch error
				** being indicated in
				** status bits, introduce a delay and then
				** disable the engine
				**  and enable the engine again.
				** With this the next incoming packet for that
				** engine would
				** not get corrupted.
				*/
				ndelay(5);

				/* disable the engine */
				__rio_local_write_config_32(mport,
							RAB_IBDSE_CTRL(dse_id),
							0);

				/*should wait till the pending bit is reset?*/

				/* enable the engine again */
				__rio_local_write_config_32(mport,
							RAB_IBDSE_CTRL(dse_id),
							1);

				break;
				}
			}
		}


	/* In case of timeout error, if not alreaday disabled, descriptor
	**	prefetch logic should be disabled and associated descriptor
	**	start address needs to be set for VSID PDUs to be
	**	eassembled again. Engine should be disabled, once
	** 	transaction pending gets reset, engine can be enabled again.
	**	TBD
	*/

	/* process maximum number of MAX_NUM_PROC_IBDS_DESC transactions */
	data_write_ptr = ptr_virt_m_cfg->data_write_ptr;

	ptr_data_desc =
			&(ptr_virt_m_cfg->ptr_ibds_data_desc[data_write_ptr]);

	/* get the done bit of the data descriptor */
	is_desc_done = (ptr_data_desc->dw0 & IB_DSE_DESC_DONE);

	while (is_desc_done) {
		ptr_virt_m_cfg->num_hw_written_bufs++;
		__ib_dse_dw_dbg(
				&(ptr_ds_priv->ib_vsid_m_stats[virt_vsid]),
				ptr_data_desc->dw0);

		if (data_write_ptr ==
			(ptr_virt_m_cfg->max_num_data_desc-1))
			data_write_ptr = 0;
		else
			data_write_ptr++;

		/* set the valid bit to be invalid */
		ptr_data_desc->dw0 &= 0xFFFFFFFE;

		ptr_data_desc =
			&(ptr_virt_m_cfg->ptr_ibds_data_desc[data_write_ptr]);

		is_desc_done = (ptr_data_desc->dw0 & IB_DSE_DESC_DONE);
	}

	ptr_virt_m_cfg->data_write_ptr = data_write_ptr;

	/* call back - TBD */

	/* clear the interrupt bit? - TBD */
	/* clear the virt_m stats bit */
	if (vsid_m_stats & 0x2) {
		__rio_local_write_config_32(mport,
					RAB_IBVIRT_M_STAT(virt_vsid),
					vsid_m_stats);
	}
	__rio_local_read_config_32(mport,
				RAB_IBVIRT_M_STAT(virt_vsid),
				&vsid_m_stats);

    spin_unlock_irqrestore(&ptr_virt_m_cfg->lock, flags);
		}

		}
	}

    return;
}

/*****************************************************************************
 * axxia_get_ibds_data -
 *
 *  This function gets an IBDS data from a descriptor chain. This function
 *  also returns the PDU length and stream ID associated with data.
 *  If there is no data available, a NULL pointer is returned.
 *
 * @mport:		Pointer to the master port
 * @source_id:		Source ID of the data stream
 * @cos:		Class of service of the stream
 * @ptr_pdu_length:	Pointer to where the PDU length is stored
 * @ptr_stream_id:	Pointer to where the stream ID is stored
 *
 * Returns %0 on success
 ****************************************************************************/
void *axxia_get_ibds_data(
	struct rio_mport   *mport,
	int		   source_id,
	int		   cos,
	int		   *ptr_pdu_length,
	int		   *ptr_stream_id)
{
	struct rio_priv *priv = mport->priv;
	struct rio_ds_priv      *ptr_ds_priv = &(priv->ds_priv_data);
	struct ibds_virt_m_cfg  *ptr_virt_m_cfg;
	struct rio_ids_data_desc *ptr_data_desc;
	u32		    m_id, data_read_ptr;
	u8		    found_one = RIO_DS_FALSE;
	void		    *user_buf;
	u32		pdu_length;
	unsigned long iflags;
	u32     vsid;
	u16		virt_vsid;
	u32     alias_reg;


	/* find the mapping between incoming VSID and internal VSID */
	__rio_local_read_config_32(mport, RAB_IBDS_VSID_ALIAS, &alias_reg);

	/* VSID = {16'b SourceID, 8'bCOS} */
	vsid = ((source_id & 0xFFFF) << 16) | (cos & 0xFF);

	/* calculate the virtual M index */
	(void)axxio_virt_vsid_convert(vsid, alias_reg, &virt_vsid);


	/* search through the virtual M table to find the one that
	** has the same source_id and cos */
	for (m_id = 0; m_id < RIO_MAX_NUM_IBDS_VSID_M; m_id++) {
		ptr_virt_m_cfg = &(ptr_ds_priv->ibds_vsid_m_cfg[m_id]);

		if ((ptr_virt_m_cfg->virt_vsid == virt_vsid)    &&
		    (ptr_virt_m_cfg->in_use == RIO_DS_TRUE)) {
			found_one = RIO_DS_TRUE;
			break;
		}
	}

	if (found_one == RIO_DS_FALSE)
		return NULL;

	/* check if the there are buffers that are written - semaphore ?*/
	if (ptr_virt_m_cfg->num_hw_written_bufs < 1)
		return NULL;

	spin_lock_irqsave(&ptr_virt_m_cfg->lock, iflags);

	data_read_ptr = ptr_virt_m_cfg->data_read_ptr;

	/* get the data descriptor */
	ptr_data_desc =
		&(ptr_virt_m_cfg->ptr_ibds_data_desc[data_read_ptr]);

	/* check if the source_id and cos matches */
	if ((((ptr_data_desc->dw0 >> 16) & 0xFFFF) != source_id) ||
		((ptr_data_desc->dw2 & 0xFF0000) >> 16) != cos) {
		spin_unlock_irqrestore(&ptr_virt_m_cfg->lock, iflags);
		return NULL;
	}

	user_buf = (void *)ptr_data_desc->virt_data_buf;

	if (user_buf == NULL) {
		*ptr_pdu_length = 0;
		spin_unlock_irqrestore(&ptr_virt_m_cfg->lock, iflags);
		return NULL;
	} else {
		pdu_length = ((ptr_data_desc->dw1 & 0xFFFF0000) >> 16);

		/* the pdu_length 0 in the HW indicates 64KB */
		if (pdu_length == 0)
			*ptr_pdu_length = 65536;
		else
			*ptr_pdu_length = pdu_length;
		*ptr_stream_id = (ptr_data_desc->dw2) & 0xFFFF;

		if (ptr_virt_m_cfg->data_read_ptr ==
			(ptr_virt_m_cfg->max_num_data_desc - 1)) {
			ptr_virt_m_cfg->data_read_ptr = 0;
		} else {
			ptr_virt_m_cfg->data_read_ptr++;
		}

		ptr_virt_m_cfg->num_hw_written_bufs--;
		ptr_virt_m_cfg->num_desc_free++;

		ptr_data_desc->buf_status = DS_DBUF_FREED;

		spin_unlock_irqrestore(&ptr_virt_m_cfg->lock, iflags);

		return user_buf;
	}

}
EXPORT_SYMBOL(axxia_get_ibds_data);

/*****************************************************************************
 * axxia_close_ib_data_stream -
 *
 *  This function closes an inbound data streaming.
 *
 * @mport:	  	Pointer to the master port
 * @source_id:     	Source ID of the data stream
 * @cos:	  	Class of service of the stream
 *
 * Returns %0 on success
 ****************************************************************************/
int axxia_close_ib_data_stream(
	struct rio_mport *mport,
	int		 source_id,
	int		 cos)
{
	struct rio_priv *priv = mport->priv;
	struct rio_ds_priv  *ptr_ds_priv = &(priv->ds_priv_data);
	struct ibds_virt_m_cfg  *ptr_virt_m_cfg;
	u8		      find_ava_virt_m = RIO_DS_FALSE;
	u8      i;
	struct rio_ids_data_desc *ptr_data_desc;
	u8 	virt_vsid;

	axxia_api_lock(priv);

	for (i = 0; i < (ptr_ds_priv->num_ibds_virtual_m); i++) {
		ptr_virt_m_cfg = &(ptr_ds_priv->ibds_vsid_m_cfg[i]);

		if ((ptr_virt_m_cfg->in_use == RIO_DS_TRUE)     &&
		    (ptr_virt_m_cfg->source_id == source_id)    &&
		    (ptr_virt_m_cfg->cos == cos)) {
			find_ava_virt_m = RIO_DS_TRUE;
			virt_vsid = i;
			break;
		}
	}

	if (find_ava_virt_m == RIO_DS_FALSE) {
		axxia_api_unlock(priv);
		return 0;
	}

	/* reset variables */
	ptr_virt_m_cfg->in_use = RIO_DS_FALSE;
	ptr_virt_m_cfg->data_read_ptr = 0;
	ptr_virt_m_cfg->data_write_ptr = 0;
	ptr_virt_m_cfg->num_desc_free = 0;
	ptr_virt_m_cfg->buf_add_ptr = 0;
	ptr_virt_m_cfg->num_hw_written_bufs = 0;

	/* release IRQ handler */
	release_irq_handler(&(ptr_ds_priv->ib_dse_vsid_irq[virt_vsid]));

	/* clear all the descriptors */
	for (i = 0; i < ptr_virt_m_cfg->max_num_data_desc; i++) {
		ptr_data_desc = &(ptr_virt_m_cfg->ptr_ibds_data_desc[i]);

		/* if an application has not yet retrieve the data */
		if (((ptr_data_desc->buf_status == DS_DBUF_ALLOC)) &&
			(ptr_data_desc->virt_data_buf)) {
			kfree((void *)ptr_data_desc->virt_data_buf);
		}
	}

	/* free the data descriptor pointer */
	if (ptr_virt_m_cfg->ptr_ibds_data_desc != NULL)
		kfree(ptr_virt_m_cfg->ptr_ibds_data_desc);

	axxia_api_unlock(priv);

	return 0;
}
EXPORT_SYMBOL(axxia_close_ib_data_stream);

/*****************************************************************************
 * axxio_virt_vsid_convert -
 *
 *  This function converts the VISD {16'bSourceID, 8'b cos} to the internal
 *  virtual VSID.  Please refer to Table 133 of rio_axi_datasheet.pdf
 *  for detail information.
 *
 * @vsid:	  	Incoming VSID
 * @alias_reg:  	RAB_IBDS_VSID_ALIAS register value
 *
 * @ptr_virt_vsid:	Pointer to where the alias VSID is stored
 *
 * Returns %0 on success
 ****************************************************************************/
int axxio_virt_vsid_convert(
	u32     vsid,
	u32     alias_reg,
	u16     *ptr_virt_vsid)
{
	u32    virt_vsid = 0;
	u32    bit_field = 0;
	u32    vsid_select;
	u32    temp_vsid;

	/* get AVSID[0] select from bit0 to bit 15 */
	vsid_select = alias_reg & 0xF;
	bit_field = ((vsid & (1<<vsid_select)) >> vsid_select) & 0x1;
	virt_vsid = bit_field;

	/* get AVSID[1] select from bit0 to bit 15 */
	vsid_select = (alias_reg & 0xF0) >> 4;
	bit_field = ((vsid & (1<<vsid_select)) >> vsid_select) & 0x1;
	virt_vsid |= (bit_field << 1);

	/* get AVSID[2] select from bit0 to bit 15 */
	vsid_select = (alias_reg & 0xF00) >> 8;
	bit_field = ((vsid & (1<<vsid_select)) >> vsid_select) & 0x1;
	virt_vsid |= (bit_field << 2);

	temp_vsid = (vsid >> 8) & 0xFFFF;

	/* get AVSID[3] select from bit8 to bit 23 */
	vsid_select = (alias_reg & 0xF000) >> 12;
	bit_field = ((temp_vsid & (1<<vsid_select)) >> vsid_select) & 0x1;
	virt_vsid |= (bit_field << 3);

	/* get AVSID[4] select from bit8 to bit 23 */
	vsid_select = (alias_reg & 0xF0000) >> 16;
	bit_field = ((temp_vsid & (1<<vsid_select)) >> vsid_select) & 0x1;
	virt_vsid |= (bit_field << 4);

	/* get AVSID[4] select from bit8 to bit 23 */
	vsid_select = (alias_reg & 0xF00000) >> 20;
	bit_field = ((temp_vsid & (1<<vsid_select)) >> vsid_select) & 0x1;
	virt_vsid |= (bit_field << 5);

	*ptr_virt_vsid = virt_vsid;

	return 0;
}

/*****************************************************************************
 * release_ob_ds - TBD
 *
 *  This is currently a stub function to be called in axxia_rio_port_irq_init().
 *
 * @h:
 *
 * Returns %0 on success
 ****************************************************************************/
void release_ob_ds(struct rio_irq_handler *h)
{
	return;
}

/*****************************************************************************
 * release_ib_ds - TBD
 *
 *  This is currently a stub function to be called in axxia_rio_port_irq_init().
 *
 * @h:
 *
 * Returns %0 on success
 ****************************************************************************/
void release_ib_ds(struct rio_irq_handler *h)
{
	return;
}

/*****************************************************************************
 * axxia_parse_dtb_ds -
 *
 *  Parse RapidIO platform entry for data streaming
 *
 * @dev: Device handle
 * @ptr_ds_dtb_info: Data extracted from the platform entry
 *
 * Returns:
 * -EFAULT          At failure
 * 0                Success
 ****************************************************************************/
int axxia_parse_dtb_ds(
	struct platform_device *dev,
	struct rio_ds_dtb_info *ptr_ds_dtb_info)
{
#if 0
	const u32 *cell;
	int rlen;
#endif
	u32 pval;

	memset(ptr_ds_dtb_info, 0, sizeof(struct rio_ds_dtb_info));

	/* set the default of ds_enable to be 1 if it is on 55XX */
	ptr_ds_dtb_info->ds_enabled = 1;

	/* Check if data streaming is enabled */
	if (!of_property_read_u32(dev->dev.of_node,
				  "enable_ds",
				  &pval)) {
		ptr_ds_dtb_info->ds_enabled = pval;
	}
	dev_dbg(&dev->dev, "enable_ds: %d\n", ptr_ds_dtb_info->ds_enabled);

	return 0;
}

/*****************************************************************************
 * axxia_cfg_ds - configure OBDS variables
 *
 * @mport: the master port
 * @ptr_ds_dtb_info: pointer to where data streaming dtb info is stored
 *
 * Returns %0 on success
 ****************************************************************************/
int axxia_cfg_ds(
	struct rio_mport	*mport,
	struct rio_ds_dtb_info  *ptr_ds_dtb_info)
{
	struct rio_priv         *priv = mport->priv;
	struct rio_ds_priv      *ptr_ds_priv = &(priv->ds_priv_data);
	u8			dse_id;
	u32			reg_val;
	u8			ds_capable;

	/*
	** Check if the ASIC supports data streaming feature.
	** Check RAB_VER register
	*/
	__rio_local_read_config_32(mport, RAB_VER, &reg_val);

	if ((reg_val == AXXIA_SRIO_RAB_VER_VAL_55xx)	||
		(reg_val == AXXIA_SRIO_RAB_VER_VAL_55xx_X7v1P1)) {
		ds_capable = 1;
	} else {
		ds_capable = 0;
		return -EINVAL;
	}

	if ((ds_capable == 1) &&
		(ptr_ds_dtb_info->ds_enabled == 1)) {
		ptr_ds_priv->is_use_ds_feature = 1;
	} else {
		ptr_ds_priv->is_use_ds_feature = 0;
	}

	ptr_ds_priv->num_obds_dses = RIO_MAX_NUM_OBDS_DSE;
	ptr_ds_priv->num_ibds_virtual_m = RIO_MAX_NUM_IBDS_VSID_M;
	ptr_ds_priv->num_ibds_dses = RIO_MAX_NUM_IBDS_DSE;

	/* Enable all VIRTM */
	for (dse_id = 0; dse_id < ptr_ds_priv->num_ibds_dses; dse_id++) {
		__rio_local_write_config_32(mport,
					RAB_IBDSE_CTRL(dse_id),
					1);
	}

	/* Enable general interrupt */
	__rio_local_read_config_32(mport, RAB_INTR_ENAB_GNRL, &reg_val);

	reg_val |= IB_DS_INT_EN;
	reg_val |= OB_DS_INT_EN;

	__rio_local_write_config_32(mport, RAB_INTR_ENAB_GNRL, reg_val);

	return 0;
}

/*****************************************************************************
 * axxia_rio_ds_port_irq_init -
 *
 * @mport: the master port
 *
 * Returns %0 on success
 ****************************************************************************/
void axxia_rio_ds_port_irq_init(
	struct rio_mport	*mport)
{
	struct rio_priv *priv = mport->priv;
	struct rio_ds_priv      *ptr_ds_priv;
	int i;

	ptr_ds_priv = &(priv->ds_priv_data);

	for (i = 0; i < RIO_MAX_NUM_OBDS_DSE; i++) {
		clear_bit(RIO_IRQ_ENABLED, &(ptr_ds_priv->ob_dse_irq[i].state));
		ptr_ds_priv->ob_dse_irq[i].mport = mport;
		ptr_ds_priv->ob_dse_irq[i].irq_enab_reg_addr =
				RAB_INTR_ENAB_ODSE;
		ptr_ds_priv->ob_dse_irq[i].irq_state_reg_addr =
				RAB_INTR_STAT_ODSE;
		ptr_ds_priv->ob_dse_irq[i].irq_state_mask = (1 << i);
		ptr_ds_priv->ob_dse_irq[i].irq_state = 0;
		ptr_ds_priv->ob_dse_irq[i].thrd_irq_fn = ob_dse_irq_handler;
		ptr_ds_priv->ob_dse_irq[i].data = NULL;
		ptr_ds_priv->ob_dse_irq[i].release_fn = release_ob_ds;
	}

	/*
	** Inbound Data Streaming
	*/
	ptr_ds_priv = &(priv->ds_priv_data);

	for (i = 0; i < RIO_MAX_NUM_IBDS_VSID_M; i++) {
		clear_bit(RIO_IRQ_ENABLED,
				&(ptr_ds_priv->ib_dse_vsid_irq[i].state));
		ptr_ds_priv->ib_dse_vsid_irq[i].mport = mport;
		ptr_ds_priv->ib_dse_vsid_irq[i].irq_enab_reg_addr =
				RAB_INTR_ENAB_IBDS;
		ptr_ds_priv->ib_dse_vsid_irq[i].irq_state_reg_addr =
				RAB_INTR_STAT_IBSE_VSID_M;
		ptr_ds_priv->ib_dse_vsid_irq[i].irq_state_mask = (1 << i);
		ptr_ds_priv->ib_dse_vsid_irq[i].irq_state = 0;
		ptr_ds_priv->ib_dse_vsid_irq[i].thrd_irq_fn =
				ib_dse_vsid_m_irq_handler;
		ptr_ds_priv->ib_dse_vsid_irq[i].data = NULL;
		ptr_ds_priv->ob_dse_irq[i].release_fn = release_ib_ds;
	}
}
