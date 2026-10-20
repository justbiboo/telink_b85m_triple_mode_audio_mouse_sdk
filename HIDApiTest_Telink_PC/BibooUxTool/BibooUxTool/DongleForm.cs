using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.Threading;
using System.Windows.Forms;

namespace BibooUxTool
{
    /// <summary>
    /// 模块①：收发调试（浅色蓝紫主题，经 USB dongle 的 HID 通道）。
    ///   连接 / 发送 15 字节 Feature payload / 接收 15 字节 Input payload / 收发日志。
    /// 由首页 HomeForm 以模态方式打开；本页自持 DongleLink 实例，与产线测试页
    /// 共用 dongle 的 HID 通道，因此两页不可同时打开（首页逐个进入即可保证）。
    /// </summary>
    public sealed class DongleForm : Form
    {
        private readonly DongleLink _link = new DongleLink();
        private volatile Thread _readerThread;
        private volatile bool _readerRunning;

        private GradientButton _btnConnect;
        private Label _lblStatus;
        private Label _lblPath;
        private TextBox _txtPayload;
        private GradientButton _btnSend;
        private Label _lblRx;
        private RichTextBox _txtLog;
        private GradientButton _btnClear;

        /// <summary>参与「内容列居中」的卡片（窗口拉宽时限宽居中，见 LayoutContent）。</summary>
        private readonly List<Control> _contentCards = new List<Control>();

        /// <summary>卡片设计宽度（ClientSize 780 - 两侧各 12px 边距）。</summary>
        private const int CardDesignWidth = 756;

        public DongleForm()
        {
            BuildUi();
            UpdateConnUi(false);
            LogLine("收发调试就绪（下行 0x09 Feature：15 字节，上行 0x08 Input：15 字节）。", UiTheme.TextSub);
            LogLine("请插入 dongle 后点击【连接】。", UiTheme.TextSub);
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            // 首显后再整体重绘一次，清除控件初始显示阶段的原生绘制角部残留。
            Refresh();
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            StopReader();
            _link.Dispose();
            base.OnFormClosed(e);
        }

        // ---------- UI 构建 ----------

        private void BuildUi()
        {
            Text = "Biboo OS - 收发调试";
            BackColor = UiTheme.FormBg;
            Font = UiTheme.FontBody;
            ClientSize = new Size(780, 600);
            MinimumSize = new Size(660, 520);
            StartPosition = FormStartPosition.CenterParent;

            try
            {
                // 复用 EXE 内嵌的 ApplicationIcon，设置到窗体标题栏 / 任务栏。
                Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            }
            catch
            {
                // 提取失败（如调试环境异常）不影响主流程。
            }

            // 设备连接
            var cardDevice = new RoundedPanel();
            cardDevice.SetBounds(12, 12, CardDesignWidth, 78);
            // 不设左右锚定：宽度/水平位置由 LayoutContent 统一管理（限宽 + 居中），
            // 否则窗口一拉宽卡片就被拉伸成「左边内容、右边一大片空白」的长条。
            cardDevice.Anchor = AnchorStyles.Top;

            cardDevice.Controls.Add(MakeCardTitle("设备"));

            _btnConnect = new GradientButton();
            _btnConnect.SetBounds(16, 34, 92, 30);
            _btnConnect.Click += OnConnect;

            _lblStatus = new Label();
            _lblStatus.SetBounds(122, 38, 100, 22);
            _lblStatus.TextAlign = ContentAlignment.MiddleLeft;
            _lblStatus.AutoSize = false;
            _lblStatus.Anchor = AnchorStyles.Top | AnchorStyles.Left;

            _lblPath = new Label();
            _lblPath.SetBounds(232, 38, 520, 22);
            _lblPath.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _lblPath.TextAlign = ContentAlignment.MiddleLeft;
            _lblPath.AutoSize = false;   // 关掉自动宽度：长 HID 路径否则会撑破卡片
            _lblPath.AutoEllipsis = true;
            _lblPath.ForeColor = UiTheme.TextSub;

            cardDevice.Controls.AddRange(new Control[] { _btnConnect, _lblStatus, _lblPath });

            // 发送
            var cardSend = new RoundedPanel();
            cardSend.SetBounds(12, 96, CardDesignWidth, 80);
            cardSend.Anchor = AnchorStyles.Top;

            cardSend.Controls.Add(MakeCardTitle("发送 payload（hex，≤15 字节，不足补 0）"));

            var lblSend = new Label();
            lblSend.SetBounds(16, 37, 110, 22);
            lblSend.Text = "payload:";
            lblSend.ForeColor = UiTheme.TextSub;
            lblSend.TextAlign = ContentAlignment.MiddleLeft;

            _txtPayload = new TextBox();
            _txtPayload.SetBounds(126, 34, 320, 24);
            _txtPayload.Text = "A1 B2 00 00 00 00 00 00 00 00 00 00 00 00 00";
            _txtPayload.Font = new Font("Consolas", 9.5f);
            _txtPayload.BorderStyle = BorderStyle.None;
            _txtPayload.BackColor = UiTheme.InputBg;
            _txtPayload.ForeColor = UiTheme.TextMain;
            _txtPayload.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

            _btnSend = new GradientButton();
            _btnSend.SetBounds(460, 32, 84, 28);
            _btnSend.Text = "发送";
            _btnSend.Anchor = AnchorStyles.Top | AnchorStyles.Right;   // 贴卡片右边，窗口拉宽时跟随
            _btnSend.Click += OnSend;

            cardSend.Controls.AddRange(new Control[] { lblSend, _txtPayload, _btnSend });

            // 接收
            var cardRx = new RoundedPanel();
            cardRx.SetBounds(12, 182, CardDesignWidth, 66);
            cardRx.Anchor = AnchorStyles.Top;

            cardRx.Controls.Add(MakeCardTitle("接收（Input Report 0x08，15 字节）"));

            _lblRx = new Label();
            _lblRx.SetBounds(16, 32, 725, 22);
            _lblRx.Font = new Font("Consolas", 9.5f);
            _lblRx.ForeColor = UiTheme.TextMain;
            _lblRx.Text = "（连接后自动监听）";
            _lblRx.TextAlign = ContentAlignment.MiddleLeft;
            _lblRx.AutoSize = false;
            _lblRx.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

            cardRx.Controls.Add(_lblRx);

            // 日志
            var cardLog = new RoundedPanel();
            cardLog.SetBounds(12, 254, CardDesignWidth, 334);
            cardLog.Anchor = AnchorStyles.Top | AnchorStyles.Bottom;   // 只做垂直伸缩，宽度同理由 LayoutContent 管

            cardLog.Controls.Add(MakeCardTitle("日志"));

            _txtLog = new RichTextBox();
            _txtLog.SetBounds(16, 32, 726, 266);
            _txtLog.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            _txtLog.ReadOnly = true;
            _txtLog.BorderStyle = BorderStyle.None;
            _txtLog.BackColor = UiTheme.InputBg;
            _txtLog.ForeColor = UiTheme.TextMain;
            _txtLog.Font = new Font("Consolas", 9f);
            _txtLog.DetectUrls = false;

            _btnClear = new GradientButton();
            _btnClear.SetBounds(16, 302, 84, 26);
            _btnClear.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
            _btnClear.Text = "清空日志";
            _btnClear.IsPrimary = false;
            _btnClear.Click += delegate { _txtLog.Clear(); };

            cardLog.Controls.Add(_txtLog);
            cardLog.Controls.Add(_btnClear);

            Controls.AddRange(new Control[] { cardDevice, cardSend, cardRx, cardLog });
            _contentCards.AddRange(new Control[] { cardDevice, cardSend, cardRx, cardLog });
            LayoutContent();   // 初始定位（默认窗口尺寸）
        }

        /// <summary>
        /// 内容列自适应：卡片铺满可用宽度（最大化时整体放大，不再保持 756 设计宽度并居中留白）；
        /// 窗口过窄时按最小宽度 240px 兜底。
        /// 卡片不设左右锚定（锚定会强制拉伸），位置/宽度由此统一管理；
        /// 卡片内部控件仍靠各自锚点跟随卡片宽度。
        /// </summary>
        private void LayoutContent()
        {
            if (_contentCards.Count == 0)
                return;
            const int margin = 12;
            int avail = ClientSize.Width - margin * 2;
            if (avail < 240)
                avail = 240;
            int w = avail;                 // 铺满：最大化时整体放大（与翻译页一致）
            int x = margin;
            foreach (var card in _contentCards)
                card.SetBounds(x, card.Top, w, card.Height);
        }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            LayoutContent();
        }

        private static Label MakeCardTitle(string text)
        {
            var lbl = new Label();
            lbl.Text = text;
            lbl.SetBounds(14, 8, 728, 20);
            lbl.Font = UiTheme.FontTitle;
            lbl.ForeColor = UiTheme.TextMain;
            lbl.TextAlign = ContentAlignment.MiddleLeft;
            lbl.AutoSize = false;   // 关掉自动宽度：长标题否则会溢出卡片被裁切
            lbl.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            return lbl;
        }

        private void UpdateConnUi(bool connected)
        {
            _btnConnect.Text = connected ? "断开" : "连接";
            _lblStatus.Text = connected ? "● 已连接" : "○ 未连接";
            _lblStatus.ForeColor = connected ? UiTheme.Success : UiTheme.Danger;
            _lblPath.Text = connected ? _link.GetOpenedPath() : "（未检测到 dongle —— 请插入后点击【连接】）";
            _btnSend.Enabled = connected;
            if (!connected)
                _lblRx.Text = "（尚未连接）";
        }

        // ---------- 事件处理 ----------

        private void OnConnect(object sender, EventArgs e)
        {
            if (_link.IsConnected)
            {
                StopReader();
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
                StartReader();
                LogLine("已连接并开始监听 Input Report: " + _link.GetOpenedPath(), UiTheme.Success);
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

        private void ResetConnectionAfterError()
        {
            StopReader();
            try
            {
                _link.Disconnect();
            }
            catch (Exception)
            {
            }
            UpdateConnUi(false);
        }

        private void OnSend(object sender, EventArgs e)
        {
            byte[] payload;
            string parseErr;
            if (!TryParsePayload(_txtPayload.Text, out payload, out parseErr))
            {
                LogLine("发送失败: " + parseErr, UiTheme.Danger);
                return;
            }

            string err = _link.Send(payload);
            if (err != null)
            {
                LogLine("TX 失败: " + err, UiTheme.Danger);
                return;
            }
            LogLine("TX  09 " + Hex(payload), UiTheme.Primary);
        }

        private void StartReader()
        {
            _readerRunning = true;
            _readerThread = new Thread(ReaderLoop);
            _readerThread.IsBackground = true;
            _readerThread.Start();
        }

        private void StopReader()
        {
            Thread reader = _readerThread;
            _readerRunning = false;
            _readerThread = null;
            if (reader != null && reader != Thread.CurrentThread)
                reader.Join(500);
        }

        private void ReaderLoop()
        {
            Thread reader = Thread.CurrentThread;
            while (_readerRunning)
            {
                byte[] rx;
                string err;
                int result = _link.TryReadTimeout(200, out rx, out err);
                if (result == 0)
                    continue;
                if (result < 0)
                {
                    _readerRunning = false;
                    if (IsHandleCreated && !IsDisposed)
                        BeginInvoke(new Action(delegate { HandleReaderDisconnect(reader, err); }));
                    return;
                }

                if (_readerRunning && IsHandleCreated && !IsDisposed)
                {
                    BeginInvoke(new Action(delegate
                    {
                        if (!_readerRunning)
                            return;
                        ShowRx(rx);
                        LogLine("RX  08 " + Hex(rx), UiTheme.Rx);
                    }));
                }
            }
        }

        private void HandleReaderDisconnect(Thread reader, string err)
        {
            if (_readerThread != reader || !_link.IsConnected)
                return;

            StopReader();
            _link.Disconnect();
            UpdateConnUi(false);
            LogLine("USB Dongle 已断开: " + err, UiTheme.Danger);
        }

        // ---------- 工具方法 ----------

        private void ShowRx(byte[] inputPayload)
        {
            _lblRx.Text = "08 " + Hex(inputPayload);
        }

        private static string Hex(byte[] data)
        {
            return BitConverter.ToString(data).Replace("-", " ");
        }

        private static bool TryParsePayload(string text, out byte[] payload, out string err)
        {
            payload = new byte[15];
            err = null;

            string cleaned = (text ?? string.Empty)
                .Replace(",", " ").Replace(";", " ")
                .Replace("0x", " ").Replace("0X", " ")
                .Replace("-", " ");
            string[] parts = cleaned.Split(
                new char[] { ' ', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);

            if (parts.Length == 0)
            {
                err = "请输入至少 1 字节的 payload";
                return false;
            }
            if (parts.Length > 15)
            {
                err = "payload 超过 15 字节（0x09 通道仅支持 15 字节）";
                return false;
            }

            for (int i = 0; i < parts.Length; i++)
            {
                byte b;
                if (!byte.TryParse(parts[i], NumberStyles.AllowHexSpecifier,
                                   CultureInfo.InvariantCulture, out b))
                {
                    err = "无法解析 \"" + parts[i] + "\"（需要 hex 字节，如 A1）";
                    return false;
                }
                payload[i] = b;
            }
            return true;
        }

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
    }
}
