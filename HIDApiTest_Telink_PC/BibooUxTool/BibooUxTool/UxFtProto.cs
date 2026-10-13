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
        public const byte CmdStart = 0x10;           // 进入产测模式（鼠标报告旁路 → 0xE1）
        public const byte CmdStop = 0x11;            // 退出产测模式，恢复 USB 转发
        public const byte CmdStatus = 0x12;          // 查询 active / DUT / SPP / tick
        public const byte CmdDutDisconnect = 0x13;   // 断开当前 DUT（产线换机）
        public const byte CmdFwd = 0x40;             // DUT 命令分片，经 SPP 0x33F3 转发

        // STATUS 响应数据（相对 UlOffsData）
        public const int StsOffsActive = 0;    // 1 = 产测模式激活
        public const int StsOffsDutLink = 1;   // 1 = DUT 已连接
        public const int StsOffsSppReady = 2;  // 1 = 0x33F3/0x33F4 已发现
        public const int StsOffsTickL = 3;     // 授权 tick（u16 小端）
        public const int StsOffsTickH = 4;

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
