/*
 *  Copyright (C) 2013 LSI Corporation
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/* #define EXTRA_DEBUG */

#include "ai2c_bus.h"
#include "ai2c_plat.h"
#include "ai2c_dev_clock_ext.h"
#include "regs/ai2c_i2c_regs.h"
#include "regs/ai2c_axi_timer_regs.h"


/*****************************************************************************
* Functions: Initialization & Base Configuration			     *
*****************************************************************************/

/*
 * Title:       ai2c_bus_init_axm5500
 * Description: This function will initialize the timer(s) and other features
 *              used by I2C.  This is a one time initialization and will
 *              called by the generic initialization sequence.
 * Inputs:
 *   @param[in] dev Device handle
 *   @param[in] ndx Region of the I2C module of interest
 * Returns: completion status
 */
static int ai2c_bus_init_axm5500(struct ai2c_priv *priv,
		u32 inRegionId)
{
	/* To check if I2C timer is initialized or not */
	int	   ai2cStatus = AI2C_ST_SUCCESS;

	u32       clockMhz;
	u32       timerLoad;

	AI2C_LOG(AI2C_MSG_ENTRY,
		"bus_init_axm5500: enter; rid=%08x\n", inRegionId);

	/* Verify the device */
	{
		struct ai2c_region_io *r =
			ai2c_region_lookup(priv, inRegionId);
		u32 v0 = 0,
		v1 = 0,
		fail = 0;

		AI2C_CALL(ai2c_dev_read32(priv, inRegionId,
			AI2C_REG_I2C_X7_UDID_W7, &v0));
			AI2C_CALL(ai2c_dev_read32(priv, inRegionId,
			AI2C_REG_I2C_X7_UDID_W4, &v1));

		if (v0 != AI2C_REG_I2C_X7_UDID_W7_DEFAULT)
			fail |= 1;

		if (v1 != AI2C_REG_I2C_X7_UDID_W4_DEFAULT)
			fail |= 2;


		AI2C_LOG(AI2C_MSG_DEBUG,
			"bus_init_axm5500: 0x%04x.0x%04x.0x%x = 0x%08x "
			"(0x%08x)\n",
			AI2C_NODE_ID(inRegionId), AI2C_TARGET_ID(inRegionId),
			AI2C_REG_I2C_X7_UDID_W7,
			v0, AI2C_REG_I2C_X7_UDID_W7_DEFAULT);
		AI2C_LOG(AI2C_MSG_DEBUG,
			"bus_init_axm5500: 0x%04x.0x%04x.0x%x = 0x%08x "
			"(0x%08x)\n",
			AI2C_NODE_ID(inRegionId), AI2C_TARGET_ID(inRegionId),
			AI2C_REG_I2C_X7_UDID_W4,
			v1, AI2C_REG_I2C_X7_UDID_W4_DEFAULT);

		AI2C_LOG(AI2C_MSG_INFO,
			"%s bus %s : %s\n",
			priv->busCfg->chipName,
			priv->pages[r->pageId].busName,
			(fail) ? "Invalid" : "Valid/Installed");

		if (fail)
			AI2C_CALL(-ENXIO);

	}

	/* Enable Master Mode */
	AI2C_CALL(ai2c_dev_write32(priv, inRegionId,
		AI2C_REG_I2C_X7_GLOBAL_CONTROL, 0x00000001));
	/* Enable Master Mode Interrupts */
	AI2C_CALL(ai2c_dev_write32(priv, inRegionId,
		AI2C_REG_I2C_X7_INTERRUPT_ENABLE, 0x00000001));
	/* Enable Individual Master Mode Interrupts */
		AI2C_CALL(ai2c_dev_write32(priv, inRegionId,
	AI2C_REG_I2C_X7_MST_INT_ENABLE, 0x0000FFF0));
	/* Enable Individual Slave Mode Interrupts */
	AI2C_CALL(ai2c_dev_write32(priv, inRegionId,
		AI2C_REG_I2C_X7_SLV_INT_ENABLE, 0x00000000));

	/* Configure Clock setup registers */
	/*   Assume PCLK=50MHZ, I2C=Fast mode (400KHz) */
	if (ai2c_dev_clock_mhz(priv, &clockMhz) != AI2C_ST_SUCCESS)
		clockMhz = 400;

	/* SCL High Time: 1.4 us - 1 PCLK */
	timerLoad = (clockMhz / 4) - 1;
	AI2C_CALL(ai2c_dev_write32(priv, inRegionId,
		AI2C_REG_I2C_X7_SCL_HIGH_PERIOD, timerLoad));
	/* SCL Low Time: 1.1 us - 1 PCLK */
	timerLoad = (clockMhz / 5) - 1;
	AI2C_CALL(ai2c_dev_write32(priv, inRegionId,
		AI2C_REG_I2C_X7_SCL_LOW_PERIOD, 0x00000000));
	/* SDA Setup Time: SDA setup=600ns with margin */
	AI2C_CALL(ai2c_dev_write32(priv, inRegionId,
		AI2C_REG_I2C_X7_SDA_SETUP_TIME, 0x0000001E));
	/* SDA Hold Time: SDA setup=500ns with margin */
	AI2C_CALL(ai2c_dev_write32(priv, inRegionId,
		AI2C_REG_I2C_X7_SDA_HOLD_TIME, 0x00000019));
	/* Filter > 50ns spikes */
	AI2C_CALL(ai2c_dev_write32(priv, inRegionId,
		AI2C_REG_I2C_X7_SPIKE_FLTR_LEN, 0x00000003));

	/* Configure Time-Out Registers */
	/* Every 10.24ux = 512 x 20ns */
	AI2C_CALL(ai2c_dev_write32(priv, inRegionId,
		AI2C_REG_I2C_X7_TIMER_CLOCK_DIV, 0x00000003));
	/* Desired Time-Out = 25ms */
	AI2C_CALL(ai2c_dev_write32(priv, inRegionId,
		AI2C_REG_I2C_X7_WAIT_TIMER_CONTROL, 0x00008989));

ai2c_return:

	AI2C_LOG(AI2C_MSG_EXIT,
		"bus_init_axm5500: exit (%d)\n", ai2cStatus);

	if (ai2cStatus)
		return -ENXIO;
	else
		return 0;
}

/*****************************************************************************
* Functions:  Hardware Layer Communication                                   *
*****************************************************************************/

/*
 * Title: ai2c_bus_block_read8_axm5500
 * Description:
 *   Read num bytes from the device and store it in buffer.
 *
 * Inputs:
 *   priv: handle of device to access
 *   regionId: Handle of space corresponding to the bus in use
 *   adap:   Current I2C adapter
 *   msg:    Current I2C message
 *   stop:   Add stop after this message if true
 *
 * Returns: completion status
 */
static int ai2c_bus_block_read8_axm5500_internal(
	struct ai2c_priv	*priv,
	u32    regionId,
	struct i2c_msg     *msg,
	u8       *buffer,
	u32       count,
	int                 stop,
	u32      *actCount)
{
	int  ai2cStatus = 0;
	u32  numInFifo, deadman, i, endit,
		lTenBitMode = (msg->flags & I2C_M_TEN);

/* Anything available yet? */
	ai2cStatus = ai2c_dev_read32(priv,
		regionId,
		AI2C_REG_I2C_X7_MST_RX_FIFO,
		&numInFifo);
	if (ai2cStatus)
		goto ai2c_return;
	if ((numInFifo & 0xFF))
		/* Didn't 'fail', but nothing really moved either */
		goto ai2c_return;

	/* # of bytes to move */
	ai2cStatus = ai2c_dev_write32(priv,
		regionId,
		AI2C_REG_I2C_X7_MST_RX_XFER,
		count);
	if (ai2cStatus)
		goto ai2c_return;

	/* target slave address */
	if (lTenBitMode) {
		u32 a1 = (0x1e << 3) |
			(((msg->addr >> 8) & 0x3) << 1) |
			0x1;
		u32 a2 = (msg->addr & 0xff);
		ai2cStatus = ai2c_dev_write32(priv,
			regionId,
			AI2C_REG_I2C_X7_MST_ADDR_1,
			a1);
		/* insert! read */
		if (ai2cStatus)
			goto ai2c_return;
		ai2cStatus = ai2c_dev_write32(priv,
			regionId,
			AI2C_REG_I2C_X7_MST_ADDR_2,
			a2);
		if (ai2cStatus)
			goto ai2c_return;

		AI2C_LOG(AI2C_MSG_DEBUG,
			"read8_i(%d):T: r %x/%x ma1 0x%08x ma2 0x%08x\n",
			__LINE__, regionId, msg->addr, a1, a2);

	} else {

		u32 a1 = ((msg->addr & 0x7f) << 1) | 0x1;
		u32 a2 = 0x00000000;
		ai2cStatus = ai2c_dev_write32(priv,
			regionId,
			AI2C_REG_I2C_X7_MST_ADDR_1,
			a1);
		/* insert! read */
		if (ai2cStatus)
			goto ai2c_return;
		ai2cStatus = ai2c_dev_write32(priv,
			regionId,
			AI2C_REG_I2C_X7_MST_ADDR_2,
			a2);
		if (ai2cStatus)
			goto ai2c_return;

		AI2C_LOG(AI2C_MSG_DEBUG,
			"read8_i(%d): r %x/%x ma1 0x%08x ma2 0x%08x\n",
			__LINE__, regionId, msg->addr, a1, a2);
	}

	/* And send it on its way (automatic or manual transfer command) */
	{
		u32   r = 0x8 |
			((stop) ? 0x1 : 0x0);
			ai2cStatus = ai2c_dev_write32(priv,
			regionId,
			AI2C_REG_I2C_X7_MST_COMMAND,
			r);
		if (ai2cStatus)
			goto ai2c_return;
	}

	/* Wait for the data to arrive */
	for (endit = FALSE, deadman = 0;
		!endit && (deadman < AI2C_I2C_CHECK_COUNT); deadman++) {
		u32   reg = 0;

		ai2cStatus = ai2c_dev_read32(priv,
			regionId,
			AI2C_REG_I2C_X7_MST_INT_STATUS,
			&reg);
		if (ai2cStatus)
			goto ai2c_return;
		if (stop && ((reg >> 10) & 0x1))
			endit = TRUE;
		if (!stop && ((reg >> 11) & 0x1))
			endit = TRUE;
		if (reg & 0x00000078) {	/* al || nd || na || ts */
			ai2cStatus = -EBADE;
			goto ai2c_return;
		}
	}
	if (!endit) {
		ai2cStatus = -ETIMEDOUT;
		goto ai2c_return;
	}

	/* Finally, acquire the queued data */
	ai2cStatus = ai2c_dev_read32(priv,
		regionId,
		AI2C_REG_I2C_X7_MST_RX_BYTES_XFRD,
		&numInFifo);
	if (ai2cStatus)
		goto ai2c_return;
	(*actCount) = 0;
	count = MIN((numInFifo & 0xff), count);
	for (i = 0; i < count; i++) {
		u32 v;

		ai2cStatus = ai2c_dev_read32(priv,
			regionId,
			AI2C_REG_I2C_X7_MST_DATA,
			&v);
		if (ai2cStatus)
			goto ai2c_return;

		buffer[i] = (v & 0xff);

		(*actCount)++;
	}

	AI2C_LOG(AI2C_MSG_DEBUG,
		"read8_i: l %d [%x,%x,%x,%x,%x,%x,%x,%x]\n",
		(*actCount),
		buffer[0], buffer[1], buffer[2], buffer[3],
		buffer[4], buffer[5], buffer[6], buffer[7]);

ai2c_return:

	AI2C_LOG(AI2C_MSG_EXIT,
		"read8_i: exit; st=%d; rcvd=%d\n", ai2cStatus, (*actCount));

	return ai2cStatus;
}

int ai2c_bus_block_read8_axm5500(
	struct ai2c_priv	       *priv,
	u32    regionId,
	struct i2c_adapter *adap,
	struct i2c_msg     *msg,
	int                 stop)
{
	int                 ai2cStatus = 0;
	u32       count = 0;
	u32       actCount = 0;
	u32       thisLim = ai2c_axm5500_cfg.maxXfrSize;
	u32       bytesRead = 0;
	u8       *buffer = NULL;

	count = msg->len;
	buffer = msg->buf;

	AI2C_LOG(AI2C_MSG_ENTRY,
		"read8: entry, rid=%x, l=%d\n", regionId, count);

	for (bytesRead = 0; count > bytesRead;) {
		u32 thisXfr = MIN(count - bytesRead, thisLim);
		int tempStop = stop;

		if ((bytesRead + thisXfr) < count)
			tempStop = 0;

		ai2cStatus = ai2c_bus_block_read8_axm5500_internal(priv,
			regionId,
			msg, buffer, thisXfr,
			tempStop, &actCount);
		if (ai2cStatus)
			goto ai2c_return;

		AI2C_LOG(AI2C_MSG_DEBUG,
			"read8: l %d [%x,%x,%x,%x,%x,%x,%x,%x]\n",
			thisXfr,
			buffer[0], buffer[1], buffer[2], buffer[3],
			buffer[4], buffer[5], buffer[6], buffer[7]);

		buffer += actCount;
		bytesRead += actCount;
	}

ai2c_return:

	AI2C_LOG(AI2C_MSG_EXIT,
		"read8: exit; st=%d l=%d\n", ai2cStatus, actCount);

	return ai2cStatus;
}


/*
 * Title:       ai2c_bus_block_write8_axm5500
 * Description: This function will read count bytes from the buffer
 *              and will store at the approparite location in the device.
 * Inputs:
 *   priv: handle of device to access
 *   regionId: Handle of space corresponding to the bus in use
 *   adap:   Current I2C adapter
 *   msg:    Current I2C message
 *   stop:   Add stop after this message if true
 * Returns: completion status
 */
static int ai2c_bus_block_write8_axm5500_internal(
	struct ai2c_priv        *priv,
	u32    regionId,
	struct i2c_msg     *msg,
	u8       *buffer,
	u32       count,
	int                 stop,
	u32      *actCount)
{
	int                 ai2cStatus = 0;
	u32       numInFifo,
	endit,
	deadman,
	i,
	lTenBitMode = (msg->flags & I2C_M_TEN);

	AI2C_LOG(AI2C_MSG_ENTRY,
		"write8_i: enter; rid=%x len=%d stop=%d\n",
		regionId, count, stop);

	/* Is a previous transfer still in progress? */
	ai2cStatus = ai2c_dev_read32(priv, regionId,
			AI2C_REG_I2C_X7_MST_TX_FIFO, &numInFifo);
	if (ai2cStatus)
		goto ai2c_return;

	if ((numInFifo & 0xFF))
		/* Didn't 'fail', but nothing really moved either */
		goto ai2c_return;

	/* # of bytes to move */
	ai2cStatus = ai2c_dev_write32(priv, regionId,
			AI2C_REG_I2C_X7_MST_TX_XFER, count);
	if (ai2cStatus)
		goto ai2c_return;

	/* target slave address */
	if (lTenBitMode) {
		u32 a1 = (0x1e << 3) |
			(((msg->addr >> 8) & 0x3) << 1) |
			0x0;
		u32 a2 = (msg->addr & 0xff);
		ai2cStatus = ai2c_dev_write32(priv,
		regionId,
		AI2C_REG_I2C_X7_MST_ADDR_1,
		a1);
		/* insert! write */
		if (ai2cStatus)
			goto ai2c_return;

		ai2cStatus = ai2c_dev_write32(priv,
			regionId,
			AI2C_REG_I2C_X7_MST_ADDR_2,
			a2);
		if (ai2cStatus)
			goto ai2c_return;

		AI2C_LOG(AI2C_MSG_DEBUG,
			"write8_i(%d):T: r %x/%x ma1 0x%08x ma2 0x%08x\n",
			__LINE__, regionId, AI2C_TARGET_ID(regionId),
			a1 | 0x0, a2);

	} else {

		u32 a1 = ((msg->addr & 0x7f) << 1) | 0x0;
		u32 a2 = 0x00000000;
		ai2cStatus = ai2c_dev_write32(priv,
			regionId,
			AI2C_REG_I2C_X7_MST_ADDR_1,
			a1);
		/* insert! write */
		if (ai2cStatus)
			goto ai2c_return;

		ai2cStatus = ai2c_dev_write32(priv,
			regionId,
			AI2C_REG_I2C_X7_MST_ADDR_2,
			a2);
		if (ai2cStatus)
			goto ai2c_return;

		AI2C_LOG(AI2C_MSG_DEBUG,
			"write8_i(%d): r %x/%x ma1 0x%08x ma2 0x%08x\n",
			__LINE__, regionId, AI2C_TARGET_ID(regionId),
			a1 | 0x0, a2);
	}

	/* Queue up the data */
	for (i = 0; i < count; i++) {
		u32 v = buffer[i];

		ai2cStatus = ai2c_dev_write32(priv,
			regionId,
			AI2C_REG_I2C_X7_MST_DATA,
			v);
		if (ai2cStatus)
			goto ai2c_return;
	}

	AI2C_LOG(AI2C_MSG_DEBUG,
		"write8_i(%d): c %d [%x %x %x %x %x %x %x %x]\n",
		__LINE__, count,
		buffer[0], buffer[1], buffer[2], buffer[3],
		buffer[4], buffer[5], buffer[6], buffer[7]);

	/* And send it on its way (automatic or manual transfer command) */
	{
		u32   r = 0x8 |
		((stop) ? 0x1 : 0x0);
		ai2cStatus = ai2c_dev_write32(priv,
		regionId,
		AI2C_REG_I2C_X7_MST_COMMAND,
		r);
		if (ai2cStatus)
			goto ai2c_return;
	}

	/* Wait for the data to arrive */
	for (endit = FALSE, deadman = 0;
		!endit && (deadman < AI2C_I2C_CHECK_COUNT); deadman++) {

		u32   reg = 0;

		ai2cStatus = ai2c_dev_read32(priv,
			regionId,
			AI2C_REG_I2C_X7_MST_INT_STATUS,
			&reg);
		if (ai2cStatus)
			goto ai2c_return;
		if (stop && ((reg >> 10) & 0x1))  /* auto xfer; ss */
			endit = TRUE;
		if (!stop && ((reg >> 11) & 0x1)) /* man xfer; sns */
			endit = TRUE;
		if (reg & 0x00000078) {		  /* al || nd || na || ts */
			ai2cStatus = -EBADE;
			goto ai2c_return;
		}
	}

	if (!endit) {
		ai2cStatus = -ETIMEDOUT;
		goto ai2c_return;
	}

	/* Finally, determine how many bytes were sent successfully */
	ai2cStatus = ai2c_dev_read32(priv,
		regionId,
		AI2C_REG_I2C_X7_MST_TX_BYTES_XFRD,
		&numInFifo);
	if (ai2cStatus)
		goto ai2c_return;

	(*actCount) = (numInFifo & 0xFF);

ai2c_return:

	AI2C_LOG(AI2C_MSG_EXIT,
		"write8_i: exit; st=%d; num of bytes sent is %d (%d)\n",
		ai2cStatus, numInFifo, (*actCount));

	return ai2cStatus;
}


static int ai2c_bus_block_write8_axm5500(
	struct ai2c_priv         *priv,
	u32     regionId,
	struct i2c_adapter  *adap,
	struct i2c_msg      *msg,
	int                  stop)
{
	int			ai2cStatus = 0;
	u32       countUsed = 0;
	u32       count;
	u32       countRem;
	u32       thisLim = ai2c_axm5500_cfg.maxXfrSize,
				lTenBitMode = (msg->flags & I2C_M_TEN);

	AI2C_LOG(AI2C_MSG_ENTRY, "write8: entry, rid=%x\n", regionId);

	if (msg->buf == NULL) {
		AI2C_LOG(AI2C_MSG_DEBUG, "write8: Data buffer is NULL\n");
		return -EFAULT;
	}

	if ((msg->addr & 0x1f) == 0x1e)
		lTenBitMode = TRUE;
	else
		lTenBitMode = FALSE;

	countRem = count = msg->len;

	for (countUsed = 0; (count > countUsed);) {

		u32   thisWid = (countRem > thisLim)
					? thisLim : countRem;
		u32   thisXfr = thisWid;
		u32   actWid = 0;
		u8    inOutTxd[AI2C_I2CPROT_MAX_BUF_SIZE];
		u32   k;
		int   tempStop = stop;

		if (thisWid < countRem)
			tempStop = 0;

		for (k = 0; k < thisWid; k++)
			inOutTxd[k] = msg->buf[countUsed+k];

		ai2cStatus =
			ai2c_bus_block_write8_axm5500_internal(priv,
				regionId, msg,
				&inOutTxd[0], thisXfr,
				tempStop, &actWid);
		if (ai2cStatus)
			goto ai2c_return;

		AI2C_LOG(AI2C_MSG_DEBUG,
			"write8: tbm %d l %d aw %d c %d cu %d"
			"[%x,%x,%x,%x,%x,%x,%x,%x]\n",
			lTenBitMode, thisXfr, actWid, count, countUsed,
			inOutTxd[0], inOutTxd[1], inOutTxd[2], inOutTxd[3],
			 inOutTxd[4], inOutTxd[5], inOutTxd[6], inOutTxd[7]);

		countUsed += actWid;
		countRem -= actWid;
	}

ai2c_return:

	AI2C_LOG(AI2C_MSG_EXIT,
		"write8: exit; st=%d\n", ai2cStatus);
	return ai2cStatus;
}


/*****************************************************************************
* More Exported State							*
*****************************************************************************/

struct ai2c_i2c_access ai2c_axm5500_cfg = {
	0,
	/* maxXfrSize */ AI2C_I2CPROT_MAX_XFR_SIZE,
	/* deviceLen */ 0,
	/* i.e. unbounded */
	ai2c_bus_init_axm5500,
	ai2c_bus_block_write8_axm5500,
	ai2c_bus_block_read8_axm5500,
	NULL,
};
