/********************************************************************************************************
 * @file    ux_host_link.h
 *
 * @brief   BIBOO UX module (dongle side): USB HID host communication link
 *          (dongle <-> PC host tool, e.g. BibooUxTool). Transport = USB HID
 *          Feature Report id 0x05 on the mouse interface, 7-byte payload,
 *          all over EP0 control transfers (wired in application/usbstd/usb.c):
 *            downlink: host SET_REPORT(Feature,0x05) -> ux_host_link_rx()
 *            uplink  : host GET_REPORT(Feature,0x05) -> ux_host_link_tx_buf()
 *          Payload protocol: [0]=cmd, [1..6]=command args / response data.
 *          Unknown commands keep the echo response (SET payload mirrored
 *          to the GET readback), so the legacy "SET -> GET compare" of the
 *          host tool keeps passing. Logic migrated from the bibo_feat_rx_buf
 *          spots in application/usbstd/usb.c.
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


#if (BIBOO_UX_HOST_LINK)

/*downlink (EP0 irq, usb.c SET_REPORT Feature 0x05): route a host command
 *payload (<=7 bytes, [0]=cmd) into the module; the response is prepared
 *synchronously (echo for unknown commands)*/
void ux_host_link_rx(u8 len, u8 *p);

/*uplink readback (EP0 irq, usb.c GET_REPORT Feature 0x05): pointer to the
 *7-byte response payload served to the host (echo of the last SET payload,
 *or the response of the last dispatched command / business tx)*/
u8  *ux_host_link_tx_buf(void);

/*business uplink (any context): queue a 7-byte response/data payload for
 *the host to read with its next GET_REPORT(Feature,0x05)*/
void ux_host_link_tx(u8 len, u8 *p);

void ux_host_link_init(void);
void ux_host_link_proc(void);	/*main loop: async command extension point*/

#endif /* BIBOO_UX_HOST_LINK */


#endif /* UX_HOST_LINK_H_ */
