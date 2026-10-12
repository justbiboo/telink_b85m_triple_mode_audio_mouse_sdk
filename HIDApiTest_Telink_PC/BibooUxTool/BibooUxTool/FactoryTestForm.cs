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
    /// dongle 产测页（Task 12）：连接管理 / 产测模式开关 / 鼠标报告 LED / 触摸滑条 /
    /// 鉴权+AES 往返 / 记录与导出。
    /// 读通道约定：本页自持 DongleLink 实例，以 50ms UI 定时器 Pump 排空上行帧；
    /// 按钮操作在 UI 线程内阻塞等待（SendCommand / SendSppFrameWait 内部也走 Pump），
    /// WinForms 定时器与按钮回调不会并发 —— 单消费者模型。
    /// 打开本页前须先断开 MainForm 的连接（同一 HID 通道独占）。
    /// </summary>
    public sealed class FactoryTestForm : Form
    {
        private const int TouchPosMax = 1008;   // 触摸位置量程（DUT 上报 pos 的满量程）
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

        // 鼠标报告卡片
        private Label _lblMouse;
        private Label _lblMouseCnt;
        private Panel _ledL;
        private Panel _ledM;
        private Panel _ledR;
        private long _mouseCount;

        // 触摸卡片
        private GradientButton _btnTouchStart;
        private GradientButton _btnTouchStop;
        private RoundedPanel _trackBg;
        private RoundedPanel _thumb;
        private Label _lblTouchState;
        private Label _lblTouchInfo;
        private long _touchCount;

        // 鉴权卡片
        private GradientButton _btnAuthRun;
        private Label _lblAuthResult;
        private Label _lblAuthDetail;

        // 日志与记录卡片
        private RichTextBox _txtLog;
        private Label _lblSummary;
        private GradientButton _btnClear;
        private GradientButton _btnExport;

        // 产测状态镜像（由 STATUS 查询 / 0xE2 事件维护）
        private bool _ftActive;
        private bool _dutUp;
        private bool _sppReady;
        private int _authTick = -1;

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
            Text = "Biboo UX 上位机 - 产线测试";
            BackColor = UiTheme.FormBg;
            Font = UiTheme.FontBody;
            ClientSize = new Size(920, 700);
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
            cardDevice.Controls.Add(MakeCardTitle("设备（Host Link：下行 0x09 / 上行 0x08，15B）"));

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

            cardDevice.Controls.AddRange(new Control[]
            {
                _btnConnect, _btnFtStart, _btnFtStop, _btnRefresh, _btnDutDisc,
                _lblDevStatus, _lblFtStatus
            });

            // 鼠标报告卡片
            var cardMouse = new RoundedPanel();
            cardMouse.SetBounds(12, 116, 896, 104);
            cardMouse.Controls.Add(MakeCardTitle("鼠标报告（0xE1；进入产测后 BLE 报告旁路为事件）"));

            _lblMouse = new Label();
            _lblMouse.SetBounds(16, 36, 520, 22);
            _lblMouse.Font = new Font("Consolas", 9.5f);
            _lblMouse.Text = "btn=--   x=--   y=--   wheel=--";
            _lblMouse.TextAlign = ContentAlignment.MiddleLeft;

            _ledL = MakeLed("L", 560, 36);
            _ledM = MakeLed("M", 612, 36);
            _ledR = MakeLed("R", 664, 36);

            _lblMouseCnt = new Label();
            _lblMouseCnt.SetBounds(724, 38, 150, 20);
            _lblMouseCnt.Text = "累计 0";
            _lblMouseCnt.ForeColor = UiTheme.TextSub;
            _lblMouseCnt.TextAlign = ContentAlignment.MiddleLeft;

            var lblMouseHint = new Label();
            lblMouseHint.SetBounds(16, 70, 864, 22);
            lblMouseHint.Text = "按键位映射：L=bit0 / M=bit2 / R=bit1；仅在产测模式下上报。";
            lblMouseHint.ForeColor = UiTheme.TextSub;
            lblMouseHint.TextAlign = ContentAlignment.MiddleLeft;

            cardMouse.Controls.AddRange(new Control[]
            {
                _lblMouse, _ledL, _ledM, _ledR, _lblMouseCnt, lblMouseHint
            });

            // 触摸卡片
            var cardTouch = new RoundedPanel();
            cardTouch.SetBounds(12, 228, 896, 104);
            cardTouch.Controls.Add(MakeCardTitle("触摸滑条（0x22 触摸流：DUT → dongle → PC）"));

            _btnTouchStart = MakeButton("开始触摸流", 16, 34, 96, 28, true, OnTouchStart);
            _btnTouchStop = MakeButton("停止触摸流", 120, 34, 96, 28, false, OnTouchStop);

            _trackBg = new RoundedPanel();
            _trackBg.SetBounds(232, 42, 480, 12);
            _trackBg.Radius = 6;
            _trackBg.BorderColor = UiTheme.CardBorder;
            _trackBg.BackColor = UiTheme.InputBg;

            _thumb = new RoundedPanel();
            _thumb.SetBounds(1, 2, 20, 8);
            _thumb.Radius = 4;
            _thumb.BackColor = UiTheme.TextSub;
            _thumb.BorderColor = UiTheme.TextSub;
            _trackBg.Controls.Add(_thumb);

            _lblTouchState = new Label();
            _lblTouchState.SetBounds(724, 36, 150, 22);
            _lblTouchState.Text = "—";
            _lblTouchState.ForeColor = UiTheme.TextSub;
            _lblTouchState.TextAlign = ContentAlignment.MiddleLeft;

            _lblTouchInfo = new Label();
            _lblTouchInfo.SetBounds(16, 72, 864, 22);
            _lblTouchInfo.Font = new Font("Consolas", 9f);
            _lblTouchInfo.Text = "（等待 0x22 触摸事件；点【开始触摸流】开启）";
            _lblTouchInfo.ForeColor = UiTheme.TextSub;
            _lblTouchInfo.TextAlign = ContentAlignment.MiddleLeft;

            cardTouch.Controls.AddRange(new Control[]
            {
                _btnTouchStart, _btnTouchStop, _trackBg, _lblTouchState, _lblTouchInfo
            });

            // 鉴权卡片
            var cardAuth = new RoundedPanel();
            cardAuth.SetBounds(12, 340, 896, 116);
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
            cardLog.SetBounds(12, 464, 896, 224);
            cardLog.Controls.Add(MakeCardTitle("日志与记录"));

            _txtLog = new RichTextBox();
            _txtLog.SetBounds(16, 32, 864, 148);
            _txtLog.ReadOnly = true;
            _txtLog.BorderStyle = BorderStyle.None;
            _txtLog.BackColor = UiTheme.InputBg;
            _txtLog.ForeColor = UiTheme.TextMain;
            _txtLog.Font = new Font("Consolas", 9f);
            _txtLog.DetectUrls = false;

            _btnClear = MakeButton("清空记录", 16, 188, 84, 26, false, OnClear);
            _btnExport = MakeButton("导出", 108, 188, 84, 26, false, OnExport);

            _lblSummary = new Label();
            _lblSummary.SetBounds(206, 190, 660, 22);
            _lblSummary.ForeColor = UiTheme.TextSub;
            _lblSummary.TextAlign = ContentAlignment.MiddleLeft;

            cardLog.Controls.AddRange(new Control[]
            {
                _txtLog, _btnClear, _btnExport, _lblSummary
            });

            Controls.AddRange(new Control[] { cardDevice, cardMouse, cardTouch, cardAuth, cardLog });

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

        private Panel MakeLed(string letter, int x, int y)
        {
            var p = new Panel();
            p.SetBounds(x, y, 44, 24);
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
                SetLed(_ledL, false);
                SetLed(_ledM, false);
                SetLed(_ledR, false);
            }

            UpdateFtStatus();
        }

        private void UpdateFtStatus()
        {
            _lblFtStatus.Text = string.Format(
                "FT: {0}   DUT: {1}   SPP: {2}   auth tick: {3}",
                _ftActive ? "ON" : "OFF",
                _dutUp ? "UP" : "DOWN",
                _sppReady ? "READY" : "N/A",
                _authTick >= 0 ? _authTick.ToString() : "N/A");

            bool connected = _link.IsConnected;
            _btnFtStart.Enabled = connected && !_ftActive;
            _btnFtStop.Enabled = connected && _ftActive;
            _btnRefresh.Enabled = connected;
            _btnDutDisc.Enabled = connected && _dutUp;
            _btnTouchStart.Enabled = connected && _ftActive;
            _btnTouchStop.Enabled = connected && _ftActive;
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
                    UpdateFtStatus();
                    LogLine(up ? "DUT 已连接。" : "DUT 已断开。", up ? UiTheme.Success : UiTheme.Danger);
                }
                return;
            }

            LogLine("RX 事件（未识别）: " + Hex(frame), UiTheme.TextSub);
        }

        private void OnSppFrameEvent(byte[] spp)
        {
            bool pressed;
            int pos;
            byte seq;
            if (UxFtProto.ParseTouch(spp, out pressed, out pos, out seq))
            {
                _touchCount++;

                Color tone = pressed ? UiTheme.Primary : UiTheme.TextSub;
                if (_thumb.BackColor != tone)
                {
                    _thumb.BackColor = tone;
                    _thumb.BorderColor = tone;
                    _thumb.Invalidate();
                }
                _thumb.Left = ThumbLeft(pos);

                string state = pressed ? "按下" : "抬起";
                if (_lblTouchState.Text != state)
                {
                    _lblTouchState.Text = state;
                    _lblTouchState.ForeColor = tone;
                }

                _lblTouchInfo.Text = string.Format("seq={0}   pressed={1}   pos={2} / {3}   累计 {4} 条",
                    seq, pressed ? 1 : 0, pos, TouchPosMax, _touchCount);
                return;
            }

            LogLine("SPP 帧（非 0x22）: " + Hex(spp), UiTheme.TextSub);
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
                LogLine("已进入产测模式（FT ON，鼠标报告旁路为 0xE1）。", UiTheme.Success);
                SendStatusQuery();
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

        private void OnTouchStart(object sender, EventArgs e)
        {
            if (!RequireConnected())
                return;
            SendTouchCmd(UxFtProto.DutCmdTouchStart, "TOUCH_START");
        }

        private void OnTouchStop(object sender, EventArgs e)
        {
            if (!RequireConnected())
                return;
            SendTouchCmd(UxFtProto.DutCmdTouchStop, "TOUCH_STOP");
        }

        private void SendTouchCmd(byte cmd, string what)
        {
            byte[] rsp;
            string err;
            int r = _link.SendSppFrameWait(NewSppFrame(cmd), SppTimeoutMs, out rsp, out err);
            if (r == 0)
            {
                LogLine(what + " 响应超时。", UiTheme.Danger);
                return;
            }
            if (r < 0)
            {
                LogLine(what + " 失败: " + err, UiTheme.Danger);
                if (!_link.IsConnected)
                    HandleLinkLost();
                return;
            }

            byte st = rsp[UxFtProto.SppOffsArgs];
            if (st != UxFtProto.DutStOk)
            {
                LogLine(what + " 返回错误（" + st + "）。", UiTheme.Danger);
                return;
            }

            LogLine(what + " 成功。", UiTheme.Success);
            if (cmd == UxFtProto.DutCmdTouchStart)
            {
                _touchCount = 0;
                _lblTouchState.Text = "流已开启";
                _lblTouchState.ForeColor = UiTheme.Primary;
            }
            else
            {
                _lblTouchState.Text = "流已停止";
                _lblTouchState.ForeColor = UiTheme.TextSub;
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

        private void OnClear(object sender, EventArgs e)
        {
            _txtLog.Clear();
            _results.Clear();
            _passCount = 0;
            _failCount = 0;
            _mouseCount = 0;
            _touchCount = 0;
            _lblMouseCnt.Text = "累计 0";
            _lblMouse.Text = "btn=--   x=--   y=--   wheel=--";
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

        private int ThumbLeft(int pos)
        {
            int usable = _trackBg.ClientSize.Width - _thumb.Width - 2;
            if (usable < 1)
                usable = 1;
            if (pos < 0)
                pos = 0;
            if (pos > TouchPosMax)
                pos = TouchPosMax;
            return 1 + (int)((long)pos * usable / TouchPosMax);
        }
    }
}
