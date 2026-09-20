/********************************************************************************************************
 * @file     aaa_sensor.h
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

#ifndef MOUSE_SENSOR_H_
#define MOUSE_SENSOR_H_



#include "aaa_sensor_pix.h"
//#include "mouse.h"

//#include "mouse_pm.h"

#ifndef MOUSE_SENSOR_CUS
    #define MOUSE_SENSOR_CUS      1
#endif

#ifndef MOUSE_SENSOR_MOTION
    #define MOUSE_SENSOR_MOTION      1
#endif

#ifndef MOUSE_SENSOR_A3000_EN
    #define MOUSE_SENSOR_A3000_EN   0//((MCU_CORE_TYPE == MCU_CORE_8366) || 0)
#endif


#define SENSOR_DIRECTION_CLOCK_12	0
#define SENSOR_DIRECTION_CLOCK_3	1
#define SENSOR_DIRECTION_CLOCK_9	2
#define SENSOR_DIRECTION_CLOCK_6	3

#define SENSOR_MODE_POWERDOWN  0x40
#define SENSOR_MODE_WORKING    0x20
#define SENSOR_MODE_POWERUP    0x10
#define SENSOR_MODE_A3000      0x80

#define SENSOR_STATUS_CTRL  0xf0
#define SENSOR_TYPE_CTRL    0x0f

//---------------------------------------------
// Sensor type
//---------------------------------------------
#define SENSOR_3204       0x00
#define SENSOR_3204LL     0x01
#define SENSOR_3204UL     0x02
#define SENSOR_3205		  0x03

#define SENSOR_AN3205	  0x04

#define SENSOR_SIGMA_8630 0x05

#define SENSOR_PAW3207	  0x06

#define SENSOR_VT108	  0x07
#define SENSOR_M8589	  0x08
#define SENSOR_KA9		  0x09




#define SENSOR_PAW3212	  0x0a
#define SENSOR_OM16		  0x0b


#define SENSOR_YS8006	  0x0c
/*
 * IDENTIFY SENSOR YS8006
 *
 * (1)Product_id2 == 0x50
 * (2)REG17 == 0x5C && REG18 == 0x40 && REG19 == 0x74
 *
 */
#define SENSOR_YS8008	  0x0d
#define SENSOR_KA8G2		0x10
#define SENSOR_SW3851       0x11

/*
 * IDENTIFY SENSOR YS8008
 * (1)Product_id2 == 0xD0
 * (2)REG16 == 0xBD && REG19 == 0x55 && REG1B == 0x1D
 *
 */

#define SENSOR_A3000	  0x0e

#define SENSOR_PAN3805EK	 0X0F

#define LAST_SUPPORT_SENSOR SENSOR_A3000   //Must keep this as the last sensor type!!!!!

#define	SENSOR_IS_8589(sensor)     ( (sensor&SENSOR_TYPE_CTRL) == SENSOR_M8589)
#define SENSOR_IS_SIGMA_8640(sensor) ( (sensor&SENSOR_TYPE_CTRL) == SENSOR_SIGMA_8630)
#define SENSOR_IS_SUNPLUS(sensor)   ( (sensor&SENSOR_TYPE_CTRL) == SENSOR_M8589)
#define SENSOR_IS_XIWANG(sensor)    ( (sensor&SENSOR_TYPE_CTRL) == SENSOR_VT108)





extern int (*pf_sensor_motion_report)(signed char *pBuf, u32 no_overflow);

u32 mouse_sensor_sleep_wakeup(u8 *p_sensor, u8 *sleep, u32 wakeup);
/*#if 0
//sim sensor blinky when mouse is in sensor sleep mode
static inline u32 mouse_sensor_blinky_wkup(u32 blink_en)
{
    _attribute_data_retention_user  static u8 sns_blnky_cnt  = 0;
    if (blink_en)
        sns_blnky_cnt++;
    else
        sns_blnky_cnt = 0;
    return sns_blnky_cnt & 0x08;
}

static inline void mouse_sensor_dir_adjust(signed char *px, signed char *py, unsigned char sensor_dir)
{
    if (sensor_dir & SENSOR_X_REVERSE)
        *px = - *px;

    if (sensor_dir & SENSOR_Y_REVERSE)
        *py = - *py;

    if (sensor_dir & SENSOR_XY_SWITCH)
    {
        s8 tmp = *py;
        *py = *px;
        *px = tmp;
    }
}
#endif*/


#endif /* MOUSE_SENSOR_H_ */

