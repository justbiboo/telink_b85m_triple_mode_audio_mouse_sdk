/********************************************************************************************************
 * @file    ux_user_data.h
 *
 * @brief   BIBOO UX: authorization-tick data layer (flash), ported from the
 *          mass-production dongle SDK (tc_ble_single_sdk_factory,
 *          vendor/user/user_data).
 *
 *            - ux_flash_dev_info_t (32 bytes, factory FLASH_DEV_INFO_AAA
 *              layout) stored @ 0x70000, rolling record writes with sector
 *              erase round-robin + read-back verify
 *            - authorization tick: default 10000, decremented one per
 *              successful DUT auth challenge (factory 0x77 protocol)
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#ifndef UX_USER_DATA_H_
#define UX_USER_DATA_H_

#include "tl_common.h"
#include "../user_module_manage.h"	/*BIBOO_UX_USER_MODULE master switch*/

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_AUTH)

/* authorization tick thresholds (factory user_data.h) */
#define DEFAULT_AUTH_TICK	10000	/*fresh device value*/
#define DEFAULT_AUTH_WARING	500		/*below: red LED blinks*/
#define DEFAULT_AUTH_OVER	2		/*below: red LED steady on (tick exhausted)*/

/* rolling-save record (flash 0x70000), must stay 32 bytes; binary layout kept
 * compatible with the factory FLASH_DEV_INFO_AAA */
typedef struct
{
	u32 dongle_id;				/*4*/
	u8  key[4];					/*8*/
	u32 authorization_tick;		/*12*/
	u8  mode;					/*13*/
	u8  os_mode;				/*14*/
	u8  mast_id;				/*15*/
	u8  temp[17];				/*32*/
} ux_flash_dev_info_t;

/* module entry (user_module_manage.c dispatcher) */
void ux_user_data_init(void);
void ux_user_data_proc(void);

u32  ux_user_data_get_auth_tick(void);
void ux_user_data_save_auth_tick(u32 tick);

/* connection up + tick available -> challenge the DUT with AUTH_CMD (0x77) */
void ux_user_data_set_authorization(void);

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_AUTH */

#endif /* UX_USER_DATA_H_ */
