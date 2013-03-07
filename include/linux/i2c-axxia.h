#ifndef __I2C_AXXIA_H__
#define __I2C_AXXIA_H__

#include <linux/platform_device.h>

/*
 * Version 2 of the I2C peripheral unit has a different register
 * layout and extra registers.  The ID register in the V2 peripheral
 * unit on the AXXIA4430 reports the same ID as the V1 peripheral
 * unit on the AXXIA3530, so we must inform the driver which IP
 * version we know it is running on from platform / cpu-specific
 * code using these constants in the hwmod class definition.
 */

#define AXXIA_I2C_IP_VERSION_1 1                /* ACP34xx */
#define AXXIA_I2C_IP_VERSION_2 2                /* AXM55xx */

/* struct axxia_i2c_bus_platform_data .flags meanings */

struct axxia_i2c_bus_platform_data {
	u32		rev;
	u32		flags;
};

#endif
