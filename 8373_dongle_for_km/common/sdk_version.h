/********************************************************************************************************
 * @file    sdk_version.h
 *
 * @brief   This is the header file for B85m
 *
 * @author  Driver Group
 * @date    2022
 *
 * @par     Copyright (c) 2022, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/
#pragma once

/*
 * It is only used by SDK, and the customer is not allowed to modify it
 */
//---------------------------FW VERSION-----------------------------------
#if(_PRJ_EAGLET_MOUSE_)
#define FW_VERSION						0x0230
#define FW_VERSION_NUM					Mouse_V2.3.0
#elif (_PRJ_EAGLET_KEYBOARD_)
#define FW_VERSION						0x0230
#define FW_VERSION_NUM					Keyboard_V2.3.0
#elif (_PRJ_EAGLET_DONGLE_)
#define FW_VERSION						0x0230
#define FW_VERSION_NUM					Dongle_V2.3.0
#endif

/*
 * Release Tool need to change this macro to match the release version,
 * the replace rules is: "$$$B85m_driver_sdk_"#sdk_version_num"$$$", The "#sdk_version_num"
 * will replace with this macro value.
 */
#if _CHIP_IS_OTP_
#define B80_SDK_VERSION_NUM				B80_V2.0.0_P12_Otp
#define B80B_SDK_VERSION_NUM			B80B_V2.0.0_P12_Otp
#else
#define B80_SDK_VERSION_NUM				B80_V2.0.0_P12_Flash
#define B80B_SDK_VERSION_NUM			B80B_V2.0.0_P12_Flash
#endif

#if(MCU_CORE_B80B)
#define SDK_VERSION_NUM					B80B_SDK_VERSION_NUM
#else
#define SDK_VERSION_NUM					B80_SDK_VERSION_NUM
#endif

#if	ALL_SRAM_CODE
#define	SDK_VERSION1(sdk_version_num,fw_version_num)	"$$$"#sdk_version_num"_All_Sram_"#fw_version_num"$$$"
#else
#define	SDK_VERSION1(sdk_version_num,fw_version_num)	"$$$"#sdk_version_num"_"#fw_version_num"$$$"
#endif
#define	SDK_VERSION(sdk_version_num,fw_version_num)		SDK_VERSION1(sdk_version_num,fw_version_num)


