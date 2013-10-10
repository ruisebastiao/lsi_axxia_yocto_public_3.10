/*
 * drivers/net/ethernet/lsi/lsi_acp_net.c
 *
 * Copyright (C) 2013 LSI
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 * NOTES:
 *
 * 1) This driver is used by both ACP (PPC) and AXM (ARM) platforms.
 *
 * 2) This driver parses the DTB for driver specific settings. A few of
 *    them can be overriden by setting environment variables in U-boot:
 *
 *    ethaddr - MAC address of interface, in xx:xx:xx:xx:xx:xx format
 *
 *    phy-addr - Specific address of PHY (0 - 0x20). If not specified,
 *               the driver will scan the bus and will attach to the first
 *               PHY it finds.
 *
 *    ad-value - PHY advertise value. Can be set to one of these or they
 *               be OR'ed together. If not set, the driver sets the
 *               advertised value equal to what the driver supports.
 *
 *               0x101 - 100/Full
 *               0x81  - 100/Half
 *               0x41  - 10/Full
 *               0x21  - 10/Half
 *
 * 3) This driver allows the option to disable auto negotiation and manually
 *    specify the speed and duplex setting, with the use of the device tree
 *    variable "phy-link". Legal values for this variable are:
 *
 *    "auto"  - auto negotiation enabled
 *    "100MF" - auto negotation disabled, set to 100MB Full Duplex
 *    "10MH"  - auto negotation disabled, set to 100MB Half Duplex
 *    "10MF"  - auto negotation disabled, set to 10MB Full Duplex
 *    "10MH"  - auto negotation disabled, set to 10MB Half Duplex
 *
 *    NOTE: If the phy-link variable is not present in the device tree, or
 *    if an invalid value is used, the driver defaults to auto negotiation
 *    mode.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/dma-mapping.h>

#include <linux/uaccess.h>
#include <linux/io.h>
#include <asm/dma.h>

#include "lsi_acp_net.h"

extern int acp_mdio_read(unsigned long, unsigned long, unsigned short *, int);
extern int acp_mdio_write(unsigned long, unsigned long, unsigned short, int);

/* Define to disable full duplex mode on Amarillo boards */
#undef AMARILLO_WA
/*#define AMARILLO_WA*/

#define LSI_DRV_NAME           "acp-femac"
#define LSI_MDIO_NAME          "acp-femac-mdio"
#define LSI_DRV_VERSION        "2013-09-10"

MODULE_AUTHOR("John Jacques");
MODULE_DESCRIPTION("LSI ACP-FEMAC Ethernet driver");
MODULE_LICENSE("GPL");

/* Base Addresses of the RX, TX, and DMA Registers. */
static void *rx_base;
static void *tx_base;
static void *dma_base;
#ifdef CONFIG_ARM
static void *gpreg_base;
#define GPREG_BASE 0x002010094000ULL
#endif

/* BCM5221 registers */
#define PHY_BCM_TEST_REG	0x1f
#define PHY_AUXILIARY_MODE3	0x1d

/*
 * ----------------------------------------------------------------------
 * appnic_mii_read
 *
 * Returns -EBUSY if unsuccessful, the (short) value otherwise.
 */

static int appnic_mii_read(struct mii_bus *bus, int phy, int reg)
{
	unsigned short value;

	/* Always returns success, so no need to check return status. */
	acp_mdio_read(phy, reg, &value, 0);

	return (int)value;
}

/*
 * ----------------------------------------------------------------------
 * appnic_mii_write
 */

static int appnic_mii_write(struct mii_bus *bus, int phy, int reg, u16 val)
{
	return acp_mdio_write(phy, reg, val, 0);
}

/*
 * ----------------------------------------------------------------------
 * appnic_handle_link_change
 *
 * Called periodically when PHY is in polling mode.
 */

static void appnic_handle_link_change(struct net_device *dev)
{
	struct appnic_device *pdata = netdev_priv(dev);
	struct phy_device *phydev = pdata->phy_dev;
	int status_change = 0;
	unsigned long rx_configuration;
	unsigned long tx_configuration = 0;

	rx_configuration =
#ifdef CONFIG_ARM
		APPNIC_RX_CONF_STRIPCRC;
#else
		(APPNIC_RX_CONF_STRIPCRC |
		 APPNIC_RX_CONF_RXFCE |
		 APPNIC_RX_CONF_TXFCE);
#endif
	tx_configuration =
		(APPNIC_TX_CONF_ENABLE_SWAP_SA |
		 APPNIC_TX_CONF_APP_CRC_ENABLE |
		 APPNIC_TX_CONF_PAD_ENABLE);

	TX_CONF_SET_IFG(tx_configuration, 0xf);

	if (phydev->link) {
		if ((pdata->speed != phydev->speed) ||
		    (pdata->duplex != phydev->duplex)) {
#ifndef AMARILLO_WA
			if (phydev->duplex) {
				rx_configuration |= APPNIC_RX_CONF_DUPLEX;
				tx_configuration |= APPNIC_TX_CONF_DUPLEX;
			}
#endif
			if (phydev->speed == SPEED_100) {
				rx_configuration |= APPNIC_RX_CONF_SPEED;
				tx_configuration |= APPNIC_TX_CONF_SPEED;
			}

			rx_configuration |= (APPNIC_RX_CONF_ENABLE |
					     APPNIC_RX_CONF_LINK);
			tx_configuration |= (APPNIC_TX_CONF_LINK |
					     APPNIC_TX_CONF_ENABLE);

			pdata->speed = phydev->speed;
			pdata->duplex = phydev->duplex;
			status_change = 1;
		}
	}
	if (phydev->link != pdata->link) {
		if (!phydev->link) {
			pdata->speed = 0;
			pdata->duplex = -1;
		}
		pdata->link = phydev->link;
		status_change = 1;
	}

	if (status_change) {
		if (phydev->link) {
			netif_carrier_on(dev);
			netdev_info(dev, "link up (%d/%s)\n",
				    phydev->speed,
				    phydev->duplex == DUPLEX_FULL ?
				    "Full" : "Half");
		} else {
			netif_carrier_off(dev);
			netdev_info(dev, "link down\n");
		}

		if (rx_configuration != read_mac(APPNIC_RX_CONF))
			write_mac(rx_configuration, APPNIC_RX_CONF);

		if (tx_configuration != read_mac(APPNIC_TX_CONF))
			write_mac(tx_configuration, APPNIC_TX_CONF);
	}

	return;
}

/*
 * ----------------------------------------------------------------------
 * appnic_mii_probe
 */

static int appnic_mii_probe(struct net_device *dev)
{
	struct appnic_device *pdata = netdev_priv(dev);
	struct phy_device *phydev = NULL;
	int ret;

	if (pdata->phy_address && (pdata->phy_address < PHY_MAX_ADDR)) {
		phydev = pdata->mii_bus->phy_map[pdata->phy_address];
		if (phydev)
			goto skip_first;
	}

	/* Find the first phy */
	phydev = phy_find_first(pdata->mii_bus);
	if (!phydev) {
		pr_crit("!!! no PHY found !!!\n");
		netdev_err(dev, " no PHY found\n");
		return -ENODEV;
	}

skip_first:

	/*
	 * Allow the option to disable auto negotiation and manually specify
	 * the link speed and duplex setting with the use of a environment
	 * setting.
	 */

	if (0 == pdata->phy_link_auto) {
		phydev->autoneg = AUTONEG_DISABLE;
		phydev->speed =
			0 == pdata->phy_link_speed ? SPEED_10 : SPEED_100;
		phydev->duplex =
			0 == pdata->phy_link_duplex ? DUPLEX_HALF : DUPLEX_FULL;
	} else {
		phydev->autoneg = AUTONEG_ENABLE;
	}

	ret = phy_connect_direct(dev, phydev,
				 &appnic_handle_link_change, 0,
				 PHY_INTERFACE_MODE_MII);

	if (ret) {
		netdev_err(dev, "Could not attach to PHY\n");
		return ret;
	}

#ifdef AMARILLO_WA
	/*
	 * For the Amarillo, without the auto-negotiate ecn.
	 */
	{
		u16 val;
		int rc;

		/* Enable access to shadow register @ 0x1d */
		rc = acp_mdio_read(phydev->addr, PHY_BCM_TEST_REG, &val, 0);
		val |= 0x80;
		rc |= acp_mdio_write(phydev->addr, PHY_BCM_TEST_REG, val, 0);

		/* Set RX FIFO size to 0x7 */
		rc |= acp_mdio_read(phydev->addr, PHY_AUXILIARY_MODE3, &val, 0);
		val &= 0xf;
		val |= 0x7;
		rc |= acp_mdio_write(phydev->addr, PHY_AUXILIARY_MODE3, val, 0);

		/* Disable access to shadow register @ 0x1d */
		rc |= acp_mdio_read(phydev->addr, PHY_BCM_TEST_REG, &val, 0);
		val &= ~0x80;
		rc |= acp_mdio_write(phydev->addr, PHY_BCM_TEST_REG, val, 0);

		if (0 != rc)
			return -EIO;
	}
#endif
	netdev_info(dev,
		    "attached PHY driver [%s] (mii_bus:phy_addr=%s, irq=%d)\n",
		    phydev->drv->name, dev_name(&phydev->dev), phydev->irq);

	/* Mask with MAC supported features */
	phydev->supported &= PHY_BASIC_FEATURES;
	if (pdata->ad_value)
		phydev->advertising = mii_adv_to_ethtool_adv_t(pdata->ad_value);
	else
		phydev->advertising = phydev->supported;

	pdata->link = 0;
	pdata->speed = 0;
	pdata->duplex = -1;
	pdata->phy_dev = phydev;

	pr_info("%s: PHY initialized successfully", LSI_DRV_NAME);
	return 0;
}

/*
 * ----------------------------------------------------------------------
 * appnic_mii_init
 */

static int __devinit appnic_mii_init(struct platform_device *pdev,
				     struct net_device *dev)
{
	struct appnic_device *pdata = netdev_priv(dev);
	int i, err = -ENXIO;

	pdata->mii_bus = mdiobus_alloc();
	if (!pdata->mii_bus) {
		err = -ENOMEM;
		goto err_out_1;
	}

	pdata->mii_bus->name = LSI_MDIO_NAME;
	snprintf(pdata->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		 pdev->name, pdev->id);
	pdata->mii_bus->priv = pdata;
	pdata->mii_bus->read = appnic_mii_read;
	pdata->mii_bus->write = appnic_mii_write;
	pdata->mii_bus->irq = pdata->phy_irq;
	for (i = 0; i < PHY_MAX_ADDR; ++i)
		pdata->mii_bus->irq[i] = PHY_POLL;

	if (mdiobus_register(pdata->mii_bus)) {
		pr_warn("%s: Error registering mii bus", LSI_DRV_NAME);
		goto err_out_free_bus_2;
	}

	if (appnic_mii_probe(dev) < 0) {
		pr_warn("%s: Error registering mii bus", LSI_DRV_NAME);
		goto err_out_unregister_bus_3;
	}

	return 0;

err_out_unregister_bus_3:
	mdiobus_unregister(pdata->mii_bus);
err_out_free_bus_2:
	mdiobus_free(pdata->mii_bus);
err_out_1:
	return err;
}

/*
  ======================================================================
  NIC Interface
  ======================================================================
*/

#define DESCRIPTOR_GRANULARITY 64
#define BUFFER_ALIGNMENT 64

#define ALIGN64B(address) \
	((((unsigned long) (address) + (64UL - 1UL)) & ~(64UL - 1UL)))

#define ALIGN64B_OFFSET(address) \
	(ALIGN64B(address) - (unsigned long) (address))

/*
 *  ----- Note On Buffer Space -----
 *
 *  Minimum number of descriptors is 64 for the receiver and 64 for the
 *  transmitter; therefore, 2048 bytes (16 bytes each).
 *  This driver uses the following parameters, all of which may be set on
 *  the command line if this drivers is used as a module.
 *
 *  - rx_num_desc : Number of receive descriptors. This  must be a multiple
 *                  of 64.
 *  - tx_num_desc : Number of transmit descriptors. This must be a multiple
 *                  of 64.
 *
 *  The scheme used will be as follows:
 *
 *  - num_[rt]x_desc will be adjusted to be a multiple of 64 (if necessary).
 *  - An skb (with the data area 64 byte aligned) will be allocated for each rx
 *    descriptor.
 */

/*
 * Receiver
 */

int rx_num_desc = (CONFIG_LSI_NET_NUM_RX_DESC * DESCRIPTOR_GRANULARITY);
module_param(rx_num_desc, int, 0);
MODULE_PARM_DESC(rx_num_desc, "appnic : Number of receive descriptors");

int rx_buf_sz = CONFIG_LSI_NET_RX_BUF_SZ;
module_param(rx_buf_sz, int, 0);
MODULE_PARM_DESC(rx_buf_sz, "appnic : Receive buffer size");

/*
 * Transmitter
 */

int tx_num_desc = (CONFIG_LSI_NET_NUM_TX_DESC * DESCRIPTOR_GRANULARITY);
module_param(tx_num_desc, int, 0);
MODULE_PARM_DESC(tx_num_desc, "appnic : Number of receive descriptors");

int tx_buf_sz = CONFIG_LSI_NET_TX_BUF_SZ;
module_param(tx_buf_sz, int, 0);
MODULE_PARM_DESC(tx_buf_sz, "Appnic : Receive buffer size");

static unsigned long dropped_by_stack;
static unsigned long out_of_tx_descriptors;
static unsigned long transmit_interrupts;
static unsigned long receive_interrupts;

/*
  ======================================================================
  Utility Functions
  ======================================================================
*/

/*
  ----------------------------------------------------------------------
  clear_statistics
*/

static void clear_statistics(struct appnic_device *pdata)
{
	int waste;

	/*
	 * Clear memory.
	 */

	memset((void *) &(pdata->stats), 0, sizeof(struct net_device_stats));

	/*
	 * Clear counters.
	 */

	waste = read_mac(APPNIC_RX_STAT_PACKET_OK); /* rx_packets */
	waste = read_mac(APPNIC_TX_STAT_PACKET_OK); /* tx_packets */

	/* rx_bytes kept by driver. */
	/* tx_bytes kept by driver. */
	/* rx_errors will be the sum of the rx errors available. */
	/* tx_errors will be the sum of the tx errors available. */
	/* rx_dropped (unable to allocate skb) will be maintained by driver */
	/* tx_dropped (unable to allocate skb) will be maintained by driver */

	/* multicast */

	waste = read_mac(APPNIC_RX_STAT_MULTICAST);

	/* collisions will be the sum of the three following. */

	waste = read_mac(APPNIC_TX_STATUS_LATE_COLLISION);
	waste = read_mac(APPNIC_TX_STATUS_EXCESSIVE_COLLISION);
	waste = read_mac(APPNIC_TX_STAT_COLLISION_ABOVE_WATERMARK);

	/* rx_length_errors will be the sum of the two following. */

	waste = read_mac(APPNIC_RX_STAT_UNDERSIZE);
	waste = read_mac(APPNIC_RX_STAT_OVERSIZE);

	/* rx_over_errors (out of descriptors?) maintained by the driver. */
	/* rx_crc_errors */

	waste = read_mac(APPNIC_RX_STAT_CRC_ERROR);

	/* rx_frame_errors */

	waste = read_mac(APPNIC_RX_STAT_ALIGN_ERROR);

	/* rx_fifo_errors */

	waste = read_mac(APPNIC_RX_STAT_OVERFLOW);

	/* rx_missed will not be maintained. */
	/* tx_aborted_errors will be maintained by the driver. */
	/* tx_carrier_errors will not be maintained. */
	/* tx_fifo_errors */

	waste = read_mac(APPNIC_TX_STAT_UNDERRUN);

	/* tx_heartbeat_errors */
	/* tx_window_errors */

	/* rx_compressed will not be maintained. */
	/* tx_compressed will not be maintained. */

	/*
	 * That's all.
	 */

	return;
}

/*
 * ----------------------------------------------------------------------
 * get_hw_statistics
 *
 *  -- NOTES --
 *
 *  1) The hardware clears the statistics registers after a read.
 */

static void get_hw_statistics(struct appnic_device *pdata)
{
	unsigned long flags;

	/* tx_packets */

	pdata->stats.tx_packets += read_mac(APPNIC_TX_STAT_PACKET_OK);

	/* multicast */

	pdata->stats.multicast += read_mac(APPNIC_RX_STAT_MULTICAST);

	/* collision */

	pdata->stats.collisions += read_mac(APPNIC_TX_STATUS_LATE_COLLISION);
	pdata->stats.collisions +=
		read_mac(APPNIC_TX_STATUS_EXCESSIVE_COLLISION);
	pdata->stats.collisions +=
	read_mac(APPNIC_TX_STAT_COLLISION_ABOVE_WATERMARK);

	/* rx_length_errors */

	pdata->stats.rx_length_errors += read_mac(APPNIC_RX_STAT_UNDERSIZE);
	pdata->stats.rx_length_errors += read_mac(APPNIC_RX_STAT_OVERSIZE);

	/* tx_fifo_errors */

	pdata->stats.tx_fifo_errors += read_mac(APPNIC_TX_STAT_UNDERRUN);

	/*
	 * Lock this section out so the statistics maintained by the driver
	 * don't get clobbered.
	 */

	spin_lock_irqsave(&pdata->dev_lock, flags);

	pdata->stats.rx_errors +=
		(pdata->stats.rx_length_errors +
		 pdata->stats.rx_crc_errors +
		 pdata->stats.rx_frame_errors +
		 pdata->stats.rx_fifo_errors +
		 pdata->stats.rx_dropped +
		 pdata->stats.rx_over_errors);

	pdata->stats.rx_dropped = 0;
	pdata->stats.rx_over_errors = 0;

	pdata->stats.tx_errors += (pdata->stats.tx_fifo_errors +
				   pdata->stats.tx_aborted_errors);
	pdata->stats.tx_aborted_errors = 0;

	spin_unlock_irqrestore(&pdata->dev_lock, flags);

	/*
	 * That's all.
	 */

	return;
}

/*
 * ----------------------------------------------------------------------
 * queue_initialized
 *
 * Returns the number of descriptors that are ready to receive packets
 * or are waiting to transmit packets.  (from tail to head).
 */

static int queue_initialized(union appnic_queue_pointer head,
			     union appnic_queue_pointer tail,
			     int size)
{
	int initialized;

	/* Calculate the number of descriptors currently initialized. */
	if (head.bits.generation_bit == tail.bits.generation_bit) {
		/* same generation */
		initialized = (head.bits.offset - tail.bits.offset);
	} else {
		/* different generation */
		initialized = head.bits.offset +
			(size * sizeof(struct appnic_dma_descriptor) -
			 tail.bits.offset);
	}

	/* Number of descriptors is offset / sizeof(a descriptor). */
	initialized /= sizeof(struct appnic_dma_descriptor);

	return initialized;
}

/*
 * ----------------------------------------------------------------------
 * queue_uninitialzed
 *
 * Returns the number of unused/uninitialized descriptors. (from head to tail).
*/

static int queue_uninitialized(union appnic_queue_pointer head,
			       union appnic_queue_pointer tail,
			       int size)
{
	int allocated;

	/* Calculate the number of descriptors currently unused/uninitialized */
	if (head.bits.generation_bit == tail.bits.generation_bit)
		/* Same generation. */
		allocated = ((size * sizeof(struct appnic_dma_descriptor)) -
			 head.bits.offset) + tail.bits.offset;
	else
		/* Different generation. */
		allocated = tail.bits.offset - head.bits.offset;

	/* Number of descriptors is offset / sizeof(a descriptor). */
	allocated /= sizeof(struct appnic_dma_descriptor);

	/* That's all. */
	return allocated;
}

/*
 * ----------------------------------------------------------------------
 * queue_increment
 */

static void queue_increment(union appnic_queue_pointer *queue,
			    int number_of_descriptors)
{
	queue->bits.offset += sizeof(struct appnic_dma_descriptor);

	if ((number_of_descriptors * sizeof(struct appnic_dma_descriptor)) ==
		queue->bits.offset) {

		queue->bits.offset = 0;
		queue->bits.generation_bit =
			(0 == queue->bits.generation_bit) ? 1 : 0;
	}

	return;
}

/*
 * ----------------------------------------------------------------------
 * queue_decrement
 */

static void queue_decrement(union appnic_queue_pointer *queue,
			    int number_of_descriptors)
{
	if (0 == queue->bits.offset) {
		queue->bits.offset =
			((number_of_descriptors - 1) *
			 sizeof(struct appnic_dma_descriptor));
		queue->bits.generation_bit =
			(0 == queue->bits.generation_bit) ? 1 : 0;
	} else {
		queue->bits.offset -= sizeof(struct appnic_dma_descriptor);
	}

	return;
}

/*
 * ----------------------------------------------------------------------
 * disable_rx_tx
 */

static void disable_rx_tx(void)
{
	unsigned long tx_configuration;
	unsigned long rx_configuration;

	pr_info("%s: Disabling the interface.\n", LSI_DRV_NAME);

	rx_configuration = read_mac(APPNIC_RX_CONF);
	rx_configuration &= ~APPNIC_RX_CONF_ENABLE;
	write_mac(rx_configuration, APPNIC_RX_CONF);

	tx_configuration = read_mac(APPNIC_TX_CONF);
	tx_configuration &= ~APPNIC_TX_CONF_ENABLE;

	write_mac(tx_configuration, APPNIC_TX_CONF);

	/* That's all. */
	return;
}


/*
  ======================================================================
  Linux Network Driver Interface
  ======================================================================
*/

/*
 * ----------------------------------------------------------------------
 * handle_transmit_interrupt
 */

static void handle_transmit_interrupt(struct net_device *dev)
{
	struct appnic_device *pdata = netdev_priv(dev);

	/*
	 * The hardware's tail pointer should be one descriptor (or more)
	 * ahead of software's copy.
	 */

	while (0 < queue_initialized(SWAB_QUEUE_POINTER(pdata->tx_tail),
				     pdata->tx_tail_copy, pdata->tx_num_desc)) {
		queue_increment(&pdata->tx_tail_copy, pdata->tx_num_desc);
	}

	return;
}

/*
 * ----------------------------------------------------------------------
 * lsinet_rx_packet
 */

static void lsinet_rx_packet(struct net_device *dev)
{
	struct appnic_device *pdata = netdev_priv(dev);
	struct appnic_dma_descriptor descriptor;
	struct sk_buff *sk_buff;
	unsigned bytes_copied = 0;
	unsigned error_num = 0;
	unsigned long ok_stat = 0, overflow_stat = 0;
	unsigned long crc_stat = 0, align_stat = 0;

	spin_lock(&pdata->extra_lock);

	readdescriptor(((unsigned long)pdata->rx_desc +
			pdata->rx_tail_copy.bits.offset), &descriptor);

	sk_buff = dev_alloc_skb(1600);

	if ((struct sk_buff *)0 == sk_buff) {
		pr_err("%s: dev_alloc_skb() failed! Dropping packet.\n",
		       LSI_DRV_NAME);
		spin_unlock(&pdata->extra_lock);
		return;
	}

	ok_stat = read_mac(APPNIC_RX_STAT_PACKET_OK);
	overflow_stat = read_mac(APPNIC_RX_STAT_OVERFLOW);
	crc_stat = read_mac(APPNIC_RX_STAT_CRC_ERROR);
	align_stat = read_mac(APPNIC_RX_STAT_ALIGN_ERROR);

	/*
	 * Copy the received packet into the skb.
	 */

	while (0 < queue_initialized(SWAB_QUEUE_POINTER(pdata->rx_tail),
				pdata->rx_tail_copy, pdata->rx_num_desc)) {

#ifdef CONFIG_PRELOAD_RX_BUFFERS
		{
			unsigned char *buffer;
			buffer = skb_put(sk_buff, descriptor.pdu_length);
			memcmp(buffer, buffer, descriptor.pdu_length);
			memcpy((void *)buffer,
			       (void *)(descriptor.host_data_memory_pointer +
				 pdata->dma_alloc_offset_rx),
			       descriptor.pdu_length);
		}
#else
		memcpy((void *)skb_put(sk_buff, descriptor.pdu_length),
		       (void *)(descriptor.host_data_memory_pointer +
				pdata->dma_alloc_offset_rx),
		       descriptor.pdu_length);
#endif
		bytes_copied += descriptor.pdu_length;
		descriptor.data_transfer_length = pdata->rx_buf_per_desc;
		writedescriptor(((unsigned long)pdata->rx_desc +
					pdata->rx_tail_copy.bits.offset),
				&descriptor);
		if (0 != descriptor.error)
			error_num = 1;
		queue_increment(&pdata->rx_tail_copy, pdata->rx_num_desc);
		if (0 != descriptor.end_of_packet)
			break;
		readdescriptor(((unsigned long)pdata->rx_desc +
					pdata->rx_tail_copy.bits.offset),
			       &descriptor);
	}

	if (0 == descriptor.end_of_packet) {
		pr_err("%s: No end of packet! %lu/%lu/%lu/%lu\n",
		       LSI_DRV_NAME, ok_stat, overflow_stat,
		       crc_stat, align_stat);
		BUG();
		dev_kfree_skb(sk_buff);

	} else {
		if (0 == error_num) {
			struct ethhdr *ethhdr =
				(struct ethhdr *) sk_buff->data;
			unsigned char broadcast[] = { 0xff, 0xff, 0xff,
						      0xff, 0xff, 0xff };
			unsigned char multicast[] = { 0x01, 0x00 };

			if ((0 == memcmp((const void *)&(ethhdr->h_dest[0]),
					 (const void *)&(dev->dev_addr[0]),
					 sizeof(ethhdr->h_dest))) ||
			    (0 == memcmp((const void *)&(ethhdr->h_dest[0]),
					 (const void *) &(broadcast[0]),
					 sizeof(ethhdr->h_dest))) ||
			    (0 == memcmp((const void *)&(ethhdr->h_dest[0]),
					 (const void *) &(multicast[0]),
					 sizeof(multicast)))) {

				pdata->stats.rx_bytes += bytes_copied;
				++pdata->stats.rx_packets;
				sk_buff->dev = dev;
				sk_buff->protocol = eth_type_trans(sk_buff,
								   dev);
				if (netif_receive_skb(sk_buff) == NET_RX_DROP)
					++dropped_by_stack;
			} else {
				dev_kfree_skb(sk_buff);
			}
		} else {
			dev_kfree_skb(sk_buff);

			if (0 != overflow_stat)
				++pdata->stats.rx_fifo_errors;
			else if (0 != crc_stat)
				++pdata->stats.rx_crc_errors;
			else if (0 != align_stat)
				++pdata->stats.rx_frame_errors;
		}
	}

	spin_unlock(&pdata->extra_lock);

	/* That's all. */
	return;
}

/*
 * ----------------------------------------------------------------------
 * lsinet_rx_packets
 */

static int lsinet_rx_packets(struct net_device *dev, int max)
{
	struct appnic_device *pdata = netdev_priv(dev);
	union appnic_queue_pointer queue;
	int updated_head_pointer = 0;
	int packets = 0;

	queue.raw = pdata->rx_tail_copy.raw;

	/* Receive Packets */

	while (0 < queue_initialized(SWAB_QUEUE_POINTER(pdata->rx_tail),
				     queue, pdata->rx_num_desc)) {
		struct appnic_dma_descriptor descriptor;

		readdescriptor(((unsigned long)pdata->rx_desc +
				  queue.bits.offset),
				&descriptor);

		if (0 != descriptor.end_of_packet) {
			lsinet_rx_packet(dev);
			++packets;
			queue.raw = pdata->rx_tail_copy.raw;

			if ((-1 != max) && (packets == max))
				break;
		} else {
			queue_increment(&queue, pdata->rx_num_desc);
		}
	}

	/* Update the Head Pointer */

	while (1 < queue_uninitialized(pdata->rx_head,
				       pdata->rx_tail_copy,
				       pdata->rx_num_desc)) {

		struct appnic_dma_descriptor descriptor;

		readdescriptor(((unsigned long)pdata->rx_desc +
				  pdata->rx_head.bits.offset), &descriptor);
		descriptor.data_transfer_length = pdata->rx_buf_per_desc;
		descriptor.write = 1;
		descriptor.pdu_length = 0;
		descriptor.start_of_packet = 0;
		descriptor.end_of_packet = 0;
		descriptor.interrupt_on_completion = 1;
		writedescriptor(((unsigned long)pdata->rx_desc +
				   pdata->rx_head.bits.offset),
				 &descriptor);
		queue_increment(&pdata->rx_head, pdata->rx_num_desc);
		updated_head_pointer = 1;
	}

	if (0 != updated_head_pointer)
		write_mac(pdata->rx_head.raw, APPNIC_DMA_RX_HEAD_POINTER);

	return packets;
}

/*
 * ----------------------------------------------------------------------
 * lsinet_poll
 */

static int lsinet_poll(struct napi_struct *napi, int budget)
{
	struct appnic_device *pdata =
		container_of(napi, struct appnic_device, napi);
	struct net_device *dev = pdata->device;
	union appnic_queue_pointer queue;

	int cur_budget = budget;
	unsigned long dma_interrupt_status;

	queue.raw = pdata->rx_tail_copy.raw;

	do {
		/* Acknowledge the RX interrupt. */
		write_mac(~APPNIC_DMA_INTERRUPT_ENABLE_RECEIVE,
			   APPNIC_DMA_INTERRUPT_STATUS);

		cur_budget -= lsinet_rx_packets(dev, cur_budget);
		if (0 == cur_budget)
			break;

		dma_interrupt_status = read_mac(APPNIC_DMA_INTERRUPT_STATUS);

	} while ((RX_INTERRUPT(dma_interrupt_status)) && cur_budget);

	napi_complete(napi);

	/*
	 * Re-enable receive interrupts (and preserve
	 * the already enabled TX interrupt).
	 */
	write_mac((APPNIC_DMA_INTERRUPT_ENABLE_RECEIVE |
		   APPNIC_DMA_INTERRUPT_ENABLE_TRANSMIT),
		  APPNIC_DMA_INTERRUPT_ENABLE);

	return 0;
}

/*
 * ----------------------------------------------------------------------
 * appnic_isr
 */

static irqreturn_t appnic_isr(int irq, void *device_id)
{
	struct net_device *dev = (struct net_device *)device_id;
	struct appnic_device *pdata = netdev_priv(dev);
	unsigned long dma_interrupt_status;
	unsigned long flags;

	/* Acquire the lock */
	spin_lock_irqsave(&pdata->dev_lock, flags);

	/* Get the status. */
	dma_interrupt_status = read_mac(APPNIC_DMA_INTERRUPT_STATUS);

	/* NAPI - don't ack RX interrupt */
	write_mac(APPNIC_DMA_INTERRUPT_ENABLE_RECEIVE,
		  APPNIC_DMA_INTERRUPT_STATUS);

	/* Handle interrupts */
	if (TX_INTERRUPT(dma_interrupt_status)) {
		/* transmition complete */
		++transmit_interrupts;
		handle_transmit_interrupt(dev);
	}

	if (RX_INTERRUPT(dma_interrupt_status)) {
		++receive_interrupts;
		if (napi_schedule_prep(&pdata->napi)) {

			/*
			 * Disable RX interrupts and tell the
			 * system we've got work
			 */
			write_mac(APPNIC_DMA_INTERRUPT_ENABLE_TRANSMIT,
				  APPNIC_DMA_INTERRUPT_ENABLE);
			__napi_schedule(&pdata->napi);
		} else {
			write_mac(APPNIC_DMA_INTERRUPT_ENABLE_TRANSMIT,
				  APPNIC_DMA_INTERRUPT_ENABLE);
		}
	}

	/* Release the lock */
	spin_unlock_irqrestore(&pdata->dev_lock, flags);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER

/*
 * ----------------------------------------------------------------------
 * appnic_poll_controller
 *
 * Polling receive - used by netconsole and other diagnostic tools
 * to allow network i/o with interrupts disabled.
 */

static void appnic_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	appnic_isr(dev->irq, dev);
	enable_irq(dev->irq);
}

#endif


/*
 * ----------------------------------------------------------------------
 * appnic_open
 *
 * Opens the interface.  The interface is opened whenever ifconfig
 * activates it.  The open method should register any system resource
 * it needs (I/O ports, IRQ, DMA, etc.) turn on the hardware, and
 * increment the module usage count.
 */

static int appnic_open(struct net_device *dev)
{
	struct appnic_device *pdata = netdev_priv(dev);
	int return_code = 0;

	/* Bring the PHY up */
	phy_start(pdata->phy_dev);

	/* Enable NAPI */
	napi_enable(&pdata->napi);

	/* Install the interrupt handlers */
	return_code = request_irq(dev->irq, appnic_isr, IRQF_DISABLED,
				   LSI_DRV_NAME, dev);
	if (0 != return_code) {
		pr_err("%s: request_irq() failed, returned 0x%x/%d\n",
		       LSI_DRV_NAME, return_code, return_code);
		return return_code;
	}

	/* Enable interrupts */
	write_mac((APPNIC_DMA_INTERRUPT_ENABLE_RECEIVE |
		   APPNIC_DMA_INTERRUPT_ENABLE_TRANSMIT),
		   APPNIC_DMA_INTERRUPT_ENABLE);

	/* Let the OS know we are ready to send packets */
	netif_start_queue(dev);

	/* That's all */
	return 0;
}

/*
 * ----------------------------------------------------------------------
 * appnic_stop
 *
 * Stops the interface.  The interface is stopped when it is brought
 * down; operations performed at open time should be reversed.
 */

static int appnic_stop(struct net_device *dev)
{
	struct appnic_device *pdata = netdev_priv(dev);

	pr_info("%s: Stopping the interface.\n", LSI_DRV_NAME);

	/* Disable all device interrupts */
	write_mac(0, APPNIC_DMA_INTERRUPT_ENABLE);
	free_irq(dev->irq, dev);

	/* Indicate to the OS that no more packets should be sent.  */
	netif_stop_queue(dev);
	napi_disable(&pdata->napi);

	/* Stop the receiver and transmitter. */
	disable_rx_tx();

	/* Bring the PHY down. */
	if (pdata->phy_dev)
		phy_stop(pdata->phy_dev);

	/* That's all. */
	return 0;
}

/*
 * ----------------------------------------------------------------------
 * appnic_hard_start_xmit
 *
 * The method initiates the transmission of a packet.  The full packet
 * (protocol headers and all) is contained in a socket buffer (sk_buff)
 * structure.
 *
 * ----- NOTES -----
 *
 * 1) This will not get called again by the kernel until it returns.
 */

static int appnic_hard_start_xmit(struct sk_buff *skb,
		       struct net_device *dev)
{
	struct appnic_device *pdata = netdev_priv(dev);
	int length;
	int buf_per_desc;

	length = skb->len < ETH_ZLEN ? ETH_ZLEN : skb->len;
	buf_per_desc = pdata->tx_buf_sz / pdata->tx_num_desc;

	/*
	 * If enough transmit descriptors are available, copy and transmit.
	 */

	while (((length / buf_per_desc) + 1) >=
		queue_uninitialized(pdata->tx_head,
				    SWAB_QUEUE_POINTER(pdata->tx_tail),
				    pdata->tx_num_desc)) {
		handle_transmit_interrupt(dev);
	}

	if (((length / buf_per_desc) + 1) <
		queue_uninitialized(pdata->tx_head,
				    SWAB_QUEUE_POINTER(pdata->tx_tail),
				    pdata->tx_num_desc)) {
		int bytes_copied = 0;
		struct appnic_dma_descriptor descriptor;

		readdescriptor(((unsigned long)pdata->tx_desc +
				pdata->tx_head.bits.offset), &descriptor);
		descriptor.start_of_packet = 1;

		while (bytes_copied < length) {
			descriptor.write = 1;
			descriptor.pdu_length = length;

			if ((length - bytes_copied) > buf_per_desc) {
				memcpy((void *)
					(descriptor.host_data_memory_pointer +
					 pdata->dma_alloc_offset_tx),
				       (void *) ((unsigned long) skb->data +
					bytes_copied),
					buf_per_desc);
				descriptor.data_transfer_length = buf_per_desc;
				descriptor.end_of_packet = 0;
				descriptor.interrupt_on_completion = 0;
				bytes_copied += buf_per_desc;
			} else {
				memcpy((void *)
					(descriptor.host_data_memory_pointer +
					 pdata->dma_alloc_offset_tx),
				       (void *) ((unsigned long) skb->data +
					bytes_copied),
					(length - bytes_copied));
				descriptor.data_transfer_length =
				 (length - bytes_copied);
				descriptor.end_of_packet = 1;
#ifdef CONFIG_DISABLE_TX_INTERRUPTS
				descriptor.interrupt_on_completion = 0;
#else
				descriptor.interrupt_on_completion = 1;
#endif
				bytes_copied = length;
			}

			pdata->stats.tx_bytes += bytes_copied;
			writedescriptor(((unsigned long) pdata->tx_desc +
				pdata->tx_head.bits.offset), &descriptor);
			queue_increment(&pdata->tx_head, pdata->tx_num_desc);
			readdescriptor(((unsigned long)pdata->tx_desc +
					 pdata->tx_head.bits.offset),
					&descriptor);
			descriptor.start_of_packet = 0;
		}

#ifdef CONFIG_ARM
		/* ARM Data sync barrier */
		asm volatile ("mcr p15,0,%0,c7,c10,4" : : "r" (0));
#endif
		write_mac(pdata->tx_head.raw, APPNIC_DMA_TX_HEAD_POINTER);
		dev->trans_start = jiffies;
	} else {
		++out_of_tx_descriptors;
		pr_err("%s: No transmit descriptors available!\n",
		       LSI_DRV_NAME);
	}

	/* Free the socket buffer */
	dev_kfree_skb(skb);

	return 0;
}

/*
 * ----------------------------------------------------------------------
 * appnic_net_device_stats
 *
 * Whenever an application needs to get statistics for the interface,
 * this method is called.  This happens, for example, when ifconfig or
 * nstat -i is run.
 */

static struct net_device_stats *appnic_get_stats(struct net_device *dev)
{
	struct appnic_device *pdata = netdev_priv(dev);

	/*
	 * Update the statistics structure.
	 */

	get_hw_statistics(pdata);

	/*
	 * That's all.
	 */

	return &pdata->stats;
}

/*
 * ----------------------------------------------------------------------
 * appnic_set_mac_address
 */

static int appnic_set_mac_address(struct net_device *dev, void *data)
{
	struct sockaddr *address = data;
	unsigned long swap_source_address;

	if (netif_running(dev))
		return -EBUSY;

	memcpy(dev->dev_addr, address->sa_data, 6);
	memcpy(dev->perm_addr, address->sa_data, 6);

	swap_source_address = ((address->sa_data[4]) << 8) |
			       address->sa_data[5];
	write_mac(swap_source_address, APPNIC_SWAP_SOURCE_ADDRESS_2);
	swap_source_address = ((address->sa_data[2]) << 8) |
			address->sa_data[3];
	write_mac(swap_source_address, APPNIC_SWAP_SOURCE_ADDRESS_1);
	swap_source_address = ((address->sa_data[0]) << 8) |
			       address->sa_data[1];
	write_mac(swap_source_address, APPNIC_SWAP_SOURCE_ADDRESS_0);
	memcpy(dev->dev_addr, address->sa_data, dev->addr_len);

	return 0;
}

/*
  ======================================================================
  ETHTOOL Operations
  ======================================================================
*/

/*
 * ----------------------------------------------------------------------
 * appnic_get_drvinfo
 */

static void appnic_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strcpy(info->driver, LSI_DRV_NAME);
	strcpy(info->version, LSI_DRV_VERSION);
}

/*
 * ----------------------------------------------------------------------
 * appnic_get_settings
 */

static int appnic_get_settings(struct net_device *dev,
			       struct ethtool_cmd *cmd)
{
	struct appnic_device *pdata = netdev_priv(dev);
	struct phy_device *phydev = pdata->phy_dev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_gset(phydev, cmd);
}

/*
 * Fill in the struture...
 */

static const struct ethtool_ops appnic_ethtool_ops = {
	.get_drvinfo = appnic_get_drvinfo,
	.get_settings = appnic_get_settings
};


/*
  ======================================================================
  Linux Module Interface.
  ======================================================================
*/

static const struct net_device_ops appnic_netdev_ops = {
	.ndo_open = appnic_open,
	.ndo_stop = appnic_stop,
	.ndo_get_stats = appnic_get_stats,
	.ndo_set_mac_address = appnic_set_mac_address,
	.ndo_start_xmit = appnic_hard_start_xmit,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = appnic_poll_controller,
#endif

};

/*
 * ----------------------------------------------------------------------
 * appnic_init
 */

int appnic_init(struct net_device *dev)
{
	struct appnic_device *pdata = netdev_priv(dev);
	void *dma_offset;
	int index;
	unsigned long buf;
	struct appnic_dma_descriptor descriptor;
	struct sockaddr address;

#ifdef CONFIG_ARM
	/* Set FEMAC to uncached */
	gpreg_base = ioremap(GPREG_BASE, 0x1000);
	writel(0x0, gpreg_base+0x78);
#endif


	/*
	 * Reset the MAC
	 */

	write_mac(0x80000000, APPNIC_DMA_PCI_CONTROL);

	/*
	 * Allocate memory and initialize the descriptors
	 */


	/*
	 * fixup num_[rt]x_desc
	 */

	if (0 != (rx_num_desc % DESCRIPTOR_GRANULARITY)) {
		pr_warn("%s: rx_num_desc was not a multiple of %d.\n",
			LSI_DRV_NAME, DESCRIPTOR_GRANULARITY);
		rx_num_desc += DESCRIPTOR_GRANULARITY -
				(rx_num_desc % DESCRIPTOR_GRANULARITY);
	}

	pdata->rx_num_desc = rx_num_desc;

	if (0 != (tx_num_desc % DESCRIPTOR_GRANULARITY)) {
		pr_warn("%s: tx_num_desc was not a multiple of %d.\n",
			LSI_DRV_NAME, DESCRIPTOR_GRANULARITY);
		tx_num_desc += DESCRIPTOR_GRANULARITY -
			(tx_num_desc % DESCRIPTOR_GRANULARITY);
	}

	pdata->tx_num_desc = tx_num_desc;

	/*
	 * up [rt]x_buf_sz. Must be some multiple of 64 bytes
	 * per descriptor.
	 */

	if (0 != (rx_buf_sz % (BUFFER_ALIGNMENT * rx_num_desc))) {
		pr_warn("%s: rx_buf_sz was not a multiple of %d.\n",
			LSI_DRV_NAME, (BUFFER_ALIGNMENT * rx_num_desc));
		rx_buf_sz += (BUFFER_ALIGNMENT * rx_num_desc) -
				(rx_buf_sz % (BUFFER_ALIGNMENT * rx_num_desc));
	}

	pdata->rx_buf_sz = rx_buf_sz;

	if (0 != (tx_buf_sz % (BUFFER_ALIGNMENT * tx_num_desc))) {
		pr_warn("%s: tx_buf_sz was not a multiple of %d.\n",
			LSI_DRV_NAME, (BUFFER_ALIGNMENT * tx_num_desc));
		tx_buf_sz += (BUFFER_ALIGNMENT * tx_num_desc) -
			(tx_buf_sz % (BUFFER_ALIGNMENT * tx_num_desc));
	}

	pdata->tx_buf_sz = tx_buf_sz;

	/*
	 * Allocate dma-able memory. Broken into smaller parts to keep
	 * from allocating a single large chunk of memory, but not too
	 * small since mappings obtained from dma_alloc_coherent() have
	 * a minimum size of one page.
	 */

	pdata->dma_alloc_size =
		/* The tail pointers (rx and tx) */
		(sizeof(union appnic_queue_pointer) * 2) +
		/* The RX descriptor ring (and padding to allow
		 * 64 byte alignment) */
		(sizeof(struct appnic_dma_descriptor) * pdata->rx_num_desc) +
		(DESCRIPTOR_GRANULARITY) +
		/* The TX descriptor ring (and padding...) */
		(sizeof(struct appnic_dma_descriptor) * pdata->tx_num_desc) +
		(DESCRIPTOR_GRANULARITY);

	pdata->dma_alloc_size_rx =
		/* The RX buffer (and padding...) */
		(pdata->rx_buf_sz) + (BUFFER_ALIGNMENT);

	pdata->dma_alloc_size_tx =
		/* The TX buffer (and padding...) */
		(pdata->tx_buf_sz) + (BUFFER_ALIGNMENT);

	/*
	 * This needs to be set to something sane for
	 * dma_alloc_coherent()
	 */

#if defined(CONFIG_ARM)
	pdata->dma_alloc = (void *)
		dma_alloc_coherent(NULL,
				   pdata->dma_alloc_size,
				   &pdata->dma_alloc_dma,
				   GFP_KERNEL);
#else
	dev->dev.archdata.dma_ops = &dma_direct_ops;

	pdata->dma_alloc = (void *)
		dma_alloc_coherent(&dev->dev,
				   pdata->dma_alloc_size,
				   &pdata->dma_alloc_dma,
				   GFP_KERNEL);
#endif

	if ((void *)0 == pdata->dma_alloc) {
		pr_err("%s: Can't allocate %d bytes of DMA-able memory!\n",
		       LSI_DRV_NAME, pdata->dma_alloc_size);
		kfree(pdata);
		return -ENOMEM;
	}

	pdata->dma_alloc_offset = (int)pdata->dma_alloc -
					(int)pdata->dma_alloc_dma;

#ifdef CONFIG_ARM
	pdata->dma_alloc_rx = (void *)dma_alloc_coherent(NULL,
#else
	pdata->dma_alloc_rx = (void *)dma_alloc_coherent(&dev->dev,
#endif
						    pdata->dma_alloc_size_rx,
						    &pdata->dma_alloc_dma_rx,
						    GFP_KERNEL);

	if ((void *)0 == pdata->dma_alloc_rx) {
		pr_err("%s: Can't allocate %d bytes of RX DMA-able memory!\n",
		       LSI_DRV_NAME, pdata->dma_alloc_size_rx);
		dma_free_coherent(&dev->dev, pdata->dma_alloc_size,
				  pdata->dma_alloc, pdata->dma_alloc_dma);
		kfree(pdata);
		return -ENOMEM;
	}

	pdata->dma_alloc_offset_rx = (int)pdata->dma_alloc_rx -
					(int)pdata->dma_alloc_dma_rx;

#ifdef CONFIG_ARM
	pdata->dma_alloc_tx = (void *)dma_alloc_coherent(NULL,
#else
	pdata->dma_alloc_tx = (void *)dma_alloc_coherent(&dev->dev,
#endif
						    pdata->dma_alloc_size_tx,
						    &pdata->dma_alloc_dma_tx,
						    GFP_KERNEL);

	if ((void *)0 == pdata->dma_alloc_tx) {
		pr_err("%s: Can't allocate %d bytes of TX DMA-able memory!\n",
		       LSI_DRV_NAME, pdata->dma_alloc_size_tx);
		dma_free_coherent(&dev->dev, pdata->dma_alloc_size,
				  pdata->dma_alloc, pdata->dma_alloc_dma);
		dma_free_coherent(&dev->dev, pdata->dma_alloc_size_rx,
				  pdata->dma_alloc_rx, pdata->dma_alloc_dma_rx);
		kfree(pdata);
		return -ENOMEM;
	}

	pdata->dma_alloc_offset_tx = (int)pdata->dma_alloc_tx -
					(int)pdata->dma_alloc_dma_tx;

	/*
	 * Initialize the tail pointers
	 */

	dma_offset = pdata->dma_alloc;

	pdata->rx_tail = (union appnic_queue_pointer *)dma_offset;
	pdata->rx_tail_dma = (int)pdata->rx_tail - (int)pdata->dma_alloc_offset;
	dma_offset += sizeof(union appnic_queue_pointer);
	memset((void *)pdata->rx_tail, 0,
	       sizeof(union appnic_queue_pointer));


	pdata->tx_tail = (union appnic_queue_pointer *)dma_offset;
	pdata->tx_tail_dma = (int)pdata->tx_tail - (int)pdata->dma_alloc_offset;
	dma_offset += sizeof(union appnic_queue_pointer);
	memset((void *)pdata->tx_tail, 0, sizeof(union appnic_queue_pointer));


	/*
	 * Initialize the descriptor pointers
	 */

	pdata->rx_desc = (struct appnic_dma_descriptor *)ALIGN64B(dma_offset);
	pdata->rx_desc_dma = (int)pdata->rx_desc - (int)pdata->dma_alloc_offset;
	dma_offset += (sizeof(struct appnic_dma_descriptor) *
			pdata->rx_num_desc) + (DESCRIPTOR_GRANULARITY);
	memset((void *)pdata->rx_desc, 0,
	       (sizeof(struct appnic_dma_descriptor) * pdata->rx_num_desc));

	pdata->tx_desc = (struct appnic_dma_descriptor *)ALIGN64B(dma_offset);
	pdata->tx_desc_dma = (int)pdata->tx_desc - (int)pdata->dma_alloc_offset;
	dma_offset += (sizeof(struct appnic_dma_descriptor) *
			pdata->tx_num_desc) + (DESCRIPTOR_GRANULARITY);
	memset((void *)pdata->tx_desc, 0,
	       (sizeof(struct appnic_dma_descriptor) * pdata->tx_num_desc));

	/*
	 * Initialize the buffer pointers
	 */

	dma_offset = pdata->dma_alloc_rx;

	pdata->rx_buf = (void *)ALIGN64B(dma_offset);
	pdata->rx_buf_dma = (int)pdata->rx_buf -
				(int)pdata->dma_alloc_offset_rx;
	pdata->rx_buf_per_desc = pdata->rx_buf_sz / pdata->rx_num_desc;

	dma_offset = pdata->dma_alloc_tx;

	pdata->tx_buf = (void *)ALIGN64B(dma_offset);
	pdata->tx_buf_dma = (int)pdata->tx_buf -
				(int)pdata->dma_alloc_offset_tx;
	pdata->tx_buf_per_desc = pdata->tx_buf_sz / pdata->tx_num_desc;

	/*
	 * Initialize the descriptors
	 */

	buf = (unsigned long)pdata->rx_buf_dma;
	for (index = 0; index < pdata->rx_num_desc; ++index) {
		memset((void *) &descriptor, 0,
		       sizeof(struct appnic_dma_descriptor));
		descriptor.write = 1;
		descriptor.interrupt_on_completion = 1;
		descriptor.host_data_memory_pointer = buf;
		descriptor.data_transfer_length = pdata->rx_buf_per_desc;

		writedescriptor(((unsigned long)pdata->rx_desc + (index *
				sizeof(struct appnic_dma_descriptor))),
				&descriptor);

		buf += pdata->rx_buf_per_desc;
	}

	buf = (unsigned long)pdata->tx_buf_dma;

	for (index = 0; index < pdata->tx_num_desc; ++index) {
		memset((void *) &descriptor, 0,
		       sizeof(struct appnic_dma_descriptor));
		descriptor.write = 1;
		descriptor.interrupt_on_completion = 1;
		descriptor.host_data_memory_pointer = buf;

		writedescriptor(((unsigned long)pdata->tx_desc + (index *
				 sizeof(struct appnic_dma_descriptor))),
				&descriptor);

		buf += pdata->tx_buf_per_desc;
	}

	/*
	 * Initialize the spinlocks.
	 */

	spin_lock_init(&pdata->dev_lock);
	spin_lock_init(&pdata->extra_lock);

	/*
	 * Take MAC out of reset
	 */

	write_mac(0x0, APPNIC_RX_SOFT_RESET);
	write_mac(0x1, APPNIC_RX_MODE);
	write_mac(0x0, APPNIC_TX_SOFT_RESET);
	write_mac(0x1, APPNIC_TX_MODE);
	write_mac(0x300a, APPNIC_TX_WATERMARK);
	write_mac(0x1, APPNIC_TX_HALF_DUPLEX_CONF);
	write_mac(0xffff, APPNIC_TX_TIME_VALUE_CONF);
	write_mac(0x1, APPNIC_TX_INTERRUPT_CONTROL);
	write_mac(0x5275, APPNIC_TX_EXTENDED_CONF);
	write_mac(0x1, APPNIC_RX_INTERNAL_INTERRUPT_CONTROL);
	write_mac(0x1, APPNIC_RX_EXTERNAL_INTERRUPT_CONTROL);
	write_mac(0x40010000, APPNIC_DMA_PCI_CONTROL);
	write_mac(0x30000, APPNIC_DMA_CONTROL);
#ifdef CONFIG_ARM
	writel(0x280044, dma_base + 0x60);
	writel(0xc0, dma_base + 0x64);
#else
	out_le32(dma_base + 0x60, 0x280044);
	out_le32(dma_base + 0x64, 0xc0);
#endif

	/*
	 * Set the MAC address
	 */
	pr_info("%s: MAC %02x:%02x:%02x:%02x:%02x:%02x\n", LSI_DRV_NAME,
		dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
		dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);

	memcpy(&(address.sa_data[0]), dev->dev_addr, 6);
	appnic_set_mac_address(dev, &address);

	/*
	 * Initialize the queue pointers.
	 */

	/*
	 * Receiver
	 */

	memset((void *)&pdata->rx_tail_copy, 0,
	       sizeof(union appnic_queue_pointer));
	memset((void *)&pdata->rx_head, 0,
	       sizeof(union appnic_queue_pointer));

	write_mac(pdata->rx_desc_dma, APPNIC_DMA_RX_QUEUE_BASE_ADDRESS);
	write_mac((pdata->rx_num_desc *
		   sizeof(struct appnic_dma_descriptor)) / 1024,
		  APPNIC_DMA_RX_QUEUE_SIZE);

	/*
	 * Indicate that all of the receive descriptors
	 * are ready
	 */

	pdata->rx_head.bits.offset = (pdata->rx_num_desc - 1) *
					sizeof(struct appnic_dma_descriptor);
	write_mac(pdata->rx_tail_dma, APPNIC_DMA_RX_TAIL_POINTER_ADDRESS);

	/*
	 * N.B.
	 *
	 * The boot loader may have used the NIC.  If so, the
	 * tail pointer must be read and the head pointer (and
	 * local copy of the tail) based on it.
	 */

	pdata->rx_tail->raw =
		  read_mac(APPNIC_DMA_RX_TAIL_POINTER_LOCAL_COPY);
	pdata->rx_tail_copy.raw = pdata->rx_tail->raw;
	pdata->rx_head.raw = pdata->rx_tail->raw;
	queue_decrement(&pdata->rx_head, pdata->rx_num_desc);
	pdata->rx_head.bits.generation_bit =
		  (0 == pdata->rx_head.bits.generation_bit) ? 1 : 0;
	write_mac(pdata->rx_head.raw, APPNIC_DMA_RX_HEAD_POINTER);

	/*
	 * Transmitter
	 */

	memset((void *) &pdata->tx_tail_copy, 0,
	       sizeof(union appnic_queue_pointer));
	memset((void *) &pdata->tx_head, 0,
	       sizeof(union appnic_queue_pointer));

	write_mac(pdata->tx_desc_dma, APPNIC_DMA_TX_QUEUE_BASE_ADDRESS);
	write_mac((pdata->tx_num_desc *
		   sizeof(struct appnic_dma_descriptor)) / 1024,
		  APPNIC_DMA_TX_QUEUE_SIZE);
	write_mac(pdata->tx_tail_dma, APPNIC_DMA_TX_TAIL_POINTER_ADDRESS);

	/*
	 * N.B.
	 *
	 * The boot loader may have used the NIC.  If so, the
	 * tail pointer must be read and the head pointer (and
	 * local copy of the tail) based on it.
	 */

	pdata->tx_tail->raw = read_mac(APPNIC_DMA_TX_TAIL_POINTER_LOCAL_COPY);
	pdata->tx_tail_copy.raw = pdata->tx_tail->raw;
	pdata->tx_head.raw = pdata->tx_tail->raw;
	write_mac(pdata->tx_head.raw, APPNIC_DMA_TX_HEAD_POINTER);

	/* Clear statistics */

	clear_statistics(pdata);

	/* Fill in the net_device structure */

	ether_setup(dev);
#ifdef CONFIG_ARM
	dev->irq = pdata->dma_interrupt;
#else
	dev->irq = irq_create_mapping(NULL, pdata->dma_interrupt);
	if (NO_IRQ == dev->irq) {
		pr_err("%s: irq_create_mapping() failed\n", LSI_DRV_NAME);
		return -EBUSY;
	}

	if (0 != irq_set_irq_type(dev->irq, IRQ_TYPE_LEVEL_HIGH)) {
		pr_err("%s: set_irq_type() failed\n", LSI_DRV_NAME);
		return -EBUSY;
	}
#endif

	dev->netdev_ops = &appnic_netdev_ops;

	SET_ETHTOOL_OPS(dev, &appnic_ethtool_ops);
	memset((void *) &pdata->napi, 0, sizeof(struct napi_struct));
	netif_napi_add(dev, &pdata->napi,
		       lsinet_poll, LSINET_NAPI_WEIGHT);
	pdata->device = dev;

	/* That's all */
	return 0;
}

/*
 * ----------------------------------------------------------------------
 * appnic_read_proc
 */

static int
appnic_read_proc(char *page, char **start, off_t offset,
		 int count, int *eof, void *data)
{
	int length;

	length = sprintf(page, "-- appnic.c -- Profiling is disabled\n");

	/* That's all */
	return length;
}

/*
 * ----------------------------------------------------------------------
 * appnic_probe_config_dt
 */

#ifdef CONFIG_OF
static int __devinit appnic_probe_config_dt(struct net_device *dev,
					    struct device_node *np)
{
	struct appnic_device *pdata = netdev_priv(dev);
	const u32 *field;
	const char *macspeed;
	int length;
#ifndef CONFIG_ARM
	u64 value64;
	u32 value32;
#endif

	if (!np)
		return -ENODEV;

#ifdef CONFIG_ARM
	rx_base = of_iomap(np, 0);
	tx_base = of_iomap(np, 1);
	dma_base = of_iomap(np, 2);
#else
	field = of_get_property(np, "enabled", NULL);

	if (!field || (field && (0 == *field)))
		return -EINVAL;

	field = of_get_property(np, "reg", NULL);

	if (!field) {
		pr_err("%s: Couldn't get \"reg\" property.", LSI_DRV_NAME);
		return -EINVAL;
	}

	value64 = of_translate_address(np, field);
	value32 = field[1];
	field += 2;
	rx_base = ioremap(value64, value32);
	value64 = of_translate_address(np, field);
	value32 = field[1];
	field += 2;
	tx_base = ioremap(value64, value32);
	value64 = of_translate_address(np, field);
	value32 = field[1];
	field += 2;
	dma_base = ioremap(value64, value32);
#endif
	pdata->rx_base = (unsigned long)rx_base;
	pdata->tx_base = (unsigned long)tx_base;
	pdata->dma_base = (unsigned long)dma_base;

#ifdef CONFIG_ARM
	pdata->tx_interrupt = irq_of_parse_and_map(np, 0);
	pdata->rx_interrupt = irq_of_parse_and_map(np, 1);
	pdata->dma_interrupt = irq_of_parse_and_map(np, 2);
#else
	field = of_get_property(np, "interrupts", NULL);
	if (!field)
		goto device_tree_failed;
	else
		pdata->dma_interrupt = field[0];
#endif

	field = of_get_property(np, "mdio-clock", NULL);
	if (!field)
		goto device_tree_failed;
	else
		pdata->mdio_clock = ntohl(field[0]);

	field = of_get_property(np, "phy-address", NULL);
	if (!field)
		goto device_tree_failed;
	else
		pdata->phy_address = ntohl(field[0]);

	field = of_get_property(np, "ad-value", NULL);
	if (!field)
		goto device_tree_failed;
	else
		pdata->ad_value = ntohl(field[0]);

	macspeed = of_get_property(np, "phy-link", NULL);

	if (macspeed) {
		if (0 == strncmp(macspeed, "auto", strlen("auto"))) {
			pdata->phy_link_auto = 1;
		} else if (0 == strncmp(macspeed, "100MF", strlen("100MF"))) {
			pdata->phy_link_auto = 0;
			pdata->phy_link_speed = 1;
			pdata->phy_link_duplex = 1;
		} else if (0 == strncmp(macspeed, "100MH", strlen("100MH"))) {
			pdata->phy_link_auto = 0;
			pdata->phy_link_speed = 1;
			pdata->phy_link_duplex = 0;
		} else if (0 == strncmp(macspeed, "10MF", strlen("10MF"))) {
			pdata->phy_link_auto = 0;
			pdata->phy_link_speed = 0;
			pdata->phy_link_duplex = 1;
		} else if (0 == strncmp(macspeed, "10MH", strlen("10MH"))) {
			pdata->phy_link_auto = 0;
			pdata->phy_link_speed = 0;
			pdata->phy_link_duplex = 0;
		} else {
			pr_err(
			  "Invalid phy-link value \"%s\" in DTS. Defaulting to \"auto\".\n",
			       macspeed);
			pdata->phy_link_auto = 1;
		}
	} else {
		/* Auto is the default. */
		pdata->phy_link_auto = 1;
	}

	field = of_get_property(np, "mac-address", &length);
	if (!field || 6 != length) {
		goto device_tree_failed;
	} else {
		int i;
		u8 *value;

		value = (u8 *)field;

		for (i = 0; i < 6; ++i)
			pdata->mac_addr[i] = value[i];
	}

	memcpy(dev->dev_addr, &pdata->mac_addr[0], 6);
	memcpy(dev->perm_addr, &pdata->mac_addr[0], 6);

	return 0;

device_tree_failed:
	pr_err("%s: Reading Device Tree Failed\n", LSI_DRV_NAME);
	iounmap(rx_base);
	iounmap(tx_base);
	iounmap(dma_base);
	iounmap(gpreg_base);
	return -EINVAL;
}
#else
static inline int appnic_probe_config_dt(struct net_device *dev,
					 struct device_node *np)
{
	return -ENODEV;
}
#endif /* CONFIG_OF */

/*
 * ----------------------------------------------------------------------
 * appnic_drv_probe
 */

static int __devinit appnic_drv_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct device_node *np = pdev->dev.of_node;
	struct net_device *dev;
	struct appnic_device *pdata;

	pr_info("%s: LSI(R) 10/100 Network Driver - version %s\n",
		LSI_DRV_NAME, LSI_DRV_VERSION);

	/* Allocate space for the device. */

	dev = alloc_etherdev(sizeof(struct appnic_device));
	if (!dev) {
		pr_err("%s: Couldn't allocate net device.\n", LSI_DRV_NAME);
		rc = -ENOMEM;
		goto out;
	}

	SET_NETDEV_DEV(dev, &pdev->dev);
	platform_set_drvdata(pdev, dev);

	pdata = netdev_priv(dev);

	/*
	 * Get the physical addresses, interrupt number, etc. from the
	 * device tree.  If no entry exists (older boot loader...) just
	 * use the pre-devicetree method.
	 */

	rc = appnic_probe_config_dt(dev, np);

	if (rc == -EINVAL) {
		goto out;
	} else if (rc == -ENODEV) {

#ifdef CONFIG_MTD_NAND_EP501X_UBOOTENV

		/*
		 * Attempt to get device settings from the DTB failed, so
		 * try to grab the ethernet MAC from the u-boot environment
		 * and use hard-coded values for device base addresses.
		 */

		unsigned char ethaddr_string[20];

		if (0 != ubootenv_get("ethaddr", ethaddr_string)) {
			pr_err("%s: Could not read ethernet address!\n",
			       LSI_DRV_NAME);
			return -EFAULT;
		} else {

			u8 mac_address[6];
			int i = 0;
			char *string = ethaddr_string;

			while ((0 != string) && (6 > i)) {
				char *value;
				unsigned long res;
				value = strsep(&string, ":");
				if (kstrtoul(value, 16, &res))
					return -EBUSY;
				mac_address[i++] = (u8)res;
			}

			memcpy(dev->dev_addr, mac_address, 6);
			memcpy(dev->perm_addr, mac_address, 6);
			dev->addr_len = 6;

			pr_info("%s: Using Static Addresses and Interrupts",
				LSI_DRV_NAME);
			rx_base = ioremap(0x002000480000ULL, 0x1000);
			pdata->rx_base =
			 (unsigned long)ioremap(0x002000480000ULL, 0x1000);
			tx_base = ioremap(0x002000481000ULL, 0x1000);
			pdata->tx_base =
			(unsigned long)ioremap(0x002000481000ULL, 0x1000);
			dma_base = ioremap(0x002000482000ULL, 0x1000);
			pdata->dma_base =
			 (unsigned long)ioremap(0x002000482000ULL, 0x1000);
			pdata->dma_interrupt = 33;
		}
#else
		/* Neither dtb info nor ubootenv driver found. */
		pr_err("%s: Could not read ethernet address!", LSI_DRV_NAME);
		return -EBUSY;
#endif

	}

#ifdef CONFIG_MTD_NAND_EP501X_UBOOTENV

	{
		unsigned char uboot_env_string[20];

		/* Override ad_value with u-boot environment variable if set. */
		if (0 == ubootenv_get("ad_value", uboot_env_string)) {
			/*
			 * Assume ad_value is always entered as a hex value,
			 * since u-boot defaults this value as hex.
			 */
			unsigned long res;
			if (kstrtoul(uboot_env_string, 16, &res))
				return -EBUSY;
			pdata->ad_value = res;
		}
	}

#endif

	/* Initialize the device. */
	rc = appnic_init(dev);
	if (0 != rc) {
		pr_err("%s: appnic_init() failed: %d\n", LSI_DRV_NAME, rc);
		rc = -ENODEV;
		goto out;
	}

	/* Register the device. */
	rc = register_netdev(dev);
	if (0 != rc) {
		pr_err("%s: register_netdev() failed: %d\n", LSI_DRV_NAME, rc);
		rc = -ENODEV;
		goto out;
	}

	/* Initialize the PHY. */
	rc = appnic_mii_init(pdev, dev);
	if (rc) {
		pr_warn("%s: Failed to initialize PHY", LSI_DRV_NAME);
		rc = -ENODEV;
		goto out;
	}

	/* Create the /proc entry. */
	create_proc_read_entry("driver/appnic", 0, NULL,
				appnic_read_proc, NULL);

out:
	return rc;
}

/*
 * ----------------------------------------------------------------------
 * appnic_drv_remove
 */

static int __devexit appnic_drv_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct appnic_device *pdata;

	pr_info("%s: Stopping driver", LSI_DRV_NAME);

	remove_proc_entry("driver/appnic", NULL);

	if (dev) {
		pdata = netdev_priv(dev);
		if (pdata->phy_dev)
			phy_disconnect(pdata->phy_dev);
		mdiobus_unregister(pdata->mii_bus);
		mdiobus_free(pdata->mii_bus);
		platform_set_drvdata(pdev, NULL);
		unregister_netdev(dev);
		free_irq(dev->irq, dev);
		dma_free_coherent(&dev->dev, pdata->dma_alloc_size,
				  pdata->dma_alloc, pdata->dma_alloc_dma);
		dma_free_coherent(&dev->dev, pdata->dma_alloc_size_rx,
				  pdata->dma_alloc_rx, pdata->dma_alloc_dma_rx);
		dma_free_coherent(&dev->dev, pdata->dma_alloc_size_tx,
				  pdata->dma_alloc_tx, pdata->dma_alloc_dma_tx);
		free_netdev(dev);
	}

	iounmap(rx_base);
	iounmap(tx_base);
	iounmap(dma_base);
	iounmap(gpreg_base);

	return 0;
}

static const struct of_device_id appnic_dt_ids[] = {
	{ .compatible = "acp-femac", }
};
MODULE_DEVICE_TABLE(of, appnic_dt_ids);

static struct platform_driver appnic_driver = {
	.probe = appnic_drv_probe,
	.remove = __devexit_p(appnic_drv_remove),
	.driver = {
		.name   = LSI_DRV_NAME,
		.owner  = THIS_MODULE,
		.pm     = NULL,
		.of_match_table = appnic_dt_ids,
	},
};

/* Entry point for loading the module */
static int __init appnic_init_module(void)
{
	return platform_driver_register(&appnic_driver);
}

/* Entry point for unloading the module */
static void __exit appnic_cleanup_module(void)
{
	platform_driver_unregister(&appnic_driver);
}

module_init(appnic_init_module);
module_exit(appnic_cleanup_module);
