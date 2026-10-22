/********************************************************************************************************
 * @file    ux_self_ota.h
 *
 * @brief   BIBOO UX module (dongle side): dongle SELF firmware upgrade over the
 *          USB HID host link (Feature 0x09 downlink / Input 0x08 uplink,
 *          15-byte payloads, transport in vendor/user/host_link/ux_host_link.c).
 *
 *          The mouse OTA (vendor/user/ota_slot, cmd 0x50..0x54) streams the
 *          image straight through to the mouse - it never touches dongle
 *          flash. This module is the other direction: the image is the
 *          DONGLE's own firmware and is written into the spare A/B bank of
 *          the dongle flash, so the dongle can be reflashed from the PC tool
 *          without a Telink burning tool.
 *
 *          Telink 8258 A/B layout (verified against the real dongle bin):
 *            bank 0 : flash 0x00000 .. 0x1FFFF  (128K)
 *            bank 1 : flash 0x20000 .. 0x3FFFF  (128K, == ota_program_bootAddr)
 *          The image that is currently RUNNING is the bank we execute from;
 *          the new image always goes into the OTHER bank. Which one that is
 *          is decided at runtime from the address of main() - see
 *          sota_run_bank() in the .c for why ota_program_offset must NOT be
 *          used for that.
 *
 *          Bank header (offset inside the bank, all verified on the real bin):
 *            0x00      : initial SP
 *            0x04      : reset vector
 *            0x08      : 0x4B  (BOOT_MARK_VALUE, == 'K' of "KNLT")
 *            0x08..0x0B: "KNLT" (Telink magic)
 *            0x18      : u32 image size  (FW_SIZE_ADDR)
 *            size-4    : u32 CRC32 of everything before it
 *
 *          Flow (PC tool, OTA page -> "USB Dongle" channel):
 *            1. INFO     0x74 : read the bank layout (no side effects)
 *            2. BEGIN    0x70 : args[0..3] = image size u32 LE. The dongle
 *                                arms the session, then PRE-ERASES the whole
 *                                target span (the only long irq_disable in the
 *                                flow) and answers OK only once that is done.
 *                                The PC is blocked in its BEGIN read the whole
 *                                time, so no SET_REPORT is in flight during the
 *                                erases (a SETUP arriving mid-erase wedges the
 *                                Telink USB control endpoint - 2026-10-21
 *                                deadlock). The DATA path then only does fast
 *                                page writes. An abandoned BEGIN leaves the
 *                                spare bank half-erased but never commits, so
 *                                the running firmware is untouched.
 *            3. DATA     0x71 : args[0..2] = byte offset u24 LE (image max
 *                                124K, 3 bytes are enough), args[3] = len,
 *                                args[4..] = payload bytes (v2, 2026-10-22:
 *                                was 4-byte offset + 8B; the 3-byte offset
 *                                squeezes 9B per frame out of the 13-byte
 *                                args area). Written to flash in the main
 *                                loop (never in the EP0 irq).
 *            4. END      0x72 : PC is done. The dongle verifies the whole
 *                                image (byte count + "KNLT" + size field +
 *                                CRC32 over size-4 bytes) and reports the
 *                                result. Until this point the RUNNING
 *                                firmware is untouched - a failed or aborted
 *                                upgrade leaves the dongle exactly as it was.
 *            5. COMMIT   0x75 : THE POINT OF NO RETURN. Invalidates the old
 *                                bank (writes 0x00 over its 0x08 mark) and
 *                                reboots. The new firmware is only executed
 *                                from here on. A wrong image that still
 *                                passes the CRC32 check can only be recovered
 *                                with a Telink burning tool.
 *
 *          Safety model:
 *            - Nothing is written outside the spare bank.
 *            - The old bank is only invalidated by an explicit COMMIT.
 *            - A 20s rx-idle watchdog aborts an abandoned session (the
 *              partial image stays in the spare bank, the running firmware
 *              is untouched).
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#ifndef UX_SELF_OTA_H_
#define UX_SELF_OTA_H_

#include "tl_common.h"
#include "../user_module_manage.h"	/*BIBOO_UX_USER_MODULE master switch*/


/* ================ host link command set (Surface 1, PC -> dongle) ================
 * Same frame layout as the factory-test / OTA-pipe commands
 * (ux_ft_proto.h): downlink [0]=cmd [1]=seq [2..14]=args,
 *                  uplink   [0]=cmd|0x80 [1]=seq [2]=status [3..14]=data.
 * Ids 0x70..0x75. The response id is cmd|0x80, so the block MUST stay clear
 * of the 0xE0..0xEF uplink event range: the first version used 0x60..0x65 and
 * its DATA/END/STATUS acks (0xE1/0xE2/0xE3) collided byte-for-byte with the
 * mouse / DUT-link / SPP-fragment events - the PC Pump() mis-filed every ack
 * as an event, the very first DATA frame timed out and the upload died at
 * rx_ok=1 (2026-10-21 field deadlock). 0x70..0x75 answers 0xF0..0xF5, clear
 * of everything (0x00..0x03 legacy, 0x10..0x15/0x40 FT, 0x50..0x54/0x55 mouse
 * OTA pipe + DevInf relay, 0x60..0x65 retired, 0x80.. reserved). */
#define UX_SOTA_CMD_BEGIN     0x70    /*args[0..3] = image size u32 LE*/
#define UX_SOTA_CMD_DATA      0x71    /*args[0..2] = u24 LE offset, args[3] = len 1..9, args[4..] = data*/
#define UX_SOTA_CMD_END       0x72    /*rsp data = verify result snapshot (below)*/
#define UX_SOTA_CMD_STATUS    0x73    /*rsp data = session snapshot (below)*/
#define UX_SOTA_CMD_INFO      0x74    /*rsp data = bank layout (below)*/
#define UX_SOTA_CMD_COMMIT    0x75    /*copy new bank over the running bank + reboot (no rsp timeout on success)*/
#define UX_SOTA_CMD_VERSION   0x76    /*rsp data = running image version (below)*/

/* VERSION (0x76) response data - 8 bytes:
 *   d[0..3] = version u32 LE from the RUNNING bank image header @0x02
 *             (the same field the PC prints as「文件头版本」when loading a bin;
 *             0x00000000 until someone starts bumping it)
 *   d[4..7] = UX_SOTA_SELF_BUILD u32 LE - a constant compiled into THIS
 *             image, bumped by hand on every release. This is the field the
 *             PC shows: under in-place-overwrite COMMIT the run bank is
 *             always 0x00000 and the bin header version never changes, so
 *             this build number is the ONLY visible proof an upgrade landed. */
#define UX_SOTA_VER_OFFS_HDR     0
#define UX_SOTA_VER_OFFS_BUILD   4

/* bump on every dongle firmware release. History: build 1 = 0x76 query
 * added (first release carrying it); build 2 = first release after the
 * build field was introduced but left at 1 in two consecutive flashes,
 * which made two different images indistinguishable - always bump!
 * 0x58..0x60 = user's own release bumps (cross-version verify done at 0x58).
 * 0x61 = windowed upload tuning: 3-byte offset frames (9B), fifo 48.
 * 0x62 = user's own release bump (v2 frame + auto-COMMIT verified twice).
 * 0x63 = interrupt OUT downlink fast path: EP5 OUT endpoint + Output 0x09
 *        report member (printer interface disabled to free EP5); PC probes
 *        and falls back to EP0 SET_REPORT on this image, so this build can
 *        be reached by self-OTA without the burning tool.
 * 0x64 = fix ep_out_irq byte count: read the full 16B OUT report
 *        ([id]+15B), was 15B - dropped the last payload byte of every
 *        frame (first victim: KNLT magic at image 0x08).
 * 0x65 = tighten the master OTA push pacing 4ms -> 2.5ms/PDU (blm_ota.c
 *        OTA_PUSH_MIN_GAP_US): transparent mouse OTA ~25s -> ~13s; keep
 *        only if the mouse verifies clean with no packet loss, else
 *        back off to 3000. */
#define UX_SOTA_SELF_BUILD      0x00000065u

/* DATA payload layout v2 (relative to the frame args offset, 13-byte args
 * area; v2 2026-10-22 - no compat kept, PC and firmware ship in lockstep):
 *   args[0..2] = byte offset of this frame in the image (u24 LE; image max
 *                is 124K so 3 bytes are enough - the 4th byte was wasted
 *                payload space)
 *   args[3]    = payload length     1..UX_SOTA_DATA_MAX
 *   args[4.. ] = payload bytes
 * The offset makes every frame idempotent: a retransmit (lost ack) is simply
 * re-written at the same place and never double-counted. sota_recv only
 * advances when a frame lands exactly at the current frontier, so a gap
 * (frame lost in flight) is caught by STATUS and repaired by the PC resending
 * from sota_recv. */
#define UX_SOTA_DATA_OFFS_OFF     0       /*u24 LE byte offset*/
#define UX_SOTA_DATA_OFFS_LEN     3       /*1..UX_SOTA_DATA_MAX*/
#define UX_SOTA_DATA_OFFS_DATA    4
#define UX_SOTA_DATA_MAX          9       /*3 + 1 + 9 = 13 = args area*/

/* session state (UX_SOTA_STS_OFFS_STATE) */
#define UX_SOTA_ST_IDLE           0       /*no session*/
#define UX_SOTA_ST_RECV           1       /*BEGIN ok, DATA frames flowing*/
#define UX_SOTA_ST_VERIFY         2       /*END seen, main loop is checking*/
#define UX_SOTA_ST_READY          3       /*image verified, waiting for COMMIT*/
#define UX_SOTA_ST_FAIL           4       /*verification failed, session dead*/
#define UX_SOTA_ST_COMMIT         5       /*COMMIT accepted, rebooting*/

/* END / COMMIT result codes (UX_SOTA_STS_OFFS_RESULT) */
#define UX_SOTA_RES_NONE          0       /*no result yet*/
#define UX_SOTA_RES_OK            1       /*verified / committed*/
#define UX_SOTA_RES_SIZE          2       /*byte count != announced size*/
#define UX_SOTA_RES_MAGIC         3       /*"KNLT" mark missing in the new bank*/
#define UX_SOTA_RES_HDR           4       /*size field @0x18 mismatch*/
#define UX_SOTA_RES_CRC           5       /*CRC32 mismatch*/
#define UX_SOTA_RES_ERASE         6       /*sector not blank after pre-erase (flash/wait broken)*/

/* INFO response data (UX_SOTA_CMD_INFO, 12 bytes at the data offset).
 * The uplink data area is exactly 12 bytes ([3..14] of the 15B frame,
 * ux_ft_proto.h) - a 4th u32 does NOT fit, so the max image size is NOT
 * on the wire: it is a build constant (UX_SOTA_IMAGE_MAX ==
 * ota_firmware_max_size) that the PC mirrors on its side. */
#define UX_SOTA_INFO_OFFS_RUN     0       /*running bank offset u32 LE (0 or 0x20000)*/
#define UX_SOTA_INFO_OFFS_OTA     4       /*upgrade target bank offset u32 LE*/
#define UX_SOTA_INFO_OFFS_BOOT    8       /*ota_program_bootAddr u32 LE (== bank size)*/

/* STATUS response data (UX_SOTA_CMD_STATUS, 10 bytes used of the 12B area).
 * The computed CRC32 is deliberately NOT here (2+4+4+4 > 12): it is only
 * meaningful after verification and travels in the END response instead. */
#define UX_SOTA_STS_OFFS_STATE    0       /*UX_SOTA_ST_*/
#define UX_SOTA_STS_OFFS_RESULT   1       /*UX_SOTA_RES_*/
#define UX_SOTA_STS_OFFS_RECV     2       /*bytes received so far, u32 LE*/
#define UX_SOTA_STS_OFFS_TOT      6       /*announced image size, u32 LE*/

/* BEGIN response data[0] when status = UX_FT_ST_ERR (see ux_ft_proto.h) */
#define UX_SOTA_BEGIN_ERR_BANK    2       /*bank layout could not be determined*/
#define UX_SOTA_BEGIN_ERR_SIZE    3       /*image size out of range*/
#define UX_SOTA_BEGIN_ERR_FLASH   4       /*flash too small for a second bank*/

/* BEGIN response data[0] when status = UX_FT_ST_BUSY */
#define UX_SOTA_BEGIN_BUSY_SESS   5       /*a session is already running*/

/* max image size the module accepts (must fit in the spare bank) */
#define UX_SOTA_IMAGE_MAX         0x1F000 /*124K == ota_firmware_max_size in this build*/

/* rx inactivity watchdog: if the PC dies / the cable is pulled mid-upload the
 *session can never complete, so abort. 20s is far beyond any legit gap
 *(frame-synchronous PC, ~15ms RTT), so it never kills a healthy transfer.*/
#define UX_SOTA_IDLE_TIMEOUT_S    20


#if (BIBOO_UX_USER_MODULE && BIBOO_UX_SELF_OTA)

/* module entry (dispatched by user_module_manage.c) */
void ux_self_ota_init(void);
void ux_self_ota_proc(void);

/* host link command entry (called from ux_host_link_dispatch, EP0 irq).
 * p = 15-byte downlink payload, r = 15-byte response buffer (pre-mirrored).
 * Returns 1 with r filled when p[0] is a self-OTA command, 0 otherwise.
 * INFO / STATUS / BEGIN answer at once; DATA answers at once but only queues
 * the bytes (the flash write happens in ux_self_ota_proc); END and COMMIT
 * are deferred and pushed from ux_self_ota_proc. */
int  ux_self_ota_host_cmd(const u8 *p, u8 *r);

/*1 while a session owns the host link (RECV/VERIFY/COMMIT). The host link
 *uses this to mute its per-frame UART dumps during an upload.*/
u8   ux_self_ota_busy(void);

#endif /* BIBOO_UX_USER_MODULE && BIBOO_UX_SELF_OTA */

#endif /* UX_SELF_OTA_H_ */
