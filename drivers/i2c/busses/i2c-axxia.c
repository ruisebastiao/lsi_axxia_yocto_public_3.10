/*
 * drivers/i2c/busses/i2c-axxia.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_i2c.h>
#include <linux/module.h>

#define SCL_WAIT_TIMEOUT_NS 25000000
#define I2C_TIMEOUT         (msecs_to_jiffies(1000))
#define TX_FIFO_SIZE        8
#define RX_FIFO_SIZE        8

struct i2c_regs {
	__le32 global_control;
	__le32 interrupt_status;
	__le32 interrupt_enable;
	__le32 wait_timer_control;
	__le32 ibml_timeout;
	__le32 ibml_low_mext;
	__le32 ibml_low_sext;
	__le32 timer_clock_div;
	__le32 i2c_bus_monitor;
	__le32 soft_reset;
	__le32 mst_command;
#define CMD_MANUAL 0x08
#define CMD_AUTO   0x09
	__le32 mst_rx_xfer;
	__le32 mst_tx_xfer;
	__le32 mst_addr_1;
	__le32 mst_addr_2;
#define CHIP_READ(_chip)  (((_chip) << 1) | 1)
#define CHIP_WRITE(_chip) (((_chip) << 1) | 0)
	__le32 mst_data;
	__le32 mst_tx_fifo;
	__le32 mst_rx_fifo;
	__le32 mst_int_enable;
	__le32 mst_int_status;
#define MST_STATUS_RFL (1<<13) /* RX FIFO serivce */
#define MST_STATUS_TFL (1<<12) /* TX FIFO service */
#define MST_STATUS_SNS (1<<11) /* Manual mode done */
#define MST_STATUS_SS  (1<<10) /* Automatic mode done */
#define MST_STATUS_SCC (1<<9)  /* Stop complete */
#define MST_STATUS_IP  (1<<8)  /* Invalid parameter */
#define MST_STATUS_TSS (1<<7)  /* Timeout */
#define MST_STATUS_AL  (1<<6)  /* Arbitration lost */
#define MST_STATUS_NAK (MST_STATUS_NA | MST_STATUS_ND)
#define MST_STATUS_ND  (1<<5)  /* NAK on data phase */
#define MST_STATUS_NA  (1<<4)  /* NAK on address phase */
#define MST_STATUS_ERR (MST_STATUS_NAK | \
			MST_STATUS_AL  | \
			MST_STATUS_IP  | \
			MST_STATUS_TSS)
	__le32 mst_tx_bytes_xfrd;
	__le32 mst_rx_bytes_xfrd;
	__le32 slv_addr_dec_ctl;
	__le32 slv_addr_1;
	__le32 slv_addr_2;
	__le32 slv_rx_ctl;
	__le32 slv_data;
	__le32 slv_rx_fifo;
	__le32 slv_int_enable;
	__le32 slv_int_status;
	__le32 slv_read_dummy;
	__le32 reserved;
	__le32 scl_high_period;
	__le32 scl_low_period;
	__le32 spike_fltr_len;
	__le32 sda_setup_time;
	__le32 sda_hold_time;
	__le32 smb_alert;
	__le32 udid_w7;
	__le32 udid_w6;
	__le32 udid_w5;
	__le32 udid_w4;
	__le32 udid_w3;
	__le32 udid_w2;
	__le32 udid_w1;
	__le32 udid_w0;
	__le32 arppec_cfg_stat;
	__le32 slv_arp_int_enable;
	__le32 slv_arp_int_status;
	__le32 mst_arp_int_enable;
	__le32 mst_arp_int_status;
};


/**
 * I2C device context
 */
struct axxia_i2c_dev {
	/** device reference */
	struct device *dev;
	/** core i2c abstraction */
	struct i2c_adapter adapter;
	/* clock reference for i2c input clock */
	struct clk *i2c_clk;
	/* ioremapped registers cookie */
	void __iomem *base;
	/* pointer to register struct */
	struct i2c_regs __iomem *regs;
	/* irq number */
	int irq;
	/* xfer completion object */
	struct completion msg_complete;
	/* pointer to current message data */
	u8 *msg_buf;
	/* size of unsent data in the message buffer */
	size_t msg_buf_remaining;
	/* identifies read transfers */
	int msg_read;
	/* error code for completed message */
	int msg_err;
	/* current i2c bus clock rate */
	u32 bus_clk_rate;
};

static void
i2c_int_disable(struct axxia_i2c_dev *idev, u32 mask)
{
	u32 int_mask = readl(&idev->regs->mst_int_enable);
	int_mask &= ~mask;
	writel(int_mask, &idev->regs->mst_int_enable);
}

static void
i2c_int_enable(struct axxia_i2c_dev *idev, u32 mask)
{
	u32 int_mask = readl(&idev->regs->mst_int_enable);
	int_mask |= mask;
	writel(int_mask, &idev->regs->mst_int_enable);
}

/**
 * Convert nanoseconds to clock cycles for the given clock frequency.
 */
static u32
ns_to_clk(u64 ns, u32 clk_mhz)
{
	return div_u64(ns*clk_mhz, 1000);
}

static int
axxia_i2c_init(struct axxia_i2c_dev *idev)
{
	u32 divisor = clk_get_rate(idev->i2c_clk) / idev->bus_clk_rate;
	u32 clk_mhz = clk_get_rate(idev->i2c_clk) / 1000000;
	u32 t_setup;
	u32 tmo_clk;
	u32 prescale;

	dev_dbg(idev->dev, "rate=%uHz per_clk=%uMHz -> ratio=1:%u\n",
		idev->bus_clk_rate, clk_mhz, divisor);

	/* Enable Master Mode */
	writel(0x1, &idev->regs->global_control);

	/* SCL High Time */
	writel(divisor/2, &idev->regs->scl_high_period);
	/* SCL Low Time */
	writel(divisor/2, &idev->regs->scl_low_period);

	t_setup = (idev->bus_clk_rate <= 100000) ?
		ns_to_clk(250, clk_mhz) : /* Standard mode tSU:DAT = 250 ns */
		ns_to_clk(100, clk_mhz); /* Fast mode tSU:DAT = 100 ns */

	/* SDA Setup Time */
	writel(t_setup, &idev->regs->sda_setup_time);
	/* SDA Hold Time, 5ns */
	writel(ns_to_clk(5, clk_mhz), &idev->regs->sda_hold_time);
	/* Filter <50ns spikes */
	writel(ns_to_clk(50, clk_mhz), &idev->regs->spike_fltr_len);

	/* Configure Time-Out Registers */
	tmo_clk = ns_to_clk(SCL_WAIT_TIMEOUT_NS, clk_mhz);

	/*
	   Find the prescaler value that makes tmo_clk fit in 15-bits counter.
	 */
	for (prescale = 0; prescale < 15; ++prescale) {
		if (tmo_clk <= 0x7fff)
			break;
		tmo_clk >>= 1;
	}
	if (tmo_clk > 0x7fff)
		tmo_clk = 0x7fff;

	/* Prescale divider (log2) */
	writel(prescale, &idev->regs->timer_clock_div);
	/* Timeout in divided clocks */
	writel((1<<15) | tmo_clk, &idev->regs->wait_timer_control);

	/* Interrupt enable */
	writel(0x01, &idev->regs->interrupt_enable);

	dev_dbg(idev->dev, "SDA_SETUP:        %08x\n",
		readl(&idev->regs->sda_setup_time));
	dev_dbg(idev->dev, "SDA_HOLD:         %08x\n",
		readl(&idev->regs->sda_hold_time));
	dev_dbg(idev->dev, "SPIKE_FILTER_LEN: %08x\n",
		readl(&idev->regs->spike_fltr_len));
	dev_dbg(idev->dev, "TIMER_DIV:        %08x\n",
		readl(&idev->regs->timer_clock_div));
	dev_dbg(idev->dev, "WAIT_TIMER:       %08x\n",
		readl(&idev->regs->wait_timer_control));

	return 0;
}

static int
axxia_i2c_empty_rx_fifo(struct axxia_i2c_dev *idev)
{
	size_t rx_fifo_avail = readl(&idev->regs->mst_rx_fifo);
	int bytes_to_transfer = min(rx_fifo_avail, idev->msg_buf_remaining);

	idev->msg_buf_remaining -= bytes_to_transfer;

	while (0 < bytes_to_transfer--)
		*idev->msg_buf++ = readl(&idev->regs->mst_data);

	return 0;
}

static int
axxia_i2c_fill_tx_fifo(struct axxia_i2c_dev *idev)
{
	size_t tx_fifo_avail = TX_FIFO_SIZE - readl(&idev->regs->mst_tx_fifo);
	int bytes_to_transfer = min(tx_fifo_avail, idev->msg_buf_remaining);

	idev->msg_buf_remaining -= bytes_to_transfer;

	while (0 < bytes_to_transfer--)
		writel(*idev->msg_buf++, &idev->regs->mst_data);

	return 0;
}

#ifdef DEBUG
static char *
status_str(u32 status)
{
	static char buf[128];

	buf[0] = '\0';

	if (status & MST_STATUS_RFL)
		strcat(buf, "RFL ");
	if (status & MST_STATUS_TFL)
		strcat(buf, "TFL ");
	if (status & MST_STATUS_SNS)
		strcat(buf, "SNS ");
	if (status & MST_STATUS_SS)
		strcat(buf, "SS ");
	if (status & MST_STATUS_SCC)
		strcat(buf, "SCC ");
	if (status & MST_STATUS_TSS)
		strcat(buf, "TSS ");
	if (status & MST_STATUS_AL)
		strcat(buf, "AL ");
	if (status & MST_STATUS_ND)
		strcat(buf, "ND ");
	if (status & MST_STATUS_NA)
		strcat(buf, "NA ");
	return buf;
}
#endif

static irqreturn_t
axxia_i2c_isr(int irq, void *_dev)
{
	struct axxia_i2c_dev *idev = _dev;
	u32 status = readl(&idev->regs->mst_int_status);

	/* Clear interrupt */
	writel(0x01, &idev->regs->interrupt_status);

	if (status & MST_STATUS_ERR) {
		idev->msg_err = status & MST_STATUS_ERR;
		i2c_int_disable(idev, ~0);
		dev_err(idev->dev, "error %#x, rx=%u/%u tx=%u/%u\n",
			idev->msg_err,
			readl(&idev->regs->mst_rx_bytes_xfrd),
			readl(&idev->regs->mst_rx_xfer),
			readl(&idev->regs->mst_tx_bytes_xfrd),
			readl(&idev->regs->mst_tx_xfer));
		complete(&idev->msg_complete);
		return IRQ_HANDLED;
	}

	/* Transfer done? */
	if (status & (MST_STATUS_SNS | MST_STATUS_SS)) {
		if (idev->msg_read && idev->msg_buf_remaining > 0)
			axxia_i2c_empty_rx_fifo(idev);
		WARN_ON(idev->msg_buf_remaining > 0);
		i2c_int_disable(idev, ~0);
		complete(&idev->msg_complete);
	}

	/* RX FIFO needs service? */
	if (idev->msg_read && (status & MST_STATUS_RFL)) {
		WARN_ON(idev->msg_buf_remaining == 0);
		axxia_i2c_empty_rx_fifo(idev);
	}

	/* TX FIFO needs service? */
	if (!idev->msg_read && (status & MST_STATUS_TFL)) {
		if (idev->msg_buf_remaining)
			axxia_i2c_fill_tx_fifo(idev);
		else
			i2c_int_disable(idev, MST_STATUS_TFL);
	}

	return IRQ_HANDLED;
}

static int
axxia_i2c_xfer_msg(struct axxia_i2c_dev *idev, struct i2c_msg *msg, int stop)
{
	u32 int_mask;
	int ret;

	dev_dbg(idev->dev, "xfer_msg: chip=%#x, buffer=[%02x %02x %02x %02x], len=%d, stop=%d\n",
		msg->addr, msg->buf[0], msg->buf[1], msg->buf[2], msg->buf[3],
		msg->len, stop);

	if (msg->len == 0 || msg->len > 255)
		return -EINVAL;

	idev->msg_buf           = msg->buf;
	idev->msg_buf_remaining = msg->len;
	idev->msg_err           = 0;
	idev->msg_read          = (msg->flags & I2C_M_RD);
	INIT_COMPLETION(idev->msg_complete);

	if (msg->flags & I2C_M_RD) {
		/* TX 0 bytes */
		writel(0, &idev->regs->mst_tx_xfer);
		/* RX # bytes */
		writel(msg->len, &idev->regs->mst_rx_xfer);
		/* Chip address for write */
		writel(CHIP_READ(msg->addr & 0xfe), &idev->regs->mst_addr_1);
	} else {
		/* TX # bytes */
		writel(msg->len, &idev->regs->mst_tx_xfer);
		/* RX 0 bytes */
		writel(0, &idev->regs->mst_rx_xfer);
		/* Chip address for write */
		writel(CHIP_WRITE(msg->addr & 0xfe), &idev->regs->mst_addr_1);
	}
	writel(msg->addr >> 8, &idev->regs->mst_addr_2);

	if (!(msg->flags & I2C_M_RD))
		axxia_i2c_fill_tx_fifo(idev);

	int_mask = MST_STATUS_ERR;
	int_mask |= stop ? MST_STATUS_SS : MST_STATUS_SNS;
	if (msg->flags & I2C_M_RD)
		int_mask |= MST_STATUS_RFL;
	else if (idev->msg_buf_remaining)
		int_mask |= MST_STATUS_TFL;

	/* Start manual mode */
	writel(stop ? 0x9 : 0x8, &idev->regs->mst_command);

	i2c_int_enable(idev, int_mask);

	ret = wait_for_completion_timeout(&idev->msg_complete, I2C_TIMEOUT);

	i2c_int_disable(idev, int_mask);

	if (WARN_ON(ret == 0)) {
		dev_warn(idev->dev, "i2c transfer timed out\n");
		/* Reset i2c controller and re-initialize */
		writel(0x01, &idev->regs->soft_reset);
		while (readl(&idev->regs->soft_reset) & 1)
			cpu_relax();
		axxia_i2c_init(idev);
		return -ETIMEDOUT;
	}

	WARN_ON(readl(&idev->regs->mst_command) & 0x8);

	dev_dbg(idev->dev, "transfer complete: %d %d %#x\n",
		ret, completion_done(&idev->msg_complete), idev->msg_err);

	if (likely(idev->msg_err == 0))
		return 0;

	return -EIO;
}

static int
axxia_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct axxia_i2c_dev *idev = i2c_get_adapdata(adap);
	int i;
	int ret = 0;

	for (i = 0; ret == 0 && i < num; i++) {
		int stop = (i == num-1);
		ret = axxia_i2c_xfer_msg(idev, &msgs[i], stop);
	}

	return ret ?: i;
}

static u32
axxia_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_10BIT_ADDR | I2C_FUNC_SMBUS_EMUL;

}

static const struct i2c_algorithm axxia_i2c_algo = {
	.master_xfer	= axxia_i2c_xfer,
	.functionality	= axxia_i2c_func,
};

static int __devinit
axxia_i2c_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct axxia_i2c_dev *idev = NULL;
	struct clk *i2c_clk = NULL;
	void __iomem *base = NULL;
	u32 bus = pdev->id;
	int irq = 0;
	int ret = 0;

	base = of_iomap(np, 0);
	if (!base) {
		dev_err(&pdev->dev, "failed to iomap registers\n");
		ret = -ENOMEM;
		goto err_cleanup;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (irq == 0) {
		dev_err(&pdev->dev, "no irq property\n");
		ret = -EINVAL;
		goto err_cleanup;
	}

	i2c_clk = clk_get(&pdev->dev, "i2c");
	if (IS_ERR(i2c_clk)) {
		dev_err(&pdev->dev, "missing bus clock");
		ret = PTR_ERR(i2c_clk);
		goto err_cleanup;
	}

	idev = kzalloc(sizeof(struct axxia_i2c_dev), GFP_KERNEL);
	if (!idev) {
		ret = -ENOMEM;
		goto err_cleanup;
	}

	idev->base         = base;
	idev->regs         = (struct __iomem i2c_regs *) base;
	idev->i2c_clk      = i2c_clk;
	idev->dev          = &pdev->dev;
	init_completion(&idev->msg_complete);

	of_property_read_u32(np, "bus", &bus);

	of_property_read_u32(np, "clock-frequency", &idev->bus_clk_rate);

	if (idev->bus_clk_rate == 0)
		idev->bus_clk_rate = 100000; /* default clock rate */

	platform_set_drvdata(pdev, idev);

	ret = axxia_i2c_init(idev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize i2c controller");
		goto err_cleanup;
	}

	ret = request_irq(irq, axxia_i2c_isr, 0, pdev->name, idev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq %i\n", idev->irq);
		goto err_cleanup;
	}
	idev->irq = irq;

	clk_enable(idev->i2c_clk);

	i2c_set_adapdata(&idev->adapter, idev);
	idev->adapter.owner = THIS_MODULE;
	idev->adapter.class = I2C_CLASS_HWMON;
	snprintf(idev->adapter.name, sizeof(idev->adapter.name),
		 "Axxia I2C%u", bus);
	idev->adapter.algo = &axxia_i2c_algo;
	idev->adapter.dev.parent = &pdev->dev;
	idev->adapter.nr = bus;
	idev->adapter.dev.of_node = pdev->dev.of_node;

	ret = i2c_add_numbered_adapter(&idev->adapter);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add I2C adapter\n");
		goto err_cleanup;
	}

	of_i2c_register_devices(&idev->adapter);

	return 0;

err_cleanup:
	if (!IS_ERR_OR_NULL(i2c_clk))
		clk_put(i2c_clk);
	if (base)
		iounmap(base);
	if (idev && idev->irq)
		free_irq(irq, idev);
	kfree(idev);

	return ret;
}

static int __devexit
axxia_i2c_remove(struct platform_device *pdev)
{
	struct axxia_i2c_dev *idev = platform_get_drvdata(pdev);
	i2c_del_adapter(&idev->adapter);
	free_irq(idev->irq, idev);
	clk_put(idev->i2c_clk);
	iounmap(idev->base);
	kfree(idev);
	return 0;
}

#ifdef CONFIG_PM
static int axxia_i2c_suspend(struct platform_device *pdev, pm_message_t state)
{
	return -EOPNOTSUPP;
}

static int axxia_i2c_resume(struct platform_device *pdev)
{
	return -EOPNOTSUPP;
}
#else
#define axxia_i2c_suspend NULL
#define axxia_i2c_resume NULL
#endif

/* Match table for of_platform binding */
static const struct of_device_id axxia_i2c_of_match[] __devinitconst = {
	{ .compatible = "lsi,api2c", },
	{},
};
MODULE_DEVICE_TABLE(of, axxia_i2c_of_match);

static struct platform_driver axxia_i2c_driver = {
	.probe   = axxia_i2c_probe,
	.remove  = __devexit_p(axxia_i2c_remove),
	.suspend = axxia_i2c_suspend,
	.resume  = axxia_i2c_resume,
	.driver  = {
		.name  = "axxia-i2c",
		.owner = THIS_MODULE,
		.of_match_table = axxia_i2c_of_match,
	},
};

module_platform_driver(axxia_i2c_driver);

MODULE_DESCRIPTION("Axxia I2C Bus driver");
MODULE_AUTHOR("Anders Berg <anders.berg@lsi.com>");
MODULE_LICENSE("GPL v2");