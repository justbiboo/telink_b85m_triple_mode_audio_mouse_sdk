/********************************************************************************************************
 * @file    blm_ota.c
 *
 * @brief   This is the source file for BLE SDK
 *
 * @author  BLE GROUP
 * @date    06,2020
 *
 * @par     Copyright (c) 2020, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "vendor/user/user_module_manage.h"	/*BIBOO UX: defines the BIBOO_UX_USER_MODULE master switch (vendor/user/user_module_manage.h). MUST stay before the first BIBOO_UX_* gate below - every BIBOO block in this file silently collapses to the stock-SDK path (e.g. master_ota_result missing at link time) when this header has not been seen yet.*/
#include "app.h"
#include "blm_host.h"


extern u8 read_by_type_req_uuidLen;
extern u8 read_by_type_req_uuid[16];
extern void host_att_set_current_readByTypeReq_uuid(u8 *uuid, u8 uuid_len);

extern const u8 my_OtaUUID[16];

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
/*BIBOO UX 2026-10-20: PC-fed stream source (ux_ota_slot.c). When
 *ota_stream_total != 0 the image arrives over the USB host link instead
 *of dongle flash: proc_ota pulls 16B blocks from the stream fifo in step
 *6 and the image never touches flash. See ux_ota_slot.h. */
extern u32 ota_stream_total;
/*BIBOO UX 2026-10-21: get16 stages a block but does not consume it - the
 *caller commits only once the push was accepted (see the .c). That is what
 *makes a rejected blc_gatt_pushWriteCommand retryable instead of silently
 *dropping 16 bytes and shifting the whole image by one slot.*/
extern int ux_ota_stream_get16(u8 *dst);	/*1 = dst filled with a full 16B block*/
extern void ux_ota_stream_commit(void);		/*after BLE_SUCCESS: consume it*/
extern u8 ux_ota_stream_eof(void);		/*1 = PC sent SLOT_END + everything pushed*/
extern u8 ux_ota_stream_ready(void);		/*1 = a full block can be built now*/
extern u32 ux_ota_stream_pending(void);	/*queued+staged bytes (stall diag)*/

/*BIBOO UX 2026-10-20: OTA characteristic handle already resolved by the
 *connect-time service discovery (simple_sdp.c app_service_discovery ->
 *conn_char_handler[2], matched on the same 128-bit UUID TELINK_SPP_DATA_OTA
 *the read-by-type request in proc_ota step 2 looks for). Reusing it skips
 *an ATT round trip the 8208 mouse did not answer (field log 20:51: the
 *session sat at discovery step 3 for 10s until the rx watchdog aborted).
 *[2] holds the characteristic VALUE handle - exactly the attribute the
 *write commands below must target (field log 21:17: the shipping
 *Biboo_8208_Dual_Mode_Origin mouse resolves it to 0x000E - that project
 *puts the OTA service BEFORE the battery service, unlike the
 *tc_ble_single_sdk reference table where it sits at 0x12).
 *NOTE: simple_sdp.c stores handles in a u8 array - fine for the current
 *mouse tables (both < 255); a future table past handle 255 would need
 *that array widened. */
extern u8 conn_char_handler[];
#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */


int master_ota_test_mode = 0;

#if(BLE_MASTER_OTA_ENABLE)



#define FW_SIZE_MAX			128  //128K
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
/*BIBOO UX: was 20s. The dongle pushes as fast as the link allows (TX FIFO
 *paced, no artificial throttle) - a 128K image needs ~5000+ packets and can
 *approach 20s on a busy 2.4G band. The slave budget is 60s
 *(blc_ota_setOtaProcessTimeout(60)), so match it here. */
#define OTA_TIMEOUT_S		60
#else
#define OTA_TIMEOUT_S		20
#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */

#define LED_INTERVAL_US  	400000
#define OTA_INTERVAL_US  	500000


#define OTA_STEP_IDLE						0
#define	OTA_STEP_UPDATE_CONN				1



u16 slave_ota_handle = 0;

typedef struct{
	u16 adr_index;
	u8	data[16];
	u16 crc_16;
}rf_packet_att_ota_data_t;


u32 flash_adr_ota_master = 0x20000;
u8 *p_firmware;
u32 n_firmware = 0;
u32 ota_adr = 0;


u32 ota_mode_begin_tick;


int master_ota_cmd;
u32 led_indicate_begin_tick;
u32 led_indicate_gpio;
u32 ota_result_tick;

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
/*BIBOO UX: master OTA result for the PC tool (polled via the ota_slot
 *status command): 0 = none/running, 1 = success, 2 = fail. Reset by the
 *trigger, latched in ota_set_result() below. */
int master_ota_result = 0;
#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */

int host_ota_start = 0;
int host_ota_update_pending;

#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
/*BIBOO UX 2026-10-20: bounded ATT discovery for the OTA characteristic
 *(proc_ota step 3). The slave can answer a read-by-type request with an
 *ATT_ERROR_RSP, or with nothing at all - blm_host.c had no handler for
 *either, so the original code sat at step 3 forever (field log 20:51:
 *10s of "ota step 3" until the rx watchdog aborted the session). Retry
 *the request twice (2s apart), then fail the session so the PC gets a
 *latched result instead of a silent hang. */
static u32 ota_disc_tick;
static u8  ota_disc_tries;
#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */

/**
 * @brief		this function serves to use led status to show the ota result
 * @param[in]	success - led status indicate
 * @return      none
 */
void ota_set_result(int success)
{
#if (UI_LED_ENABLE)
	if(master_ota_cmd == 1){
		led_indicate_gpio = GPIO_LED_BLUE;
	}
	else{
		led_indicate_gpio = GPIO_LED_GREEN;
	}
#endif

	if(success){
		led_indicate_begin_tick = clock_time() | 1;
	}
	else{
		led_indicate_begin_tick = 0;
	}

#if (UI_LED_ENABLE)
	gpio_write(led_indicate_gpio,!LED_ON_LEVEL);  //ota fail
#endif

	host_ota_update_pending = 0;

	host_ota_start = 0;
	master_ota_cmd = 0;
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
	master_ota_result = success ? 1 : 2;	/*BIBOO UX: latch for the PC tool*/
#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */
	ota_result_tick = clock_time() | 1;

	//write_reg8(reg_cmd_result,status);  //1 OK  0 fail
	//SET_CMD_DONE;
}

/*Set OTA process timeout*/
u8 adr_index_max_l;
u8 adr_index_max_h;

/**
 * @brief		this function serves to use button to trigger the ota start
 * @param[in]	button0_press_flag
 * @param[in]	button1_press_flag
 * @return      none
 */
void host_button_trigger_ota_start(int button0_press_flag, int button1_press_flag)
{
	if(blc_ll_getCurrentState() == BLS_LINK_STATE_CONN){
		if(button0_press_flag){
			master_ota_cmd = 1;
		}
		else if(button1_press_flag){
			master_ota_cmd = 2;
		}
	}
}



/**
 * @brief		this function serves to update connection parameter
 *              connection_interval = latency *interval + interval
 * @param[in]	interval - interval * 1.25ms
 * @param[in]	latency
 * @param[in]	timeout
 * @return      none
 */
void host_ota_update_conn_complete( u16 interval, u16 latency, u16 timeout )
{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
	/*BIBOO UX 2026-10-20: the original code advanced only on an exact
	 * (interval 8, latency 0, timeout 200) match. The requested update is
	 * a pure optimization - the dongle-mouse link already runs at interval
	 * 8 / latency 0 - but if the slave rejects the update, or the complete
	 * event carries different params, host_ota_start stayed at 1 forever
	 * and the master OTA could never start (field log 2026-10-20 20:32:
	 * 10s at step 1 until the rx watchdog aborted the session). Any
	 * completed update while the OTA one is pending is good enough to
	 * move on to the ATT discovery. */
	if(host_ota_update_pending == 2){
		host_ota_update_pending = 0;

		if(host_ota_start == 1){
			host_ota_start = 2;
			ota_disc_tries = 0;	/*fresh discovery window, see step 3*/
			printf("[blm_ota] conn update done (int %d lat %d to %d), start discovery\r\n",
				   interval, latency, timeout);
		}
	}
#else
	if(host_ota_update_pending == 2){
		host_ota_update_pending = 0;

		if(interval==8 && latency==0 && timeout==200){
			if(host_ota_start == 1){
				host_ota_start = 2;
			}
		}
	}
#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */
}




/**
 * @brief		this function serves to find ota slave attribute handle
 * @param[in]	p - pointer to point the l2cap data packet
 * @return      none
 */
void host_find_slave_ota_attHandle(u8 *p)
{
	if(host_ota_start == 3 && read_by_type_req_uuidLen == 16 && !memcmp(read_by_type_req_uuid, my_OtaUUID, 16) )
	{
		rf_pkt_att_readByTypeRsp_t *ptr = (rf_pkt_att_readByTypeRsp_t *)p;
		slave_ota_handle = ptr->data[0] | ptr->data[1]<<8;

		host_ota_start = 4;
		printf("[blm_ota] slave OTA write handle 0x%04X\r\n", slave_ota_handle);
	}
}



u32 otaStart_cmd_tick;
u32 ota_lastData_tick;

/**
 * @brief		ota proc in main loop
 * @param[in]	none
 * @return      none
 */
void proc_ota (void)
{


	if(host_ota_update_pending == 1 && !host_update_conn_param_req && !central_smp_pending && !central_sdp_pending)
	{

		if(blc_ll_getCurrentState() == BLS_LINK_STATE_CONN){  //still in connection state
			//10ms interval,  latency = 0   timeout = 6S
			/*BIBOO UX 2026-10-20 (field log 21:17): was timeout=200 (2S).
			 *The dongle-mouse link ALREADY runs at interval 8 (10ms), so
			 *this update gains nothing on speed - it only used to HALVE the
			 *supervision timeout (4S -> 2S). The 8208 mouse does flash work
			 *when the OTA starts (area erase / first page write), its BLE
			 *stack stalls for a moment, the push engine gates on the TX
			 *fifo, and with only 2S of supervision the link died ~2s into
			 *the very first push (attempt 1: 8 PDUs, then terminate). The
			 *mouse's own preferred supervision timeout is 6S (its GAP conn
			 *params) and the PC-direct OTA channel runs at that value with
			 *no drops - match it here. If the mouse rejects the update the
			 *step-1 1.5s fallback advances anyway. */
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
			blm_ll_updateConnection (cur_conn_device.conn_handle,
										8, 8, 0,  600,
											  0, 0 );
#else
			blm_ll_updateConnection (cur_conn_device.conn_handle,
								8, 8, 0,  200,
									  0, 0 );
			#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */

			host_ota_update_pending = 2;

		}
	}




	static u32 ota_begin_tick;
    static int led_step = 0;


	//enter OTA mode,  2 led shine for 3 times
    if(master_ota_test_mode == 1)
    {
#if (UI_LED_ENABLE)
    	if(led_step==0 && clock_time_exceed(ota_mode_begin_tick, OTA_INTERVAL_US)){
    		gpio_write(GPIO_LED_BLUE, LED_ON_LEVEL);
    		gpio_write(GPIO_LED_GREEN, LED_ON_LEVEL);
    		led_step = 1;
    	}
    	else if(led_step==1 && clock_time_exceed(ota_mode_begin_tick, OTA_INTERVAL_US*2)){
    		gpio_write(GPIO_LED_BLUE, !LED_ON_LEVEL);
    		gpio_write(GPIO_LED_GREEN, !LED_ON_LEVEL);
    		led_step = 2;
    	}
    	else if(led_step==2 && clock_time_exceed(ota_mode_begin_tick, OTA_INTERVAL_US*3)){
    		gpio_write(GPIO_LED_BLUE, LED_ON_LEVEL);
    		gpio_write(GPIO_LED_GREEN, LED_ON_LEVEL);
    		led_step = 3;
    	}
    	else if(led_step==3 && clock_time_exceed(ota_mode_begin_tick, OTA_INTERVAL_US*4)){
    		gpio_write(GPIO_LED_BLUE, !LED_ON_LEVEL);
    		gpio_write(GPIO_LED_GREEN, !LED_ON_LEVEL);
    		led_step = 4;
    	}
    	else if(led_step==4 && clock_time_exceed(ota_mode_begin_tick, OTA_INTERVAL_US*5)){
    		gpio_write(GPIO_LED_BLUE, LED_ON_LEVEL);
    		gpio_write(GPIO_LED_GREEN, LED_ON_LEVEL);
    		led_step = 5;
    	}
    	else if(led_step==5 && clock_time_exceed(ota_mode_begin_tick, OTA_INTERVAL_US*6)){
    		gpio_write(GPIO_LED_BLUE, !LED_ON_LEVEL);
    		gpio_write(GPIO_LED_GREEN, !LED_ON_LEVEL);
    		led_step = 0;


    		master_ota_test_mode = 2;
    	}
#else
    	master_ota_test_mode = 2;
#endif
    }



    //led indicate ota
    if(led_indicate_begin_tick){
#if (UI_LED_ENABLE)
    	if(led_step==0 && clock_time_exceed(led_indicate_begin_tick, LED_INTERVAL_US)){
    		gpio_write(led_indicate_gpio,LED_ON_LEVEL);
    		led_step = 1;
    	}
    	else if(led_step==1 && clock_time_exceed(led_indicate_begin_tick, LED_INTERVAL_US*2)){
    		gpio_write(led_indicate_gpio,!LED_ON_LEVEL);
    		led_step = 2;
    	}
    	else if(led_step==2 && clock_time_exceed(led_indicate_begin_tick, LED_INTERVAL_US*3)){
    		gpio_write(led_indicate_gpio,LED_ON_LEVEL);
    		led_step = 3;
    	}
    	else if(led_step==3 && clock_time_exceed(led_indicate_begin_tick, LED_INTERVAL_US*4)){
    		gpio_write(led_indicate_gpio,!LED_ON_LEVEL);
    		led_step = 4;
    	}
    	else if(led_step==4 && clock_time_exceed(led_indicate_begin_tick, LED_INTERVAL_US*5)){
    		gpio_write(led_indicate_gpio,LED_ON_LEVEL);
    		led_step = 5;
    	}
    	else if(led_step==5 && clock_time_exceed(led_indicate_begin_tick, LED_INTERVAL_US*6)){
    		gpio_write(led_indicate_gpio,!LED_ON_LEVEL);
    		led_indicate_begin_tick = 0;
    		led_step = 0;
    	}
#else
    	led_indicate_begin_tick = 0;
#endif
    }



    //process OTA timeout
    if(host_ota_start){
    	//ota timeout or disconnect. ota fail
    	if( clock_time_exceed(ota_begin_tick, OTA_TIMEOUT_S * 1000000) || blc_ll_getCurrentState() != BLS_LINK_STATE_CONN){
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
        	/*BIBOO UX 2026-10-20 (field log 21:17 attempt 2): the link died
        	 *within ~1s of CMD_OTA_START with 0 bytes pushed. That is the
        	 *signature of the mouse REJECTING the START - it was still in
        	 *OTA mode from a previous session that ended in a link drop
        	 *(OTA_FLOW_ERR=6, and its result callback unconditionally sets
        	 *the shutdown flag -> reboot + auto power-off). The fix is on
        	 *the mouse side: power-cycle it (or wait ~60s for its OTA
        	 *process timeout) before retrying. */
    		if(host_ota_start >= 4 && ota_adr == 0){
    			printf("[blm_ota] link lost before first push (step %d): mouse likely rejected START (stale OTA session?) - power-cycle the mouse\r\n", host_ota_start);
    		}
#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */
        	ota_set_result(0);
    		return;
    	}
    }


	if(host_ota_start == 0)
	{
		if(master_ota_cmd){  //UI: pc tool trig OTA mode

			if(master_ota_cmd == 1){
				#if (UI_LED_ENABLE)
					gpio_write(GPIO_LED_BLUE,LED_ON_LEVEL);  //ota begin
				#endif
			    flash_adr_ota_master = 0x20000;
			}
			else {
				#if (UI_LED_ENABLE)
					gpio_write(GPIO_LED_GREEN,LED_ON_LEVEL);  //ota begin
				#endif
			    flash_adr_ota_master = 0x40000;
			}


#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
			/*BIBOO UX: stream mode - the PC fed the image over the host
			 *link, so the size comes from SLOT_BEGIN (validated there)
			 *and no flash read is needed. Otherwise (button-triggered
			 *legacy path) read the size word from the slot header. */
			if(ota_stream_total){
				n_firmware = ota_stream_total;
			}
			else
#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */
			{
				n_firmware = *(u32 *)(flash_adr_ota_master + 0x18);
				if(n_firmware > (FW_SIZE_MAX<<10)){  //bigger then 128K
					ota_set_result(0);
					return;
				}
			}


		    host_ota_start = 1;
		    ota_begin_tick = clock_time();

		    host_ota_update_pending = 1;
		}
	}
	else if(host_ota_start == 1)
	{
		/*BIBOO UX 2026-10-20: bounded wait for the connection parameter
		 *update requested at step 0 (see host_ota_update_conn_complete).
		 *The update is a pure optimization - the dongle-mouse link already
		 *runs at interval 8 / latency 0 - but if the slave rejects it, the
		 *complete event never arrives, or it carries different params, the
		 *original code waited here forever (field log 2026-10-20 20:32:
		 *10s at step 1, rx watchdog abort). Advance to the discovery step
		 *anyway after 1.5s: a 10ms link pushes at full speed without the
		 *update. A normal update completes within ~100ms, so this never
		 *delays the happy path. */
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
		if(clock_time_exceed(ota_begin_tick, 1500000))
		{
			host_ota_update_pending = 0;
			host_ota_start = 2;
			ota_disc_tries = 0;	/*fresh discovery window, see step 3*/
			printf("[blm_ota] conn update wait timeout, start discovery anyway\r\n");
		}
#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */
	}
	else if(host_ota_start == 2)
	{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
		/*BIBOO UX 2026-10-20: prefer the handle the connect-time service
		 *discovery already resolved. The mouse OTA characteristic sits in
		 *its ATT table from boot, so there is nothing to discover - and
		 *the 8208 mouse did not answer the 128-bit read-by-type request
		 *below over the air (field log 20:51: stuck at step 3 until the
		 *session watchdog aborted). The SDP result is the very attribute
		 *that request would have returned, with zero ATT round trips. */
		if(conn_char_handler[2]){
			slave_ota_handle = conn_char_handler[2];
			host_ota_start = 4;
			printf("[blm_ota] OTA write handle 0x%04X (connect-time SDP)\r\n",
				   slave_ota_handle);
		}
		else
#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */
		{
			/*Send read_by_type_req to get remote's OTA handle
			 * once receive remote's response, call back function att_read_by_type_rsp_handler
			 * will be called to get the handle and set host_ota_start to 2*/
			host_att_set_current_readByTypeReq_uuid((u8 *)my_OtaUUID, 16);

			if(BLE_SUCCESS==blc_gatt_pushReadByTypeRequest (BLM_CONN_HANDLE, 1, 0xffff, read_by_type_req_uuid, 16)){
				host_ota_start = 3;
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
				ota_disc_tick = clock_time();
#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */
			}
		}

	}
	else if(host_ota_start == 3)
	{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
		/*BIBOO UX 2026-10-20: bounded discovery wait - see the
		 *ota_disc_tries declaration. No response within 2s: resend the
		 *request (the first push may have been dropped on a congested
		 *link). Still nothing after 3 tries: fail the session cleanly
		 *(ota_set_result latches result=2 for the PC and releases the
		 *deferred SLOT_BEGIN response with an error). */
		if(clock_time_exceed(ota_disc_tick, 2000000)){
			if(ota_disc_tries < 3){
				ota_disc_tries++;
				printf("[blm_ota] OTA discovery timeout, resend (%d)\r\n", ota_disc_tries);
				host_ota_start = 2;	/*step 2 re-sends the request*/
			}
			else{
				printf("[blm_ota] OTA discovery failed (no att rsp), abort\r\n");
				ota_set_result(0);
			}
		}
#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */
	}
	else if (host_ota_start == 4)
	{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
		/*BIBOO UX: stream mode consumes the PC-fed fifo in step 6; the
		 *flash pointer is only needed for the legacy/button path. */
		if(!ota_stream_total)
#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */
		{
			p_firmware = (u8 *)flash_adr_ota_master;
		}
		ota_adr = 0;


		u16 ota_start = CMD_OTA_START;
		if(BLE_SUCCESS == blc_gatt_pushWriteCommand(BLM_CONN_HANDLE,slave_ota_handle,(u8 *)&ota_start, 2)){
			host_ota_start = 5;
			otaStart_cmd_tick = clock_time();
		}

	}
	else if (host_ota_start == 5)
	{
		if( clock_time_exceed(otaStart_cmd_tick, 50000) ){
			host_ota_start = 6;
		}
	}
	else if (host_ota_start == 6)  //send ota data form addr 0 ~ n_firmware
	{
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
		/*BIBOO UX 2026-10-20 (field log 21:17 attempt 1): the push engine
		 *gated on !isRfStateMachineBusy() && getTxFifoNumber() < 5 for the
		 *rest of the session after 8 PDUs - the mouse was doing its
		 *OTA-start flash work / its RX was congested, so the TX fifo never
		 *drained and the dongle went silent on a link whose supervision
		 *timeout had been shortened to 2S (see step 0). Result: link
		 *terminate, mouse left stuck in OTA mode, next START rejected with
		 *OTA_FLOW_ERR(6). Track the stall, print the gate state so the
		 *field log shows WHY, and after 3s push anyway - queuing one more
		 *PDU is safe even mid connection event (the push API itself fails
		 *gracefully when the fifo is truly full). */
		static u32 ota_push_idle_tick = 0;	/*last push, or last dry-fifo tick*/
		static u8  ota_push_stall_logged = 0;
		/*BIBOO UX 2026-10-21: pace the push - without it the dongle packs
		 *several PDUs into one connection event; the mouse writes each OTA PDU
		 *to flash and has a tiny ATT RX queue, so the extras are dropped
		 *silently - no error anywhere, just a hole in adr_index ->
		 *OTA_PACKET_LOSS(1). Note this is NOT part of `allow` on purpose:
		 *pacing must not look like a gate stall to the watchdog below.
		 *2026-10-22 tightened 4000->2500us: with the 10ms connection interval
		 *this allows up to 4 PDUs per event (inside the txFifo<5 gate),
		 *pushing the 4980-PDU image from ~25s to ~13s. Validation bar: the
		 *transparent push must finish with NO OTA_PACKET_LOSS and a clean
		 *verify; if the mouse drops packets, back off to 3000. (2026-10-21
		 *note about matching OtaForm's 4ms is historical - the fast path is
		 *what made this pace revisit.)*/
		static u32 ota_pace_tick = 0;
		#define OTA_PUSH_MIN_GAP_US		2500

		/*"ready" (not "pending >= 16"): the short tail block needs only the
		 *last 4 bytes and no further PC frame will arrive, so the old gate
		 *could never release it - the push engine sat there until the rx
		 *watchdog killed the session.*/
		u8 pending = ota_stream_total && ux_ota_stream_ready();
		u8 allow   = !blm_ll_isRfStateMachineBusy() && blc_ll_getTxFifoNumber() < 5;
		/*stream mode only: the legacy flash-slot path never arms ota_pace_tick
		 *and must not be throttled by a stale value from a previous session*/
		u8 paced   = (ota_stream_total &&
					  !clock_time_exceed(ota_pace_tick, OTA_PUSH_MIN_GAP_US)) ? 1 : 0;

		if (pending && !allow)
		{
			if (clock_time_exceed(ota_push_idle_tick, 1000000) && !ota_push_stall_logged)
			{
				ota_push_stall_logged = 1;
				printf("[blm_ota] push stall: txFifo %d, rfBusy %d, streamFifo %d, pushed %d\r\n",
					   blc_ll_getTxFifoNumber(), blm_ll_isRfStateMachineBusy(),
					   (int)ux_ota_stream_pending(), (int)ota_adr);
			}
			if (clock_time_exceed(ota_push_idle_tick, 3000000))
			{
				/*3s gated with data waiting: force one push per 3s window*/
				allow = 1;
				ota_push_idle_tick = clock_time();
				ota_push_stall_logged = 0;
			}
		}
		else
		{
			/*pushing, or fifo dry (waiting for the next PC frame) - either
			 *way the engine is alive, re-arm the stall timer*/
			ota_push_idle_tick = clock_time();
			ota_push_stall_logged = 0;
		}

		if( allow && !paced )
#else
		if( !blm_ll_isRfStateMachineBusy() && blc_ll_getTxFifoNumber() < 5 )
#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */
		{
				u8 ota_buffer[20];
				rf_packet_att_ota_data_t *p = (rf_packet_att_ota_data_t *)ota_buffer;

				int nlen = ota_adr < n_firmware ? 16 : 0;

				p->adr_index = ota_adr>>4;

			if(nlen == 16){
#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)
				/*BIBOO UX: PC-fed stream mode - build the block from the
				 *host-link fifo instead of flash. get16 hands out only a
				 *FULL block (tail block 0xFF-padded, byte-identical to the
				 *BLE-direct channel) and does not consume it; commit below
				 *so a rejected push keeps the bytes for the retry. */
				if(ota_stream_total){
					if(ux_ota_stream_get16(p->data)){
						p->crc_16 = crc16((u8 *)&(p->adr_index), 18);

						if(BLE_SUCCESS == blc_gatt_pushWriteCommand(BLM_CONN_HANDLE,slave_ota_handle,(u8 *)p, 20)){ // send write command OK
							ux_ota_stream_commit();	//consume the staged block
							ota_adr += 16;  //next data index
							ota_lastData_tick = clock_time();
							ota_pace_tick = clock_time();	//4ms inter-PDU budget
						}
						/*else: link congested. Do NOT commit - the next
						 *iteration re-pushes these exact bytes with the same
						 *adr_index. Dropping them here shifted every later
						 *block one slot and made the mouse answer
						 *OTA_FW_CHECK_ERR(6). */
					}
					/*else: fifo dry. Only finish when the PC closed the
					 *stream (SLOT_END + everything pushed) - a dry fifo
					 *mid-transfer just means "wait for the next frame". */
					else if(ux_ota_stream_eof()){
						host_ota_start = 7;  //all stream data OK, to next step
					}
				}
				else
#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */
				{
					memcpy(p->data, p_firmware + ota_adr, 16);

					p->crc_16 = crc16((u8 *)&(p->adr_index), 18);

					if(BLE_SUCCESS == blc_gatt_pushWriteCommand(BLM_CONN_HANDLE,slave_ota_handle,(u8 *)p, 20)){ // send write command OK
						ota_adr += 16;  //next data index
						ota_lastData_tick = clock_time();
					}
				}

			}
				else{
					host_ota_start = 7;  //all ota data OK, to next step
				}

		}
	}
	else if(host_ota_start == 7)
	{
		if( clock_time_exceed(ota_lastData_tick, 50000) ){  //50ms after last valid data, master send OTA_end cmd
			host_ota_start = 8;
		}
	}
	else if(host_ota_start == 8)   //send OTA end cmd, no need 2 byte CRC
	{
		if( !blm_ll_isRfStateMachineBusy() && blc_ll_getTxFifoNumber() < 5 )
		{
			u8 ota_buffer[20];
			rf_packet_att_ota_data_t *p = (rf_packet_att_ota_data_t *)ota_buffer;
			p->adr_index = CMD_OTA_END;

			adr_index_max_l = (ota_adr-16)>>4;
			adr_index_max_h = (ota_adr-16)>>12;
			p->data[0] = adr_index_max_l;    //adr_index_max for slave to check if any packet miss
			p->data[1] = adr_index_max_h;
			p->data[2] = ~adr_index_max_l;  //adr_index_max check
			p->data[3] = ~adr_index_max_h;

			memset(p->data + 4, 0, 12);

			if(BLE_SUCCESS == blc_gatt_pushWriteCommand(BLM_CONN_HANDLE,slave_ota_handle,(u8 *)p, 6)){ // send write command OK
				host_ota_start = 9;
			}


		}

	}
	else if(host_ota_start == 9)
	{
		if( blc_ll_getTxFifoNumber() == 0 ){ //all data acked,OTA OK
			ota_set_result(1);
		}
	}
}




#endif  //end of BLE_MASTER_OTA_ENABLE
