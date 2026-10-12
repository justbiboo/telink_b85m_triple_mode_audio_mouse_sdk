using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Globalization;
using System.Threading;
using System.Windows.Forms;

namespace BibooUxTool
{
    /// <summary>
    /// dongle 收发调试主界面（浅色蓝紫主题）：
    ///   连接 / 发送 15 字节 Feature payload / 接收 15 字节 Input payload / 收发日志。
    /// </summary>
    public sealed class MainForm : Form
    {
        private readonly DongleLink _link = new DongleLink();
        private volatile Thread _readerThread;
        private volatile bool _readerRunning;

        private GradientButton _btnConnect;
        private GradientButton _btnFactory;
        private Label _lblStatus;
        private Label _lblPath;
        private TextBox _txtPayload;
        private GradientButton _btnSend;
        private Label _lblRx;
        private RichTextBox _txtLog;
        private GradientButton _btnClear;

        public MainForm()
        {
            BuildUi();
            UpdateConnUi(false);
            LogLine("Biboo UX 收发调试工具就绪（下行 0x09 Feature：15 字节，上行 0x08 Input：15 字节）。", UiTheme.TextSub);
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
            Text = "Biboo UX 上位机 - Dongle 收发调试";
            BackColor = UiTheme.FormBg;
            Font = UiTheme.FontBody;
            ClientSize = new Size(780, 600);
            MinimumSize = new Size(660, 520);
            StartPosition = FormStartPosition.CenterScreen;

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
            cardDevice.SetBounds(12, 12, 756, 78);
            cardDevice.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

            cardDevice.Controls.Add(MakeCardTitle("设备"));

            _btnConnect = new GradientButton();
            _btnConnect.SetBounds(16, 34, 92, 30);
            _btnConnect.Click += OnConnect;

            _lblStatus = new Label();
            _lblStatus.SetBounds(122, 38, 100, 22);
            _lblStatus.TextAlign = ContentAlignment.MiddleLeft;

            _lblPath = new Label();
            _lblPath.SetBounds(232, 38, 412, 22);
            _lblPath.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _lblPath.TextAlign = ContentAlignment.MiddleLeft;
            _lblPath.AutoEllipsis = true;
            _lblPath.ForeColor = UiTheme.TextSub;

            _btnFactory = new GradientButton();
            _btnFactory.SetBounds(656, 34, 84, 30);
            _btnFactory.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _btnFactory.Text = "产测页";
            _btnFactory.IsPrimary = false;
            _btnFactory.Click += OnOpenFactory;

            cardDevice.Controls.AddRange(new Control[] { _btnConnect, _lblStatus, _lblPath, _btnFactory });

            // 发送
            var cardSend = new RoundedPanel();
            cardSend.SetBounds(12, 96, 756, 80);
            cardSend.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

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

            _btnSend = new GradientButton();
            _btnSend.SetBounds(460, 32, 84, 28);
            _btnSend.Text = "发送";
            _btnSend.Click += OnSend;

            cardSend.Controls.AddRange(new Control[] { lblSend, _txtPayload, _btnSend });

            // 接收
            var cardRx = new RoundedPanel();
            cardRx.SetBounds(12, 182, 756, 66);
            cardRx.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

            cardRx.Controls.Add(MakeCardTitle("接收（Input Report 0x08，15 字节）"));

            _lblRx = new Label();
            _lblRx.SetBounds(16, 32, 725, 22);
            _lblRx.Font = new Font("Consolas", 9.5f);
            _lblRx.ForeColor = UiTheme.TextMain;
            _lblRx.Text = "（连接后自动监听）";
            _lblRx.TextAlign = ContentAlignment.MiddleLeft;

            cardRx.Controls.Add(_lblRx);

            // 日志
            var cardLog = new RoundedPanel();
            cardLog.SetBounds(12, 254, 756, 334);
            cardLog.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;

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
        }

        private static Label MakeCardTitle(string text)
        {
            var lbl = new Label();
            lbl.Text = text;
            lbl.SetBounds(14, 8, 500, 20);
            lbl.Font = UiTheme.FontTitle;
            lbl.ForeColor = UiTheme.TextMain;
            lbl.TextAlign = ContentAlignment.MiddleLeft;
            lbl.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            return lbl;
        }

        private void UpdateConnUi(bool connected)
        {
            _btnConnect.Text = connected ? "断开" : "连接";
            _lblStatus.Text = connected ? "● 已连接" : "○ 未连接";
            _lblStatus.ForeColor = connected ? UiTheme.Success : UiTheme.Danger;
            _lblPath.Text = connected ? _link.GetOpenedPath() : string.Empty;
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

        private void OnOpenFactory(object sender, EventArgs e)
        {
            if (_link.IsConnected)
            {
                // 产测页自持独立 DongleLink：同一 HID 通道需独占，先停读线程并断开。
                StopReader();
                _link.Disconnect();
                UpdateConnUi(false);
                LogLine("已断开（进入产测页需独占 HID 通道）。", UiTheme.TextSub);
            }

            using (var ft = new FactoryTestForm())
            {
                ft.ShowDialog(this);
            }

            LogLine("已从产测页返回。", UiTheme.TextSub);
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

    /// <summary>
    /// 浅色蓝紫主题配色与字体规范。
    /// </summary>
    internal static class UiTheme
    {
        public static readonly Color FormBg    = Color.FromArgb(0xF4, 0xF7, 0xFE); // 窗体背景：浅蓝白
        public static readonly Color CardBorder = Color.FromArgb(0xE3, 0xE9, 0xF5); // 卡片描边
        public static readonly Color Primary   = Color.FromArgb(0x4A, 0x7C, 0xFF); // 主色（渐变左 / 描边 / 文字）
        public static readonly Color Primary2  = Color.FromArgb(0x7B, 0x5C, 0xFF); // 渐变右
        public static readonly Color PrimaryHover  = Color.FromArgb(0x60, 0x8E, 0xFF); // Hover 渐变左
        public static readonly Color PrimaryHover2 = Color.FromArgb(0x91, 0x7A, 0xFF); // Hover 渐变右
        public static readonly Color TextMain  = Color.FromArgb(0x1F, 0x2D, 0x3D); // 主文字
        public static readonly Color TextSub   = Color.FromArgb(0x7A, 0x86, 0x9A); // 次要说明
        public static readonly Color Success   = Color.FromArgb(0x34, 0xC7, 0x7B); // 成功状态
        public static readonly Color Danger    = Color.FromArgb(0xF5, 0x6C, 0x6C); // 错误状态
        public static readonly Color InputBg   = Color.FromArgb(0xFB, 0xFC, 0xFE); // 输入框 / 日志框底色
        public static readonly Color Timestamp = Color.FromArgb(0x9A, 0xA7, 0xBD); // 日志时间戳
        public static readonly Color Rx        = Color.FromArgb(0x00, 0xA8, 0xA8); // 接收日志

        public static readonly Font FontBody  = new Font("Microsoft YaHei UI", 9F);
        public static readonly Font FontTitle = new Font("Microsoft YaHei UI", 9F, FontStyle.Bold);
    }

    /// <summary>
    /// 圆角白底卡片：白底 + 浅蓝描边，替代默认 GroupBox 的硬边框。
    /// </summary>
    internal sealed class RoundedPanel : Panel
    {
        public int Radius { get; set; }
        public Color BorderColor { get; set; }

        public RoundedPanel()
        {
            Radius = 10;
            BorderColor = UiTheme.CardBorder;
            BackColor = Color.White;
            DoubleBuffered = true;
        }

        protected override void OnPaintBackground(PaintEventArgs e)
        {
            // 先按父容器背景色铺满整个矩形：圆角路径外的像素若保持窗口初始
            // 内容会显示为黑点，描边抗锯齿与黑色混合后更明显。
            Color parentBg = Parent != null ? Parent.BackColor : UiTheme.FormBg;
            using (var brush = new SolidBrush(parentBg))
            {
                e.Graphics.FillRectangle(brush, ClientRectangle);
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;

            using (var path = RoundedRectPath(ClientRectangle, Radius))
            using (var fill = new SolidBrush(BackColor))
            {
                g.FillPath(fill, path);
            }

            var borderRect = ClientRectangle;
            borderRect.Inflate(-1, -1);
            using (var path = RoundedRectPath(borderRect, Radius))
            using (var pen = new Pen(BorderColor))
            {
                g.DrawPath(pen, path);
            }

            base.OnPaint(e);
        }

        internal static GraphicsPath RoundedRectPath(Rectangle rect, int radius)
        {
            int r = radius * 2;
            if (r > rect.Width) r = rect.Width;
            if (r > rect.Height) r = rect.Height;
            if (r < 2) r = 2;

            var path = new GraphicsPath();
            path.AddArc(rect.X, rect.Y, r, r, 180, 90);
            path.AddArc(rect.Right - r, rect.Y, r, r, 270, 90);
            path.AddArc(rect.Right - r, rect.Bottom - r, r, r, 0, 90);
            path.AddArc(rect.X, rect.Bottom - r, r, r, 90, 90);
            path.CloseFigure();
            return path;
        }
    }

    /// <summary>
    /// 圆角渐变按钮：主按钮为蓝紫渐变白字；IsPrimary=false 时为白底蓝字描边（次要操作）。
    /// </summary>
    internal sealed class GradientButton : Button
    {
        private bool _hover;

        public bool IsPrimary { get; set; }
        public int Radius { get; set; }

        public GradientButton()
        {
            IsPrimary = true;
            Radius = 8;
            Font = UiTheme.FontBody;
            Cursor = Cursors.Hand;
            TabStop = true;

            // 完全扁平：阻止原生 BUTTON 窗口类绘制默认边框 / 主题背景，
            // 否则其主题位图的黑色角像素会从自绘圆角外透出形成黑点。
            FlatStyle = FlatStyle.Flat;
            FlatAppearance.BorderSize = 0;
            FlatAppearance.BorderColor = Color.FromArgb(0, 255, 255, 255);

            SetStyle(ControlStyles.UserPaint |
                     ControlStyles.AllPaintingInWmPaint |
                     ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.ResizeRedraw |
                     ControlStyles.Opaque, true);

            MouseEnter += delegate { _hover = true; Invalidate(); };
            MouseLeave += delegate { _hover = false; Invalidate(); };
        }

        protected override CreateParams CreateParams
        {
            get
            {
                // 不使用原生 BUTTON 窗口类（改为 WinForms 通用窗口），
                // 杜绝系统在焦点变化等时机绘制原生按钮边框。
                CreateParams cp = base.CreateParams;
                cp.ClassName = null;
                return cp;
            }
        }

        protected override void OnEnabledChanged(EventArgs e)
        {
            base.OnEnabledChanged(e);
            Invalidate();
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            // 窗口创建/首次显示时系统原生绘制可能留下角部残留，
            // 句柄就绪后补一次重绘将其抹掉。
            BeginInvoke(new MethodInvoker(Invalidate));
        }

        protected override void OnPaintBackground(PaintEventArgs e)
        {
            // Opaque 样式：系统不再擦除背景，整个客户区由 OnPaint 负责绘制，
            // 避免 UserPaint 管线下 OnPaintBackground 不生效导致的圆角黑点。
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;

            // 1) 先用父容器背景色彻底铺满客户区（Clear 不受抗锯齿影响，
            //    不会与缓冲位图初始黑色混合产生暗边 / 角部黑点）。
            Color parentBg = Parent != null ? Parent.BackColor : Color.White;
            g.Clear(parentBg);

            // 2) 圆角主体绘制需要平滑边缘，此处再开启抗锯齿。
            g.SmoothingMode = SmoothingMode.AntiAlias;

            var rect = new Rectangle(0, 0, Width - 1, Height - 1);
            Color textColor;

            using (var path = RoundedPanel.RoundedRectPath(rect, Radius))
            {
                if (IsPrimary)
                {
                    Color c1, c2;
                    if (!Enabled)
                    {
                        c1 = Color.FromArgb(0xD9, 0xE1, 0xF2);
                        c2 = Color.FromArgb(0xD9, 0xE1, 0xF2);
                        textColor = Color.FromArgb(0xA6, 0xB1, 0xC6);
                    }
                    else
                    {
                        c1 = _hover ? UiTheme.PrimaryHover : UiTheme.Primary;
                        c2 = _hover ? UiTheme.PrimaryHover2 : UiTheme.Primary2;
                        textColor = Color.White;
                    }

                    using (var brush = new LinearGradientBrush(rect, c1, c2, LinearGradientMode.Horizontal))
                    {
                        g.FillPath(brush, path);
                    }
                }
                else
                {
                    Color fill = Enabled ? Color.White : Color.FromArgb(0xF2, 0xF4, 0xFA);
                    Color border = Enabled ? UiTheme.Primary : UiTheme.CardBorder;
                    textColor = Enabled ? UiTheme.Primary : UiTheme.TextSub;

                    using (var brush = new SolidBrush(fill))
                    {
                        g.FillPath(brush, path);
                    }
                    using (var pen = new Pen(border))
                    {
                        g.DrawPath(pen, path);
                    }
                }
            }

            TextRenderer.DrawText(g, Text ?? string.Empty, Font, ClientRectangle, textColor,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter |
                TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
        }
    }
}
