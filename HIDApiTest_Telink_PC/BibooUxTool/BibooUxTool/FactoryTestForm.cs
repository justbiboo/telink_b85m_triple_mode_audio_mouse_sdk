using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace BibooUxTool
{
    /// <summary>
    /// dongle 产测页（Task 12）：连接管理 / 产测模式开关 / 鼠标报告 LED /
    /// 鉴权+AES 往返 / 记录与导出。
    /// 读通道约定：本页自持 DongleLink 实例，以 50ms UI 定时器 Pump 排空上行帧；
    /// 按钮操作在 UI 线程内阻塞等待（SendCommand / SendSppFrameWait 内部也走 Pump），
    /// WinForms 定时器与按钮回调不会并发 —— 单消费者模型。
    /// 与收发调试页（DongleForm）共用同一 HID 通道，须独占：
    /// 由首页 HomeForm 逐个模态进入即可保证两者不会同时打开。
    /// </summary>
    public sealed class FactoryTestForm : Form
    {
        private const int SppTimeoutMs = 3000;  // SPP 命令响应等待（含分片桥接往返）

        private static readonly Color LedOffColor = Color.FromArgb(0xE3, 0xE9, 0xF5);

        private readonly DongleLink _link = new DongleLink();
        private Timer _timer;

        // 设备卡片
        private GradientButton _btnConnect;
        private GradientButton _btnFtStart;
        private GradientButton _btnFtStop;
        private GradientButton _btnRefresh;
        private GradientButton _btnDutDisc;
        private Label _lblDevStatus;
        private Label _lblFtStatus;
        private readonly ToolTip _tips = new ToolTip();

        // 鼠标报告卡片
        private Label _lblMouse;
        private Label _lblMouseCnt;
        private Panel _ledL;
        private Panel _ledM;
        private Panel _ledR;
        private MouseTrackView _viewXY;      // XY 位移轨迹视图（自绘）
        private WheelBarView _viewWheel;     // 滚轮瞬时量竖条（自绘）
        private Label _lblWheelVal;          // 滚轮累计数值（大字）
        private int _wheelTotal;             // 滚轮累计（代数和，清记录时复位）
        private bool _clrComboLatch;         // L+R 组合键清空锁存（防按住期间每帧重复触发）
        private long _mouseCount;

        // 鉴权卡片
        private GradientButton _btnAuthRun;
        private Label _lblAuthResult;
        private Label _lblAuthDetail;

        // 日志与记录卡片
        private RichTextBox _txtLog;
        private Label _lblSummary;
        private GradientButton _btnClear;
        private GradientButton _btnExport;

        // 服务器对接（demo：上报产测记录到中央 Windows 服务器）
        private TextBox _txtServerUrl;
        private TextBox _txtStation;
        private GradientButton _btnUpload;

        // 产测状态镜像（由 STATUS 查询 / 0xE2 事件维护）
        private bool _ftActive;
        private bool _dutUp;
        private bool _sppReady;
        private int _authTick = -1;
        private string _dutName = string.Empty;    // DUT 蓝牙名（固件从连接时 ADV 缓存上报）
        private string _dutMac = string.Empty;     // DUT 对端 MAC（MSB 前显示格式 AA:BB:...）

        // 记录与统计（CSV 导出的数据源）
        private int _passCount;
        private int _failCount;
        private readonly List<string[]> _results = new List<string[]>();

        public FactoryTestForm()
        {
            BuildUi();
            UpdateConnUi(false);

            _link.FrameEvent += OnFrameEvent;
            _link.SppFrameEvent += OnSppFrameEvent;

            _timer = new Timer();
            _timer.Interval = 50;
            _timer.Tick += OnTimerTick;

            LogLine("产测页就绪：点击【连接】打开 dongle，再【进入产测】开始测试。", UiTheme.TextSub);
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            _timer.Stop();
            _link.Dispose();
            base.OnFormClosed(e);
        }

        // ---------- UI 构建 ----------

        private void BuildUi()
        {
            Text = "Biboo OS - 产线测试";
            BackColor = UiTheme.FormBg;
            Font = UiTheme.FontBody;
            ClientSize = new Size(920, 688);
            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterParent;

            try
            {
                Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            }
            catch
            {
                // 提取失败不影响主流程
            }

            // 设备卡片
            var cardDevice = new RoundedPanel();
            cardDevice.SetBounds(12, 12, 896, 96);
            cardDevice.Controls.Add(MakeCardTitle("Host Link"));

            _btnConnect = MakeButton("连接", 16, 32, 92, 30, true, OnConnect);
            _btnFtStart = MakeButton("进入产测", 118, 32, 92, 30, true, OnFtStart);
            _btnFtStop = MakeButton("退出产测", 220, 32, 92, 30, false, OnFtStop);
            _btnRefresh = MakeButton("刷新状态", 322, 32, 92, 30, false, OnRefresh);
            _btnDutDisc = MakeButton("断开 DUT", 424, 32, 92, 30, false, OnDutDisc);

            _lblDevStatus = new Label();
            _lblDevStatus.SetBounds(530, 36, 350, 22);
            _lblDevStatus.TextAlign = ContentAlignment.MiddleLeft;
            _lblDevStatus.AutoEllipsis = true;

            _lblFtStatus = new Label();
            _lblFtStatus.SetBounds(16, 66, 864, 22);
            _lblFtStatus.ForeColor = UiTheme.TextSub;
            _lblFtStatus.Font = new Font("Consolas", 9f);
            _lblFtStatus.TextAlign = ContentAlignment.MiddleLeft;

            // 状态行字段说明（悬停提示；DUT 名字/MAC 来自 0x14 DUT_INFO / 0x15 DUT_MAC 查询）
            _tips.SetToolTip(_lblFtStatus,
                "FT: 产测模式开关（进入后 BLE 报告镜像为 0xE1 事件，USB 转发不受影响）\r\n" +
                "DUT: 被测鼠标连接状态；其后为连接时广播的蓝牙名，[ ] 内为对端蓝牙地址（MAC）\r\n" +
                "SPP: 数据通道就绪状态；auth tick: 授权剩余时间(ms)，10000=待授权，0=已过期");

            cardDevice.Controls.AddRange(new Control[]
            {
                _btnConnect, _btnFtStart, _btnFtStop, _btnRefresh, _btnDutDisc,
                _lblDevStatus, _lblFtStatus
            });

            // 鼠标报告卡片
            var cardMouse = new RoundedPanel();
            cardMouse.SetBounds(12, 116, 896, 196);
            cardMouse.Controls.Add(MakeCardTitle("鼠标报告"));

            _lblMouse = new Label();
            _lblMouse.SetBounds(16, 34, 500, 22);
            _lblMouse.Font = new Font("Consolas", 9.5f);
            _lblMouse.Text = "btn=--   x=--   y=--   wheel=--";
            _lblMouse.TextAlign = ContentAlignment.MiddleLeft;

            _lblMouseCnt = new Label();
            _lblMouseCnt.SetBounds(524, 36, 120, 20);
            _lblMouseCnt.Text = "累计 0";
            _lblMouseCnt.ForeColor = UiTheme.TextSub;
            _lblMouseCnt.TextAlign = ContentAlignment.MiddleLeft;

            // 按键按钮：绿色 = 按下（L=bit0 / M=bit2 / R=bit1）
            _ledL = MakeLed("L", 656, 30, 64, 30);
            _ledM = MakeLed("M", 728, 30, 64, 30);
            _ledR = MakeLed("R", 800, 30, 64, 30);

            // XY 位移轨迹视图（左）：虚拟游标按上报的相对位移累积，绘制轨迹折线
            _viewXY = new MouseTrackView();
            _viewXY.SetBounds(16, 62, 236, 120);

            // 滚轮方向方块（中）：上滑点亮上块（▲ 主色）、下滑点亮下块（▼ 橙色），约 150ms 自动熄灭
            _viewWheel = new WheelBarView();
            _viewWheel.SetBounds(264, 62, 36, 120);

            // 滚轮累计数值（方块右侧大字）
            _lblWheelVal = new Label();
            _lblWheelVal.SetBounds(308, 62, 60, 120);
            _lblWheelVal.Font = new Font("Consolas", 16f, FontStyle.Bold);
            _lblWheelVal.Text = "0";
            _lblWheelVal.TextAlign = ContentAlignment.MiddleCenter;
            _lblWheelVal.ForeColor = UiTheme.TextSub;

            var lblMouseHint = new Label();
            lblMouseHint.SetBounds(384, 62, 480, 120);
            lblMouseHint.Text = "按键映射：L=bit0 / M=bit2 / R=bit1（绿色 = 按下）\r\n"
                + "XY 视图：随上报位移绘制轨迹，碰边自动钳制\r\n"
                + "滚轮：上滑点亮上方块、下滑点亮下方块；左侧大数字为累计代数和\r\n"
                + "组合键：左键+右键同时按下 = 清空所有记录";
            lblMouseHint.ForeColor = UiTheme.TextSub;
            lblMouseHint.TextAlign = ContentAlignment.MiddleLeft;

            cardMouse.Controls.AddRange(new Control[]
            {
                _lblMouse, _ledL, _ledM, _ledR, _lblMouseCnt,
                _viewXY, _viewWheel, _lblWheelVal, lblMouseHint
            });

            // 触摸卡片已移除（2026-10 产测流程裁剪：触摸流测试项下线，0x22 协议保留在固件侧）

            // 鉴权卡片
            var cardAuth = new RoundedPanel();
            cardAuth.SetBounds(12, 320, 896, 116);
            cardAuth.Controls.Add(MakeCardTitle("鉴权与 AES（PING 0x01 / AES_ENC 0x10 / AES_DEC 0x11 / AUTH 0x12）"));

            _btnAuthRun = MakeButton("运行鉴权测试", 16, 32, 120, 30, true, OnAuthRun);

            _lblAuthResult = new Label();
            _lblAuthResult.SetBounds(148, 32, 120, 30);
            _lblAuthResult.Font = new Font("Microsoft YaHei UI", 14f, FontStyle.Bold);
            _lblAuthResult.TextAlign = ContentAlignment.MiddleCenter;

            _lblAuthDetail = new Label();
            _lblAuthDetail.SetBounds(280, 38, 600, 22);
            _lblAuthDetail.Text = "（点按左侧按钮：PING → AES 往返 ×3 → 鉴权响应 ×3）";
            _lblAuthDetail.ForeColor = UiTheme.TextSub;
            _lblAuthDetail.TextAlign = ContentAlignment.MiddleLeft;

            var lblAuthHint = new Label();
            lblAuthHint.SetBounds(16, 72, 864, 22);
            lblAuthHint.Text = "期望值由 PC 侧同密钥 AES-128 独立计算：随机 16B 加解密往返、随机 8B challenge（8B+8B 零填充）密文比对。";
            lblAuthHint.ForeColor = UiTheme.TextSub;
            lblAuthHint.TextAlign = ContentAlignment.MiddleLeft;

            cardAuth.Controls.AddRange(new Control[]
            {
                _btnAuthRun, _lblAuthResult, _lblAuthDetail, lblAuthHint
            });

            // 日志与记录卡片
            var cardLog = new RoundedPanel();
            cardLog.SetBounds(12, 444, 896, 232);
            cardLog.Controls.Add(MakeCardTitle("日志与记录"));

            _txtLog = new RichTextBox();
            _txtLog.SetBounds(16, 32, 864, 120);
            _txtLog.ReadOnly = true;
            _txtLog.BorderStyle = BorderStyle.None;
            _txtLog.BackColor = UiTheme.InputBg;
            _txtLog.ForeColor = UiTheme.TextMain;
            _txtLog.Font = new Font("Consolas", 9f);
            _txtLog.DetectUrls = false;

            _btnClear = MakeButton("清空记录", 16, 186, 84, 26, false, OnClear);
            _btnExport = MakeButton("导出", 108, 186, 84, 26, false, OnExport);

            _txtServerUrl = new TextBox();
            _txtServerUrl.SetBounds(16, 158, 340, 23);
            _txtServerUrl.Text = "http://127.0.0.1:8081";
            _txtServerUrl.BackColor = UiTheme.InputBg;

            _txtStation = new TextBox();
            _txtStation.SetBounds(362, 158, 80, 23);
            _txtStation.Text = "ST-01";
            _txtStation.BackColor = UiTheme.InputBg;

            _btnUpload = MakeButton("上报服务器", 450, 156, 110, 26, false, OnUpload);

            _lblSummary = new Label();
            _lblSummary.SetBounds(206, 188, 660, 22);
            _lblSummary.ForeColor = UiTheme.TextSub;
            _lblSummary.TextAlign = ContentAlignment.MiddleLeft;

            cardLog.Controls.AddRange(new Control[]
            {
                _txtLog, _btnClear, _btnExport, _txtServerUrl, _txtStation, _btnUpload, _lblSummary
            });

            Controls.AddRange(new Control[] { cardDevice, cardMouse, cardAuth, cardLog });

            UpdateSummary();
        }

        private static Label MakeCardTitle(string text)
        {
            var lbl = new Label();
            lbl.Text = text;
            lbl.SetBounds(14, 8, 700, 20);
            lbl.Font = UiTheme.FontTitle;
            lbl.ForeColor = UiTheme.TextMain;
            lbl.TextAlign = ContentAlignment.MiddleLeft;
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

        private Panel MakeLed(string letter, int x, int y, int w = 44, int h = 24)
        {
            var p = new Panel();
            p.SetBounds(x, y, w, h);
            p.Tag = letter;
            p.Font = UiTheme.FontTitle;
            p.BackColor = Color.White;
            p.ForeColor = LedOffColor;   // ForeColor 承载灯体颜色
            p.Paint += OnLedPaint;
            return p;
        }

        private static void OnLedPaint(object sender, PaintEventArgs e)
        {
            var p = (Panel)sender;
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;

            var rect = new Rectangle(0, 0, p.Width - 1, p.Height - 1);
            using (GraphicsPath path = RoundedPanel.RoundedRectPath(rect, 8))
            using (var brush = new SolidBrush(p.ForeColor))
            {
                g.FillPath(brush, path);
            }

            Color textColor = p.ForeColor == UiTheme.Success ? Color.White : UiTheme.TextSub;
            TextRenderer.DrawText(g, (string)p.Tag, p.Font, p.ClientRectangle, textColor,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
        }

        private static void SetLed(Panel led, bool on)
        {
            Color want = on ? UiTheme.Success : LedOffColor;
            if (led.ForeColor != want)
            {
                led.ForeColor = want;
                led.Invalidate();
            }
        }

        private void UpdateConnUi(bool connected)
        {
            _btnConnect.Text = connected ? "断开" : "连接";
            _lblDevStatus.Text = connected
                ? "● 已连接  " + _link.GetOpenedPath()
                : "○ 未连接";
            _lblDevStatus.ForeColor = connected ? UiTheme.Success : UiTheme.Danger;

            if (!connected)
            {
                _ftActive = false;
                _dutUp = false;
                _sppReady = false;
                _authTick = -1;
                _dutName = string.Empty;
                _dutMac = string.Empty;
                SetLed(_ledL, false);
                SetLed(_ledM, false);
                SetLed(_ledR, false);
                ResetMouseView();
            }

            UpdateFtStatus();
        }

        private void UpdateFtStatus()
        {
            string dutTxt = _dutUp ? "UP" : "DOWN";
            if (_dutUp && !string.IsNullOrEmpty(_dutMac))
            {
                dutTxt += !string.IsNullOrEmpty(_dutName)
                    ? "  " + _dutName + "  [" + _dutMac + "]"
                    : "  [" + _dutMac + "]";
            }
            _lblFtStatus.Text = string.Format(
                "FT: {0}   DUT: {1}   SPP: {2}   auth tick: {3}",
                _ftActive ? "ON" : "OFF",
                dutTxt,
                _sppReady ? "READY" : "N/A",
                _authTick >= 0 ? _authTick.ToString() : "N/A");

            bool connected = _link.IsConnected;
            _btnFtStart.Enabled = connected && !_ftActive;
            _btnFtStop.Enabled = connected && _ftActive;
            _btnRefresh.Enabled = connected;
            /*主按钮样式：DUT 连上 = 蓝紫可点击，断开 = 灰（Enabled=false 的 IsPrimary 渲染）*/
            _btnDutDisc.IsPrimary = true;
            _btnDutDisc.Enabled = connected && _dutUp;
            _btnAuthRun.Enabled = connected && _ftActive;
        }

        // ---------- 事件处理与测试流程 ----------

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
                SendStatusQuery();
                SendDutInfoQuery();
            }
            catch (DllNotFoundException)
            {
                ResetConnectionAfterError();
                LogLine("找不到 BibooHid.dll —— 请先重新生成整个 BibooUxTool.sln（x64）。", UiTheme.Danger);
            }
            catch (BadImageFormatException)
            {
                ResetConnectionAfterError();
                LogLine("BibooHid.dll 架构不匹配 —— DLL 与程序都必须是 x64。", UiTheme.Danger);
            }
            catch (EntryPointNotFoundException ex)
            {
                ResetConnectionAfterError();
                LogLine("BibooHid.dll 版本不匹配 —— 请重新生成整个 BibooUxTool.sln（x64）：" + ex.Message, UiTheme.Danger);
            }
            catch (Exception ex)
            {
                ResetConnectionAfterError();
                LogLine("连接异常（" + ex.GetType().Name + "）：" + ex.Message, UiTheme.Danger);
            }
        }

        /// <summary>50ms 排空上行帧（单消费者）：Pump 内部分发响应 / SPP 重组 / 事件回调。</summary>
        private void OnTimerTick(object sender, EventArgs e)
        {
            for (int i = 0; i < 64; i++)
            {
                int r = _link.Pump(0);
                if (r < 0)
                {
                    HandleLinkLost();
                    return;
                }
                if (r == 0)
                    break;
            }
        }

        private void OnFrameEvent(byte[] frame)
        {
            byte btn;
            short x;
            short y;
            sbyte wheel;
            if (UxFtProto.ParseMouse(frame, out btn, out x, out y, out wheel))
            {
                _mouseCount++;

                string txt = string.Format("btn=0x{0:X2}   x={1}   y={2}   wheel={3}", btn, x, y, wheel);
                if (_lblMouse.Text != txt)
                    _lblMouse.Text = txt;

                SetLed(_ledL, (btn & 0x01) != 0);
                SetLed(_ledM, (btn & 0x04) != 0);
                SetLed(_ledR, (btn & 0x02) != 0);

                // 组合键：左键+右键同时按下 -> 清空所有记录（与"清空记录"按钮等效）；
                // 锁存防止按住期间每帧重复触发，需两键都松开后才能再次触发
                bool clrCombo = (btn & 0x03) == 0x03;
                if (clrCombo && !_clrComboLatch)
                {
                    _clrComboLatch = true;
                    OnClear(null, EventArgs.Empty);
                    LogLine("组合键 左键+右键：已清空所有记录", UiTheme.Primary);
                }
                else if (!clrCombo)
                {
                    _clrComboLatch = false;
                }

                // 可视化视图：XY 轨迹 + 滚轮条 / 滚轮累计
                _viewXY.Feed(x, y);
                _viewWheel.Feed(wheel);
                _wheelTotal += wheel;
                string wtxt = (_wheelTotal > 0 ? "+" : string.Empty) + _wheelTotal;
                if (_lblWheelVal.Text != wtxt)
                {
                    _lblWheelVal.Text = wtxt;
                    _lblWheelVal.ForeColor = _wheelTotal > 0 ? UiTheme.Primary
                        : (_wheelTotal < 0 ? UiTheme.Danger : UiTheme.TextSub);
                }

                string cnt = string.Format("累计 {0}", _mouseCount);
                if (_lblMouseCnt.Text != cnt)
                    _lblMouseCnt.Text = cnt;
                return;
            }

            bool up;
            if (UxFtProto.ParseDutLink(frame, out up))
            {
                if (_dutUp != up)
                {
                    _dutUp = up;
                    if (up)
                    {
                        SendDutInfoQuery();     // 拉取 DUT 蓝牙名 + MAC
                        LogLine("DUT 已连接。", UiTheme.Success);
                    }
                    else
                    {
                        _dutName = string.Empty;
                        _dutMac = string.Empty;
                        LogLine("DUT 已断开。", UiTheme.Danger);
                    }
                    UpdateFtStatus();
                }
                return;
            }

            LogLine("RX 事件（未识别）: " + Hex(frame), UiTheme.TextSub);
        }

        private void OnSppFrameEvent(byte[] spp)
        {
            LogLine("SPP 帧（非响应）: " + Hex(spp), UiTheme.TextSub);
        }

        private void SendStatusQuery()
        {
            if (!_link.IsConnected)
                return;

            byte[] rsp;
            string err;
            int r = _link.SendCommand(UxFtProto.CmdStatus, null, DongleLink.CommandTimeoutMs, out rsp, out err);
            if (!HandleCmdResult(r, err, "STATUS"))
                return;

            if (rsp[UxFtProto.UlOffsStatus] != UxFtProto.StOk)
            {
                LogLine("STATUS 返回 status=" + rsp[UxFtProto.UlOffsStatus], UiTheme.Danger);
                return;
            }

            _ftActive = rsp[UxFtProto.UlOffsData + UxFtProto.StsOffsActive] != 0;
            _dutUp = rsp[UxFtProto.UlOffsData + UxFtProto.StsOffsDutLink] != 0;
            _sppReady = rsp[UxFtProto.UlOffsData + UxFtProto.StsOffsSppReady] != 0;
            _authTick = rsp[UxFtProto.UlOffsData + UxFtProto.StsOffsTickL] |
                        (rsp[UxFtProto.UlOffsData + UxFtProto.StsOffsTickH] << 8);
            UpdateFtStatus();
            if (_dutUp && string.IsNullOrEmpty(_dutMac))
                SendDutInfoQuery();     // DUT 已连但信息未拉取（如 E2 事件先于工具连接到达）
        }

        /// <summary>
        /// 查询 DUT 蓝牙名（0x14 DUT_INFO，12B）+ 对端 MAC（0x15 DUT_MAC，6B）。
        /// 仅在 DUT 已连接时调用；失败静默（DUT 断开时随后的 0xE2 事件 / STATUS 会刷新显示），不刷错误日志。
        /// </summary>
        private void SendDutInfoQuery()
        {
            if (!_link.IsConnected || !_dutUp)
                return;

            byte[] rsp;
            string err;

            // 名字：0x14，响应 data 区 12B 全给本地名（MAC 一帧放不下，单独走 0x15）
            int r = _link.SendCommand(UxFtProto.CmdDutInfo, null, DongleLink.CommandTimeoutMs, out rsp, out err);
            if (r > 0 && rsp[UxFtProto.UlOffsStatus] == UxFtProto.StOk)
            {
                int n = UxFtProto.DutInfoNameLen;
                while (n > 0 && rsp[UxFtProto.UlOffsData + UxFtProto.DutInfoOffsName + n - 1] == 0)
                    n--;        // 名字零填充，去掉尾部 0
                _dutName = (n > 0)
                    ? Encoding.ASCII.GetString(rsp, UxFtProto.UlOffsData + UxFtProto.DutInfoOffsName, n)
                    : string.Empty;
            }

            // MAC：0x15，6B，固件序 LSB 在前 → 显示倒序
            r = _link.SendCommand(UxFtProto.CmdDutMac, null, DongleLink.CommandTimeoutMs, out rsp, out err);
            if (r > 0 && rsp[UxFtProto.UlOffsStatus] == UxFtProto.StOk)
            {
                var mac = new byte[6];
                Array.Copy(rsp, UxFtProto.UlOffsData + UxFtProto.DutMacOffsMac, mac, 0, 6);
                _dutMac = FormatMac(mac);
            }

            if (r > 0)
                UpdateFtStatus();
        }

        /// <summary>固件上报的 MAC 为 LSB 在前（控制器原序），转为惯例的 MSB 前 "AA:BB:CC:DD:EE:FF"。</summary>
        private static string FormatMac(byte[] mac)
        {
            var sb = new StringBuilder(17);
            for (int i = 5; i >= 0; i--)
                sb.Append(mac[i].ToString("X2")).Append(i == 0 ? string.Empty : ":");
            return sb.ToString();
        }

        private void OnFtStart(object sender, EventArgs e)
        {
            if (!RequireConnected())
                return;

            byte[] rsp;
            string err;
            int r = _link.SendCommand(UxFtProto.CmdStart, null, DongleLink.CommandTimeoutMs, out rsp, out err);
            if (!HandleCmdResult(r, err, "FT_START"))
                return;

            if (rsp[UxFtProto.UlOffsStatus] == UxFtProto.StOk)
            {
                _ftActive = true;
                UpdateFtStatus();
                LogLine("已进入产测模式（FT ON，鼠标报告镜像为 0xE1，USB 转发不受影响）。", UiTheme.Success);
                SendStatusQuery();
                SendDutInfoQuery();
            }
            else
            {
                LogLine("FT_START 被拒绝：status=" + rsp[UxFtProto.UlOffsStatus] + "（DUT 不在连？）", UiTheme.Danger);
            }
        }

        private void OnFtStop(object sender, EventArgs e)
        {
            if (!RequireConnected())
                return;

            byte[] rsp;
            string err;
            int r = _link.SendCommand(UxFtProto.CmdStop, null, DongleLink.CommandTimeoutMs, out rsp, out err);
            if (!HandleCmdResult(r, err, "FT_STOP"))
                return;

            if (rsp[UxFtProto.UlOffsStatus] == UxFtProto.StOk)
            {
                _ftActive = false;
                UpdateFtStatus();
                LogLine("已退出产测模式（FT OFF，恢复 USB 转发）。", UiTheme.Success);
                SendStatusQuery();
            }
            else
            {
                LogLine("FT_STOP 返回 status=" + rsp[UxFtProto.UlOffsStatus], UiTheme.Danger);
            }
        }

        private void OnRefresh(object sender, EventArgs e)
        {
            SendStatusQuery();
            LogLine(string.Format("状态刷新：FT={0}  DUT={1}  SPP={2}  tick={3}",
                _ftActive ? "ON" : "OFF",
                _dutUp ? "UP" : "DOWN",
                _sppReady ? "READY" : "N/A",
                _authTick >= 0 ? _authTick.ToString() : "N/A"), UiTheme.TextSub);
        }

        private void OnDutDisc(object sender, EventArgs e)
        {
            if (!RequireConnected())
                return;

            byte[] rsp;
            string err;
            int r = _link.SendCommand(UxFtProto.CmdDutDisconnect, null, DongleLink.CommandTimeoutMs, out rsp, out err);
            if (!HandleCmdResult(r, err, "DUT_DISCONNECT"))
                return;

            if (rsp[UxFtProto.UlOffsStatus] == UxFtProto.StOk)
            {
                LogLine("已断开当前 DUT。", UiTheme.Success);
                SendStatusQuery();
            }
            else
            {
                LogLine("DUT_DISCONNECT 返回 status=" + rsp[UxFtProto.UlOffsStatus], UiTheme.Danger);
            }
        }

        private void OnAuthRun(object sender, EventArgs e)
        {
            if (!RequireConnected())
                return;

            _lblAuthResult.Text = "测试中";
            _lblAuthResult.ForeColor = UiTheme.Primary;
            _lblAuthDetail.Text = "PING → AES 往返 ×3 → 鉴权响应 ×3，请稍候...";
            Application.DoEvents();

            int pass0 = _passCount;
            int fail0 = _failCount;
            var rnd = new Random();
            bool abort = false;

            // 1) PING / 版本握手
            {
                byte[] rsp;
                string err;
                int r = _link.SendSppFrameWait(NewSppFrame(UxFtProto.DutCmdPing), SppTimeoutMs, out rsp, out err);
                if (r == 1)
                {
                    string val = string.Format("ver={0}  model=0x{1:X2}  mouseLen={2}",
                        rsp[UxFtProto.SppOffsArgs + UxFtProto.PingOffsVersion],
                        rsp[UxFtProto.SppOffsArgs + UxFtProto.PingOffsModel],
                        rsp[UxFtProto.SppOffsArgs + UxFtProto.PingOffsMouseLen]);
                    LogLine("PING 响应：" + val, UiTheme.Rx);
                    AddResult("PING", val, true);
                }
                else
                {
                    string why = r == 0 ? "响应超时" : err;
                    LogLine("PING 失败：" + why, UiTheme.Danger);
                    AddResult("PING", why, false);
                    abort = r < 0;
                }
            }

            // 2) AES 加解密往返 ×3（随机 16B 明文，期望与 PC 侧密文一致 / 解密还原）
            for (int i = 1; i <= 3 && !abort; i++)
            {
                var plain = new byte[16];
                rnd.NextBytes(plain);
                byte[] cipher = UxFtAes.EncryptBlock(plain);

                byte[] req = NewSppFrame(UxFtProto.DutCmdAesEnc);
                Array.Copy(plain, 0, req, UxFtProto.SppOffsArgs, 16);
                byte[] rsp;
                string err;
                int r = _link.SendSppFrameWait(req, SppTimeoutMs, out rsp, out err);
                byte[] got = r == 1 ? Slice(rsp, UxFtProto.SppOffsArgs, 16) : null;
                bool encOk = got != null && UxFtAes.BlockEquals(cipher, got);
                LogLine(string.Format("AES_ENC #{0}: {1}（plain={2}）", i, encOk ? "PASS" : "FAIL", Hex(plain)),
                    encOk ? UiTheme.Success : UiTheme.Danger);
                AddResult("AES_ENC #" + i, got != null ? Hex(got) : (r == 0 ? "响应超时" : err), encOk);
                abort = r < 0;

                req = NewSppFrame(UxFtProto.DutCmdAesDec);
                Array.Copy(cipher, 0, req, UxFtProto.SppOffsArgs, 16);
                r = _link.SendSppFrameWait(req, SppTimeoutMs, out rsp, out err);
                got = r == 1 ? Slice(rsp, UxFtProto.SppOffsArgs, 16) : null;
                bool decOk = got != null && UxFtAes.BlockEquals(plain, got);
                LogLine(string.Format("AES_DEC #{0}: {1}", i, decOk ? "PASS" : "FAIL"),
                    decOk ? UiTheme.Success : UiTheme.Danger);
                AddResult("AES_DEC #" + i, got != null ? Hex(got) : (r == 0 ? "响应超时" : err), decOk);
                abort = abort || r < 0;
            }

            // 3) 鉴权 challenge-response ×3（随机 8B challenge，期望 AES(8B + 8B 零填充)）
            for (int i = 1; i <= 3 && !abort; i++)
            {
                var challenge = new byte[8];
                rnd.NextBytes(challenge);
                byte[] expect = UxFtAes.AuthResponse(challenge);

                byte[] req = NewSppFrame(UxFtProto.DutCmdAuth);
                Array.Copy(challenge, 0, req, UxFtProto.SppOffsArgs, 8);
                byte[] rsp;
                string err;
                int r = _link.SendSppFrameWait(req, SppTimeoutMs, out rsp, out err);
                byte[] got = r == 1 ? Slice(rsp, UxFtProto.SppOffsArgs, 16) : null;
                bool ok = got != null && UxFtAes.BlockEquals(expect, got);
                LogLine(string.Format("AUTH #{0}: {1}（challenge={2}）", i, ok ? "PASS" : "FAIL", Hex(challenge)),
                    ok ? UiTheme.Success : UiTheme.Danger);
                AddResult("AUTH #" + i, got != null ? Hex(got) : (r == 0 ? "响应超时" : err), ok);
                abort = abort || r < 0;
            }

            if (abort)
            {
                LogLine("链路中断，鉴权测试提前结束。", UiTheme.Danger);
                if (!_link.IsConnected)
                    HandleLinkLost();
            }

            int pass = _passCount - pass0;
            int fail = _failCount - fail0;
            _lblAuthResult.Text = (pass > 0 && fail == 0) ? "PASS" : "FAIL";
            _lblAuthResult.ForeColor = (pass > 0 && fail == 0) ? UiTheme.Success : UiTheme.Danger;
            _lblAuthDetail.Text = string.Format("本次：通过 {0} / 失败 {1}；累计 PASS {2} / FAIL {3}",
                pass, fail, _passCount, _failCount);
            LogLine(string.Format("鉴权测试完成：通过 {0} / 失败 {1}。", pass, fail),
                fail == 0 ? UiTheme.Success : UiTheme.Danger);
        }

        private async void OnUpload(object sender, EventArgs e)
        {
            if (_results.Count == 0)
            {
                LogLine("没有可上报的记录（先跑一次鉴权测试）。", UiTheme.TextSub);
                return;
            }
            string baseUrl = _txtServerUrl.Text.Trim().TrimEnd('/');
            string station = _txtStation.Text.Trim();
            if (string.IsNullOrEmpty(baseUrl))
            {
                LogLine("服务器地址为空。", UiTheme.Danger);
                return;
            }
            LogLine("上报 " + _results.Count + " 条记录到 " + baseUrl + " ...", UiTheme.TextSub);
            bool ok = await FactoryTestUploader.UploadAsync(baseUrl, station, string.Empty,
                _dutName, _dutMac, _results, _passCount, _failCount).ConfigureAwait(false);
            LogLine(ok ? "上报成功（服务器已存 " + _results.Count + " 条）。"
                      : "上报失败：服务器不可达或返回错误。", ok ? UiTheme.Success : UiTheme.Danger);
        }

        private void OnClear(object sender, EventArgs e)
        {
            _txtLog.Clear();
            _results.Clear();
            _passCount = 0;
            _failCount = 0;
            _mouseCount = 0;
            _lblMouseCnt.Text = "累计 0";
            _lblMouse.Text = "btn=--   x=--   y=--   wheel=--";
            ResetMouseView();
            _lblAuthResult.Text = string.Empty;
            _lblAuthDetail.Text = "（点按左侧按钮：PING → AES 往返 ×3 → 鉴权响应 ×3）";
            UpdateSummary();
        }

        private void OnExport(object sender, EventArgs e)
        {
            using (var dlg = new SaveFileDialog())
            {
                dlg.Title = "导出产测记录";
                dlg.Filter = "文本日志 (*.txt)|*.txt|CSV 记录 (*.csv)|*.csv";
                dlg.FilterIndex = 1;
                dlg.DefaultExt = "txt";
                dlg.FileName = "BibooUxFt_" + DateTime.Now.ToString("yyyyMMdd_HHmmss");
                if (dlg.ShowDialog(this) != DialogResult.OK)
                    return;

                try
                {
                    if (dlg.FilterIndex == 2)
                        ExportCsv(dlg.FileName);
                    else
                        File.WriteAllText(dlg.FileName, _txtLog.Text, new UTF8Encoding(true));
                    LogLine("已导出: " + dlg.FileName, UiTheme.Success);
                }
                catch (Exception ex)
                {
                    LogLine("导出失败: " + ex.Message, UiTheme.Danger);
                }
            }
        }

        private void ExportCsv(string path)
        {
            var sb = new StringBuilder();
            sb.AppendLine("Time,Item,Value,Result");
            foreach (string[] row in _results)
            {
                for (int i = 0; i < row.Length; i++)
                {
                    if (i > 0)
                        sb.Append(',');
                    sb.Append('"').Append((row[i] ?? string.Empty).Replace("\"", "\"\"")).Append('"');
                }
                sb.AppendLine();
            }
            sb.AppendFormat("Summary,PASS={0},FAIL={1},Total={2}", _passCount, _failCount, _results.Count);
            sb.AppendLine();
            File.WriteAllText(path, sb.ToString(), new UTF8Encoding(true));
        }

        private void AddResult(string item, string value, bool ok)
        {
            _results.Add(new string[]
            {
                DateTime.Now.ToString("HH:mm:ss"),
                item,
                value ?? string.Empty,
                ok ? "PASS" : "FAIL"
            });

            if (ok)
                _passCount++;
            else
                _failCount++;
            UpdateSummary();
        }

        private void UpdateSummary()
        {
            _lblSummary.Text = string.Format("记录 {0} 条   PASS {1}   FAIL {2}",
                _results.Count, _passCount, _failCount);
        }

        // ---------- 工具方法 ----------

        private void LogLine(string msg, Color color)
        {
            if (_txtLog.TextLength > 60000)
                _txtLog.Clear();

            _txtLog.SelectionStart = _txtLog.TextLength;
            _txtLog.SelectionColor = UiTheme.Timestamp;
            _txtLog.AppendText(DateTime.Now.ToString("HH:mm:ss.fff") + "  ");
            _txtLog.SelectionColor = color;
            _txtLog.AppendText(msg + Environment.NewLine);
            _txtLog.ScrollToCaret();
        }

        private static string Hex(byte[] data)
        {
            return BitConverter.ToString(data).Replace("-", " ");
        }

        private static byte[] Slice(byte[] src, int offset, int count)
        {
            var dst = new byte[count];
            Array.Copy(src, offset, dst, 0, count);
            return dst;
        }

        private static byte[] NewSppFrame(byte cmd)
        {
            var f = new byte[UxFtProto.SppSize];
            f[UxFtProto.SppOffsCmd] = cmd;
            return f;
        }

        private bool RequireConnected()
        {
            if (_link.IsConnected)
                return true;
            LogLine("尚未连接 dongle。", UiTheme.Danger);
            return false;
        }

        private bool HandleCmdResult(int r, string err, string what)
        {
            if (r == 0)
            {
                LogLine(what + " 超时（无响应）。", UiTheme.Danger);
                return false;
            }
            if (r < 0)
            {
                LogLine(what + " 失败: " + err, UiTheme.Danger);
                if (!_link.IsConnected)
                    HandleLinkLost();
                return false;
            }
            return true;
        }

        private void HandleLinkLost()
        {
            _timer.Stop();
            try
            {
                _link.Disconnect();
            }
            catch (Exception)
            {
            }
            UpdateConnUi(false);
            LogLine("USB Dongle 已断开。", UiTheme.Danger);
        }

        private void ResetConnectionAfterError()
        {
            _timer.Stop();
            try
            {
                _link.Disconnect();
            }
            catch (Exception)
            {
            }
            UpdateConnUi(false);
        }

        private void ResetMouseView()
        {
            _viewXY.ResetView();
            _viewWheel.ResetView();
            _wheelTotal = 0;
            _lblWheelVal.Text = "0";
            _lblWheelVal.ForeColor = UiTheme.TextSub;
        }

        // ---------- 鼠标可视化视图（自绘） ----------

        /// <summary>
        /// XY 位移轨迹视图：虚拟游标按 0xE1 上报的相对位移累积移动（±Range 钳制，
        /// 绘制最近 MaxTrail 个点的轨迹折线 + 当前位置亮点。BLE 报告 y 即屏幕
        /// 方向（向下为正，与 USB 鼠标一致），不做取反。
        /// </summary>
        private sealed class MouseTrackView : Panel
        {
            private const int Range = 512;       // 虚拟坐标 ±Range
            private const int MaxTrail = 400;    // 轨迹点上限（超出丢弃最早的）

            private Point _cursor;
            private readonly List<Point> _trail = new List<Point>(MaxTrail + 1);

            public MouseTrackView()
            {
                DoubleBuffered = true;
                BackColor = UiTheme.InputBg;
            }

            public void Feed(int dx, int dy)
            {
                _cursor.X = Math.Max(-Range, Math.Min(Range, _cursor.X + dx));
                _cursor.Y = Math.Max(-Range, Math.Min(Range, _cursor.Y + dy));
                _trail.Add(_cursor);
                if (_trail.Count > MaxTrail)
                    _trail.RemoveRange(0, _trail.Count - MaxTrail);
                Invalidate();
            }

            public void ResetView()
            {
                _cursor = Point.Empty;
                _trail.Clear();
                Invalidate();
            }

            protected override void OnPaint(PaintEventArgs e)
            {
                Graphics g = e.Graphics;
                g.SmoothingMode = SmoothingMode.AntiAlias;
                Rectangle rc = ClientRectangle;
                var center = new Point(rc.Width / 2, rc.Height / 2);
                float sx = (rc.Width / 2f - 8f) / Range;
                float sy = (rc.Height / 2f - 8f) / Range;

                using (var penBorder = new Pen(UiTheme.CardBorder))
                    g.DrawRectangle(penBorder, 0, 0, rc.Width - 1, rc.Height - 1);

                using (var penDash = new Pen(Color.FromArgb(0xC9, 0xD4, 0xE8)) { DashStyle = DashStyle.Dash })
                {
                    g.DrawLine(penDash, center.X, 4, center.X, rc.Height - 4);
                    g.DrawLine(penDash, 4, center.Y, rc.Width - 4, center.Y);
                }

                if (_trail.Count > 1)
                {
                    using (var pen = new Pen(UiTheme.Primary, 2f))
                    {
                        PointF prev = PointF.Empty;
                        bool first = true;
                        foreach (Point pt in _trail)
                        {
                            var cur = new PointF(center.X + pt.X * sx, center.Y + pt.Y * sy);
                            if (!first)
                                g.DrawLine(pen, prev, cur);
                            prev = cur;
                            first = false;
                        }
                    }
                }

                if (_trail.Count > 0)
                {
                    float px = center.X + _cursor.X * sx;
                    float py = center.Y + _cursor.Y * sy;
                    using (var brush = new SolidBrush(UiTheme.Primary))
                        g.FillEllipse(brush, px - 4f, py - 4f, 8f, 8f);
                    using (var brush = new SolidBrush(Color.White))
                        g.FillEllipse(brush, px - 1.5f, py - 1.5f, 3f, 3f);
                }
                else
                {
                    using (var brush = new SolidBrush(Color.FromArgb(0xC9, 0xD4, 0xE8)))
                        g.FillEllipse(brush, center.X - 3, center.Y - 3, 6, 6);
                }
            }
        }

        /// <summary>
        /// 滚轮方向方块：上块 = 上滑（wheel>0）、下块 = 下滑（wheel&lt;0）。滚轮滚动时
        /// 对应方向方块点亮（▲ 主色 / ▼ 橙色）约 150ms 后自动熄灭，方向一眼可辨；
        /// 累计量看旁边的大数字 Label。
        /// </summary>
        private sealed class WheelBarView : Panel
        {
            private static readonly Color WheelNeg = Color.FromArgb(0xF5, 0x9E, 0x6C);
            private static readonly Color BlockOff = Color.FromArgb(0xE9, 0xEE, 0xF8);

            private int _value;
            private int _fade;                    // 剩余保持 tick 数（150ms × 1 ≈ 150ms）
            private readonly Timer _fadeTimer;

            public WheelBarView()
            {
                DoubleBuffered = true;
                BackColor = UiTheme.InputBg;
                _fadeTimer = new Timer { Interval = 150 };
                _fadeTimer.Tick += OnFadeTick;
            }

            public void Feed(int wheel)
            {
                if (wheel != 0)
                {
                    _value = wheel;
                    _fade = 1;                    // 重新点亮保持
                    _fadeTimer.Start();
                    Invalidate();
                }
            }

            public void ResetView()
            {
                _value = 0;
                _fade = 0;
                _fadeTimer.Stop();
                Invalidate();
            }

            protected override void Dispose(bool disposing)
            {
                if (disposing)
                    _fadeTimer.Dispose();
                base.Dispose(disposing);
            }

            private void OnFadeTick(object sender, EventArgs e)
            {
                if (--_fade <= 0)
                {
                    _value = 0;
                    _fadeTimer.Stop();
                }
                Invalidate();
            }

            protected override void OnPaint(PaintEventArgs e)
            {
                Graphics g = e.Graphics;
                g.SmoothingMode = SmoothingMode.AntiAlias;
                Rectangle rc = ClientRectangle;

                using (var penBorder = new Pen(UiTheme.CardBorder))
                    g.DrawRectangle(penBorder, 0, 0, rc.Width - 1, rc.Height - 1);

                int gap = 4;
                int half = (rc.Height - gap) / 2;
                var rectUp = new Rectangle(3, 3, rc.Width - 6, half - 3);
                var rectDown = new Rectangle(3, half + gap, rc.Width - 6, rc.Height - half - gap - 3);

                bool upOn = _value > 0;
                bool downOn = _value < 0;

                using (var path = RoundedPanel.RoundedRectPath(rectUp, 6))
                using (var brush = new SolidBrush(upOn ? UiTheme.Primary : BlockOff))
                    g.FillPath(brush, path);
                using (var path = RoundedPanel.RoundedRectPath(rectDown, 6))
                using (var brush = new SolidBrush(downOn ? WheelNeg : BlockOff))
                    g.FillPath(brush, path);

                // ▲ / ▼ 符号：点亮块白字，熄灭块灰字
                using (var font = new Font("Microsoft YaHei UI", 11f, FontStyle.Bold))
                {
                    TextRenderer.DrawText(g, "▲", font, rectUp, upOn ? Color.White : UiTheme.TextSub,
                        TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
                    TextRenderer.DrawText(g, "▼", font, rectDown, downOn ? Color.White : UiTheme.TextSub,
                        TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
                }
            }
        }
    }
}
