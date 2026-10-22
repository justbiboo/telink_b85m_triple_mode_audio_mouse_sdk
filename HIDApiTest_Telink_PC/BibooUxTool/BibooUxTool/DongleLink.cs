using System;
using System.Threading;

namespace BibooUxTool
{
    /// <summary>
    /// dongle HID 通道连接管理 + 产测协议层（Host Link 命令/事件 + SPP 桥，见 ux_ft_proto.h）。
    /// 下行 Feature Report 0x09 与上行 Input Report 0x08 均为 15 字节 payload。
    /// </summary>
    public sealed class DongleLink : IDisposable
    {
        private const int FeaturePayloadSize = 15;
        private const int InputPayloadSize = 15;
        private IntPtr _h;
        private readonly object _sync = new object();

        // 协议层状态
        internal const byte NoSeq = 0xFF;          // 无等待者标记（seq 0..254 循环）
        private byte _seq;                        // 下行命令 seq 递增器
        private readonly UxFtFragmentAssembler _sppAsm = new UxFtFragmentAssembler();
        private byte[] _rspBuf;                   // Pump 命中的命令响应（15B）
        private byte _rspWaitSeq = NoSeq;         // 当前等待的响应 seq
        private bool _rspHit;
        private byte[] _sppBuf;                   // Pump 重组完成的 SPP 帧（20B）
        private bool _sppHit;
        private bool _sppWait;                    // SendSppFrameWait 等待中（响应由等待者消费）

        public bool IsConnected
        {
            get { lock (_sync) { return _h != IntPtr.Zero; } }
        }

        /// <summary>
        /// 枚举并打开 dongle 的独立 Host Link HID Interface。
        /// 成功返回 null；失败返回错误描述。
        /// </summary>
        public string Connect()
        {
            lock (_sync)
            {
                if (_h != IntPtr.Zero)
                    return null;

                IntPtr h = BibooHidNative.bh_open_default();
                if (h == IntPtr.Zero)
                {
                    string err = BibooHidNative.LastError();
                    return string.IsNullOrEmpty(err) ? "bh_open_default 失败" : err;
                }
                _h = h;
                return null;
            }
        }

        public void Disconnect()
        {
            lock (_sync)
            {
                if (_h != IntPtr.Zero)
                {
                    BibooHidNative.bh_close(_h);
                    _h = IntPtr.Zero;
                }
            }
        }

        /// <summary>发送 15 字节 payload。成功返回 null；失败返回错误描述。</summary>
        public string Send(byte[] payload)
        {
            lock (_sync)
            {
                if (_h == IntPtr.Zero)
                    return "未连接";
                if (payload == null || payload.Length != FeaturePayloadSize)
                    return "payload 必须为 15 字节";

                int r = BibooHidNative.bh_send(_h, payload, payload.Length);
                if (r != 0)
                {
                    string err = BibooHidNative.LastError();
                    return string.IsNullOrEmpty(err) ? "bh_send 失败" : err;
                }
                return null;
            }
        }

        /// <summary>阻塞等待设备主动发送的 Input Report。返回 0 成功 / -1 失败。</summary>
        public int TryRead(out byte[] payload, out string err)
        {
            lock (_sync)
            {
                payload = new byte[InputPayloadSize];
                err = null;
                if (_h == IntPtr.Zero)
                {
                    err = "未连接";
                    return -1;
                }

                int r = BibooHidNative.bh_read(_h, payload, payload.Length);
                if (r < 0)
                {
                    err = BibooHidNative.LastError();
                    if (string.IsNullOrEmpty(err))
                        err = "bh_read 失败";
                    return -1;
                }
                return 0;
            }
        }

        /// <summary>最多等待 timeoutMs 毫秒。返回 1=收到，0=超时，-1=失败。</summary>
        public int TryReadTimeout(int timeoutMs, out byte[] payload, out string err)
        {
            lock (_sync)
            {
                payload = new byte[InputPayloadSize];
                err = null;
                if (_h == IntPtr.Zero)
                {
                    err = "未连接";
                    return -1;
                }

                int r = BibooHidNative.bh_read_timeout(_h, payload, payload.Length, timeoutMs);
                if (r == 0)
                    return 0;
                if (r < 0)
                {
                    err = BibooHidNative.LastError();
                    if (string.IsNullOrEmpty(err))
                        err = "bh_read_timeout 失败";
                    return -1;
                }
                return 1;
            }
        }

        public string GetOpenedPath()
        {
            return BibooHidNative.OpenedPath;
        }

        // ---------- 产测协议层（Task 11）：Host Link 命令 / 事件 / SPP 桥 ----------

        /// <summary>命令响应默认超时（ms）。</summary>
        public const int CommandTimeoutMs = 1500;

        /// <summary>
        /// 上行事件帧回调（0xE1 鼠标 / 0xE2 DUT 链接 / 未知帧），15B 完整帧；
        /// 在 Pump() 的调用线程同步触发。0xE3 由 SPP 重组消化，不走此回调。
        /// </summary>
        public event Action<byte[]> FrameEvent;

        /// <summary>
        /// 重组完成的 20B SPP 帧回调（触摸流等自发帧）；主动等待
        /// （SendSppFrameWait）期间的响应由等待者消费，不触发此事件。
        /// 在 Pump() 的调用线程同步触发。
        /// </summary>
        public event Action<byte[]> SppFrameEvent;

        /// <summary>
        /// 读取鼠标 DevInf_aaa（15B）。dongle 连上鼠标后以 BLE master 身份自行读取并缓存，
        /// PC 经 0x55 分两帧（偏移 0 / 12，每帧 12B）取回。
        /// 返回 1=成功（raw 15B，可直接喂 BleDevInf.Parse）；0=鼠标未就绪；-1=失败。
        /// </summary>
        public int ReadDevInfo(int timeoutMs, out byte[] raw, out string err)
        {
            raw = new byte[UxFtProto.DevInfoTotal];
            err = null;

            int got = 0;
            while (got < UxFtProto.DevInfoTotal)
            {
                var args = new byte[1];
                args[0] = (byte)got;

                byte[] rsp;
                string e;
                int r = SendCommand(UxFtProto.CmdDevInfo, args, timeoutMs, out rsp, out e);
                if (r != 1)
                {
                    err = (e != null ? e : "DEVINFO 无响应");
                    return r;
                }

                byte st = rsp[UxFtProto.UlOffsStatus];
                if (st == UxFtProto.DevInfoStNotReady)
                {
                    err = "鼠标未连上 Dongle，或版本尚未读到（请让鼠标连上 Dongle 后再次点【连接 Dongle】）";
                    return 0;
                }
                if (st != UxFtProto.DevInfoStOk)
                {
                    err = "DEVINFO 失败（status=" + st + "）";
                    return -1;
                }

                int n = Math.Min(UxFtProto.DevInfoChunk, UxFtProto.DevInfoTotal - got);
                for (int k = 0; k < n; k++)
                    raw[got + k] = rsp[UxFtProto.UlOffsData + k];
                got += n;
            }
            return 1;
        }

        /// <summary>
        /// 发送下行命令并等待匹配响应（seq 递增 + 响应匹配）。
        /// 返回 1=收到响应（rsp 15B，rsp[2] 为 status）；0=超时；-1=失败。
        /// 等待期间的非匹配帧由 Pump() 正常分发（事件不丢失）。
        /// </summary>
        public int SendCommand(byte cmd, byte[] args, int timeoutMs, out byte[] rsp, out string err)
        {
            rsp = null;
            err = null;

            byte seq;
            lock (_sync)
            {
                if (_h == IntPtr.Zero)
                {
                    err = "未连接";
                    return -1;
                }
                seq = _seq++;
                if (seq == NoSeq)
                {
                    /*0xFF 是 Pump() 的「无等待者」哨兵（匹配守卫
                     * _rspWaitSeq != NoSeq），绝不能当 seq 用：dongle 会正常
                     * 回包，但匹配器永远拒收，命令必超时。
                     * 2026-10-20 实测：OTA 落盘第 253 帧（off=3024）正好轮到
                     * seq 0xFF，连撞两次同一偏移——dongle 日志里 RX/TX 都在，
                     * PC 侧却报响应超时。跳过着个值（注释里「seq 0..254 循环」
                     * 一早写了，只是没实现）。*/
                    seq = _seq++;
                }
                _rspWaitSeq = seq;
                _rspHit = false;
                _rspBuf = null;
            }

            var payload = new byte[FeaturePayloadSize];
            payload[UxFtProto.DlOffsCmd] = cmd;
            payload[UxFtProto.DlOffsSeq] = seq;
            if (args != null)
            {
                int n = Math.Min(args.Length, FeaturePayloadSize - UxFtProto.DlOffsArgs);
                Array.Copy(args, 0, payload, UxFtProto.DlOffsArgs, n);
            }

            string sendErr = Send(payload);
            if (sendErr != null)
            {
                lock (_sync) { _rspWaitSeq = NoSeq; }
                err = sendErr;
                return -1;
            }

            int deadline = Environment.TickCount + timeoutMs;
            while (true)
            {
                lock (_sync)
                {
                    if (_rspHit)
                    {
                        rsp = _rspBuf;
                        _rspWaitSeq = NoSeq;
                        return 1;
                    }
                }

                int remain = deadline - Environment.TickCount;
                if (remain <= 0)
                    break;

                int r = Pump(remain > 50 ? 50 : remain);
                if (r < 0)
                {
                    lock (_sync) { _rspWaitSeq = NoSeq; }
                    err = "读取上行帧失败（设备已断开？）";
                    return -1;
                }
            }

            lock (_sync) { _rspWaitSeq = NoSeq; }
            err = "等待命令响应超时（cmd=0x" + cmd.ToString("X2") + "）";
            return 0;
        }

        /// <summary>
        /// 发送一个 ≤20B 的 SPP 帧（自动 12B 分片，逐片走 0x40 FT_FWD）。
        /// 成功返回 null；失败返回错误描述。
        /// </summary>
        public string SendSppFrame(byte[] sppFrame)
        {
            if (sppFrame == null || sppFrame.Length == 0 || sppFrame.Length > UxFtProto.SppSize)
                return "SPP 帧长度必须为 1.." + UxFtProto.SppSize + " 字节";

            for (int off = 0; off < sppFrame.Length; off += UxFtProto.FwdFragMax)
            {
                int n = Math.Min(UxFtProto.FwdFragMax, sppFrame.Length - off);
                var args = new byte[1 + n];
                args[UxFtProto.FwdOffsFlags] = (byte)n;
                if (off + n < sppFrame.Length)
                    args[UxFtProto.FwdOffsFlags] |= UxFtProto.FwdFlagMore;
                Array.Copy(sppFrame, off, args, UxFtProto.FwdOffsData, n);

                byte[] rsp;
                string err;
                int r = SendCommand(UxFtProto.CmdFwd, args, CommandTimeoutMs, out rsp, out err);
                if (r == 0)
                    return "FT_FWD 分片响应超时（off=" + off + "）";
                if (r < 0)
                    return "FT_FWD 分片发送失败: " + err;

                byte status = rsp[UxFtProto.UlOffsStatus];
                if (status == UxFtProto.StBusy)
                    return "FT_FWD 忙：上一条命令尚未完成";
                if (status != UxFtProto.StOk)
                    return "FT_FWD 失败（status=" + status + "）";
            }
            return null;
        }

        /// <summary>
        /// 发送 SPP 帧并等待其响应（0xE3 重组，[0] 与请求命令匹配）。
        /// 返回 1=收到响应（sppRsp 20B）；0=超时；-1=失败。
        /// </summary>
        public int SendSppFrameWait(byte[] sppFrame, int timeoutMs, out byte[] sppRsp, out string err)
        {
            sppRsp = null;
            err = null;

            lock (_sync)
            {
                _sppWait = true;
                _sppHit = false;
                _sppBuf = null;
            }

            string sendErr = SendSppFrame(sppFrame);
            if (sendErr != null)
            {
                lock (_sync) { _sppWait = false; }
                err = sendErr;
                return -1;
            }

            byte want = sppFrame[UxFtProto.SppOffsCmd];
            int deadline = Environment.TickCount + timeoutMs;
            while (true)
            {
                byte[] got = null;
                lock (_sync)
                {
                    if (_sppHit)
                    {
                        got = _sppBuf;
                        _sppBuf = null;
                        _sppHit = false;
                    }
                }

                if (got != null)
                {
                    if (got.Length >= 1 && got[0] == want)
                    {
                        lock (_sync) { _sppWait = false; }
                        sppRsp = got;
                        return 1;
                    }

                    // 非本请求的帧（如触摸流）：转交异步事件，继续等待
                    Action<byte[]> evt = SppFrameEvent;
                    if (evt != null)
                        evt(got);
                }

                int remain = deadline - Environment.TickCount;
                if (remain <= 0)
                    break;

                int r = Pump(remain > 50 ? 50 : remain);
                if (r < 0)
                {
                    lock (_sync) { _sppWait = false; }
                    err = "读取上行帧失败（设备已断开？）";
                    return -1;
                }
            }

            lock (_sync) { _sppWait = false; }
                err = "等待 SPP 响应超时（cmd=0x" + want.ToString("X2") + "）";
            return 0;
        }

        /// <summary>
        /// 发送一帧 SELF_DATA（偏移感知 [off(4)][len(1)][data(8)]）但不阻塞等响应。
        /// seq 取自统一递增器（跳过 0xFF）。返回使用的 seq；发送失败（未连接 /
        /// 长度越界 / HID 写错误）返回 NoSeq。
        /// 用于「灌完即走、靠 STATUS(0x73) 对齐」的窗口式上传：ACK 上行不可靠时，
        /// 逐帧等 ACK 会退化为每帧 4s 超时（2026-10-21 死锁后的二次修复），故此处
        /// 不等 ACK，由调用方周期性问 dongle 的连续前沿 recv 并从缺口重发。
        /// </summary>
        public byte SendSelfDataNoWait(byte[] bin, uint off, int len)
        {
            if (bin == null || (long)off + len > bin.Length || len < 1 || len > UxFtProto.SelfDataMax)
                return NoSeq;

            byte seq;
            lock (_sync)
            {
                if (_h == IntPtr.Zero)
                    return NoSeq;
                seq = _seq++;
                if (seq == NoSeq)
                    seq = _seq++;
            }

            var payload = new byte[FeaturePayloadSize];
            payload[UxFtProto.DlOffsCmd] = UxFtProto.CmdSelfData;
            payload[UxFtProto.DlOffsSeq] = seq;

            int a = UxFtProto.DlOffsArgs;
            payload[a + UxFtProto.SelfDataOffsOff + 0] = (byte)(off);
            payload[a + UxFtProto.SelfDataOffsOff + 1] = (byte)(off >> 8);
            payload[a + UxFtProto.SelfDataOffsOff + 2] = (byte)(off >> 16);
            payload[a + UxFtProto.SelfDataOffsOff + 3] = (byte)(off >> 24);
            payload[a + UxFtProto.SelfDataOffsLen] = (byte)len;
            Array.Copy(bin, (int)off, payload, a + UxFtProto.SelfDataOffsData, len);

            string sendErr = Send(payload);
            if (sendErr != null)
                return NoSeq;
            return seq;
        }

        /// <summary>
        /// 处理一帧上行数据（阻塞至多 timeoutMs）：匹配响应 → 交给等待者；
        /// 0xE3 → SPP 重组；其余事件 → FrameEvent。
        /// 返回 2=响应命中 / 1=事件 / 0=无帧 / -1=失败。
        /// 注意：同一时间只应有唯一调用者（UI 定时器或命令等待循环）。
        /// </summary>
        public int Pump(int timeoutMs)
        {
            byte[] rx;
            string err;
            int r = TryReadTimeout(timeoutMs, out rx, out err);
            if (r == 0)
                return 0;
            if (r < 0)
                return -1;

            byte id = rx[UxFtProto.UlOffsCmd];

            // (a) 命令响应？(cmd|RspMask) 且 seq 命中当前等待者
            if ((id & UxFtProto.RspMask) != 0 && id != UxFtProto.EvtMouse &&
                id != UxFtProto.EvtDutLink && id != UxFtProto.EvtFwdRsp)
            {
                bool hit = false;
                lock (_sync)
                {
                    if (_rspWaitSeq != NoSeq && rx[UxFtProto.UlOffsSeq] == _rspWaitSeq)
                    {
                        _rspBuf = rx;
                        _rspHit = true;
                        hit = true;
                    }
                }
                if (hit)
                    return 2;
                /*无关/过时响应（如窗口式 SELF_DATA 的 ACK——PC 端不再逐帧等
                 *响应）直接丢弃，绝不当作事件转交 FrameEvent（那会污染鼠标 /
                 *DUT 链接回调）。*/
                return 0;
            }

            // (b) 0xE3 分片 → 重组
            if (id == UxFtProto.EvtFwdRsp)
            {
                byte[] spp;
                if (_sppAsm.Feed(rx, out spp))
                {
                    bool consumed = false;
                    lock (_sync)
                    {
                        if (_sppWait)
                        {
                            _sppBuf = spp;
                            _sppHit = true;
                            consumed = true;
                        }
                    }

                    if (!consumed)
                    {
                        Action<byte[]> evt = SppFrameEvent;
                        if (evt != null)
                            evt(spp);
                    }
                }
                return 1;
            }

            // (c) 其他事件（0xE1 鼠标 / 0xE2 链接 / 未知）
            Action<byte[]> cb = FrameEvent;
            if (cb != null)
                cb(rx);
            return 1;
        }

        public void Dispose()
        {
            Disconnect();
        }
    }

    /// <summary>
    /// Dongle A/B bank 布局（cmd 0x74 SELF_INFO 回读）。镜像 ux_self_ota.h 的
    /// SelfInfoOffs* 常量。RunBank = 正在运行的 bank（0 或 0x20000），
    /// OtaBank = 新镜像写入的 bank（另一个），BankSize = 单 bank 容量
    /// （线上字段即 ota_program_bootAddr）。MaxImage 不上线，取 PC 侧常量
    /// SelfImageMax（镜像 ota_firmware_max_size）。
    /// </summary>
    public sealed class DongleBankInfo
    {
        public uint RunBank;
        public uint OtaBank;
        public uint BankSize;
        public uint MaxImage;

        public string RunBankText { get { return "0x" + RunBank.ToString("X5"); } }
        public string OtaBankText { get { return "0x" + OtaBank.ToString("X5"); } }
    }

    /// <summary>Dongle 自身升级进度（SelfUpgrade 的 onProgress 参数）。</summary>
    public sealed class SelfOtaProgress
    {
        public int Percent;
        public long Sent;
        public long Total;
        public double BytesPerSec;
        public double EtaSeconds = -1;
        public string Phase = "";
    }

    /// <summary>
    /// Dongle 自身固件升级（host link 0x70..0x75，镜像 vendor/user/self_ota）。
    /// 与鼠标 OTA（0x50..0x54，镜像流过 dongle 不落 flash）相反：这里的镜像就是
    /// dongle 自己的固件，写进它 flash 的备用 A/B bank。COMMIT 之前运行中的固件
    /// 一根字节都没被碰过——上传/校验失败或中途拔线，dongle 照旧跑旧固件。
    /// </summary>
    public static class DongleSelfOta
    {
        /// <summary>每帧数据字节数（15B 帧预算：13 args - 1 len 字节）。</summary>
        public const int ChunkMax = UxFtProto.SelfDataMax;

        /// <summary>帧间隔（ms）。灌 flash 比灌 BLE 快得多，4ms 足够，且给 sector 擦除留出窗口。</summary>
        public const int DefaultPaceMs = 4;

        /// <summary>END 校验超时（ms）：dongle 要把整个镜像读回来算 CRC32。</summary>
        public const int VerifyTimeoutMs = 60000;

        /// <summary>
        /// DATA 帧响应超时（ms）。比普通命令宽：一帧可能正好撞上一次 4KB sector
        /// 擦除（dongle 全程关中断等 flash busy，实测可达数百 ms），叠加 BLE
        /// 链路重连的抖动，1.5s 的普通命令超时会把正常帧误判为丢帧。
        /// </summary>
        public const int DataTimeoutMs = 4000;

        /// <summary>COMMIT 响应超时（ms）：响应发出后 dongle 才作废旧 bank 并重启。</summary>
        public const int CommitTimeoutMs = 8000;

        /// <summary>
        /// BEGIN 响应超时（ms）：dongle 现在会先把整个目标 bank 区间擦完（每
        /// 个 4KB sector 约 79ms 关中断）再回 OK，PC 在此期间阻塞在 BEGIN 的
        /// 读响应上、不会发新的 SET_REPORT，所以这段长关中断是安全的（不会
        /// 卡死 USB 控制端点）。最大镜像 124KB≈31 sector≈2.4s，留足余量。
        /// </summary>
        public const int BeginTimeoutMs = 30000;

        // Telink bin CRC32：nibble 表算法（poly 0xEDB88320 / init 0xFFFFFFFF / 无最终异或），
        // 覆盖除末 4 字节外的全部。与 ux_self_ota.c / tl_check_fw 完全一致。
        private static readonly uint[] Crc32HalfTbl = new uint[]
        {
            0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
            0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
            0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
            0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
        };

        /// <summary>计算 Telink bin 的 CRC32（与固件端 ux_self_ota.c sota_verify 同算法）。</summary>
        public static uint ComputeBinCrc(byte[] bin)
        {
            if (bin == null || bin.Length < 8)
                throw new ArgumentException("bin 太短", "bin");
            uint crc = 0xFFFFFFFF;
            int n = bin.Length - 4;
            for (int i = 0; i < n; i++)
            {
                byte b = bin[i];
                crc = Crc32HalfTbl[(crc ^ (b & 0x0F)) & 0x0F] ^ (crc >> 4);
                crc = Crc32HalfTbl[(crc ^ (b >> 4)) & 0x0F] ^ (crc >> 4);
            }
            return crc;
        }

        /// <summary>
        /// 本地预检：大小越界 / "KNLT" 缺失 / 0x18 size 字段不符 / CRC32 不符。
        /// 返回 null 表示通过，否则为错误描述（上传前就拦住，省得白传 70KB）。
        /// </summary>
        public static string PreCheck(byte[] bin)
        {
            if (bin == null || bin.Length < 32)
                return "固件文件太小（<32 字节），不是有效的 Telink bin";
            if (bin.Length > UxFtProto.SelfImageMax)
                return "镜像 " + bin.Length + " 字节超过单 bank 上限 " + UxFtProto.SelfImageMax + " 字节";
            if (bin[8] != 'K' || bin[9] != 'N' || bin[10] != 'L' || bin[11] != 'T')
                return "0x08 处不是 \"KNLT\"（Telink 魔数缺失）";
            uint sz = (uint)(bin[0x18] | (bin[0x19] << 8) | (bin[0x1A] << 16) | (bin[0x1B] << 24));
            if (sz != bin.Length)
                return "0x18 处 size 字段 " + sz + " 与实际文件长度 " + bin.Length + " 不符";
            uint stored = (uint)(bin[bin.Length - 4] | (bin[bin.Length - 3] << 8) |
                                 (bin[bin.Length - 2] << 16) | (bin[bin.Length - 1] << 24));
            uint calc = ComputeBinCrc(bin);
            if (stored != calc)
                return "整体 CRC32 不符（文件内 0x" + stored.ToString("X8") + "，实算 0x" + calc.ToString("X8") + "）";
            return null;
        }

        /// <summary>
        /// 查询运行固件版本（cmd 0x76，只读、无副作用）：hdr = 运行 bank 镜像头 0x02 处版本，
        /// build = dongle 编译期 build 号。原地覆盖 COMMIT 下运行 bank 恒为 0x00000，
        /// build 号是升级是否真正落地的唯一可见证据。
        /// 返回 1=成功；0=超时；-1=失败。旧 dongle 固件无此命令（超时）。
        /// </summary>
        public static int QuerySelfVersion(DongleLink link, int timeoutMs,
                                           out uint hdr, out uint build, out string err)
        {
            hdr = 0;
            build = 0;
            err = null;
            byte[] rsp;
            int r = link.SendCommand(UxFtProto.CmdSelfVersion, null, timeoutMs, out rsp, out err);
            if (r != 1)
                return r;
            if (rsp[UxFtProto.UlOffsStatus] != UxFtProto.StOk)
            {
                err = "0x76 状态异常：0x" + rsp[UxFtProto.UlOffsStatus].ToString("X2");
                return -1;
            }
            if (rsp.Length < UxFtProto.UlOffsData + 8)
            {
                err = "0x76 响应数据不完整（" + rsp.Length + "B）";
                return -1;
            }
            hdr = Rd32(rsp, UxFtProto.UlOffsData + UxFtProto.SelfVerOffsHdr);
            build = Rd32(rsp, UxFtProto.UlOffsData + UxFtProto.SelfVerOffsBuild);
            return 1;
        }

        /// <summary>
        /// 查询 A/B bank 布局（cmd 0x74，只读、无副作用）。
        /// 返回 1=成功；0=超时；-1=失败。
        /// </summary>
        public static int QueryBankInfo(DongleLink link, int timeoutMs, out DongleBankInfo info, out string err)
        {
            info = null;
            err = null;
            byte[] rsp;
            int r = link.SendCommand(UxFtProto.CmdSelfInfo, null, timeoutMs, out rsp, out err);
            if (r != 1)
            {
                if (r == 0)
                    err = (err ?? "超时") + "（Dongle 固件可能过旧，不含自身升级模块 ux_self_ota）";
                return r;
            }
            if (rsp[UxFtProto.UlOffsStatus] != UxFtProto.StOk)
            {
                err = "Dongle 不支持自身升级命令（0x74）：请先把 Dongle 固件升到含 ux_self_ota 的版本";
                return -1;
            }
            /*数据区 12B：run / ota / bootAddr。先验长度再读——
             *2026-10-21 修复：旧版没加 UlOffsData 基准且直接 Rd32(rsp,12)，
             *rsp[15] 越界，点「查询 Bank」直接 UI 崩溃。*/
            if (rsp.Length < UxFtProto.UlOffsData + 12)
            {
                err = "INFO 响应数据不完整（" + rsp.Length + "B）";
                return -1;
            }
            info = new DongleBankInfo();
            info.RunBank  = Rd32(rsp, UxFtProto.UlOffsData + UxFtProto.SelfInfoOffsRun);
            info.OtaBank  = Rd32(rsp, UxFtProto.UlOffsData + UxFtProto.SelfInfoOffsOta);
            info.BankSize = Rd32(rsp, UxFtProto.UlOffsData + UxFtProto.SelfInfoOffsBoot);
            /*max image 不上线（12B 装不下第 4 个 u32）：用 PC 侧镜像常量，
             *dongle BEGIN 处还会用自己的 ota_firmware_max_size 再查一次。*/
            info.MaxImage = UxFtProto.SelfImageMax;
            return 1;
        }

        /// <summary>
        /// 阶段一：上传镜像到备用 bank 并等 dongle 校验（BEGIN/DATA/END）。
        /// 到此为止运行中的固件未被触碰，失败/取消都安全。
        /// 返回 1=校验通过（可 COMMIT）；0=取消；-1=失败（err 为描述）。
        /// </summary>
        public static int Upload(DongleLink link, byte[] bin, int paceMs,
                                 Action<SelfOtaProgress> onProgress,
                                 Action<string, int> onLog,
                                 CancellationToken ct)
        {
            string bad = PreCheck(bin);
            if (bad != null)
            {
                Log(onLog, "本地预检未通过：" + bad, 2);
                return -1;
            }

            DongleBankInfo bi;
            string e;
            int r = QueryBankInfo(link, DongleLink.CommandTimeoutMs, out bi, out e);
            if (r != 1)
            {
                Log(onLog, "查询 bank 布局失败：" + (e ?? "无响应"), 1);
                return -1;
            }
            Log(onLog, string.Format("A/B bank：运行 {0} → 新镜像写 {1}（单 bank {2} 字节，上限 {3} 字节）",
                bi.RunBankText, bi.OtaBankText, bi.BankSize, bi.MaxImage), 0);
            if (bi.OtaBank == bi.RunBank || (bi.OtaBank != 0 && bi.OtaBank != bi.BankSize))
            {
                Log(onLog, "bank 布局异常（run=" + bi.RunBankText + " ota=" + bi.OtaBankText +
                           " size=" + bi.BankSize + "），拒绝升级", 2);
                return -1;
            }
            if (bin.Length > bi.MaxImage)
            {
                Log(onLog, "镜像 " + bin.Length + " 字节超过该 Dongle 的上限 " + bi.MaxImage + " 字节", 2);
                return -1;
            }

            // ---- BEGIN ----
            var beginArgs = new byte[4];
            beginArgs[0] = (byte)bin.Length;
            beginArgs[1] = (byte)(bin.Length >> 8);
            beginArgs[2] = (byte)(bin.Length >> 16);
            beginArgs[3] = (byte)(bin.Length >> 24);

            byte[] rsp;
            r = link.SendCommand(UxFtProto.CmdSelfBegin, beginArgs, BeginTimeoutMs, out rsp, out e);
            if (r != 1)
            {
                Log(onLog, "BEGIN 无响应（" + (e ?? "超时") + "）", 2);
                return -1;
            }
            if (rsp[UxFtProto.UlOffsStatus] == UxFtProto.StBusy)
            {
                Log(onLog, "Dongle 已有升级会话在进行（等它结束或给 Dongle 断电重启）", 2);
                return -1;
            }
            if (rsp[UxFtProto.UlOffsStatus] != UxFtProto.StOk)
            {
                Log(onLog, "BEGIN 被拒绝：" + DescribeBeginErr(rsp), 2);
                return -1;
            }
            Log(onLog, "BEGIN 成功，开始灌入 " + bin.Length + " 字节（8B/帧）…", 3);

            // ---- DATA（偏移感知帧 + 逐帧等 ACK + STATUS 兜底重对齐）----
            // 2026-10-21 第三轮修复：上一版「灌完即走、250ms 后才读 ACK」实测只过 16 帧就死。
            // 根因＝Windows HID 的 SET_REPORT 是 USB 控制传输，其 status 阶段要等主机读回
            // 输入报告（即 ACK）才算完成；连发多帧却不读 ACK，这些控制传输全卡在 status
            // 阶段、dongle 的 EP0 被占死，后续 SET_REPORT（含 STATUS 0x73）根本到不了 dongle
            // ——dongle 侧 rx_ok 恒为 16 可为证。逐帧 SendCommand（发→Pump 读 ACK）每帧都及时
            // 收尾，EP0 始终空闲。偏移感知帧（ux_self_ota.h）使重发幂等、不重复计数；
            // 万一 ACK 真丢，走 STATUS 问真实前沿再从缺口重发。
            var sw = System.Diagnostics.Stopwatch.StartNew();
            long off = 0;                    // 下一个要发的字节偏移（顺序发送）
            int stall = 0;                   // 连续 BUSY 计数
            int sinceStatus = 0;             // 距上次 STATUS 校验的帧数
            var prog = new SelfOtaProgress { Total = bin.Length };
            const int dataTimeoutMs = 1200;  // 单帧 ACK 等待；丢包即走 STATUS 重对齐

            while (off < bin.Length)
            {
                if (ct.IsCancellationRequested)
                {
                    Log(onLog, "已取消（Dongle 侧会话会在 20s 无数据后自动作废，运行中的固件不受影响）", 1);
                    return 0;
                }

                int n = (int)Math.Min(ChunkMax, bin.Length - off);
                byte[] args = new byte[UxFtProto.SelfDataOffsData + n];
                args[UxFtProto.SelfDataOffsLen] = (byte)n;
                args[UxFtProto.SelfDataOffsOff + 0] = (byte)(off);
                args[UxFtProto.SelfDataOffsOff + 1] = (byte)(off >> 8);
                args[UxFtProto.SelfDataOffsOff + 2] = (byte)(off >> 16);
                args[UxFtProto.SelfDataOffsOff + 3] = (byte)(off >> 24);
                Array.Copy(bin, (int)off, args, UxFtProto.SelfDataOffsData, n);

                int dr = link.SendCommand(UxFtProto.CmdSelfData, args, dataTimeoutMs, out rsp, out e);

                if (dr == 1 && rsp[UxFtProto.UlOffsStatus] == UxFtProto.StOk)
                {
                    off += n;
                    sinceStatus++;
                    stall = 0;
                }
                else if (dr == 1 && rsp[UxFtProto.UlOffsStatus] == UxFtProto.StBusy)
                {
                    // flash 写入暂时跟不上（pre-erase 后正常不会，仅防御）：极短等，重发同帧（幂等）
                    if (++stall > 200)
                    {
                        Log(onLog, "Dongle 持续 BUSY，写入通路异常，中止", 2);
                        return -1;
                    }
                    System.Threading.Thread.Sleep(2);
                    continue;
                }
                else if (dr < 0)
                {
                    // 下行 SET_REPORT 本身写失败（不是 ACK 超时）：HID 通道已坏——设备
                    // 可能已断开/被 Windows 重新枚举/USB 控制端点卡死（EP0 卡死时一次
                    // SET_REPORT 会挂 ~5s 才返回错误，再连发 3 次 STATUS 只会白挂 15s）。
                    // 直接中止；dongle 会话 20s 无数据自动作废，运行中固件未被触碰，
                    // 重新开始升级即可（预擦除幂等）。2026-10-22 实测：灌入中途写失败
                    // 后旧代码仍去问 3 次 STATUS，白等且报错误因被掩盖（只显示最后一次
                    // STATUS 的发送失败）。
                    Log(onLog, "下行 HID 写失败（" + (e ?? "?") + "）——Dongle 可能已断开/重新枚举/USB 卡死，中止（运行中固件未被触碰）", 2);
                    return -1;
                }
                else
                {
                    // ACK 超时（写入本身成功）或被拒：用 STATUS 问真实前沿，从缺口幂等重发（不动已写区）
                    byte st = 0, rs = 0; long rv = 0, tt = 0; uint dcrc = 0; string se = null;
                    int qr = 0;
                    for (int a = 0; a < 3; a++)
                    {
                        qr = QueryStatus(link, out st, out rs, out rv, out tt, out dcrc, out se);
                        if (qr == 1) break;
                        System.Threading.Thread.Sleep(30);
                    }
                    if (qr != 1)
                    {
                        Log(onLog, "DATA 帧无响应且 STATUS 持续无响应（" + (se ?? "超时") + "），中止（运行中固件未被触碰）", 2);
                        return -1;
                    }
                    if (st == UxFtProto.SelfStIdle)
                    {
                        // STATUS 通了但会话已是 IDLE：dongle 已把会话作废（20s 无数据
                        // 看门狗或 Dongle 重启过）。此时继续重发只会每帧收 ERR、前沿恒
                        // 为 0，永不收敛——明确中止让用户重新开始（运行中固件未被触碰）。
                        Log(onLog, "Dongle 侧会话已作废（20s 无数据看门狗或 Dongle 重启过），中止（运行中固件未被触碰）", 2);
                        return -1;
                    }
                    long frontier = rv;
                    if (frontier >= bin.Length)
                    {
                        off = bin.Length;       // 已全部写入（丢的只是 ACK）
                        break;
                    }
                    if (frontier > off)
                        off = frontier;          // 本帧实际已写，跳到缺口
                    // frontier <= off：重发当前帧（幂等、无害）
                    Log(onLog, string.Format(
                        "DATA 帧无响应，STATUS 前沿 {0}/{1}，从 {2} 重发", frontier, bin.Length, off), 1);
                }

                // 周期性校验前沿一致（每 512 帧），防止静默错位
                if (sinceStatus >= 512)
                {
                    sinceStatus = 0;
                    byte st = 0, rs = 0; long rv = 0, tt = 0; uint dcrc = 0; string se = null;
                    if (QueryStatus(link, out st, out rs, out rv, out tt, out dcrc, out se) == 1
                        && rv != off && rv < bin.Length)
                    {
                        Log(onLog, string.Format("前沿回读 {0} 与发送偏移 {1} 不符，跳转到缺口", rv, off), 1);
                        off = rv;
                    }
                }

                if (paceMs > 0)
                    System.Threading.Thread.Sleep(paceMs);

                if (onProgress != null)
                {
                    double el = sw.Elapsed.TotalSeconds;
                    prog.Sent = off;
                    prog.Percent = (int)(off * 100 / bin.Length);
                    prog.BytesPerSec = el > 0.001 ? off / el : 0;
                    prog.EtaSeconds = prog.BytesPerSec > 1 ? (bin.Length - off) / prog.BytesPerSec : -1;
                    prog.Phase = "上传中";
                    onProgress(prog);
                }
            }
            Log(onLog, string.Format("灌入完成：{0} 字节，{1:F1} B/s，等 Dongle 校验…",
                bin.Length, bin.Length / Math.Max(0.001, sw.Elapsed.TotalSeconds)), 0);

            // ---- END（dongle 侧要读回整个镜像算 CRC32，可能几秒）----
            if (onProgress != null)
            {
                prog.Phase = "校验中";
                onProgress(prog);
            }
            r = link.SendCommand(UxFtProto.CmdSelfEnd, null, VerifyTimeoutMs, out rsp, out e);
            if (r != 1)
            {
                Log(onLog, "END 无响应（" + (e ?? "超时") + "）", 2);
                return -1;
            }
            byte res = rsp.Length > UxFtProto.UlOffsData ? rsp[UxFtProto.UlOffsData] : (byte)0;
            if (rsp[UxFtProto.UlOffsStatus] != UxFtProto.StOk || res != UxFtProto.SelfResOk)
            {
                Log(onLog, "镜像校验失败：" + DescribeVerifyErr(res) +
                           "（运行中的固件未被触碰，可重新上传）", 2);
                return -1;
            }
            Log(onLog, "镜像校验通过（字节数 / KNLT / size 字段 / CRC32 全部一致）", 3);
            return 1;
        }

        /// <summary>
        /// 阶段二：COMMIT（cmd 0x75）——不可逆点。dongle 把校验过的新镜像覆盖写入运行区并自动重启（约 20s），
        /// 新固件从这一刻开始运行。返回 1=已提交；0=状态未知（响应超时）；
        /// -1=被拒绝。
        /// </summary>
        public static int Commit(DongleLink link, Action<string, int> onLog)
        {
            byte[] rsp;
            string e;
            int r = link.SendCommand(UxFtProto.CmdSelfCommit, null, CommitTimeoutMs, out rsp, out e);
            if (r == 1 && rsp[UxFtProto.UlOffsStatus] == UxFtProto.StOk)
            {
                Log(onLog, "COMMIT 已确认：Dongle 正在覆盖写入运行区并自动重启（约 20 秒，请勿断电）", 3);
                return 1;
            }
            if (r == 1)
            {
                Log(onLog, "COMMIT 被拒绝（status=" + rsp[UxFtProto.UlOffsStatus] +
                           "）：只有校验通过的镜像才能提交", 2);
                return -1;
            }
            // 响应没等到：dongle 可能在发出响应前就重启了。不能假定成功，
            // 让调用方去确认 Dongle 是否真的重启了。
            Log(onLog, "COMMIT 响应未收到（" + (e ?? "超时") + "）：请观察 Dongle 是否已重启", 1);
            return 0;
        }

        /// <summary>查询会话状态（cmd 0x73）。返回 1=成功；0=超时；-1=失败。
        /// crc 恒为 0：协议数据区只有 10B（state/result/recv/total），
        /// CRC32 不在线上（见 UxFtProto.SelfStsOffs* 注释）。</summary>
        public static int QueryStatus(DongleLink link, out byte state, out byte result,
                                      out long recv, out long total, out uint crc, out string err)
        {
            state = 0; result = 0; recv = 0; total = 0; crc = 0; err = null;
            byte[] rsp;
            int r = link.SendCommand(UxFtProto.CmdSelfStatus, null, DongleLink.CommandTimeoutMs, out rsp, out err);
            if (r != 1)
                return r;
            /*先验长度再读——2026-10-21 修复：旧版 Rd32(rsp,13) 读 crc，
             *rsp[15] 越界（crc 字段本来也放不进 12B 数据区）。*/
            if (rsp.Length < UxFtProto.UlOffsData + 10)
            {
                err = "STATUS 响应数据不完整（" + rsp.Length + "B）";
                return -1;
            }
            state  = rsp[UxFtProto.UlOffsData + UxFtProto.SelfStsOffsState];
            result = rsp[UxFtProto.UlOffsData + UxFtProto.SelfStsOffsResult];
            recv   = Rd32(rsp, UxFtProto.UlOffsData + UxFtProto.SelfStsOffsRecv);
            total  = Rd32(rsp, UxFtProto.UlOffsData + UxFtProto.SelfStsOffsTotal);
            return 1;
        }

        private static uint Rd32(byte[] rsp, int off)
        {
            if (rsp == null || off < 0 || off + 4 > rsp.Length)
                return 0;
            return (uint)(rsp[off] | (rsp[off + 1] << 8) | (rsp[off + 2] << 16) | (rsp[off + 3] << 24));
        }

        private static void Log(Action<string, int> onLog, string msg, int level)
        {
            if (onLog != null)
                onLog(msg, level);
        }

        private static string DescribeBeginErr(byte[] rsp)
        {
            if (rsp.Length <= UxFtProto.UlOffsData)
                return "未知原因";
            byte d = rsp[UxFtProto.UlOffsData];
            switch (d)
            {
                case UxFtProto.SelfBeginErrBank: return "无法判定运行 bank（防呆拒绝）";
                case UxFtProto.SelfBeginErrSize: return "镜像大小越界";
                case UxFtProto.SelfBeginErrFlash: return "flash 容量不足以容纳第二个 bank";
                case UxFtProto.SelfBeginBusySession: return "已有会话在进行";
                default: return "data[0]=" + d;
            }
        }

        private static string DescribeVerifyErr(byte res)
        {
            switch (res)
            {
                case UxFtProto.SelfResSize: return "实收字节数与宣告不符";
                case UxFtProto.SelfResMagic: return "新 bank 0x08 处没有 \"KNLT\"";
                case UxFtProto.SelfResHdr: return "0x18 处 size 字段与宣告不符";
                case UxFtProto.SelfResCrc: return "CRC32 不符（镜像损坏或写入丢字节）";
                default: return "result=" + res;
            }
        }
    }
}
