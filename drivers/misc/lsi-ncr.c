/*
 *  Copyright (C) 2009 LSI Corporation
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

#include <linux/module.h>

#include <asm/io.h>

#include "lsi-ncr.h"

static void __iomem *nca_address = NULL;

#ifdef CONFIG_ARCH_AXXIA
#define NCA_PHYS_ADDRESS 0x002020100000ULL
#else
#define NCA_PHYS_ADDRESS 0x002000520000ULL
#endif

#define WFC_TIMEOUT (400000)

#define LOCK_DOMAIN 0

typedef union {
	unsigned long raw;
	struct {
#ifdef __BIG_ENDIAN
		unsigned long start_done              :1;
		unsigned long unused                  :6;
		unsigned long local_bit               :1;
		unsigned long status                  :2;
		unsigned long byte_swap_enable        :1;
		unsigned long cfg_cmpl_int_enable     :1;
		unsigned long cmd_type                :4;
		unsigned long dbs                     :16;
#else
		unsigned long dbs                     :16;
		unsigned long cmd_type                :4;
		unsigned long cfg_cmpl_int_enable     :1;
		unsigned long byte_swap_enable        :1;
		unsigned long status                  :2;
		unsigned long local_bit               :1;
		unsigned long unused                  :6;
		unsigned long start_done              :1;
#endif
	} __attribute__ ((packed)) bits;
} __attribute__ ((packed)) command_data_register_0_t;

typedef union {
	unsigned long raw;
	struct {
		unsigned long target_address          :32;
	} __attribute__ ((packed)) bits;
} __attribute__ ((packed)) command_data_register_1_t;

typedef union {
	unsigned long raw;
	struct {
#ifdef __BIG_ENDIAN
		unsigned long unused                  :16;
		unsigned long target_node_id          :8;
		unsigned long target_id_address_upper :8;
#else
		unsigned long target_id_address_upper :8;
		unsigned long target_node_id          :8;
		unsigned long unused                  :16;
#endif
	} __attribute__ ((packed)) bits;
} __attribute__ ((packed)) command_data_register_2_t;

#ifdef CONFIG_ARM

/*
  ----------------------------------------------------------------------
  ncr_register_read
*/

static __inline__ unsigned long
ncr_register_read(unsigned *address)
{
	unsigned long value;

	value = ioread32be(address);
	printk("%s: value=0x%x address=0x%p\n", __FUNCTION__, value, address);

	return value;
}

/*
  ----------------------------------------------------------------------
  ncr_register_write
*/

static __inline__ void
ncr_register_write(const unsigned value, unsigned *address)
{
	printk("%s: value=0x%x address=0x%p\n", __FUNCTION__, value, address);
	iowrite32be(value, address);

	return;
}

#else

/*
  ----------------------------------------------------------------------
  ncr_register_read
*/

static __inline__ unsigned long
ncr_register_read(unsigned *address)
{
	return in_be32((unsigned *)address);
}

/*
  ----------------------------------------------------------------------
  ncr_register_write
*/

static __inline__ void
ncr_register_write(const unsigned value, unsigned *address)
{
	out_be32(address, value);
}

#endif

/*
  ------------------------------------------------------------------------------
  ncr_lock
*/

static int
ncr_lock(int domain)
{
	unsigned long offset;
	unsigned long value;
	int loops = 10000;

	offset=(0xff80 + (domain * 4));

	do {
		value = ncr_register_read((unsigned *)(nca_address + offset));
	} while ((0 != value) && (0 < --loops));

	if (0 == loops)
		return -1;

	return 0;
}

/*
  ------------------------------------------------------------------------------
  ncr_unlock
*/

static void
ncr_unlock(int domain)
{
	unsigned long offset;

	offset=(0xff80 + (domain * 4));
	ncr_register_write(0, (unsigned *)(nca_address + offset));

	return;
}

/*
  ======================================================================
  ======================================================================
  Public Interface
  ======================================================================
  ======================================================================
*/

/*
  ----------------------------------------------------------------------
  ncr_read
*/

int
ncr_read(unsigned long region, unsigned long address, int number,
	void *buffer)
{
	command_data_register_0_t cdr0;
	command_data_register_1_t cdr1;
	command_data_register_2_t cdr2;
	int wfc_timeout = WFC_TIMEOUT;

	if (NULL == nca_address)
		nca_address = ioremap(NCA_PHYS_ADDRESS, 0x20000);

	if (0 != ncr_lock(LOCK_DOMAIN))
		return -1;

	/*
	  Set up the read command.
	*/

	cdr2.raw = 0;
	cdr2.bits.target_node_id = NCP_NODE_ID(region);
	cdr2.bits.target_id_address_upper = NCP_TARGET_ID(region);
	ncr_register_write(cdr2.raw, (unsigned *) (nca_address + 0xf8));

	cdr1.raw = 0;
	cdr1.bits.target_address = (address >> 2);
	ncr_register_write(cdr1.raw, (unsigned *) (nca_address + 0xf4));

	cdr0.raw = 0;
	cdr0.bits.start_done = 1;

	if (0xff == cdr2.bits.target_id_address_upper)
		cdr0.bits.local_bit = 1;

	cdr0.bits.cmd_type = 4;
	/* TODO: Verify number... */
	cdr0.bits.dbs = (number - 1);
	ncr_register_write(cdr0.raw, (unsigned *) (nca_address + 0xf0));
	mb();

	/*
	  Wait for completion.
	*/

	do {
		--wfc_timeout;
	} while ((0x80000000UL ==
		  ncr_register_read((unsigned *)(nca_address + 0xf0))) &&
		 0 < wfc_timeout);

	if (0 == wfc_timeout) {
		ncr_unlock(LOCK_DOMAIN);
		return -1;
	}

	/*
	  Copy data words to the buffer.
	*/

	address = (unsigned long)(nca_address + 0x1000);
	while (4 <= number) {
		*((unsigned long *) buffer) =
			ncr_register_read((unsigned *) address);
		address += 4;
		number -= 4;
	}

	if (0 < number) {
		unsigned long temp =
			ncr_register_read((unsigned *) address);
		memcpy((void *) buffer, &temp, number);
	}

	ncr_unlock(LOCK_DOMAIN);

	return 0;
}

/*
  ----------------------------------------------------------------------
  ncr_write
*/

int
ncr_write(unsigned long region, unsigned long address, int number,
	  void *buffer)
{
	command_data_register_0_t cdr0;
	command_data_register_1_t cdr1;
	command_data_register_2_t cdr2;
	unsigned long data_word_base;
	int dbs = (number - 1);
	int wfc_timeout = WFC_TIMEOUT;

	if (NULL == nca_address)
		nca_address = ioremap(NCA_PHYS_ADDRESS, 0x20000);

	if (0 != ncr_lock(LOCK_DOMAIN))
		return -1;

	/*
	  Set up the write.
	*/

	cdr2.raw = 0;
	cdr2.bits.target_node_id = NCP_NODE_ID(region);
	cdr2.bits.target_id_address_upper = NCP_TARGET_ID(region);
	ncr_register_write(cdr2.raw, (unsigned *) (nca_address + 0xf8));

	cdr1.raw = 0;
	cdr1.bits.target_address = (address >> 2);
	ncr_register_write(cdr1.raw, (unsigned *) (nca_address + 0xf4));

	/*
	  Copy from buffer to the data words.
	*/

	data_word_base = (unsigned long)(nca_address + 0x1000);

	while (4 <= number) {
		ncr_register_write(*((unsigned long *) buffer),
				   (unsigned *) data_word_base);
		data_word_base += 4;
		buffer += 4;
		number -= 4;
	}

	if (0 < number) {
		unsigned long temp = 0;

		memcpy((void *) &temp, (void *) buffer, number);
		ncr_register_write(temp, (unsigned *) data_word_base);
		data_word_base += number;
		buffer += number;
		number = 0;
	}

	cdr0.raw = 0;
	cdr0.bits.start_done = 1;

	if (0xff == cdr2.bits.target_id_address_upper) {
		cdr0.bits.local_bit = 1;
	}

	cdr0.bits.cmd_type = 5;
	/* TODO: Verify number... */
	cdr0.bits.dbs = dbs;
	ncr_register_write(cdr0.raw, (unsigned *) (nca_address + 0xf0));
	mb();

	/*
	  Wait for completion.
	*/

	do {
		--wfc_timeout;
	} while ((0x80000000UL ==
		  ncr_register_read((unsigned *)(nca_address + 0xf0))) &&
		 0 < wfc_timeout);

	if (0 == wfc_timeout) {
		ncr_unlock(LOCK_DOMAIN);
		return -1;
	}

	/*
	  Check status.
	*/

	if( 0x3 !=
	    ( ( ncr_register_read( ( unsigned * ) ( nca_address + 0xf0 ) ) &
		0x00c00000 ) >> 22 ) ) {
		unsigned long status;

		status = ncr_register_read((unsigned *)(nca_address + 0xe4));
		ncr_unlock(LOCK_DOMAIN);

		return status;
	}

	ncr_unlock(LOCK_DOMAIN);

	return 0;
}

/*
  ----------------------------------------------------------------------
  ncr_init
*/

int
ncr_init(void)
{
#if 1
	{
		unsigned long value;

#ifdef CONFIG_ARCH_AXXIA
		if (0 != ncr_read(NCP_REGION_ID(0x22, 0), 0x694, 4, &value))
			printk(KERN_CRIT
			       "%s: ncr_read() failed!\n", __FUNCTION__);
		else
			printk(KERN_CRIT
			       "%s: value is 0x%x\n", __FUNCTION__, value);
#else
		if (0 != ncr_read(NCP_REGION_ID(0x22, 0), 0x50, 4, &value))
			printk(KERN_CRIT
			       "%s: ncr_read() failed!\n", __FUNCTION__);
		else
			printk(KERN_CRIT
			       "%s: value is 0x%x\n", __FUNCTION__, value);
#endif
	}
#endif

	return 0;
}

module_init(ncr_init);

/*
  ----------------------------------------------------------------------
  ncr_exit
*/

void __exit
ncr_exit(void)
{
	/* Unmap the NCA. */
	iounmap(nca_address);

	return;
}

module_exit( ncr_exit );

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Register Ring access for LSI's ACP board");

EXPORT_SYMBOL(ncr_read);
EXPORT_SYMBOL(ncr_write);
