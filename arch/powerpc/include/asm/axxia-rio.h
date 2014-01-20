/*
 * RapidIO support for LSI Axxia 3400 parts
 *
 */
#ifndef __ASM_AXXIA_RIO_H__
#define __ASM_AXXIA_RIO_H__

#include <linux/platform_device.h>

/* Constants, Macros, etc. */

#define AXXIA_RIO_SYSMEM_BARRIER()      __asm__ __volatile__("msync")

#define     AXXIA_RIO_DISABLE_MACHINE_CHECK()                       \
            mtmsr( mfmsr() & ~(MSR_ME));                            \
            __asm__ __volatile__("msync")

#define     AXXIA_RIO_ENABLE_MACHINE_CHECK()                        \
            mtmsr(mfmsr() | (MSR_ME) );                             \
            __asm__ __volatile__("msync")

#define     AXXIA_RIO_IF_MACHINE_CHECK(mcsr)                        \
            __asm__ __volatile__("msync");                          \
            mcsr = mfspr(SPRN_MCSR);                                \
            if ( mcsr != 0) {                                       \
                    /* machine check would have occurred ! */       \
                    /* clear it */                                  \
                    mtspr(SPRN_MCSR, 0);                            \
                    __asm__ __volatile__("msync");                  \
            }

#define __acp_read_rio_config(x, addr, err, op)			\
	__asm__ __volatile__(					\
		"msync" "\n"					\
		"0:	"op" %1,0(%2)\n"			\
		"1:     sync\n"					\
		"2:     nop\n"					\
		"3:\n"						\
		".section .fixup,\"ax\"\n"			\
		"4:	li %1,-1\n"				\
		"	li %0,%3\n"				\
		"	b 3b\n"					\
		".previous\n"					\
		".section __ex_table,\"a\"\n"			\
		PPC_LONG_ALIGN "\n"				\
		PPC_LONG "0b,4b\n"				\
		PPC_LONG "1b,4b\n"				\
		PPC_LONG "2b,4b\n"				\
		".previous"					\
		: "=r" (err), "=r" (x)				\
		: "b" (addr), "i" (-EFAULT), "0" (err))

#define IN_SRIO8(a, v, ec)	__acp_read_rio_config(v, a, ec, "lbz")
#define IN_SRIO16(a, v, ec)	__acp_read_rio_config(v, a, ec, "lhz")
#define IN_SRIO32(a, v, ec)	__acp_read_rio_config(v, a, ec, "lwz")

#define OUT_SRIO8(a, v)   	out_8((u8 *) a, v)
#define OUT_SRIO16(a, v)  	out_be16((u16 *) a, v)
#define OUT_SRIO32(a, v)  	out_be32((u32 *) a, v)

#define CORRECT_GRIO(a)         __le32_to_cpu(a)
#define CORRECT_RAB(a)          (a)


/* ACP RIO board-specific stuff */

extern int axxia_rio_apio_enable(struct rio_mport *mport, u32 mask, u32 bits);
extern int axxia_rio_apio_disable(struct rio_mport *mport);
extern int axxia_rio_rpio_enable(struct rio_mport *mport, u32 mask, u32 bits);
extern int axxia_rio_rpio_disable(struct rio_mport *mport);

static inline int axxia_rapidio_board_init(struct platform_device *dev,
					   int devNum, int *portNdx)
{
	(*portNdx) = 0;
	return 0;
}


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
	u32 tmp = 0;
	(void)val;

	__asm__ volatile
	(
		" cntlzw %0,%1"
		: /*outputs*/ "=r" (tmp)
		: /*inputs*/ "r" (val)
	);

	return tmp;
}

#endif /* __ASM_AXXIA_RIO_H__ */
