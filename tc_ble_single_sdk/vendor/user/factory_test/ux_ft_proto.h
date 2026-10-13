/********************************************************************************************************
 * @file    ux_ft_proto.h
 *
 * @brief   BIBOO UX factory-test protocol, shared by BOTH firmware ends:
 *            - dongle side : vendor/user/factory_test/ux_factory_test.*
 *                            (switch BIBOO_UX_FACTORY_TEST, dongle app_config.h)
 *            - DUT   side  : vendor/user/factory_test/ux_ft_dut.*
 *                            (switch BIBOO_UX_FACTORY_TEST_DUT, mouse AAA_app_config.h)
 *          The PC tool (BibooUxTool: DongleLink.cs / FactoryTestForm.cs)
 *          mirrors these constants by hand - keep all three in sync.
 *
 *          Surface 1: PC <-> dongle, the existing host link
 *          (Feature 0x09 downlink / Input 0x08 uplink, 15-byte payloads,
 *          transport in vendor/user/host_link/ux_host_link.c):
 *            downlink cmd  : [0]=cmd, [1]=seq, [2..14]=args
 *            uplink  rsp   : [0]=cmd|UX_FT_RSP_MASK, [1]=seq, [2]=status, [3..14]=data
 *            uplink  event : [0]=evt id, [1]=seq, [2..14]=data
 *          The legacy commands 0x00 ECHO / 0x01 IDENT / 0x02 BEACON keep
 *          their original formats (UX_HLINK_CMD_*, ux_host_link.c); the
 *          factory-test commands below start at 0x10 and never collide.
 *
 *          Surface 2: dongle <-> DUT, the factory SPP channel
 *          (write 0x33F3 downlink / notify 0x33F4 uplink, 20-byte frames,
 *          transport in vendor/user/spp/ux_spp.c):
 *            frame: [0]=cmd, [1..19]=args/results
 *          The factory-native auth commands (0x77 AUTH_CMD / 0x78
 *          GET_AUTH_STATE_CMD) and their result enums stay defined in
 *          ux_spp.h and are NOT redefined here.
 *
 * @author  BIBOO UX
 *
 *******************************************************************************************************/
#ifndef UX_FT_PROTO_H_
#define UX_FT_PROTO_H_


/* ================ Surface 1: PC <-> dongle (host link, 15B payload) ================ */

#define UX_FT_HLINK_SIZE            15      /*payload bytes, both directions*/

/* downlink frame (Feature 0x09): [0]=cmd [1]=seq [2..14]=args */
#define UX_FT_DL_OFFS_CMD           0
#define UX_FT_DL_OFFS_SEQ           1       /*copied into the response / matched by the PC*/
#define UX_FT_DL_OFFS_ARGS          2

/* uplink response (Input 0x08): [0]=cmd|RSP_MASK [1]=seq [2]=status [3..14]=data */
#define UX_FT_RSP_MASK              0x80    /*response cmd id = request cmd id | mask*/
#define UX_FT_UL_OFFS_CMD           0
#define UX_FT_UL_OFFS_SEQ           1
#define UX_FT_UL_OFFS_STATUS        2
#define UX_FT_UL_OFFS_DATA          3

/* uplink event (Input 0x08, spontaneous): [0]=evt id [1]=seq [2..14]=data */
#define UX_FT_EVT_OFFS_ID           0
#define UX_FT_EVT_OFFS_SEQ          1       /*rolling counter */
#define UX_FT_EVT_OFFS_DATA         2

/* response status ([2] of an uplink response) */
#define UX_FT_ST_OK                 0
#define UX_FT_ST_ERR                1       /*unknown cmd / bad args / wrong mode / no DUT link*/
#define UX_FT_ST_BUSY               2       /*previous FT command still in flight*/

/* PC -> dongle commands (downlink [0]) */
#define UX_FT_CMD_START             0x10    /*enter factory-test mode: mouse reports are mirrored to 0xE1, beacon paused*/
#define UX_FT_CMD_STOP              0x11    /*leave factory-test mode (the USB forwarding always runs, no bypass)*/
#define UX_FT_CMD_STATUS            0x12    /*query: active / DUT link / SPP ready / auth tick*/
#define UX_FT_CMD_DUT_DISCONNECT    0x13    /*drop the current DUT mouse (swap unit on the line)*/
#define UX_FT_CMD_DUT_INFO          0x14    /*query: DUT local name (cached from the connect-time ADV report), 12B*/
#define UX_FT_CMD_DUT_MAC           0x15    /*query: DUT peer MAC (6B, live from cur_conn_device)*/
#define UX_FT_CMD_FWD               0x40    /*one fragment of a DUT command, forwarded over SPP 0x33F3*/

/* UX_FT_CMD_STATUS response data ([3..14] of the response) */
#define UX_FT_STS_OFFS_ACTIVE       0       /*1 = factory-test mode active*/
#define UX_FT_STS_OFFS_DUT_LINK     1       /*1 = DUT mouse connected*/
#define UX_FT_STS_OFFS_SPP_READY    2       /*1 = 0x33F3/0x33F4 handles discovered*/
#define UX_FT_STS_OFFS_TICK_L       3       /*authorization tick, u16 little endian*/
#define UX_FT_STS_OFFS_TICK_H       4

/* dongle -> PC events (uplink [0], spontaneous) */
#define UX_FT_EVT_MOUSE             0xE1    /*one bypassed BLE mouse report*/
#define UX_FT_EVT_DUT_LINK          0xE2    /*DUT connected / disconnected*/
#define UX_FT_EVT_FWD_RSP           0xE3    /*one fragment of a DUT response*/

/* UX_FT_EVT_MOUSE data ([2..14]; raw BLE report bytes, MOUSE_DATA_LEN_AAA
 * layout - both firmware ends are fixed to 4: btn, s8 x, s8 y, s8 wheel;
 * the PC parses these offsets per UxFtProto.cs) */
#define UX_FT_MOUSE_OFFS_BTN        0
#define UX_FT_MOUSE_OFFS_X          1       /*s8 x*/
#define UX_FT_MOUSE_OFFS_Y          2       /*s8 y*/
#define UX_FT_MOUSE_OFFS_WHEEL      3       /*s8 wheel*/

/* UX_FT_EVT_DUT_LINK data */
#define UX_FT_LINK_OFFS_STATE       0
#define UX_FT_LINK_DOWN             0
#define UX_FT_LINK_UP               1

/* UX_FT_CMD_DUT_INFO response data ([3..14] of the response, 12B):
 * local name cached from the connect-time ADV report (AD type 0x09/0x08),
 * zero padded / truncated to UX_FT_DUTI_NAME_LEN. The peer MAC does NOT
 * fit into the same frame (6+18 > 12), so it has its own command:
 * UX_FT_CMD_DUT_MAC. */
#define UX_FT_DUTI_OFFS_NAME        0
#define UX_FT_DUTI_NAME_LEN         12

/* UX_FT_CMD_DUT_MAC response data ([3..8] of the response):
 * peer MAC, LSB first (exactly as the controller reports it); the PC
 * reverses the bytes for the usual MSB-first display */
#define UX_FT_DUTM_OFFS_MAC         0

/* FT_FWD / EVT_FWD_RSP fragmentation (payload starts at the frame's
 * args/data offset, i.e. downlink [2] / event [2]):
 *   frame[args+0]           = flags:
 *                               bit7      MORE: this is not the last fragment
 *                               bits[3:0] fragment length, 1..12
 *   frame[args+1 .. +len]   = the fragment bytes
 * The length nibble is required because the 15-byte host link payload pads
 * the last fragment (no out-of-band length is available): e.g. AES_ENC on
 * the SPP side is a 17B frame -> 2 host link fragments (12B MORE + 5B last). */
#define UX_FT_FWD_OFFS_FLAGS        0       /*relative to UX_FT_DL_OFFS_ARGS / UX_FT_EVT_OFFS_DATA*/
#define UX_FT_FWD_OFFS_DATA         1       /*relative to UX_FT_DL_OFFS_ARGS / UX_FT_EVT_OFFS_DATA*/
#define UX_FT_FWD_FRAG_MAX          12      /*fragment payload bytes per host link frame*/
#define UX_FT_FWD_FLAG_MORE         0x80    /*flags bit7: more fragments follow*/
#define UX_FT_FWD_LEN_MASK          0x0F    /*flags bits[3:0]: fragment length (1..12)*/


/* ================ Surface 2: dongle <-> DUT (SPP, 20B frames) ================ */

#define UX_FT_SPP_SIZE              20      /*frame bytes on 0x33F3 / 0x33F4*/
#define UX_FT_SPP_OFFS_CMD          0
#define UX_FT_SPP_OFFS_ARGS         1

/* DUT commands ([0] of a 0x33F3 write; the response comes back as a 0x33F4
 * notify with [0] echoing the command id). The factory-native auth commands
 * 0x77/0x78 stay in ux_spp.h (SPP_APP_MSG_AUTH_CMD / SPP_APP_MSG_GET_AUTH_STATE_CMD). */
#define UX_FT_DUT_CMD_PING          0x01    /*-> rsp [1]=fw version [2]=model [3]=MOUSE_DATA_LEN_AAA*/
#define UX_FT_DUT_CMD_AES_ENC       0x10    /*args [1..16]=16B plaintext   -> rsp [1..16]=ciphertext*/
#define UX_FT_DUT_CMD_AES_DEC       0x11    /*args [1..16]=16B ciphertext  -> rsp [1..16]=plaintext*/
#define UX_FT_DUT_CMD_AUTH          0x12    /*args [1..8]=8B challenge     -> rsp [1..16]=16B response*/
#define UX_FT_DUT_CMD_TOUCH_START   0x20    /*start the touch event stream -> rsp [1]=UX_FT_DUT_ST_* */
#define UX_FT_DUT_CMD_TOUCH_STOP    0x21    /*stop  the touch event stream -> rsp [1]=UX_FT_DUT_ST_* */

/* DUT responses: [1] status of ack-only commands (TOUCH_START / TOUCH_STOP / ...) */
#define UX_FT_DUT_ST_OK             0
#define UX_FT_DUT_ST_ERR            1

/* DUT -> dongle spontaneous notify ([0] of a 0x33F4 frame) */
#define UX_FT_DUT_EVT_TOUCH         0x22    /*one touch sample*/

/* UX_FT_DUT_CMD_PING response fields (relative to UX_FT_SPP_OFFS_ARGS) */
#define UX_FT_PING_OFFS_VERSION     0       /*u8, DUT firmware version*/
#define UX_FT_PING_OFFS_MODEL       1       /*u8, model id*/
#define UX_FT_PING_OFFS_MOUSE_LEN   2       /*u8, MOUSE_DATA_LEN_AAA of the DUT*/

/* UX_FT_DUT_EVT_TOUCH fields (absolute offsets) */
#define UX_FT_TOUCH_OFFS_PRESSED    1       /*1 = finger down / 0 = up*/
#define UX_FT_TOUCH_OFFS_POS_L      2       /*touch position, u16 little endian*/
#define UX_FT_TOUCH_OFFS_POS_H      3
#define UX_FT_TOUCH_OFFS_SEQ        4       /*sample sequence, wraps at 0xFF*/


#endif /* UX_FT_PROTO_H_ */
