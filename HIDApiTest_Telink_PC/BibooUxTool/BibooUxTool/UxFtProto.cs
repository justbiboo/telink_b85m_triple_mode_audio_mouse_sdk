using System;
using System.Collections.Generic;
using System.Security.Cryptography;

namespace BibooUxTool
{
    /// <summary>
    /// BIBOO UX 产测协议常量（镜像固件端 ux_ft_proto.h / ux_spp.h，三端须保持同步）。
    /// Surface 1：PC ↔ dongle（Host Link：Feature 0x09 下行 / Input 0x08 上行，15 字节 payload）
    /// Surface 2：dongle ↔ DUT（SPP 0x33F3 写 / 0x33F4 notify，20 字节帧）
    /// </summary>
    internal static class UxFtProto
    {
        // ===== Surface 1: PC ↔ dongle（15B payload） =====
        public const int HlinkSize = 15;
        public const int DlOffsCmd = 0;      // 下行 [0]=cmd [1]=seq [2..14]=args
        public const int DlOffsSeq = 1;
        public const int DlOffsArgs = 2;
        public const byte RspMask = 0x80;    // 响应 cmd = 请求 cmd | mask
        public const int UlOffsCmd = 0;      // 响应 [0]=cmd|RspMask [1]=seq [2]=status [3..14]=data
        public const int UlOffsSeq = 1;
        public const int UlOffsStatus = 2;
        public const int UlOffsData = 3;
        public const int EvtOffsId = 0;      // 事件 [0]=evt id [1]=seq [2..14]=data
        public const int EvtOffsSeq = 1;
        public const int EvtOffsData = 2;

        // 响应状态（uplink 响应 [2]）
        public const byte StOk = 0;
        public const byte StErr = 1;         // 未知命令 / 参数错误 / 模式错误 / 无 DUT
        public const byte StBusy = 2;        // 上一条产测命令仍在途

        // 下行命令（[0]）
        public const byte CmdStart = 0x10;           // 进入产测模式（鼠标报告镜像 → 0xE1）
        public const byte CmdStop = 0x11;            // 退出产测模式（USB 转发不受影响）
        public const byte CmdStatus = 0x12;          // 查询 active / DUT / SPP / tick
        public const byte CmdDutDisconnect = 0x13;   // 断开当前 DUT（产线换机）
        public const byte CmdDutInfo = 0x14;         // 查询 DUT 本地名（连接时 ADV 缓存），12B
        public const byte CmdDutMac = 0x15;          // 查询 DUT 对端 MAC，6B
        public const byte CmdFwd = 0x40;             // DUT 命令分片，经 SPP 0x33F3 转发
        public const byte CmdMstat = 0x03;           // 鼠标链路计数器（诊断，镜像 dongle MSTAT 0x03）：args[0]=组 / 0xFF 清零
        public const byte MstatGroup0 = 0;           // → att_rx / ring_in / ring_drop
        public const byte MstatGroup1 = 1;           // → tx_ok / epq / fifo_drop
        public const byte MstatReset = 0xFF;         // 清零全部计数器

        // ===== OTA 透传流管道命令（镜像固件 ux_ota_slot.h，2026-10-20 重写）=====
        // 流程：PC 经 USB 把固件字节流灌进 dongle（BEGIN/DATA/END），dongle 不落 flash，
        // 而是由 Telink master OTA 框架（blm_ota.c proc_ota）实时组 20B PDU、
        // 通过它到鼠标的 BLE 链路推给鼠标；鼠标固件零改动。
        // BEGIN 即启动推包（连接间隔切 10ms + OTA 特征发现 + START），无 TRIGGER
        // （0x53 已随落盘方案一起废除）。命令字 0x50..0x52/0x54 与 FT 的 0x10..0x15/0x40 不重叠。
        public const byte CmdOtaSlotBegin = 0x50;    // args[0..3] = 固件总字节 u32 LE（1..128K）
        public const byte CmdOtaSlotData = 0x51;     // args[0] = len 1..12，args[1..12] = data
        public const byte CmdOtaSlotEnd = 0x52;      // rsp data[0..3] = 已收字节数 u32 LE
        public const byte CmdOtaStatus = 0x54;       // rsp data = 12B 快照（见下）

        // ===== 0x55 GET_DEV_INFO：鼠标 DevInf_aaa 中继（2026-10-21 新增）=====
        // dongle 是鼠标的 BLE master，连上鼠标后自己读 OTA 服务下的只读特征 DevInf_aaa
        // （句柄 0x0010，无需配对/加密）并缓存；PC 的 BLE 链路被 dongle 占着，只能由它中继。
        // 15B 放不进一帧（r[3..14] 只有 12B），故 args[0]=字节偏移，分两帧取回（0 与 12）。
        public const byte CmdDevInfo = 0x55;          // args[0]=偏移 → rsp data = 12B 分片
        public const int DevInfoTotal = 15;           // DevInf_aaa 总长
        public const int DevInfoChunk = 12;           // 每帧有效数据（r[3..14]）
        public const byte DevInfoStOk = 0;
        public const byte DevInfoStNotReady = 1;      // 鼠标未连上 dongle / 尚未读到
        public const byte DevInfoStBadOffset = 2;

        // ===== 0x70..0x75 dongle 自身升级（2026-10-21 重编号，镜像 ux_self_ota.h）=====
        // 与鼠标 OTA（0x50..0x54，镜像流过 dongle 不落 flash）相反：这里的镜像就是
        // dongle 自己的固件，写进它 flash 的另一个 A/B bank（运行在 0x00000，
        // 新镜像写 0x20000）。COMMIT 之前运行中的固件一根字节都没被碰过，
        // 失败/中断的上传不会让 dongle 变砖；COMMIT 才作废旧 bank 并重启。
        // 2026-10-21 字段死锁根因：首版用 0x60..0x65，而响应 = cmd|0x80 =
        // 0xE0..0xE5，其中 DATA/END/STATUS 的 ACK（0xE1/0xE2/0xE3）与上行事件
        // EvtMouse/EvtDutLink/EvtFwdRsp 逐字节相撞——Pump 把每个 ACK 当事件丢弃，
        // 第一帧 DATA 就超时（dongle 侧 rx_ok 恒为 1、STATUS 永不匹配，最后
        // 20s 空闲看门狗把会话杀掉）。重编号到 0x70..0x75（响应 0xF0..0xF5）
        // 后与事件区彻底错开。新旧固件/工具必须成对升级：旧 dongle 对 0x74
        // INFO 只会回 echo，QueryBankInfo 超时报「固件过旧」，属预期拦截。
        public const byte CmdSelfBegin = 0x70;      // args[0..3] = 镜像总字节 u32 LE
        public const byte CmdSelfData = 0x71;       // args[0] = len 1..12，args[1..12] = data
        public const byte CmdSelfEnd = 0x72;        // rsp data[0] = SelfRes*（校验结果）
        public const byte CmdSelfStatus = 0x73;     // rsp data = 12B 会话快照（见下）
        public const byte CmdSelfInfo = 0x74;       // rsp data = 12B bank 布局（见下）
        public const byte CmdSelfCommit = 0x75;     // 覆盖写入运行区 + 自动重启（不可逆）
        public const byte CmdSelfVersion = 0x76;    // 查运行固件版本（8B：bin 头版本 + build 号）

        // SELF_VERSION 响应数据布局（相对 UlOffsData，8B）——与 dongle ux_self_ota.h 对齐：
        // 原地覆盖 COMMIT 下运行 bank 恒为 0x00000、bin 头版本常为 0，build 号是
        // 升级是否真正落地的唯一可见证据（dongle 侧每次发版手动递增）。
        public const int SelfVerOffsHdr = 0;        // 运行 bank 镜像头 0x02 处的版本 u32 LE
        public const int SelfVerOffsBuild = 4;      // dongle 编译期 build 号 u32 LE

        // SELF_DATA 载荷布局 v2（相对 DlOffsArgs）——与 dongle ux_self_ota.h 对齐
        // （v2 2026-10-22：不做新旧兼容，PC 与固件同步发版；镜像上限 124K，
        //   3 字节偏移足够，省下的 1 字节换成数据）：
        //   args[0..2] = 字节偏移 u24 LE（镜像内绝对偏移，使每帧幂等）
        //   args[3]    = 本帧数据长度 1..SelfDataMax
        //   args[4.. ] = 数据
        public const int SelfDataOffsOff = 0;       // u24 LE 字节偏移
        public const int SelfDataOffsLen = 3;       // 1..9
        public const int SelfDataOffsData = 4;
        public const int SelfDataMax = 9;           // 3 + 1 + 9 = 13 = args 区

        // SELF_INFO 响应数据布局（相对 UlOffsData，数据区共 12B）
        // 注意：第 4 个 u32 放不进 12B 数据区，max image 不上线——
        // 它是构建常量（ota_firmware_max_size），PC 侧用 SelfImageMax 镜像。
        public const int SelfInfoOffsRun = 0;       // 正在运行的 bank 偏移 u32 LE（0 或 0x20000）
        public const int SelfInfoOffsOta = 4;       // 升级目标 bank 偏移 u32 LE
        public const int SelfInfoOffsBoot = 8;      // ota_program_bootAddr u32 LE（== 单 bank 大小）

        // SELF_STATUS 响应数据布局（相对 UlOffsData，用 10B / 共 12B）
        // CRC32 不在这里（2+4+4+4 > 12）：校验通过后由 END 响应的 result 告知，
        // dongle UART 日志里有实算 CRC 可查。
        public const int SelfStsOffsState = 0;      // 会话状态 SelfSt*
        public const int SelfStsOffsResult = 1;     // 校验/提交结果 SelfRes*
        public const int SelfStsOffsRecv = 2;       // 已收字节 u32 LE
        public const int SelfStsOffsTotal = 6;      // BEGIN 宣告的镜像大小 u32 LE

        // 会话状态（SelfStsOffsState）
        public const byte SelfStIdle = 0;           // 无会话
        public const byte SelfStRecv = 1;           // BEGIN 成功，正在收 DATA
        public const byte SelfStVerify = 2;         // END 已到，正在校验
        public const byte SelfStReady = 3;          // 校验通过，等待 COMMIT
        public const byte SelfStFail = 4;           // 校验失败，会话作废
        public const byte SelfStCommit = 5;         // COMMIT 已接受，随即重启

        // 校验 / 提交结果（SelfStsOffsResult）
        public const byte SelfResNone = 0;          // 尚无结果
        public const byte SelfResOk = 1;            // 校验通过 / 已提交
        public const byte SelfResSize = 2;          // 实收字节数与宣告不符
        public const byte SelfResMagic = 3;         // 新 bank 0x08 处没有 "KNLT"
        public const byte SelfResHdr = 4;           // 0x18 处 size 字段与宣告不符
        public const byte SelfResCrc = 5;           // CRC32 不符

        // BEGIN 响应 data[0]（仅 status=ERR 时有效）
        public const byte SelfBeginErrBank = 2;     // 无法判定运行 bank（拒绝执行，防呆）
        public const byte SelfBeginErrSize = 3;     // 镜像大小越界
        public const byte SelfBeginErrFlash = 4;    // flash 容量不足以容纳第二个 bank

        // BEGIN 响应 data[0]（仅 status=BUSY 时有效）
        public const byte SelfBeginBusySession = 5; // 已有会话在进行

        // 单 bank 最大镜像（dongle 侧 UX_SOTA_IMAGE_MAX == ota_firmware_max_size）
        public const int SelfImageMax = 0x1F000;    // 124K

        // SLOT_DATA 载荷布局（相对 DlOffsArgs）
        public const int OtaDataOffsLen = 0;         // 1..12
        public const int OtaDataOffsData = 1;
        public const int OtaDataMax = 12;            // 15B 帧预算：13 args - 1 len 字节

        // OTA STATUS 响应数据布局（相对 UlOffsData，共 12B）
        public const int OtaStsOffsState = 0;        // 流管道状态机 OtaSt*
        public const int OtaStsOffsMasterCmd = 1;    // master_ota_cmd（blm_ota.c）
        public const int OtaStsOffsStart = 2;        // host_ota_start（master OTA 步骤）
        public const int OtaStsOffsAdr = 3;          // ota_adr u32 LE：已推给鼠标的字节数
        public const int OtaStsOffsSize = 7;         // n_firmware u32 LE：本次镜像大小
        public const int OtaStsOffsResult = 11;      // master OTA 结果 OtaRes*

        // 流管道状态（OtaStsOffsState）
        public const byte OtaStIdle = 0;             // 无会话
        public const byte OtaStStream = 1;           // PC 灌入中 / dongle 推包中
        public const byte OtaStFinish = 2;           // SLOT_END 已到，正在推尾包

        // master OTA 结果（OtaStsOffsResult）
        public const byte OtaResNone = 0;            // 未运行 / 运行中
        public const byte OtaResOk = 1;              // 成功（鼠标随即重启，可能自动关机）
        public const byte OtaResFail = 2;            // 失败（链路中断/超时/设备拒绝）

        // SLOT_BEGIN 响应 data[0]（仅 status=ERR 时有效）：2 = 鼠标未连接 dongle
        // （dongle 与鼠标的 BLE 链路就是推包通路，PC 直连不算）
        public const byte OtaBeginDutNotConnected = 2;

        // SLOT_BEGIN 响应 data[0]（仅 status=BUSY 时有效）：3 = master OTA 推包进行中
        // （此时开新镜像会破坏推包；等它出结果或给 dongle 断电重启）
        public const byte OtaBeginBusyOtaPush = 3;

        // STATUS 响应数据（相对 UlOffsData）
        public const int StsOffsActive = 0;    // 1 = 产测模式激活
        public const int StsOffsDutLink = 1;   // 1 = DUT 已连接
        public const int StsOffsSppReady = 2;  // 1 = 0x33F3/0x33F4 已发现
        public const int StsOffsTickL = 3;     // 授权 tick（u16 小端）
        public const int StsOffsTickH = 4;

        // DUT_INFO 响应数据（相对 UlOffsData，镜像 ux_ft_proto.h）：本地名单独占 12B，
        // MAC 单独走 CmdDutMac（一帧 12B data 放不下 MAC 6B + 名字 18B）
        public const int DutInfoOffsName = 0;        // ≤12B 本地名（ADV 0x09/0x08 缓存，零填充/截断）
        public const int DutInfoNameLen = 12;

        // DUT_MAC 响应数据（相对 UlOffsData）
        public const int DutMacOffsMac = 0;          // 6B 对端 MAC（固件内存序 LSB 在前，显示时倒序）

        // 上行事件（[0]）
        public const byte EvtMouse = 0xE1;     // 旁路的 BLE 鼠标报告
        public const byte EvtDutLink = 0xE2;   // DUT 连接/断开
        public const byte EvtFwdRsp = 0xE3;    // DUT 响应分片（SPP 重组）

        // 鼠标事件字段（相对 EvtOffsData；双端固定 4 字节布局 btn,s8 x,s8 y,s8 wheel，镜像 ux_ft_proto.h）
        public const int MouseOffsBtn = 0;
        public const int MouseOffsX = 1;       // s8 x
        public const int MouseOffsY = 2;       // s8 y
        public const int MouseOffsWheel = 3;   // s8 wheel

        // DUT 链接事件字段（相对 EvtOffsData）
        public const int LinkOffsState = 0;
        public const byte LinkDown = 0;
        public const byte LinkUp = 1;

        // FT_FWD / EVT_FWD_RSP 分片（flags 相对 DlOffsArgs / EvtOffsData）
        public const int FwdOffsFlags = 0;     // bit7=MORE，bits[3:0]=长度（1..12）
        public const int FwdOffsData = 1;
        public const int FwdFragMax = 12;
        public const byte FwdFlagMore = 0x80;
        public const byte FwdLenMask = 0x0F;

        // ===== Surface 2: dongle ↔ DUT（SPP 20B 帧） =====
        public const int SppSize = 20;
        public const int SppOffsCmd = 0;       // [0]=cmd [1..19]=args/results
        public const int SppOffsArgs = 1;

        // DUT 命令（[0]；响应 [0] 回显命令 id）
        public const byte DutCmdPing = 0x01;       // → [1]=版本 [2]=型号 [3]=MOUSE_DATA_LEN
        public const byte DutCmdAesEnc = 0x10;     // [1..16] 明文 → [1..16] 密文
        public const byte DutCmdAesDec = 0x11;     // [1..16] 密文 → [1..16] 明文
        public const byte DutCmdAuth = 0x12;       // [1..8] challenge → [1..16] 响应
        public const byte DutCmdTouchStart = 0x20; // 开启触摸流 → [1]=DutStOk
        public const byte DutCmdTouchStop = 0x21;  // 停止触摸流 → [1]=DutStOk

        // DUT 响应状态（[1]，仅 ack 类命令）
        public const byte DutStOk = 0;
        public const byte DutStErr = 1;

        // DUT 主动 notify 事件（[0]）
        public const byte DutEvtTouch = 0x22;  // 一次触摸采样

        // PING 响应字段（相对 SppOffsArgs）
        public const int PingOffsVersion = 0;
        public const int PingOffsModel = 1;
        public const int PingOffsMouseLen = 2;

        // TOUCH 事件字段（绝对偏移）
        public const int TouchOffsPressed = 1; // 1=按下 / 0=抬起
        public const int TouchOffsPosL = 2;    // u16 位置，小端
        public const int TouchOffsPosH = 3;
        public const int TouchOffsSeq = 4;     // 采样序号（0xFF 回绕）

        // ===== factory 原生鉴权命令（镜像 ux_spp.h，SPP 通道 0x77/0x78） =====
        public const byte AuthCmd = 0x77;        // [1]=Tick → [2]=AuthResult*
        public const byte GetAuthStateCmd = 0x78; // → [2]=AuthState*
        public const byte AuthResultFail = 0;
        public const byte AuthResultAlready = 1;
        public const byte AuthResultSuccess = 2;
        public const byte AuthStateValid = 0;
        public const byte AuthStateInvalid = 1;

        // ===== 解析辅助 =====

        /// <summary>解析 0xE1 鼠标事件（15B 上行帧）。返回 false 表示帧格式不符。</summary>
        public static bool ParseMouse(byte[] frame, out byte btn, out short x, out short y, out sbyte wheel)
        {
            btn = 0; x = 0; y = 0; wheel = 0;
            if (frame == null || frame.Length < HlinkSize)
                return false;
            if (frame[EvtOffsId] != EvtMouse)
                return false;

            btn = frame[EvtOffsData + MouseOffsBtn];
            x = (short)(unchecked((sbyte)frame[EvtOffsData + MouseOffsX]));
            y = (short)(unchecked((sbyte)frame[EvtOffsData + MouseOffsY]));
            wheel = unchecked((sbyte)frame[EvtOffsData + MouseOffsWheel]);
            return true;
        }

        /// <summary>解析 0xE2 DUT 链接事件（15B 上行帧）；up=true 表示 DUT 已连接。</summary>
        public static bool ParseDutLink(byte[] frame, out bool up)
        {
            up = false;
            if (frame == null || frame.Length < HlinkSize)
                return false;
            if (frame[EvtOffsId] != EvtDutLink)
                return false;

            up = frame[EvtOffsData + LinkOffsState] == LinkUp;
            return true;
        }

        /// <summary>解析 0x22 触摸事件（20B SPP 帧，经 0xE3 重组后）。</summary>
        public static bool ParseTouch(byte[] spp, out bool pressed, out int pos, out byte seq)
        {
            pressed = false; pos = 0; seq = 0;
            if (spp == null || spp.Length < SppSize)
                return false;
            if (spp[SppOffsCmd] != DutEvtTouch)
                return false;

            pressed = spp[TouchOffsPressed] != 0;
            pos = spp[TouchOffsPosL] | (spp[TouchOffsPosH] << 8);
            seq = spp[TouchOffsSeq];
            return true;
        }

        // ===== MSTAT 计数器（鼠标链路诊断） =====

        /// <summary>计数器显示名，索引即组内序号：0..2=组0，3..5=组1。</summary>
        public static readonly string[] MstatNames =
        {
            "BLE收帧 att_rx",   // 组0：att_mouse 收到的 BLE 鼠标报告总数
            "进ring ring_in",   // 组0：写入转发环形缓冲的帧数
            "ring丢 ring_drop", // 组0：ring 缓冲满丢弃的帧数（dongle 丢帧）
            "USB发出 tx_ok",    // 组1：成功写入 USB 端点的帧数
            "EP忙排队 epq",     // 组1：端点忙转入 usb_fifo 的帧数
            "fifo丢 fifo_drop", // 组1：usb_fifo 满丢弃的帧数（dongle 丢帧，与键盘共用 FIFO）
        };

        /// <summary>解析 MSTAT 响应（15B 上行帧）：status + 3×u32 小端。返回 false 表示帧格式不符。</summary>
        public static bool ParseMstat(byte[] frame, out byte status, out uint c0, out uint c1, out uint c2)
        {
            status = 0; c0 = 0; c1 = 0; c2 = 0;
            if (frame == null || frame.Length < HlinkSize)
                return false;
            if (frame[UlOffsCmd] != (byte)(CmdMstat | RspMask))
                return false;

            status = frame[UlOffsStatus];
            int d = UlOffsData;
            c0 = Le32(frame, d);
            c1 = Le32(frame, d + 4);
            c2 = Le32(frame, d + 8);
            return true;
        }

        private static uint Le32(byte[] f, int o)
        {
            return (uint)(f[o] | (f[o + 1] << 8) | (f[o + 2] << 16) | (f[o + 3] << 24));
        }

        /// <summary>OTA STATUS 快照（0x54 响应，15B 上行帧）。</summary>
        internal sealed class OtaStatusSnapshot
        {
            public byte State;        // OtaSt*
            public byte MasterCmd;    // master_ota_cmd
            public byte Start;        // host_ota_start
            public uint Adr;          // 已推给鼠标的字节数
            public uint Size;         // slot 内镜像大小
            public byte Result;       // OtaRes*
        }

        /// <summary>解析 OTA STATUS 响应。返回 false 表示帧格式不符。</summary>
        public static bool ParseOtaStatus(byte[] frame, out OtaStatusSnapshot snap)
        {
            snap = null;
            if (frame == null || frame.Length < HlinkSize)
                return false;
            if (frame[UlOffsCmd] != (byte)(CmdOtaStatus | RspMask))
                return false;

            snap = new OtaStatusSnapshot();
            int d = UlOffsData;
            snap.State     = frame[d + OtaStsOffsState];
            snap.MasterCmd = frame[d + OtaStsOffsMasterCmd];
            snap.Start     = frame[d + OtaStsOffsStart];
            snap.Adr       = Le32(frame, d + OtaStsOffsAdr);
            snap.Size      = Le32(frame, d + OtaStsOffsSize);
            snap.Result    = frame[d + OtaStsOffsResult];
            return true;
        }
    }

    /// <summary>
    /// AES-128 辅助：与固件端 drivers/B8x aes.c（aes_encrypt/aes_decrypt，ECB 单块 16B）
    /// 使用同一密钥与语义，用于产测 AES 往返与鉴权 challenge-response 的 PC 侧比对。
    /// </summary>
    internal static class UxFtAes
    {
        /// <summary>与 DUT 固件 UX_FT_DUT_AES_KEY 完全一致的 16 字节密钥（"BIBOO-UX-FT-0001"）。</summary>
        public static readonly byte[] Key =
        {
            0x42, 0x49, 0x42, 0x4F, 0x4F, 0x2D, 0x55, 0x58,
            0x2D, 0x46, 0x54, 0x2D, 0x30, 0x30, 0x30, 0x31
        };

        /// <summary>AES-128 ECB 单块加密（无填充，16B 输入 → 16B 输出）。</summary>
        public static byte[] EncryptBlock(byte[] plain16)
        {
            return Transform(plain16, true);
        }

        /// <summary>AES-128 ECB 单块解密（无填充，16B 输入 → 16B 输出）。</summary>
        public static byte[] DecryptBlock(byte[] cipher16)
        {
            return Transform(cipher16, false);
        }

        /// <summary>0x12 AUTH 响应：8B challenge + 8B 零填充 → 16B 密文（与 DUT 侧一致）。</summary>
        public static byte[] AuthResponse(byte[] challenge8)
        {
            if (challenge8 == null || challenge8.Length != 8)
                throw new ArgumentException("challenge 必须为 8 字节", "challenge8");

            var block = new byte[16];
            Array.Copy(challenge8, 0, block, 0, 8);
            return EncryptBlock(block);
        }

        /// <summary>比较两个 16 字节块是否一致（PC 侧往返/鉴权比对）。</summary>
        public static bool BlockEquals(byte[] a, byte[] b)
        {
            if (a == null || b == null || a.Length != 16 || b.Length != 16)
                return false;
            for (int i = 0; i < 16; i++)
            {
                if (a[i] != b[i])
                    return false;
            }
            return true;
        }

        private static byte[] Transform(byte[] data, bool encrypt)
        {
            if (data == null || data.Length != 16)
                throw new ArgumentException("数据必须为 16 字节", "data");

            using (Aes aes = Aes.Create())
            {
                aes.Key = Key;
                aes.Mode = CipherMode.ECB;
                aes.Padding = PaddingMode.None;
                using (ICryptoTransform t = encrypt ? aes.CreateEncryptor() : aes.CreateDecryptor())
                {
                    return t.TransformFinalBlock(data, 0, data.Length);
                }
            }
        }
    }

    /// <summary>
    /// 0xE3（EVT_FWD_RSP）分片重组器：按 flags 的 MORE/长度 nibble 累积，
    /// 重组出完整的 20B SPP 帧（DUT 命令响应或 0x22 触摸流帧）。
    /// </summary>
    internal sealed class UxFtFragmentAssembler
    {
        private readonly List<byte> _buf = new List<byte>(32);

        public void Reset()
        {
            _buf.Clear();
        }

        /// <summary>
        /// 喂入一帧 15B 的 0xE3 上行事件帧。
        /// 返回 true 时 sppFrame 为重组完成的 SPP 帧；false 表示仍在累积，
        /// 或帧格式异常（异常时已清空累计数据，丢弃这一组）。
        /// </summary>
        public bool Feed(byte[] frame, out byte[] sppFrame)
        {
            sppFrame = null;
            if (frame == null || frame.Length < UxFtProto.HlinkSize)
                return false;

            byte flags = frame[UxFtProto.EvtOffsData + UxFtProto.FwdOffsFlags];
            int len = flags & UxFtProto.FwdLenMask;
            if (len < 1 || len > UxFtProto.FwdFragMax)
            {
                Reset();
                return false;
            }

            for (int i = 0; i < len; i++)
                _buf.Add(frame[UxFtProto.EvtOffsData + UxFtProto.FwdOffsData + i]);

            if ((flags & UxFtProto.FwdFlagMore) != 0)
                return false;

            sppFrame = _buf.ToArray();
            Reset();
            return true;
        }
    }
}
