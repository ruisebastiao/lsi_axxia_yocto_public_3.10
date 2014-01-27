/*
 * AXXIA RapidIO support
 *
 *   This program is free software; you can redistribute  it and/or modify it
 *   under  the terms of  the GNU General  Public License as published by the
 *   Free Software Foundation;  either version 2 of the  License, or (at your
 *   option) any later version.
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

#include <linux/io.h>
/* #include <asm/machdep.h> */
#include <linux/uaccess.h>

#include "axxia-rio.h"

#ifdef CONFIG_RAPIDIO_HOTPLUG

struct acp_rio_work {
	struct rio_mport *mport;
	u32 flags;
	struct completion *cmp;
	struct work_struct work;
};

/**
 * srds_phy_reset - Do Soft Reset of the RIO SerDes Controller
 *
 * @mport: Master port
 *
 */
static inline void __srds_phy_disable(struct rio_mport *mport)
{
	u32 srds_ctrl;

	__rio_local_read_config_32(mport, RAB_SRDS_CTRL1, &srds_ctrl);
	srds_ctrl |= RAB_SRDS_CTRL1_RST;
	__rio_local_write_config_32(mport, RAB_SRDS_CTRL1, srds_ctrl);
	udelay(50);
}

static inline void __srds_phy_enable(struct rio_mport *mport)
{
	u32 srds_ctrl;

	__rio_local_read_config_32(mport, RAB_SRDS_CTRL1, &srds_ctrl);
	srds_ctrl &= ~RAB_SRDS_CTRL1_RST;
	__rio_local_write_config_32(mport, RAB_SRDS_CTRL1, srds_ctrl);
	msleep(100);
#if defined(CONFIG_AXXIA_RIO_16B_ID)
	__rio_local_read_config_32(mport, RAB_SRDS_CTRL0, &srds_ctrl);
	__rio_local_write_config_32(mport, RAB_SRDS_CTRL0,
					 srds_ctrl | RAB_SRDS_CTRL0_16B_ID);
#endif

}
/**
 * rio_rab_pio_reset - Reset Peripheral Bus bridge,
 *                     PIO engines including engine specific
 *                     HW register
 *
 * @mport: Master port
 *
 * Disable AXI PIO + outbound nwrite/nread/maintenance
 * Disable RIO PIO (enable rx maint port-write packet)
 */
static inline void __rio_rab_pio_disable(struct rio_mport *mport)
{
	__rio_local_write_config_32(mport, RAB_PIO_RESET,
				(RAB_PIO_RESET_RPIO | RAB_PIO_RESET_APIO));
	msleep(1);
}

static inline void __rio_rab_pio_enable(struct rio_mport *mport)
{
	__rio_local_write_config_32(mport, RAB_PIO_RESET, 0);
	msleep(1);
}

static int get_input_status(struct rio_mport *mport, u32 *rsp)
{
	struct rio_priv *priv = mport->priv;
	int checkcount;
	u32 sts_csr, rsp_csr;

	/* Debug: Show current state */
	__rio_local_read_config_32(mport, RIO_ACK_STS_CSR(priv->portNdx), &sts_csr);

	dev_dbg(priv->dev, "RIO_ACK_STS_CSR(%d) %8.8x, IA %x, OUTA %x, OBA %x\n",
		priv->portNdx,
		sts_csr, (sts_csr & RIO_ACK_STS_IA) >> 24,
		(sts_csr & RIO_ACK_STS_OUTA) >> 8,
		(sts_csr) & RIO_ACK_STS_OBA);

	/* Read to clear valid bit... */
	__rio_local_read_config_32(mport, RIO_MNT_RSP_CSR(priv->portNdx), &rsp_csr);
	udelay(50);

	/* Issue Input-status command */
	__rio_local_write_config_32(mport, RIO_MNT_REQ_CSR(priv->portNdx), RIO_MNT_REQ_CMD_IS);

	/* Wait for reply */
	checkcount = 3;
	while (checkcount--) {
		udelay(50);
		__rio_local_read_config_32(mport, RIO_MNT_RSP_CSR(priv->portNdx), &rsp_csr);

		if (rsp_csr & RIO_PORT_N_MNT_RSP_RVAL) {
			*rsp = rsp_csr;
			return 0;
		}
	}
	return -EIO;
}

static int clr_sync_err(struct rio_mport *mport)
{
	struct rio_priv *priv = mport->priv;
	u32 linkstate, sts_csr, escsr;
	u32 far_ackid, far_linkstat, near_ackid;

	if (get_input_status(mport, &linkstate)) {
		dev_warn(priv->dev, "Input-status response timeout\n");
		return 0;
	}
	dev_dbg(priv->dev, "Input-status response=0x%08x\n", linkstate);
	far_ackid = (linkstate & RIO_PORT_N_MNT_RSP_ASTAT) >> 5;
	far_linkstat = linkstate & RIO_PORT_N_MNT_RSP_LSTAT;
	__rio_local_read_config_32(mport, RIO_ACK_STS_CSR(priv->portNdx), &sts_csr);
	near_ackid = (sts_csr & RIO_ACK_STS_IA) >> 24;
	dev_dbg(priv->dev,
		"far_ackID=0x%02x far_linkstat=0x%02x near_ackID=0x%02x\n",
		far_ackid, far_linkstat, near_ackid);

	if ((far_ackid != ((sts_csr & RIO_ACK_STS_OUTA) >> 8)) ||
	    (far_ackid != (sts_csr & RIO_ACK_STS_OBA))) {
		/* Align near outstanding/outbound ackIDs with
		 * far inbound.
		 * Hrm... In ACP we can only reset if there are no outstanding
		 * unacknowledge packets.
		 * Should be cleande up after reset though, it may work.
		 */
		__rio_local_write_config_32(mport, RIO_ACK_STS_CSR(priv->portNdx),
			(near_ackid << 24) | (far_ackid << 8) | far_ackid);
	}
	/* Debug: Show current state */
	__rio_local_read_config_32(mport, RIO_ACK_STS_CSR(priv->portNdx), &sts_csr);

	dev_dbg(priv->dev, "RIO_ACK_STS_CSR(%d) %8.8x, IA %x, OUTA %x, OBA %x\n",
		priv->portNdx,
		sts_csr, (sts_csr & RIO_ACK_STS_IA) >> 24,
		(sts_csr & RIO_ACK_STS_OUTA) >> 8,
		(sts_csr) & RIO_ACK_STS_OBA);

	__rio_local_read_config_32(mport, RIO_ESCSR(priv->portNdx), &escsr);
	return escsr & (RIO_ESCSR_OES | RIO_ESCSR_IES) ? -EFAULT : 0;
}

static int rio_port_stopped(struct rio_mport *mport)
{
	u32 escsr;

	__rio_local_read_config_32(mport, RIO_ESCSR(priv->portNdx), &escsr);
	return escsr & RIO_ESCSR_PU;
}
static int rio_port_started(struct rio_mport *mport)
{
	u32 escsr;

	__rio_local_read_config_32(mport, RIO_ESCSR(priv->portNdx), &escsr);
	return escsr & RIO_ESCSR_PO;
}

static void rio_lp_extract(struct rio_mport *mport)
{
	u32 ccsr;

	/* Set port lockout */
	__rio_local_read_config_32(mport, RIO_CCSR(priv->portNdx), &ccsr);
	ccsr |= RIO_PORT_N_CTL_LOCKOUT;
	__rio_local_write_config_32(mport, RIO_CCSR(priv->portNdx), ccsr);

	/* Drain output/input buffers:
	 * Is there a way of doing this other than
	 * having to reset the PIO?
	 * PIO reset will reset PIO registers as well
	 * so remove static win setup and restore again
	 */
	axxia_rio_static_win_release(mport);
	__rio_rab_pio_disable(mport);

	/* Force input state to normal */
	ccsr |= RIO_PORT_N_CTL_PORT_DIS;
	__rio_local_write_config_32(mport, RIO_CCSR(priv->portNdx), ccsr);

	/* Clear in and outbound ACK IDs */
	__rio_local_write_config_32(mport, RIO_ACK_STS_CSR(priv->portNdx), 0);

	/* Enable input port and leave drain mode */
	ccsr &= ~RIO_PORT_N_CTL_PORT_DIS;
	__rio_local_write_config_32(mport, RIO_CCSR(priv->portNdx), ccsr);
	__rio_rab_pio_enable(mport);
	ccsr &= ~RIO_PORT_N_CTL_LOCKOUT;
	__rio_local_write_config_32(mport, RIO_CCSR(priv->portNdx), ccsr);
	axxia_rio_start_port(mport);
}

static void rio_mp_extract(struct rio_mport *mport)
{
	/* Close down PIO - is this wise??? */
	axxia_rio_static_win_release(mport);
	__rio_rab_pio_disable(mport);
	__srds_phy_disable(mport);
}

static void rio_mp_activate(struct rio_mport *mport)
{
	__srds_phy_enable(mport);
	__rio_rab_pio_enable(mport);
	axxia_rio_start_port(mport);
}

static void rio_mp_insert(struct rio_mport *mport)
{
	u32 ccsr;

	/* Set port lockout */
	__rio_local_read_config_32(mport, RIO_CCSR(priv->portNdx), &ccsr);
	ccsr |= RIO_PORT_N_CTL_LOCKOUT;
	__rio_local_write_config_32(mport, RIO_CCSR(priv->portNdx), ccsr);
	/* sync ACK IDs */
	clr_sync_err(mport);
	/* Clear port lockout */
	ccsr &= ~RIO_PORT_N_CTL_LOCKOUT;
	__rio_local_write_config_32(mport, RIO_CCSR(priv->portNdx), ccsr);
}

static void acp_rio_hotswap_work(struct work_struct *work)
{
	struct acp_rio_work *hotswap_work =
			 container_of(work, struct acp_rio_work, work);
	struct rio_mport *mport = hotswap_work->mport;
	struct rio_priv *priv = mport->priv;
	struct completion *cmp = hotswap_work->cmp;
	u32 flags = hotswap_work->flags;


	axxia_api_lock();

	if (RIO_EXTRACT_LP(flags)) {
		if (rio_port_started(mport)) {
			dev_info(priv->dev,
				 "Link partner is active - Hotswap extract "
				 "aborted\n");
			goto out;
		}
		dev_dbg(priv->dev,
			 "Handle hotswap link partner extract\n");
		rio_lp_extract(mport);
	}
	if (RIO_EXTRACT_MP(flags)) {
		if (atomic_read(&priv->api_user) > 0) {
			dev_warn(priv->dev,
				 "Will not stop master port when %d\n"
				 "RIO resources are still in use\n",
				 atomic_read(&priv->api_user));
			BUG();
		}
		dev_dbg(priv->dev,
			 "Handle hotswap mport extract\n");
		axxia_rio_port_irq_disable(mport);
		rio_mp_extract(mport);
	}
	if (RIO_INSERT_LP(flags)) {
		if (rio_port_stopped(mport)) {
			dev_info(priv->dev,
				 "Link partner is not active - Hotswap "
				 "insert aborted\n");
			goto out;
		}
		dev_dbg(priv->dev,
			 "Handle hotswap link partner insert\n");
		axxia_rio_set_mport_disc_mode(mport);
		axxia_rio_port_get_state(mport, 0);
	}
	if (RIO_INSERT_MP(flags)) {
		rio_mp_activate(mport);
		if (rio_port_stopped(mport)) {
			dev_info(priv->dev,
				 "Link partner is not active - Hotswap "
				"insert aborted\n");
			goto out;
		}
		dev_dbg(priv->dev,
			 "Handle hotswap mport insert\n");
		rio_mp_insert(mport);
		axxia_rio_set_mport_disc_mode(mport);
		axxia_rio_port_irq_enable(mport);
	}
out:
	dev_dbg(priv->dev,
		 "Port work done\n");
	complete(cmp);
	kfree(hotswap_work);
	axxia_api_unlock();
}

int axxia_rio_hotswap(struct rio_mport *mport, u8 flags)
{
	struct rio_priv *priv = mport->priv;
	struct completion cmp;
	unsigned long tmo = msecs_to_jiffies(6000);
	struct acp_rio_work *hotswap_work =
		 kzalloc(sizeof(*hotswap_work), GFP_KERNEL);
	int rc = 0;

	if (!hotswap_work)
		return -ENOMEM;

	if ((flags >> 4) || (!flags))
		return -EINVAL;

	hotswap_work->mport = mport;
	INIT_WORK(&hotswap_work->work, acp_rio_hotswap_work);
	init_completion(&cmp);
	hotswap_work->cmp = &cmp;
	hotswap_work->flags = flags;

	schedule_work(&hotswap_work->work);

	/* Wait for completion */
	tmo = wait_for_completion_timeout(&cmp, tmo);
	if (!tmo) {
		dev_warn(priv->dev, "%s TimeOut\n", __func__);
		return -ETIME;
	}
	if ((flags & RIO_HOT_SWAP_INSERT) && rio_port_stopped(mport))
		rc = -EFAULT;
	else if ((flags & RIO_HOT_SWAP_EXTRACT) && !rio_port_stopped(mport))
		rc = -EFAULT;
	if (rc)
		dev_info(priv->dev, "(%s): Exit %s (%d)\n",
			 __func__, (rc ? "NOK" : "OK"), rc);

	return rc;
}

#endif /* defined(CONFIG_RAPIDIO_HOTPLUG) */
