
/********************************************************************************************************
 * @file     aaa_24g_rf.h
 *
 * @brief    for TLSR chips
 *
 * @author	 telink
 * @date     Sep. 30, 2010
 *
 * @par      Copyright (c) 2010, Telink Semiconductor (Shanghai) Co., Ltd.
 *           All rights reserved.
 *
 *			 The information contained herein is confidential and proprietary property of Telink
 * 		     Semiconductor (Shanghai) Co., Ltd. and is available under the terms
 *			 of Commercial License Agreement between Telink Semiconductor (Shanghai)
 *			 Co., Ltd. and the licensee in separate contract or the terms described here-in.
 *           This heading MUST NOT be removed from this file.
 *
 * 			 Licensees are granted free, non-transferable use of the information in this
 *			 file under Mutual Non-Disclosure Agreement. NO WARRENTY of ANY KIND is provided.
 *
 *******************************************************************************************************/

#ifndef _RF_LINK_LAYER_24_H_
#define _RF_LINK_LAYER_24_H_

_attribute_data_retention_user extern u8	device_channel;
extern u8	get_next_channel_with_mask(u32 mask, u8 chn);
extern _attribute_ram_code_ void irq_device_tx(void);
extern _attribute_ram_code_ void irq_device_rx(void);
extern void ll_device_init(void);

typedef void (*task_when_rf_func)(void);
extern task_when_rf_func p_task_when_rf;

extern _attribute_ram_code_ void rf_drv_private_2m_init();
extern void set_pair_access_code(u32 code);
extern void set_data_access_code(u32 code);


extern void rf_set_access_code0(u32 code);
extern void rf_set_access_code1(u32 code);
extern u32 rf_get_access_code1();
extern  void rf_irq_src_allclr();
extern _attribute_ram_code_ void irq_handle_private_2m();
extern int rf_trx_state_set(RF_StatusTypeDef rf_status, signed char rf_channel);
extern void rf_acc_len_set(unsigned char len);
void rf_receiving_pipe_enble(u8 channel_mask);



#endif

