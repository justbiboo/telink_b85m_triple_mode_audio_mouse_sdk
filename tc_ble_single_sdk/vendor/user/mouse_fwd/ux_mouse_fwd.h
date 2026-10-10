/********************************************************************************************************
 * @file    ux_mouse_fwd.h
 *
 * @brief   BIBOO UX module (dongle side): forward the BLE mouse to USB.
 *          - auto pairing mode (power-on / after link loss, no button; only
 *            the mouse ADV signature is accepted)
 *          - enable HID report notify (CCC write) after SMP & SDP
 *          - convert the 6-byte BLE mouse report to 4-byte USB HID frames
 *          Logic migrated from vendor/ble_master_kma_dongle/{app.c,blm_host.c,
 *          blm_att.c}. Switch: BIBOO_UX_ENABLE (defined in the dongle project's
 *          app_config.h; undefined on the mouse project, where this module
 *          compiles to nothing). All entry points are dispatched from
 *          vendor/user/user_module_manage.c.
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#ifndef UX_MOUSE_FWD_H_
#define UX_MOUSE_FWD_H_

#include "tl_common.h"
#include "vendor/user/user_module_manage.h"	/*BIBOO UX: brings in the master switch BIBOO_UX_USER_MODULE*/


#if (BIBOO_UX_ENABLE)

/*BIBOO UX: auto pairing mode (set at power-on / after link loss, no button needed)*/
extern int central_auto_pairing_enable;
/*BIBOO UX: one-shot flag, suppress auto pairing for the disconnect caused by unpair*/
extern int ux_unpair_no_repair;

void ux_mouse_fwd_init(void);
void ux_mouse_fwd_set_unpair_no_repair(void);
/* pa points to event_adv_report_t (kept as void* so this header needs no BLE stack headers) */
int  ux_mouse_fwd_adv_auto_pair(void *pa, s8 rssi);
void ux_mouse_fwd_disconnect_evt(u8 reason);
void ux_mouse_fwd_att_mouse(u8 *p);
void ux_mouse_fwd_notify_proc(void);

#endif /* BIBOO_UX_ENABLE */


#endif /* UX_MOUSE_FWD_H_ */
