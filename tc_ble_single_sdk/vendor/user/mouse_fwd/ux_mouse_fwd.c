/********************************************************************************************************
 * @file    ux_mouse_fwd.c
 *
 * @brief   BIBOO UX module (dongle side): forward the BLE mouse to USB.
 *          Logic migrated from vendor/ble_master_kma_dongle/{app.c,blm_host.c,
 *          blm_att.c} (originally wrapped by "BIBOO_UX_ENABLE" at each site).
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#include "tl_common.h"
#include "ux_mouse_fwd.h"


#if (BIBOO_UX_USER_MODULE && BIBOO_UX_ENABLE)

#include "drivers.h"
#include "stack/ble/ble.h"
#include "vendor/common/tlkapi_debug.h"				/* tlkapi_printf */
#include "vendor/common/simple_sdp.h"				/* central_sdp_pending, conn_char_handler[] */
#include "../../ble_master_kma_dongle/blm_host.h"	/* central_smp_pending */
#include "application/app/usbmouse.h"				/* mouse_data_t, usbmouse_add_frame */


/*BIBOO UX: auto pairing mode.
 * - set to 1 at power-on (ux_mouse_fwd_init) and after every link loss
 *   (ux_mouse_fwd_disconnect_evt), so the dongle searches the mouse and
 *   pairs/connects automatically without pressing any button.
 * - while set, only the mouse ADV signature is accepted (see
 *   ux_mouse_fwd_adv_auto_pair) to avoid connecting to unrelated
 *   nearby BLE devices.
 * - cleared by the unpair procedure (see ux_mouse_fwd_set_unpair_no_repair)
 *   so the just-unpaired mouse is not immediately re-connected; it is set
 *   again on next power-on or on the next non-unpair link loss.*/
int central_auto_pairing_enable = 0;
/*one-shot: set when unpair triggers the disconnect, consumed in
 *ux_mouse_fwd_disconnect_evt to suppress auto pairing for that disconnect*/
int ux_unpair_no_repair = 0;


/*power-on: enter auto pairing mode automatically
 *(dispatched from user_module_init, dongle app.c user_init)*/
void ux_mouse_fwd_init(void)
{
	central_auto_pairing_enable = 1;
}

/*host_pair_unpair_proc (app.c): mark the disconnect triggered by unpair so
 *that the just-unpaired mouse is not immediately re-connected*/
void ux_mouse_fwd_set_unpair_no_repair(void)
{
	ux_unpair_no_repair = 1;
}

/*blm_le_adv_report_event_handle (blm_host.c): auto pairing trigger (power-on /
 *after link loss, no button). accept only the mouse ADV signature to avoid
 *connecting to unrelated nearby BLE devices. two fixed layouts on mouse side:
 *  - MS swift-pair layout : [3]=len3 [4]=0x19(Appearance) [5..6]=0xC2,0x03 (0x03C2 mouse)
 *  - plain layout         : [3]=len3 [4]=0x03(UUID16 list) [5..6]=0x12,0x18 (0x1812 HID)
 *returns 1 to trigger pairing, 0 to ignore this ADV*/
int ux_mouse_fwd_adv_auto_pair(void *pa_, s8 rssi)
{
	event_adv_report_t *pa = (event_adv_report_t *)pa_;
	if(central_auto_pairing_enable && (rssi > -56) && (pa->len >= 7))
	{
		if((pa->data[3] == 0x03) && (pa->data[4] == 0x19) && (pa->data[5] == 0xC2) && (pa->data[6] == 0x03))
		{
			return 1;  //Appearance = 0x03C2 (mouse)
		}
		else if((pa->data[3] == 0x03) && (pa->data[4] == 0x03) && (pa->data[5] == 0x12) && (pa->data[6] == 0x18))
		{
			return 1;  //uuid16 list contains 0x1812 (HID service)
		}
	}
	return 0;
}

/*blm_disconnect_event_handle (blm_host.c): link lost -> re-enter pairing mode
 *automatically, so the mouse can be found and re-connected/re-paired without
 *pressing any button.
 *exception: if this disconnect was triggered by the unpair procedure, stay
 *out of auto pairing (otherwise the just-unpaired mouse would be re-connected
 *immediately); auto pairing comes back on next power-on or next normal
 *link loss.*/
void ux_mouse_fwd_disconnect_evt(u8 reason)
{
	if(ux_unpair_no_repair)
	{
		ux_unpair_no_repair = 0;
		central_auto_pairing_enable = 0;
		#if (BIBOO_UX_DEBUG)
		tlkapi_printf(APP_LOG_EN, "[DBG] disconnect by unpair, auto pairing suppressed\n");
		#endif
	}
	else
	{
		central_auto_pairing_enable = 1;
		#if (BIBOO_UX_DEBUG)
		tlkapi_printf(APP_LOG_EN, "[DBG] link lost (r=%x), enter auto pairing\n", reason);
		#endif
	}
}

/*att_mouse (blm_att.c): convert the mouse BLE report to USB HID frames.
 *MOUSE_DATA_LEN_AAA = 6 (e.g. 8258_dual_mouse):
 *  p[0]:btn, p[1..2]:x(s16, little-endian), p[3..4]:y(s16), p[5]:wheel
 *USB HID mouse report is 4 bytes (btn, s8 x, s8 y, s8 wheel),
 *  split big moves into several frames when |x| or |y| > 127*/
void ux_mouse_fwd_att_mouse(u8 *p)
{
	#if (USB_MOUSE_ENABLE)
	#if (MOUSE_DATA_LEN_AAA == 6)
	mouse_data_t mouse_dat_report;
	mouse_dat_report.btn = p[0];
	mouse_dat_report.wheel = 0; /*wheel only attached to the last frame*/

	s8 wheel = (s8)p[5];
	s16 x = (s16)(p[1] | (p[2] << 8));
	s16 y = (s16)(p[3] | (p[4] << 8));

	while (x > 127)
	{
		mouse_dat_report.x = 127;  mouse_dat_report.y = 0;
		usbmouse_add_frame(&mouse_dat_report, 1);
		x -= 127;
	}
	while (x < -127)
	{
		mouse_dat_report.x = -127;  mouse_dat_report.y = 0;
		usbmouse_add_frame(&mouse_dat_report, 1);
		x += 127;
	}
	while (y > 127)
	{
		mouse_dat_report.x = 0;  mouse_dat_report.y = 127;
		usbmouse_add_frame(&mouse_dat_report, 1);
		y -= 127;
	}
	while (y < -127)
	{
		mouse_dat_report.x = 0;  mouse_dat_report.y = -127;
		usbmouse_add_frame(&mouse_dat_report, 1);
		y += 127;
	}

	mouse_dat_report.x = (s8)x;
	mouse_dat_report.y = (s8)y;
	mouse_dat_report.wheel = wheel; /*attach wheel to the last frame*/
	usbmouse_add_frame(&mouse_dat_report, 1);
	#endif
	#endif
}

/*main loop (dispatched from user_module_proc): enable slave HID report
 *notification by writing CCC (0x2902) after SMP & SDP done.
 *mouse side bls_att_pushNotifyData checks CCC, without this no notify can be received.*/
void ux_mouse_fwd_notify_proc(void)
{
	static u8 ccc_done = 0;

	if(blc_ll_getCurrentState() != BLS_LINK_STATE_CONN)
	{
		ccc_done = 0;  //reset for next connection
		return;
	}
	if(central_sdp_pending)  //SDP still ongoing
	{
		return;
	}
	#if (BLE_HOST_SMP_ENABLE)
	if(central_smp_pending)  //SMP pairing still ongoing
	{
		return;
	}
	#endif

	if(ccc_done)  //HID CCC already done
	{
		return;
	}
	if(!conn_char_handler[3] || !conn_char_handler[4] || !conn_char_handler[5])
	{
		return;  //handle not ready (SDP failed or not started)
	}

	/*CCC handle = report value handle + 1 (ATT table: prop, value, CCC, report-ref)*/
	u8 ccc[2] = {0x01, 0x00};
	u8 fail = 0;
	if(blc_gatt_pushWriteCommand(BLM_CONN_HANDLE, conn_char_handler[3] + 1, ccc, 2)){
		fail = 1;
	}
	if(blc_gatt_pushWriteCommand(BLM_CONN_HANDLE, conn_char_handler[4] + 1, ccc, 2)){
		fail = 1;
	}
	if(blc_gatt_pushWriteCommand(BLM_CONN_HANDLE, conn_char_handler[5] + 1, ccc, 2)){
		fail = 1;
	}

	if(!fail)
	{
		ccc_done = 1;
		#if (BIBOO_UX_DEBUG)
		tlkapi_printf(APP_LOG_EN, "[DBG] CCC enabled (H3=%x H4=%x H5=%x)\n",
			conn_char_handler[3], conn_char_handler[4], conn_char_handler[5]);
		#endif
	}
}

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_ENABLE */
