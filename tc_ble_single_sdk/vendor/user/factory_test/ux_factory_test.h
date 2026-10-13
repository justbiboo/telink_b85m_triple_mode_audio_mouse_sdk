/********************************************************************************************************
 * @file    ux_factory_test.h
 *
 * @brief   BIBOO UX module (dongle side): factory-test mode entry points and
 *          integration hooks. Switch: BIBOO_UX_FACTORY_TEST (dongle
 *          app_config.h; requires BIBOO_UX_HOST_LINK for the PC channel).
 *          Protocol shared with the DUT mouse firmware and the PC tool:
 *          ux_ft_proto.h.
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#ifndef UX_FACTORY_TEST_H_
#define UX_FACTORY_TEST_H_

#include "tl_common.h"
#include "../user_module_manage.h"	/*BIBOO_UX_USER_MODULE master switch*/

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_FACTORY_TEST)

/* module entry (dispatched by user_module_manage.c) */
void ux_factory_test_init(void);
void ux_factory_test_proc(void);

#if (BIBOO_UX_HOST_LINK)
/* host link command interception: called first in ux_host_link_dispatch()
 * (ux_host_link.c, EP0 irq context). Returns 1 when p[0] is a factory-test
 * command (0x10..0x13 / 0x40); the uplink response has been written into the
 * 15-byte r buffer (built here) or is deferred to ux_factory_test_proc().
 * Returns 0 for non-factory-test commands (legacy host link handling). */
int  ux_ft_host_cmd(const u8 *p, u8 *r);

/* 1 while the factory-test mode is active: the host link test beacon pauses
 * (otherwise its uplink frames would interleave with the FT events); the
 * host_link module also uses this to drive the 0xE1 mouse-report bypass
 * (ux_host_link_att_mouse, top-level att_mouse hook like the LED consumer) */
u8   ux_ft_host_active(void);
#endif /* BIBOO_UX_HOST_LINK */

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_FACTORY_TEST */

#endif /* UX_FACTORY_TEST_H_ */
