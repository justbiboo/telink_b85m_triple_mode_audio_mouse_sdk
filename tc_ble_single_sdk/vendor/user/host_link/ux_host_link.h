/********************************************************************************************************
 * @file    ux_host_link.h
 *
 * @brief   BIBOO UX module (dongle side): USB HID host communication link
 *          (dongle <-> PC host tool, e.g. BibooUxTool). The 7-byte payload
 *          uses a dedicated vendor HID interface on EP3:
 *            downlink: host SET_REPORT(Feature,0x09) -> ux_host_link_rx()
 *            uplink  : ux_host_link_tx() -> Input Report 0x08 (Interrupt IN)
 *          Payload protocol: [0]=cmd, [1..6]=command args / response data.
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

#define UX_HLINK_FEATURE_REPORT_ID     0x09
#define UX_HLINK_INPUT_REPORT_ID       0x08
#define UX_HLINK_REPORT_PAYLOAD_SIZE   7
#define UX_HLINK_REPORT_DESC_SIZE      38

u8 *ux_host_link_get_report_desc(void);
u16 ux_host_link_get_report_desc_size(void);
void ux_host_link_rx(u8 len, u8 *p);
void ux_host_link_tx(u8 len, u8 *p);

void ux_host_link_init(void);
void ux_host_link_proc(void);

#endif /* BIBOO_UX_HOST_LINK */


#endif /* UX_HOST_LINK_H_ */