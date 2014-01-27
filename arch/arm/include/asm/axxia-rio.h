/*
 * RapidIO support for LSI Axxia parts
 *
 */
#ifndef __ASM_AXXIA_RIO_H__
#define __ASM_AXXIA_RIO_H__

#include <linux/platform_device.h>

/* Constants, Macros, etc. */

#define AXXIA_RIO_SMALL_SYSTEM

#define AXXIA_RIO_SYSMEM_BARRIER() \
    __asm__ __volatile__ ("dmb" : : : "memory")

#define AXXIA_RIO_DISABLE_MACHINE_CHECK()
#define AXXIA_RIO_ENABLE_MACHINE_CHECK()
#define AXXIA_RIO_IF_MACHINE_CHECK(mcsr)	mcsr = 0

#define IN_SRIO8(a, v, ec)	v = inb((long unsigned int)a); ec = 0
#define IN_SRIO16(a, v, ec)	v = inw((long unsigned int)a); ec = 0
#define IN_SRIO32(a, v, ec)	v = inl((long unsigned int)a); ec = 0

#define OUT_SRIO8(a, v)		outb_p(v, (long unsigned int) a)
#define OUT_SRIO16(a, v)	outw_p(v, (long unsigned int) a)
#define OUT_SRIO32(a, v)	outl_p(v, (long unsigned int) a)

#define _SWAP32(x)		((((x) & 0x000000FF) << 24) | (((x) & 0x0000FF00) <<  8) | (((x) & 0x00FF0000) >> 8) | (((x) & 0xFF000000) >> 24))
#define CORRECT_GRIO(a)		_SWAP32(a)
#define CORRECT_RAB(a)		(a)

/* AXXIA RIO Patching Macros */

#define RAPIDIO_REDUNDANT_PATH_LOCK_FAULT()				\
	{								\
		u32 __id;						\
		rio_mport_read_config_32(prev->hport, destid, hopcount,	\
					RIO_DEV_ID_CAR, &__id);		\
		if ((lock == prev->hport->host_deviceid) &&		\
		    (__id == 0x5120000a)) {				\
		        pr_debug("axxia-rio: Patch 1 for HBDIDLCSR[%x:%x]\n", \
				(__id & 0xffff), (((__id) >> 16) & 0xffff)); \
			goto out;					\
		    }							\
	}

#define RAPIDIO_HW_LOCK_LOCK_ERR()					\
	{								\
		u32 __id;						\
		rio_mport_read_config_32(port, destid, hopcount,	\
					RIO_DEV_ID_CAR,	&__id);		\
		if (__id == 0x5120000a) {				\
		        pr_debug("axxia-rio: Patch 2 for HBDIDLCSR[%x:%x]\n", \
				(__id & 0xffff), (((__id) >> 16) & 0xffff)); \
			goto done;					\
		}							\
	}

#define RAPIDIO_HW_UNLOCK_LOCK_ERR()					\
	{								\
		u32 __id;						\
		rio_mport_read_config_32(port, destid,			\
					hopcount,			\
					RIO_DEV_ID_CAR, &__id);		\
		if ((lock == 0xffff) && (__id == 0x5120000a)) {		\
	        	pr_debug("axxia-rio: Patch 3 for HBDIDLCSR[%x:%x]\n", \
				(__id & 0xffff), (((__id) >> 16) & 0xffff)); \
			goto done;					\
		}							\
	}


/* ACP RIO board-specific stuff */

extern int axxia_rio_apio_enable(struct rio_mport *mport, u32 mask, u32 bits);
extern int axxia_rio_apio_disable(struct rio_mport *mport);
extern int axxia_rio_rpio_enable(struct rio_mport *mport, u32 mask, u32 bits);
extern int axxia_rio_rpio_disable(struct rio_mport *mport);

extern int axxia_rapidio_board_init(struct platform_device *dev, int devNum,
				    int *portNdx);


/*****************************/
/* ACP RIO operational stuff */
/*****************************/

/**
 * CNTLZW - Count leading zeros word
 * @val: value from which count number of leading zeros
 *
 * Return: number of zeros
 */
static inline u32 CNTLZW(u32 val)
{
	int n = 0;
	if (val == 0)
		return 32;

	if ((val & 0xFFFF0000) == 0) {
		n += 16;
		val = val << 16;
	}
		/* 1111 1111 1111 1111 0000 0000 0000 0000
		// 16 bits from left are zero! so we omit 16 left bits */
	if ((val & 0xFF000000) == 0) {
		n = n + 8;
		val = val << 8;
	}
		/* 8 left bits are 0 */
	if ((val & 0xF0000000) == 0) {
		n = n + 4;
		val = val << 4;
	}
		/* 4 left bits are 0 */
	if ((val & 0xC0000000) == 0) {
		n = n + 2;
		val = val << 2;
	}
		/* 110000....0 2 left bits are zero */
	if ((val & 0x80000000) == 0) {
		n = n + 1;
		val = val << 1;
	}
		/* first left bit is zero */
	return n;
}

static inline unsigned long get_tb(void)
{
	unsigned int value;
	/* Read CCNT Register (cyclecount) */
	asm volatile ("MRC p15, 0, %0, c9, c13, 0\t\n" : "=r"(value));
	return value;
}

#endif /* __ASM_AXXIA_RIO_H__ */
