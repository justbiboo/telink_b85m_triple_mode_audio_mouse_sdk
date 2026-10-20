using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.Threading.Tasks;
using System.Windows.Forms;
using Windows.Devices.Bluetooth;

namespace BibooUxTool
{
    /// <summary>
    /// 蓝牙连接调试窗（Surface 0：PC ↔ 鼠标，不经 dongle）。
    /// 主流程（默认）：直接列出「系统已配对设备」（AEP watcher，免扫描）→ 实时连接状态 →
    /// 选中【连接】/双击回连 → 发现 SPP(0x33F3/0x33F4) → 收发 20B 帧 / dump GATT。
    /// 次流程（【扫描附近】）：广播扫描未配对设备 → 地址直连（不占 bonding 名额）。
    /// 与 DongleLink 收发调试页（DongleForm）平行，可作为独立通道使用。
    /// </summary>
    public sealed class BleScannerForm : Form
    {
        private readonly BleDirectLink _link = new BleDirectLink();
        private readonly Dictionary<ulong, BleDeviceInfo> _found = new Dictionary<ulong, BleDeviceInfo>();

        private GradientButton _btnScan;
        private TextBox _txtFilter;
        private CheckBox _chkHideUnnamed;
        private Label _lblStatus;
        private ListBox _lstDevices;
        private GradientButton _btnConnect;
        private GradientButton _btnDisconnect;
        private GradientButton _btnSysPair;
        private GradientButton _btnModePaired;
        private GradientButton _btnModeScan;
        private GradientButton _btnRefresh;
        private Label _lblPairedInfo;
        private TextBox _txtSlide;          // 滑动参数输入（12 字节 hex，3组×4B）
        private GradientButton _btnSlideEdit;   // 编辑参数：弹对话框可视化编辑，汇总成命令
        private GradientButton _btnSlideRead;   // 读参数（0x80）：读回并解析显示
        private GradientButton _btnSlideWrite;  // 写参数（0x79）：写入鼠标 flash
        private RichTextBox _txtLog;
        private GradientButton _btnClearLog;   // 清空日志

        /// <summary>参与「内容列居中」的卡片（窗口拉宽时限宽居中，见 LayoutContent）。</summary>
        private readonly List<Control> _contentCards = new List<Control>();

        /// <summary>内容列最大宽度：本页信息密度高（设备列表/日志），比卡片设计宽度 736 略放宽。</summary>
        private const int ContentMaxWidth = 1000;

        /// <summary>链路健康巡检：周期做一次 ATT 活性读，把「系统显示已连、实际已断」的假连接揪出来。</summary>
        private System.Windows.Forms.Timer _linkTimer;
        private bool _uiConnected;
        private int _verifyBusy;

        /// <summary>列表模式：true = 系统已配对设备（默认，免扫描主入口）；false = 广播扫描（未配对设备直连）。</summary>
        private bool _pairedMode = true;
        private readonly Dictionary<string, BleDeviceInfo> _pairedItems = new Dictionary<string, BleDeviceInfo>();

        public BleScannerForm()
        {
            BuildUi();
            ApplyListMode();
            _link.DeviceDiscovered += OnDeviceDiscovered;
            _link.SppReceived += OnSppReceived;
            _link.ConnectionChanged += OnConnectionChanged;
            _link.ScanCompleted += OnScanCompleted;
            _link.PairedDeviceChanged += OnPairedDeviceChanged;
            _link.PairedWatchStateChanged += OnPairedWatchState;

            _linkTimer = new System.Windows.Forms.Timer();
            _linkTimer.Interval = 2500;   // 每 2.5s 做一次 ATT 活性读（连续 2 次失败即判定断开）
            _linkTimer.Tick += OnLinkTimer;
            _linkTimer.Start();

            LogLine("蓝牙连接就绪：默认列出系统已配对设备（连接状态实时刷新），选中 →【连接】"
                  + "或双击条目即可回连并收发。未配对设备可切到【扫描附近】直连，"
                  + "或点【系统配对】从 Windows 蓝牙设置添加。「已连接」以 ATT 级真实读取为准。",
                UiTheme.TextSub);
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            // 句柄已创建后再启动 AEP watcher：事件回调在线程池线程，需要能 BeginInvoke 到 UI。
            if (_pairedMode)
            {
                _link.StartPairedWatch();
                LogLine("正在加载系统已配对设备列表…", UiTheme.TextSub);
            }
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            if (_linkTimer != null)
            {
                _linkTimer.Stop();
                _linkTimer.Tick -= OnLinkTimer;
                _linkTimer.Dispose();
                _linkTimer = null;
            }
            _link.DeviceDiscovered -= OnDeviceDiscovered;
            _link.SppReceived -= OnSppReceived;
            _link.ConnectionChanged -= OnConnectionChanged;
            _link.ScanCompleted -= OnScanCompleted;
            _link.PairedDeviceChanged -= OnPairedDeviceChanged;
            _link.PairedWatchStateChanged -= OnPairedWatchState;
            _link.StopPairedWatch();
            _link.Dispose();
            base.OnFormClosed(e);
        }

        /// <summary>周期活性校验：ATT 读通不过就认定「未连接」，不再看 WinRT 的 ConnectionStatus 脸色。</summary>
        private async void OnLinkTimer(object sender, EventArgs e)
        {
            if (IsDisposed || _verifyBusy != 0)
                return;
            _verifyBusy = 1;
            try
            {
                if (_link.IsLinkUp)
                    await _link.VerifyAsync(1200);

                bool up = _link.IsConnected;
                if (up == _uiConnected)
                    return;

                _uiConnected = up;
                UpdateConnUi(up);
                LogLine(up
                    ? "链路巡检：ATT 活性读通过 → 已连接。"
                    : "链路巡检：ATT 活性读失败 → 判定为「未连接」。"
                      + (string.IsNullOrEmpty(_link.LastDropReason) ? "" : "原因：" + _link.LastDropReason),
                    up ? UiTheme.Success : UiTheme.Danger);
            }
            catch (Exception ex)
            {
                LogLine("链路巡检异常（" + ex.GetType().Name + "）：" + ex.Message, UiTheme.Warn);
            }
            finally
            {
                _verifyBusy = 0;
            }
        }

        // ---------- UI 构建 ----------
        private void BuildUi()
        {
            Text = "Biboo OS - 蓝牙连接";
            BackColor = UiTheme.FormBg;
            Font = UiTheme.FontBody;
            // 布局基准：客户区 760×664。所有卡片左右锚定随窗口伸缩；右侧按钮列右锚定
            // 始终贴卡片右边；列表框四边锚定但右侧预留按钮列宽度（208px 右边距）。
            // 注意 WinForms z-order：先 Add 的控件在更上层，所以列表框必须最后 Add（沉底），
            // 否则窗口拉宽后拉伸的列表框会把右侧按钮列整列盖掉（历史 bug）。
            ClientSize = new Size(760, 664);
            MinimumSize = new Size(660, 700);
            StartPosition = FormStartPosition.CenterScreen;
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }

            // 扫描卡
            var cardScan = new RoundedPanel();
            cardScan.SetBounds(12, 12, 736, 96);
            // 不设左右锚定：宽度/水平位置由 LayoutContent 统一管理（限宽 + 居中），
            // 否则窗口一拉宽卡片就被拉伸成「左边内容、右边一大片空白」的长条。
            cardScan.Anchor = AnchorStyles.Top;
            cardScan.Controls.Add(MakeTitle("扫描（留空 = 列出全部设备；名称过滤按包含匹配、不区分大小写；勾选可隐藏无名称设备）"));

            _btnScan = new GradientButton();
            _btnScan.SetBounds(16, 34, 84, 30);
            _btnScan.Text = "扫描";
            _btnScan.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            _btnScan.Click += OnScan;

            _txtFilter = new TextBox();
            _txtFilter.SetBounds(110, 36, 140, 24);
            _txtFilter.Text = "";   // 默认不过滤：先确认能否收到广播
            _txtFilter.Font = new Font("Consolas", 9.5f);
            _txtFilter.BorderStyle = BorderStyle.None;
            _txtFilter.BackColor = UiTheme.InputBg;
            _txtFilter.ForeColor = UiTheme.TextMain;
            _txtFilter.Anchor = AnchorStyles.Top | AnchorStyles.Left;

            _chkHideUnnamed = new CheckBox();
            _chkHideUnnamed.SetBounds(260, 37, 116, 22);
            _chkHideUnnamed.Text = "隐藏无名称";
            _chkHideUnnamed.Checked = true;      // 默认过滤掉没有名称的广播
            _chkHideUnnamed.ForeColor = UiTheme.TextMain;
            _chkHideUnnamed.FlatStyle = FlatStyle.Standard;
            _chkHideUnnamed.CheckedChanged += OnHideUnnamedChanged;
            _chkHideUnnamed.Anchor = AnchorStyles.Top | AnchorStyles.Left;

            _lblStatus = new Label();
            _lblStatus.SetBounds(384, 38, 336, 22);
            _lblStatus.TextAlign = ContentAlignment.MiddleLeft;
            _lblStatus.ForeColor = UiTheme.TextSub;
            _lblStatus.AutoSize = false;   // 必须关：默认 AutoSize=true 会让文字撑宽标签、溢出卡片被裁切
            _lblStatus.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

            cardScan.Controls.AddRange(new Control[] { _btnScan, _txtFilter, _chkHideUnnamed, _lblStatus });

            // 设备列表 + 连接
            var cardDev = new RoundedPanel();
            cardDev.SetBounds(12, 116, 736, 268);
            cardDev.Anchor = AnchorStyles.Top;
            cardDev.Controls.Add(MakeTitle("设备"));

            // 列表模式切换：已配对设备（默认，免扫描）/ 扫描附近（未配对设备直连）
            _btnModePaired = new GradientButton();
            _btnModePaired.SetBounds(16, 32, 104, 26);
            _btnModePaired.Text = "已配对设备";
            _btnModePaired.IsPrimary = true;
            _btnModePaired.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            _btnModePaired.Click += OnModePaired;

            _btnModeScan = new GradientButton();
            _btnModeScan.SetBounds(124, 32, 104, 26);
            _btnModeScan.Text = "扫描附近";
            _btnModeScan.IsPrimary = false;
            _btnModeScan.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            _btnModeScan.Click += OnModeScan;

            _btnRefresh = new GradientButton();
            _btnRefresh.SetBounds(232, 32, 60, 26);
            _btnRefresh.Text = "刷新";
            _btnRefresh.IsPrimary = false;
            _btnRefresh.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            _btnRefresh.Click += OnRefreshPaired;

            _lblPairedInfo = new Label();
            _lblPairedInfo.SetBounds(300, 33, 420, 24);
            _lblPairedInfo.TextAlign = ContentAlignment.MiddleLeft;
            _lblPairedInfo.ForeColor = UiTheme.TextSub;
            _lblPairedInfo.AutoSize = false;
            _lblPairedInfo.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

            // 右侧按钮列：统一右锚定（距卡片右边 16px），窗口拉宽时整列跟随右移，
            // 不会再出现「按钮留在原地、中间空一大片」或「被列表框盖住」。
            _btnConnect = new GradientButton();
            _btnConnect.SetBounds(544, 66, 176, 30);
            _btnConnect.Text = "连接";
            _btnConnect.IsPrimary = false;
            _btnConnect.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _btnConnect.Click += OnConnect;

            _btnDisconnect = new GradientButton();
            _btnDisconnect.SetBounds(544, 104, 176, 30);
            _btnDisconnect.Text = "断开";
            _btnDisconnect.IsPrimary = false;
            _btnDisconnect.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _btnDisconnect.Click += OnDisconnect;

            _btnSysPair = new GradientButton();
            _btnSysPair.SetBounds(544, 142, 176, 30);
            _btnSysPair.Text = "系统配对";
            _btnSysPair.IsPrimary = false;
            _btnSysPair.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _btnSysPair.Click += OnSystemPair;
            var tipSysPair = new ToolTip();
            tipSysPair.SetToolTip(_btnSysPair,
                "一键打开 Windows「蓝牙和设备」设置页，在系统里配对/连接。\n"
                + "配对是一次性 bonding 动作，系统 UI 有完整进度与确认，最稳。\n"
                + "成功后回到本工具【扫描】→【连接】即可。");

            // 列表框四边锚定，右侧预留 208px（按钮列 176 + 间距 32）：
            // 锚定会同时锁定左右边距，因此任意窗口宽度下列表右沿永远碰不到按钮列。
            _lstDevices = new ListBox();
            _lstDevices.SetBounds(16, 66, 512, 180);
            _lstDevices.Font = new Font("Consolas", 9f);
            _lstDevices.BackColor = UiTheme.InputBg;
            _lstDevices.ForeColor = UiTheme.TextMain;
            _lstDevices.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            // 选中变化时刷新按钮可用态（连接要求先选中设备）
            _lstDevices.SelectedIndexChanged += delegate { UpdateConnUi(_uiConnected); };
            // 双击已配对条目 = 直接连接（主流程少点一次按钮）
            _lstDevices.DoubleClick += OnDeviceDoubleClick;

            // z-order：WinForms 里先 Add 的控件在更上层。列表框放最后（沉底），
            // 即使个别尺寸下与按钮列相交，按钮也永远盖在列表之上。
            cardDev.Controls.AddRange(new Control[] {
                _btnModePaired, _btnModeScan, _btnRefresh, _lblPairedInfo,
                _btnConnect, _btnDisconnect, _btnSysPair, _lstDevices });

            // SPP 卡片：滑动参数读写（0x79/0x82 写、0x80/0x81 读，iOS/Android 两组）
            var cardSpp = new RoundedPanel();
            cardSpp.SetBounds(12, 396, 736, 72);
            cardSpp.Anchor = AnchorStyles.Top;
            cardSpp.Controls.Add(MakeTitle("滑动参数（0x79/0x82 写、0x80/0x81 读）—— iOS/Android 两组，存鼠标 flash，重启保持"));

            _txtSlide = new TextBox();
            _txtSlide.SetBounds(16, 32, 380, 24);
            // 固件编译默认值（v2：每方向 [前进间隔/回位间隔/步长/步数]，两段间隔均 3ms、步长4）：
            //                 iOS 组 上下滑动30、左右滑动35(70/2)；Android 组 上下滑动30、左右滑动60(120/2)
            _txtSlide.Text = "03 00 03 00 04 1E 03 00 03 00 04 23 03 00 03 00 04 1E 03 00 03 00 04 3C";
            _txtSlide.Font = new Font("Consolas", 9.5f);
            _txtSlide.BorderStyle = BorderStyle.None;
            _txtSlide.BackColor = UiTheme.InputBg;
            _txtSlide.ForeColor = UiTheme.TextMain;
            _txtSlide.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            var tipSlide = new ToolTip();
            tipSlide.SetToolTip(_txtSlide,
                "24 字节 = iOS组12 + Android组12；每组 2方向×6B[前进lo,前进hi,回位lo,回位hi,步长,步数]（间隔小端u16，ms）\n"
                + "方向顺序：上下滑动 / 左右滑动；步长与步数为两段共用（前进=回位，光标必回原点）\n"
                + "写：先发 0x79 写 iOS 组、再发 0x82 写 Android 组；读：0x80/0x81 各读一组\n"
                + "约束：前进/回位间隔均1~100ms，步长1~127，步数1~255\n"
                + "注意：v2 协议，需配套固件使用（旧 flash 配置因魔数不符自动回默认值）");

            _btnSlideEdit = new GradientButton();
            _btnSlideEdit.SetBounds(408, 30, 100, 28);
            _btnSlideEdit.Text = "编辑参数…";
            _btnSlideEdit.IsPrimary = false;
            _btnSlideEdit.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _btnSlideEdit.Click += OnSlideEditParam;

            _btnSlideRead = new GradientButton();
            _btnSlideRead.SetBounds(516, 30, 92, 28);
            _btnSlideRead.Text = "读参数";
            _btnSlideRead.IsPrimary = false;
            _btnSlideRead.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _btnSlideRead.Click += OnSlideRead;

            _btnSlideWrite = new GradientButton();
            _btnSlideWrite.SetBounds(616, 30, 100, 28);
            _btnSlideWrite.Text = "写参数";
            _btnSlideWrite.IsPrimary = true;
            _btnSlideWrite.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _btnSlideWrite.Click += OnSlideWrite;

            cardSpp.Controls.AddRange(new Control[] {
                _txtSlide, _btnSlideEdit, _btnSlideRead, _btnSlideWrite });

            // 日志（唯一四边锚定的卡片：窗口拉高时它吸收全部多余高度）
            var cardLog = new RoundedPanel();
            cardLog.SetBounds(12, 480, 736, 172);
            cardLog.Anchor = AnchorStyles.Top | AnchorStyles.Bottom;   // 只做垂直伸缩，宽度同理由 LayoutContent 管

            // 清空日志：清空日志面板并重置 RX 帧计数（一键测试的统计基准随之清零）。
            // 位于日志卡片标题栏右侧；必须先于标题 Add（本工程 z-order 约定：先 Add 的在更上层），
            // 否则会被标题标签（14,8,708,20）整块盖住而不可见。
            _btnClearLog = new GradientButton();
            _btnClearLog.SetBounds(616, 6, 100, 22);
            _btnClearLog.Text = "清空日志";
            _btnClearLog.IsPrimary = false;
            _btnClearLog.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _btnClearLog.Click += OnClearLog;
            cardLog.Controls.Add(_btnClearLog);

            cardLog.Controls.Add(MakeTitle("日志 / 0x33F4 notify"));

            _txtLog = new RichTextBox();
            _txtLog.SetBounds(16, 32, 704, 80);
            _txtLog.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            _txtLog.ReadOnly = true;
            _txtLog.BorderStyle = BorderStyle.None;
            _txtLog.BackColor = UiTheme.InputBg;
            _txtLog.ForeColor = UiTheme.TextMain;
            _txtLog.Font = new Font("Consolas", 9f);
            _txtLog.DetectUrls = false;

            cardLog.Controls.Add(_txtLog);

            Controls.AddRange(new Control[] { cardScan, cardDev, cardSpp, cardLog });
            _contentCards.AddRange(new Control[] { cardScan, cardDev, cardSpp, cardLog });
            LayoutContent();   // 初始定位（默认窗口尺寸）
        }

        /// <summary>
        /// 内容列自适应：卡片铺满可用宽度（最大化时整体放大，不再限宽 1000 并两侧留白）；
        /// 窗口过窄时按最小宽度 240px 兜底。
        /// 卡片不设左右锚定（锚定会强制拉伸），位置/宽度由此统一管理；
        /// 卡片内部控件仍靠各自锚点跟随卡片宽度（右侧按钮列/预留边距都不受影响）。
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

        private static Label MakeTitle(string text)
        {
            var l = new Label();
            l.Text = text;
            l.SetBounds(14, 8, 708, 20);
            l.Font = UiTheme.FontTitle;
            l.ForeColor = UiTheme.TextMain;
            l.TextAlign = ContentAlignment.MiddleLeft;
            l.AutoSize = false;   // 关掉自动宽度：长标题否则会溢出卡片、被卡片/窗口裁切
            l.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            return l;
        }

        private void UpdateConnUi(bool connected)
        {
            _uiConnected = connected;

            if (!connected)
            {
                // 只要没通过 ATT 活性读，一律显示未连接 —— 宁可显示保守，也不给假连接。
                _lblStatus.Text = "○ 未连接";
                _lblStatus.ForeColor = UiTheme.Danger;
            }
            else
            {
                _lblStatus.Text = "● 已连接（GATT 链路活性校验通过）";
                _lblStatus.ForeColor = UiTheme.Success;
            }

            _btnSlideRead.Enabled = connected;
            _btnSlideWrite.Enabled = connected;
            _btnDisconnect.Enabled = connected;
            _btnSysPair.Enabled = true;
            // 连接按钮：未连接且列表有选中项时可用（OnConnect 开头会临时禁用防重入，
            // 结束后由本方法恢复——此前漏了这行，接入后按钮永久置灰）
            _btnConnect.Enabled = !connected && _lstDevices.SelectedItem != null;

            // 已配对列表：把「本工具已接入」标记打到对应行，并刷新统计。
            // 未连接时传 null 清掉标记——SoftRelease 保留设备对象，ConnectedDeviceId
            // 仍非空，照传会让已断开的行继续显示「已接入」。
            MarkAttached(connected ? _link.ConnectedDeviceId : null);
            UpdatePairedCount();
        }

        // ---------- 列表模式：已配对设备（默认） / 扫描附近 ----------
        /// <summary>按当前模式刷新控件可用态与清空列表（不负责启停 watcher）。</summary>
        private void ApplyListMode()
        {
            _btnModePaired.IsPrimary = _pairedMode;
            _btnModeScan.IsPrimary = !_pairedMode;
            _btnScan.Enabled = !_pairedMode;
            _txtFilter.Enabled = !_pairedMode;
            _chkHideUnnamed.Enabled = !_pairedMode;
            _btnRefresh.Enabled = _pairedMode;

            _lstDevices.Items.Clear();
            _found.Clear();
            _pairedItems.Clear();

            if (_pairedMode)
            {
                _lblStatus.Text = "";
                _lblPairedInfo.Text = "已配对设备列表（连接状态实时刷新，双击条目直接连接）";
            }
            else
            {
                _lblPairedInfo.Text = "扫描模式：10s 广播扫描，发现附近未配对设备";
            }
            UpdateConnUi(_uiConnected);
        }

        private void OnModePaired(object sender, EventArgs e)
        {
            if (_pairedMode)
                return;
            _pairedMode = true;
            ApplyListMode();
            _link.StartPairedWatch();
            LogLine("已切换到「已配对设备」：直接取系统已配对 BLE 设备，实时显示连接状态，"
                  + "选中后点【连接】（或双击条目）即可回连并收发，无需先扫描。", UiTheme.TextSub);
        }

        private void OnModeScan(object sender, EventArgs e)
        {
            if (!_pairedMode)
                return;
            _pairedMode = false;
            _link.StopPairedWatch();
            ApplyListMode();
            LogLine("已切换到「扫描附近」：用于直连未配对设备（不占鼠标 bonding 名额），"
                  + "点【扫描】开始 10s 广播扫描。", UiTheme.TextSub);
        }

        private void OnRefreshPaired(object sender, EventArgs e)
        {
            if (!_pairedMode)
                return;
            _lstDevices.Items.Clear();
            _found.Clear();
            _pairedItems.Clear();
            _link.RestartPairedWatch();
            LogLine("正在刷新已配对设备列表…", UiTheme.TextSub);
        }

        private void OnPairedDeviceChanged(BleDeviceInfo info, BleDirectLink.PairedChangeKind kind)
        {
            if (IsDisposed || !IsHandleCreated || info == null)
                return;
            BeginInvoke(new Action(() => UpsertPairedItem(info, kind)));
        }

        /// <summary>已配对列表增/改/删（按 DeviceId 去重，快照与 watcher 重复上报不会产生重复行）。</summary>
        private void UpsertPairedItem(BleDeviceInfo info, BleDirectLink.PairedChangeKind kind)
        {
            if (IsDisposed)
                return;

            BleDeviceInfo old;
            switch (kind)
            {
                case BleDirectLink.PairedChangeKind.Added:
                case BleDirectLink.PairedChangeKind.Updated:
                    if (_pairedItems.TryGetValue(info.DeviceId, out old))
                    {
                        // 同一条目重复上报（watcher + FindAll 快照）：替换对象、不重复建行
                        int idx = _lstDevices.Items.IndexOf(old);
                        if (idx >= 0)
                        {
                            bool wasSel = ReferenceEquals(_lstDevices.SelectedItem, old);
                            _lstDevices.Items.RemoveAt(idx);
                            _lstDevices.Items.Insert(idx, info);
                            if (wasSel)
                                _lstDevices.SelectedItem = info;   // 重排丢选中，恢复
                        }
                        _pairedItems[info.DeviceId] = info;
                    }
                    else
                    {
                        _pairedItems[info.DeviceId] = info;
                        _lstDevices.Items.Add(info);
                    }
                    break;
                case BleDirectLink.PairedChangeKind.Removed:
                    if (_pairedItems.TryGetValue(info.DeviceId, out old))
                    {
                        _lstDevices.Items.Remove(old);
                        _pairedItems.Remove(info.DeviceId);
                    }
                    break;
            }
            UpdatePairedCount();
        }

        private void OnPairedWatchState(string msg)
        {
            if (IsDisposed || !IsHandleCreated)
                return;
            BeginInvoke(new Action(() => LogLine(msg, UiTheme.Warn)));
        }

        /// <summary>双击已配对条目 = 直接连接（主流程少点一次按钮）。</summary>
        private void OnDeviceDoubleClick(object sender, EventArgs e)
        {
            if (!_pairedMode || _uiConnected)
                return;
            if (_lstDevices.SelectedItem != null)
                OnConnect(sender, e);
        }

        /// <summary>把「本工具已接入」标记打到对应行（连接/断开状态变化时调用）。</summary>
        private void MarkAttached(string deviceId)
        {
            // 先记录选中项：下方 RemoveAt+Insert 重排会使 ListBox 丢失选中，
            // 不恢复的话「连接」按钮因 SelectedItem==null 而永久置灰（断开即全灰）。
            var sel = _lstDevices.SelectedItem as BleDeviceInfo;
            string selId = sel != null ? sel.DeviceId : null;
            try
            {
                MarkAttachedInner(deviceId);
            }
            finally
            {
                if (selId != null)
                {
                    BleDeviceInfo cur;
                    if (_pairedItems.TryGetValue(selId, out cur))
                        _lstDevices.SelectedItem = cur;
                }
            }
        }

        private void MarkAttachedInner(string deviceId)
        {
            if (!_pairedMode || string.IsNullOrEmpty(deviceId))
            {
                if (_pairedMode)
                {
                    // 断开后清掉所有接入标记（系统连接态由 watcher 维护）
                    foreach (var kv in _pairedItems)
                    {
                        if (!kv.Value.Attached)
                            continue;
                        kv.Value.Attached = false;
                        int idx = _lstDevices.Items.IndexOf(kv.Value);
                        if (idx >= 0)
                        {
                            _lstDevices.Items.RemoveAt(idx);
                            _lstDevices.Items.Insert(idx, kv.Value);
                        }
                    }
                }
                return;
            }
            foreach (var kv in _pairedItems)
            {
                bool attached = kv.Key == deviceId;
                if (kv.Value.Attached == attached)
                    continue;
                kv.Value.Attached = attached;
                int i = _lstDevices.Items.IndexOf(kv.Value);
                if (i >= 0)
                {
                    _lstDevices.Items.RemoveAt(i);
                    _lstDevices.Items.Insert(i, kv.Value);
                }
            }
        }

        private void UpdatePairedCount()
        {
            if (!_pairedMode)
                return;
            int conn = 0;
            foreach (var kv in _pairedItems)
                if (kv.Value.Attached || kv.Value.SystemConnected)
                    conn++;
            _lblPairedInfo.Text = string.Format("共 {0} 个已配对设备 · {1} 个已连接（实时刷新，双击条目直接连接）",
                _pairedItems.Count, conn);
        }

        // ---------- 事件 ----------
        private void OnScan(object sender, EventArgs e)
        {
            _lstDevices.Items.Clear();
            _found.Clear();
            _link.StopScan();   // 支持连点重扫
            UpdateConnUi(_uiConnected);   // 选中被清空，刷新按钮可用态

            string filter = _txtFilter.Text.Trim();
            string extra = _chkHideUnnamed.Checked ? "，隐藏无名称" : "";
            LogLine(filter.Length == 0
                ? "扫描中（10s，不过滤" + extra + "）..."
                : "扫描中（10s，名称含 \"" + filter + "\"" + extra + "）...", UiTheme.TextSub);

            SetScanning(true);
            _link.StartScan(filter, 10000, _chkHideUnnamed.Checked);
        }

        private void OnHideUnnamedChanged(object sender, EventArgs e)
        {
            // 勾选时立刻清掉列表里已出现的「无名称」条目，不必等下一次扫描。
            if (_chkHideUnnamed.Checked)
                PurgeUnnamed();
        }

        /// <summary>移除列表中名称为空的条目（连同去重字典）。</summary>
        private void PurgeUnnamed()
        {
            var dead = new List<ulong>();
            foreach (var kv in _found)
            {
                if (string.IsNullOrWhiteSpace(kv.Value.Name))
                    dead.Add(kv.Key);
            }
            foreach (var mac in dead)
            {
                BleDeviceInfo info;
                if (_found.TryGetValue(mac, out info))
                {
                    _lstDevices.Items.Remove(info);
                    _found.Remove(mac);
                }
            }
            if (dead.Count > 0)
                LogLine("已从列表移除 " + dead.Count + " 个无名称设备。", UiTheme.TextSub);
        }

        private void OnScanCompleted(int rawCount, int matchedCount, int unnamedCount, string reason)
        {
            if (IsDisposed)
                return;
            BeginInvoke(new Action(() =>
            {
                SetScanning(false);
                UpdateConnUi(_link.IsConnected);   // 扫描结束恢复真实链路状态，别让进度文案把状态顶掉
                LogLine(string.Format("扫描结束（{0}）：收到广播 {1} 条，列出设备 {2} 个{3}。",
                    reason, rawCount, matchedCount,
                    unnamedCount > 0 ? "，隐藏无名称 " + unnamedCount + " 个" : ""), UiTheme.TextSub);

                if (rawCount == 0)
                {
                    LogLine("一条广播都没收到 → 依次排查：①Windows 蓝牙是否已打开；"
                          + "②鼠标是否处于广播态（已连 dongle / 已连其它主机时通常不广播，需重回配对/重连模式）；"
                          + "③距离是否过远；④是否被其它主机占用。", UiTheme.Danger);
                }
                else if (matchedCount == 0)
                {
                    if (unnamedCount > 0 && unnamedCount >= rawCount)
                        LogLine("收到的广播全部没有名称 → 都是无名称设备（可能是随机地址外设或被系统缓存过）。"
                              + "可取消勾选「隐藏无名称」查看，或等鼠标进入配对态后重扫。", UiTheme.Danger);
                    else
                        LogLine("能收到广播但被名称过滤全部排除 → 清空左侧过滤框后重扫即可看到设备。", UiTheme.Danger);
                }
            }));
        }

        private void SetScanning(bool scanning)
        {
            _btnScan.Enabled = !scanning;
            _btnScan.Text = scanning ? "扫描中" : "扫描";
            _txtFilter.Enabled = !scanning;
        }

        private void OnDeviceDiscovered(BleDeviceInfo info)
        {
            if (IsDisposed)
                return;
            BeginInvoke(new Action(() => UpsertDevice(info)));
        }

        /// <summary>按 MAC 去重：已存在就刷新 RSSI，避免列表被重复条目刷爆。</summary>
        private void UpsertDevice(BleDeviceInfo info)
        {
            // 勾选「隐藏无名称」时丢弃无名称条目（引擎层已过滤，这里作为兜底）
            if (_chkHideUnnamed.Checked && string.IsNullOrWhiteSpace(info.Name))
                return;

            BleDeviceInfo old;
            if (_found.TryGetValue(info.BluetoothAddress, out old))
            {
                old.Name = info.Name ?? old.Name;
                old.Rssi = info.Rssi;
                old.LastSeen = info.LastSeen;
                old.AddressType = info.AddressType;
                int idx = _lstDevices.Items.IndexOf(old);
                if (idx >= 0)
                {
                    _lstDevices.Items.RemoveAt(idx);
                    _lstDevices.Items.Insert(idx, old);
                }
            }
            else
            {
                _found[info.BluetoothAddress] = info;
                _lstDevices.Items.Add(info);
            }
            if (!_link.IsLinkUp)
            {
                _lblStatus.Text = "● 扫描中… 已发现 " + _found.Count + " 个设备";
                _lblStatus.ForeColor = UiTheme.Primary;
            }
        }

        private bool _connecting;

        private async void OnConnect(object sender, EventArgs e)
        {
            var dev = _lstDevices.SelectedItem as BleDeviceInfo;
            if (dev == null)
            {
                LogLine("请先在列表中选择一个设备。", UiTheme.Danger);
                return;
            }
            if (_connecting)
            {
                LogLine("上一次连接还在进行中，请稍候…", UiTheme.Danger);
                return;
            }

            // 已配对主流程：不需要扫描/地址类型，由 Windows 负责建链，本工具负责等链 + 接入 + 校验。
            bool paired = _pairedMode && !string.IsNullOrEmpty(dev.DeviceId);
            if (paired)
            {
                LogLine(string.Format("正在连接已配对设备 {0}（{1}）…",
                    dev.Name ?? "(未命名设备)", dev.Mac), UiTheme.TextSub);
            }
            else
            {
                // 广播条目是有寿命的：随机地址会轮换，旧条目直接连必然失败。
                TimeSpan age = DateTime.Now - dev.LastSeen;
                LogLine(string.Format("正在连接 {0}（{1}，地址类型 {2}，广播 {3:0.0}s 前）...",
                    dev.Mac, dev.Name ?? "(无名称)", dev.AddressType, age.TotalSeconds), UiTheme.TextSub);
                if (dev.AddressType == BluetoothAddressType.Random && age.TotalSeconds > 5)
                    LogLine("提示：该设备使用随机地址，广播已过去 "
                          + age.TotalSeconds.ToString("0") + "s，地址可能已轮换 → 建议重扫后立刻连接。",
                        UiTheme.Danger);
            }

            _connecting = true;
            _btnConnect.Enabled = false;
            try
            {
                string err = paired
                    ? await _link.ConnectByIdAsync(dev.DeviceId)
                    : await _link.ConnectAsync(dev.BluetoothAddress, dev.AddressType);
                if (err != null)
                {
                    LogLine("连接失败: " + err, UiTheme.Danger);
                    UpdateConnUi(false);
                    return;
                }
                // 走到这里 = SPP 已发现 **且** ATT 活性读通过（校验不过不会返回成功）。
                LogLine("已连接并发现 SPP：" + dev.Mac, UiTheme.Success);
                LogLine("链路活性校验通过：" + (_link.LastLivenessNote ?? "真实空口读取成功"),
                    UiTheme.Success);
                LogLine("SPP 命令探测：" + (_link.LastSppCmdNote ?? "未探测"), UiTheme.TextSub);
                if (paired)
                    LogLine("该设备已与 Windows 配对：可在系统蓝牙设置里管理/断开；"
                          + "本工具【断开】只释放本进程接入，系统可能仍保持链路（空闲后自动回连）。",
                        UiTheme.TextSub);
                else
                    LogLine("提示：未走系统配对的设备不会出现在 Windows「蓝牙和其他设备」列表里"
                          + "（属正常，不是没连上）；需要系统级管理可点【系统配对】先配对。", UiTheme.TextSub);
                UpdateConnUi(true);
            }
            catch (Exception ex)
            {
                LogLine("连接异常（" + ex.GetType().Name + "）：" + ex.Message, UiTheme.Danger);
                UpdateConnUi(false);
            }
            finally
            {
                _connecting = false;
                UpdateConnUi(_uiConnected);
            }
        }

        private async void OnDisconnect(object sender, EventArgs e)
        {
            _btnDisconnect.Enabled = false;
            LogLine("正在断开…", UiTheme.TextSub);
            // DisconnectAsync 会关 CCCD、释放对象，并回读系统真实链路状态：
            // WinRT 没有主动断开 API，「点了断开」不等于「链路一定断了」，如实告知用户。
            string note = await _link.DisconnectAsync();
            UpdateConnUi(false);
            foreach (var line in note.Split('；'))
            {
                string t = line.Trim();
                if (t.Length > 0)
                    LogLine(t + "；", t.Contains("仍维持") ? UiTheme.Warn : UiTheme.TextSub);
            }
            if (_pairedMode)
                LogLine("已配对设备：本工具只是释放接入，Windows 可能仍保持链路（空闲后会自动回连）。"
                      + "如需彻底断开/删除设备，用【系统配对】进系统蓝牙设置操作。", UiTheme.TextSub);
            _btnDisconnect.Enabled = false;
        }

        /// <summary>
        /// 【系统配对】：一键打开 Windows「蓝牙和设备」设置页。
        /// 配对是一次性 bonding 动作，系统 UI 有完整进度/确认/成功反馈，比应用内 PairAsync 稳
        /// （用户已实测：系统设置里配对连接完全正常）。配好后回到本工具扫描→连接即可，
        /// 且已配对设备的随机地址可由系统解析，「广播过期连不上」的问题一并消失。
        /// </summary>
        private void OnSystemPair(object sender, EventArgs e)
        {
            try
            {
                Process.Start(new ProcessStartInfo("ms-settings:bluetooth")
                {
                    UseShellExecute = true
                });
                LogLine("已打开 Windows 蓝牙设置：在设备列表里点该鼠标 → 配对/连接。"
                      + "成功后回到本工具【扫描】→【连接】（鼠标刚重启过，建议重扫一次）。",
                    UiTheme.TextSub);
            }
            catch (Exception ex)
            {
                LogLine("打开系统蓝牙设置失败（" + ex.GetType().Name + "）：" + ex.Message
                      + "。可手动打开：设置 → 蓝牙和设备 → 添加设备。", UiTheme.Danger);
            }
        }

        /// <summary>【读参数】：0x80 读 iOS 组 + 0x81 读 Android 组，回显 24 字节并解析两组数值。</summary>
        private async void OnSlideRead(object sender, EventArgs e)
        {
            _btnSlideRead.Enabled = false;
            LogLine("读滑动参数：TX  33F3 80 …（iOS 组）/  TX  33F3 81 …（Android 组）", UiTheme.TextSub);
            string err = await _link.SppSlideParamReadAsync(1500);
            if (err != null)
            {
                LogLine("读滑动参数失败：" + err, UiTheme.Danger);
            }
            else
            {
                // 照实显示设备上报：valid=1 为 flash 存储值；valid=0 时固件回的是编译默认值。
                byte[] p = _link.LastSlideParam;
                string[] osNames = { "iOS", "Android" };
                LogLine(string.Format("滑动参数 valid={0}（{1}）",
                        _link.LastSlideValid, _link.LastSlideValid == 1 ? "已配置" : "未配置，显示固件默认值"),
                    _link.LastSlideValid == 1 ? UiTheme.Success : UiTheme.Warn);
                for (int os = 0; os < 2; os++)
                {
                    int b = os * BleUxProto.SlideParamGroupSize;
                    int uf = p[b] | (p[b + 1] << 8), ur = p[b + 2] | (p[b + 3] << 8);
                    int lf = p[b + 6] | (p[b + 7] << 8), lrt = p[b + 8] | (p[b + 9] << 8);
                    LogLine(osNames[os] + " 组：", UiTheme.Rx);
                    LogLine(string.Format("  上下滑动 前进{0}ms 回位{1}ms / 步长{2} / 步数{3}", uf, ur, p[b + 4], p[b + 5]), UiTheme.Rx);
                    LogLine(string.Format("  左右滑动 前进{0}ms 回位{1}ms / 步长{2} / 步数{3}", lf, lrt, p[b + 10], p[b + 11]), UiTheme.Rx);
                }
                _txtSlide.Text = Hex(p);
            }
            UpdateConnUi(_link.IsConnected);
        }

        /// <summary>【写参数】：校验 24 字节 hex 后分两组发送——0x79 写 iOS 组 + 0x82 写 Android 组，
        /// 两组都成功才提示完成，参数已存鼠标 flash。</summary>
        private async void OnSlideWrite(object sender, EventArgs e)
        {
            byte[] cfg;
            string parseErr;
            if (!TryParseHex(_txtSlide.Text, out cfg, out parseErr) || cfg.Length != BleUxProto.SlideParamSize)
            {
                LogLine("写滑动参数失败：参数须为 24 字节 hex（iOS组12 + Android组12）。" + (parseErr ?? ""), UiTheme.Danger);
                return;
            }
            // 与固件 slideConfigCheck 一致的预校验：前进间隔 1~10000、回位间隔 1~2000、步长 1~127、步数 1~255
            string[] osNames = { "iOS", "Android" };
            string[] names = { "上下滑动", "左右滑动" };
            for (int os = 0; os < 2; os++)
            {
                for (int i = 0; i < 2; i++)
                {
                    int k = os * BleUxProto.SlideParamGroupSize + i * 6;
                    int fwdIv = cfg[k] | (cfg[k + 1] << 8), retIv = cfg[k + 2] | (cfg[k + 3] << 8);
                    if (fwdIv < 1 || fwdIv > 100 || retIv < 1 || retIv > 100 || cfg[k + 4] < 1
                        || cfg[k + 4] > 127 || cfg[k + 5] < 1)
                    {
                        LogLine(string.Format("写滑动参数失败：{0} 组·{1} 越限（前进/回位间隔均须1~100ms、步长须1~127、步数须1~255）。",
                            osNames[os], names[i]), UiTheme.Danger);
                        return;
                    }
                }
            }
            _btnSlideWrite.Enabled = false;
            LogLine("写滑动参数：TX  33F3 79 0C " + Hex(cfg).Substring(0, 35) + " …（iOS 组）", UiTheme.TextSub);
            LogLine("                33F3 82 0C " + Hex(cfg).Substring(36) + " …（Android 组）", UiTheme.TextSub);
            string err = await _link.SppSlideParamWriteAsync(cfg, 1500);
            if (err != null)
            {
                LogLine("写滑动参数失败：" + err, UiTheme.Danger);
            }
            else
            {
                LogLine("写滑动参数成功：两组均 status=1，已写入鼠标 flash，重启后保持。", UiTheme.Success);
            }
            UpdateConnUi(_link.IsConnected);
        }

        /// <summary>
        /// 【编辑参数…】：弹对话框可视化编辑 iOS/Android 两组滑动参数（各 2 方向：前进间隔/回位间隔/步长/步数），
        /// 输入实时汇总成 24 字节并预览 0x79/0x82 写参数帧；点【确定】把 24 字节填回参数输入框，
        /// 由客户自行点【写参数】写入鼠标 flash（本按钮不直接发送）。
        /// 打开时从参数框解析当前 24 字节作为初值，保持「读参数 → 编辑 → 写回」闭环。
        /// </summary>
        private void OnSlideEditParam(object sender, EventArgs e)
        {
            byte[] cur = null;
            byte[] parsed;
            string parseErr;
            if (TryParseHex(_txtSlide.Text, out parsed, out parseErr) && parsed.Length == BleUxProto.SlideParamSize)
                cur = parsed;

            using (var dlg = new SlideParamDialog(cur, _link))
            {
                if (dlg.ShowDialog(this) == DialogResult.OK && dlg.ResultParam != null)
                {
                    _txtSlide.Text = Hex(dlg.ResultParam);
                    LogLine("参数已汇总生成，点击【写参数】写入鼠标：TX  33F3 79 0C（iOS 组）+ 82 0C（Android 组）"
                          + "  共 " + dlg.ResultParam.Length + " 字节", UiTheme.TextSub);
                }
            }
        }

        private void OnSppReceived(byte[] frame)
        {
            if (IsDisposed)
                return;
            BeginInvoke(new Action(() => { LogLine("RX  33F4 " + Hex(frame), UiTheme.Rx); }));
        }

        private void OnConnectionChanged(bool up)
        {
            if (IsDisposed)
                return;
            BeginInvoke(new Action(() =>
            {
                UpdateConnUi(up);
                string msg;
                if (up)
                {
                    msg = "链路已建立（往返校验通过）。";
                }
                else if (!string.IsNullOrEmpty(_link.LastDropReason))
                {
                    msg = "链路已断开：" + _link.LastDropReason;
                }
                else
                {
                    msg = "设备已断开。";
                }
                LogLine(msg, up ? UiTheme.Success : UiTheme.Danger);
                if (up)
                {
                    // 链路建立后自动读取当前滑动参数并填回参数输入框；
                    // 「编辑参数…」对话框的初值取自该框，客户打开即看到设备当前参数。
                    LogLine("自动读取当前滑动参数…", UiTheme.TextSub);
                    OnSlideRead(null, EventArgs.Empty);
                }
            }));
        }

        // ---------- 工具 ----------
        private static string Hex(byte[] data)
        {
            return BitConverter.ToString(data).Replace("-", " ");
        }

        private static bool TryParseHex(string text, out byte[] payload, out string err)
        {
            payload = new byte[0];
            err = null;
            string cleaned = (text ?? string.Empty)
                .Replace(",", " ").Replace(";", " ")
                .Replace("0x", " ").Replace("0X", " ")
                .Replace("-", " ");
            string[] parts = cleaned.Split(
                new char[] { ' ', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 0)
            {
                err = "请输入至少 1 字节的 hex";
                return false;
            }
            if (parts.Length > BleUxProto.SlideParamSize)
            {
                err = "超过 " + BleUxProto.SlideParamSize + " 字节（滑动参数上限）";
                return false;
            }
            var buf = new byte[parts.Length];
            for (int i = 0; i < parts.Length; i++)
            {
                if (!byte.TryParse(parts[i], NumberStyles.AllowHexSpecifier,
                        CultureInfo.InvariantCulture, out buf[i]))
                {
                    err = "无法解析 \"" + parts[i] + "\"";
                    return false;
                }
            }
            payload = buf;
            return true;
        }

        /// <summary>【清空日志】：清空日志面板内容。</summary>
        private void OnClearLog(object sender, EventArgs e)
        {
            _txtLog.Clear();
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
