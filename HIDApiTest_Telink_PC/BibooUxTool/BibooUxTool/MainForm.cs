using System;
using System.Drawing;
using System.Globalization;
using System.Windows.Forms;

namespace BibooUxTool
{
    /// <summary>
    /// dongle 收发调试主界面：
    ///   连接（枚举+探测打开 col04） / 发送 7 字节 payload / 回读与轮询 / 收发日志。
    /// 固件当前 0x05 通道为回显：GET 返回最近一次 SET 的内容。
    /// </summary>
    public sealed class MainForm : Form
    {
        private readonly DongleLink _link = new DongleLink();
        private readonly Timer _pollTimer = new Timer();
        private readonly byte[] _lastRx = new byte[7];

        private Button _btnConnect;
        private Label _lblStatus;
        private Label _lblPath;
        private TextBox _txtPayload;
        private Button _btnSend;
        private CheckBox _chkEcho;
        private Button _btnReadNow;
        private Label _lblRx;
        private CheckBox _chkPoll;
        private NumericUpDown _numInterval;
        private RichTextBox _txtLog;
        private Button _btnClear;

        public MainForm()
        {
            BuildUi();
            UpdateConnUi(false);
            LogLine("Biboo UX 收发调试工具就绪（dongle 0x248A:0xCA10，report 0x05，payload 7 字节）。", Color.DimGray);
            LogLine("请插入 dongle 后点击【连接】。", Color.DimGray);
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            _pollTimer.Stop();
            _link.Dispose();
            base.OnFormClosed(e);
        }

        // ---------- UI 构建 ----------

        private void BuildUi()
        {
            Text = "Biboo UX 上位机 - Dongle 收发调试";
            ClientSize = new Size(780, 600);
            MinimumSize = new Size(660, 520);
            StartPosition = FormStartPosition.CenterScreen;

            // 设备连接
            var grpDevice = new GroupBox();
            grpDevice.Text = "设备";
            grpDevice.SetBounds(12, 12, 756, 78);
            grpDevice.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

            _btnConnect = new Button();
            _btnConnect.SetBounds(15, 28, 92, 30);
            _btnConnect.Click += OnConnect;

            _lblStatus = new Label();
            _lblStatus.SetBounds(120, 35, 100, 22);
            _lblStatus.TextAlign = ContentAlignment.MiddleLeft;

            _lblPath = new Label();
            _lblPath.SetBounds(226, 35, 515, 22);
            _lblPath.TextAlign = ContentAlignment.MiddleLeft;
            _lblPath.AutoEllipsis = true;
            _lblPath.ForeColor = Color.DimGray;

            grpDevice.Controls.AddRange(new Control[] { _btnConnect, _lblStatus, _lblPath });

            // 发送
            var grpSend = new GroupBox();
            grpSend.Text = "发送 payload（hex，≤7 字节，不足补 0）";
            grpSend.SetBounds(12, 96, 756, 80);
            grpSend.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

            var lblSend = new Label();
            lblSend.SetBounds(15, 33, 110, 22);
            lblSend.Text = "payload:";
            lblSend.TextAlign = ContentAlignment.MiddleLeft;

            _txtPayload = new TextBox();
            _txtPayload.SetBounds(125, 30, 320, 24);
            _txtPayload.Text = "A1 B2 00 00 00 00 00";
            _txtPayload.Font = new Font("Consolas", 9.5f);

            _btnSend = new Button();
            _btnSend.SetBounds(460, 28, 80, 28);
            _btnSend.Text = "发送";
            _btnSend.Click += OnSend;

            _chkEcho = new CheckBox();
            _chkEcho.SetBounds(555, 31, 150, 22);
            _chkEcho.Text = "发送后自动回读";
            _chkEcho.Checked = true;

            grpSend.Controls.AddRange(new Control[] { lblSend, _txtPayload, _btnSend, _chkEcho });

            // 接收
            var grpRx = new GroupBox();
            grpRx.Text = "接收（GET_REPORT 0x05）";
            grpRx.SetBounds(12, 182, 756, 66);
            grpRx.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

            _btnReadNow = new Button();
            _btnReadNow.SetBounds(15, 24, 92, 28);
            _btnReadNow.Text = "立即读取";
            _btnReadNow.Click += OnReadNow;

            _lblRx = new Label();
            _lblRx.SetBounds(120, 28, 620, 22);
            _lblRx.Font = new Font("Consolas", 9.5f);
            _lblRx.Text = "（尚未读取）";
            _lblRx.TextAlign = ContentAlignment.MiddleLeft;

            grpRx.Controls.AddRange(new Control[] { _btnReadNow, _lblRx });

            // 轮询
            var grpPoll = new GroupBox();
            grpPoll.Text = "轮询";
            grpPoll.SetBounds(12, 254, 756, 58);
            grpPoll.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

            _chkPoll = new CheckBox();
            _chkPoll.SetBounds(15, 22, 80, 22);
            _chkPoll.Text = "轮询";
            _chkPoll.CheckedChanged += OnPollToggle;

            _numInterval = new NumericUpDown();
            _numInterval.SetBounds(100, 21, 80, 24);
            _numInterval.Minimum = 100;
            _numInterval.Maximum = 5000;
            _numInterval.Increment = 100;
            _numInterval.Value = 500;
            _numInterval.ValueChanged += OnIntervalChanged;

            var lblPollTip = new Label();
            lblPollTip.SetBounds(190, 25, 550, 22);
            lblPollTip.ForeColor = Color.DimGray;
            lblPollTip.Text = "ms；轮询只发 GET 不发 SET，内容变化时才记日志";
            lblPollTip.TextAlign = ContentAlignment.MiddleLeft;

            grpPoll.Controls.AddRange(new Control[] { _chkPoll, _numInterval, lblPollTip });

            // 日志
            var grpLog = new GroupBox();
            grpLog.Text = "日志";
            grpLog.SetBounds(12, 318, 756, 270);
            grpLog.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;

            _txtLog = new RichTextBox();
            _txtLog.SetBounds(15, 22, 726, 214);
            _txtLog.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            _txtLog.ReadOnly = true;
            _txtLog.BackColor = Color.White;
            _txtLog.Font = new Font("Consolas", 9f);
            _txtLog.DetectUrls = false;

            _btnClear = new Button();
            _btnClear.SetBounds(15, 240, 80, 24);
            _btnClear.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
            _btnClear.Text = "清空日志";
            _btnClear.Click += delegate { _txtLog.Clear(); };

            grpLog.Controls.Add(_txtLog);
            grpLog.Controls.Add(_btnClear);

            Controls.AddRange(new Control[] { grpDevice, grpSend, grpRx, grpPoll, grpLog });

            _pollTimer.Tick += OnPollTick;
        }

        private void UpdateConnUi(bool connected)
        {
            _btnConnect.Text = connected ? "断开" : "连接";
            _lblStatus.Text = connected ? "● 已连接" : "○ 未连接";
            _lblStatus.ForeColor = connected ? Color.ForestGreen : Color.Firebrick;
            _lblPath.Text = connected ? _link.GetOpenedPath() : string.Empty;
            _btnSend.Enabled = connected;
            _btnReadNow.Enabled = connected;
            if (!connected)
            {
                _pollTimer.Stop();
                _chkPoll.Checked = false;
                _lblRx.Text = "（尚未读取）";
            }
        }

        // ---------- 事件处理 ----------

        private void OnConnect(object sender, EventArgs e)
        {
            if (_link.IsConnected)
            {
                _pollTimer.Stop();
                _chkPoll.Checked = false;
                _link.Disconnect();
                UpdateConnUi(false);
                LogLine("已断开。", Color.DimGray);
                return;
            }

            LogLine("正在枚举并打开 dongle（col04 优先，GET 0x05 探测）...", Color.DimGray);
            string err;
            try
            {
                err = _link.Connect();
            }
            catch (DllNotFoundException)
            {
                LogLine("找不到 BibooHid.dll —— 请先在 VS 中编译 BibooHid 工程（x64）。", Color.Red);
                return;
            }
            catch (BadImageFormatException)
            {
                LogLine("BibooHid.dll 架构不匹配 —— DLL 与程序都必须是 x64。", Color.Red);
                return;
            }

            if (err != null)
            {
                LogLine("连接失败: " + err, Color.Red);
                UpdateConnUi(false);
                return;
            }

            UpdateConnUi(true);
            LogLine("已连接: " + _link.GetOpenedPath(), Color.DarkGreen);
        }

        private void OnSend(object sender, EventArgs e)
        {
            byte[] payload;
            string parseErr;
            if (!TryParsePayload(_txtPayload.Text, out payload, out parseErr))
            {
                LogLine("发送失败: " + parseErr, Color.Red);
                return;
            }

            string err = _link.Send(payload);
            if (err != null)
            {
                LogLine("TX 失败: " + err, Color.Red);
                return;
            }
            LogLine("TX  05 " + Hex(payload), Color.DarkBlue);

            if (_chkEcho.Checked)
            {
                byte[] rx;
                string rerr;
                if (_link.TryRead(out rx, out rerr) != 0)
                {
                    LogLine("回读失败: " + rerr, Color.Red);
                    return;
                }
                ShowRx(rx);
                bool same = BytesEqual(rx, payload);
                LogLine("RX  05 " + Hex(rx) + (same ? "   (回显一致)" : "   (与发送不一致!)"),
                        same ? Color.DarkGreen : Color.DarkOrange);
            }
        }

        private void OnReadNow(object sender, EventArgs e)
        {
            byte[] rx;
            string err;
            if (_link.TryRead(out rx, out err) != 0)
            {
                LogLine("读取失败: " + err, Color.Red);
                return;
            }
            ShowRx(rx);
            LogLine("RX  05 " + Hex(rx), Color.DarkCyan);
        }

        private void OnPollToggle(object sender, EventArgs e)
        {
            _pollTimer.Stop();
            if (_chkPoll.Checked && _link.IsConnected)
            {
                _pollTimer.Interval = (int)_numInterval.Value;
                _pollTimer.Start();
            }
        }

        private void OnIntervalChanged(object sender, EventArgs e)
        {
            if (_pollTimer.Enabled)
                _pollTimer.Interval = (int)_numInterval.Value;
        }

        private void OnPollTick(object sender, EventArgs e)
        {
            if (!_link.IsConnected)
                return;

            byte[] rx;
            string err;
            if (_link.TryRead(out rx, out err) != 0)
            {
                LogLine("轮询读取失败: " + err, Color.Red);
                return;
            }
            ShowRx(rx);
            if (!BytesEqual(rx, _lastRx))
            {
                LogLine("RX(变化) 05 " + Hex(rx), Color.DarkCyan);
                Array.Copy(rx, _lastRx, 7);
            }
        }

        // ---------- 工具方法 ----------

        private void ShowRx(byte[] payload7)
        {
            _lblRx.Text = "05 " + Hex(payload7);
        }

        private static bool BytesEqual(byte[] a, byte[] b)
        {
            if (a == null || b == null || a.Length != b.Length)
                return false;
            for (int i = 0; i < a.Length; i++)
                if (a[i] != b[i])
                    return false;
            return true;
        }

        private static string Hex(byte[] data)
        {
            return BitConverter.ToString(data).Replace("-", " ");
        }

        private static bool TryParsePayload(string text, out byte[] payload, out string err)
        {
            payload = new byte[7];
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
            if (parts.Length > 7)
            {
                err = "payload 超过 7 字节（0x05 通道仅支持 7 字节）";
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
            _txtLog.SelectionColor = Color.DimGray;
            _txtLog.AppendText(DateTime.Now.ToString("HH:mm:ss.fff") + "  ");
            _txtLog.SelectionColor = color;
            _txtLog.AppendText(msg + Environment.NewLine);
            _txtLog.ScrollToCaret();
        }
    }
}
