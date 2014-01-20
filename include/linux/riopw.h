#ifndef _RIOPW_H_
#define _RIOPW_H_

#define RIOPW_IOC_MAGIC	'j'	/* Arbitrary */
#define RIOPW_IOC_MINNR	0x40
#define RIOPW_IOC_MAXNR	0x80

#define IOCTL_MPORT_GETHID	_IOR(RIOPW_IOC_MAGIC, 0x40, int)
#define IOCTL_MPORT_GETPWMSG	_IOR(RIOPW_IOC_MAGIC, 0x45, int)

#ifdef __KERNEL__
#else

#define RIO_PW_MSG_SIZE		64

union rio_pw_msg {
	struct {
		unsigned int comptag;	/* Component Tag CSR */
		unsigned int errdetect;	/* Port N Error Detect CSR */
		unsigned int is_port;	/* Implementation specific + PortID */
		unsigned int ltlerrdet;	/* LTL Error Detect CSR */
		unsigned int padding[12];
	} em;
	unsigned int raw[RIO_PW_MSG_SIZE/sizeof(unsigned int)];
};

#endif


#endif
