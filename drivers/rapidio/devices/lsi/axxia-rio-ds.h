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

#include <linux/rio-axxia.h>

/* #define DS_DEBUG 1  */
#define USE_IOCTRL 1

/******************************************************************************
    #defines
******************************************************************************/

#define     AXXIA_SRIO_RAB_VER_VAL_55xx           (0x00211021)
#define     AXXIA_SRIO_RAB_VER_VAL_55xx_X7v1P1    (0x00221013)

/******************************************************************************
    #Type definitions
******************************************************************************/

struct axxia_rio_ds_dtb_info {
	int     ds_enabled;
#if 0
	int     num_inb_virtual_m;      /* number of inbound virtual M */
	int     num_outb_dses;          /* number of outbound DSEs */
	int     inb_num_data_descs;     /* number of inbound data descriptors */
	int     outb_num_hdr_descs;     /* number of outbound header descriptors */
	int     outb_num_data_descs;    /* number of outbound data descriptors */
#endif
};


/* Outbound data stream stats */
struct axxia_rio_ds_obds_dse_stats {
	u32	   num_desc_chain_transferred;
	u32	   num_desc_transferred;
	u32        num_desc_err;
	u32        num_desc_fetch_err;
	u32        num_desc_data_transaction_err;
	u32        num_desc_update_err;
	u32        num_desc_axi_err;
};

/* Inbound data stream stats */
struct axxia_rio_ds_ibds_vsid_m_stats {
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
struct axxia_rio_ds_priv {
	struct rio_irq_handler  ob_dse_irq[RIO_MAX_NUM_OBDS_DSE];
	struct rio_irq_handler  ib_dse_vsid_irq[RIO_MAX_NUM_IBDS_VSID_M];

	struct axxia_rio_ds_ibds_vsid_m_stats     ib_vsid_m_stats[RIO_MAX_NUM_IBDS_VSID_M];
	struct axxia_rio_ds_obds_dse_stats        ob_dse_stats[RIO_MAX_NUM_OBDS_DSE];

	u8                      is_use_ds_feature;
};

/******************************************************************************
    Platform Driver APIs
******************************************************************************/

/* Platform driver initialization */
extern int axxia_parse_dtb_ds(
	struct platform_device *dev,
	struct axxia_rio_ds_dtb_info *ptr_ds_dtb_info);

extern int axxia_cfg_ds(
	struct rio_mport        *mport,
	struct axxia_rio_ds_dtb_info  *ptr_ds_dtb_info);

extern void axxia_rio_ds_port_irq_init(
	struct rio_mport        *mport);

/* handle outbound data streaming DSE interrupt */
void ob_dse_irq_handler(struct rio_irq_handler *h, u32 state);

/* handle inbound VSID interrupt */
void ib_dse_vsid_m_irq_handler(struct rio_irq_handler *h, u32 state);

void release_ob_ds(struct rio_irq_handler *h);

void release_ib_ds(struct rio_irq_handler *h);

#endif /* _AXXIA_RIO_DS_H_ */
