/********************************************************************************************************
 * @file    ux_spp.h
 *
 * @brief   BIBOO UX: SPP-like ATT data channel with the DUT mouse, ported from
 *          the mass-production dongle SDK (tc_ble_single_sdk_factory,
 *          vendor/user/spp/spp_device.c/h).
 *
 *          Characteristics (dongle = central):
 *            write  0x33F3 (C2S, dongle -> mouse, 20-byte frames)
 *            notify 0x33F4 (S2C, mouse -> dongle, dongle subscribes CCC)
 *          ATT handles are resolved by the central service discovery
 *          (vendor/common/simple_sdp.c -> spp_my_write_handler/spp_my_read_handler).
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#ifndef UX_SPP_H_
#define UX_SPP_H_

#include "tl_common.h"
#include "../user_module_manage.h"	/*BIBOO_UX_USER_MODULE master switch*/

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_SPP)

/* SPP ATT handles, resolved in app_service_discovery() (vendor/common/simple_sdp.c) */
extern u8 spp_my_write_handler;	/*0x33F3: dongle -> mouse (write without response)*/
extern u8 spp_my_read_handler;	/*0x33F4: mouse -> dongle (notify)*/

#define SPP_DEVICE_DATA_MAX_LEN		20

/* 20-byte frame layout (factory protocol) */
#define SPP_PKT_OFFS_CMD			0	/*[0] command*/
#define SPP_PKT_OFFS_ARG			1	/*[1] argument (e.g. auth Tick)*/
#define SPP_PKT_OFFS_RESULT			2	/*[2] result / state in responses*/

/* dongle -> mouse commands (factory protocol, implemented by the DUT mouse) */
typedef enum
{
	SPP_APP_MSG_AUTH_CMD			= 0x77,	/*auth challenge: [0]=0x77 [1]=Tick -> rsp [2]=AuthRspResult_e*/
	SPP_APP_MSG_GET_AUTH_STATE_CMD	= 0x78,	/*query auth state -> rsp [2]=auth_status_t*/
} SppAppMsgCmd_e;

/* [2] of the SPP_APP_MSG_AUTH_CMD response */
typedef enum
{
	AUTH_SEND_RSP_RESULT_FAIL	 = 0,
	AUTH_SEND_RSP_RESULT_ALREADY = 1,
	AUTH_SEND_RSP_RESULT_SUCCESS = 2,
	AUTH_SEND_RSP_RESULT_INIT	 = 0xcc,	/*dongle side internal: challenge sent, waiting response*/
} AuthRspResult_e;

/* [2] of the SPP_APP_MSG_GET_AUTH_STATE_CMD response */
typedef enum
{
	AUTH_VALID	 = 0,
	AUTH_INVALID = 1,
} auth_status_t;

typedef struct
{
	unsigned char rx_data[SPP_DEVICE_DATA_MAX_LEN];
	unsigned char tx_data[SPP_DEVICE_DATA_MAX_LEN];
} ux_spp_rf_data_t;

/* send result (factory SppAppDataSendResult) */
typedef enum
{
	SPP_APP_DATA_SEND_ERR	  = 0,
	SPP_APP_DATA_SEND_TOO_LONG = 1,
	SPP_APP_DATA_SEND_SUCCESS  = 2,
} SppAppDataSendResult_e;

/* user rx frame handler: called for frames not consumed by the built-in
 * factory auth logic (0x77/0x78) */
typedef bool (*ux_spp_rce_data_proc_t)(unsigned char *p_data, unsigned int len);

/* module entry (dispatched by user_module_manage.c) */
void ux_spp_init(void);
void ux_spp_proc(void);

/* send one frame to the DUT mouse on 0x33F3 (padded to 20 bytes by the write command) */
SppAppDataSendResult_e ux_spp_send(unsigned char *p_data, unsigned int len);

/* factory auth protocol */
void ux_spp_auth_cmd(u8 Tick);			/*AUTH_CMD 0x77: challenge with the current authorization tick*/
void ux_spp_get_auth_state_cmd(void);	/*GET_AUTH_STATE_CMD 0x78: query DUT auth state*/
u8   ux_spp_auth_state_get(void);		/*latest auth state (AuthRspResult_e)*/

/* rx entry: called by att_spp() (blm_att.c) on the 0x33F4 notify from the mouse */
int ux_spp_on_receive_data(unsigned char *p, u8 len);

/* register the production-test rx handler (see vendor/user/factory_test) */
void ux_spp_rce_data_proc_register(ux_spp_rce_data_proc_t handler);

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_SPP */

#endif /* UX_SPP_H_ */
