/**************************************************************************
 **                                                                       *
 **   LSI CONFIDENTIAL                                                    *
 **                           PROPRIETARY NOTE                            *
 **                                                                       *
 **    This software contains information confidential and proprietary    *
 **    to LSI Inc.  It shall not be reproduced in whole or in             *
 **    part, or transferred to other documents, or disclosed to third     *
 **    parties, or used for any purpose other than that for which it was  *
 **    obtained, without the prior written consent of LSI Inc.            *
 **    (c) 2001-2013, LSI Inc.  All rights reserved.                      *
 **                                                                       *
 **  **********************************************************************/

#ifndef _AI2C_CFG_NODE_REGS_H_
#define _AI2C_CFG_NODE_REGS_H_

/*! @struct ai2c_cfg_node_node_info_0_r_t
 *  @brief CFG Node Info Register 0
 *  @details This register contains the module type and module revision
 *  for the module containing this CFG Node.
 */
struct ai2c_cfg_node_node_info_0_r {

#ifdef AI2C_BIG_ENDIAN
	unsigned      module_type:16;
	unsigned      module_revision:16;
#else    /* Little Endian */
	unsigned      module_revision:16;
	unsigned      module_type:16;
#endif
};

/*! @struct ai2c_cfg_node_node_info_1_r
 *  @brief CFG Node Info Register 1
 *  @details This read-only register contains the module instance
 *  and lower 24 bits of the module info field for the module
 *  containing this CFG Node.
 */
struct ai2c_cfg_node_node_info_1_r {

#ifdef AI2C_BIG_ENDIAN
	unsigned      module_info:24;
	unsigned      module_instance:8;
#else    /* Little Endian */
	unsigned      module_instance:8;
	unsigned      module_info:24;
#endif
};

/*! @struct ai2c_cfg_node_node_info_2_r
 *  @brief CFG Node Info Register 2
 *  @details This read-only register contains bits 55:24 of the module info field for the module containing this CFG Node.
 */
struct ai2c_cfg_node_node_info_2_r {
	unsigned  int   module_info;
};

/*! @struct ai2c_cfg_node_node_info_3_r
 *  @brief CFG Node Info Register 3
 *  @details This read-only register contains bits 87:56 of the module info field for the module containing this CFG Node.
 */
struct ai2c_cfg_node_node_info_3_r {
	unsigned  int               module_info;
};

/*! @struct ai2c_cfg_node_node_cfg_r
 *  @brief CFG Node Configuration Register
 *  @details This register contains fields that control the operation of the CFG node.
 */
struct ai2c_cfg_node_node_cfg_r {

#ifdef AI2C_BIG_ENDIAN
	unsigned      fpga:1;
	unsigned      reserved0:26;
	unsigned      opt_fill:1;
	unsigned      clk_apb_sel:2;
	unsigned      parity_enable:1;
	unsigned      parity_odd:1;
#else    /* Little Endian */
	unsigned      parity_odd:1;
	unsigned      parity_enable:1;
	unsigned      clk_apb_sel:2;
	unsigned      opt_fill:1;
	unsigned      reserved0:26;
	unsigned      fpga:1;
#endif
};

/*! @struct ai2c_cfg_node_write_err_addr_r
 *  @brief CFG Node Write Error Address Register
 *  @details This read-only register holds the address associated with the first write error for the last write command.
 */
struct ai2c_cfg_node_write_err_addr_r {
	unsigned  int             error_address;
};

/*! @struct ai2c_cfg_node_node_error_r
 *  @brief CFG Node Error Register
 *  @details This register holds the sticky errors detected by the CFG node.
 */
struct ai2c_cfg_node_node_error_r {

#ifdef AI2C_BIG_ENDIAN
	unsigned      reserved0:26;
	unsigned      node_error:1;
	unsigned      parity_error:1;
	unsigned      afifo_overflow:1;
	unsigned      afifo_underflow:1;
	unsigned      dfifo_overflow:1;
	unsigned      dfifo_underflow:1;
#else    /* Little Endian */
	unsigned      dfifo_underflow:1;
	unsigned      dfifo_overflow:1;
	unsigned      afifo_underflow:1;
	unsigned      afifo_overflow:1;
	unsigned      parity_error:1;
	unsigned      node_error:1;
	unsigned      reserved0:26;
#endif
};

/*! @struct ai2c_cfg_node_node_error_data_r
 *  @brief CFG Node Error Data Register
 *  @details This register holds the error code associated with the first protocol error detected by the CFG node.
 */
struct ai2c_cfg_node_node_error_data_r {

#ifdef AI2C_BIG_ENDIAN
	unsigned      reserved0:28;
	unsigned      error_code:4;
#else    /* Little Endian */
	unsigned      error_code:4;
	unsigned      reserved0:28;
#endif
};

/*! @struct ai2c_cfg_node_node_scratch_r
 *  @brief CFG Node Scratch Register
 *  @details This register is a scratch location for software's use.
 */
struct ai2c_cfg_node_node_scratch_r {
	unsigned int scratch;
};

#endif /* _AI2C_CFG_NODE_REGS_H_ */
