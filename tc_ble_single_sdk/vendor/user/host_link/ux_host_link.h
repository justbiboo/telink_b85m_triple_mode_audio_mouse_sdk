/********************************************************************************************************
 * @file    ux_host_link.h
 *
 * @brief   BIBOO UX module (dongle side): USB HID host communication link
 *          (dongle <-> PC host tool, e.g. BibooUxTool). It uses a dedicated
 *          vendor HID interface on EP3:
 *            downlink: host SET_REPORT(Feature,0x09), 15-byte payload (EP0),
 *                      or WriteFile to Output Report 0x09 through the EP5
 *                      interrupt OUT endpoint (2026-10-22 fast path; the
 *                      Windows HID driver picks the OUT pipe when the report
 *                      descriptor declares an Output member, and hid.dll
 *                      WriteFile fails with ERROR_INVALID_FUNCTION on old
 *                      firmware without it - the PC tool probes once and
 *                      falls back to SET_REPORT, so both are compatible)
 *            uplink  : Input Report 0x08, 15-byte payload (Interrupt IN)
 *          Feature payload protocol: [0]=cmd, [1..14]=command args.
 *          Switch: BIBOO_UX_HOST_LINK (dongle app_config.h; undefined on the
 *          mouse project, where this header is empty).
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#ifndef UX_HOST_LINK_H_
#define UX_HOST_LINK_H_

#include "tl_common.h"
#include "vendor/user/user_module_manage.h"	/*BIBOO UX: brings in the master switch BIBOO_UX_USER_MODULE*/


#if (BIBOO_UX_USER_MODULE && BIBOO_UX_HOST_LINK)

#define UX_HLINK_FEATURE_REPORT_ID          0x09
#define UX_HLINK_INPUT_REPORT_ID            0x08
#define UX_HLINK_FEATURE_REPORT_PAYLOAD_SIZE 15
#define UX_HLINK_INPUT_REPORT_PAYLOAD_SIZE  15
#define UX_HLINK_REPORT_PAYLOAD_SIZE        UX_HLINK_FEATURE_REPORT_PAYLOAD_SIZE
/*38 (Feature 0x09 + Input 0x08) + 15 (Output 0x09 member, 2026-10-22)*/
#define UX_HLINK_REPORT_DESC_SIZE           53

u8 *ux_host_link_get_report_desc(void);
u16 ux_host_link_get_report_desc_size(void);
void ux_host_link_rx(u8 len, u8 *p);
void ux_host_link_tx(u8 len, u8 *p);

/*EP5 interrupt OUT downlink fast path (2026-10-22): called from usb_handle_irq()
 *when FLD_USB_EDP5_IRQ fires. Same 16B wire format [0x09][15B payload] as the
 *EP0 SET_REPORT path; feeds user_module_host_link_rx().*/
void ux_host_link_ep_out_irq(void);

void ux_host_link_init(void);
void ux_host_link_proc(void);

/*1 when everything queued for the uplink has really left (fifo drained and
 *the IN endpoint idle). vendor/user/self_ota uses this to hold its reboot
 *back until the COMMIT response is on the wire.*/
u8   ux_host_link_tx_idle(void);

/*att_mouse (blm_att.c): factory-test mouse-report mirror, called at the
 *top level of att_mouse() just like the LED consumer (user_module_att_mouse_led).
 *While the factory-test mode is active (BIBOO_UX_FACTORY_TEST, see
 *ux_ft_host_active()) the raw BLE report is wrapped into a 0xE1 event
 *(ux_ft_proto.h) and pushed to the PC through this link. Returns 1 when
 *the report has been mirrored (0xE1 sent), 0 in normal mode. This is a
 *pure mirror: the USB mouse forwarding in att_mouse() always runs, so
 *the PC mouse keeps working at any time (no bypass).*/
int  ux_host_link_att_mouse(u8 *p);

#endif /* BIBOO_UX_HOST_LINK */


#endif /* UX_HOST_LINK_H_ */