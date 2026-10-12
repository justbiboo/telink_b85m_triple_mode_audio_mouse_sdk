using System;
using System.Drawing;
using System.Globalization;
using System.Threading;
using System.Windows.Forms;

namespace BibooUxTool
{
    /// <summary>
    /// dongle 收发调试主界面：
    ///   连接 / 发送 7 字节 payload / 后台监听 Input Report / 收发日志。
    /// </summary>
    public sealed class MainForm : Form
    {
        private readonly DongleLink _link = new DongleLink();
        private volatile Thread _readerThread;
        private volatile bool _readerRunning;

        private Button _btnConnect;
        private Label _lblStatus;
        private Label _lblPath;
        private TextBox _txtPayload;
        private Button _btnSend;
        private Label _lblRx;
        private RichTextBox _txtLog;
        private Button _btnClear;

        public MainForm()
        {
            BuildUi();
            UpdateConnUi(false);
            LogLine("Biboo UX 收发调试工具就绪（下行 0x09 Feature，上行 0x08 Input，payload 7 字节）。", Color.DimGray);
            LogLine("请插入 dongle 后点击【连接】。", Color.DimGray);
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

            grpSend.Controls.AddRange(new Control[] { lblSend, _txtPayload, _btnSend });

            var grpRx = new GroupBox();
            grpRx.Text = "接收（Input Report 0x08）";
            grpRx.SetBounds(12, 182, 756, 66);
            grpRx.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

            _lblRx = new Label();
            _lblRx.SetBounds(15, 28, 725, 22);
            _lblRx.Font = new Font("Consolas", 9.5f);
            _lblRx.Text = "（连接后自动监听）";
            _lblRx.TextAlign = ContentAlignment.MiddleLeft;

            grpRx.Controls.Add(_lblRx);

            var grpLog = new GroupBox();
            grpLog.Text = "日志";
            grpLog.SetBounds(12, 254, 756, 334);
            grpLog.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;

            _txtLog = new RichTextBox();
            _txtLog.SetBounds(15, 22, 726, 278);
            _txtLog.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            _txtLog.ReadOnly = true;
            _txtLog.BackColor = Color.White;
            _txtLog.Font = new Font("Consolas", 9f);
            _txtLog.DetectUrls = false;

            _btnClear = new Button();
            _btnClear.SetBounds(15, 304, 80, 24);
            _btnClear.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
            _btnClear.Text = "清空日志";
            _btnClear.Click += delegate { _txtLog.Clear(); };

            grpLog.Controls.Add(_txtLog);
            grpLog.Controls.Add(_btnClear);

            Controls.AddRange(new Control[] { grpDevice, grpSend, grpRx, grpLog });
        }

        private void UpdateConnUi(bool connected)
        {
            _btnConnect.Text = connected ? "断开" : "连接";
            _lblStatus.Text = connected ? "● 已连接" : "○ 未连接";
            _lblStatus.ForeColor = connected ? Color.ForestGreen : Color.Firebrick;
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
                LogLine("已断开。", Color.DimGray);
                return;
            }

            LogLine("正在枚举并打开独立 Host Link HID Interface...", Color.DimGray);
            try
            {
                string err = _link.Connect();
                if (err != null)
                {
                    LogLine("连接失败: " + err, Color.Red);
                    UpdateConnUi(false);
                    return;
                }

                UpdateConnUi(true);
                StartReader();
                LogLine("已连接并开始监听 Input Report: " + _link.GetOpenedPath(), Color.DarkGreen);
            }
            catch (DllNotFoundException)
            {
                ResetConnectionAfterError();
                LogLine("找不到 BibooHid.dll —— 请先重新生成整个 BibooUxTool.sln（x64）。", Color.Red);
            }
            catch (BadImageFormatException)
            {
                ResetConnectionAfterError();
                LogLine("BibooHid.dll 架构不匹配 —— DLL 与程序都必须是 x64。", Color.Red);
            }
            catch (EntryPointNotFoundException ex)
            {
                ResetConnectionAfterError();
                LogLine("BibooHid.dll 版本不匹配 —— 请重新生成整个 BibooUxTool.sln（x64）：" + ex.Message, Color.Red);
            }
            catch (Exception ex)
            {
                ResetConnectionAfterError();
                LogLine("连接异常（" + ex.GetType().Name + "）：" + ex.Message, Color.Red);
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
                LogLine("发送失败: " + parseErr, Color.Red);
                return;
            }

            string err = _link.Send(payload);
            if (err != null)
            {
                LogLine("TX 失败: " + err, Color.Red);
                return;
            }
            LogLine("TX  09 " + Hex(payload), Color.DarkBlue);
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
                        LogLine("RX  08 " + Hex(rx), Color.DarkCyan);
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
            LogLine("USB Dongle 已断开: " + err, Color.Red);
        }

        // ---------- 工具方法 ----------

        private void ShowRx(byte[] payload7)
        {
            _lblRx.Text = "08 " + Hex(payload7);
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
                err = "payload 超过 7 字节（0x09 通道仅支持 7 字节）";
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
