/********************************************************************************************************
 * @file    ux_ft_touch.h
 *
 * @brief   BIBOO UX: DUT-side touch sample source of the factory test (mouse
 *          firmware).
 *          While the touch event stream is active (TOUCH_START 0x20 ..
 *          TOUCH_STOP 0x21, handled in ux_ft_dut.c), ux_ft_touch_proc()
 *          samples the touch driver adapter every 20ms and feeds each sample
 *          to ux_ft_touch_poll(), which notifies it as a UX_FT_DUT_EVT_TOUCH
 *          (0x22) frame through ux_ft_dut_touch_report().
 *
 *          Driver adapter: ux_ft_touch_hw_read() is a WEAK hook; the weak
 *          default in ux_ft_touch.c generates a simulated sweep so the whole
 *          dongle + PC-tool chain can be verified without touch hardware.
 *          The real touch driver (I2C touch IC or chip-side ADC/QDEC) only
 *          has to provide a strong ux_ft_touch_hw_read() (same override
 *          pattern as drivers/B8x flash_write_status).
 *
 *          Wiring: dispatched by user_module_manage.c; switch is
 *          BIBOO_UX_FACTORY_TEST_DUT (set in the mouse AAA_app_config.h).
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#ifndef UX_FT_TOUCH_H_
#define UX_FT_TOUCH_H_

#include "tl_common.h"
#include "../user_module_manage.h"	/*BIBOO_UX_USER_MODULE master switch*/
#include "ux_ft_dut.h"				/*BIBOO_UX_FACTORY_TEST_DUT switch + touch API*/

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_FACTORY_TEST_DUT)

/* ================ configuration ================ */

/* sampling period while the touch stream is active (plan: 20..50ms) */
#define UX_FT_TOUCH_POLL_US			20000

/* ================ module entry (dispatched by user_module_manage.c) ================ */
void ux_ft_touch_init(void);
void ux_ft_touch_proc(void);

/* feed one touch sample into the running stream: checked against the
 * 0x20..0x21 window and forwarded as a 0x22 notify (sequence number is
 * maintained in ux_ft_dut.c). Called by ux_ft_touch_proc() after a
 * successful ux_ft_touch_hw_read(); a real driver may call it directly
 * (e.g. from the touch interrupt) as well. */
void ux_ft_touch_poll(u8 pressed, u16 pos);

/* touch driver adapter (WEAK, overridable by a strong definition):
 * return 1 when a fresh sample is available and fill
 * pressed (0 = finger up / != 0 = finger down) and pos (touch position).
 * Weak default (ux_ft_touch.c): simulated sweep, no hardware involved. */
u8 ux_ft_touch_hw_read(u8 *pressed, u16 *pos);

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_FACTORY_TEST_DUT */

#endif /* UX_FT_TOUCH_H_ */
