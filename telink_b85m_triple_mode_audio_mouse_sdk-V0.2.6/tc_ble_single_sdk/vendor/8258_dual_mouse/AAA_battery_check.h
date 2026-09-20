/********************************************************************************************************
 * @file     AAA_battery_check.h
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

#ifndef BATTERY_CHECK_H_
#define BATTERY_CHECK_H_

#define	LOW_BATT_FLG					0x5A




//void battery_set_detect_enable (int en);
//int  battery_get_detect_enable (void);

u16  user_btn_adc_check_proc(u8 chn);
void battery_set_detect_enable(int en);



#endif /* APP_BATTDET_H_ */
