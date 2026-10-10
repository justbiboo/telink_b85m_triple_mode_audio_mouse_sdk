/********************************************************************************************************
 * @file    ux_mic_stream.h
 *
 * @brief   BIBOO UX module: BLE mouse continuous mic stream (mouse side).
 *          After BLE link-up with the dongle, automatically opens the AMIC and
 *          keeps streaming real microphone audio (16kHz/16bit/mono) as mSBC
 *          57B frames (7.5ms) via GATT notify (my_Data 0xB03E).
 *          Switch: BIBOO_UX_MIC_STREAM (defined in the mouse project's
 *          AAA_app_config.h; undefined on the dongle project, where this
 *          module compiles to nothing).
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#ifndef UX_MIC_STREAM_H_
#define UX_MIC_STREAM_H_

#include "tl_common.h"
#include "vendor/user/user_module_manage.h"	/*BIBOO UX: brings in the master switch BIBOO_UX_USER_MODULE*/


#if (BIBOO_UX_MIC_STREAM)

void ux_mic_stream_init(void);
void ux_mic_stream_proc(void);

#endif /* BIBOO_UX_MIC_STREAM */


#endif /* UX_MIC_STREAM_H_ */
