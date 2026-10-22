/********************************************************************************************************
 * @file    blm_host.h
 *
 * @brief   This is the header file for BLE SDK
 *
 * @author  BLE GROUP
 * @date    06,2020
 *
 * @par     Copyright (c) 2020, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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
#ifndef APP_HOST_H_
#define APP_HOST_H_


/**
 * @brief      callback function of HCI Controller Event
 * @param[in]  h - HCI Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     0
 */
int 	controller_event_callback (u32 h, u8 *p, int n);

/**
 * @brief      callback function of L2CAP layer handle packet data
 * @param[in]  conn_handle - connect handle
 * @param[in]  raw_pkt - Pointer point to l2cap data packet
 * @return     0
 */
int 	app_l2cap_handler (u16 conn_handle, u8 *raw_pkt);

/**
 * @brief      callback function of smp finish
 * @param[in]  none
 * @return     0
 */
int 	app_host_smp_finish (void);

/**
 * @brief      update connection parameter in mainloop
 * @param[in]  none
 * @return     none
 */
void 	host_update_conn_proc(void);

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
/*BIBOO UX 2026-10-21: one-shot mouse DevInf_aaa read after connect + SDP.
 *Called from main_idle_loop() every iteration (NOT from
 *host_update_conn_proc() - that one only runs while a connection-parameter
 *update is pending, so hooking it there never fires).*/
void 	ux_devinf_proc(void);
u8   *ux_devinf_get(void);
u8    ux_devinf_ready(void);
void   ux_devinf_reset(void);
#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */

extern u32 host_update_conn_param_req;
extern int central_smp_pending;

extern int central_pairing_enable;
extern int central_unpair_enable;

/*BIBOO UX: central_auto_pairing_enable / ux_unpair_no_repair externs moved to
 *vendor/user/mouse_fwd/ux_mouse_fwd.h (dispatched via user_module_manage.c)*/

#if (BIBOO_UX_DEBUG)
/*debug: mouse link trace*/
extern volatile u8  dbg_smp_done;
extern volatile u8  dbg_sdp_pending;
extern volatile u32 dbg_noti_cnt;
extern volatile u32 dbg_mouse_cnt;
extern volatile u16 dbg_last_noti_handle;
extern volatile u32 dbg_sdkloop_us_max;
extern volatile u32 dbg_usbhs_us_max;
extern volatile u32 dbg_sdkloop_us;
extern volatile u32 dbg_usbhs_us;
void dbg_ble_status_print(void);
#endif

/*BIBOO UX: host_enable_notify_proc() / bibo_sdp_get_handle_cb() moved to
 *vendor/user/mouse_fwd (ux_mouse_fwd.h) and vendor/user/voice_dec (ux_voice_dec.h),
 *dispatched via user_module_proc()/user_module_init() in user_module_manage.c*/
#endif /* APP_HOST_H_ */
