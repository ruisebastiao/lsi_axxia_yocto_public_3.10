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

/*! @file     ai2c_axi_timer_regs.h
 *  @brief    Constants, structs, and APIs used to configure the ACP Timer
 *            associated with various hardware modules and interfaces.
 */

#ifndef _AI2C_AXI_TIMER_REGS_H_
#define _AI2C_AXI_TIMER_REGS_H_

/*****************************************************************************
* Macros & Constants                                                         *
*****************************************************************************/

/*
 * Register Group offsets
 *
 * The ACP has 8 groups of these handles at offsets 0x00, 0x20, 0x40,
 * 0x60, 0x80, 0xA0, 0xC0, and 0xE0.
 */
#define AI2C_TIMER_SSP_OFFSET    (0x0000) /*!< Offset to start of
						SSP timer group */
#define AI2C_TIMER_I2C_OFFSET    (0x0020) /*!< Offset to start of
						I2C timer group */
#define AI2C_TIMER_UART0_CLK_OFFSET    (0x0040) /*!< Offset to start of
						UART0 Clk timer group */
#define AI2C_TIMER_UART1_CLK_OFFSET    (0x0060) /*!< Offset to start of
						UART1 Clk timer group */
#define AI2C_TIMER_WDOG_RESET_OFFSET   (0x0080) /*!< Offset to start of
						Watchdog Reset timer group */
#define AI2C_TIMER_GP5_OFFSET          (0x00A0) /*!< Offset to start of General
						Purpose Timer #5 group */
#define AI2C_TIMER_GP6_OFFSET          (0x00C0) /*!< Offset to start of General
						Purpose Timer #6 group */
#define AI2C_TIMER_GP7_OFFSET          (0x00E0) /*!< Offset to start of General
						Purpose Timer #7 group */

/*
 * Register handle offsets
 */

#define AI2C_REG_TIMER_TLV   (0x0000) /*!< Byte offset to Timer
					load value register */
#define AI2C_REG_TIMER_TV    (0x0004) /*!< Byte offset to Timer
					value register */
#define AI2C_REG_TIMER_TC    (0x0008) /*!< Byte offset to Timer
					control register */
#define AI2C_REG_TIMER_TIC   (0x000C) /*!< Byte offset to Timer
					interrupt clear register */
#define AI2C_REG_TIMER_RIS   (0x0010) /*!< Byte offset to Timer
					raw interrupt source register */
#define AI2C_REG_TIMER_IS    (0x0014) /*!< Byte offset to Timer
					interrupt source */
#define AI2C_REG_TIMER_BLV   (0x0018) /*!< Byte offset to Timer
					background load value register */


/*****************************************************************************
* Register Definitions                                                       *
*****************************************************************************/

/*! @struct  ai2c_reg_timer_tlv
 *  @brief   Timer load value register
 *           Let:
 *              Prescale=<1..4>
 *              Clk_Period=K MHz
 *              Output Freq=J MHz
 *
 *           Solve,
 *              Output Freq = (Clk_Period / Timer Prescale) /
 *                            (Timer Load Value + 1)
 *           for
 *              "Timer Load Value"
 *
 *           Example:
 *              Prescale=1, Clk_Period=400 MHz, Output Freq=4 MHz
 *              4 MHZ = (400 MHz / 1) / (TLV + 1)
 *              TLV = (400/4) - 1
 *                  = 99
 */
struct ai2c_reg_timer_tlv {

#ifdef AI2C_BIG_ENDIAN
	unsigned value:32;        /*!< bits 31:00  timer load value */
#else
	unsigned value:32;        /*!< bits 31:00  timer load value */
#endif
};

/*! @struct  ai2c_reg_timer_tv
 *  @brief   Timer value register
 */
struct ai2c_reg_timer_tv {

#ifdef AI2C_BIG_ENDIAN
	unsigned value:32;         /*!< bits 31:00  timer value */
#else
	unsigned value:32;         /*!< bits 31:00  timer value */
#endif
};

/*! @struct  ai2c_reg_timer_tc
 *  @brief   Timer control register
 */
struct ai2c_reg_timer_tc {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved_31_08:24;  /* bits 31:08  reserved */
	unsigned te:1;               /*!< bits  7     timer enable */
	unsigned rmode:1;            /*!< bits  6     mode */
	unsigned ie:1;               /*!< bits  5     interrupt enable */
	unsigned omode:1;            /*!< bits  4     output mode */
	unsigned pres:2;             /*!< bits  3: 2  pre-scaler */
	unsigned size:1;             /*!< bits  1     size */
	unsigned osm:1;              /*!< bits  0     one shot mode */
#else
	unsigned osm:1;              /*!< bits  0     one shot mode */
	unsigned size:1;             /*!< bits  1     size */
	unsigned pres:2;             /*!< bits  3: 2  pre-scaler */
	unsigned omode:1;            /*!< bits  4     output mode */
	unsigned ie:1;               /*!< bits  5     interrupt enable */
	unsigned rmode:1;            /*!< bits  6     mode */
	unsigned te:1;               /*!< bits  7     timer enable */
	unsigned reserved_31_08:24;  /* bits 31:08  reserved */
#endif
};

/*! @struct  ai2c_reg_timer_tic
 *  @brief   Timer interrupt clear register
 */
struct ai2c_reg_timer_tic {

#ifdef AI2C_BIG_ENDIAN
	unsigned value:31;      /* bits 31:01  reserved */
	unsigned tic:1;         /*!< bits  0     timer interrupt clear */
#else
	unsigned tic:1;         /*!< bits  0     timer interrupt clear */
	unsigned value:31;      /* bits 31:01  reserved */
#endif
};

/*! @struct  ai2c_reg_timer_ris
 *  @brief   Timer raw interrupt status register
 */
struct ai2c_reg_timer_ris {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved:31;   /* bits 31:01  reserved */
	unsigned ris:1;         /*!< bits  0     raw interrupt status */
#else
	unsigned ris:1;         /*!< bits  0     raw interrupt status */
	unsigned reserved:31;   /* bits 31:01  reserved */
#endif
};

/*! @struct  ai2c_reg_timer_is
 *  @brief   Timer interrupt status register
 */
struct ai2c_reg_timer_is {

#ifdef AI2C_BIG_ENDIAN
	unsigned reserved:31;  /* bits 31:01  reserved */
	unsigned is:1;         /*!< bits  0 interrupt status */
#else
	unsigned is:1;         /*!< bits  0 interrupt status */
	unsigned reserved:31;   /* bits 31:01 reserved */
#endif
};

/*! @struct  ai2c_reg_timer_blv
 *  @brief   Timer background load value register
 */
struct ai2c_reg_timer_blv {

#ifdef AI2C_BIG_ENDIAN
	unsigned value:32;     /*!< bits 31:00  background load value */
#else
	unsigned value:32;     /*!< bits 31:00  background load value */
#endif
};

#endif /* _AI2C_AXI_TIMER_REGS_H_ */
