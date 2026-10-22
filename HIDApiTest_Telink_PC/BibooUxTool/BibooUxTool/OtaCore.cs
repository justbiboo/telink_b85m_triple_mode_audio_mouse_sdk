using System;
using System.Globalization;
using System.Threading;
using System.Threading.Tasks;

namespace BibooUxTool
{
    /// <summary>
    /// Telink OTA 协议常量与 PDU 构造（移植自 tool/ota_app 与 SDK stack/ble/service/ota）。
    ///
    /// 两处来源字节级一致，只是承载不同：
    ///   ① USB HID OTA（tool/ota_app）：帧 = [report_id][0x02 opcode][len][payload]，payload 内是
    ///      [idx_lo idx_hi][16B data][crc16_lo crc16_hi]，结束帧 payload = [02 ff][max_lo max_hi][~max_lo ~max_hi]；
    ///   ② BLE OTA（SDK blm_ota.c / ota.h）：直接写 OTA 特征，
    ///      开始 = u16 CMD_OTA_START，数据 PDU = [adr_index u16][16B data][crc16 u16]，结束 = [CMD_OTA_END u16][max/!max]。
    /// 上位机统一以「PDU」表述：[idx u16][data][crc16 u16]，CRC16 覆盖 idx+data（不含 CRC 自身）。
    /// </summary>
    internal static class OtaProto
    {
        // ---------- Telink BLE OTA 命令字（stack/ble/service/ota/ota.h，client -> server） ----------
        public const ushort CmdVersion    = 0xFF00;
        public const ushort CmdStart      = 0xFF01;
        public const ushort CmdEnd        = 0xFF02;
        public const ushort CmdStartExt   = 0xFF03;
        public const ushort CmdFwVerReq   = 0xFF04;
        public const ushort CmdFwVerRsp   = 0xFF05;   // server -> client
        public const ushort CmdResult     = 0xFF06;   // server -> client

        // ---------- 结果码（ota.h 的 OTA_RESULT_*） ----------
        public const byte ResSuccess          = 0x00;
        public const byte ResPacketSeqErr     = 0x01;
        public const byte ResPacketInvalid    = 0x02;
        public const byte ResDataCrcErr       = 0x03;
        public const byte ResWriteFlashErr    = 0x04;
        public const byte ResDataIncomplete   = 0x05;
        public const byte ResFlowErr          = 0x06;
        public const byte ResFwCheckErr       = 0x07;
        public const byte ResVersionCompare   = 0x08;
        public const byte ResPduLenErr        = 0x09;
        public const byte ResFirmwareMarkErr  = 0x0A;
        public const byte ResFwSizeErr        = 0x0B;
        public const byte ResPacketTimeout    = 0x0C;
        public const byte ResTimeout          = 0x0D;

        /// <summary>USB HID OTA 的结果回包特征（tool/ota_app）：05 02 03 00 06 ff。</summary>
        public const byte RspTag      = 0x05;
        public const byte RspOkData0  = 0x02;
        public const byte RspOkData1  = 0x03;
        public const byte RspOkData2  = 0x00;
        public const byte RspOkData3  = 0x06;
        public const byte RspOkData4  = 0xFF;

        /// <summary>单个数据 PDU 承载的固件字节数（USB/BLE 两侧都是 16）。</summary>
        public const int PduDataLen = 16;

        /// <summary>完整数据 PDU 长度：idx(2) + data(16) + crc16(2)。</summary>
        public const int PduLen = 2 + PduDataLen + 2;

        /// <summary>
        /// CRC16-CCITT（反向poly 0xA001 / init 0xFFFF），与 tool/ota_app 的 crc16() 及
        /// SDK 的 crc16() 逐位一致：按位右移、LFSR 在最低位。
        /// </summary>
        public static ushort Crc16(byte[] data, int offset, int len)
        {
            ushort crc = 0xFFFF;
            for (int i = 0; i < len; i++)
            {
                ushort ds = data[offset + i];
                for (int b = 0; b < 8; b++)
                {
                    crc = (ushort)((crc >> 1) ^ (((crc ^ ds) & 1) != 0 ? 0xA001 : 0));
                    ds = (ushort)(ds >> 1);
                }
            }
            return crc;
        }

        /// <summary>开始 PDU：u16 小端 CMD_OTA_START（01 FF）。</summary>
        public static byte[] BuildStartPdu()
        {
            return new byte[] { (byte)(CmdStart & 0xFF), (byte)(CmdStart >> 8) };
        }

        /// <summary>查询固件版本 PDU：u16 小端 CMD_OTA_FW_VERSION_REQ。</summary>
        public static byte[] BuildVersionReqPdu()
        {
            return new byte[] { (byte)(CmdFwVerReq & 0xFF), (byte)(CmdFwVerReq >> 8) };
        }

        /// <summary>
        /// 数据 PDU：[adr_index_lo adr_index_hi][16B data][crc_lo crc_hi]，crc16 覆盖前 18 字节。
        ///
        /// adr_index 的语义是「固件字节地址 &gt;&gt; 4」，**不是**字节地址本身：
        ///   blm_ota.c:377  p-&gt;adr_index = ota_adr &gt;&gt; 4;
        ///   bootloader.c:247  if ((start_index + 1) != pd-&gt;cmd) → OTA_PACKET_LOSS
        /// 即每发一个 16B PDU，该字段必须严格 +1（0,1,2,…,N）。若直接填字节地址（0,16,32…），
        /// 设备收到第 2 包（期望 1，实收 16）立刻判 OTA_PACKET_LOSS(result=1) 并重启，升级必失败。
        ///
        /// 也正因为设备按「+1」校验，每 PDU 必须恰好携带 16 字节数据（20B 定长），
        /// 想靠 14B/PDU 提高帧效率是行不通的。
        /// </summary>
        public static byte[] BuildDataPdu(int fragIndex, byte[] fw, int fwLen, int dataLen)
        {
            var pdu = new byte[2 + dataLen + 2];
            int addr = fragIndex * dataLen;
            int adrIndex = addr >> 4;                      // ← Telink adr_index（16B 为一单位）
            pdu[0] = (byte)(adrIndex & 0xFF);
            pdu[1] = (byte)((adrIndex >> 8) & 0xFF);
            int n = Math.Min(dataLen, fwLen - addr);
            if (n < 0)
                n = 0;
            Array.Copy(fw, addr, pdu, 2, n);
            // 尾部不足一包的部分补 0xFF（与 flash 擦除态、以及 ota_app 的处理一致），保持 PDU 定长
            for (int i = n; i < dataLen; i++)
                pdu[2 + i] = 0xFF;
            ushort crc = Crc16(pdu, 0, 2 + dataLen);
            pdu[2 + dataLen] = (byte)(crc & 0xFF);
            pdu[3 + dataLen] = (byte)(crc >> 8);
            return pdu;
        }

        /// <summary>
        /// 结束 PDU（6B，无 CRC）：[CMD_OTA_END u16][max_lo][max_hi][~max_lo][~max_hi]。
        /// max = 最后一个数据 PDU 的 adr_index = (最后一包字节地址 &gt;&gt; 4)，
        /// 与固件 blm_ota.c:409 `adr_index_max = (ota_adr-16)&gt;&gt;4` 一致，
        /// slave 用它校验有没有丢包 / 数据是否收齐。
        /// </summary>
        public static byte[] BuildEndPdu(int lastFragIndex, int dataLen)
        {
            int adrIndex = (lastFragIndex * dataLen) >> 4;
            byte lo = (byte)(adrIndex & 0xFF);
            byte hi = (byte)((adrIndex >> 8) & 0xFF);
            return new byte[]
            {
                (byte)(CmdEnd & 0xFF), (byte)(CmdEnd >> 8),
                lo, hi, (byte)~lo, (byte)~hi
            };
        }

        /// <summary>USB HID OTA 结果回包是否表示成功（05 02 03 00 06 ff）。</summary>
        public static bool IsUsbOkResponse(byte[] rsp)
        {
            return rsp != null && rsp.Length >= 6 &&
                   rsp[0] == RspTag && rsp[1] == RspOkData0 && rsp[2] == RspOkData1 &&
                   rsp[3] == RspOkData2 && rsp[4] == RspOkData3 && rsp[5] == RspOkData4;
        }

        /// <summary>结果码 → 中文描述。</summary>
        public static string DescribeResult(byte code)
        {
            switch (code)
            {
                case ResSuccess:        return "成功";
                case ResPacketSeqErr:   return "序号错误（有重复或丢失的 PDU）";
                case ResPacketInvalid:  return "无效的 OTA 包";
                case ResDataCrcErr:     return "PDU CRC 校验失败";
                case ResWriteFlashErr:  return "写 flash 失败";
                case ResDataIncomplete: return "数据不完整（丢了尾部 PDU）";
                case ResFlowErr:        return "流程错误（命令/数据顺序不对）";
                case ResFwCheckErr:     return "固件整体 CRC 校验失败";
                case ResVersionCompare: return "版本号低于当前版本，被拒";
                case ResPduLenErr:      return "PDU 长度错误";
                case ResFirmwareMarkErr:return "固件非 Telink SDK 生成";
                case ResFwSizeErr:      return "固件大小非法";
                case ResPacketTimeout:  return "两个数据包间隔超时";
                case ResTimeout:        return "OTA 总超时";
                default:                return "未知结果码 0x" + code.ToString("X2");
            }
        }

        /// <summary>byte[] → "05 02 03 00 06 ff" 形式。</summary>
        public static string Hex(byte[] b, int offset, int len)
        {
            if (b == null)
                return "(null)";
            var sb = new System.Text.StringBuilder();
            int n = Math.Min(len, b.Length - offset);
            for (int i = 0; i < n; i++)
            {
                if (i > 0)
                    sb.Append(' ');
                sb.Append(b[offset + i].ToString("X2"));
            }
            return sb.ToString();
        }
    }

    /// <summary>
    /// Telink 固件 bin 镜像（tool/ota_app 的解析规则）：
    ///   size    = u32 @0x18
    ///   version = u32 @0x02
    ///   crc     = u32 @(size-4)
    /// 数据区取 [0, size)，末尾补 16B 0xFF（与 ota_app 一致，保证最后一个 PDU 完整）。
    /// </summary>
    internal sealed class TelinkFirmware
    {
        public uint Version;
        public uint Size;
        public uint Crc32;
        public byte[] Data;      // 长度 = Size + 16（尾部 0xFF 补齐）
        public byte[] Raw;       // 原始 bin 字节，未补齐（Dongle 自身升级 0x70..0x75 必须用这个）

        public int FragmentCount(int dataBytesPerPdu)
        {
            return (int)((Size + dataBytesPerPdu - 1) / dataBytesPerPdu);
        }

        public string VersionHex
        {
            get { return "0x" + Version.ToString("X8", CultureInfo.InvariantCulture); }
        }

        public string CrcHex
        {
            get { return "0x" + Crc32.ToString("X8", CultureInfo.InvariantCulture); }
        }

        /// <summary>解析 bin；失败抛 InvalidOperationException（消息可直接给用户看）。</summary>
        public static TelinkFirmware Parse(byte[] bin)
        {
            if (bin == null || bin.Length < 0x40)
                throw new InvalidOperationException("文件太小，不是有效的 Telink 固件（< 64 字节）");

            uint size = BitConverter.ToUInt32(bin, 0x18);
            if (size == 0 || size == 0xFFFFFFFF)
                throw new InvalidOperationException("固件大小字段非法（0x" + size.ToString("X8") + "），不是有效的 Telink bin");
            if (size > bin.Length)
                throw new InvalidOperationException("声明的大小 0x" + size.ToString("X8") + " 超过文件实际长度 " + bin.Length);
            if (size < 8)
                throw new InvalidOperationException("固件大小过小（" + size + " 字节）");

            var fw = new TelinkFirmware();
            fw.Size = size;
            fw.Version = BitConverter.ToUInt32(bin, 0x02);
            fw.Crc32 = BitConverter.ToUInt32(bin, (int)(size - 4));
            fw.Raw = bin;                                   // 原样保留，供自身升级逐字节上传
            fw.Data = new byte[size + OtaProto.PduDataLen];
            Array.Copy(bin, 0, fw.Data, 0, (int)size);
            for (int i = 0; i < OtaProto.PduDataLen; i++)
                fw.Data[size + i] = 0xFF;
            return fw;
        }
    }

    /// <summary>
    /// OTA 传输通道抽象：USB dongle（整映像透传，见 IOtaBulkChannel）与 BLE 直连（写 OTA 特征）各自实现。
    /// 帧（PDU）由 OtaSession 构造，通道只负责把 ≤ MaxFrameBytes 的字节写出去。
    /// </summary>
    internal interface IOtaChannel
    {
        string DisplayName { get; }

        /// <summary>通道一次可写的最大帧字节（USB SPP 20 / BLE OTA 特征 20）。</summary>
        int MaxFrameBytes { get; }

        /// <summary>打开通道（连接设备/发现特征）。成功返回 null。</summary>
        Task<string> OpenAsync();

        /// <summary>写一个 PDU（长度必须 ≤ MaxFrameBytes）。成功返回 null。</summary>
        Task<string> SendPduAsync(byte[] pdu);

        /// <summary>关闭通道。</summary>
        void Close();
    }

    /// <summary>OTA 会话事件（在后台线程触发，UI 侧自行切回）。</summary>
    internal sealed class OtaProgressEventArgs : EventArgs
    {
        public int Sent;
        public int Total;
        public int Percent;
        public double BytesPerSec;
        public int EtaSeconds;
    }

    /// <summary>
    /// OTA 状态机。两种模式：
    ///   ① 逐 PDU（BLE 直连）：START → DATA(0..N-1) → END → 等结果，每 PDU 16B；
    ///   ② 整映像（USB dongle，实现 IOtaBulkChannel）：PC 把整份固件写进 dongle
    ///      flash 后触发 master OTA，由 dongle 推给鼠标——RunAsync 检测到
    ///      IOtaBulkChannel 即整体委托，不走下面的逐 PDU 流程。
    /// </summary>
    internal sealed class OtaSession
    {
        private readonly TelinkFirmware _fw;
        private readonly IOtaChannel _ch;
        private readonly bool _simulate;
        private readonly int _resultTimeoutMs;
        private readonly int _interFrameMs;
        private readonly int _dataLen;

        public event EventHandler<OtaProgressEventArgs> Progress;
        public event Action<string, int> Log;          // (msg, level) level: 0=info 1=warn 2=err 3=ok
        public event Action<bool, string> Finished;    // (ok, msg)

        public OtaSession(TelinkFirmware fw, IOtaChannel ch, int dataBytesPerPdu,
                          bool simulate = true, int resultTimeoutMs = 8000, int interFrameMs = 0)
        {
            _fw = fw;
            _ch = ch;
            _dataLen = dataBytesPerPdu > 0 ? dataBytesPerPdu : OtaProto.PduDataLen;
            _simulate = simulate;
            _resultTimeoutMs = resultTimeoutMs;
            _interFrameMs = interFrameMs;
        }

        private void RaiseLog(string msg, int level) { var h = Log; if (h != null) h(msg, level); }

        private void RaiseProgress(OtaProgressEventArgs a) { var h = Progress; if (h != null) h(this, a); }

        public async Task RunAsync(CancellationToken ct)
        {
            var sw = System.Diagnostics.Stopwatch.StartNew();

            // ---- 整映像通道（USB dongle：USB 灌流 + dongle 实时推包）----
            // 流程与逐 PDU 完全不同（PC 只灌字节流 + 轮询，PDU 构造/序号/CRC/控速
            // 全在 dongle 的 Telink master OTA 框架里），直接委托给通道实现。
            var bulk = _ch as IOtaBulkChannel;
            if (bulk != null)
            {
                try
                {
                    string msg = await bulk.RunImageAsync(_fw, _resultTimeoutMs, ct, RaiseProgress, RaiseLog);
                    RaiseLog(msg, 3);
                    var f = Finished; if (f != null) f(true, msg);
                }
                catch (OperationCanceledException)
                {
                    RaiseLog("OTA 已取消", 1);
                    var c = Finished; if (c != null) c(false, "已取消");
                }
                catch (OtaException ex)
                {
                    RaiseLog("OTA 失败：" + ex.Message, 2);
                    var f = Finished; if (f != null) f(false, ex.Message);
                }
                catch (Exception ex)
                {
                    RaiseLog("OTA 异常：" + ex.Message, 2);
                    var f = Finished; if (f != null) f(false, ex.Message);
                }
                return;
            }

            int total = _fw.FragmentCount(_dataLen);
            RaiseLog(string.Format("OTA 开始：固件版本 {0}，大小 {1} 字节，每 PDU {2}B，共 {3} 个数据 PDU",
                _fw.VersionHex, _fw.Size, _dataLen, total), 0);
            if (_simulate)
                RaiseLog("模拟模式：不会真正发送数据（固件命令就绪后取消勾选即可实发）", 1);

            try
            {
                // ---- START ----
                RaiseLog("发送 START（CMD_OTA_START）...", 0);
                string err = await SendAsync(OtaProto.BuildStartPdu(), ct);
                if (err != null)
                    throw new OtaException("START 发送失败：" + err);

                // ---- DATA ----
                for (int i = 0; i < total; i++)
                {
                    ct.ThrowIfCancellationRequested();
                    byte[] pdu = OtaProto.BuildDataPdu(i, _fw.Data, (int)_fw.Size, _dataLen);
                    err = await SendAsync(pdu, ct);
                    if (err != null)
                        throw new OtaException("第 " + i + " 个 PDU 发送失败：" + err);

                    if ((i % 200 == 0) || i == total - 1)
                    {
                        double bps = sw.Elapsed.TotalSeconds > 0.01
                            ? (i + 1) * (double)_dataLen / sw.Elapsed.TotalSeconds : 0.0;
                        int pct = total > 0 ? (int)((i + 1) * 100L / total) : 100;
                        int eta = bps > 1 ? (int)((total - i - 1) * (double)_dataLen / bps) : -1;
                        var a = new OtaProgressEventArgs { Sent = i + 1, Total = total, Percent = pct, BytesPerSec = bps, EtaSeconds = eta };
                        var h = Progress; if (h != null) h(this, a);
                    }
                    // 发包节流：严格按「每 PDU N 毫秒」的预算批校准。
                    // 不能直接 Task.Delay(N)：Windows 默认定时器精度约 15.6ms，逐包睡会把
                    // 4981 包拖到 75s+，超过固件 blc_ota_setOtaProcessTimeout(60) 的 60s 总超时。
                    // 这里每 8 包对一次表，落后才补睡，误差被下一批自动吃掉。
                    // 不节流则 WinRT/控制器发送队列会溢出，write without response 静默丢包 →
                    // 设备侧序号断档 → OTA_PACKET_LOSS。
                    if (_interFrameMs > 0 && (i % 8) == 7)
                    {
                        long targetMs = (long)(i + 1) * _interFrameMs;
                        int wait = (int)(targetMs - sw.ElapsedMilliseconds);
                        if (wait > 0)
                            await Task.Delay(wait, ct);
                    }
                }

                // ---- END ----
                // 与官方 OTA 主机一致：最后一包发完留 50~60ms 再发 END，
                // 让设备把尾部数据落 flash（blm_ota.c:397 ota_lastData_tick）。
                await Task.Delay(60, ct);
                int lastIdx = total - 1;
                RaiseLog("发送 END（CMD_OTA_END，最后序号 " + lastIdx + "）...", 0);
                err = await SendAsync(OtaProto.BuildEndPdu(lastIdx, _dataLen), ct);
                if (err != null)
                    throw new OtaException("END 发送失败：" + err);

                // ---- 结果 ----
                if (_simulate)
                {
                    RaiseLog(string.Format("模拟完成：{0} 个 PDU 全部构造并通过通道接口，用时 {1:F1}s",
                        total, sw.Elapsed.TotalSeconds), 3);
                    var done = Finished; if (done != null) done(true, "模拟完成（未实发）");
                    return;
                }

                RaiseLog("END 已发出，等待设备校验并重启（" + _resultTimeoutMs + "ms）...", 0);
                // 结果回包由通道侧推送（OtaForm 订阅 IOtaChannel 的实现细节），
                // 这里只做超时兜底：实发链路下通道实现会直接调 FinishWithResult。
                await Task.Delay(_resultTimeoutMs, ct);
                // Telink 设备收到 END 后立刻走 app_ota_result → 置关机标志 → 重启，
                // 链路必然断开、不会有回包，所以「无回包」不等于失败。
                RaiseLog("未收到结果回包：设备在 END 后立即重启（固件还会置关机标志），属正常现象；"
                         + "升级是否成功看设备是否亮新固件的灯/能被重新连上", 1);
                var fin = Finished; if (fin != null) fin(true, "数据已全部发送（设备已重启，请手动上电开机）");
            }
            catch (OperationCanceledException)
            {
                RaiseLog("OTA 已取消", 1);
                var c = Finished; if (c != null) c(false, "已取消");
            }
            catch (OtaException ex)
            {
                RaiseLog("OTA 失败：" + ex.Message, 2);
                var f = Finished; if (f != null) f(false, ex.Message);
            }
            catch (Exception ex)
            {
                RaiseLog("OTA 异常：" + ex.Message, 2);
                var f = Finished; if (f != null) f(false, ex.Message);
            }
        }

        private async Task<string> SendAsync(byte[] pdu, CancellationToken ct)
        {
            if (_simulate)
            {
                await Task.Yield();   // 让出线程，保持异步语义
                return null;
            }
            if (pdu.Length > _ch.MaxFrameBytes)
                return "PDU 长度 " + pdu.Length + " 超过通道上限 " + _ch.MaxFrameBytes;
            return await _ch.SendPduAsync(pdu);
        }
    }

    /// <summary>OTA 流程错误（带可读消息）。</summary>
    internal sealed class OtaException : Exception
    {
        public OtaException(string msg) : base(msg) { }
    }
}
