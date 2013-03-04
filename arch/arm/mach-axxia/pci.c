/*
 * PCI / PCI-X / PCI-Express support for ARM A15 Cortex parts
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/bootmem.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>

#include <linux/io.h>

#include <asm/sizes.h>
#include <asm/mach/pci.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm-generic/errno-base.h>

#include <linux/msi.h>

#define AXXIA55xx_NUM_MSI_IRQS 256

#undef PRINT_CONFIG_ACCESSES
/*#define PRINT_CONFIG_ACCESSES*/

static u32 last_mpage;
static u32 last_port;

#define U64_TO_U32_LOW(val)	((u32)((val) & 0x00000000ffffffffULL))
#define U64_TO_U32_HIGH(val)	((u32)((val) >> 32))

#define ACPX1_PCIE_MPAGE_UPPER(n) (0x1010 + (n * 8))
#define ACPX1_PCIE_MPAGE_LOWER(n) (0x1014 + (n * 8))

struct axxia_pciex_port {
	unsigned int	index;
	u8 root_bus_nr;
	bool link_up;
	void __iomem	*cfg_addr;
	void __iomem	*cfg_data;
	u64 pci_addr;
	int endpoint;
	struct device_node	*node;
	struct resource	utl_regs;
	struct resource	cfg_space;
	struct resource	outbound, inbound;
	dma_addr_t msi_phys;
};

static struct axxia_pciex_port *axxia_pciex_ports;
static unsigned int axxia_pciex_port_count = 3;

static void axxia_probe_pciex_bridge(struct device_node *np);

static void __init
fixup_axxia_pci_bridge(struct pci_dev *dev)
{
	/* if we aren't a PCIe don't bother */
	if (!pci_find_capability(dev, PCI_CAP_ID_EXP))
		return ;

	/*
	 * Set the class appropriately for a bridge device
	 */
	printk(KERN_INFO
	       "PCI: Setting PCI Class to PCI_CLASS_BRIDGE_HOST for %04x:%04x\n",
	       dev->vendor, dev->device);
	dev->class = PCI_CLASS_BRIDGE_HOST << 8;
	/*
	 * Make the bridge transparent
	 */
	dev->transparent = 1;

	return ;
}

DECLARE_PCI_FIXUP_HEADER(0x1000, 0x5101, fixup_axxia_pci_bridge);
DECLARE_PCI_FIXUP_HEADER(0x1000, 0x5108, fixup_axxia_pci_bridge);
DECLARE_PCI_FIXUP_HEADER(0x1000, 0x5120, fixup_axxia_pci_bridge);

/* Convert to Bus# to PCIe port# */
static struct axxia_pciex_port *bus_to_port(struct pci_bus *bus)
{
	return axxia_pciex_ports + pci_domain_nr(bus);
}

/* Validate the Bus#/Device#/Function# */
static int axxia_pciex_validate_bdf(struct pci_bus *bus, unsigned int devfn)
{
	struct axxia_pciex_port *port;

	port = bus_to_port(bus);

	/* Endpoint can not generate upstream(remote) config cycles */
	if (port->endpoint)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (((!((PCI_FUNC(devfn) == 0) && (PCI_SLOT(devfn) == 0)))
		&& (bus->number == port->root_bus_nr))
		|| (!(PCI_SLOT(devfn) == 0)
		&& (bus->number == port->root_bus_nr+1))) {
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	return 0;
}

/* Get the configuration access base address */
static void __iomem *axxia_pciex_get_config_base(struct axxia_pciex_port *port,
			struct pci_bus *bus, unsigned int devfn)
{
	unsigned mpage;
	u32 addr;
	int dev, fn;
	int cfg_type;
	int relbus;

	if (bus->number == port->root_bus_nr) {
		return port->cfg_addr;
	} else {
		relbus = bus->number - (port->root_bus_nr + 1);
		dev = ((PCI_SLOT(devfn) & 0xf8) >> 3);
		fn  = PCI_FUNC(devfn) & 0x7;

		if (dev > 31)
			return NULL;

		/* Primary bus */
		if (relbus && (bus->number != port->root_bus_nr))
			cfg_type = 1;
		else
			cfg_type = 0;

		/* build the mpage register */
		mpage = (bus->number << 11) | (dev << 6) | (cfg_type << 5);
		mpage |= 0x10;   /* enable MPAGE for configuration access */
		mpage |= (fn << 19);

		if ((mpage != last_mpage) || (port->index != last_port)) {
			addr = ((u32)port->cfg_addr)
					+ ACPX1_PCIE_MPAGE_UPPER(7);
			writel(0x0, (u32 *) addr);
			addr = ((u32)port->cfg_addr)
					+ ACPX1_PCIE_MPAGE_LOWER(7);
			writel(mpage, (u32 *) addr);
			/* printk("pcie_get_base: %02x:%02x:%02x setting
				mpage = 0x%08x in addr = 0x%08x\n", bus->number,
				dev, fn, mpage, addr);*/
			last_mpage = mpage;
			last_port = port->index;
		}
		return port->cfg_data;
	}
}

/* Read the config space */
static int
arm_pciex_axxia_read_config(struct pci_bus *bus, unsigned int devfn,
		int offset, int len, u32 *val)
{
	struct axxia_pciex_port *port = bus_to_port(bus);
	void __iomem *addr;
	u32 bus_addr;
	u32 val32;
	int bo = offset & 0x3;
	int rc = PCIBIOS_SUCCESSFUL;
	u32 bus_addr1;

	if (axxia_pciex_validate_bdf(bus, devfn) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = axxia_pciex_get_config_base(port, bus, devfn);

	if (!addr) {
		*val = 0;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	/*
	 * addressing is different for local config access vs.
	 * access through the mapped config space.
	 */
	if (bus->number == 0) {
		int wo = offset & 0xfffffffc;
		bus_addr = (u32) addr + wo;
		bus_addr1 = bus_addr;
	} else {
		/*
		 * mapped config space only supports 32-bit access
		 *
		 *  AXI address [3:0] is not used at all.
		 *  AXI address[9:4] becomes register number.
		 *  AXI address[13:10] becomes Ext. register number
		 *  AXI address[17:14] becomes 1st DWBE for configuration
		 *  read only.
		 *  AXI address[29:27] is used to select one of 8 Mpage
		 *  registers.
		 */
		bus_addr = (u32) addr + (offset << 2);
		bus_addr1 = bus_addr;

		switch (len) {
		case 1:
			bus_addr |=  ((1 << bo)) << 14;
			break;
		case 2:
			bus_addr |=  ((3 << bo)) << 14;
			break;
		default:
			bus_addr |=  (0xf) << 14;
			break;
		}
	}
	/*
	 * do the read
	 */
	val32 = readl((u32 *)bus_addr);

	switch (len) {
	case 1:
		*val = (val32 >> (bo * 8)) & 0xff;
		break;
	case 2:
		*val = (val32 >> (bo * 8)) & 0xffff;
		break;
	default:
		*val = val32;
		break;
	}

#ifdef PRINT_CONFIG_ACCESSES
	printk(KERN_INFO
		"acp_read_config for PEI%d: %3d  fn=0x%04x o=0x%04x l=%d a=0x%08x v=0x%08x, dev=%d\n",
			port->index, bus->number, devfn, offset, len,
			bus_addr, *val, PCI_SLOT(devfn));
#endif
	return rc;
}

/* Write the config space */
static int arm_pciex_axxia_write_config(struct pci_bus *bus, unsigned int devfn,
			int offset, int len, u32 val)
{
	struct axxia_pciex_port *port = bus_to_port(bus);
	void __iomem *addr;
	u32 bus_addr;
	u32 val32;

	if (axxia_pciex_validate_bdf(bus, devfn) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr = axxia_pciex_get_config_base(port, bus, devfn);

	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * addressing is different for local config access vs.
	 * access through the mapped config space. We need to
	 * translate the offset for mapped config access
	 */
	if (bus->number == 0) {
		/* the local ACP RC only supports 32-bit dword config access,
		 * so if this is a byte or 16-bit word access we need to
		 * perform a read-modify write
		 */
		if (len == 4) {
			bus_addr = (u32) addr + offset;
		} else {
			int bs = ((offset & 0x3) * 8);

			bus_addr = (u32) addr + (offset & 0xfffffffc);
			val32 = readl((u32 *)bus_addr);

			if (len == 2) {
				val32 = (val32 & ~(0xffff << bs))
					| ((val & 0xffff) << bs);
			} else {
				val32 = (val32 & ~(0xff << bs))
					| ((val & 0xff) << bs);
			}

			val = val32;
			len = 4;
		}
	} else {
		bus_addr = (u32) addr + (offset << 2) + (offset & 0x3);
	}

#ifdef PRINT_CONFIG_ACCESSES
	printk(KERN_INFO
		"acp_write_config: bus=%3d devfn=0x%04x offset=0x%04x len=%d addr=0x%08x val=0x%08x\n",
		bus->number, devfn, offset, len, bus_addr, val);
#endif

	switch (len) {
	case 1:
		writeb(val, (u8 *)(bus_addr));
		break;
	case 2:
		writew(val, (u16 *)(bus_addr));
		break;
	default:
		writel(val, (u32 *)(bus_addr));
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops axxia_pciex_pci_ops = {
	.read  = arm_pciex_axxia_read_config,
	.write = arm_pciex_axxia_write_config,
};

/* ACP PCIe ISR to handle Legacy Interrupts */
static irqreturn_t
acp_pcie_isr(int irq, void *arg)
{
	u32 intr_status;
	u32 msg_fifo_stat;
	u32 msg_fifo_info;
	u8  externalPciIntr = 0;
	struct axxia_pciex_port *port = (struct axxia_pciex_port *)arg;
	void __iomem *mbase = (void __iomem *)port->cfg_addr;

	/* read the PEI interrupt status register */
	intr_status = readl(mbase+0x10c0);

	/* check if this is a PCIe message from an external device */
	if (intr_status & 0x00000010) {
		externalPciIntr = 1;

		msg_fifo_stat = readl(mbase+0x10b4);

		/* loop until the message fifo is empty */
		while ((msg_fifo_stat & 0x01) == 0)  {
			u8 bus, dev, fn;
			u8 msg_type;
			msg_fifo_info = readl(mbase+0x10b0);

			bus = (msg_fifo_info >> 16) & 0xff;
			dev = (msg_fifo_info >> 11) & 0x1f;
			fn  = (msg_fifo_info >>  8) & 0x07;
			msg_type = msg_fifo_info & 0xff;

			/* print out the BDF and message type.
			 * We ignore the common message types.
			 */
			switch (msg_type) {
			case 0x20:  /*    assert_INTa */
				printk(KERN_INFO "PEI%d --> INTa asserted\n",
					port->index);
				break;
			case 0x21:  /*    assert_INTb */
				printk(KERN_INFO "PEI%d --> INTb asserted\n",
					port->index);
				break;
			case 0x22:  /*    assert_INTc */
				printk(KERN_INFO "PEI%d --> INTc asserted\n",
					port->index);
				break;
			case 0x23:  /*    assert_INTd */
				printk(KERN_INFO "PEI%d --> INTd asserted\n",
					port->index);
				break;
			case 0x24:  /* de-assert_INTa */
				printk(KERN_INFO "PEI%d --> INTa de-asserted\n",
					port->index);
				break;
			case 0x25:  /* de-assert_INTb */
				printk(KERN_INFO "PEI%d --> INTb de-asserted\n",
					port->index);
				break;
			case 0x26:  /* de-assert_INTc */
				printk(KERN_INFO "PEI%d --> INTc de-asserted\n",
					port->index);
				break;
			case 0x27:  /* de-assert_INTd */
				printk(KERN_INFO "PEI%d --> INTd de-asserted\n",
					port->index);
				break;
			default:
				printk(KERN_INFO "BDF %02x:%02x.%x sent msgtype 0x%02x\n",
						bus, dev, fn, msg_type);
				break;
			}

			/* re-read fifo status */
			msg_fifo_stat = readl(mbase+0x10b4);
		}
	} else {
		/* Ignore the common interrupts, still need to figure out what
		 * they all mean.*/
		if (intr_status & 0xf3ffffab) {
			u32 t2a_err_stat;
			u32 t2a_other_err_stat;
			u32 int_enb;
			u32 linkStatus;
			u32 offset;

			printk(KERN_ERR
				"ACP_PCIE_ISR: got PEI%d error interrupt 0x%08x\n",
				intr_status, port->index);

			linkStatus = readl(mbase+0x117c);
			printk(KERN_ERR "link_status (0x117c) = 0x%08x\n",
				linkStatus);

			if (intr_status & 0x00020000) {
				t2a_err_stat = readl(mbase+0x1170);
				printk(KERN_ERR "t2a_fn_indp_err_stat = 0x%08x\n",
					t2a_err_stat);

				int_enb = readl(mbase+0x10c4);
				int_enb &= 0xfffdffff;
				writel(int_enb, mbase + 0x10c4);
			}

			if (intr_status & 0x00040000) {
				t2a_other_err_stat = readl(mbase+0x1174);
				printk(KERN_ERR "t2a_fn_indp_other_err_stat = 0x%08x\n",
					t2a_other_err_stat);
				int_enb = readl(mbase+0x10c4);
				int_enb &= 0xfffbffff;
				writel(int_enb, mbase + 0x10c4);
			}

			if (intr_status & 0x00000800) {
				printk(KERN_INFO "pci_config = 0x%08x\n",
					readl(mbase + 0x1000));
				printk(KERN_INFO "pci_status = 0x%08x\n",
					readl(mbase + 0x1004));
				int_enb = readl(mbase + 0x10c4);
				int_enb &= 0xfffff7ff;
				writel(int_enb, mbase + 0x10c4);
			}
			/*
			 * dump all the potentially interesting PEI registers
			 */
			for (offset = 0x114c; offset <= 0x1180; offset += 4) {
				printk(KERN_INFO "  0x%04x : 0x%08x\n", offset,
					readl(mbase + offset));
			}
		}
	}

	/*
	 *  We clear all the interrupts in the PEI status, even though
	 *  interrupts from external devices have not yet been handled.
	 *  That should be okay, since the PCI IRQ in the MPIC won't be
	 *  re-enabled until all external handlers have been called.
	 */
	writel(intr_status, mbase + 0x10c0);
	return externalPciIntr ? IRQ_NONE : IRQ_HANDLED;
}

/* ACP PCIe ISR to handle MSI Interrupts */
static irqreturn_t
acp_pcie_MSI_isr(int irq, void *arg) {
	u32 intr_status;
	u8  msiIntr = 0, bit = 0;
	struct axxia_pciex_port *port = (struct axxia_pciex_port *)arg;
	void __iomem *mbase = (void __iomem *)port->cfg_addr;
	u32 statusReg, statusVal;

	/* read the PEI MSI Level2 interrupt status register */
	intr_status = readl(mbase+0x1230);

	/* check if this is a PCIe MSI interrupt */
	if (intr_status & 0x0000ffff) {
		msiIntr = 1;
		for (bit = 0; bit < 16; bit++) {
			if (intr_status & (0x1 << bit)) {
				printk(KERN_INFO "PEI%d --> MSI%d-%d interrupt asserted\n",
					port->index, bit*16, ((bit+1)*16)-1);
				/* MSI Level 1 interrupt status */
				statusReg = (0x123c + (0xc * bit));
				statusVal = readl(mbase+statusReg);
				printk(KERN_INFO "MSI status Reg 0x%x val = 0x%x\n",
					statusReg, statusVal);
				/* clear statusReg */
				writel(statusVal, mbase+statusReg);
			}
		}
	}
	/*
	 *  We clear all the interrupts in the PEI status, even though
	 *  interrupts from MSI devices have not yet been handled.
	 */
	writel(intr_status, mbase + 0x1230);
	return msiIntr ? IRQ_NONE : IRQ_HANDLED;
}


/* PCIe setup function */
int axxia_pcie_setup(int portno, struct pci_sys_data *sys)
{
	struct axxia_pciex_port *port;
	u32 pci_config, pci_status, link_state;
	int i, num_pages, err;
	u32 mpage_lower, pciah, pcial;
	u64 size, bar0_size;
	void __iomem *cfg_addr = NULL;
	void __iomem *cfg_data = NULL;
	void __iomem *tpage_base;
	int mappedIrq;
	u32 inbound_size;

	port = &axxia_pciex_ports[sys->domain];
	printk(KERN_INFO "cfg_space start = 0x%012llx, end = 0x%012llx\n",
		port->cfg_space.start, port->cfg_space.end);
	printk(KERN_INFO "utl_regs start = 0x%012llx, end = 0x%012llx\n",
		port->utl_regs.start, port->utl_regs.end);
	port->root_bus_nr = sys->busnr;

	/* 1M external config */
	cfg_data = ioremap(port->cfg_space.start, 0x100000);
	if (cfg_data == NULL) {
		printk(KERN_ERR "%s: Can't map external config space !",
			port->node->full_name);
		goto fail;
	}
	port->cfg_data = cfg_data;

	/* IORESOURCE_MEM */
	port->outbound.name = "PCIe MEM";
	port->outbound.start = port->cfg_space.start - 0x38000000;
	/* allocate 256 M -- 2 MPAGEs worth */
	port->outbound.end = port->outbound.start + 0x10000000 - 1;
	port->outbound.flags = IORESOURCE_MEM;

	if (request_resource(&iomem_resource, &port->outbound))
		panic("Request PCIe Memory resource failed for port %d\n",
		      portno);

	pci_add_resource_offset(&sys->resources, &port->outbound,
			sys->mem_offset);
	printk(KERN_INFO "port res start = 0x%012llx, end = 0x%012llx\n",
		port->outbound.start, port->outbound.end);
	printk(KERN_INFO "port system mem_offset start = 0x%012llx\n",
		sys->mem_offset);

	/* 4K  internal config */
	cfg_addr = ioremap(port->utl_regs.start, 0x10000);
	if (cfg_addr == NULL) {
		printk(KERN_ERR "%s: Can't map external config space !",
			port->node->full_name);
		goto fail;
	}
	port->cfg_addr = cfg_addr;
	printk(KERN_INFO "cfg_addr for port %d = 0x%8x\n", port->index,
		(unsigned int)port->cfg_addr);
	pci_config = readl(cfg_addr);
#ifdef PRINT_CONFIG_ACCESSES
	printk(KERN_INFO "pci_vendor = 0x%08x\n", pci_config);
#endif

	/* hookup an interrupt handler */
	printk(KERN_INFO "PCIE%d mapping interrupt\n", port->index);
	mappedIrq = irq_of_parse_and_map(port->node, 0);

	if (sys->domain == 0) {
		/* IRQ# 68 for PEI0 */
		mappedIrq = 100;
	} else if (sys->domain == 2) {
		/* IRQ# 70 for PEI2 */
		mappedIrq = 102;
	}
	printk(KERN_INFO "Requesting irq#%d for PEI%d Legacy INTs\n",
			mappedIrq, port->index);
	err = request_irq(mappedIrq, acp_pcie_isr,
			  IRQF_SHARED, "acp_pcie", port);
	if (err) {
		printk(KERN_ERR "request_irq failed!!!! for IRQ# %d err = %d\n",
			mappedIrq, err);
		goto fail;
	}

	/* MSI INTS for PEI0 */
	if (sys->domain == 0) {
		/* IRQ# 73-88 for PEI0 MSI INTs */
		for (mappedIrq = 73; mappedIrq <= 88; mappedIrq++) {
			printk(KERN_INFO
				"Requesting irq#%d for PEI0 MSI INTs\n",
				mappedIrq+32);
			err = request_irq(mappedIrq+32, acp_pcie_MSI_isr,
				IRQF_SHARED, "acp_pcie_MSI", port);
			if (err) {
				printk(KERN_ERR
					"request_irq failed!!!! for IRQ# %d err = %d\n",
					mappedIrq+32, err);
				goto fail;
			}
		}
	}

	/* Setup as root complex */
	pci_config = readl(cfg_addr + 0x1000);
#ifdef PRINT_CONFIG_ACCESSES
	printk("pci_config = 0x%08x\n", pci_config);
#endif

	pci_status = readl(cfg_addr + 0x1004);
	link_state = (pci_status & 0x3f00) >> 8;
	printk(KERN_INFO
		"PCIE%d status = 0x%08x : PCI link state = 0x%x\n",
		port->index, pci_status, link_state);

	/* make sure the ACP device is configured as PCI Root Complex */
	if ((pci_status & 0x18) != 0x18) {
		printk(KERN_INFO
			"ACP device is not PCI Root Complex! status = 0x%08x\n",
			pci_status);
		goto fail;
	}

	/* make sure the link is up */
	if (link_state != 0xb) {
		/* reset */
		printk("PCI link in bad state - resetting\n");
		pci_config |= 1;
		writel(pci_config, cfg_addr + 0x1000);
		msleep(1000);
		pci_status = readl(cfg_addr + 0x1004);
		link_state = (pci_status & 0x3f00) >> 8;
		printk(KERN_INFO "PCI link state now = 0x%x\n", link_state);
		if (link_state != 0xb) {
			printk(KERN_INFO "PCI link still in bad state - giving up!\n");
			goto fail;
		}
	}

	/* ACP X1 setup MPAGE registers */
	/*
	 * MPAGE7 is dedicated to config access, so we only
	 *  have 7 128MB pages available for memory i/o.
	 *  Calculate how many pages we need
	 */
	size = 7 * 1024*128*1024;
	num_pages = ((size - 1) >> 27) + 1;
	pciah = U64_TO_U32_HIGH(port->pci_addr);
	pcial = U64_TO_U32_LOW(port->pci_addr);
	for (i = 0; i < num_pages; i++) {
		mpage_lower = (pcial & 0xf8000000);
		mpage_lower |= 0x0;
		writel(pciah, cfg_addr + ACPX1_PCIE_MPAGE_UPPER(i));
		writel(mpage_lower, cfg_addr + ACPX1_PCIE_MPAGE_LOWER(i));
		pcial += 0x08000000;
	}

	inbound_size = (u32) (port->inbound.end - port->inbound.start + 1);

	/* configures the RC Memory Space Configuration Register */
	writel(inbound_size, cfg_addr + 0x11f4);

	/* write all 1s to BAR0 register */
	writel(0xffffffff, cfg_addr + 0x10);

	/* read back BAR0 */
	bar0_size = readl(cfg_addr + 0x10);
	if ((bar0_size & inbound_size) != inbound_size)
		printk(KERN_INFO "Writing/Reading BAR0 reg failed\n");

	/* set the BASE0 address to start of PCIe base */
	writel(port->inbound.start, cfg_addr + 0x10);

	/* setup TPAGE registers for inbound mapping */
	/* We set the MSB of each TPAGE to select 128-bit AXI access.
	 * For the address field we simply program an incrementing value
	 * to map consecutive pages
	 */
	tpage_base = cfg_addr + 0x1050;
	for (i = 0; i < 8; i++) {
		writel((0x80000000 + i), tpage_base);
		tpage_base += 4;
	}


	return 1;
fail:
	if (cfg_data)
		iounmap(cfg_data);
	if (cfg_addr)
		iounmap(cfg_addr);
	return 0;
}

/* Just a dummy arch_setup_msi_irq() function */
int arch_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *desc)
{
	return 0;
}


void arch_teardown_msi_irq(unsigned int irq)
{
}



/* Scan PCIe bus */
static struct pci_bus __init *
axxia_pcie_scan_bus(int nr, struct pci_sys_data *sys)
{
	struct pci_bus *bus;
	struct axxia_pciex_port *port;

	/* get the pointer to port struct from domain# */
	port = &axxia_pciex_ports[sys->domain];

	if (nr < axxia_pciex_port_count) {
		bus = pci_scan_root_bus(NULL, sys->busnr,
			&axxia_pciex_pci_ops, sys, &sys->resources);
	} else {
		bus = NULL;
		BUG();
	}

	return bus;
}

/* MSI setup */
static void __devinit axxia_pcie_msi_enable(struct pci_dev *dev)
{
	u32 pci_higher = 0, msi_lower = 0;
	u16 flag_val;
	int pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
	struct axxia_pciex_port *port;
	void *msi_virt;
	int device, fn, bus_num;
	static u32 pci_lower, msi_count;

	port = bus_to_port(dev->bus);

	if (pos <= 0) {
		dev_err(&dev->dev, "skipping MSI enable\n");
		printk(KERN_INFO "skipping MSI enable\n");
		return;
	}

	/* MSI applicable only to PEI0 */
	if (port->index == 0) {
		/* MSI is generated when EP writes to address mapped */
		if (msi_lower == 0) {
			/* MSI support only in PEI0 */
			/* allocate 1K to manage 256 MSIs
			 * one for each endpoint */

			if ((!dma_set_coherent_mask(&dev->dev,
				DMA_BIT_MASK(64))) ||
				(!dma_set_coherent_mask(&dev->dev,
				DMA_BIT_MASK(32)))) {
				msi_virt = dma_alloc_coherent(&dev->dev,
					1024, &(port->msi_phys), GFP_KERNEL);
			} else {
				printk(KERN_INFO
					"No suitable DMA available. MSI cannot be supported\n");
				return;
			}
			msi_lower = (u32)port->msi_phys;
			/* Please note we have 1:1 mapping for inbound */
			pci_lower = port->inbound.start + msi_lower;
			printk("PEI%d dma_alloc_coherent msiAddr = 0x%x\n",
				port->index, msi_lower);
			writel(msi_lower>>10, port->cfg_addr + 0x1190);
		}
		device = ((PCI_SLOT(dev->devfn) & 0xf8) >> 3);
		fn  = PCI_FUNC(dev->devfn) & 0x7;
		bus_num = dev->bus->number;

		printk(KERN_INFO
			"PEI%d axxia_pcie_msi_enable Found MSI%d, msi_lower = 0x%x for bus_num = %d, dev = %d, fn = %d\n",
			port->index, msi_count, msi_lower,
			bus_num, device, fn);
		pci_write_config_dword(dev, pos + PCI_MSI_ADDRESS_LO,
			pci_lower);
		pci_write_config_dword(dev, pos + PCI_MSI_ADDRESS_HI,
			pci_higher);

		/* Enable MSI */
		dev->msi_enabled = 1;

		pci_read_config_word(dev, pos + PCI_MSI_FLAGS, &flag_val);
		flag_val = flag_val | 0x1;
		pci_write_config_word(dev, pos + PCI_MSI_FLAGS,
			(flag_val | 0x1));

		/* for next EP MSI */
		pci_lower = pci_lower + 0x4;
		msi_count++;
	}
	return;
}
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, axxia_pcie_msi_enable);

/* Port definition struct
 * Please note: PEI core#1 is not used in AXM5500 */
static struct hw_pci __refdata axxia_pcie_hw[] = {
	[0] = {
		.nr_controllers = 1,
		.domain = 0,
		.swizzle = pci_std_swizzle,
		.setup = axxia_pcie_setup,
		.scan = axxia_pcie_scan_bus
	},
	[1] = {
		.nr_controllers = 0,
		.domain = 1
	},
	[2] = {
		.nr_controllers = 1,
		.domain = 2,
		.swizzle = pci_std_swizzle,
		.setup = axxia_pcie_setup,
		.scan = axxia_pcie_scan_bus
	}
};

/* Initialize PCIe */
void __init axxia_pcie_init(void)
{
	struct device_node *np;
	/* allocate memory */
	axxia_pciex_ports = kzalloc(axxia_pciex_port_count
		* sizeof(struct axxia_pciex_port), GFP_KERNEL);

	if (!axxia_pciex_ports) {
		printk(KERN_WARNING "PCIE: failed to allocate ports array\n");
		return;
	}
	for_each_compatible_node(np, NULL, "lsi,plb-pciex")
		axxia_probe_pciex_bridge(np);

	pci_common_init(&axxia_pcie_hw[0]);
	pci_common_init(&axxia_pcie_hw[1]);
	pci_common_init(&axxia_pcie_hw[2]);

	return;
}

static void axxia_probe_pciex_bridge(struct device_node *np)
{
	struct axxia_pciex_port *port;
	u32 pval;
	int portno;
	const char *val;
	const u32 *field;
	int rlen;
	int pna = of_n_addr_cells(np);
	int num = pna + 5;
	u64 size;
	u64 pci_addr;

	/* Get the port number from the device-tree */
	if (!of_property_read_u32(np, "port", &pval)) {
		portno = pval;
		if (portno == 1) {
			/* only PCIe0 and PCIe2 are supported in AXM5500 */
			return;
		}
		printk(KERN_INFO "PCIE Port %d found\n", portno);
	} else {
		printk(KERN_ERR "PCIE: Can't find port number for %s\n",
		       np->full_name);
		return;
	}

	if (portno > axxia_pciex_port_count) {
		printk(KERN_ERR "PCIE: port number out of range for %s\n",
		       np->full_name);
		return;
	}

	port = &axxia_pciex_ports[portno];
	port->index = portno;

	port->node = of_node_get(np);

	/* Check if device_type property is set to "pci" or "pci-endpoint".
	 * Resulting from this setup this PCIe port will be configured
	 * as root-complex or as endpoint.
	 */
	val = of_get_property(port->node, "device_type", NULL);
	if (!strcmp(val, "pci-endpoint")) {
		port->endpoint = 1;
	} else if (!strcmp(val, "pci")) {
		port->endpoint = 0;
	} else {
		printk(KERN_ERR "PCIE%d: missing or incorrect device_type for %s\n",
		       portno, np->full_name);
		return;
	}
	printk(KERN_ERR "PCIE%d: endpoint = %d\n", portno, port->endpoint);

	/* Fetch config space registers address */
	if (of_address_to_resource(np, 0, &port->cfg_space)) {
		printk(KERN_ERR "%s: Can't get PCI-E config space !",
		       np->full_name);
		return;
	}
	printk(KERN_INFO
		"cfg_space start = 0x%012llx, end = 0x%012llx\n",
		port->cfg_space.start, port->cfg_space.end);

	/* Fetch host bridge internal registers address */
	if (of_address_to_resource(np, 1, &port->utl_regs)) {
		printk(KERN_ERR "%s: Can't get UTL register base !",
		       np->full_name);
		return;
	}
	printk(KERN_INFO
		"utl_regs start = 0x%012llx, end = 0x%012llx\n",
		port->utl_regs.start, port->utl_regs.end);

	field = of_get_property(np, "ranges", &rlen);
	if (field == NULL)
		printk("not able to get ranges\n");

	pci_addr = of_read_number(field + 1, 2);
	printk(KERN_INFO "pci_addr = 0x%012llx\n", pci_addr);
	port->pci_addr = pci_addr;

	printk(KERN_INFO "%s PCIE%d config base = 0x%012llx\n",
		np->full_name, port->index, port->utl_regs.start);

	/* Default 256 MB */
	port->inbound.start = 0;
	size = 0x10000000;
	port->inbound.end = size - 1;
	port->inbound.flags = IORESOURCE_MEM | IORESOURCE_PREFETCH;

	/* Get dma-ranges property */
	field = of_get_property(np, "dma-ranges", &rlen);
	if (field == NULL) {
		printk(KERN_INFO "not able to get dma-ranges\n");
		/* use default */
		return;
	}

	/* Walk it */
	while ((rlen -= num * 4) >= 0) {
		u64 pci_addr = of_read_number(field + 1, 2);
		u64 cpu_addr = of_read_number(field + 3, 1);
		size = of_read_number(field + pna + 3, 2);
		field += num;

		/* We currently only support memory at 0, and pci_addr
		 * within 32 bits space and 1:1 mapping
		 */
		if (cpu_addr != 0 || pci_addr > 0xffffffff) {
			printk(KERN_WARNING
			       "%s: Ignored unsupported dma range 0x%016llx...0x%016llx -> 0x%016llx\n",
				np->full_name, pci_addr,
				pci_addr + size - 1, cpu_addr);
			continue;
		}
		/* Use that */
		port->inbound.start = pci_addr;
		/* Beware of 32 bits resources */
		if (sizeof(resource_size_t) == sizeof(u32) &&
			(pci_addr + size) > 0x100000000ull) {
			port->inbound.end = 0xffffffff;
		} else
			port->inbound.end = port->inbound.start + size - 1;
		break;
	}
	printk(KERN_INFO "inbound start = 0x%016llx, end = 0x%016llx\n",\
		port->inbound.start, port->inbound.end);
}
