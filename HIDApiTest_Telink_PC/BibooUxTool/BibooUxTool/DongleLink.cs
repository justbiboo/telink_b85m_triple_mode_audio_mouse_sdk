using System;

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
        private const byte NoSeq = 0xFF;          // 无等待者标记（seq 0..254 循环）
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
                // 过时/无关响应：按事件转交，便于 UI 观察
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
}
