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

/*! @file       ai2c_i2c_regs.h
 *  @brief      I2C Hardware Layer Register Map
 */

#ifndef _AI2C_I2C_REGS_H_
#define _AI2C_I2C_REGS_H_

/*!
 * @defgroup _i2c_hw_regs_     I2C Hardware Register Map
 * @brief    I2C Hardware Registers (i2chwregs)
 * @details  ACP HW registers used to communicate with I2C devices on the
 *           I2C bus immediately associated with the ACP
 * @{
 * @ingroup _i2c_
 */

/******************************************************************************
 * Register handle offsets (X1/X2 aka ACP3400, ACP2500)                       *
 *                                                                            *
 * Regions:0x143.0, 0x14c.0                                                   *
 ******************************************************************************/

#define AI2C_REG_I2C_MAST_XMT_CFG       (0x0000)     /*!< Offset to reg Master
							Transmit Config */
#define AI2C_REG_I2C_MAST_RCV_CFG       (0x0004)     /*!< Offset to reg Master
							Receive Config */
#define AI2C_REG_I2C_MAST_XMT_STAT      (0x0008)     /*!< Offset to reg Master
							Transmit Status */
#define AI2C_REG_I2C_MAST_RCV_STAT      (0x000C)     /*!< Offset to reg Master
							Receive Status */
#define AI2C_REG_I2C_MAST_INT_ENABLE    (0x0010)     /*!< Offset to reg Master
							Interrupt Enable */
#define AI2C_REG_I2C_MAST_INT_CLEAR     (0x0014)     /*!< Offset to reg Master
							Interrupt Clear */
#define AI2C_REG_I2C_MAST_INT_STAT      (0x0018)     /*!< Offset to reg Master
							Interrupt Status */
#define AI2C_REG_I2C_MAST_CLK_CFG       (0x001C)     /*!< Offset to reg Master
							Clock Config */
#define AI2C_REG_I2C_MAST_START_HLD_CFG (0x0020)     /*!< Offset to reg Master
							Start Hold
							Timing Config */
#define AI2C_REG_I2C_MAST_STOP_HLD_CFG  (0x0024)     /*!< Offset to reg Master
							Stop Hold
							Timing Config */
#define AI2C_REG_I2C_MAST_DATA_HLD_CFG  (0x0028)     /*!< Offset to reg Master
							Data Hold
							Timing Config */
#define AI2C_REG_I2C_MAST_BYPASS_MODE   (0x002C)     /*!< Offset to reg Master
							Bypass Mode */
#define AI2C_REG_I2C_MAST_SLV_ADDRESS   (0x0030)     /*!< Offset to reg Master
							Slave Address target */
#define AI2C_REG_I2C_MAST_TXD0          (0x0034)     /*!< Offset to reg Master
							Transmit Data 0 */
#define AI2C_REG_I2C_MAST_TXD1          (0x0038)     /*!< Offset to reg Master
							Transmit Data 1 */
#define AI2C_REG_I2C_MAST_RXD0          (0x003C)     /*!< Offset to reg Master
							Receive Data 0 */
#define AI2C_REG_I2C_MAST_RXD1          (0x0040)     /*!< Offset to reg Master
							Receive Data 1 */
#define AI2C_REG_I2C_SLV_TXRXCONFIG     (0x0044)     /*!< Offset to reg Slave
							Transmit/Receive
							Config */
#define AI2C_REG_I2C_SLV_STAT           (0x0048)     /*!< Offset to reg
							Slave Status */
#define AI2C_REG_I2C_SLV_SELF_ADDRESS   (0x004C)     /*!< Offset to reg
							Slave Self Address */
#define AI2C_REG_I2C_SLV_TXD0           (0x0050)     /*!< Offset to reg Slave
							Transmit Data 0 */
#define AI2C_REG_I2C_SLV_TXD1           (0x0054)     /*!< Offset to reg Slave
							Transmit Data 1 */
#define AI2C_REG_I2C_SLV_RXD0           (0x0058)     /*!< Offset to reg Slave
							Receive Data 0 */
#define AI2C_REG_I2C_SLV_RXD1           (0x005C)     /*!< Offset to reg Slave
							Receive Data 1 */
#define AI2C_REG_I2C_SLV_INT_ENABLE     (0x0060)     /*!< Offset to reg Slave
							Interrupt Enable */
#define AI2C_REG_I2C_SLV_INT_CLEAR      (0x0064)     /*!< Offset to reg Slave
							Interrupt Clear */
#define AI2C_REG_I2C_SLV_DATA_HLD_CFG   (0x0068)     /*!< Offset to reg Slave
							Data Hold
							Timing Config */
#define AI2C_REG_I2C_SLV_CLK_CFG        (0x006C)     /*!< Offset to reg
							Slave Clock Config */


/**********************************
 * Register Structure definitions *
 *********************************/

/*! @struct  ai2c_reg_i2c_mast_xmt_cfg
 *  @brief   I2C Master XMIT Cfg register
 */
struct ai2c_reg_i2c_mast_xmt_cfg {

#ifdef AI2C_BIG_ENDIAN
	unsigned:2;                 /* bits 31:30  reserved */
	unsigned stop:1;            /*!< bits 29     Stop I2C clock */
	unsigned:18;                /* bits 28:11  reserved */
	unsigned zeroAll:1;         /*!< bits 10     Zero all control/data */
	unsigned command:1;         /*!< bits  9     write=0, read=1 */
	unsigned mastModeActive:1;  /*!< bits  8     Master mode active */
	unsigned tenBitAddr:1;      /*!< bits  7     10-bit address mode */
	unsigned:1;                 /* bits  6     reserved */
	unsigned endianness:1;      /*!< bits  5     0=BE (def), 1=LE */
	unsigned numBytes:4;        /*!< bits  4:1   number of bytes to xmit*/
	unsigned xmtReady:1;        /*!< bits  0     Transmit Ready */
#else
	unsigned xmtReady:1;        /*!< bits  0     Transmit Ready */
	unsigned numBytes:4;        /*!< bits  4:1   number of bytes to xmit*/
	unsigned endianness:1;      /*!< bits  5     0=BE (def), 1=LE */
	unsigned:1;                 /* bits  6     reserved */
	unsigned tenBitAddr:1;      /*!< bits  7     10-bit address mode */
	unsigned mastModeActive:1;  /*!< bits  8     Master mode active */
	unsigned command:1;         /*!< bits  9     write=0, read=1 */
	unsigned zeroAll:1;         /*!< bits 10     Zero all control/data */
	unsigned:18;                /* bits 28:11  reserved */
	unsigned stop:1;            /*!< bits 29     Stop I2C clock */
	unsigned:2;                 /* bits 31:30  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_mast_rcv_cfg
 *  @brief   I2C Master RCV Cfg register
 */
struct ai2c_reg_i2c_mast_rcv_cfg {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_8:24;  /* bits 31:8   reserved */
	unsigned reserved_7_6:2;    /* bits  7:6   reserved */
	unsigned chgEndianness:1;   /*!< bits  5     0=BE (def), 1=LE */
	unsigned numBytes:4;        /*!< bits  4:1   number of bytes to rcv */
	unsigned rcvReady:1;        /*!< bits  0     Receive Ready */
#else
	unsigned rcvReady:1;        /*!< bits  0     Receive Ready */
	unsigned numBytes:4;        /*!< bits  4:1   number of bytes to rcv */
	unsigned chgEndianness:1;   /*!< bits  5     0=BE (def), 1=LE */
	unsigned reserved_7_6:2;    /* bits  7:6   reserved */
	unsigned reserved_31_8:24;  /* bits 31:8   reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_mast_xmt_stat
 *  @brief   I2C Master XMIT Status register
 */
struct ai2c_reg_i2c_mast_xmt_stat {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_8:25;  /* bits 31:8   reserved */
	unsigned reserved_7_2:5;    /* bits  7:2   reserved */
	unsigned xmtError:1;        /*!< bits  1     Transmission error */
	unsigned xmtDone:1;         /*!< bits  0     Transmit done */
#else
	unsigned xmtDone:1;         /*!< bits  0     Transmit done */
	unsigned xmtError:1;        /*!< bits  1     Transmission error */
	unsigned reserved_7_2:5;    /* bits  7:2   reserved */
	unsigned reserved_31_8:25;  /* bits 31:8   reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_mast_rcv_stat
 *  @brief   I2C Master RCV Status register
 */
struct ai2c_reg_i2c_mast_rcv_stat {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_6:26;  /* bits 31:6   reserved */
	unsigned numBytesRcv:4;     /*!< bits  5:2   Num Bytes received */
	unsigned rcvError:1;        /*!< bits  1     Reception error */
	unsigned rcvDone:1;         /*!< bits  0     Receive done */
#else
	unsigned rcvDone:1;         /*!< bits  0     Receive done */
	unsigned rcvError:1;        /*!< bits  1     Reception error */
	unsigned numBytesRcv:4;     /*!< bits  5:2   Num Bytes received */
	unsigned reserved_31_6:26;  /* bits 31:6   reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_mast_int_enable
 *  @brief   I2C Master Interrupt Enable register
 */
struct ai2c_reg_i2c_mast_int_enable {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_4:28;  /* bits 31:4  reserved */
	unsigned intRcvErr:1;       /*!< bits  3    Enable int on rcv error */
	unsigned intXmtErr:1;       /*!< bits  2    Enable int on xmt error */
	unsigned intRcvComplete:1;  /*!< bits  1    Enable int on completion
					of received data */
	unsigned intXmtComplete:1;  /*!< bits  0    Enable int on completion
					of transmitted data */
#else
	unsigned intXmtComplete:1;  /*!< bits  0    Enable int on completion
					of transmitted data */
	unsigned intRcvComplete:1;  /*!< bits  1    Enable int on completion
					of received data */
	unsigned intXmtErr:1;       /*!< bits  2    Enable int on xmt error */
	unsigned intRcvErr:1;       /*!< bits  3    Enable int on rcv error */
	unsigned reserved_31_4:28;  /* bits 31:4  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_mast_int_clear
 *  @brief   I2C Master Interrupt Clear register
 */
struct ai2c_reg_i2c_mast_int_clear {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_4:28;  /* bits 31:4  reserved */
	unsigned intRcvErr:1;       /*!< bits  3    Clear int on rcv error */
	unsigned intXmtErr:1;       /*!< bits  2    Clear int on xmt error */
	unsigned intRcvComplete:1;  /*!< bits  1    Clear int on completion
					of received data */
	unsigned intXmtComplete:1;  /*!< bits  0    Clear int on completion
					of transmitted data */
#else
	unsigned intXmtComplete:1;  /*!< bits  0    Clear int on completion
					of transmitted data */
	unsigned intRcvComplete:1;  /*!< bits  1    Clear int on completion
					of received data */
	unsigned intXmtErr:1;       /*!< bits  2    Clear int on xmt error */
	unsigned intRcvErr:1;       /*!< bits  3    Clear int on rcv error */
	unsigned reserved_31_4:28;  /* bits 31:4  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_mast_int_stat
 *  @brief   I2C Master Interrupt Status register
 */
struct ai2c_reg_i2c_mast_int_stat {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_4:28;  /* bits 31:4  reserved */
	unsigned rcvErr:1;          /*!< bits  3    Receive error */
	unsigned xmtDone:1;         /*!< bits  2    Transmit done */
	unsigned rcvDataRdy:1;      /*!< bits  1    Receive data ready */
	unsigned xmtErr:1;          /*!< bits  0    Transmit error */
#else
	unsigned xmtErr:1;          /*!< bits  0    Transmit error */
	unsigned rcvDataRdy:1;      /*!< bits  1    Receive data ready */
	unsigned xmtDone:1;         /*!< bits  2    Transmit done */
	unsigned rcvErr:1;          /*!< bits  3    Receive error */
	unsigned reserved_31_4:28;  /* bits 31:4  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_mast_clk_cfg
 *  @brief   I2C Master Clock Configuration register
 */
struct ai2c_reg_i2c_mast_clk_cfg {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_26:6;  /* bits 31:26 reserved */
	unsigned pclkHighSCL:10;    /*!< bits 25:16 # of pclk durations that
					equal high period of SCL*/
	unsigned reserved_15_10:6;  /* bits 15:10 reserved */
	unsigned pclkLowSCL:10;     /*!< bits  9:0  # of pclk durations that
					equal low period of SCL */
#else
	unsigned pclkLowSCL:10;     /*!< bits  9:0  # of pclk durations that
					equal low period of SCL */
	unsigned reserved_15_10:6;  /* bits 15:10 reserved */
	unsigned pclkHighSCL:10;    /*!< bits 25:16 # of pclk durations that
					equal high period of SCL*/
	unsigned reserved_31_26:6;  /* bits 31:26 reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_mast_start_hld_cfg
 *  @brief   I2C Master Start Hold Timing Configuration register
 */
struct ai2c_reg_i2c_mast_start_hld_cfg {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_26:6;  /* bits 31:26 reserved */
	unsigned pclkStart:10;      /*!< bits 25:16 # of pclk durations that
					equal start cond on SDA */
	unsigned reserved_15_10:6;  /* bits 15:10 reserved */
	unsigned pclkSetup:10;      /*!< bits  9:0  # of pclk durations that
					equal setup cond on SDA */
#else
	unsigned pclkSetup:10;      /*!< bits  9:0  # of pclk durations that
					equal setup cond on SDA */
	unsigned reserved_15_10:6;  /* bits 15:10 reserved */
	unsigned pclkStart:10;      /*!< bits 25:16 # of pclk durations that
					equal start cond on SDA */
	unsigned reserved_31_26:6;  /* bits 31:26 reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_mast_stop_hld_cfg
 *  @brief   I2C Master Stop Hold Timing Configuration register
 */
struct ai2c_reg_i2c_mast_stop_hld_cfg {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_26:6;  /* bits 31:26 reserved */
	unsigned pclkStop:10;       /*!< bits 25:16 # of pclk durations that
					equal stop cond on SDA */
	unsigned reserved_15_10:6;  /* bits 15:10 reserved */
	unsigned pclkSetup:10;      /*!< bits  9:0  # of pclk durations that
					equal setup cond on SDA */
#else
	unsigned pclkSetup:10;      /*!< bits  9:0  # of pclk durations that
					equal setup cond on SDA */
	unsigned reserved_15_10:6;  /* bits 15:10 reserved */
	unsigned pclkStop:10;       /*!< bits 25:16 # of pclk durations that
					equal stop cond on SDA */
	unsigned reserved_31_26:6;  /* bits 31:26 reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_mast_data_hld_cfg
 *  @brief   I2C Master Data Hold Timing Configuration register
 */
struct ai2c_reg_i2c_mast_data_hld_cfg {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_26:6;  /* bits 31:26 reserved */
	unsigned pclkStop:10;       /*!< bits 25:16 # of pclk durations that
					equal stop cond on SDA
					for data changing */
	unsigned reserved_15_10:6;  /* bits 15:10 reserved */
	unsigned pclkSetup:10;      /*!< bits  9:0  # of pclk durations that
					equal setup cond on SDA
					for data changing */
#else
	unsigned pclkSetup:10;      /*!< bits  9:0  # of pclk durations that
					equal setup cond on SDA
					for data changing */
	unsigned reserved_15_10:6;  /* bits 15:10 reserved */
	unsigned pclkStop:10;       /*!< bits 25:16 # of pclk durations that
					equal stop cond on SDA
					for data changing */
	unsigned reserved_31_26:6;  /* bits 31:26 reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_mast_bypass_mode
 *  @brief   I2C Master Bypass Mode register
 */
struct ai2c_reg_i2c_mast_bypass_mode {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_3:29;  /* bits 31:3  reserved */
	unsigned enableBypass:1;    /*!< bits  2    Enable bypass mode */
	unsigned SCLvalue:1;        /*!< bits  1    SCL value */
	unsigned SDAvalue:1;        /*!< bits  0    SDA value */
#else
	unsigned SDAvalue:1;        /*!< bits  0    SDA value */
	unsigned SCLvalue:1;        /*!< bits  1    SCL value */
	unsigned enableBypass:1;    /*!< bits  2    Enable bypass mode */
	unsigned reserved_31_3:29;  /* bits 31:3  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_mast_slv_address
 *  @brief   I2C Master Slave Address register
 */
struct ai2c_reg_i2c_mast_slv_address {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_16:16; /* bits 31:16 reserved */
	unsigned targSlvAddr:16;    /*!< bits 16:0  Address of the slave
					device being addressed */
#else
	unsigned targSlvAddr:16;    /*!< bits 16:0  Address of the slave
					device being addressed */
	unsigned reserved_31_16:16; /* bits 31:16 reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_mast_txd0
 *  @brief   I2C Master Transmit Data 0 register
 */
struct ai2c_reg_i2c_mast_txd0 {
#ifdef AI2C_BIG_ENDIAN
	unsigned data:32;           /*!< bits 31:0  Data to be transmitted */
#else
	unsigned data:32;           /*!< bits 31:0  Data to be transmitted */
#endif
};

/*! @struct  ai2c_reg_i2c_mast_rxd0_t
 *  @brief   I2C Master Receive Data 0 register
 */
struct ai2c_reg_i2c_mast_rxd0 {

#ifdef AI2C_BIG_ENDIAN
	unsigned data:32;           /*!< bits 31:0  Data received */
#else
	unsigned data:32;           /*!< bits 31:0  Data received */
#endif
};

/*! @struct  ai2c_reg_i2c_slv_txrxconfig
 *  @brief   I2C Slave Transmit/Receive Config register
 */
struct ai2c_reg_i2c_slv_txrxconfig {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_11:21; /* bits 31:11  reserved */
	unsigned zeroAll:1;         /*!< bits 10     Zero all control/data */
	unsigned rcvEndianness:1;   /*!< bits  9     0=BE (def), 1=LE */
	unsigned slvModeActive:1;   /*!< bits  8     Slave mode active */
	unsigned tenBitAddr:1;      /*!< bits  7     10-bit address mode */
	unsigned xmtEndianness:1;   /*!< bits  6     0=BE (def), 1=LE */
	unsigned numBytes:5;        /*!< bits  5:1   number of bytes to xmit*/
	unsigned xmtReady:1;        /*!< bits  0     Transmit Ready */
#else
	unsigned xmtReady:1;        /*!< bits  0     Transmit Ready */
	unsigned numBytes:5;        /*!< bits  5:1   number of bytes to xmit*/
	unsigned xmtEndianness:1;   /*!< bits  6     0=BE (def), 1=LE */
	unsigned tenBitAddr:1;      /*!< bits  7     10-bit address mode */
	unsigned slvModeActive:1;   /*!< bits  8     Slave mode active */
	unsigned rcvEndianness:1;   /*!< bits  9     0=BE (def), 1=LE */
	unsigned zeroAll:1;         /*!< bits 10     Zero all control/data */
	unsigned reserved_31_11:21; /* bits 31:11  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_slv_stat
 *  @brief   I2C Slave Status register
 */
struct ai2c_reg_i2c_slv_stat {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_10:23; /* bits 31:10  reserved */
	unsigned numBytesRcv:4;     /*!< bits  9:5   # bytes rcved on slave
					transaction */
	unsigned slvXmtErr:1;       /*!< bits  4     Slave xmit in error */
	unsigned slvXmtDone:1;      /*!< bits  3     Slave done xmit data */
	unsigned rcvErr:1;          /*!< bits  2     Rcv data in error */
	unsigned cmdRcv:1;          /*!< bits  1     Command received by slv*/
	unsigned slvRcvRdy:1;       /*!< bits  0     Slv rcved data ready */
#else
	unsigned slvRcvRdy:1;       /*!< bits  0     Slv rcved data ready */
	unsigned cmdRcv:1;          /*!< bits  1     Command received by slv*/
	unsigned rcvErr:1;          /*!< bits  2     Rcv data in error */
	unsigned slvXmtDone:1;      /*!< bits  3     Slave done xmit data */
	unsigned slvXmtErr:1;       /*!< bits  4     Slave xmit in error */
	unsigned numBytesRcv:4;     /*!< bits  9:5   # bytes rcved on slave
					transaction */
	unsigned reserved_31_10:23; /* bits 31:10  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_slv_self_address
 *  @brief   I2C Slave Self Address register
 */
struct ai2c_reg_i2c_slv_self_address {

#ifdef AI2C_BIG_ENDIAN
	unsigned address:32;        /*!< bits 31:0   address of APPI2C */
#else
	unsigned address:32;        /*!< bits 31:0   address of APPI2C */
#endif
};

/*! @struct  ai2c_reg_i2c_slv_txd0
 *  @brief   I2C Slave Transmit Data 0 register
 */
struct ai2c_reg_i2c_slv_txd0 {

#ifdef AI2C_BIG_ENDIAN
	unsigned data:32;           /*!< bits 31:0  Data to be transmitted */
#else
	unsigned data:32;           /*!< bits 31:0  Data to be transmitted */
#endif
};

/*! @struct  ai2c_reg_i2c_slv_rxd0
 *  @brief   I2C Slave Receive Data 0 register
 */
struct ai2c_reg_i2c_slv_rxd0 {

#ifdef AI2C_BIG_ENDIAN
	unsigned data:32;           /*!< bits 31:0  Data received */
#else
	unsigned data:32;           /*!< bits 31:0  Data received */
#endif
};

/*! @struct  ai2c_reg_i2c_slv_int_enable
 *  @brief   I2C Slave Interrupt Enable register
 */
struct ai2c_reg_i2c_slv_int_enable {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_4:28;  /* bits 31:4  reserved */
	unsigned intRcvErr:1;       /*!< bits  3    Enable int on rcv error */
	unsigned intRcvDataRdy:1;   /*!< bits  2    Enable int on rcv ready */
	unsigned intXmtErr:1;       /*!< bits  1    Enable int on xmt error */
	unsigned intXmtComplete:1;  /*!< bits  0    Enable int on completion
					of transmitted data */
#else
	unsigned intXmtComplete:1;  /*!< bits  0    Enable int on completion
					of transmitted data */
	unsigned intXmtErr:1;       /*!< bits  1    Enable int on xmt error */
	unsigned intRcvDataRdy:1;   /*!< bits  2    Enable int on rcv ready */
	unsigned intRcvErr:1;       /*!< bits  3    Enable int on rcv error */
	unsigned reserved_31_4:28;  /* bits 31:4  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_slv_int_clear
 *  @brief   I2C Slave Interrupt Clear register
 */
struct ai2c_reg_i2c_slv_int_clear {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_4:28;  /* bits 31:4  reserved */
	unsigned intRcvErr:1;       /*!< bits  3    Clear int on rcv error */
	unsigned intRcvDataRdy:1;   /*!< bits  2    Clear int on rcv ready */
	unsigned intXmtErr:1;       /*!< bits  1    Clear int on xmt error */
	unsigned intXmtComplete:1;  /*!< bits  0    Clear int on completion
					of transmitted data */
#else
	unsigned intXmtComplete:1;  /*!< bits  0    Clear int on completion
					of transmitted data */
	unsigned intXmtErr:1;       /*!< bits  1    Clear int on xmt error */
	unsigned intRcvDataRdy:1;   /*!< bits  2    Clear int on rcv ready */
	unsigned intRcvErr:1;       /*!< bits  3    Clear int on rcv error */
	unsigned reserved_31_4:28;  /* bits 31:4  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_slv_data_hld_cfg
 *  @brief   I2C Slave Data Hold Timing Config register
 */
struct ai2c_reg_i2c_slv_data_hld_cfg {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_26:6;  /* bits 31:26 reserved */
	unsigned pclkHold:10;       /*!< bits 25:16 # of pclk durations that
					equal hold cond on SDA */
	unsigned reserved_15_10:6;  /*!< bits 15:10 reserved */
	unsigned pclkSetup:10;      /*!< bits  9:0  # of pclk durations that
					equal setup cond on SDA */
#else
	unsigned pclkSetup:10;      /*!< bits  9:0  # of pclk durations that
					equal setup cond on SDA */
	unsigned reserved_15_10:6;  /*!< bits 15:10 reserved */
	unsigned pclkHold:10;       /*!< bits 25:16 # of pclk durations that
					equal hold cond on SDA */
	unsigned reserved_31_26:6;  /* bits 31:26 reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_slv_clk_cfg
 *  @brief   I2C Slave Clock Config register
 */
struct ai2c_reg_i2c_slv_clk_cfg {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_26:6;  /* bits 31:26 reserved */
	unsigned pclkLowSCL:10;     /*!< bits 25:16 # of pclk durations that
					equal low period of SCL*/
	unsigned reserved_15_10:6;  /* bits 15:10 reserved */
	unsigned pclkHighSCL:10;    /*!< bits  9:0  # of pclk durations that
					equal high period of SCL*/
#else
	unsigned pclkHighSCL:10;    /*!< bits  9:0  # of pclk durations that
					equal high period of SCL*/
	unsigned reserved_15_10:6;  /* bits 15:10 reserved */
	unsigned pclkLowSCL:10;     /*!< bits 25:16 # of pclk durations that
					equal low period of SCL*/
	unsigned reserved_31_26:6;  /* bits 31:26 reserved */
#endif
};


/******************************************************************************
 * Register handle offsets (X7 aka AXM55xx)                                   *
 *                                                                            *
 * Regions:0x143.0, 0x14c.0, 0x152.0                                          *
 *                                                                            *
 * Regerence: AMBA (tm) Peripheral I2c Bus Controller with SMB Functions      *
 *                (API2C) Reference Manual, Version 0.1, April 2011,          *
 *                DB13-00329-00.                                              *
 ******************************************************************************/

#define AI2C_REG_I2C_X7_GLOBAL_CONTROL     (0x0000)   /*!< Offset to reg
							Global Control */
#define AI2C_REG_I2C_X7_INTERRUPT_STATUS   (0x0004)   /*!< Offset to reg
							Interrupt Status */
#define AI2C_REG_I2C_X7_INTERRUPT_ENABLE   (0x0008)   /*!< Offset to reg
							Interrupt Enable */
#define AI2C_REG_I2C_X7_WAIT_TIMER_CONTROL (0x000C)   /*!< Offset to reg
							Wait Timer Control */
#define AI2C_REG_I2C_X7_IBML_TIMEOUT       (0x0010)   /*!< Offset to reg
							IBML Timeout */
#define AI2C_REG_I2C_X7_IBML_LOW_MEXT      (0x0014)   /*!< Offset to reg
							IBML Low MEXT */
#define AI2C_REG_I2C_X7_IBML_LOW_SEXT      (0x0018)   /*!< Offset to reg
							IBML Low SEXT */
#define AI2C_REG_I2C_X7_TIMER_CLOCK_DIV    (0x001C)   /*!< Offset to reg
							Timer Clock Division */
#define AI2C_REG_I2C_X7_I2C_BUS_MONITOR    (0x0020)   /*!< Offset to reg I2C
							Bus Monitor */
#define AI2C_REG_I2C_X7_SOFT_RESET         (0x0024)   /*!< Offset to reg Soft
							Reset */
#define AI2C_REG_I2C_X7_MST_COMMAND        (0x0028)   /*!< Offset to reg
							Master Command */
#define AI2C_REG_I2C_X7_MST_RX_XFER        (0x002C)   /*!< Offset to reg
							Master Receive
							Transfer */
#define AI2C_REG_I2C_X7_MST_TX_XFER        (0x0030)   /*!< Offset to reg
							Master Transmit
							Transfer */
#define AI2C_REG_I2C_X7_MST_ADDR_1         (0x0034)   /*!< Offset to reg
							Master Address 1 */
#define AI2C_REG_I2C_X7_MST_ADDR_2         (0x0038)   /*!< Offset to reg
							Master Address 2 */
#define AI2C_REG_I2C_X7_MST_DATA           (0x003C)   /*!< Offset to reg
							Master Data */
#define AI2C_REG_I2C_X7_MST_TX_FIFO        (0x0040)   /*!< Offset to reg
							Master Transmit FIFO */
#define AI2C_REG_I2C_X7_MST_RX_FIFO        (0x0044)   /*!< Offset to reg
							Master Receive FIFO */
#define AI2C_REG_I2C_X7_MST_INT_ENABLE     (0x0048)   /*!< Offset to reg
							Master Interrupt
							Enable */
#define AI2C_REG_I2C_X7_MST_INT_STATUS     (0x004C)   /*!< Offset to reg
							Master Interrupt
							Status */
#define AI2C_REG_I2C_X7_MST_TX_BYTES_XFRD  (0x0050)   /*!< Offset to reg
							Master TX Bytes
							Transferred */
#define AI2C_REG_I2C_X7_MST_RX_BYTES_XFRD  (0x0054)   /*!< Offset to reg
							Master RX Bytes
							Transferred */
#define AI2C_REG_I2C_X7_SLV_ADDR_DEC_CTL   (0x0058)   /*!< Offset to reg
							Slave Address
							Decrement Ctl */
#define AI2C_REG_I2C_X7_SLV_ADDR_1         (0x005C)   /*!< Offset to reg
							Slave Address 1 */
#define AI2C_REG_I2C_X7_SLV_ADDR_2         (0x0060)   /*!< Offset to reg
							Slave Address 2 */
#define AI2C_REG_I2C_X7_SLV_RX_CTL         (0x0064)   /*!< Offset to reg
							Slave Receive Control */
#define AI2C_REG_I2C_X7_SLV_DATA           (0x0068)   /*!< Offset to reg
							Slave Data */
#define AI2C_REG_I2C_X7_SLV_RX_FIFO        (0x006C)   /*!< Offset to reg
							Slave Receive FIFO */
#define AI2C_REG_I2C_X7_SLV_INT_ENABLE     (0x0070)   /*!< Offset to reg
							Slave Interrupt
							Enable */
#define AI2C_REG_I2C_X7_SLV_INT_STATUS     (0x0074)   /*!< Offset to reg
							Slave Interrupt
							Status */
#define AI2C_REG_I2C_X7_SLV_READ_DUMMY     (0x0078)   /*!< Offset to reg
							Slave Read Dummy */
#define AI2C_REG_I2C_X7_SCL_HIGH_PERIOD    (0x0080)   /*!< Offset to reg
							SCL High Period */
#define AI2C_REG_I2C_X7_SCL_LOW_PERIOD     (0x0084)   /*!< Offset to reg
							SCL Low Period */
#define AI2C_REG_I2C_X7_SPIKE_FLTR_LEN     (0x0088)   /*!< Offset to reg
							Spike Filter Length */
#define AI2C_REG_I2C_X7_SDA_SETUP_TIME     (0x008C)   /*!< Offset to reg
							SDA Setup Time */
#define AI2C_REG_I2C_X7_SDA_HOLD_TIME      (0x0090)   /*!< Offset to reg
							SDA Hold Time */
#define AI2C_REG_I2C_X7_SMB_ALERT          (0x0094)   /*!< Offset to reg
							SMB Alert */
#define AI2C_REG_I2C_X7_UDID_W7            (0x0098)   /*!< Offset to reg
							UDID W7 */
#define AI2C_REG_I2C_X7_UDID_W7_DEFAULT    (0x00000008) /*!< Def value reg
							UDID W7 */
#define AI2C_REG_I2C_X7_UDID_W6            (0x009C)   /*!< Offset to reg
							UDID W6 */
#define AI2C_REG_I2C_X7_UDID_W5            (0x00A0)   /*!< Offset to reg
							UDID W5 */
#define AI2C_REG_I2C_X7_UDID_W4            (0x00A4)   /*!< Offset to reg
							UDID W4 */
#define AI2C_REG_I2C_X7_UDID_W4_DEFAULT    (0x00000004) /*!< Def value reg
							UDID W4 */
#define AI2C_REG_I2C_X7_UDID_W3            (0x00A8)   /*!< Offset to reg
							UDID W3 */
#define AI2C_REG_I2C_X7_UDID_W2            (0x00AC)   /*!< Offset to reg
							UDID W2 */
#define AI2C_REG_I2C_X7_UDID_W1            (0x00B0)   /*!< Offset to reg
							UDID W1 */
#define AI2C_REG_I2C_X7_UDID_W0            (0x00B4)   /*!< Offset to reg
							UDID W0 */
#define AI2C_REG_I2C_X7_ARPPEC_CFG_STAT    (0x00B8)   /*!< Offset to reg
							ARPPEC Cfg Status */
#define AI2C_REG_I2C_X7_SLV_ARP_INT_ENABLE (0x00BC)   /*!< Offset to reg
							Slave ARP Interrupt
							Enable */
#define AI2C_REG_I2C_X7_SLV_ARP_INT_STATUS (0x00C0)   /*!< Offset to reg
							Slave ARP Interrupt
							Status */
#define AI2C_REG_I2C_X7_MST_ARP_INT_ENABLE (0x00C4)   /*!< Offset to reg
							Master ARP Interrupt
							Enable */
#define AI2C_REG_I2C_X7_MST_ARP_INT_STATUS (0x00C8)   /*!< Offset to reg
							Master ARP Interrupt
							Status */
/*
** Unused                                  0x00CC - 0x00FC
*/


/**********************************
 * Register Structure definitions *
 *********************************/

/*! @struct  ai2c_reg_i2c_x7_global_control
 *  @brief   I2C Global Control register (X7)
 */
struct ai2c_reg_i2c_x7_global_control {

#ifdef AI2C_BIG_ENDIAN
	unsigned:29;     /*!< bits 31:3  reserved */
	unsigned IE:1;   /*!< bits  2     enable/disable IBML timers:
						0=disable (def), 1=enable */
	unsigned SE:1;   /*!< bits  1     enable/disable SSM address
						decode:0=disable, 1=enable */
	unsigned ME:1;   /*!< bits  0     enable/disable MSM:
						0=disable, 1=enable */
#else
	unsigned ME:1;   /*!< bits  0     enable/disable MSM:
						0=disable, 1=enable */
	unsigned SE:1;   /*!< bits  1     enable/disable SSM address
						decode:0=disable, 1=enable */
	unsigned IE:1;   /*!< bits  2     enable/disable IBML timers:
						=disable (def), 1=enable */
	unsigned:29;     /* bits 31:3  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_interrupt_status
 *  @brief   I2C Interrupt Status register (X7)
 */
struct ai2c_reg_i2c_x7_interrupt_status {
#ifdef AI2C_BIG_ENDIAN
	unsigned:28;         /*!< bits 31:4  reserved */
	unsigned mai:1;      /*!< bits  3     OR of all master ARP
				interrupt sources */
	unsigned sai:1;      /*!< bits  2     OR of all slave ARP
				interrupt sources */
	unsigned si:1;       /*!< bits  1     slave interrupt source(s) OR */
	unsigned mi:1;       /*!< bits  0     master interrupt source(s) OR */
#else
	unsigned mi:1;       /*!< bits  0     master interrupt source(s) OR */
	unsigned si:1;       /*!< bits  1     slave interrupt source(s) OR */
	unsigned sai:1;      /*!< bits  2     OR of all slave ARP
				interrupt sources */
	unsigned mai:1;      /*!< bits  3     OR of all master ARP
				interrupt sources */
	unsigned:28;         /*!< bits 31:4  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_interrupt_enable
 *  @brief   I2C Interrupt Enable register (X7)
 */
struct ai2c_reg_i2c_x7_interrupt_enable {
#ifdef AI2C_BIG_ENDIAN
	unsigned:28;         /*!< bits 31:4  reserved */
	unsigned maie:1;     /*!< bits  3     enable of all master ARP
				interrupt sources */
	unsigned saie:1;     /*!< bits  2     enable of all slave ARP
				interrupt sources */
	unsigned sie:1;      /*!< bits  1     enable of slave interrupt
				source(s) */
	unsigned mie:1;      /*!< bits  0     enable of master interrupt
				source(s) */
#else
	unsigned mie:1;      /*!< bits  0     enable of master interrupt
				source(s) */
	unsigned sie:1;      /*!< bits  1     enable of slave interrupt
				source(s) */
	unsigned saie:1;     /*!< bits  2     enable of all slave ARP
				interrupt sources */
	unsigned maie:1;     /*!< bits  3     enable of all master ARP
				interrupt sources */
	unsigned:28;         /*!< bits 31:4  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_wait_timer_control
 *  @brief   I2C Master XMIT Status register (X7)
 */
struct ai2c_reg_i2c_x7_wait_timer_control {
#ifdef AI2C_BIG_ENDIAN
	unsigned:16;         /*!< bits 31:16  reserved */
	unsigned te:1;       /*!< bits 15     enable master/slave wait timer */
	unsigned tlv:15;     /*!< bits 14:0   timer load value to extend the
				SCL low timer */
#else
	unsigned tlv:15;     /*!< bits 14:0   timer load value to extend the
				SCL low timer */
	unsigned te:1;       /*!< bits 15     enable master/slave wait timer */
	unsigned:16;         /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_ibml_timeout
 *  @brief   I2C Master RCV Status register (X7)
 */
struct ai2c_reg_i2c_x7_ibml_timeout {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;         /*!< bits 31:16  reserved */
	unsigned te:1;       /*!< bits 15     enable/disable IBML Timeout */
	unsigned itlv:15;    /*!< bits 14:0   timer load value to extend
				the SCL low timer after high */
#else
	unsigned itlv:15;    /*!< bits 14:0   timer load value to extend
				the SCL low timer after high */
	unsigned te:1;       /*!< bits 15     enable/disable IBML Timeout */
	unsigned:16;         /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_ibml_low_mext
 *  @brief   I2C IBML Low MEXT register (X7)
 */
struct ai2c_reg_i2c_x7_ibml_low_mext {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;         /*!< bits 31:16  reserved */
	unsigned te:1;       /*!< bits 15     enable/disable IBML
				Low MEXT flag*/
	unsigned imtlv:15;   /*!< bits 14:0   timer load value at
				begin of every master data byte transfer */
#else
	unsigned imtlv:15;   /*!< bits 14:0   timer load value at begin
				of every master data byte transfer */
	unsigned te:1;       /*!< bits 15     enable master/slave wait timer */
	unsigned:16;         /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_low_sext
 *  @brief   I2C Master Interrupt Clear register (X7)
 */
struct ai2c_reg_i2c_x7_low_sext {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;         /*!< bits 31:16  reserved */
	unsigned te:1;       /*!< bits 15     enable/disable IBML
				Low MEXT flag*/
	unsigned istlv:15;   /*!< bits 14:0   timer load value at
				begin of every slave data byte transfer */
#else
	unsigned istlv:15;   /*!< bits 14:0   timer load value at begin
				of every slave data byte transfer */
	unsigned te:1;       /*!< bits 15     enable master/slave wait timer */
	unsigned:16;         /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_i2c_bus_monitor
 *  @brief   I2C Master Interrupt Status register (X7)
 */
struct ai2c_reg_i2c_x7_i2c_bus_monitor {

#ifdef AI2C_BIG_ENDIAN
	unsigned:26;         /*!< bits 31:6  reserved */
	unsigned sdae:1;     /*!< bits  5     At least 1 high-to-low
				transition occurred on SDA status line */
	unsigned scle:1;     /*!< bits  4     At least 1 high-to-low
				transition occurred on SCL status line */
	unsigned sdac:1;     /*!< bits  3     Controls SDA signal */
	unsigned sclc:1;     /*!< bits  2     Controls SCL signal */
	unsigned sdas:1;     /*!< bits  1     Current value of SDA signal
				on I2C bus */
	unsigned scls:1;     /*!< bits  0     Current value of SCL signal
				on I2C bus */
#else
	unsigned scls:1;     /*!< bits  0     Current value of SCL signal
				on I2C bus */
	unsigned sdas:1;     /*!< bits  1     Current value of SDA signal
				on I2C bus */
	unsigned sclc:1;     /*!< bits  2     Controls SCL signal */
	unsigned sdac:1;     /*!< bits  3     Controls SDA signal */
	unsigned scle:1;     /*!< bits  4     At least 1 high-to-low
				transition occurred on SCL status line */
	unsigned sdae:1;     /*!< bits  5     At least 1 high-to-low
				transition occurred on SDA status line */
	unsigned:26;         /*!< bits 31:6  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_soft_reset
 *  @brief   I2C Soft Reset register (X7)
 */
struct ai2c_reg_i2c_x7_soft_reset {

#ifdef AI2C_BIG_ENDIAN
	unsigned:31;       /*!< bits 31:1  reserved */
	unsigned i2cr:1;   /*!< bits  0     Reset the entire API2C module
				when set to 1 */
#else
	unsigned i2cr:1;   /*!< bits  0     Reset the entire API2C module
				when set to 1 */
	unsigned:31;       /*!< bits 31:1  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_mst_command
 *  @brief   I2C Master Command register (X7)
 */
struct ai2c_reg_i2c_x7_mst_command {

#ifdef AI2C_BIG_ENDIAN
	unsigned:28;         /*!< bits 31:4  reserved */
	unsigned sc:1;       /*!< bits  3     Issue command when set to 1 */
	unsigned cmdtype:3;  /*!< bits  2:0  Command type:
				000: Manual Mode Transfer
				001: Automatic Mode Transfer
				010: Sequence Mode Transfer
				011: Issue master Stop
				111: Flush Master Transmit FIFO */
#else
	unsigned cmdtype:3;  /*!< bits  2:0  Command type:
				000: Manual Mode Transfer
				001: Automatic Mode Transfer
				010: Sequence Mode Transfer
				011: Issue master Stop
				111: Flush Master Transmit FIFO */
	unsigned sc:1;         /*!< bits  3     Issue command when set to 1 */
	unsigned:28;         /*!< bits 31:4  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_mst_rx_xfer
 *  @brief   I2C Master Receive Transfer Length register (X7)
 */
struct ai2c_reg_i2c_x7_mst_rx_xfer {

#ifdef AI2C_BIG_ENDIAN
	unsigned:24;         /*!< bits 31:8  reserved */
	unsigned length:8;   /*!< bits  7:0  Number of bytes master receives
				during read xfer */
#else
	unsigned length:8;   /*!< bits  7:0  Number of bytes master receives
				during read xfer */
	unsigned:24;         /*!< bits 31:8  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_mst_tx_xfer
 *  @brief   I2C Master Transmit Transfer Length register (X7)
 */
struct ai2c_reg_i2c_x7_mst_tx_xfer {

#ifdef AI2C_BIG_ENDIAN
	unsigned:24;         /*!< bits 31:8  reserved */
	unsigned length:8;   /*!< bits  7:0  Number of bytes master sends
				during write xfer */
#else
	unsigned length:8;   /*!< bits  7:0  Number of bytes master sends
				during write xfer */
	unsigned:24;         /*!< bits 31:8  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_mst_addr_1
 *  @brief   I2C Master Address 1 register (X7)
 */
struct ai2c_reg_i2c_x7_mst_addr_1 {

#ifdef AI2C_BIG_ENDIAN
	unsigned:24;         /*!< bits 31:8  reserved */
	unsigned i2ca1:7;    /*!< bits  7:1  First 7 bits of the address phase
				of transaction. If bits [7:3] = 11110, then
				10-bit addressing is selected, and bits [2:1]
				become the two most significant bits of a
				10-bit address. */
	unsigned ad:1;       /*!< bits  0     Data direction:0=write, 1=read */
#else
	unsigned ad:1;       /*!< bits  0     Data direction:0=write, 1=read */
	unsigned i2ca1:7;    /*!< bits  7:1  First 7 bits of the address phase
				of transaction. If bits [7:3] = 11110, then
				10-bit addressing is selected, and bits [2:1]
				become the two most significant bits of a
				10-bit address. */
	unsigned:24;         /*!< bits 31:8  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_mst_addr_2
 *  @brief   I2C Master Address 2 register (X7)
 */
struct ai2c_reg_i2c_x7_mst_addr_2 {

#ifdef AI2C_BIG_ENDIAN
	unsigned:24;         /*!< bits 31:8  reserved */
	unsigned i2ca2:8;    /*!< bits  7:0  I2C Address bits of a 10-bit
				I2C address when
				selected.  If 10-bit addressing is enabled, this
				field is sent as the second byte in the address
				phase of an I2C transaction. */
#else
	unsigned i2ca2:8;    /*!< bits  7:0  I2C Address bits of a 10-bit
				I2C address when selected.
				If 10-bit addressing is enabled, this
				field is sent as the second byte in the address
				phase of an I2C transaction. */
	unsigned:24;         /*!< bits 31:8  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_mst_data
 *  @brief   I2C Master Data register (X7)
 */
struct ai2c_reg_i2c_x7_mst_data {

#ifdef AI2C_BIG_ENDIAN
	unsigned:24;         /*!< bits 31:8  reserved */
	unsigned data:8;     /*!< bits  7:0  Each byte written goes to the
				Master Transmit FIFO.
				Each byte read returned from the
				Master Transmit FIFO. */
#else
	unsigned data:8;     /*!< bits  7:0  Each byte written goes to the
				Master Transmit FIFO.
				Each byte read returned from the
				Master Transmit FIFO. */
	unsigned:24;         /*!< bits 31:8  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_mst_tx_fifo
 *  @brief   I2C Master Transmit FIFO Status register (X7)
 */
struct ai2c_reg_i2c_x7_mst_tx_fifo {

#ifdef AI2C_BIG_ENDIAN
	unsigned:28;         /*!< bits 31:4  reserved */
	unsigned depth:4;    /*!< bits  3:0  Number of bytes in
				master Transmit FIFO */
#else
	unsigned depth:4;    /*!< bits  3:0  Number of bytes in
				master Transmit FIFO */
	unsigned:28;         /*!< bits 31:4  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_mst_rx_fifo
 *  @brief   I2C Master Receive FIFO Status register (X7)
 */
struct ai2c_reg_i2c_x7_mst_rx_fifo {

#ifdef AI2C_BIG_ENDIAN
	unsigned:28;         /*!< bits 31:4  reserved */
	unsigned depth:4;    /*!< bits  3:0  Number of bytes in master
				Receive FIFO */
#else
	unsigned depth:4;    /*!< bits  3:0  Number of bytes in master
				Receive FIFO */
	unsigned:28;         /*!< bits 31:4  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_mst_int_enable
 *  @brief   I2C Master Interrupt Enable register (X7)
 */
struct ai2c_reg_i2c_x7_mst_int_enable {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;        /*!< bits 31:16  reserved */
	unsigned mie:12;    /*!< bits 15:4  Bits to enable individual
				interrupt resources */
	unsigned:4;         /*!< bits  3:0  reserved */
#else
	unsigned:4;         /*!< bits  3:0  reserved */
	unsigned mie:12;    /*!< bits 15:4  Bits to enable individual
				interrupt resources */
	unsigned:16;        /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_mst_int_status
 *  @brief   I2C Master Interrupt Status register (X7)
 */
struct ai2c_reg_i2c_x7_mst_int_status {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;         /*!< bits 31:16  reserved */
	unsigned itt:1;         /*!< bits 15     */
	unsigned itlm:1;         /*!< bits 14     */
	unsigned rfl:1;         /*!< bits 13     */
	unsigned tfl:1;         /*!< bits 12     */
	unsigned sns:1;         /*!< bits 11     */
	unsigned ss:1;         /*!< bits 10     */
	unsigned scc:1;         /*!< bits  9     */
	unsigned ip:1;         /*!< bits  8     */
	unsigned tss:1;         /*!< bits  7     */
	unsigned al:1;         /*!< bits  6     */
	unsigned nd:1;         /*!< bits  5     */
	unsigned na:1;         /*!< bits  4     */
	unsigned ts:1;         /*!< bits  3     */
	unsigned stp:1;         /*!< bits  2     */
	unsigned ttp:1;         /*!< bits  1     */
	unsigned rtp:1;         /*!< bits  0     */
#else
	unsigned rtp:1;         /*!< bits  0     */
	unsigned ttp:1;         /*!< bits  1     */
	unsigned stp:1;         /*!< bits  2     */
	unsigned ts:1;         /*!< bits  3     */
	unsigned na:1;         /*!< bits  4     */
	unsigned nd:1;         /*!< bits  5     */
	unsigned al:1;         /*!< bits  6     */
	unsigned tss:1;         /*!< bits  7     */
	unsigned ip:1;         /*!< bits  8     */
	unsigned scc:1;         /*!< bits  9     */
	unsigned ss:1;         /*!< bits 10     */
	unsigned sns:1;         /*!< bits 11     */
	unsigned tfl:1;         /*!< bits 12     */
	unsigned rfl:1;         /*!< bits 13     */
	unsigned itlm:1;         /*!< bits 14     */
	unsigned itt:1;         /*!< bits 15     */
	unsigned:16;         /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_mst_tx_bytes_xfrd
 *  @brief   I2C Master Transmit Bytes Transferred register (X7)
 */
struct ai2c_reg_i2c_x7_mst_tx_bytes_xfrd {

#ifdef AI2C_BIG_ENDIAN
	unsigned:24;         /*!< bits 31:8  reserved */
	unsigned num:8;      /*!< bits  7:0  Number of bytes
				transferred successfully */
#else
	unsigned num:8;      /*!< bits  7:0  Number of bytes
				transferred successfully */
	unsigned:24;         /*!< bits 31:8  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_mst_rx_bytes_xfrd
 *  @brief   I2C Master Receive Bytes Transferred register (X7)
 */
struct ai2c_reg_i2c_x7_mst_rx_bytes_xfrd {
#ifdef AI2C_BIG_ENDIAN
	unsigned:24;         /*!< bits 31:8  reserved */
	unsigned num:8;      /*!< bits  7:0  Number of bytes
				received successfully */
#else
	unsigned num:8;      /*!< bits  7:0  Number of bytes
				received successfully */
	unsigned:24;         /*!< bits 31:8  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_slv_addr_dec_ctl
 *  @brief   I2C Master Transmit Transfer Length register (X7)
 */
struct ai2c_reg_i2c_x7_slv_addr_dec_ctl {

#ifdef AI2C_BIG_ENDIAN
	unsigned:26;      /*!< bits 31:6  reserved */
	unsigned sa2m:8;  /*!< bits  5     7-bit or 10-bit slave addressing
				for Slave Addr Decoder 2 */
	unsigned sa2e:8;  /*!< bits  4     Slave State machine to ACK during
				addr phase of trans for Slave Addr 2 */
	unsigned sa1m:8;  /*!< bits  3     7-bit or 10-bit slave addressing
				for Slave Addr Decoder 1 */
	unsigned sa1e:8;  /*!< bits  2     Slave State machine to ACK during
				addr phase of trans for Slave Addr 1 */
	unsigned ogce:8;  /*!< bits  1     Slave State machine to ACK to
				General Call Address from its master */
	unsigned gce:8;   /*!< bits  0     Slave State machine to ACK to
				General Call Address from other masters */
#else
	unsigned gce:8;   /*!< bits  0     Slave State machine to ACK to
				General Call Address from other masters */
	unsigned ogce:8;  /*!< bits  1     Slave State machine to ACK to
				General Call Address from its master */
	unsigned sa1e:8;  /*!< bits  2     Slave State machine to ACK during
				addr phase of trans for Slave Addr 1 */
	unsigned sa1m:8;  /*!< bits  3     7-bit or 10-bit slave addressing
				for Slave Addr Decoder 1 */
	unsigned sa2e:8;  /*!< bits  4     Slave State machine to ACK during
				addr phase of trans for Slave Addr 2 */
	unsigned sa2m:8;  /*!< bits  5     7-bit or 10-bit slave addressing
				for Slave Addr Decoder 2 */
	unsigned:24;      /*!< bits 31:8  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_slv_addr_1
 *  @brief   I2C Slave Address 1 register (X7)
 */
struct ai2c_reg_i2c_x7_slv_addr_1 {

#ifdef AI2C_BIG_ENDIAN
	unsigned:22;         /*!< bits 31:10  reserved */
	unsigned sa1:10;         /*!< bits  9:0  7-bit or 10-bit address */
#else
	unsigned sa1:10;         /*!< bits  9:0  7-bit or 10-bit address */
	unsigned:22;         /*!< bits 31:10  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_slv_addr_2
 *  @brief   I2C Slave Address 2 register (X7)
 */
struct ai2c_reg_i2c_x7_slv_addr_2 {

#ifdef AI2C_BIG_ENDIAN
	unsigned:22;         /*!< bits 31:10  reserved */
	unsigned sa2:10;         /*!< bits  9:0  7-bit or 10-bit address */
#else
	unsigned sa2:10;         /*!< bits  9:0  7-bit or 10-bit address */
	unsigned:22;         /*!< bits 31:10  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_slv_rx_ctl
 *  @brief   I2C Slave Receive Control register (X7)
 */
struct ai2c_reg_i2c_x7_slv_rx_ctl {

#ifdef AI2C_BIG_ENDIAN
	unsigned:29;         /*!< bits 31:3  reserved */
	unsigned acgca:1;    /*!< bits  2     ACK data (write) phase to GCA */
	unsigned acsa2:1;    /*!< bits  1     ACK for data (write) phase
				to Slave Addr 2 */
	unsigned acsa1:1;    /*!< bits  0     ACK for data (write) phase
				to Slave Addr 1 */
#else
	unsigned acsa1:1;         /*!< bits  0     ACK for data (write) phase
					to Slave Addr 1 */
	unsigned acsa2:1;         /*!< bits  1     ACK for data (write) phase
					to Slave Addr 2 */
	unsigned acgca:1;         /*!< bits  2     ACK data (write) phase
					to GCA */
	unsigned:29;         /*!< bits 31:3  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_slv_data
 *  @brief   I2C Slave Data register (X7)
 */
struct ai2c_reg_i2c_x7_slv_data {

#ifdef AI2C_BIG_ENDIAN
	unsigned:23;         /*!< bits 31:9  reserved */
	unsigned pec:1;      /*!< bits  8     Set to subst PEC data
				for transmission */
	unsigned strd:8;     /*!< bits  7:0  Read data from top
				of Receive FIFO
				Write places transmit data into Slave Read
				Data Register. */
#else
	unsigned strd:8;     /*!< bits  7:0  Read data from top
				of Receive FIFO
				Write places transmit data into Slave Read
				Data Register. */
	unsigned pec:1;      /*!< bits  8     Set to subst PEC data
				for transmission */
	unsigned:24;         /*!< bits 31:8  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_slv_rx_fifo
 *  @brief   I2C Slave Receive FIFO Status register (X7)
 */
struct ai2c_reg_i2c_x7_slv_rx_fifo {

#ifdef AI2C_BIG_ENDIAN
	unsigned:24;         /*!< bits 31:8  reserved */
	unsigned pec:1;      /*!< bits  7     PEC good or not */
	unsigned stpc:1;     /*!< bits  6     Data transfer termined due to
				stop condition */
	unsigned rsc:1;      /*!< bits  4     Received byte was first after
				repeated start */
	unsigned strc:1;     /*!< bits  4     This is first data byte for a
				slave addr after start */
	unsigned tnak:1;     /*!< bits  3     NAK sent due to timeout */
	unsigned as:1;       /*!< bits  2     Data byte valid (0=ACK, 1=NAK) */
	unsigned dv:2;       /*!< bits  1:0  Data in FIFO is valid and for
				which slave
				*               00: empty
				*               01: Valid for slave addr 1
				*               10: Valid for slave addr 2
				*               11: General call address
						data valid */
#else
	unsigned dv:2;       /*!< bits  1:0  Data in FIFO is valid and for
				which slave
				*               00: empty
				*               01: Valid for slave addr 1
				*               10: Valid for slave addr 2
				*               11: General call address
						data valid */
	unsigned as:1;       /*!< bits  2     Data byte valid (0=ACK, 1=NAK) */
	unsigned tnak:1;     /*!< bits  3     NAK sent due to timeout */
	unsigned strc:1;     /*!< bits  4     This is first data byte for a
				slave addr after start */
	unsigned rsc:1;      /*!< bits  4     Received byte was first after
				repeated start */
	unsigned stpc:1;     /*!< bits  6     Data transfer termined due to
				stop condition */
	unsigned pec:1;      /*!< bits  7     PEC good or not */
	unsigned:24;         /*!< bits 31:8  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_slv_int_enable
 *  @brief   I2C Slave Interrupt Enable register (X7)
 */
struct ai2c_reg_i2c_x7_slv_int_enable {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;        /*!< bits 31:16  reserved */
	unsigned sie2:8;    /*!< bits 15:10  Bit enable individual sources in
				corresponding status reg */
	unsigned:1;         /*!< bits  9     reserved */
	unsigned sie1:5;    /*!< bits  8:4  Bit enable individual sources in
				corresponding status reg */
	unsigned:1;         /*!< bits  3     reserved */
	unsigned sie0:3;    /*!< bits  2:0  Bit enable individual sources in
				corresponding status reg */
#else
	unsigned sie0:3;    /*!< bits  2:0  Bit enable individual sources in
				corresponding status reg */
	unsigned:1;         /*!< bits  3     reserved */
	unsigned sie1:5;    /*!< bits  8:4  Bit enable individual sources in
				corresponding status reg */
	unsigned:1;         /*!< bits  9     reserved */
	unsigned sie2:8;    /*!< bits 15:10  Bit enable individual sources in
				corresponding status reg */
	unsigned:16;        /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_slv_int_status
 *  @brief   I2C Slave Interrupt Status register (X7)
 */
struct ai2c_reg_i2c_x7_slv_int_status {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;         /*!< bits 31:16  reserved */
	unsigned it:1;       /*!< bits 15     See spec for details */
	unsigned itls:1;     /*!< bits 14     See spec for details */
	unsigned srdre2:1;   /*!< bits 13     See spec for details */
	unsigned srat2:1;    /*!< bits 12     See spec for details */
	unsigned src2:1;     /*!< bits 11     See spec for details */
	unsigned srnd2:1;    /*!< bits 10     See spec for details */
	unsigned srrs2:1;    /*!< bits  9     See spec for details */
	unsigned srs2:1;     /*!< bits  8     See spec for details */
	unsigned srdre1:1;   /*!< bits  7     See spec for details */
	unsigned srat1:1;    /*!< bits  6     See spec for details */
	unsigned src1:1;     /*!< bits  5     See spec for details */
	unsigned srnd1:1;    /*!< bits  4     See spec for details */
	unsigned srrs1:1;    /*!< bits  3     See spec for details */
	unsigned srs1:1;     /*!< bits  2     See spec for details */
	unsigned wtc:1;      /*!< bits  1     See spec for details */
	unsigned rfh:1;      /*!< bits  0     See spec for details */
#else
	unsigned rfh:1;      /*!< bits  0     See spec for details */
	unsigned wtc:1;      /*!< bits  1     See spec for details */
	unsigned srs1:1;     /*!< bits  2     See spec for details */
	unsigned srrs1:1;    /*!< bits  3     See spec for details */
	unsigned srnd1:1;    /*!< bits  4     See spec for details */
	unsigned src1:1;     /*!< bits  5     See spec for details */
	unsigned srat1:1;    /*!< bits  6     See spec for details */
	unsigned srdre1:1;   /*!< bits  7     See spec for details */
	unsigned srs2:1;     /*!< bits  8     See spec for details */
	unsigned srrs2:1;    /*!< bits  9     See spec for details */
	unsigned srnd2:1;    /*!< bits 10     See spec for details */
	unsigned src2:1;     /*!< bits 11     See spec for details */
	unsigned srat2:1;    /*!< bits 12     See spec for details */
	unsigned srdre2:1;   /*!< bits 13     See spec for details */
	unsigned itls:1;     /*!< bits 14     See spec for details */
	unsigned it:1;       /*!< bits 15     See spec for details */
	unsigned:16;         /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_slv_read_dummy
 *  @brief   I2C Slave Read Dummy register (X7)
 */
struct ai2c_reg_i2c_x7_slv_read_dummy {

#ifdef AI2C_BIG_ENDIAN
	unsigned:23;      /*!< bits 31:9  reserved */
	unsigned srte:1;  /*!< bits  8    Disable/Enable slave read time-out */
	unsigned dd:8;    /*!< bits  7:0  Incoming slave read data */
#else
	unsigned dd:8;    /*!< bits  7:0  Incoming slave read data */
	unsigned srte:1;  /*!< bits  8    Disable/Enable slave read time-out */
	unsigned:23;      /*!< bits 31:9  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_scl_high_period
 *  @brief   I2C SCL High Period register (X7)
 */
struct ai2c_reg_i2c_x7_scl_high_period {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;         /*!< bits 31:16  reserved */
	unsigned period:16;  /*!< bits 15:0  (Desired SCL High Time /
				PCLK period) - 1 */
#else
	unsigned period:16;  /*!< bits 15:0  (Desired SCL High Time /r
i				 PCLK period) - 1 */
	unsigned:16;         /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_scl_low_period
 *  @brief   I2C SCL Low Period register (X7)
 */
struct ai2c_reg_i2c_x7_scl_low_period {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;         /*!< bits 31:16  reserved */
	unsigned period:16;  /*!< bits 15:0  (Desired SCL Low Time /
				PCLK period) - 1 */
#else
	unsigned period:16;  /*!< bits 15:0  (Desired SCL Low Time /
				PCLK period) - 1 */
	unsigned:16;         /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_spike_fltr_len
 *  @brief   I2C Spike Filter Length register (X7)
 */
struct ai2c_reg_i2c_x7_spike_fltr_len {

#ifdef AI2C_BIG_ENDIAN
	unsigned:27;         /*!< bits 31:5  reserved */
	unsigned fs:5;       /*!< bits  4:0  Num of spike filter stages */
#else
	unsigned fs:5;       /*!< bits  4:0  Num of spike filter stages */
	unsigned:27;         /*!< bits 31:5  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_scl_sda_setup_time
 *  @brief   I2C SDA Setup Time register (X7)
 */
struct ai2c_reg_i2c_x7_sda_setup_time {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;         /*!< bits 31:16  reserved */
	unsigned period:16;  /*!< bits 15:0  Suitable period */
#else
	unsigned period:16;  /*!< bits 15:0  Suitable period */
	unsigned:16;         /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_sda_hold_time
 *  @brief   I2C SDA Hold Time register (X7)
 */
struct ai2c_reg_i2c_x7_sda_hold_time {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;         /*!< bits 31:16  reserved */
	unsigned period:16;  /*!< bits 15:0  Suitable period */
#else
	unsigned period:16;  /*!< bits 15:0  Suitable period */
	unsigned:16;         /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_smb_alert
 *  @brief   I2C SMB Alert register (X7)
 */
struct ai2c_reg_i2c_x7_smb_alert {

#ifdef AI2C_BIG_ENDIAN
	unsigned:28;                      /*!< bits 31:4  reserved */
	unsigned slave_addr_sel:1;        /*!< bits  3     Select a slave
					   address register to use in alerts */
	unsigned smb_alert_pec_enable:1;  /*!< bits  2     Suitable period */
	unsigned smb_alert_enable:1;      /*!< bits  1     Suitable period */
	unsigned smb_alert:1;             /*!< bits  0     Suitable period */
#else
	unsigned smb_alert:1;             /*!< bits  0     Suitable period */
	unsigned smb_alert_enable:1;      /*!< bits  1     Suitable period */
	unsigned smb_alert_pec_enable:1;  /*!< bits  2     Suitable period */
	unsigned slave_addr_sel:1;        /*!< bits  3     Select a slave
					   address register to use in alerts */
	unsigned:28;                      /*!< bits 31:4  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_udid_w7
 *  @brief   I2C UDID 127:112 register (X7)
 */
struct ai2c_reg_i2c_x7_udid_w7 {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;               /*!< bits 31:16  reserved */
	unsigned addr_type:2;      /*!< bits 15:14  Types of address */
	unsigned:5;                /*!< bits 13:9  reserved */
	unsigned pec_supported:1;  /*!< bits  8     Ability to support
					slave PEC check/gen for non-ARP cmd */
	unsigned:2;                /*!< bits  7:6  reserved */
	unsigned version:3;        /*!< bits  5:3  UDID Version 1 */
	unsigned revision:3;       /*!< bits  2:0  Silicon Revision 0 */
#else
	unsigned revision:3;       /*!< bits  2:0  Silicon Revision 0 */
	unsigned version:3;        /*!< bits  5:3  UDID Version 1 */
	unsigned:2;                /*!< bits  7:6  reserved */
	unsigned pec_supported:1;  /*!< bits  8     Ability to support
					slave PEC check/gen for non-ARP cmd */
	unsigned:5;                /*!< bits 13:9  reserved */
	unsigned addr_type:2;      /*!< bits 15:14  Types of address */
	unsigned:16;               /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_udid_w6
 *  @brief   I2C UDID 111:96 register (X7)
 */
struct ai2c_reg_i2c_x7_udid_w6 {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;         /*!< bits 31:16  reserved */
	unsigned vendorId:16;         /*!< bits 15:0  Unique Vendor Id */
#else
	unsigned vendorId:16;         /*!< bits 15:0  Unique Vendor Id */
	unsigned:16;         /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_udid_w5
 *  @brief   I2C UDID 95:80 register (X7)
 */
struct ai2c_reg_i2c_x7_udid_w5 {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;         /*!< bits 31:16  reserved */
	unsigned deviceId:16;         /*!< bits 15:0  Unique Device Id */
#else
	unsigned deviceId:16;         /*!< bits 15:0  Unique Device Id */
	unsigned:16;         /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_udid_w4
 *  @brief   I2C UDID 79:64 register (X7)
 */
struct ai2c_reg_i2c_x7_udid_w4 {

#ifdef AI2C_BIG_ENDIAN
	unsigned:25;             /*!< bits 31:7  reserved */
	unsigned ipmi:1;         /*!< bits  6     Extra capab per IPMI specs */
	unsigned asf:1;          /*!< bits  5     Extra capab per ASF specs */
	unsigned oem:1;          /*!< bits  4     Extra capab per OEM
				subsystem specs */
	unsigned smbus_ver:4;    /*!< bits  3:0  SMBus Version 2.0 */
#else
	unsigned smbus_ver:4;   /*!< bits  3:0  SMBus Version 2.0 */
	unsigned oem:1;         /*!< bits  4     Extra capab per OEM
				subsystem specs */
	unsigned asf:1;         /*!< bits  5     Extra capab per ASF specs */
	unsigned ipmi:1;        /*!< bits  6     Extra capab per IPMI specs */
	unsigned:25;            /*!< bits 31:7  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_udid_w3
 *  @brief   I2C UDID 63:48 register (X7)
 */
struct ai2c_reg_i2c_x7_udid_w3 {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;             /*!< bits 31:16  reserved */
	unsigned subVendorId:16; /*!< bits 15:0  Subsystem Vendor Id */
#else
	unsigned subVendorId:16; /*!< bits 15:0  Subsystem Vendor Id */
	unsigned:16;             /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_udid_w2
 *  @brief   I2C UDID 47:32 register (X7)
 */
struct ai2c_reg_i2c_x7_udid_w2 {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;             /*!< bits 31:16  reserved */
	unsigned subDeviceId:16; /*!< bits 15:0  Subsystem Device Id */
#else
	unsigned subDeviceId:16; /*!< bits 15:0  Subsystem Device Id */
	unsigned:16;             /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_udid_w1
 *  @brief   I2C UDID 31:16 register (X7)
 */
struct ai2c_reg_i2c_x7_udid_w1 {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;         /*!< bits 31:16  reserved */
	unsigned vendorIDHI:16;
		/*!< bits 15:0  Upper 16-bits of 32-bit unique value
		*               to differentiate between the same
		*               type of device.  This field must be
		*               loaded to correct value before ARP
		*               is enabled. */
#else
	unsigned vendorIDHI:16;
		/*!< bits 15:0  Upper 16-bits of 32-bit unique value
		*               to differentiate between the same
		*               type of device.  This field must be
		*               loaded to correct value before ARP
		*               is enabled. */
	unsigned:16;         /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_udid_w0
 *  @brief   I2C UDID 15:0 register (X7)
 */
struct ai2c_reg_i2c_x7_udid_w0 {

#ifdef AI2C_BIG_ENDIAN
	unsigned:16;         /*!< bits 31:16  reserved */
	unsigned vendorIDLO:16;
		/*!< bits 15:0  Lower 16-bits of 32-bit unique value
		*               to differentiate between the same
		*               type of device.  This field must be
		*               loaded to correct value before ARP
		*               is enabled. */
#else
	unsigned vendorIDLO:16;
		/*!< bits 15:0  Lower 16-bits of 32-bit unique value
		*               to differentiate between the same
		*               type of device.  This field must be
		*               loaded to correct value before ARP
		*               is enabled. */
unsigned:16;         /*!< bits 31:16  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_arppec_cfg_stat
 *  @brief   I2C ARP/PEC Configuration & Status register (X7)
 */
struct ai2c_reg_i2c_x7_arppec_cfg_stat {

#ifdef AI2C_BIG_ENDIAN
	unsigned:26;                  /*!< bits 31:6  reserved */
	unsigned slavePECEnable:1;    /*!< bits  5     PEC gen/check
					enabled for non-ARP cmds for slave */
	unsigned masterPECEnable:1;   /*!< bits  4     PEC gen/check enabled
					for non-ARP cmds for master */
	unsigned PSAEnable:1;         /*!< bits  3     Persistent Slave
					Address Enabled */
	unsigned ARPEnable:1;         /*!< bits  2     SMBus logic may
					respond to ARP Slave Commands */
	unsigned AVFlag:1;            /*!< bits  2     Address valid
					for ARP slave commands */
	unsigned ARFlag:1;            /*!< bits  0     Device address
					resolved with ARP master */
#else
	unsigned ARFlag:1;            /*!< bits  0     Device address
					resolved with ARP master */
	unsigned AVFlag:1;            /*!< bits  2     Address valid
					for ARP slave commands */
	unsigned ARPEnable:1;         /*!< bits  2     SMBus logic may
					respond to ARP Slave Commands */
	unsigned PSAEnable:1;         /*!< bits  3     Persistent Slave
					Address Enabled */
	unsigned masterPECEnable:1;   /*!< bits  4     PEC gen/check enabled
					for non-ARP cmds for master */
	unsigned slavePECEnable:1;    /*!< bits  5     PEC gen/check enabled
					for non-ARP cmds for slave */
	unsigned:26;                  /*!< bits 31:6  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_slv_arp_int_enable
 *  @brief   I2C Slave ARP Interrupt Enable register (X7)
 */
struct ai2c_reg_i2c_x7_slv_arp_int_enable {

#ifdef AI2C_BIG_ENDIAN
	unsigned:25;         /*!< bits 31:7  reserved */
	unsigned arpie:7;    /*!< bits  6:0  Bit enable individual
				interrupt sources for ARP Slave Int Stat reg */
#else
	unsigned arpie:7;    /*!< bits  6:0  Bit enable individual interrupt
				sources for ARP Slave Int Stat reg */
	unsigned:25;         /*!< bits 31:7  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_slv_arp_int_status
 *  @brief   I2C Slave ARP Interrupt Status register (X7)
 */
struct ai2c_reg_i2c_x7_slv_arp_int_status {

#ifdef AI2C_BIG_ENDIAN
	unsigned:25;                  /*!< bits 31:7  reserved */
	unsigned SMBAlertResp:7;      /*!< bits  6     After receive/reply
					to Alert Response message */
	unsigned PrepARPRcv:7;        /*!< bits  5     When decode/receive
					a Prep to ARP command */
	unsigned RstDevRcv:7;         /*!< bits  4     When decode/receive a
					Reset Device General Command */
	unsigned GetUDIDRcv:7;        /*!< bits  3     After receive/reply to
					Get UDID General command */
	unsigned AssignAddrRcv:7;     /*!< bits  2     When receive an Assign
					Address command */
	unsigned GetUDIDDirectRcv:7;  /*!< bits  1     After receive/reply
					Get UDID Directed Command */
	unsigned ResetDevDirect:7;    /*!< bits  0     When slave receives
					areset device directed command */
#else
	unsigned ResetDevDirect:7;    /*!< bits  0     When slave receives
					areset device directed command */
	unsigned GetUDIDDirectRcv:7;  /*!< bits  1     After receive/reply
					Get UDID Directed Command */
	unsigned AssignAddrRcv:7;     /*!< bits  2     When receive an
					Assign Address command */
	unsigned GetUDIDRcv:7;        /*!< bits  3     After receive/reply
					to Get UDID General command */
	unsigned RstDevRcv:7;         /*!< bits  4     When decode/receive
					a Reset Device General Command */
	unsigned PrepARPRcv:7;        /*!< bits  5     When decode/receive
					a Prep to ARP command */
	unsigned SMBAlertResp:7;      /*!< bits  6     After receive/reply
					to Alert Response message */
	unsigned:25;                  /*!< bits 31:7  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_mst_arp_int_enable
 *  @brief   I2C Master ARP Interrupt Enable register (X7)
 */
struct ai2c_reg_i2c_x7_mst_arp_int_enable {

#ifdef AI2C_BIG_ENDIAN
	unsigned:30;         /*!< bits 31:2  reserved */
	unsigned armmstie:2;         /*!< bits  1:0  Bit enable individual
					interrupt sources for ARP Master
					Int Stat reg */
#else
	unsigned armmstie:2;         /*!< bits  1:0  Bit enable individual
					interrupt sources for ARP Master
					Int Stat reg */
	unsigned:30;         /*!< bits 31:2  reserved */
#endif
};

/*! @struct  ai2c_reg_i2c_x7_mst_arp_int_status
 *  @brief   I2C Master ARP Interrupt Status register (X7)
 */
struct ai2c_reg_i2c_x7_mst_arp_int_status {

#ifdef AI2C_BIG_ENDIAN
	unsigned:30;         /*!< bits 31:2  reserved */
	unsigned PECRdErr:1;         /*!< bits  1     Master Read msg with PECr
					enabled has detected a PEC error with
					Slave response */
	unsigned SMBAlert:1;         /*!< bits  0     State of SMB_Alert#
					signal on the SM Bus */
#else
	unsigned SMBAlert:1;         /*!< bits  0     State of SMB_Alert#r
					 signal on the SM Bus */
	unsigned PECRdErr:1;         /*!< bits  1     Master Read msg with
					PEC enabled has detected a PEC error
					with Slave response */
	unsigned:30;         /*!< bits 31:2  reserved */
#endif
};

#endif /* _AI2C_I2C_REGS_H_ */
