/********************************************************************************************************
 * @file    ux_mouse_fwd.h
 *
 * @brief   BIBOO UX module (dongle side): forward the BLE mouse to USB.
 *          - auto pairing mode (power-on / after link loss, no button; only
 *            the mouse ADV signature is accepted)
 *          - enable HID report notify (CCC write) after SMP & SDP
 *          - convert the 6-byte BLE mouse report to 4-byte USB HID frames
 *          Logic migrated from vendor/ble_master_kma_dongle/{app.c,blm_host.c,
 *          blm_att.c}. Switch: BIBOO_UX_MOUSE_FWD (defined in the dongle
 *          project's app_config.h; undefined on the mouse project, where this
 *          module compiles to nothing). All entry points are dispatched from
 *          vendor/user/user_module_manage.c.
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#ifndef UX_MOUSE_FWD_H_
#define UX_MOUSE_FWD_H_

#include "tl_common.h"
#include "vendor/user/user_module_manage.h"	/*BIBOO UX: brings in the master switch BIBOO_UX_USER_MODULE*/


#if (BIBOO_UX_MOUSE_FWD)

/*BIBOO UX: auto pairing mode (set at power-on / after link loss, no button needed)*/
extern int central_auto_pairing_enable;
/*BIBOO UX: one-shot flag, suppress auto pairing for the disconnect caused by unpair*/
extern int ux_unpair_no_repair;
/*BIBOO UX diag: BLE mouse reports received at att_mouse (host link MSTAT 0x03,
 *see vendor/user/host_link/ux_host_link.c)*/
extern volatile u32 uxm_att_rx;
/*BIBOO UX diag: inter-arrival gaps measured in DONGLE time at the att_mouse
 *hook (MSTAT 0x03 group 2/3). Unlike the PC-side 0xE1 interval monitor these
 *are immune to the 50ms host-link read batching on the PC. Gaps > 300ms are
 *treated as stream breaks (pen lift / pause) and counted separately so idle
 *time does not pollute the stutter statistics.*/
#define UXM_GAP_BREAK_MS   300
#define UXM_GAP_SPIKE_MS   20
extern volatile u32 uxm_gap_cnt;        /*measured in-stream gaps (<=BREAK_MS)*/
extern volatile u32 uxm_gap_max_ms;     /*largest in-stream gap, ms*/
extern volatile u32 uxm_gap_over20_cnt; /*in-stream gaps > SPIKE_MS (the stutter metric)*/
extern volatile u32 uxm_gap_sum_ms;     /*sum of in-stream gaps (avg = sum/cnt)*/
extern volatile u32 uxm_gap_break_cnt;  /*stream breaks (> BREAK_MS), e.g. pen lift*/
void uxm_gap_reset(void);

void ux_mouse_fwd_init(void);
void ux_mouse_fwd_set_unpair_no_repair(void);
/* pa points to event_adv_report_t (kept as void* so this header needs no BLE stack headers) */
int  ux_mouse_fwd_adv_auto_pair(void *pa, s8 rssi);
void ux_mouse_fwd_disconnect_evt(u8 reason);
void ux_mouse_fwd_att_mouse(u8 *p);
void ux_mouse_fwd_notify_proc(void);

#endif /* BIBOO_UX_MOUSE_FWD */


#endif /* UX_MOUSE_FWD_H_ */
