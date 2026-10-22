using System;
using System.Threading;
using System.Threading.Tasks;

namespace BibooUxTool
{
    /// <summary>
    /// 整映像升级通道（可选能力）：USB dongle 不走「逐 PDU 写设备」，而是
    ///   ① PC 经 USB 把整份固件字节流灌进 dongle（12B/帧，见 ux_ota_slot.h）；
    ///   ② dongle 不落 flash，由 Telink master OTA（blm_ota.c proc_ota）实时
    ///      组 20B PDU、通过它到鼠标的 BLE 链路推包（连接间隔自动切 10ms，
    ///      TX FIFO 自动控速）——灌入与推包并行；
    ///   ③ PC 轮询 dongle 的 OTA 状态快照，直到 master OTA 出结果。
    /// 鼠标（slave）固件零改动：它本来就是 Telink OTA server（BLE_OTA_SERVER_ENABLE=1），
    /// dongle 发的 20B PDU 与 PC 直连 BLE 通道用的格式字节级同构。
    /// 实现此接口时 OtaSession 把整个流程委托给通道，进度/日志经回调上报；失败抛 OtaException。
    /// </summary>
    internal interface IOtaBulkChannel
    {
        Task<string> RunImageAsync(TelinkFirmware fw, int resultTimeoutMs, CancellationToken ct,
                                   Action<OtaProgressEventArgs> onProgress, Action<string, int> onLog);
    }

    /// <summary>
    /// USB dongle OTA 通道（2026-10-20 透传重写）：PC 灌字节流 → dongle 实时推包。
    ///
    /// 历史方案（均已废弃）：
    /// ① 最旧：PC 逐 PDU 经 SPP 0x33F3 透传给鼠标——要求鼠标 SPP 新增 OTA 命令分支，
    ///    且 dongle FT 的 12B 分片每片一次 USB 往返，实测 ~45 片/s，4981 包远超固件 60s 预算；
    /// ② 落盘：PC 把整份固件写进 dongle flash 0x20000 再 TRIGGER——每次升级擦写 80KB
    ///    dongle flash（磨损 + ~800ms irq-off 擦除阻塞，是 mouse BLE 掉链的头号嫌疑），
    ///    且落盘与推包串行，总耗时 55s+。
    /// 现方案把 PDU 构造/序号/CRC/控速全部交给 dongle 里 Telink 原厂验证过的 master OTA
    /// 框架，PC 只负责灌字节流 + 轮询结果；镜像不碰 dongle flash。
    /// </summary>
    internal sealed class OtaChannelUsbDongle : IOtaChannel, IOtaBulkChannel
    {
        private readonly DongleLink _link;
        private readonly bool _ownsLink;

        /// <summary>BEGIN 的短等待（ms）：只等「立即类」响应（拒绝/错误）。
        /// 成功 OK 被固件推迟到 master OTA 引擎真正推包时才回（step≥6），所以这里
        /// 超时不等于失败——PC 立即开始灌流，DATA 的逐帧 ack 就是会话已建立的证据。
        /// 2026-10-20 20:51 实测：干等 OK 会让 dongle 10s rx 空闲看门狗误杀会话
        /// （PC 未发帧 → 看门狗 abort → BEGIN 永远等不到确认）。</summary>
        private const int BeginTimeoutMs = 5000;

        /// <summary>END 的等待上限（ms）：透传模式下即时响应，15s 纯属余量。</summary>
        private const int EndTimeoutMs = 15000;

        /// <summary>SLOT_DATA 单帧 ack 超时（ms）。
        /// 2026-10-20 实测：默认 1500ms 在第 252 帧误杀——dongle 主循环同时要跑
        /// BLE 栈 + 推包，偶发停顿会让 ack 迟到。3000ms 仍远小于「真丢帧」的代价，
        /// 且 BUSY 重试有独立通道。</summary>
        private const int SlotDataTimeoutMs = 3000;

        public OtaChannelUsbDongle(DongleLink link, bool ownsLink)
        {
            _link = link;
            _ownsLink = ownsLink;
        }

        public string DisplayName
        {
            get { return "USB Dongle（固件经 USB 流入 Dongle，实时转成 OTA PDU 走 BLE 推给鼠标，不写 Dongle flash）"; }
        }

        public int MaxFrameBytes
        {
            get { return 20; }                       // 整映像模式不使用逐 PDU 接口，保留以兼容 IOtaChannel
        }

        public async Task<string> OpenAsync()
        {
            string err = _link.Connect();
            if (err != null)
                return "连接 Dongle 失败：" + err;

            // 探活兼能力校验：STATUS（0x54）只有带 OTA 流管道模块的 dongle 固件才会应答。
            byte[] rsp;
            int r = _link.SendCommand(UxFtProto.CmdOtaStatus, null, DongleLink.CommandTimeoutMs, out rsp, out err);
            if (r != 1)
                return "Dongle 无 OTA 状态响应：固件可能过旧（不含 OTA 流管道命令 0x50..0x52/0x54），"
                     + "请先把 Dongle 固件升级到 2026-10-20 后版本";
            if (rsp[UxFtProto.UlOffsStatus] != UxFtProto.StOk)
                return "Dongle 拒绝 OTA 状态查询（status=" + rsp[UxFtProto.UlOffsStatus] + "）";
            await Task.Yield();
            return null;
        }

        /// <summary>整映像模式不逐 PDU 发送；保留仅为满足 IOtaChannel（OtaSession 不会调用）。</summary>
        public Task<string> SendPduAsync(byte[] pdu)
        {
            return Task.FromResult("本通道为整映像模式（USB 灌流 + Dongle 实时推包），不逐 PDU 发送");
        }

        public void Close()
        {
            if (_ownsLink && _link != null)
            {
                try { _link.Dispose(); }
                catch (Exception) { /* 关闭失败不影响主流程 */ }
            }
        }

        /// <summary>查询一次 dongle OTA 状态，格式化成一行诊断文本（失败返回空串，绝不抛异常）。</summary>
        private string QueryStatusText()
        {
            try
            {
                byte[] rsp;
                string err;
                int r = _link.SendCommand(UxFtProto.CmdOtaStatus, null, DongleLink.CommandTimeoutMs,
                                          out rsp, out err);
                if (r != 1 || rsp[UxFtProto.UlOffsStatus] != UxFtProto.StOk)
                    return "";
                UxFtProto.OtaStatusSnapshot snap;
                if (!UxFtProto.ParseOtaStatus(rsp, out snap))
                    return "";
                string st;
                switch (snap.State)
                {
                    case UxFtProto.OtaStStream: st = "灌流/推包中"; break;
                    case UxFtProto.OtaStFinish: st = "已收完（推尾包）"; break;
                    default: st = "空闲"; break;
                }
                return string.Format("（Dongle 现场：会话 {0}，master OTA 步骤 {1}，已推 {2}/{3} 字节）",
                                     st, snap.Start, snap.Adr, snap.Size);
            }
            catch (Exception)
            {
                return "";	/* 诊断尽力而为，不影响主错误 */
            }
        }

        // ---------- 整映像流程：灌流（BEGIN/DATA/END）→ 轮询推包结果 ----------

        public async Task<string> RunImageAsync(TelinkFirmware fw, int resultTimeoutMs, CancellationToken ct,
                                                Action<OtaProgressEventArgs> onProgress, Action<string, int> onLog)
        {
            var sw = System.Diagnostics.Stopwatch.StartNew();
            int total = (int)fw.Size;
            int frameTotal = (total + UxFtProto.OtaDataMax - 1) / UxFtProto.OtaDataMax;

            Log(onLog, string.Format(
                "透传模式：固件 {0} 字节经 USB 流入 Dongle（12B/帧，共 {1} 帧，灌入与 BLE 推包并行），"
                + "Dongle 实时组 20B OTA PDU 推给鼠标（不写 Dongle flash，鼠标零改动）", total, frameTotal), 0);

            // ---- 1) SLOT_BEGIN：dongle 立即启动 master OTA 状态机（连接间隔切 10ms
            //    + OTA 特征定位 + START），OK 响应推迟到推包引擎真正消费 fifo 时才回。
            //    因此这里只等立即类响应；超时≠失败——马上灌流，DATA 的 ack 证明会话
            //    已建立（干等 OK 曾让 dongle 10s rx 看门狗误杀会话，见 20:51 实测）。----
            // BUSY 只剩「上一轮 master OTA 推包未完」一种合法原因（data[0]=3，
            // 此时开新镜像会破坏推包）；「上一趟上传未结束」已被固件的 BEGIN 隐式中止 +
            // 10s rx 空闲看门狗取代。ERR data[0]=2 = 鼠标未连接 dongle（推包通路不通）。
            var beginArgs = new byte[4];
            beginArgs[0] = (byte)(total);
            beginArgs[1] = (byte)(total >> 8);
            beginArgs[2] = (byte)(total >> 16);
            beginArgs[3] = (byte)(total >> 24);

            byte[] rsp = null;
            string err = null;
            int r = 0;
            bool begun = false;
            for (int attempt = 1; attempt <= 3; attempt++)
            {
                r = _link.SendCommand(UxFtProto.CmdOtaSlotBegin, beginArgs, BeginTimeoutMs, out rsp, out err);
                if (r == 1 && rsp[UxFtProto.UlOffsStatus] == UxFtProto.StOk)
                {
                    begun = true;
                    break;
                }
                if (r == 1 && rsp[UxFtProto.UlOffsStatus] == UxFtProto.StBusy)
                {
                    if (rsp[UxFtProto.UlOffsData] == UxFtProto.OtaBeginBusyOtaPush)
                        throw new OtaException("Dongle 正在向鼠标推送固件（上一轮 master OTA 未结束）："
                                               + "请等推完成，或给 Dongle 断电重启后再试");
                    if (attempt < 3)
                    {
                        await Task.Delay(1500, ct);
                        continue;
                    }
                }
                if (r == 1 && rsp[UxFtProto.UlOffsStatus] == UxFtProto.StErr
                    && rsp[UxFtProto.UlOffsData] == UxFtProto.OtaBeginDutNotConnected)
                    throw new OtaException("鼠标未连接 Dongle：请给鼠标上电，等它自动回连 Dongle 后再试"
                                           + "（Dongle 与鼠标的 BLE 链路是推包通路，PC 直连不算）");
                if (r == 1)
                {
                    // 带出 dongle 现场：master OTA 死在哪一步、鼠标链路是否还在
                    string why = QueryStatusText();
                    throw new OtaException("SLOT_BEGIN 被拒（status=" + rsp[UxFtProto.UlOffsStatus]
                                           + "）：master OTA 引擎启动失败。" + why
                                           + "请确认鼠标已上电并回连 Dongle，或给 Dongle 断电重启后再试");
                }
                if (r < 0)
                    throw new OtaException("SLOT_BEGIN 发送失败：" + (err ?? "未知错误"));
                break;  // r == 0：超时——dongle 已受理、确认帧推迟中，不阻塞灌流
            }
            if (begun)
                Log(onLog, "master OTA 已就绪（连接间隔 10ms、OTA 特征已定位），开始灌入固件...", 0);
            else
            {
                // 超时≠失败：带出 dongle 现场（master OTA 步骤）后直接开灌
                string snap = QueryStatusText();
                Log(onLog, "BEGIN 已发送（Dongle 受理中，确认帧推迟到推包引擎就绪后才回）——不等待，"
                           + "立即灌流；DATA 逐帧 ack 即会话已建立的证据"
                           + (snap.Length > 0 ? "。" + snap : ""), 1);
            }

            // ---- 2) SLOT_DATA：12B/帧，逐帧等 ack（帧同步，天然不会堆积）。
            //    ack 只代表「已入 dongle fifo」，推包由 master OTA 并行进行；
            //    每 200 帧顺手查一次 STATUS，把 BLE 推送进度也显示出来，
            //    并借它报告推迟的 BEGIN 确认（引擎进到推包步 step≥6）。----
            int frames = 0;
            bool engineReadyLogged = begun;   // BEGIN 已确认过就不再重复提示
            for (int off = 0; off < total; off += UxFtProto.OtaDataMax)
            {
                ct.ThrowIfCancellationRequested();
                int n = Math.Min(UxFtProto.OtaDataMax, total - off);
                var args = new byte[1 + UxFtProto.OtaDataMax];
                args[UxFtProto.OtaDataOffsLen] = (byte)n;
                Array.Copy(fw.Data, off, args, UxFtProto.OtaDataOffsData, n);

                // BUSY = dongle fifo 满（推包引擎被 BLE 拥塞拖住），稍候重发同一帧
                int tries = 0;
                while (true)
                {
                    r = _link.SendCommand(UxFtProto.CmdOtaSlotData, args, SlotDataTimeoutMs, out rsp, out err);
                    if (r < 0)
                        throw new OtaException("SLOT_DATA 发送失败（off=" + off + "）：" + err);
                    if (r == 0)
                        throw new OtaException("SLOT_DATA 响应超时（off=" + off + "，等了 "
                                               + SlotDataTimeoutMs + "ms）：Dongle 主循环停顿过久，"
                                               + "请把 Dongle UART 日志里的 [ux_ota_slot] 进度贴出来定位");
                    byte st = rsp[UxFtProto.UlOffsStatus];
                    if (st == UxFtProto.StOk)
                        break;
                    if (st == UxFtProto.StBusy)
                    {
                        if (++tries > 800)
                            throw new OtaException("SLOT_DATA 持续繁忙（off=" + off + "）：Dongle 推包异常？");
                        await Task.Delay(5, ct);
                        continue;
                    }
                    throw new OtaException("SLOT_DATA 被拒（off=" + off + "，status=" + st + "）"
                                           + (begun ? "" : "；BEGIN 尚未确认，可能是 BEGIN 命令丢失，请重试")
                                           + "）。会话已被 Dongle 中止（鼠标侧链路断开）——若这是紧接着的重试，"
                                           + "多半是鼠标仍停留在上一次 OTA 会话中（START 被拒 → OTA_FLOW_ERR(6) "
                                           + "→ 鼠标重启并自动关机）：请给鼠标断电重启，或等约 60s 让鼠标超时退出后再试");
                }

                frames++;
                if (frames % 100 == 0 || off + n >= total)
                {
                    double bps = sw.Elapsed.TotalSeconds > 0.01 ? (off + n) / sw.Elapsed.TotalSeconds : 0.0;
                    int pct = (int)((off + n) * 100L / total);
                    int eta = bps > 1 ? (int)((total - off - n) / bps) : -1;
                    Progress(onProgress, new OtaProgressEventArgs
                    {
                        Sent = frames, Total = frameTotal, Percent = pct, BytesPerSec = bps, EtaSeconds = eta
                    });
                }
                if (frames % 200 == 0)
                {
                    // 顺手查一次推送进度（查询失败不影响灌流主循环）
                    int rs = _link.SendCommand(UxFtProto.CmdOtaStatus, null, DongleLink.CommandTimeoutMs,
                                               out rsp, out err);
                    if (rs == 1 && rsp[UxFtProto.UlOffsStatus] == UxFtProto.StOk)
                    {
                        UxFtProto.OtaStatusSnapshot snap;
                        if (UxFtProto.ParseOtaStatus(rsp, out snap))
                        {
                            // 推迟的 BEGIN 确认：引擎进到推包步（step≥6）才提示一次
                            if (!engineReadyLogged && snap.Start >= 6)
                            {
                                engineReadyLogged = true;
                                Log(onLog, string.Format("  master OTA 引擎已就绪（步骤 {0}），正在推包...",
                                    snap.Start), 0);
                            }
                            if (snap.Size > 0 && snap.Adr > 0)
                            {
                                Log(onLog, string.Format("  BLE 推送进度 {0}/{1} 字节（{2}%）",
                                    snap.Adr, snap.Size, (int)(snap.Adr * 100L / snap.Size)), 0);
                            }
                            // 早停检测：已灌入 >=2400B 而 dongle 推出 <512B → BLE 推包通路是死的。
                            // 健康引擎与灌流并行（pushed≈fed）；pushed 停在 0 附近只有一个原因——
                            // 鼠标不该收到 START：多半是它还停留在上一次失败的 OTA 会话里
                            // （START 被拒 → OTA_FLOW_ERR(6) → 鼠标重启并自动关机 → 链路断）。
                            if (off >= 2400 && snap.Adr < 512)
                            {
                                throw new OtaException(string.Format(
                                    "BLE 推包停滞：已灌入 {0} 字节，Dongle 仅推出 {1} 字节（step {2}）。"
                                    + "鼠标可能仍处于上一次 OTA 会话中（START 被拒 → OTA_FLOW_ERR(6)）："
                                    + "请给鼠标断电重启，或等待约 60s 让鼠标超时退出后再试",
                                    off, snap.Adr, snap.Start));
                            }
                        }
                    }
                }
            }

            // ---- 3) SLOT_END：PC 侧发完，dongle 校验字节数后推尾包 + OTA END 帧。
            //    透传模式下即时响应（data = 已收字节数）。----
            r = _link.SendCommand(UxFtProto.CmdOtaSlotEnd, null, EndTimeoutMs, out rsp, out err);
            if (r != 1)
                throw new OtaException("SLOT_END 无响应：" + (err ?? "超时"));
            if (rsp[UxFtProto.UlOffsStatus] != UxFtProto.StOk)
                throw new OtaException("SLOT_END 被拒（status=" + rsp[UxFtProto.UlOffsStatus] + "）");
            uint received = (uint)(rsp[UxFtProto.UlOffsData]
                                   | (rsp[UxFtProto.UlOffsData + 1] << 8)
                                   | (rsp[UxFtProto.UlOffsData + 2] << 16)
                                   | (rsp[UxFtProto.UlOffsData + 3] << 24));
            if (received != total)
                throw new OtaException(string.Format(
                    "Dongle 接收字节数不符：{0} != {1}（USB 传输丢帧？）", received, total));
            Log(onLog, string.Format("固件已全部灌入（{0} 字节，{1:F1}s），Dongle 正在推尾包...",
                received, sw.Elapsed.TotalSeconds), 3);

            // ---- 4) 轮询 STATUS 快照，直到 master OTA 出结果 ----
            int deadline = Environment.TickCount + resultTimeoutMs;
            while (true)
            {
                ct.ThrowIfCancellationRequested();
                await Task.Delay(500, ct);

                r = _link.SendCommand(UxFtProto.CmdOtaStatus, null, DongleLink.CommandTimeoutMs, out rsp, out err);
                if (r != 1)
                    throw new OtaException("STATUS 查询失败：" + (err ?? "超时"));

                UxFtProto.OtaStatusSnapshot snap;
                if (!UxFtProto.ParseOtaStatus(rsp, out snap))
                    throw new OtaException("STATUS 响应解析失败");

                if (snap.Size > 0)
                {
                    uint pushed = snap.Adr > snap.Size ? snap.Size : snap.Adr;
                    int pct = (int)(pushed * 100L / snap.Size);
                    double bps = sw.Elapsed.TotalSeconds > 0.01 ? pushed / sw.Elapsed.TotalSeconds : 0.0;
                    Progress(onProgress, new OtaProgressEventArgs
                    {
                        Sent = (int)((pushed + 15) / 16),
                        Total = (int)((snap.Size + 15) / 16),
                        Percent = pct,
                        BytesPerSec = bps,
                        EtaSeconds = bps > 1 ? (int)((snap.Size - pushed) / bps) : -1
                    });
                }

                if (snap.Result == UxFtProto.OtaResOk)
                    return "升级成功：Dongle 已将全部固件推送给鼠标并通过校验"
                           + "（鼠标可能已自动关机，请手动上电）";
                if (snap.Result == UxFtProto.OtaResFail)
                    throw new OtaException("master OTA 失败（Dongle 结果码 2：链路中断 / 超时 / 设备拒绝）。"
                                           + "若鼠标其实已重启并运行新固件，可忽略此提示；"
                                           + "若推送字节数很小，请给鼠标断电重启或等约 60s 后再试");
                if (Environment.TickCount >= deadline)
                    throw new OtaException("等待 master OTA 结果超时（" + resultTimeoutMs
                                           + "ms）：Dongle 仍未报结果，请观察鼠标是否已升级");
            }
        }

        private static void Log(Action<string, int> onLog, string msg, int level)
        {
            var h = onLog;
            if (h != null)
                h(msg, level);
        }

        private static void Progress(Action<OtaProgressEventArgs> onProgress, OtaProgressEventArgs a)
        {
            var h = onProgress;
            if (h != null)
                h(a);
        }
    }

    /// <summary>
    /// BLE 直连 OTA 通道：PC 用 WinRT 直连鼠标，写其 OTA 特征
    /// （service = TELINK_OTA_UUID_SERVICE 12 19 0D..，write = TELINK_SPP_DATA_OTA 12 2B 0D..，
    ///  属性 READ|WRITE_WITHOUT_RSP，见 vendor/8258_dual_mouse/AAA_app_att.c:452-465）。
    /// 每 PDU 16B 数据（Telink 标准），write without response，无应用层回包；
    /// 设备结果经 OTA 特征的 notify 回来（若开了 CCC）。
    /// </summary>
    internal sealed class OtaChannelBle : IOtaChannel
    {
        private readonly BleDirectLink _link;

        public OtaChannelBle(BleDirectLink link)
        {
            _link = link;
        }

        public string DisplayName
        {
            get { return "BLE 直连鼠标（写 OTA 特征）"; }
        }

        public int MaxFrameBytes
        {
            get { return 20; }                       // Telink OTA PDU 固定 20B
        }

        public async Task<string> OpenAsync()
        {
            if (!_link.IsLinkUp)
                return "GATT 链路未连接，请先在「蓝牙连接」页连上鼠标";
            if (!_link.HasOtaChar)
                return "未发现 OTA 写特征（UUID " + BleUxProto.OtaWriteUuid +
                       "）：该固件未开 BLE_OTA_SERVER_ENABLE 或 OTA 服务未编入 ATT 表。" + _link.OtaDiagNote;
            await Task.Yield();
            return null;
        }

        public Task<string> SendPduAsync(byte[] pdu)
        {
            return _link.WriteOtaAsync(pdu);
        }

        public void Close()
        {
            // 连接由「蓝牙连接」页统一管理，这里不主动断开。
        }
    }
}
