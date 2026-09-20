/********************************************************************************************************
 * @file     emi.h
 *
 * @brief    for TLSR chips
 *
 * @author	 public@telink-semi.com;
 * @date     May. 12, 2018
 *
 * @par      Copyright (c) Telink Semiconductor (Shanghai) Co., Ltd.
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
/*
 * emi.h
 *
 *  Created on: 2018-5-31
 *      Author: Administrator
 */

#ifndef _AAA_EMI_H_
#define _AAA_EMI_H_


//singletone mode
void rf_emi_single_tone_aaa(RF_PowerTypeDef power_level, signed char rf_chn);

//rx function
void rf_emi_rx_aaa(RF_ModeTypeDef mode, signed char rf_chn);
void rf_emi_rx_loop_aaa(void);
unsigned int rf_emi_get_rxpkt_cnt_aaa(void);
char rf_emi_get_rssi_avg_aaa(void);
unsigned char *rf_emi_get_rxpkt_aaa(void);

//tx cd mode
void rf_emi_tx_continue_setup_aaa(RF_ModeTypeDef rf_mode, RF_PowerTypeDef power_level, signed char rf_chn, unsigned char pkt_type);
void rf_continue_mode_loop_aaa(void);
void rf_continue_mode_run_aaa(void);


//tx brust mode
void rf_emi_tx_brust_setup_aaa(RF_ModeTypeDef rf_mode, RF_PowerTypeDef power_level, signed char rf_chn, unsigned char pkt_type);
void rf_emi_tx_brust_loop_aaa(RF_ModeTypeDef rf_mode, unsigned char pkt_type);

//stop tx rx
void rf_emi_stop_aaa(void);

#endif /* EMI_H_ */
