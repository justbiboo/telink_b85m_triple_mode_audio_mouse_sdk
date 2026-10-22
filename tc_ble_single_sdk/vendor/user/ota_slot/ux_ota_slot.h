/********************************************************************************************************
 * @file    ux_ota_slot.h
 *
 * @brief   BIBOO UX module (dongle side): PC-fed OTA stream pipe + master-OTA
 *          driver, commanded by the PC tool over the host link (Feature 0x09 /
 *          Input 0x08, 15-byte payloads, transport in
 *          vendor/user/host_link/ux_host_link.c).
 *
 *          Architecture (2026-10-20 rewrite - "stream/pipe" mode):
 *          The image NEVER touches dongle flash. The PC streams the raw
 *          firmware over the host link (12B per frame); the EP0 irq only
 *          memcpy's frames into a small RAM fifo, and blm_ota.c proc_ota
 *          step 6 (main loop) pulls 16B at a time from that fifo, builds
 *          the standard 20B OTA PDU ([adr_index u16][16B data][crc16]) and
 *          pushes it to the mouse with blc_gatt_pushWriteCommand - exactly
 *          what the PC does in the BLE-direct channel, except the dongle
 *          owns the radio link to the mouse.
 *
 *          Flow (PC tool, e.g. BibooUxTool OTA page -> USB dongle channel):
 *            1. SLOT_BEGIN 0x50 : total image size -> dongle arms the master
 *               OTA state machine (10ms connection parameters, OTA
 *               characteristic discovery, CMD_OTA_START). The OK response is
 *               deferred until the push engine actually consumes the fifo,
 *               so the PC starts feeding only when the dongle is pushing.
 *               A stale session from a previous (dead) transfer is aborted
 *               implicitly; a 10s rx-idle watchdog aborts an abandoned one.
 *            2. SLOT_DATA  0x51 : 12 payload bytes per frame, ACKed one by
 *               one (ACK = "queued in the fifo"; the push drains it)
 *            3. SLOT_END   0x52 : PC is done; response reports the byte
 *               count, proc_ota pushes the tail, sends the OTA END frame
 *               and latches master_ota_result
 *            4. STATUS     0x54 : poll host_ota_start / ota_adr /
 *               n_firmware / master_ota_result for progress
 *          (there is no TRIGGER any more - SLOT_BEGIN starts the push)
 *
 *          The mouse side needs NO firmware change: it is already a Telink
 *          OTA server (BLE_OTA_SERVER_ENABLE=1) and the dongle speaks the
 *          same 20B PDU protocol as the PC-direct channel.
 *
 *          Why not the flash slot (the previous design): it erased + wrote
 *          ~80KB of dongle flash on every upgrade (wear), needed ~800ms of
 *          irq-off erase work in the main loop (prime suspect for the mouse
 *          BLE supervision-timeout drops seen in the field), doubled the
 *          latency (write then read back) and required a whole slot state
 *          machine (ERASE/RECV/READY + unlock/lock + watchdog). The pipe
 *          mode keeps only what is needed: a fifo and the master-OTA kick.
 *
 *          NOTE: this module requires BLE_MASTER_OTA_ENABLE (it drives the
 *          blm_ota.c globals directly) and BIBOO_UX_HOST_LINK.
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#ifndef UX_OTA_SLOT_H_
#define UX_OTA_SLOT_H_

#include "tl_common.h"
#include "../user_module_manage.h"	/*BIBOO_UX_USER_MODULE master switch*/


/* ================ host link command set (Surface 1, PC -> dongle) ================
 * Reuses the host link frame layout and the UX_FT_ST_* status codes
 * (ux_ft_proto.h): downlink [0]=cmd [1]=seq [2..14]=args,
 *                  uplink   [0]=cmd|0x80 [1]=seq [2]=status [3..14]=data.
 * Command ids 0x50..0x52/0x54 are free (0x00..0x03 legacy, 0x10..0x15/0x40 FT).
 * 0x53 (TRIGGER) was removed with the flash-slot design - SLOT_BEGIN now
 * starts the master OTA directly. */
#define UX_OTA_CMD_SLOT_BEGIN     0x50    /*args[0..3] = total size u32 LE (1..128K)*/
#define UX_OTA_CMD_SLOT_DATA      0x51    /*args[0] = len 1..12, args[1..12] = data*/
#define UX_OTA_CMD_SLOT_END       0x52    /*rsp data[0..3] = bytes received u32 LE*/
#define UX_OTA_CMD_STATUS         0x54    /*rsp data = stream/ota state snapshot (below)*/

/* SLOT_DATA payload layout (relative to the frame args offset) */
#define UX_OTA_DATA_OFFS_LEN      0       /*1..12 payload bytes*/
#define UX_OTA_DATA_OFFS_DATA     1
#define UX_OTA_DATA_MAX           12      /*frame budget: 13 args - 1 len byte*/

/* STATUS response data (relative to the uplink data offset, 12 bytes) */
#define UX_OTA_STS_OFFS_STATE     0       /*UX_OTA_ST_* pipe state*/
#define UX_OTA_STS_OFFS_MCMD      1       /*master_ota_cmd (blm_ota.c)*/
#define UX_OTA_STS_OFFS_START     2       /*host_ota_start (blm_ota.c)*/
#define UX_OTA_STS_OFFS_ADR_L     3       /*ota_adr u32 LE (bytes pushed so far)*/
#define UX_OTA_STS_OFFS_SIZE_L    7       /*n_firmware u32 LE (image size)*/
#define UX_OTA_STS_OFFS_RESULT    11      /*master_ota_result: UX_OTA_RES_* */

/* pipe state machine (UX_OTA_STS_OFFS_STATE) */
#define UX_OTA_ST_IDLE            0       /*no session*/
#define UX_OTA_ST_STREAM          1       /*PC feeding / dongle pushing*/
#define UX_OTA_ST_FINISH          2       /*SLOT_END seen, pushing the tail*/
/* master OTA result (UX_OTA_STS_OFFS_RESULT, latched in blm_ota.c) */
#define UX_OTA_RES_NONE           0       /*no OTA running / not finished*/
#define UX_OTA_RES_OK             1       /*master OTA success (mouse reboots)*/
#define UX_OTA_RES_FAIL           2       /*master OTA failed*/

/* SLOT_BEGIN response data[0] when status = UX_FT_ST_ERR:
 * 2 = DUT mouse not connected to the dongle (nothing to push to).
 * Other ERR paths leave the data area zeroed. */
#define UX_OTA_BEGIN_DUT_NOT_CONN 2

/* SLOT_BEGIN response data[0] when status = UX_FT_ST_BUSY:
 * 3 = a master-OTA push is still running (starting a new image now would
 * corrupt it). Stale upload sessions are NOT reported here - a new
 * SLOT_BEGIN aborts them implicitly. */
#define UX_OTA_BEGIN_BUSY_OTA     3

/* max image size the pipe accepts (== FW_SIZE_MAX in blm_ota.c) */
#define UX_OTA_IMAGE_MAX          0x20000 /*128K*/


#if (BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT)

/* module entry (dispatched by user_module_manage.c) */
void ux_ota_slot_init(void);
void ux_ota_slot_proc(void);

/*1 while a stream session owns the pipe (STREAM/FINISH). The host link uses
 *this to mute its per-frame UART dumps during an upload - they run in the
 *EP0 irq (~4ms busy-wait each) and starved the BLE stack hard enough to
 *drop the mouse link mid-upload (2026-10-20 field observation).*/
u8   ux_ota_slot_busy(void);

/* host link command entry (called from ux_host_link_dispatch, EP0 irq).
 * p = 15-byte downlink payload, r = 15-byte response buffer (pre-mirrored).
 * Returns 1 with r filled when p[0] is an OTA pipe command, 0 otherwise.
 * The SLOT_BEGIN response is deferred (pushed from ux_ota_slot_proc once
 * the master OTA push engine is running); everything else answers at once. */
int  ux_ota_slot_host_cmd(const u8 *p, u8 *r);


/* ================ stream source API (called from blm_ota.c) ================
 * ux_ota_slot.c owns the fifo; blm_ota.c proc_ota step 6 is the consumer.
 * All main-loop context. */

/*image size of the running pipe session (0 = none: proc_ota then uses the
 *legacy flash-slot source, e.g. the button-triggered path).*/
extern u32 ota_stream_total;

/*Build - but do NOT consume - the next 16B OTA data block from the fifo.
 *Returns 1 with dst[0..15] filled when a COMPLETE block is available: 16 real
 *bytes, or the short tail block padded with 0xFF exactly like the BLE-direct
 *channel (that channel passes, so the bytes must match it byte for byte).
 *Returns 0 when the fifo cannot complete the block yet - the bytes already
 *moved into the internal staging buffer are kept and topped up by the next
 *call, so nothing is lost and a partial block is NEVER handed out as ready
 *(the PC feeds 12B/frame while a PDU carries 16B, so a naive pop-and-go
 *implementation zero-pads the gap on almost every block - that was the
 *2026-10-21 OTA_FW_CHECK_ERR(6) corruption).
 *
 *Call ux_ota_stream_commit() only after blc_gatt_pushWriteCommand returned
 *BLE_SUCCESS; on a rejected push just retry the next loop - the same staged
 *bytes are handed out again.*/
int  ux_ota_stream_get16(u8 *dst);

/*Commit the block last returned by ux_ota_stream_get16 (advances the
 *consumed-byte counter and frees the staging buffer).*/
void ux_ota_stream_commit(void);

/*1 = the PC sent SLOT_END and every byte has been pushed (safe to send the
 *OTA END frame).*/
u8   ux_ota_stream_eof(void);

/*1 = a full block can be composed right now. This is what blm_ota.c step 6
 *gates on - "pending >= 16" would deadlock on the short tail block, where
 *only 4 bytes remain and no further frame will ever arrive.*/
u8   ux_ota_stream_ready(void);

/*bytes queued in the fifo plus bytes staged but not pushed yet - blm_ota.c
 *step 6 stall watchdog uses this to tell "PC feed is slow" (nothing queued)
 *from "BLE push path is stuck" (bytes queued, nothing goes out).*/
u32  ux_ota_stream_pending(void);

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_OTA_SLOT */

#endif /* UX_OTA_SLOT_H_ */
