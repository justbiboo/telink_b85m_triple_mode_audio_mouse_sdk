/********************************************************************************************************
 * @file    user_module_manage.h
 *
 * @brief   BIBOO UX user module manager — umbrella entry of all custom (non-Telink) code.
 *          All new self-contained UX modules live under "vendor/user/<module>/"; the
 *          original Telink SDK files only keep minimal one-line hooks:
 *
 *            dongle: vendor/ble_master_kma_dongle/app.c
 *                    - user_init()       -> user_module_init()
 *                    - main_idle_loop()  -> user_module_proc()
 *            mouse : vendor/8258_dual_mouse/AAA_app.c
 *                    - main_loop()       -> user_module_proc()
 *
 *          Each hook is wrapped by "#if (BIBOO_UX_USER_MODULE)" so searching
 *          "BIBOO_UX_USER_MODULE" locates every integration point, and each
 *          sub-module inside user_module_manage.c is additionally gated by its
 *          own per-module switch (BIBOO_UX_MIC_STREAM, BIBOO_UX_MOUSE_FWD,
 *          BIBOO_UX_VOICE_DEC, BIBOO_UX_HOST_LINK, defined in each project's
 *          app_config header). Surgical patches that are deeply coupled with
 *          original SDK code (e.g. blm_att.c report parsing, usb.c forwarding,
 *          usbkb.c release ordering) intentionally stay in place, wrapped by
 *          the per-module switch they belong to.
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#ifndef USER_MODULE_MANAGE_H_
#define USER_MODULE_MANAGE_H_

#include "tl_common.h"


/* ================ BIBOO UX: vendor/user master switch ================
 * Master switch of the whole "vendor/user" directory.
 *   1 : user modules enabled (default)
 *   0 : all vendor/user code compiled out, original SDK behavior;
 *       the hooks below compile to nothing
 * Per-module switches (BIBOO_UX_MIC_STREAM, ...) still control each
 * module individually; set this to 0 only to disable ALL of them.
 * =================================================================== */
#ifndef BIBOO_UX_USER_MODULE
#define BIBOO_UX_USER_MODULE	1
#endif


/* user module init entry: called once at the end of user_init()
 * (wired on both the dongle (app.c user_init) and the mouse
 * (AAA_app.c user_init_normal)) */
void user_module_init(void);

/* user module loop entry: called in the firmware main loop */
void user_module_proc(void);


#if (BIBOO_UX_USER_MODULE)
/* ================ dongle-side event hooks ================
 * Dispatched to the dongle feature modules (mouse_fwd, voice_dec, host_link).
 * Every hook compiles to a no-op when the master switch BIBOO_UX_USER_MODULE
 * or the module's own switch is 0, so the call sites in the original SDK
 * files stay unconditional-looking and thin. Search "user_module_" in the
 * SDK files to locate every hook.
 * ========================================================= */

/* blm_le_adv_report_event_handle (blm_host.c): auto pairing ADV filter.
 * pa points to event_adv_report_t. returns 1 to trigger pairing. */
int  user_module_adv_auto_pair(void *pa, s8 rssi);

/* blm_disconnect_event_handle (blm_host.c): link lost -> re-enter auto
 * pairing, unless the disconnect was triggered by the unpair procedure */
void user_module_disconnect_evt(u8 reason);

/* host_pair_unpair_proc (app.c): mark the next disconnect as unpair-caused */
void user_module_unpair_triggered(void);

/* att_mouse (blm_att.c): convert the 6-byte BLE mouse report (btn+s16x+
 * s16y+wheel) to 4-byte USB HID frames and queue them for USB */
void user_module_att_mouse(u8 *p);

/* att_mouse (blm_att.c): mouse movement/button LED indication (green =
 * activity, yellow = button; report parsed per MOUSE_DATA_LEN_AAA),
 * implemented by the vendor/user/led module */
void user_module_att_mouse_led(u8 *p);

/* ux_mouse_fwd_att_mouse (ux_mouse_fwd.c): factory-test bypass hook -
 * while the factory-test mode is active the BLE mouse report is reported
 * to the PC (host link 0xE1 event) instead of being forwarded to USB.
 * returns 1 to skip the USB forwarding.
 * implemented by the vendor/user/factory_test module */
int  user_module_factory_test_bypass(u8 *p);

/* blm_host.c L2CAP notify dispatch: current value handle of the mouse
 * voice characteristic (my_Data 0xB03E); 0 = not discovered yet */
u16  user_module_voice_att_handle(void);

/* blm_host.c L2CAP notify dispatch: route an incoming voice frame */
void user_module_voice_rx(u16 len, u8 *p);

/* app_audio.c usb_endpoints_irq_handler/proc_audio: voice stream hooks */
u8   user_module_voice_active(void);
void user_module_voice_dec_proc(void);
void user_module_voice_usb(void);
#if (BIBOO_UX_SINE_TEST)
void user_module_voice_usb_sine(void);
#endif

void user_module_host_link_rx(u8 len, u8 *p);

#endif /* BIBOO_UX_USER_MODULE */


#endif /* USER_MODULE_MANAGE_H_ */
