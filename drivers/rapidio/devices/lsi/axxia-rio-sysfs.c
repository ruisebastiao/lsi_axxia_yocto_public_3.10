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
#include <linux/device.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/slab.h>

#include "axxia-rio.h"
#include "axxia-rio-irq.h"

#ifdef CONFIG_AXXIA_RIO_STAT

static const char *event_str[] = {
	"TX Port Packet dropped                 ",
	"TX Port Error threshold exceeded       ",
	"Tx Port Degraded threshold exceeded    ",
	"Tx Port Retry conditions               ",
	"Tx Port Transmission errors            ",
	"Rx Port Transmission errors            ",
	"Tx and Rx Ports stopped                ",
	"Messaging unit Retry threshold exceeded",
	"RIO_EVENT_NUM"
};

static const char *state_str[] = {
	"TX Port stopped due to retry condition",
	"TX Port stopped due to transmission err",
	"RX Port stopped due to retry condition",
	"RX Port stopped due to transmission err",
	"TX and RX Ports Initialized OK",
	"TX and RX Ports NOT Initialized",
	"RIO_STATE_NUM"
};

static const char *irq_str[] = {
	/* Axxia Error Events - really bad! */
	"Axxia Master Write timouts                            ",
	"Axxia Master Read timouts                             ",
	"Axxia Slave write decode error response               ",
	"Axxia Slave write error response                      ",
	"Axxia Slave read decode error response                ",
	"Axxia Slave read error response                       ",
	"Axxia Slave unsupported cmds                          ",
	"Logical/Transport layer errors                       ",
	"General RapidIO Controller errors                    ",
	"Unsupported RIO req received                         ",
	"Linkdown per Deadman Monitor IRQ                     ",
	/*
	 * Peripheral Bus bridge, RapidIO -> Peripheral
	 * bus events - mostly bad!
	 */
	"RapidIO PIO Transaction completed                    ",
	"RapidIO PIO Transaction failed                       ",
	"RapidIO PIO Response error                           ",
	"RapidIO PIO Invalid address map                      ",
	"RapidIO PIO engine disabled                          ",
	/*
	 * Peripheral Bus bridge, Peripheral bus ->
	 * RapidIO events - mostly bad!
	 */
	"Axi PIO Transaction completed                        ",
	"Axi PIO Request format error                         ",
	"Axi PIO Transaction timouts                          ",
	"Axi PIO Response errors                              ",
	"Axi PIO Address not mapped                           ",
	"Axi PIO Maint mapping disabled                       ",
	"Axi PIO Memory mapping disabled                      ",
	"Axi PIO Engine disabled                              ",
	/* Port Write - service irq */
	"Port-Write FIFO events                               ",
	"Port-Write spurious events                           ",
	"Port-Write messages received                         ",
	/* Doorbells - service irq */
	"Outbound Doorbell completed                          ",
	"Outbound Doorbell retry response                     ",
	"Outbound Doorbell error response                     ",
	"Outbound Doorbell response timeouts                  ",
	"Inbound Dorrbell completed                           ",
	"Inbound Dorrbell spurious                            ",
	/* Outbound Messaging unit - service irq */
	"Outbound Message Engine response timeouts            ",
	"Outbound Message Engine response error               ",
	"Outbound Message Engine transaction error            ",
	"Outbound Message Engine descriptor update error      ",
	"Outbound Message Engine descriptor error             ",
	"Outbound Message Engine descriptor fetch error       ",
	"Outbound Message Engine sleeping mode                ",
	"Outbound Message Engine transaction completed        ",
	"Outbound Message Engine desc chain transfer completed",
	"Outbound Message Engine transaction pending          ",
	"Outbound Message descriptor error response           ",
	"Outbound Message descriptor Axi error                ",
	"Outbound Message descriptor timeout error            ",
	"Outbound Message descriptor transaction completed    ",
	/* Inbound Messaging unit - service irq */
	"Unexpected Inbound message received                  ",
	"Inbound Message Engine recv multi-seg timeouts       ",
	"Inbound Message Engine message checking error        ",
	"Inbound Message Engine data transaction error        ",
	"Inbound Message Engine descriptor update error       ",
	"Inbound Message Engine descriptor error              ",
	"Inbound Message Engine descriptor fetch error        ",
	"Inbound Message Engine sleeping mode                 ",
	"Inbound Message Engine transaction completed         ",
	"Inbound Message Engine desc chain transfer completed ",
	"Inbound Message Engine spurious                      ",
	"Inbound Message descriptor error response            ",
	"Inbound Message descriptor Axi error                 ",
	"Inbound Message descriptor timeout error             ",
	"Inbound Message descriptor transaction completed     ",
	"RIO_IRQ_NUM"
};

static const char *ib_dme_str[] = {
	"Inbound Message descriptors push to net stack        ",
	"Inbound Message descriptors ok pop by net stack      ",
	"Inbound Message descriptors err pop by net stack     ",
	"Inbound Message descriptors virt buf ring empty      ",
	"Inbound Message descriptors receive ring full event  ",
	"Inbound Message descriptors pending at wakeup        ",
	"Inbound Message descriptors engine sleep             ",
	"Inbound Message descriptors engine wakeup            ",
	"RIO_IB_DME_NUM"
};

static const char *ob_dme_str[] = {
	"Outbound Message descriptors push by net stack       ",
	"Outbound Message descriptors net push when ring full ",
	"Outbound Message descriptors ack to net stack        ",
	"Outbound Message descriptors transaction completed   ",
	"Outbound Message descriptors pending after dme event ",
	"Outbound Message descriptors engine sleep            ",
	"Outbound Message descriptors engine wakeup           ",
	"RIO_OB_DME_NUM"
};

static ssize_t axxia_rio_stat_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	struct rio_priv *priv = mport->priv;
	char *str = buf;
	int i;

	axxia_rio_port_get_state(mport, 0);
	str += sprintf(str, "Master Port state:\n");
	for (i = 0; i < RIO_STATE_NUM; i++) {
		if (atomic_read(&priv->state[i]))
			str += sprintf(str, "%s\n", state_str[i]);
	}
	str += sprintf(str, "Master Port event counters:\n");
	for (i = 0; i < RIO_EVENT_NUM; i++) {
		int c = atomic_read(&priv->event[i]);
		if (c)
			str += sprintf(str, "%s:\t%d\n", event_str[i], c);
	}
	str += sprintf(str, "Master Port interrupt counters:\n");
	for (i = 0; i < RIO_IRQ_NUM; i++) {
		int c = atomic_read(&priv->irq[i]);
		if (c)
			str += sprintf(str, "%s:\t%d\n", irq_str[i], c);
	}
	return str - buf;
}
static DEVICE_ATTR(stat, S_IRUGO, axxia_rio_stat_show, NULL);

static ssize_t axxia_rio_ib_dme_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	struct rio_priv *priv = mport->priv;
	char *str = buf;
	int i, e;

	str += sprintf(str, "Inbound Message Engine event counters:\n");
	for (e = 0; e < DME_MAX_IB_ENGINES; e++) {
		str += sprintf(str, "Mailbox %d Letter %d:\n", e/4, e % 4);
		for (i = 0; i < RIO_IB_DME_NUM; i++) {
			int c = atomic_read(&priv->ib_dme[e][i]);
			if (c)
				str += sprintf(str, "%s:\t%d\n",
					       ib_dme_str[i], c);
		}
	}
	return str - buf;
}

static ssize_t axxia_rio_ib_dme_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t count)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	struct rio_priv *priv = mport->priv;
	int i, e;

	for (e = 0; e < DME_MAX_IB_ENGINES; e++) {
		for (i = 0; i < RIO_IB_DME_NUM; i++)
			atomic_set(&priv->ib_dme[e][i], 0);
	}
	return count;
}
static DEVICE_ATTR(ib_dme_event, S_IRUGO|S_IWUGO,
		   axxia_rio_ib_dme_show, axxia_rio_ib_dme_store);

static ssize_t axxia_rio_ob_dme_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	struct rio_priv *priv = mport->priv;
	char *str = buf;
	int i, e;

	str += sprintf(str, "Outbound Message Engine event counters:\n");
	for (e = 0; e < DME_MAX_OB_ENGINES; e++) {
		str += sprintf(str, "Mailbox %d:\n", e);
		for (i = 0; i < RIO_OB_DME_NUM; i++) {
			int c = atomic_read(&priv->ob_dme[e][i]);
			if (c)
				str += sprintf(str, "%s:\t%d\n",
					       ob_dme_str[i], c);
		}
	}
	return str - buf;
}

static ssize_t axxia_rio_ob_dme_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t count)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	struct rio_priv *priv = mport->priv;
	int i, e;

	for (e = 0; e < DME_MAX_OB_ENGINES; e++) {
		for (i = 0; i < RIO_OB_DME_NUM; i++)
			atomic_set(&priv->ob_dme[e][i], 0);
	}
	return count;
}
static DEVICE_ATTR(ob_dme_event, S_IWUGO|S_IRUGO,
		   axxia_rio_ob_dme_show, axxia_rio_ob_dme_store);

#ifdef CONFIG_SRIO_IRQ_TIME

static ssize_t axxia_rio_ib_dme_time_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	struct rio_priv *priv = mport->priv;
	u32 mbox, letter;
	char *str = buf;

	for (mbox = 0; mbox < RIO_MAX_RX_MBOX; mbox++) {
		struct rio_irq_handler *h = &priv->ib_dme_irq[mbox];

		if (atomic_read(&h->start_time)) {
			struct rio_rx_mbox *mb =
					(struct rio_rx_mbox *)(h->data);

			atomic_set(&h->start_time, 0);
			if (!mb)
				return -EINVAL;
			for (letter = 0;
			     letter < RIO_MSG_MAX_LETTER;
			     letter++) {
				struct rio_msg_dme *me = mb->me[letter];
				if (!me)
					return -EINVAL;

				if (me->pkt)
					str += sprintf(str,
						       "mailbox %d letter %d\n"
						       "start_irq_tb   %llu\n"
						       "stop_irq_tb    %llu\n"
						       "start_thrd_tb  %llu\n"
						       "stop_thrd_tb   %llu\n"
						       "min_lat        %llu\n"
						       "max_lat        %llu\n"
						       "pkt            %u\n"
						       "bytes          %u\n",
						       mbox, letter,
						       me->start_irq_tb,
						       me->stop_irq_tb,
						       me->start_thrd_tb,
						       me->stop_thrd_tb,
						       me->min_lat,
						       me->max_lat,
						       me->pkt,
						       me->bytes);
			}
		}
	}
	return str - buf;
}

static ssize_t axxia_rio_ib_dme_time_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t count)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	struct rio_priv *priv = mport->priv;
	struct rio_rx_mbox *mb;
	struct rio_msg_dme *me;
	struct rio_irq_handler *h;
	u32 mbox, letter;
	int rc = sscanf(buf, "%u %u", &mbox, &letter);

	if (rc != 2)
		return -EINVAL;
	if (mbox > RIO_MAX_RX_MBOX || letter > (RIO_MSG_MAX_LETTER-1))
		return -EINVAL;

	h = &priv->ib_dme_irq[mbox];
	if (!test_bit(RIO_IRQ_ENABLED, &h->state))
		return -EINVAL;

	mb = (struct rio_rx_mbox *)h->data;
	if (!mb)
		return -EINVAL;

	me = mb->me[letter];
	if (!me)
		return -EINVAL;

	me->start_irq_tb = (u64)(-1);
	me->stop_irq_tb = (u64)(-1);
	me->start_thrd_tb = (u64)(-1);
	me->stop_thrd_tb = (u64)(-1);
	me->min_lat = (u64)(-1);
	me->max_lat = 0;
	me->pkt = 0;
	me->bytes = 0;

	AXXIA_RIO_SYSMEM_BARRIER();

	atomic_set(&h->start_time, 1);

	return count;
}

static DEVICE_ATTR(ib_dme_time, S_IWUGO|S_IRUGO,
		   axxia_rio_ib_dme_time_show, axxia_rio_ib_dme_time_store);
#endif

static ssize_t axxia_rio_irq_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	u32 stat;
	char *str = buf;

	str += sprintf(str, "Interrupt enable bits:\n");
	__rio_local_read_config_32(mport, RAB_INTR_ENAB_GNRL, &stat);
	str += sprintf(str, "General Interrupt Enable (%p)\t%8.8x\n",
		       (void *)RAB_INTR_ENAB_GNRL, stat);
	__rio_local_read_config_32(mport, RAB_INTR_ENAB_ODME, &stat);
	str += sprintf(str, "Outbound Message Engine  (%p)\t%8.8x\n",
		       (void *)RAB_INTR_ENAB_ODME, stat);
	__rio_local_read_config_32(mport, RAB_INTR_ENAB_IDME, &stat);
	str += sprintf(str, "Inbound Message Engine   (%p)\t%8.8x\n",
		       (void *)RAB_INTR_ENAB_IDME, stat);
	__rio_local_read_config_32(mport, RAB_INTR_ENAB_MISC, &stat);
	str += sprintf(str, "Miscellaneous Events     (%p)\t%8.8x\n",
		       (void *)RAB_INTR_ENAB_MISC, stat);
	__rio_local_read_config_32(mport, RAB_INTR_ENAB_APIO, &stat);
	str += sprintf(str, "Axxia Bus to RIO Events  (%p)\t%8.8x\n",
		       (void *)RAB_INTR_ENAB_APIO, stat);
	__rio_local_read_config_32(mport, RAB_INTR_ENAB_RPIO, &stat);
	str += sprintf(str, "RIO to Axxia Bus Events  (%p)\t%8.8x\n",
		       (void *)RAB_INTR_ENAB_RPIO, stat);

	return str - buf;
}
static DEVICE_ATTR(irq, S_IRUGO, axxia_rio_irq_show, NULL);

static ssize_t axxia_rio_tmo_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	struct rio_priv *priv = mport->priv;
	u32 stat;
	char *str = buf;

	str += sprintf(str, "Port Link Timeout Control Registers:\n");
	__rio_local_read_config_32(mport, RIO_PLTOCCSR, &stat);
	str += sprintf(str, "PLTOCCSR (%p)\t%8.8x\n",
		       (void *)RIO_PLTOCCSR, stat);
	__rio_local_read_config_32(mport, RIO_PRTOCCSR, &stat);
	str += sprintf(str, "PRTOCCSR (%p)\t%8.8x\n",
		       (void *)RIO_PRTOCCSR, stat);
	__rio_local_read_config_32(mport, RAB_STAT, &stat);
	str += sprintf(str, "RAB_STAT (%p)\t%8.8x\n",
		       (void *)RAB_STAT, stat);
	__rio_local_read_config_32(mport, RAB_APIO_STAT, &stat);
	str += sprintf(str, "RAB_APIO_STAT (%p)\t%8.8x\n",
		       (void *)RAB_APIO_STAT, stat);
	__rio_local_read_config_32(mport, RIO_ESCSR(priv->portNdx), &stat);
	str += sprintf(str, "PNESCSR (%p)\t%8.8x\n",
		       (void *)RIO_ESCSR(priv->portNdx), stat);

	return str - buf;
}
static DEVICE_ATTR(tmo, S_IRUGO, axxia_rio_tmo_show, NULL);

static ssize_t axxia_ib_dme_log_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	u32 stat, log;
	char *str = buf;

	__rio_local_read_config_32(mport, RAB_INTR_STAT_MISC, &stat);
	log = (stat & UNEXP_MSG_LOG) >> 24;
	str += sprintf(str, "mbox[1:0]   %x\n", (log & 0xc0) >> 6);
	str += sprintf(str, "letter[1:0] %x\n", (log & 0x30) >> 4);
	str += sprintf(str, "xmbox[3:0] %x\n", log & 0x0f);

	return str - buf;
}
static DEVICE_ATTR(dme_log, S_IRUGO, axxia_ib_dme_log_show, NULL);

static ssize_t apio_enable(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf,
			   size_t count)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	u32 mask;
	int rc = sscanf(buf, "%x", &mask);

	if (rc == 1) {
		axxia_rio_apio_disable(mport);
		rc = axxia_rio_apio_enable(mport, 0, mask);
		if (!rc)
			return count;
	}
	return rc;
}
static DEVICE_ATTR(apio_enable, S_IWUGO, NULL, apio_enable);

static ssize_t apio_disable(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,
			    size_t count)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	struct rio_priv *priv = mport->priv;
	u32 mask;
	int rc = sscanf(buf, "%x", &mask);

	if (rc == 1) {
		axxia_rio_apio_disable(mport);
		if (priv->apio_irq.irq_state_mask != mask) {
			rc = axxia_rio_apio_enable(mport, mask, 0);
			if (!rc)
				return count;
		}
	}
	return rc;
}
static DEVICE_ATTR(apio_disable, S_IWUGO, NULL, apio_disable);

static ssize_t rpio_enable(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf,
			   size_t count)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	u32 mask;
	int rc = sscanf(buf, "%x", &mask);

	if (rc == 1) {
		axxia_rio_rpio_disable(mport);
		rc = axxia_rio_rpio_enable(mport, 0, mask);
		if (!rc)
			return count;
	}
	return rc;
}
static DEVICE_ATTR(rpio_enable, S_IWUGO, NULL, rpio_enable);

static ssize_t rpio_disable(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,
			    size_t count)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	struct rio_priv *priv = mport->priv;
	u32 mask;
	int rc = sscanf(buf, "%x", &mask);

	if (rc == 1) {
		axxia_rio_rpio_disable(mport);
		if (priv->rpio_irq.irq_state_mask != mask) {
			rc = axxia_rio_rpio_enable(mport, mask, 0);
			if (!rc)
				return count;
		}
	}
	return rc;
}
static DEVICE_ATTR(rpio_disable, S_IWUGO, NULL, rpio_disable);

#define MBOX_MAGIC 0xabcdefaa

struct ob_dme_stat {
	u32 magic;
	int tx_cb;
	u32 desc_error;
	u32 desc_done;
	int seq_no;
	int tx_seq_no;
	int ack_out_of_order;
	u8 *bufs;
};

static struct ob_dme_stat ob_stat[RIO_MAX_TX_MBOX];
static struct ob_dme_stat ib_stat[RIO_MAX_TX_MBOX];

static ssize_t ob_dme_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	int i;
	char *str = buf;

	for (i = 0; i < RIO_MAX_TX_MBOX; i++) {
		struct ob_dme_stat *stat = &ob_stat[i];

		if (stat->magic != MBOX_MAGIC)
			continue;
		str += sprintf(str, "mbox id      %d\n", i);
		str += sprintf(str, "tx_cb        %d\n", stat->tx_cb);
		str += sprintf(str, "desc_error   %d\n", stat->desc_error);
		str += sprintf(str, "desc_done    %d\n", stat->desc_done);
		str += sprintf(str, "seq_no       %d\n", stat->seq_no);
		str += sprintf(str, "tx_seq_no    %d\n", stat->tx_seq_no);
		str += sprintf(str, "out_of_order %d\n",
			       stat->ack_out_of_order);
	}
	return str - buf;
}
static DEVICE_ATTR(ob_mbox_stat, S_IRUGO, ob_dme_show, NULL);

static ssize_t ib_dme_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	struct rio_priv *priv = mport->priv;
	int i;
	int ready = 0, valid = 0, not_valid = 0;
	char *str = buf;

	for (i = 0; i < RIO_MAX_RX_MBOX; i++) {
		struct ob_dme_stat *stat = &ib_stat[i];

		if (stat->magic != MBOX_MAGIC)
			continue;
		str += sprintf(str, "mbox id      %d\n", i);
		str += sprintf(str, "tx_cb        %d\n", stat->tx_cb);
		str += sprintf(str, "desc_error   %d\n", stat->desc_error);
		str += sprintf(str, "desc_done    %d\n", stat->desc_done);
	}
	for (i = 0; i < DME_MAX_IB_ENGINES; i++) {
		int dme_no = i;
		u32 data;

		__rio_local_read_config_32(mport,
					   RAB_IB_DME_STAT(dme_no),
					   &data);
		if (data) {
			str += sprintf(str, "dme %d state %8.8x %s %s\n",
				       dme_no, data,
					(data & IB_DME_STAT_ERROR_MASK ?
					"DME_ERROR" : "OK"),
				       (data & IB_DME_STAT_SLEEPING ?
					"DME_SLEEP" : "OK"));
		}
	}
	if (!priv->internalDesc) {
		int j, k;
		int ne = 0;
		struct rio_irq_handler *ob = NULL;
		struct rio_msg_dme *mb = NULL;
		for (j = 0; j < DME_MAX_IB_ENGINES; j++) {
			ob = &priv->ob_dme_irq[j];
			if (ob == NULL)
				continue;
			mb = ob->data;
			if (mb == NULL)
				continue;
			ne += mb->entries;
			for (k = 0; k < mb->entries; k++) {
				int desc_no = i;
				u32 data;
				data = *((u32 *)DESC_TABLE_W0_MEM(mb, desc_no));
				if (data & DME_DESC_DW0_READY_MASK)
					ready++;
				if (data  & DME_DESC_DW0_VALID)
					valid++;
				else
					not_valid++;
			}
		}
		str += sprintf(str, "External Message Descriptor Memory (%d)\n",
				ne);
	} else {
		for (i = 0; i < priv->desc_max_entries; i++) {
			int desc_no = i;
			u32 data;
			__rio_local_read_config_32(mport,
					   	DESC_TABLE_W0(desc_no),
					   	&data);
			if (data & DME_DESC_DW0_READY_MASK)
				ready++;
			if (data  & DME_DESC_DW0_VALID)
				valid++;
			else
				not_valid++;
		}
		str += sprintf(str, "Internal Message Descriptor Memory (%d)\n",
				priv->desc_max_entries);
	}
	str += sprintf(str, "desc ready %d desc valid %d desc not valid %d\n",
			ready, valid, not_valid);
	return str - buf;
}
static DEVICE_ATTR(ib_mbox_stat, S_IRUGO, ib_dme_show, NULL);

static void tx_msg_callback(struct rio_mport *mport, void *dev_id,
			    int mbox, int rc, void *cookie)
{
	struct ob_dme_stat *stat = dev_id;
	int seq_no = (int)cookie;

	if (stat != &ob_stat[mbox])
		pr_err("--- %s --- stat/mbox mismatch, mbox %d\n",
		       __func__, mbox);
	else {
		stat->tx_cb++;
		if (rc)
			stat->desc_error = rc;
		else
			stat->desc_done++;
		if ((stat->seq_no) != seq_no)
			stat->ack_out_of_order++;
		stat->seq_no++;
	}
}

static void rx_msg_callback(struct rio_mport *mport, void *dev_id,
			    int mbox, int letter)
{
	struct ob_dme_stat *stat = dev_id;

	if (stat != &ib_stat[mbox])
		pr_err("--- %s --- stat/mbox mismatch, mbox %d\n",
			__func__, mbox);
	else {
		u8 *buf;
		int sz = 0;
		int seq = 0;
		u16 destid = 0;
		int i = 0;
		stat->tx_cb++;
retry:
		buf = mport->ops->get_inb_message(mport,
			 mbox, letter, &sz, &seq, &destid);
		if (IS_ERR(buf) || !buf) {
			if (!buf)
				return;

			stat->desc_error = PTR_ERR(buf);

			if (PTR_ERR(buf) == -ENOMEM) {
				buf = &stat->bufs[0];
				goto add_buff;
			}
		}
		stat->desc_done++;
		for (i = 0; i < sz; i++) {
			if (buf[i] != (i & 0xff)) {
				pr_err("--- %s --- unexpected data %hhx "
					"returned in byte %d\n",
					__func__, buf[i], i);
				break;
			}
		}
		pr_info("--- %s --- inbound message size %d %d %hx\n",
			 __func__, sz, seq, destid);
add_buff:
		if (mport->ops->add_inb_buffer(mport, mbox, buf)) {
			pr_err("--- %s --- add inound buffer\n", __func__);
			return;
		}
		goto retry;
	}
}

static ssize_t open_ob_dme(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf,
			   size_t count)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	int mbox, entries, prio;
	int rc = sscanf(buf, "%d %d %d", &mbox, &entries, &prio);

	if (rc == 3) {
		rc = rio_request_outb_mbox(mport,
					   &ob_stat[mbox],
					   mbox,
					   entries,
					   prio, /* prio */
					   tx_msg_callback);
		if (!rc) {
			memset(&ob_stat[mbox], 0, sizeof(struct ob_dme_stat));
			ob_stat[mbox].magic = MBOX_MAGIC;
			return count;
		}
		return rc;
	} else
			return -EINVAL;
}
static DEVICE_ATTR(open_ob_mbox, S_IWUGO, NULL, open_ob_dme);

static ssize_t open_ib_dme(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf,
			   size_t count)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	int mbox, entries;
	int rc = sscanf(buf, "%d %d", &mbox, &entries);

	if (rc == 2) {
		int bsz = RIO_MBOX_TO_BUF_SIZE(mbox);
		u8 *bufs = kzalloc(4*entries*bsz, GFP_KERNEL);
		int i;
		if (!bufs)
			return -ENOMEM;
		rc = rio_request_inb_mbox(mport,
					  &ib_stat[mbox],
					  mbox,
					  entries,
					  rx_msg_callback);
		if (!rc) {
			memset(&ib_stat[mbox], 0, sizeof(struct ob_dme_stat));
			ib_stat[mbox].magic = MBOX_MAGIC;
			ib_stat[mbox].bufs = bufs;
			for (i = 0; i < (4*entries); i++) {
				rc = mport->ops->add_inb_buffer(mport,
								mbox,
								&bufs[i*bsz]);
				if (rc) {
					pr_err("--- %s --- add inbound buffer"
						" rc %d\n", __func__, rc);
					rio_release_inb_mbox(mport, mbox);
					goto err;
				}
			}
			return count;
		}
err:
		kfree(bufs);
		return rc;
	} else
		return -EINVAL;
}
static DEVICE_ATTR(open_ib_mbox, S_IWUGO, NULL, open_ib_dme);

struct axxia_mbox_work {
	struct rio_mport *mport;
	int out;
	int mbox;
	struct work_struct work;
};

static void mbox_work(struct work_struct *work)
{
	struct axxia_mbox_work *mb_work = container_of(work,
						     struct axxia_mbox_work,
						     work);
	struct rio_mport *mport = mb_work->mport;
	int rc = 0;

	if (mb_work->out)
		rc = rio_release_outb_mbox(mport, mb_work->mbox);
	else {
		rc = rio_release_inb_mbox(mport, mb_work->mbox);
		kfree(ib_stat[mb_work->mbox].bufs);
		ib_stat[mb_work->mbox].bufs = NULL;
	}
	if (rc)
		pr_err("--- %s --- release mbox %d error %d\n",
			__func__, mb_work->mbox, rc);
}

static ssize_t close_ob_dme(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,
			    size_t count)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	int dme;
	int rc = sscanf(buf, "%d", &dme);

	if (rc == 1) {
		struct axxia_mbox_work *mb_work = kzalloc(sizeof(*mb_work),
							GFP_KERNEL);

		if (!mb_work)
			return -ENOMEM;
		mb_work->mport = mport;
		mb_work->out = 1;
		mb_work->mbox = dme;
		INIT_WORK(&mb_work->work, mbox_work);
		schedule_work(&mb_work->work);
		return count;
	} else
		return -EINVAL;
}
static DEVICE_ATTR(close_ob_mbox, S_IWUGO, NULL, close_ob_dme);

static ssize_t close_ib_dme(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,
			    size_t count)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	int mbox;
	int rc = sscanf(buf, "%d", &mbox);

	if (rc == 1) {
		struct axxia_mbox_work *mb_work = kzalloc(sizeof(*mb_work),
							GFP_KERNEL);

		if (!mb_work)
			return -ENOMEM;
		mb_work->mport = mport;
		mb_work->out = 0;
		mb_work->mbox = mbox;
		INIT_WORK(&mb_work->work, mbox_work);
		schedule_work(&mb_work->work);
		return count;
	} else
		return -EINVAL;
}
static DEVICE_ATTR(close_ib_mbox, S_IWUGO, NULL, close_ib_dme);

static ssize_t ob_dme_send(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf,
			   size_t count)
{
	struct rio_mport *mport = dev_get_drvdata(dev);
	int mbox_dest, letter;
	u16 dest_id;
	u32 flags;
	int size;
	int rc = sscanf(buf, "%d %hu %d %x %d",
			&mbox_dest, &dest_id, &letter, &flags, &size);

	if (rc == 5) {
		struct rio_dev *rdev = NULL;
		void *cookie = NULL;
		int i;
		u8 *buff = kzalloc(RIO_MAX_MSG_SIZE, GFP_KERNEL);

		while ((rdev = rio_get_device(0xffff, 0xffff, rdev)) != NULL) {
			if (rdev->destid == dest_id)
				break;
		}
		if (!rdev) {
			pr_info("destid %hu not found\n", dest_id);
			return -ENODEV;
		}
		for (i = 0; i < (RIO_MAX_MSG_SIZE); i++)
			buff[i] = (i & 0xff);
		cookie = (void *)ob_stat[mbox_dest].tx_seq_no;
		rc = mport->ops->add_outb_message(mport, rdev, mbox_dest,
						  letter, flags, buff, size,
						  cookie);
		ob_stat[mbox_dest].tx_seq_no++;
		kfree(buff);
		if (!rc)
			return count;
		return rc;
	} else
		return -EINVAL;
}
static DEVICE_ATTR(ob_send, S_IWUGO, NULL, ob_dme_send);


static struct attribute *rio_attributes[] = {
	&dev_attr_stat.attr,
	&dev_attr_irq.attr,
	&dev_attr_ob_dme_event.attr,
	&dev_attr_ib_dme_event.attr,
#ifdef CONFIG_SRIO_IRQ_TIME
	&dev_attr_ib_dme_time.attr,
#endif
	&dev_attr_tmo.attr,
	&dev_attr_dme_log.attr,
	&dev_attr_apio_enable.attr,
	&dev_attr_apio_disable.attr,
	&dev_attr_rpio_enable.attr,
	&dev_attr_rpio_disable.attr,
	&dev_attr_ob_mbox_stat.attr,
	&dev_attr_ib_mbox_stat.attr,
	&dev_attr_open_ob_mbox.attr,
	&dev_attr_open_ib_mbox.attr,
	&dev_attr_close_ob_mbox.attr,
	&dev_attr_close_ib_mbox.attr,
	&dev_attr_ob_send.attr,
	NULL
};

static struct attribute_group rio_attribute_group = {
	.name = NULL,
	.attrs = rio_attributes,
};

int axxia_rio_init_sysfs(struct platform_device *dev)
{
	return sysfs_create_group(&dev->dev.kobj, &rio_attribute_group);
}
void axxia_rio_release_sysfs(struct platform_device *dev)
{
	sysfs_remove_group(&dev->dev.kobj, &rio_attribute_group);
}

#endif /* #ifdef CONFIG_AXXIA_RIO_STAT */
