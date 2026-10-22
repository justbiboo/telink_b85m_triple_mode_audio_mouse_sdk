using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace BibooUxTool
{
    /// <summary>
    /// 鼠标链路诊断页：定位"画线断线/卡顿"发生在哪一环。
    ///   ① 链路计数器（host link MSTAT 0x03）：BLE 收帧 / 进 ring / ring 丢 /
    ///      USB 发出 / EP 忙排队 / fifo 丢 —— 哪个计数器随画线上涨，丢帧就在哪一环；
    ///   ② 0xE1 间隔监视（进入产测模式后）：报告到达间隔统计。间隔平稳但系统光标
    ///      仍卡顿 = 问题在 dongle USB 上报之后（PC/系统侧）；间隔同样尖峰 = BLE 上游。
    /// 本页自持 DongleLink 实例并模态打开，与产线测试/收发调试天然互斥（HID 通道独占）。
    /// 读通道约定同产线测试页：50ms UI 定时器 Pump 排空上行帧，单消费者模型。
    /// </summary>
    public sealed class MouseDiagForm : Form
    {
        private const int CardMargin = 12;
        private const int CardGap = 8;
        private const int GapThresholdMs = 8;   // 相邻 0xE1 间隔超过该值记一次卡顿间隙
        private const double GapBigThresholdMs = 20;  // 真尖峰阈值（10ms 连接间隔的跨事件边界约 10ms，>20ms 才是异常）

        private readonly DongleLink _link = new DongleLink();
        private Timer _timer;
        private int _tickCount;

        // 连接卡片
        private RoundedPanel _cardConn;
        private GradientButton _btnConnect;
        private Label _lblConnStatus;

        // 计数器卡片
        private RoundedPanel _cardCnt;
        private GradientButton _btnCntRefresh;
        private GradientButton _btnCntAuto;
        private GradientButton _btnCntReset;
        private readonly Label[] _lblCntTotal = new Label[6];
        private readonly Label[] _lblCntRate = new Label[6];
        private bool _autoRefresh;
        private bool _cntInit;
        private readonly uint[] _prevCnt = new uint[6];
        private int _prevCntTick;

        // 0xE1 间隔监视卡片
        private RoundedPanel _cardGap;
        private GradientButton _btnFtStart;
        private GradientButton _btnFtStop;
        private GradientButton _btnGapReset;
        private Label _lblGapStats;
        private bool _ftActive;
        private long _e1Count;
        private double _e1SumMs;
        private double _e1MaxMs;
        private double _e1LastGapMs;
        private long _e1GapCnt;
        private long _e1LastStamp;
        private bool _e1HasLast;
        private long _e1BigGapCnt;                      // >20ms 真尖峰次数（排除 10ms 连接间隔的正常边界）
        private readonly List<string> _e1BigGaps = new List<string>();  // 最近 12 个尖峰的时间+数值

        // 日志卡片
        private RoundedPanel _cardLog;
        private RichTextBox _txtLog;
        private GradientButton _btnSave;

        public MouseDiagForm()
        {
            BuildUi();
            UpdateConnUi(false);

            _link.FrameEvent += OnFrameEvent;

            _timer = new Timer();
            _timer.Interval = 50;
            _timer.Tick += OnTimerTick;

            LogLine("诊断页就绪（构建标记 1019-2，含全局异常捕获）。【连接】后画线，观察哪个计数器随动作上涨。", UiTheme.TextSub);
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            _timer.Stop();
            // 离开前恢复 dongle 的正常模式（产测模式只是镜像旁路，不退出也不影响鼠标）
            if (_ftActive && _link.IsConnected)
            {
                byte[] rsp;
                string err;
                _link.SendCommand(UxFtProto.CmdStop, null, 500, out rsp, out err);
            }
            _link.Dispose();
            base.OnFormClosed(e);
        }

        // ---------- UI 构建 ----------

        private void BuildUi()
        {
            Text = "Biboo OS - 鼠标链路诊断";
            BackColor = UiTheme.FormBg;
            Font = UiTheme.FontBody;
            ClientSize = new Size(920, 680);
            MinimumSize = new Size(900, 660);
            StartPosition = FormStartPosition.CenterParent;

            try
            {
                Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            }
            catch
            {
                // 提取失败不影响主流程
            }

            // 连接卡片
            _cardConn = new RoundedPanel();
            _cardConn.SetBounds(CardMargin, CardMargin, 896, 64);
            _cardConn.Anchor = AnchorStyles.Top;    // 宽度由 OnResize 统一设置
            _cardConn.Controls.Add(MakeCardTitle("Host Link"));

            _btnConnect = MakeButton("连接", 16, 30, 92, 26, true, OnConnect);

            _lblConnStatus = new Label();
            _lblConnStatus.SetBounds(120, 30, 760, 26);
            _lblConnStatus.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _lblConnStatus.TextAlign = ContentAlignment.MiddleLeft;
            _lblConnStatus.AutoSize = false;
            _lblConnStatus.AutoEllipsis = true;

            _cardConn.Controls.AddRange(new Control[] { _btnConnect, _lblConnStatus });

            // 计数器卡片
            int yCnt = CardMargin + 64 + CardGap;
            _cardCnt = new RoundedPanel();
            _cardCnt.SetBounds(CardMargin, yCnt, 896, 252);
            _cardCnt.Anchor = AnchorStyles.Top;
            _cardCnt.Controls.Add(MakeCardTitle("链路计数器（Dongle 侧，MSTAT 0x03）"));

            _btnCntRefresh = MakeButton("刷新", 16, 30, 92, 26, true, OnCntRefresh);
            _btnCntAuto = MakeButton("自动刷新", 116, 30, 92, 26, false, OnCntAuto);
            _btnCntReset = MakeButton("清零", 216, 30, 92, 26, false, OnCntReset);

            var hdrName = MakeSubLabel("指标", 16, 62, 210, 18);
            var hdrTotal = MakeSubLabel("累计", 240, 62, 130, 18);
            var hdrRate = MakeSubLabel("每秒增量", 370, 62, 130, 18);
            _cardCnt.Controls.AddRange(new Control[] { hdrName, hdrTotal, hdrRate });

            for (int i = 0; i < 6; i++)
            {
                var name = MakeSubLabel(UxFtProto.MstatNames[i], 16, 82 + i * 22, 210, 20);
                name.ForeColor = UiTheme.TextMain;

                var total = new Label();
                total.SetBounds(240, 82 + i * 22, 130, 20);
                total.Font = new Font("Consolas", 9.5f);
                total.Text = "--";
                total.TextAlign = ContentAlignment.MiddleLeft;
                total.AutoSize = false;

                var rate = new Label();
                rate.SetBounds(370, 82 + i * 22, 130, 20);
                rate.Font = new Font("Consolas", 9.5f);
                rate.Text = "--";
                rate.ForeColor = UiTheme.TextSub;
                rate.TextAlign = ContentAlignment.MiddleLeft;
                rate.AutoSize = false;

                _lblCntTotal[i] = total;
                _lblCntRate[i] = rate;
                _cardCnt.Controls.AddRange(new Control[] { name, total, rate });
            }

            var hintCnt = new Label();
            hintCnt.SetBounds(16, 218, 860, 26);
            hintCnt.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            hintCnt.AutoSize = false;
            hintCnt.Font = new Font(UiTheme.FontBody.FontFamily, 8.5f);
            hintCnt.ForeColor = UiTheme.TextSub;
            hintCnt.Text = "判读：ring丢 / fifo丢 随画线上涨 = Dongle 缓冲溢出丢帧（红色）；"
                + "BLE收帧 ≈ USB发出 且两处丢帧全 0 = Dongle 无丢帧，问题在 BLE 上游（鼠标端/RF）或 PC 侧。";
            _cardCnt.Controls.Add(hintCnt);
            _cardCnt.Controls.AddRange(new Control[] { _btnCntRefresh, _btnCntAuto, _btnCntReset });

            // 0xE1 间隔监视卡片
            int yGap = yCnt + 252 + CardGap;
            _cardGap = new RoundedPanel();
            _cardGap.SetBounds(CardMargin, yGap, 896, 132);
            _cardGap.Anchor = AnchorStyles.Top;
            _cardGap.Controls.Add(MakeCardTitle("0xE1 报告间隔监视（需先进入产测模式）"));

            _btnFtStart = MakeButton("进入产测", 16, 30, 92, 26, true, OnFtStart);
            _btnFtStop = MakeButton("退出产测", 116, 30, 92, 26, false, OnFtStop);
            _btnGapReset = MakeButton("清零统计", 216, 30, 92, 26, false, OnGapReset);

            _lblGapStats = new Label();
            _lblGapStats.SetBounds(320, 24, 556, 66);
            _lblGapStats.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _lblGapStats.AutoSize = false;
            _lblGapStats.Font = new Font("Consolas", 9.5f);
            _lblGapStats.ForeColor = UiTheme.TextMain;
            _lblGapStats.TextAlign = ContentAlignment.MiddleLeft;
            UpdateGapStatsLabel();

            var hintGap = new Label();
            hintGap.SetBounds(16, 94, 860, 28);
            hintGap.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            hintGap.AutoSize = false;
            hintGap.Font = new Font(UiTheme.FontBody.FontFamily, 8.5f);
            hintGap.ForeColor = UiTheme.TextSub;
            hintGap.Text = "间隔平稳但系统光标仍卡顿 → 问题在 Dongle USB 上报之后（PC/系统侧）；"
                + "间隔同样出现尖峰 → 问题在 BLE 上游。产测模式只增加上行镜像，不改变 USB 转发路径。";
            _cardGap.Controls.AddRange(new Control[]
            {
                _btnFtStart, _btnFtStop, _btnGapReset, _lblGapStats, hintGap
            });

            // 日志卡片
            int yLog = yGap + 132 + CardGap;
            _cardLog = new RoundedPanel();
            _cardLog.SetBounds(CardMargin, yLog, 896, ClientSize.Height - yLog - CardMargin);
            _cardLog.Anchor = AnchorStyles.Top | AnchorStyles.Bottom;
            _cardLog.Controls.Add(MakeCardTitle("日志"));

            _txtLog = new RichTextBox();
            _txtLog.SetBounds(16, 30, 860, _cardLog.Height - 40 - 30);
            _txtLog.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            _txtLog.BackColor = UiTheme.InputBg;
            _txtLog.ForeColor = UiTheme.TextMain;
            _txtLog.Font = new Font("Consolas", 9f);
            _txtLog.ReadOnly = true;
            _txtLog.BorderStyle = BorderStyle.None;
            _cardLog.Controls.Add(_txtLog);

            // 保存报告：测完一键导出计数器快照 + 间隔统计 + crash.log + 页面日志
            _btnSave = MakeButton("保存报告", _cardLog.Width - 136, _cardLog.Height - 32, 120, 26, false, OnSaveReport);
            _btnSave.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
            _cardLog.Controls.Add(_btnSave);

            Controls.AddRange(new Control[] { _cardConn, _cardCnt, _cardGap, _cardLog });
        }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            if (_cardConn == null)
                return;   // BuildUi 中设置 ClientSize 会同步触发 OnResize，此时卡片尚未创建
            int w = ClientSize.Width - CardMargin * 2;
            if (w < 320)
                w = 320;
            _cardConn.Width = w;
            _cardCnt.Width = w;
            _cardGap.Width = w;
            _cardLog.Width = w;
        }

        private static Label MakeCardTitle(string text)
        {
            var lbl = new Label();
            lbl.Text = text;
            lbl.SetBounds(14, 8, 700, 20);
            lbl.Font = UiTheme.FontTitle;
            lbl.ForeColor = UiTheme.TextMain;
            lbl.TextAlign = ContentAlignment.MiddleLeft;
            lbl.AutoSize = false;
            lbl.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            return lbl;
        }

        private static Label MakeSubLabel(string text, int x, int y, int w, int h)
        {
            var lbl = new Label();
            lbl.Text = text;
            lbl.SetBounds(x, y, w, h);
            lbl.ForeColor = UiTheme.TextSub;
            lbl.TextAlign = ContentAlignment.MiddleLeft;
            lbl.AutoSize = false;
            return lbl;
        }

        private static GradientButton MakeButton(string text, int x, int y, int w, int h, bool primary, EventHandler onClick)
        {
            var b = new GradientButton();
            b.SetBounds(x, y, w, h);
            b.Text = text;
            b.IsPrimary = primary;
            b.Click += onClick;
            return b;
        }

        // ---------- 连接与 UI 状态 ----------

        private void UpdateConnUi(bool connected)
        {
            _btnConnect.Text = connected ? "断开" : "连接";
            _lblConnStatus.Text = connected
                ? "● 已连接  " + _link.GetOpenedPath()
                : "○ 未连接";
            _lblConnStatus.ForeColor = connected ? UiTheme.Success : UiTheme.Danger;

            _btnCntRefresh.Enabled = connected;
            _btnCntAuto.Enabled = connected;
            _btnCntReset.Enabled = connected;
            _btnFtStart.Enabled = connected && !_ftActive;
            _btnFtStop.Enabled = connected && _ftActive;
            _btnGapReset.Enabled = connected;

            if (!connected)
            {
                _ftActive = false;
                _autoRefresh = false;
                _btnCntAuto.Text = "自动刷新";
                _cntInit = false;
                for (int i = 0; i < 6; i++)
                {
                    _lblCntTotal[i].Text = "--";
                    _lblCntTotal[i].ForeColor = UiTheme.TextMain;
                    _lblCntRate[i].Text = "--";
                    _lblCntRate[i].ForeColor = UiTheme.TextSub;
                }
                ResetGapStats();
            }
        }

        private void OnConnect(object sender, EventArgs e)
        {
            if (_link.IsConnected)
            {
                _timer.Stop();
                _link.Disconnect();
                UpdateConnUi(false);
                LogLine("已断开。", UiTheme.TextSub);
                return;
            }

            LogLine("正在枚举并打开独立 Host Link HID Interface...", UiTheme.TextSub);
            try
            {
                string err = _link.Connect();
                if (err != null)
                {
                    LogLine("连接失败: " + err, UiTheme.Danger);
                    UpdateConnUi(false);
                    return;
                }

                UpdateConnUi(true);
                _timer.Start();
                LogLine("已连接: " + _link.GetOpenedPath(), UiTheme.Success);
                RefreshCounters();
            }
            catch (Exception ex)
            {
                LogLine("连接异常（" + ex.GetType().Name + "）：" + ex.Message, UiTheme.Danger);
            }
        }

        // ---------- 计数器（MSTAT 0x03） ----------

        private uint _dgGapCnt, _dgGapMaxMs, _dgGapOver20, _dgGapSumMs, _dgGapBreak;
        private bool _dgGapInit;

        private bool ReadCounters(out uint[] vals, out string err)
        {
            vals = new uint[12];   // 组0/1 六个流水线计数 + 组2/3 五个 dongle 实测间隔统计（末位保留）
            err = null;
            for (int g = 0; g < 4; g++)
            {
                byte[] rsp;
                int r = _link.SendCommand(UxFtProto.CmdMstat, new byte[] { (byte)g },
                    DongleLink.CommandTimeoutMs, out rsp, out err);
                if (r <= 0)
                    return false;

                byte status;
                uint c0, c1, c2;
                if (!UxFtProto.ParseMstat(rsp, out status, out c0, out c1, out c2) || status != 0)
                {
                    err = "MSTAT 响应异常（status=" + status + "）";
                    return false;
                }
                vals[g * 3] = c0;
                vals[g * 3 + 1] = c1;
                vals[g * 3 + 2] = c2;
            }
            return true;
        }

        private void RefreshCounters()
        {
            uint[] vals;
            string err;
            if (!ReadCounters(out vals, out err))
            {
                LogLine("计数器读取失败：" + err, UiTheme.Danger);
                return;
            }

            int now = Environment.TickCount;
            for (int i = 0; i < 6; i++)
            {
                bool isDrop = (i == 2 || i == 5);
                _lblCntTotal[i].Text = vals[i].ToString();
                _lblCntTotal[i].ForeColor = (isDrop && vals[i] > 0) ? UiTheme.Danger : UiTheme.TextMain;

                if (_cntInit)
                {
                    double dt = now - _prevCntTick;
                    long diff = (long)vals[i] - _prevCnt[i];
                    double rate = dt > 0 ? diff * 1000.0 / dt : 0;
                    _lblCntRate[i].Text = (rate >= 0 ? "+" : "") + rate.ToString("0.#") + "/s";
                    _lblCntRate[i].ForeColor = (isDrop && rate > 0) ? UiTheme.Danger : UiTheme.TextSub;
                }
                else
                {
                    _lblCntRate[i].Text = "--";
                    _lblCntRate[i].ForeColor = UiTheme.TextSub;
                }
                _prevCnt[i] = vals[i];
            }
            _prevCntTick = now;
            _cntInit = true;

            /*组2/3：dongle 固件实测的到达间隔（不受 PC 50ms 轮询批量影响）*/
            _dgGapCnt = vals[6];
            _dgGapMaxMs = vals[7];
            _dgGapOver20 = vals[8];
            _dgGapSumMs = vals[9];
            _dgGapBreak = vals[10];
            _dgGapInit = true;
            UpdateGapStatsLabel();
        }

        private void OnCntRefresh(object sender, EventArgs e)
        {
            RefreshCounters();
        }

        private void OnCntAuto(object sender, EventArgs e)
        {
            _autoRefresh = !_autoRefresh;
            _btnCntAuto.Text = _autoRefresh ? "停止刷新" : "自动刷新";
            LogLine(_autoRefresh ? "自动刷新已开启（500ms）。" : "自动刷新已关闭。", UiTheme.TextSub);
        }

        private void OnCntReset(object sender, EventArgs e)
        {
            byte[] rsp;
            string err;
            int r = _link.SendCommand(UxFtProto.CmdMstat, new byte[] { UxFtProto.MstatReset },
                DongleLink.CommandTimeoutMs, out rsp, out err);
            if (r <= 0)
            {
                LogLine("计数器清零失败：" + err, UiTheme.Danger);
                return;
            }
            _cntInit = false;
            RefreshCounters();
            LogLine("Dongle 计数器已清零。", UiTheme.Success);
        }

        // ---------- 0xE1 间隔监视 ----------

        private void OnFrameEvent(byte[] frame)
        {
            byte btn;
            short x, y;
            sbyte wheel;
            if (!UxFtProto.ParseMouse(frame, out btn, out x, out y, out wheel))
                return;

            long now = Stopwatch.GetTimestamp();
            if (_e1HasLast)
            {
                double dt = (now - _e1LastStamp) * 1000.0 / Stopwatch.Frequency;
                _e1Count++;
                _e1SumMs += dt;
                if (dt > _e1MaxMs)
                    _e1MaxMs = dt;
                if (dt > GapThresholdMs)
                {
                    _e1GapCnt++;
                    _e1LastGapMs = dt;
                }
                if (dt > GapBigThresholdMs)
                {
                    _e1BigGapCnt++;
                    _e1BigGaps.Insert(0, string.Format("{0:HH:mm:ss.fff} {1:0.0}ms", DateTime.Now, dt));
                    if (_e1BigGaps.Count > 12)
                        _e1BigGaps.RemoveAt(_e1BigGaps.Count - 1);
                }
            }
            _e1LastStamp = now;
            _e1HasLast = true;
        }

        private void ResetGapStats()
        {
            _e1Count = 0;
            _e1SumMs = 0;
            _e1MaxMs = 0;
            _e1LastGapMs = 0;
            _e1GapCnt = 0;
            _e1HasLast = false;
            _e1BigGapCnt = 0;
            _e1BigGaps.Clear();
            UpdateGapStatsLabel();
        }

        private void UpdateGapStatsLabel()
        {
            double avg = _e1Count > 0 ? _e1SumMs / _e1Count : 0;
            string recent = _e1BigGaps.Count > 0
                ? string.Join("; ", _e1BigGaps.ToArray())
                : "（无）";
            string dgLine = _dgGapInit
                ? string.Format("[Dongle 实测] 间隔 {0}  平均 {1:0.0} ms  最大 {2} ms  >20ms {3} 次  断流 {4} 次",
                    _dgGapCnt,
                    _dgGapCnt > 0 ? (double)_dgGapSumMs / _dgGapCnt : 0,
                    _dgGapMaxMs, _dgGapOver20, _dgGapBreak)
                : "[Dongle 实测] 尚未读取（需新固件 MSTAT 组2/3）";
            _lblGapStats.Text = string.Format(
                "报告数 {0}   平均 {1:0.0} ms   最大 {2:0.0} ms\r\n> {3} ms 次数 {4}（最近一次 {5:0.0} ms）\r\n> {6:0} ms 尖峰 {7} 次：{8}\r\n{9}",
                _e1Count, avg, _e1MaxMs, GapThresholdMs, _e1GapCnt, _e1LastGapMs,
                GapBigThresholdMs, _e1BigGapCnt, recent, dgLine);
        }

        private void OnFtStart(object sender, EventArgs e)
        {
            byte[] rsp;
            string err;
            int r = _link.SendCommand(UxFtProto.CmdStart, null, DongleLink.CommandTimeoutMs, out rsp, out err);
            if (r <= 0)
            {
                LogLine("进入产测失败：" + err, UiTheme.Danger);
                return;
            }
            if (rsp[UxFtProto.UlOffsStatus] != UxFtProto.StOk)
            {
                LogLine("进入产测被拒（status=" + rsp[UxFtProto.UlOffsStatus] + "）", UiTheme.Danger);
                return;
            }
            _ftActive = true;
            ResetGapStats();
            UpdateConnUi(true);
            LogLine("已进入产测模式：0xE1 报告镜像流开始上行（USB 鼠标转发不受影响）。", UiTheme.Success);
        }

        private void OnFtStop(object sender, EventArgs e)
        {
            byte[] rsp;
            string err;
            int r = _link.SendCommand(UxFtProto.CmdStop, null, DongleLink.CommandTimeoutMs, out rsp, out err);
            if (r <= 0)
            {
                LogLine("退出产测失败：" + err, UiTheme.Danger);
                return;
            }
            _ftActive = false;
            UpdateConnUi(true);
            LogLine("已退出产测模式。", UiTheme.Success);
        }

        private void OnGapReset(object sender, EventArgs e)
        {
            ResetGapStats();
        }

        /// <summary>
        /// 测完点【保存报告】：汇总计数器快照、0xE1 间隔统计、crash.log（若存在）
        /// 与页面日志，写入 exe 同目录的 MouseDiag_*.log，并把完整路径贴到
        /// 页面日志和弹窗里，方便整份文件回传分析。
        /// </summary>
        private void OnSaveReport(object sender, EventArgs e)
        {
            try
            {
                if (_link.IsConnected)
                {
                    uint[] vals;
                    string err;
                    ReadCounters(out vals, out err);   // 保存前拉一次最新计数
                }

                var sb = new StringBuilder();
                sb.AppendLine("Biboo OS 鼠标链路诊断报告");
                sb.AppendLine("时间    : " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff"));
                sb.AppendLine("构建标记: 1019-2（含全局异常捕获）");
                sb.AppendLine("已连接  : " + (_link.IsConnected ? "是（" + _link.GetOpenedPath() + "）" : "否"));
                sb.AppendLine();
                sb.AppendLine("== 计数器快照（MSTAT 0x03）==");
                for (int i = 0; i < 6; i++)
                {
                    sb.AppendLine(string.Format("{0,-16} 累计 {1,-12} 速率 {2}",
                        UxFtProto.MstatNames[i], _lblCntTotal[i].Text, _lblCntRate[i].Text));
                }
                sb.AppendLine();
                sb.AppendLine("== 0xE1 间隔统计 ==");
                sb.AppendLine(_lblGapStats.Text);
                sb.AppendLine();

                string crashPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "crash.log");
                if (File.Exists(crashPath))
                {
                    sb.AppendLine("== crash.log（全局未处理异常）==");
                    sb.AppendLine(File.ReadAllText(crashPath));
                    sb.AppendLine();
                }

                sb.AppendLine("== 页面日志 ==");
                sb.AppendLine(_txtLog.Text);

                string path = Path.Combine(AppDomain.CurrentDomain.BaseDirectory,
                    "MouseDiag_" + DateTime.Now.ToString("yyyyMMdd_HHmmss") + ".log");
                File.WriteAllText(path, sb.ToString());

                LogLine("诊断报告已保存：" + path, UiTheme.Success);
                MessageBox.Show(this, "诊断报告已保存，请把这个文件发回：\r\n\r\n" + path,
                    "已保存", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Exception ex)
            {
                LogLine("保存报告失败：" + ex.Message, UiTheme.Danger);
                MessageBox.Show(this, "保存失败：" + ex.Message, "错误",
                    MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        // ---------- 定时器与日志 ----------

        /// <summary>50ms 排空上行帧；每 10 拍（500ms）做一次慢速刷新。</summary>
        private void OnTimerTick(object sender, EventArgs e)
        {
            for (int i = 0; i < 64; i++)
            {
                int r = _link.Pump(0);
                if (r < 0)
                {
                    LogLine("上行读取失败，连接已断开。", UiTheme.Danger);
                    _timer.Stop();
                    UpdateConnUi(false);
                    return;
                }
                if (r == 0)
                    break;
            }

            _tickCount++;
            if (_tickCount % 10 != 0)
                return;

            if (_autoRefresh && _link.IsConnected)
                RefreshCounters();
            UpdateGapStatsLabel();
        }

        private void LogLine(string msg, Color color)
        {
            _txtLog.SelectionStart = _txtLog.TextLength;
            _txtLog.SelectionColor = color;
            _txtLog.AppendText("[" + DateTime.Now.ToString("HH:mm:ss.fff") + "] " + msg + "\n");
            _txtLog.SelectionColor = _txtLog.ForeColor;
            _txtLog.ScrollToCaret();
        }
    }
}
