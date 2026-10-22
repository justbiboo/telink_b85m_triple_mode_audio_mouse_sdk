using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace BibooUxTool
{
    /// <summary>
    /// 固件升级（OTA）页：把 Telink bin 经三条通道之一送出去——
    ///   ① USB dongle：PC 经 USB 把固件灌进 dongle → dongle 实时组 OTA PDU 走 BLE 推给鼠标（OtaChannelUsbDongle，不写 dongle flash，鼠标零改动）
    ///   ② BLE 直连 ：PC 直连鼠标，写其 Telink OTA 特征（OtaChannelBle）
    ///   ③ Dongle 自身：镜像经 host link 0x70..0x75 写进 dongle 自己的备用 A/B bank，校验通过后 COMMIT 切换（DongleSelfOta）
    /// 协议层（bin 解析 / CRC16 / PDU 构造 / 状态机）见 OtaCore.cs，移植自 tool/ota_app 与
    /// SDK stack/ble/service/ota + blm_ota.c。
    ///
    /// ⚠ 当前默认「模拟模式」：固件侧命令（dongle SPP 0x02 / BLE OTA 已就绪）尚未全部实现，
    ///    模拟模式只跑协议与流程、不实发；固件就绪后取消勾选即可实发。
    /// </summary>
    public sealed class OtaForm : Form
    {
        private const int CardMargin = 12;
        private const int CardGap = 12;
        private const int FileCardHeight = 116;
        private const int ChannelCardHeight = 224;   // BLE 子面板要放下模式/刷新/系统配对/连接/断开
        private const int ProgressCardHeight = 96;
        private const int LogCardMinHeight = 180;

        // 文件卡
        private TextBox _txtBin;
        private Label _lblInfo;

        // 通道卡
        private RadioButton _radUsb;
        private RadioButton _radSelf;   // Dongle 自身固件升级（host link 0x70..0x75）
        private RadioButton _radBle;
        private Panel _pnlUsb;
        private Panel _pnlBle;
        private Button _btnUsbConnect;
        private Label _lblUsbState;
        private Button _btnBleConnect;
        private ListBox _lstBle;
        private Label _lblBleState;
        private DongleLink _dongle;
        private readonly BleDirectLink _ble = new BleDirectLink();

        // BLE 子面板的配对逻辑状态（对齐「蓝牙连接」BleScannerForm：
        // 默认列系统已配对设备 → 选中回连 → ATT 活性校验 → 断开/系统配对/扫描旁路）
        private GradientButton _btnBleModePaired;
        private GradientButton _btnBleModeScan;
        private GradientButton _btnBleRefresh;
        private GradientButton _btnBleSysPair;
        private GradientButton _btnBleDisconnect;
        private readonly Dictionary<string, BleDeviceInfo> _blePairedItems = new Dictionary<string, BleDeviceInfo>();
        private readonly Dictionary<ulong, BleDeviceInfo> _bleFound = new Dictionary<ulong, BleDeviceInfo>();
        private bool _blePairedMode = true;      // true = 系统已配对设备（默认，免扫描）；false = 扫描附近
        private bool _uiBleConnected;            // UI 口径的连接态（只由 ATT 活性校验给出）
        private bool _bleBusyConnect;            // 连接进行中（防重入）
        private int _bleVerifyBusy;
        private int _bleFwProbeBusy;             // 回连后补读固件版本（防重入）
        private bool _bleLinkDropped;            // 断开过链路（回连时强制重读固件版本）
        private int _bleFwProbeTries;            // 回连补读已尝试次数（防无 DevInf 固件时无限重试）
        // OTA 传输进行中：此时必须停掉 2.5s 的 ATT 活性巡检。
        // 原因：设备正在写 flash，对 Device Name 的读请求常被拖到 1200ms 超时，
        // 巡检会误判「链路不可达」→ ReleaseDevice → 正在进行的写全部 E_ABORT，升级被自己掐断。
        private volatile bool _otaRunning;
        private System.Windows.Forms.Timer _bleLinkTimer;   // 2.5s 一次 ATT 活性读，揪出假连接

        // 进度卡
        private ProgressBar _prog;
        private Label _lblProg;
        private Button _btnStart;
        private Button _btnStop;

        // 日志卡
        private RichTextBox _txtLog;
        private Button _btnClearLog;

        // 四张卡片的引用（BuildUi 里创建，LayoutContent 用；不用 Controls 索引取，避免顺序耦合）
        private RoundedPanel _cardFile;
        private RoundedPanel _cardCh;
        private RoundedPanel _cardProg;
        private RoundedPanel _cardLog;

        private TelinkFirmware _fw;
        private CancellationTokenSource _cts;
        private readonly object _runLock = new object();

        public OtaForm()
        {
            BuildUi();
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            // 句柄就绪后再启「已配对设备」枚举（AEP watcher）：事件在线程池线程回调，
            // 必须能 BeginInvoke 到 UI（与「蓝牙连接」页同一时序）。
            if (_blePairedMode)
            {
                _ble.StartPairedWatch();
                LogLine("正在加载系统已配对设备列表…", UiTheme.TextSub);
                // 版本号只在广播 Manufacturer Data 里（固件 AAA_app.c：05 FF co_id major minor），
                // AEP 列表拿不到。顺带跑一次 3s 后台扫描，只用来给已配对条目补版本号。
                _ble.StartScan(null, 3000, true);
                LogLine("后台短扫描 3s：读取设备广播中的固件版本号…", UiTheme.TextSub);
            }
        }

        // ---------- UI 构建 ----------

        private void BuildUi()
        {
            Text = "Biboo OS - 固件升级 (OTA)";
            BackColor = UiTheme.FormBg;
            Font = UiTheme.FontBody;
            ClientSize = new Size(920, 700);
            MinimumSize = new Size(900, 720);   // 下限要容得下四张卡 + 日志最小高度（通道卡 224 后抬高）
            StartPosition = FormStartPosition.CenterParent;
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); }
            catch (Exception) { }

            // ===== 卡1：固件文件 =====
            var cardFile = new RoundedPanel();
            cardFile.SetBounds(CardMargin, CardMargin, 896, FileCardHeight);
            cardFile.Anchor = AnchorStyles.Top;
            _cardFile = cardFile;
            cardFile.Controls.Add(MakeCardTitle("固件文件"));

            _txtBin = new TextBox();
            _txtBin.SetBounds(16, 38, 700, 24);
            _txtBin.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            cardFile.Controls.Add(_txtBin);

            var btnBrowse = MakeButton("浏览...", 724, 36, 76, 28, false, OnBrowse);
            btnBrowse.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            cardFile.Controls.Add(btnBrowse);
            cardFile.Controls.SetChildIndex(btnBrowse, 0);

            _lblInfo = new Label();
            _lblInfo.Text = "尚未选择固件";
            _lblInfo.ForeColor = UiTheme.TextSub;
            _lblInfo.SetBounds(16, 72, 860, 32);
            _lblInfo.AutoSize = false;
            _lblInfo.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _lblInfo.TextAlign = ContentAlignment.MiddleLeft;
            cardFile.Controls.Add(_lblInfo);

            // ===== 卡2：升级通道 =====
            var cardCh = new RoundedPanel();
            cardCh.SetBounds(CardMargin, CardMargin + FileCardHeight + CardGap, 896, ChannelCardHeight);
            cardCh.Anchor = AnchorStyles.Top;
            _cardCh = cardCh;
            cardCh.Controls.Add(MakeCardTitle("升级通道"));

            _radUsb = new RadioButton();
            _radUsb.Text = "USB Dongle";
            _radUsb.ForeColor = UiTheme.TextMain;
            _radUsb.SetBounds(214, 34, 190, 20);
            _radUsb.Checked = true;
            _radUsb.CheckedChanged += (s, e) => SyncChannelPanels();
            cardCh.Controls.Add(_radUsb);

            _radSelf = new RadioButton();
            _radSelf.Text = "Dongle Self";
            _radSelf.ForeColor = UiTheme.TextMain;
            _radSelf.SetBounds(16, 34, 200, 20);
            _radSelf.CheckedChanged += (s, e) => SyncChannelPanels();
            cardCh.Controls.Add(_radSelf);

            _radBle = new RadioButton();
            _radBle.Text = "BLE Direct";
            _radBle.ForeColor = UiTheme.TextMain;
            _radBle.SetBounds(424, 34, 400, 20);
            cardCh.Controls.Add(_radBle);

            // --- USB 子面板 ---
            _pnlUsb = new Panel();
            _pnlUsb.SetBounds(16, 62, 860, 46);
            _pnlUsb.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _pnlUsb.BackColor = Color.Transparent;

            _btnUsbConnect = MakeButton("连接 Dongle", 0, 8, 110, 28, true, OnUsbConnect);
            _pnlUsb.Controls.Add(_btnUsbConnect);

            _lblUsbState = new Label();
            _lblUsbState.Text = "未连接";
            _lblUsbState.ForeColor = UiTheme.TextSub;
            _lblUsbState.SetBounds(222, 12, 638, 22);
            _lblUsbState.AutoSize = false;
            _lblUsbState.TextAlign = ContentAlignment.MiddleLeft;
            _pnlUsb.Controls.Add(_lblUsbState);

            cardCh.Controls.Add(_pnlUsb);

            // --- BLE 子面板（配对逻辑与「蓝牙连接」页一致：
            //     默认列系统已配对设备 → 选中【连接】/双击回连 → 系统配对兜底 → 断开 → ATT 活性巡检）---
            _pnlBle = new Panel();
            _pnlBle.SetBounds(16, 62, 860, 150);
            _pnlBle.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _pnlBle.BackColor = Color.Transparent;
            _pnlBle.Visible = false;

            _btnBleModePaired = MakeButton("已配对设备", 0, 4, 96, 26, true, OnBleModePaired);
            _pnlBle.Controls.Add(_btnBleModePaired);
            _btnBleModeScan = MakeButton("扫描附近", 100, 4, 96, 26, false, OnBleModeScan);
            _pnlBle.Controls.Add(_btnBleModeScan);
            _btnBleRefresh = MakeButton("刷新", 200, 4, 60, 26, false, OnBleRefresh);
            _pnlBle.Controls.Add(_btnBleRefresh);
            _btnBleSysPair = MakeButton("系统配对", 264, 4, 88, 26, false, OnBleSysPair);
            _pnlBle.Controls.Add(_btnBleSysPair);

            _lblBleState = new Label();
            _lblBleState.Text = "○ 未连接";
            _lblBleState.ForeColor = UiTheme.TextSub;
            _lblBleState.SetBounds(362, 6, 498, 24);
            _lblBleState.AutoSize = false;
            _lblBleState.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _lblBleState.TextAlign = ContentAlignment.MiddleLeft;
            _pnlBle.Controls.Add(_lblBleState);

            _lstBle = new ListBox();
            _lstBle.SetBounds(0, 36, 470, 108);
            _lstBle.Font = new Font("Consolas", 9f);
            _lstBle.BackColor = UiTheme.InputBg;
            _lstBle.ForeColor = UiTheme.TextMain;
            _lstBle.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            _lstBle.SelectedIndexChanged += (s, e) => UpdateBleConnUi(_uiBleConnected);
            _lstBle.DoubleClick += OnBleListDoubleClick;
            _pnlBle.Controls.Add(_lstBle);

            // 右侧按钮列统一右锚定：窗口拉宽时整列跟随右移，不与列表框相交
            _btnBleConnect = MakeButton("连接", 478, 36, 96, 30, true, OnBleConnect);
            _btnBleConnect.Enabled = false;
            _btnBleConnect.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _pnlBle.Controls.Add(_btnBleConnect);

            _btnBleDisconnect = MakeButton("断开", 478, 70, 96, 30, false, OnBleDisconnect);
            _btnBleDisconnect.Enabled = false;
            _btnBleDisconnect.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _pnlBle.Controls.Add(_btnBleDisconnect);

            cardCh.Controls.Add(_pnlBle);

            _ble.DeviceDiscovered += OnBleDiscovered;
            _ble.ConnectionChanged += OnBleConnChanged;
            _ble.PairedDeviceChanged += OnBlePairedChanged;
            _ble.PairedWatchStateChanged += OnBlePairedWatchState;
            _ble.ScanCompleted += OnBleScanCompleted;

            _bleLinkTimer = new System.Windows.Forms.Timer();
            _bleLinkTimer.Interval = 2500;   // 与「蓝牙连接」页一致：2.5s 一次 ATT 活性读
            _bleLinkTimer.Tick += OnBleLinkTimer;
            _bleLinkTimer.Start();

            // ===== 卡3：进度 =====
            var cardProg = new RoundedPanel();
            cardProg.SetBounds(CardMargin, CardMargin + FileCardHeight + CardGap + ChannelCardHeight + CardGap,
                               896, ProgressCardHeight);
            cardProg.Anchor = AnchorStyles.Top;
            _cardProg = cardProg;
            cardProg.Controls.Add(MakeCardTitle("进度"));

            _btnStart = MakeButton("开始升级", 14, 30, 110, 30, true, OnStart);
            cardProg.Controls.Add(_btnStart);
            _btnStop = MakeButton("停止", 130, 30, 90, 30, false, OnStop);
            _btnStop.Enabled = false;
            cardProg.Controls.Add(_btnStop);

            _prog = new ProgressBar();
            _prog.SetBounds(232, 34, 648, 22);
            _prog.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _prog.Minimum = 0;
            _prog.Maximum = 100;
            cardProg.Controls.Add(_prog);

            _lblProg = new Label();
            _lblProg.Text = "就绪";
            _lblProg.ForeColor = UiTheme.TextSub;
            _lblProg.SetBounds(14, 64, 866, 22);
            _lblProg.AutoSize = false;
            _lblProg.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _lblProg.TextAlign = ContentAlignment.MiddleLeft;
            cardProg.Controls.Add(_lblProg);

            // ===== 卡4：日志 =====
            var cardLog = new RoundedPanel();
            cardLog.SetBounds(CardMargin,
                CardMargin + FileCardHeight + CardGap + ChannelCardHeight + CardGap + ProgressCardHeight + CardGap,
                896, 200);
            cardLog.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Bottom;
            _cardLog = cardLog;
            cardLog.Controls.Add(MakeCardTitle("日志"));

            _btnClearLog = MakeButton("清空日志", 0, 6, 92, 22, false, (s, e) => _txtLog.Clear());
            _btnClearLog.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            cardLog.Controls.Add(_btnClearLog);
            cardLog.Controls.SetChildIndex(_btnClearLog, 0);   // 索引 0 = 最上层，压过标题 Label

            _txtLog = new RichTextBox();
            _txtLog.SetBounds(12, 34, 872, 150);
            _txtLog.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Bottom;
            _txtLog.BackColor = UiTheme.InputBg;
            _txtLog.ForeColor = UiTheme.TextMain;
            _txtLog.ReadOnly = true;
            _txtLog.DetectUrls = false;
            cardLog.Controls.Add(_txtLog);

            Controls.Add(cardFile);
            Controls.Add(cardCh);
            Controls.Add(cardProg);
            Controls.Add(cardLog);

            SyncChannelPanels();
            LayoutContent();
            LogLine("OTA 页就绪：选择固件 bin → 选通道 →【开始升级】。", UiTheme.TextSub);
            LogLine("提示：USB 通道需 Dongle 已插且鼠标已连上 Dongle；BLE 通道默认列出已配对且已连接的设备"
                    + "（与「蓝牙连接」页同一套配对逻辑），选中【连接】即可，未配对先用【系统配对】。", UiTheme.TextSub);
            LogLine("「Dongle Self」通道升级的是 Dongle 自己的固件：镜像写入备用 A/B bank，" +
                    "上传/校验阶段不影响运行中的固件，只有最后确认【切换】才不可逆。", UiTheme.TextSub);
        }

        private void LayoutContent()
        {
            int availW = ClientSize.Width - CardMargin * 2;
            if (availW < 300)
                availW = 300;
            int x = CardMargin;
            int y = CardMargin;

            if (_cardFile != null)
            {
                _cardFile.SetBounds(x, y, availW, FileCardHeight);
                y += FileCardHeight + CardGap;
            }
            if (_cardCh != null)
            {
                _cardCh.SetBounds(x, y, availW, ChannelCardHeight);
                y += ChannelCardHeight + CardGap;
            }
            if (_cardProg != null)
            {
                _cardProg.SetBounds(x, y, availW, ProgressCardHeight);
                y += ProgressCardHeight + CardGap;
            }
            if (_cardLog != null)
            {
                int logH = ClientSize.Height - y - CardMargin;
                if (logH < LogCardMinHeight)
                    logH = LogCardMinHeight;
                _cardLog.SetBounds(x, y, availW, logH);
            }
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
            lbl.SetBounds(14, 8, 700, 20);
            lbl.Font = UiTheme.FontTitle;
            lbl.ForeColor = UiTheme.TextMain;
            lbl.TextAlign = ContentAlignment.MiddleLeft;
            lbl.AutoSize = false;
            lbl.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            return lbl;
        }

        private static GradientButton MakeButton(string text, int x, int y, int w, int h,
                                                 bool primary, EventHandler onClick)
        {
            var b = new GradientButton();
            b.SetBounds(x, y, w, h);
            b.Text = text;
            b.IsPrimary = primary;
            b.Click += onClick;
            return b;
        }

        private void LogLine(string msg, Color color)
        {
            if (_txtLog == null)
                return;
            if (_txtLog.TextLength > 60000)
                _txtLog.Clear();
            _txtLog.SelectionStart = _txtLog.TextLength;
            _txtLog.SelectionColor = UiTheme.Timestamp;
            _txtLog.AppendText(DateTime.Now.ToString("HH:mm:ss.fff") + "  ");
            _txtLog.SelectionColor = color;
            _txtLog.AppendText(msg + Environment.NewLine);
            _txtLog.ScrollToCaret();
        }

        // ---------- 文件选择 ----------

        private void OnBrowse(object sender, EventArgs e)
        {
            using (var dlg = new OpenFileDialog())
            {
                dlg.Filter = "Telink 固件 (*.bin)|*.bin|所有文件 (*.*)|*.*";
                dlg.Title = "选择固件 bin";
                if (dlg.ShowDialog(this) != DialogResult.OK)
                    return;
                _txtBin.Text = dlg.FileName;
                LoadBin(dlg.FileName);
            }
        }

        private void LoadBin(string path)
        {
            try
            {
                var bin = System.IO.File.ReadAllBytes(path);
                _fw = TelinkFirmware.Parse(bin);
                int pduCount = _fw.FragmentCount(OtaProto.PduDataLen);
                _lblInfo.Text = string.Format("文件头版本 {0}    大小 {1} 字节    整体 CRC {2}    "
                                            + "数据 PDU 数 {3}（鼠标当前版本见连接成功后的状态栏/日志）",
                    _fw.VersionHex, _fw.Size, _fw.CrcHex, pduCount);
                _lblInfo.ForeColor = UiTheme.TextMain;
                LogLine("固件已加载：" + System.IO.Path.GetFileName(path) + "  " + _lblInfo.Text, UiTheme.Success);
            }
            catch (Exception ex)
            {
                _fw = null;
                _lblInfo.Text = "解析失败：" + ex.Message;
                _lblInfo.ForeColor = UiTheme.Danger;
                LogLine("固件解析失败：" + ex.Message, UiTheme.Danger);
            }
        }

        // ---------- 通道：USB dongle ----------

        private void SyncChannelPanels()
        {
            _pnlUsb.Visible = _radUsb.Checked || _radSelf.Checked;
            _pnlBle.Visible = _radBle.Checked;
            if (_btnStart != null)
                _btnStart.Text = _radSelf.Checked ? "升级 Dongle" : "开始升级";
        }

        private void OnUsbConnect(object sender, EventArgs e)
        {
            try
            {
                if (_dongle != null && _dongle.IsConnected)
                {
                    _dongle.Disconnect();
                    _lblUsbState.Text = "已断开 Dongle";
                    LogLine("Dongle 已断开", UiTheme.TextSub);
                    return;
                }

                // 2026-10-22：打开 + 握手全程移到后台线程。Dongle 半死（如升级事故后
                // EP0 卡死）时每条 SET_REPORT 控制传输要挂 ~5s 才报错，同步跑在 UI 线程
                // 就是 0x50 一条 + DevInf 0x55 多帧 = 15s+，窗口直接“未响应”。
                _dongle = new DongleLink();
                _btnUsbConnect.Enabled = false;
                _lblUsbState.Text = "正在连接 Dongle…";

                System.Threading.Tasks.Task.Run(() =>
                {
                    string err = _dongle.Connect();
                    if (err != null)
                    {
                        UiLog("连接 Dongle 失败：" + err, 2);
                        SafeConnectUi("连接失败：" + err);
                        return;
                    }

                    byte[] rsp;
                    int r = _dongle.SendCommand(UxFtProto.CmdOtaStatus, null, DongleLink.CommandTimeoutMs, out rsp, out err);
                    if (r == 1 && rsp[UxFtProto.UlOffsStatus] == UxFtProto.StOk)
                    {
                        UiLog("Dongle 已连接：" + _dongle.GetOpenedPath() + "，OTA 流管道命令就绪（0x50..0x52/0x54）", 3);

                        // 先落默认状态栏文案，再读 DevInf——后者的 BeginInvoke 排在后面，
                        // 读取成功时状态栏最终显示带鼠标版本号的文案。
                        SafeConnectUi("已连接 Dongle（OTA 流管道就绪）");

                        // BIBOO UX 2026-10-21：连上后读一次鼠标固件版本号。鼠标此刻被 dongle
                        // 独占 BLE 链路（PC 无法另开一路 GATT 读），故由 dongle 以 BLE master
                        // 身份读 OTA 服务下的只读特征 DevInf_aaa（句柄 0x0010）后经 0x55 中继回来。
                        ReadDongleDevInfo();

                        // 2026-10-22：读 dongle 运行固件 build 号（0x76）。原地覆盖 COMMIT 后
                        // 运行 bank 恒为 0x00000、bin 头版本恒为 0，这个 build 号是升级
                        // 真正落地的唯一可见证据（dongle 侧每次发版递增）。
                        uint vHdr, vBuild;
                        int vr = DongleSelfOta.QuerySelfVersion(_dongle, DongleLink.CommandTimeoutMs,
                                                                out vHdr, out vBuild, out err);
                        if (vr == 1)
                            UiLog(string.Format(
                                "运行 dongle 固件 build 0x{0:X8}（bin 头版本 0x{1:X8}）——升级后重连，build 变化即为成功",
                                vBuild, vHdr), 3);
                        return;
                    }

                    // 打开了 HID 句柄但 0x50 没得到 OK。dongle 半死（EP0 卡死无响应）与
                    // 固件过旧都会走到这里：给可操作的指引，而不是只让人升级固件。
                    UiLog("连接检查失败：Dongle 无响应（" + (err ?? "status 异常") +
                          "）。反复出现时给 Dongle 断电重插；若 dongle 固件早于 2026-10-20 也无此命令", 1);
                    SafeConnectUi("Dongle 无响应（详见日志）");
                });
            }
            catch (Exception ex)
            {
                _btnUsbConnect.Enabled = true;
                LogLine("连接 Dongle 异常：" + ex.Message, UiTheme.Danger);
            }
        }

        /// <summary>后台连接线程回 UI：更新状态栏并恢复按钮（BeginInvoke 防跨线程）。</summary>
        private void SafeConnectUi(string stateText)
        {
            if (IsDisposed || !IsHandleCreated)
                return;
            try
            {
                BeginInvoke(new Action(() =>
                {
                    _btnUsbConnect.Enabled = true;
                    _lblUsbState.Text = stateText;
                }));
            }
            catch (Exception) { }
        }

        // ---------- USB Dongle 通道：鼠标固件版本号（DevInf 经 dongle 中继）----------

        /// <summary>
        /// 连上 Dongle 后读一次鼠标 DevInf，只把固件版本号显示到状态栏/日志
        /// （鼠标 SDK / IC SDK / 原始字节不打印，信息量过载）。
        /// </summary>
        private void ReadDongleDevInfo()
        {
            if (_dongle == null || !_dongle.IsConnected)
                return;

            byte[] raw;
            string err;
            // 2026-10-22：本方法现在由后台连接线程调用，UI 更新一律走 UiLog/BeginInvoke。
            int r = _dongle.ReadDevInfo(DongleLink.CommandTimeoutMs, out raw, out err);
            if (r != 1)
            {
                UiLog("读取鼠标版本号失败：" + (err ?? "未知") + "（不影响 OTA 升级）", 1);
                return;
            }

            var di = BleDevInf.Parse(raw);
            if (di == null)
            {
                UiLog("DevInf 长度异常：" + raw.Length + " 字节", 1);
                return;
            }

            if (IsDisposed || !IsHandleCreated)
                return;
            try
            {
                BeginInvoke(new Action(() =>
                {
                    _lblUsbState.Text = "已连接 Dongle（OTA 流管道就绪） · 鼠标固件 " + di.FwText;
                    LogLine("GATT（经 Dongle 中继）读回鼠标固件版本：" + di.FwText, UiTheme.Success);
                }));
            }
            catch (Exception) { }
        }

        // ---------- 通道：BLE 直连（配对逻辑对齐「蓝牙连接」页 BleScannerForm）----------

        /// <summary>模式切换后刷新控件可用态与列表（不负责启停 watcher）。</summary>
        private void ApplyBleMode()
        {
            if (_btnBleModePaired == null)
                return;
            _btnBleModePaired.IsPrimary = _blePairedMode;
            _btnBleModeScan.IsPrimary = !_blePairedMode;
            _btnBleRefresh.Enabled = _blePairedMode;
            _lstBle.Items.Clear();
            _bleFound.Clear();
            _blePairedItems.Clear();
            UpdateBleConnUi(_uiBleConnected);
        }

        private void OnBleModePaired(object sender, EventArgs e)
        {
            if (_blePairedMode)
                return;
            _blePairedMode = true;
            ApplyBleMode();
            _ble.StartPairedWatch();
            LogLine("已切到「已配对设备」：取系统已配对 BLE 设备中当前已连接的（免扫描），"
                  + "选中后点【连接】或双击条目即可回连。", UiTheme.TextSub);
        }

        private void OnBleModeScan(object sender, EventArgs e)
        {
            if (_blePairedMode)
            {
                _blePairedMode = false;
                _ble.StopPairedWatch();
                ApplyBleMode();
                LogLine("已切到「扫描附近」：10s 广播扫描，用于直连未配对设备"
                      + "（不占鼠标 bonding 名额；随机地址会轮换，扫到后尽快连）。", UiTheme.TextSub);
            }
            StartBleScan();   // 再点一次 = 重新扫描
        }

        private void OnBleRefresh(object sender, EventArgs e)
        {
            if (!_blePairedMode)
                return;
            _lstBle.Items.Clear();
            _blePairedItems.Clear();
            _ble.RestartPairedWatch();
            LogLine("正在刷新已配对设备列表…", UiTheme.TextSub);
        }

        /// <summary>
        /// 【系统配对】：一键打开 Windows「蓝牙和设备」设置页（与「蓝牙连接」页同一做法）。
        /// 配对是一次性 bonding，系统 UI 有完整进度/确认，比应用内 PairAsync 稳；
        /// 配好后已配对列表会自动多出该设备，选中【连接】即可。
        /// </summary>
        private void OnBleSysPair(object sender, EventArgs e)
        {
            try
            {
                Process.Start(new ProcessStartInfo("ms-settings:bluetooth") { UseShellExecute = true });
                LogLine("已打开 Windows 蓝牙设置：在设备列表里点该鼠标 → 配对/连接；"
                      + "成功后回本页【刷新】→【连接】。", UiTheme.TextSub);
            }
            catch (Exception ex)
            {
                LogLine("打开系统蓝牙设置失败（" + ex.GetType().Name + "）：" + ex.Message
                      + "。可手动：设置 → 蓝牙和设备 → 添加设备。", UiTheme.Danger);
            }
        }

        private void StartBleScan()
        {
            _lstBle.Items.Clear();
            _bleFound.Clear();
            _ble.StopScan();
            LogLine("扫描中（10s，隐藏无名称设备）…", UiTheme.TextSub);
            _ble.StartScan(null, 10000, true);
        }

        private void OnBleDiscovered(BleDeviceInfo dev)
        {
            if (IsDisposed || !IsHandleCreated || dev == null)
                return;
            try { BeginInvoke(new Action(() => UpsertScannedBleDevice(dev))); }
            catch (Exception) { }
        }

        /// <summary>扫描条目按 MAC 去重（已存在就刷新名称/RSSI，不重复建行）。</summary>
        private void UpsertScannedBleDevice(BleDeviceInfo dev)
        {
            // 已配对模式下，扫描结果只用来补「固件版本」：AEP 列表没有广播数据，
            // 而版本号只存在于广播 Manufacturer Data（固件 AAA_app.c）。
            if (_blePairedMode)
            {
                AttachFwVersionToPaired(dev);
                return;
            }
            BleDeviceInfo old;
            if (_bleFound.TryGetValue(dev.BluetoothAddress, out old))
            {
                old.Name = dev.Name ?? old.Name;
                old.Rssi = dev.Rssi;
                old.LastSeen = dev.LastSeen;
                old.AddressType = dev.AddressType;
                if (!string.IsNullOrEmpty(dev.FwVersion))
                    old.FwVersion = dev.FwVersion;
                int idx = _lstBle.Items.IndexOf(old);
                if (idx >= 0)
                {
                    _lstBle.Items.RemoveAt(idx);
                    _lstBle.Items.Insert(idx, old);
                }
            }
            else
            {
                _bleFound[dev.BluetoothAddress] = dev;
                _lstBle.Items.Add(dev);
            }
        }

        /// <summary>
        /// 已配对模式下，把扫描得到的固件版本补到对应 MAC 的已配对条目上。
        /// 版本号只在广播 Manufacturer Data 里，AEP 列表拿不到，故连接前跑一次短扫描。
        /// </summary>
        private void AttachFwVersionToPaired(BleDeviceInfo dev)
        {
            if (string.IsNullOrEmpty(dev.FwVersion))
                return;
            foreach (var kv in _blePairedItems)
            {
                if (kv.Value.BluetoothAddress != dev.BluetoothAddress || kv.Value.AddressKnown == false)
                    continue;
                if (kv.Value.FwVersion == dev.FwVersion)
                    return;
                kv.Value.FwVersion = dev.FwVersion;
                int idx = _lstBle.Items.IndexOf(kv.Value);
                if (idx >= 0)
                {
                    int sel = _lstBle.SelectedIndex;      // 刷新行不能把用户的选中打掉
                    _lstBle.Items.RemoveAt(idx);
                    _lstBle.Items.Insert(idx, kv.Value);
                    _lstBle.SelectedIndex = sel;
                }
                return;
            }
        }

        private void OnBleScanCompleted(int rawCount, int matchedCount, int unnamedCount, string reason)
        {
            if (IsDisposed || !IsHandleCreated)
                return;
            try
            {
                BeginInvoke(new Action(() =>
                {
                    if (_blePairedMode)
                    {
                        // 已配对模式下的短扫描只为补版本号，不列设备
                        LogLine(string.Format("版本读取扫描结束（{0}）：收到广播 {1} 条。", reason, rawCount),
                            UiTheme.TextSub);
                        return;
                    }
                    LogLine(string.Format("扫描结束（{0}）：收到广播 {1} 条，列出设备 {2} 个{3}。",
                        reason, rawCount, matchedCount,
                        unnamedCount > 0 ? "，隐藏无名称 " + unnamedCount + " 个" : ""), UiTheme.TextSub);
                    if (rawCount == 0)
                        LogLine("一条广播都没收到 → 查：①Windows 蓝牙是否打开；②鼠标是否在广播态"
                              + "（已连 Dongle/其它主机时通常不广播）；③距离；④是否被其它主机占用。",
                            UiTheme.Danger);
                }));
            }
            catch (Exception) { }
        }

        private void OnBlePairedChanged(BleDeviceInfo info, BleDirectLink.PairedChangeKind kind)
        {
            if (IsDisposed || !IsHandleCreated || info == null)
                return;
            try { BeginInvoke(new Action(() => UpsertPairedBleItem(info, kind))); }
            catch (Exception) { }
        }

        /// <summary>已配对列表增/改/删（按 DeviceId 去重；快照与 watcher 重复上报不会产生重复行）。</summary>
        private void UpsertPairedBleItem(BleDeviceInfo info, BleDirectLink.PairedChangeKind kind)
        {
            if (IsDisposed)
                return;
            BleDeviceInfo old;
            switch (kind)
            {
                case BleDirectLink.PairedChangeKind.Added:
                case BleDirectLink.PairedChangeKind.Updated:
                    // 「已配对」列表只保留系统已连接的设备：未连接的不建行；已在列表中的
                    // 条目掉线（Updated 且 SystemConnected=false）时立即移出，保证所见即可连。
                    if (!info.SystemConnected)
                    {
                        if (_blePairedItems.TryGetValue(info.DeviceId, out old))
                        {
                            _lstBle.Items.Remove(old);
                            _blePairedItems.Remove(info.DeviceId);
                        }
                        break;
                    }
                    if (_blePairedItems.TryGetValue(info.DeviceId, out old))
                    {
                        int idx = _lstBle.Items.IndexOf(old);
                        if (idx >= 0)
                        {
                            bool wasSel = ReferenceEquals(_lstBle.SelectedItem, old);
                            _lstBle.Items.RemoveAt(idx);
                            _lstBle.Items.Insert(idx, info);
                            if (wasSel)
                                _lstBle.SelectedItem = info;   // 重排会丢选中，恢复
                        }
                        _blePairedItems[info.DeviceId] = info;
                    }
                    else
                    {
                        _blePairedItems[info.DeviceId] = info;
                        _lstBle.Items.Add(info);
                    }
                    break;
                case BleDirectLink.PairedChangeKind.Removed:
                    if (_blePairedItems.TryGetValue(info.DeviceId, out old))
                    {
                        _lstBle.Items.Remove(old);
                        _blePairedItems.Remove(info.DeviceId);
                    }
                    break;
            }
            UpdateBleConnUi(_uiBleConnected);
        }

        private void OnBlePairedWatchState(string msg)
        {
            if (IsDisposed || !IsHandleCreated)
                return;
            try { BeginInvoke(new Action(() => LogLine(msg, UiTheme.Warn))); }
            catch (Exception) { }
        }

        /// <summary>把「已接入」标记打到对应行（刷新状态文字；重排会丢选中，需恢复）。</summary>
        private void MarkBleAttached(string deviceId)
        {
            if (!_blePairedMode)
                return;
            var sel = _lstBle.SelectedItem as BleDeviceInfo;
            string selId = sel != null ? sel.DeviceId : null;
            try
            {
                foreach (var kv in _blePairedItems)
                {
                    bool attached = !string.IsNullOrEmpty(deviceId) && kv.Key == deviceId;
                    if (kv.Value.Attached == attached)
                        continue;
                    kv.Value.Attached = attached;
                    int i = _lstBle.Items.IndexOf(kv.Value);
                    if (i >= 0)
                    {
                        _lstBle.Items.RemoveAt(i);
                        _lstBle.Items.Insert(i, kv.Value);
                    }
                }
            }
            finally
            {
                if (selId != null)
                {
                    BleDeviceInfo cur;
                    if (_blePairedItems.TryGetValue(selId, out cur))
                        _lstBle.SelectedItem = cur;
                }
            }
        }

        /// <summary>连接态 UI。只认 ATT 活性校验结果（BleDirectLink.IsConnected），不给假连接。</summary>
        private void UpdateBleConnUi(bool connected)
        {
            _uiBleConnected = connected;
            if (_lblBleState == null)
                return;

            if (connected)
            {
                string ver = _lstBle.SelectedItem is BleDeviceInfo
                    ? ((BleDeviceInfo)_lstBle.SelectedItem).FwVersion : null;
                string verText = string.IsNullOrEmpty(ver) ? "" : " · 固件 " + ver;
                _lblBleState.Text = _ble.HasOtaChar
                    ? "● 已连接（ATT 活性校验通过 · 已发现 OTA 特征，可升级" + verText + "）"
                    : "● 已连接（ATT 活性校验通过 · 未发现 OTA 特征，无法升级）";
                _lblBleState.ForeColor = _ble.HasOtaChar ? UiTheme.Success : UiTheme.Warn;
            }
            else
            {
                if (_otaRunning)
                {
                    // 升级中掉线是固件写 flash / 重启的正常表现，不给红色警告
                    _lblBleState.Text = "◌ 升级传输中：设备重启后会重新连上（属预期）";
                    _lblBleState.ForeColor = UiTheme.TextSub;
                }
                else
                {
                    _lblBleState.Text = _blePairedMode
                        ? "○ 未连接：选中已配对设备后点【连接】（或双击条目）；未配对先用【系统配对】"
                        : "○ 未连接：点【扫描附近】开始扫描，选中后点【连接】";
                    _lblBleState.ForeColor = UiTheme.Danger;
                }
            }

            _btnBleConnect.Enabled = !connected && _lstBle.SelectedItem != null;
            _btnBleDisconnect.Enabled = connected;
            _btnBleRefresh.Enabled = _blePairedMode;
            MarkBleAttached(connected ? _ble.ConnectedDeviceId : null);
        }

        private void OnBleListDoubleClick(object sender, EventArgs e)
        {
            if (_uiBleConnected || _lstBle.SelectedItem == null)
                return;
            OnBleConnect(sender, e);
        }

        private void OnBleConnChanged(bool up)
        {
            if (IsHandleCreated == false)
                return;
            try
            {
                BeginInvoke(new Action(() =>
                {
                    if (!up)
                        _bleLinkDropped = true;   // 断开过：回连后必须重读固件版本
                    UpdateBleConnUi(up);
                    if (!up && _otaRunning)
                    {
                        // 升级中掉线 = 固件正在写 flash / 已重启，属预期，别报成异常
                        LogLine("BLE 连接状态：设备在升级过程中重启（预期现象，请稍候手动上电）", UiTheme.TextSub);
                    }
                    else
                    {
                        LogLine("BLE 连接状态：" + (up ? "已连接（往返校验通过）" : "已断开"),
                            up ? UiTheme.Success : UiTheme.Warn);
                    }
                    if (up)
                        ProbeBleFwVersionAfterReconnect();
                }));
            }
            catch (Exception) { }
        }

        /// <summary>2.5s 链路巡检：ATT 活性读不过就判未连接（与「蓝牙连接」页同一铁律）。</summary>
        private async void OnBleLinkTimer(object sender, EventArgs e)
        {
            if (IsDisposed || _bleVerifyBusy != 0)
                return;
            _bleVerifyBusy = 1;
            try
            {
                // 升级传输中不做活性巡检：设备写 flash 时 ATT 读会超时，误判掉线会掐断升级。
                // 真正的掉线会以写特征失败的形式在 OtaSession 里暴露。
                if (_otaRunning)
                    return;

                if (_ble.IsLinkUp)
                    await _ble.VerifyAsync(1200);

                // 本页判据用 IsOtaReady（链路 + OTA 特征 + 活性读），不能用 IsConnected ——
                // 后者额外要求 SPP 0x33F3/0x33F4，会让「连上但没 SPP」的设备每 2.5s 被判掉线。
                bool up = _ble.IsOtaReady;
                if (up == _uiBleConnected)
                {
                    // 常驻回连补读：断链标志还挂着（此前尝试失败）就随巡检周期再试，
                    // 直到读到/重试上限 —— 只在状态翻转时试一次会错过 GATT 就绪窗口
                    if (up && _bleLinkDropped && !_otaRunning)
                        ProbeBleFwVersionAfterReconnect();
                    return;
                }
                if (!up)
                    _bleLinkDropped = true;      // 巡检先发现掉线：同样标记，回连后重读版本

                UpdateBleConnUi(up);
                LogLine(up
                    ? "链路巡检：ATT 活性读通过 → 已连接。"
                    : "链路巡检：ATT 活性读失败 → 判定未连接。"
                      + (string.IsNullOrEmpty(_ble.LastDropReason) ? "" : "原因：" + _ble.LastDropReason),
                    up ? UiTheme.Success : UiTheme.Danger);
                if (up)
                    ProbeBleFwVersionAfterReconnect();
            }
            catch (Exception ex)
            {
                LogLine("链路巡检异常（" + ex.GetType().Name + "）：" + ex.Message, UiTheme.Warn);
            }
            finally
            {
                _bleVerifyBusy = 0;
            }
        }

        /// <summary>
        /// 回连后补读固件版本：BLE Direct 升级中设备重启后，回连走系统链路恢复
        /// （ConnectionChanged / 链路巡检），不经过手动【连接】的特征发现路径，
        /// PC 侧 GATT 视图还是旧固件的（无 DevInf）——ReadDevInfoAsync 内部会先重枚举。
        /// 断开过的链路必须重读一次：固件可能已换新，旧版本号不能再当现状显示；
        /// 读不到（链路未就绪/固件无 DevInf）保持静默。
        /// </summary>
        private async void ProbeBleFwVersionAfterReconnect()
        {
            if (IsDisposed || Interlocked.CompareExchange(ref _bleFwProbeBusy, 1, 0) != 0)
                return;
            try
            {
                await Task.Delay(1500);          // 回连瞬间 GATT 未就绪，退一拍再读
                /*目标条目定位：优先按连接设备的 AEP Id 精确命中已配对表 ——
                 *升级成功后无需用户选中对应行，版本号直接刷到那一行上；
                 *定位不到（扫描模式/Id 未命中）再退回当前选中项*/
                string connId = _ble.ConnectedDeviceId;
                BleDeviceInfo dev = null;
                if (!string.IsNullOrEmpty(connId))
                    _blePairedItems.TryGetValue(connId, out dev);
                bool byConnId = dev != null;
                if (dev == null)
                    dev = _lstBle.SelectedItem as BleDeviceInfo;
                if (dev == null)
                {
                    LogLine("回连补读版本跳过：无法定位设备条目（连接 Id 未命中已配对表且列表无选中项）",
                        UiTheme.TextSub);
                    return;
                }
                if (!byConnId && !string.IsNullOrEmpty(connId) && dev.DeviceId != null
                    && dev.DeviceId != connId)
                {
                    LogLine("回连补读版本跳过：选中项与连接设备 Id 不一致，conn="
                        + connId + " dev=" + dev.DeviceId, UiTheme.TextSub);
                    return;                      // 选中项与当前连接设备不一致，别张冠李戴
                }
                if (byConnId)
                {
                    int ci = _lstBle.Items.IndexOf(dev);
                    if (ci >= 0 && _lstBle.SelectedIndex != ci)
                        _lstBle.SelectedIndex = ci;   // 已接入的设备默认选中：状态栏直接带出它的版本
                }
                bool dropped = _bleLinkDropped;  // 断开过 → 强制重读
                if (!dropped && !string.IsNullOrEmpty(dev.FwVersion))
                    return;                      // 没断链且已有版本（手动连接刚读过）→ 正常静默
                if (dropped && ++_bleFwProbeTries > 4)
                {
                    _bleLinkDropped = false;     // 重试上限：多半是固件无 DevInf 特征，放弃
                    _bleFwProbeTries = 0;
                    LogLine("回连补读版本放弃（重试 4 次失败）：固件可能无 DevInf 特征", UiTheme.Warn);
                    return;
                }
                BleDevInf di = await _ble.ReadDevInfoAsync(1500);
                if (di == null)
                {
                    if (dropped)
                        LogLine("回连补读版本失败（第 " + _bleFwProbeTries + "/4 次）："
                            + (_ble.LastDevInfoNote ?? "未知原因"), UiTheme.TextSub);
                    return;                      // 失败：标志保留，随下一轮巡检再试
                }
                _bleLinkDropped = false;         // 读到才清标志
                _bleFwProbeTries = 0;
                string old = dev.FwVersion;
                bool changed = old != di.FwText;
                dev.FwVersion = di.FwText;
                int idx = _lstBle.Items.IndexOf(dev);
                if (idx >= 0)
                {
                    int sel = _lstBle.SelectedIndex;  // 刷新行不能把用户的选中打掉
                    _lstBle.Items.RemoveAt(idx);
                    _lstBle.Items.Insert(idx, dev);
                    _lstBle.SelectedIndex = sel;
                }
                LogLine("设备回连，GATT 读回鼠标固件版本：" + di.FwText
                    + (dropped && !string.IsNullOrEmpty(old)
                        ? (changed ? "（升级前 " + old + "）" : "（与升级前相同）")
                        : ""),
                    changed ? UiTheme.Success : UiTheme.TextSub);
                UpdateBleConnUi(true);           // 状态栏连带刷新「固件 vX.Y」后缀
            }
            catch (Exception) { }
            finally
            {
                _bleFwProbeBusy = 0;
            }
        }

        private async void OnBleConnect(object sender, EventArgs e)
        {
            var dev = _lstBle.SelectedItem as BleDeviceInfo;
            if (dev == null)
            {
                LogLine("请先在列表中选择一个设备。", UiTheme.Danger);
                return;
            }
            if (_bleBusyConnect)
            {
                LogLine("上一次连接还在进行中，请稍候…", UiTheme.Warn);
                return;
            }

            _bleBusyConnect = true;
            _btnBleConnect.Enabled = false;
            try
            {
                LogLine(string.Format("正在连接 {0}（{1}）…", dev.Name ?? "(未命名设备)", dev.Mac), UiTheme.TextSub);
                // 已配对条目走 AEP Id（不怕随机地址轮换）；扫描来的走地址 + 地址类型。
                // 第三参 requireSpp=false：本页只需要 OTA 写特征，SPP 0x33F3/0x33F4 缺失
                // 不应判成连接失败（8208 量产固件实测存在这种枚举结果）。
                string err = dev.IsPairedItem
                    ? await _ble.ConnectByIdAsync(dev.DeviceId, 10000, false)
                    : await _ble.ConnectAsync(dev.BluetoothAddress, dev.AddressType, false);
                if (err != null)
                {
                    LogLine("连接失败：" + err, UiTheme.Danger);
                    UpdateBleConnUi(false);
                    return;
                }
                LogLine("已连上并发现特征；链路活性校验：" + (_ble.LastLivenessNote ?? "真实空口读取成功"),
                    UiTheme.Success);
                if (string.IsNullOrEmpty(dev.FwVersion))
                    LogLine("设备固件版本：未知（广播里没有：已连系统后不再广播；"
                        + "GATT 里也没有：旧固件 OTA 服务下无 DevInf 特征 —— 升到带 DevInf 的固件后，"
                        + "连接时会自动 GATT 读回版本）", UiTheme.TextSub);
                else
                    LogLine("设备固件版本：" + dev.FwVersion, UiTheme.Success);
                if (!string.IsNullOrEmpty(_ble.LastSppCmdNote))
                    LogLine(_ble.LastSppCmdNote, UiTheme.TextSub);
                if (_ble.HasOtaChar)
                    LogLine("已发现 OTA 写特征（" + _ble.OtaCharUuid + "），可点【开始升级】。", UiTheme.Success);
                else
                    LogLine("未发现 OTA 写特征（" + BleUxProto.OtaWriteUuid + "）：" + _ble.OtaDiagNote, UiTheme.Warn);

                // 读取鼠标当前固件版本号（DevInf_aaa，GATT 只读，无需配对/加密/广播）。
                // 比广播 Manufacturer Data 解析更权威（已配对/未广播的设备也能读到），
                // 版本号字节布局见 BleDevInf 注释（实测在 idx 12-14，不是 Telink 模板注释的 6-8）。
                if (_ble.HasDevInfoChar)
                {
                    BleDevInf di = await _ble.ReadDevInfoAsync(1200);
                    if (di != null)
                    {
                        dev.FwVersion = di.FwText;
                        LogLine("GATT 读回鼠标固件版本：" + di.FwText, UiTheme.Success);
                    }
                    else
                    {
                        LogLine("已发现 DevInf 特征但读取失败（超时/空口不可达），暂用广播解析版本：" +
                                (string.IsNullOrEmpty(dev.FwVersion) ? "未知" : dev.FwVersion), UiTheme.Warn);
                    }
                }
                UpdateBleConnUi(true);
            }
            catch (Exception ex)
            {
                LogLine("连接异常（" + ex.GetType().Name + "）：" + ex.Message, UiTheme.Danger);
                UpdateBleConnUi(false);
            }
            finally
            {
                _bleBusyConnect = false;
                UpdateBleConnUi(_uiBleConnected);
            }
        }

        private async void OnBleDisconnect(object sender, EventArgs e)
        {
            _btnBleDisconnect.Enabled = false;
            LogLine("正在断开…", UiTheme.TextSub);
            string note = await _ble.DisconnectAsync();
            UpdateBleConnUi(false);
            foreach (var line in note.Split('；'))
            {
                string t = line.Trim();
                if (t.Length > 0)
                    LogLine(t + "；", UiTheme.TextSub);
            }
        }

        // ---------- 升级流程 ----------

        private void OnStart(object sender, EventArgs e)
        {
            lock (_runLock)
            {
                if (_cts != null)
                    return;
            }
            if (_fw == null)
            {
                LogLine("请先选择有效的固件 bin", UiTheme.Warn);
                return;
            }

            if (_radSelf.Checked)
            {
                StartSelfUpgrade();
                return;
            }

            IOtaChannel ch;
            int dataLen;
            if (_radUsb.Checked)
            {
                if (_dongle == null || !_dongle.IsConnected)
                {
                    LogLine("请先【连接 Dongle】", UiTheme.Warn);
                    return;
                }
                // 整映像模式（2026-10-20 透传重写）：固件经 USB 灌入 dongle，由 dongle 的
                // Telink master OTA 框架实时推给鼠标，鼠标固件零改动、dongle 不写 flash。
                // 鼠标需已上电并自动回连 dongle——BEGIN 时会校验 BLE 链路，未连接会明确报错
                // （PC 直连不算数）。
                LogLine("USB 通道为透传模式：请确认鼠标已上电并回连 Dongle（BEGIN 时会校验）。", UiTheme.TextSub);
                ch = new OtaChannelUsbDongle(_dongle, false);
                dataLen = OtaProto.PduDataLen;    // 整映像模式不使用逐 PDU 参数，占位
            }
            else
            {
                if (!_ble.IsLinkUp)
                {
                    LogLine("BLE 未连接：请先扫描并连接鼠标", UiTheme.Warn);
                    return;
                }
                if (!_ble.HasOtaChar)
                {
                    LogLine("未发现 OTA 写特征，无法走 BLE 通道：" + _ble.OtaDiagNote, UiTheme.Danger);
                    return;
                }
                ch = new OtaChannelBle(_ble);
                dataLen = OtaProto.PduDataLen;
            }

            LogLine("使用通道：" + ch.DisplayName, UiTheme.TextSub);
            // 结果等待：BLE 直连设备收完 END 立即重启、无回包，8s 只是兜底；
            // USB dongle 是轮询 dongle 的 master OTA 结果，推 79684B 要约 17~25s
            // （dongle 自身预算是 60s），必须放宽到 90s 才算数。
            int resultMs = _radUsb.Checked ? 90000 : 8000;
            // 发包节流（每 PDU 毫秒，仅逐 PDU 的 BLE 通道使用）：写特征无响应，队列塞满
            // 会静默丢包；按 4ms/包 + 每 8 包批校准，4981 包约 25~35s，落在固件 60s 预算内。
            int paceMs = _radUsb.Checked ? 0 : 4;
            var session = new OtaSession(_fw, ch, dataLen, false, resultMs, paceMs);
            session.Log += (msg, lvl) => UiLog(msg, lvl);
            session.Progress += (s, a) => UiProgress(a);
            session.Finished += (ok, msg) => UiFinished(ok, msg);

            _cts = new CancellationTokenSource();
            _otaRunning = true;                    // 停掉 ATT 巡检，避免误判掉线打断升级
            _btnStart.Enabled = false;
            _btnStop.Enabled = true;
            _prog.Value = 0;
            _lblProg.Text = "开始...";

            var ct = _cts.Token;
            var t = Task.Run(async () =>
            {
                string openErr = await ch.OpenAsync();
                if (openErr != null)
                {
                    UiLog("打开通道失败：" + openErr, 2);
                    UiFinished(false, openErr);
                    return;
                }
                await session.RunAsync(ct);
            });
            t.ContinueWith(prev =>
            {
                var ex = prev.Exception;
                if (ex != null)
                    UiLog("OTA 任务异常：" + ex.InnerException.Message, 2);
            }, TaskScheduler.Default);
        }

        private void OnStop(object sender, EventArgs e)
        {
            var cts = _cts;
            if (cts != null)
            {
                try { cts.Cancel(); }
                catch (Exception) { }
                LogLine("已请求停止", UiTheme.Warn);
            }
        }

        // ---------- Dongle 自身升级（host link 0x70..0x75）----------

        /// <summary>
        /// 入口：校验前置条件后把「上传+校验」丢到后台线程。
        /// COMMIT（不可逆那一步）不在这里做——由 RunSelfUpgrade 在校验通过后
        /// 切回 UI 线程弹确认框，用户点了才提交。
        /// </summary>
        private void StartSelfUpgrade()
        {
            if (_fw == null || _fw.Raw == null)
            {
                LogLine("请先选择有效的固件 bin", UiTheme.Warn);
                return;
            }
            if (_dongle == null || !_dongle.IsConnected)
            {
                LogLine("请先【连接 Dongle】", UiTheme.Warn);
                return;
            }
            string bad = DongleSelfOta.PreCheck(_fw.Raw);
            if (bad != null)
            {
                LogLine("固件预检未通过：" + bad, UiTheme.Danger);
                return;
            }

            _cts = new CancellationTokenSource();
            _btnStart.Enabled = false;
            _btnStop.Enabled = true;
            _prog.Value = 0;
            _lblProg.Text = "开始...";

            var ct = _cts.Token;
            var t = Task.Run(() => RunSelfUpgrade(ct));
            t.ContinueWith(prev =>
            {
                var ex = prev.Exception;
                if (ex != null)
                    UiLog("自身升级任务异常：" + ex.InnerException.Message, 2);
            }, TaskScheduler.Default);
        }

        private void RunSelfUpgrade(CancellationToken ct)
        {
            try
            {
                UiLog("Dongle Self 升级：镜像 " + _fw.Size + " 字节，CRC " + _fw.CrcHex +
                      "（上传与校验阶段运行中的固件不会被修改）", 0);

                int r = DongleSelfOta.Upload(_dongle, _fw.Raw, DongleSelfOta.DefaultPaceMs,
                                             SelfProgress, UiLog, ct);
                // 2026-10-22：Upload 约定 1=校验通过（可 COMMIT）；0=取消；-1=失败。
                // 历史坑：曾写成 if (r != 0) 把 1 也拦截——COMMIT 成死代码、dongle 永不重启。
                // 只有 r <= 0（取消/失败）才收尾返回；r == 1 继续 COMMIT。
                if (r <= 0)
                {
                    UiSelfFinished(false);
                    return;
                }

                // 2026-10-22：按用户要求去掉「是否现在切换」确认框，校验通过后直接
                // 自动 COMMIT（内部开发/产测工具，链路已多次验证：预擦 fail-closed、
                // CRC32 校验、失败/取消均不碰运行区；唯一不可逆点是 COMMIT 后的
                // 覆盖写入，中断仍需烧录器救——与有确认框时风险一致）。
                UiLog("镜像已写入备用 bank 并通过校验，自动切换到新固件（不可逆，请勿断电）…", 1);

                int c = DongleSelfOta.Commit(_dongle, UiLog);
                UiSelfFinished(c == 1, c);
            }
            catch (Exception ex)
            {
                UiLog("自身升级异常：" + ex.Message, 2);
                UiSelfFinished(false);
            }
        }

        private void SelfProgress(SelfOtaProgress p)
        {
            if (IsHandleCreated == false)
                return;
            try
            {
                BeginInvoke(new Action(() =>
                {
                    _prog.Value = Math.Max(0, Math.Min(100, p.Percent));
                    _lblProg.Text = string.Format("{0}  {1}%  已发 {2}/{3} 字节  {4:F0} B/s  剩余约 {5}",
                        p.Phase, p.Percent, p.Sent, p.Total, p.BytesPerSec,
                        p.EtaSeconds >= 0 ? p.EtaSeconds.ToString("F0") + "s" : "--");
                }));
            }
            catch (Exception) { }
        }

        /// <summary>收尾：恢复按钮态；提交成功时提示重新插拔。</summary>
        private void UiSelfFinished(bool ok, int commitResult)
        {
            UiSelfFinished(ok);
            if (commitResult == 0)
                UiLog("COMMIT 状态未知：请观察 Dongle 是否已重启（跑新固件会重新枚举 USB）。", 1);
        }

        private void UiSelfFinished(bool ok)
        {
            if (IsHandleCreated == false)
                return;
            try
            {
                BeginInvoke(new Action(() =>
                {
                    var cts = _cts;
                    _cts = null;
                    _btnStart.Enabled = true;
                    _btnStop.Enabled = false;
                    if (ok)
                    {
                        _lblProg.Text = "已提交：Dongle 正在覆盖运行区（约 20s，勿断电）";
                        // 旧连接随覆盖写入消失，主动断开让 UI 回到「未连接」，避免拿着失效句柄
                        try
                        {
                            if (_dongle != null)
                            {
                                _dongle.Disconnect();
                                _lblUsbState.Text = "Dongle 正在写入新固件，等它自动重启回来…";
                            }
                        }
                        catch (Exception) { }
                        // 2026-10-22：原地覆盖 COMMIT 后 dongle 会断开 USB、覆盖运行区、复位重启。
                        // PC 不会自动重连，日志静止 ≠ dongle 死了——上一轮用户就盯着静止日志
                        // 误判成「没重启」。这里禁用手动连接，开后台探测：USB 重新枚举时自动
                        // 接管连接、读 0x76 build 号并报告升级结果。
                        _btnUsbConnect.Enabled = false;
                        LogLine("COMMIT 已执行：Dongle 正在把新固件覆盖写入运行区（约 20 秒）。" +
                                "写入期间请勿拔插断电；完成后会自动重新连接并验证固件版本，无需手动操作。", UiTheme.Success);
                        StartRebootWatch();
                    }
                    else
                    {
                        _lblProg.Text = "未切换";
                    }
                }));
            }
            catch (Exception) { }
        }

        /// <summary>COMMIT 后台等待：dongle 覆盖 + 复位后 USB 重新枚举，自动探测、自动
        /// 接管连接并读 0x76 build 号，把「升级是否真的落地 + 自动重启是否发生」直接
        /// 报告到日志。等待期手动连接按钮被禁用，避免与探测竞态。</summary>
        private void StartRebootWatch()
        {
            System.Threading.Tasks.Task.Run(() =>
            {
                var sw = System.Diagnostics.Stopwatch.StartNew();
                while (sw.ElapsedMilliseconds < 75000)
                {
                    System.Threading.Thread.Sleep(2500);
                    if (IsDisposed || IsHandleCreated == false)
                        return;
                    var probe = new DongleLink();
                    string perr = probe.Connect();
                    if (perr != null)
                        continue;                       //USB 还没回来（枚举不到），继续等

                    //USB 回来了：读 0x76 确认正在跑的固件，然后把这条连接交给 UI。
                    //2026-10-22 补充：dongle 复位后 Windows 枚举有个半就绪窗口（能
                    //open 但 HID 驱动栈未完成启动，实测 0x63→0x64 复位后 2.5s 时
                    //0x76 写入报 gle=433、后续全 1167）——此时把连接交给 UI 只会拿到
                    //一个写不动的坏句柄。所以查询未真正应答就丢弃这条连接继续等，
                    //直到 0x76 真正往返成功才移交（0x62→0x63 那次 5s 等待恰好
                    //跨过了半就绪窗口，掩盖了这个问题）。
                    uint vHdr, vBuild;
                    int vr = DongleSelfOta.QuerySelfVersion(probe, DongleLink.CommandTimeoutMs,
                                                            out vHdr, out vBuild, out perr);
                    if (vr != 1)
                    {
                        try { probe.Disconnect(); } catch (Exception) { }
                        continue;
                    }
                    int waited = (int)sw.ElapsedMilliseconds / 1000;
                    BeginInvoke(new Action(() =>
                    {
                        if (IsDisposed)
                        {
                            try { probe.Disconnect(); } catch (Exception) { }
                            return;
                        }
                        if (_dongle != null && _dongle != probe)
                        {
                            try { _dongle.Disconnect(); } catch (Exception) { }
                        }
                        _dongle = probe;
                        UiLog(string.Format(
                            "Dongle 已自动重启回来（等待 {0}s），运行 build 0x{1:X8}（bin 头版本 0x{2:X8}）" +
                            "——USB 断开又自动回来 = 覆盖 + 复位成功，自动重启打通！", waited, vBuild, vHdr),
                            3);
                        SafeConnectUi("已连接 Dongle（升级完成，自动重连）");
                        _btnUsbConnect.Enabled = true;
                    }));
                    return;
                }
                //超时：覆盖阶段被中断或 dongle 半死，交还给用户处理
                BeginInvoke(new Action(() =>
                {
                    if (IsDisposed)
                        return;
                    _btnUsbConnect.Enabled = true;
                    _lblUsbState.Text = "未等到 Dongle 重启";
                    UiLog("75 秒内未等到 Dongle 的 USB 重新枚举。请拔插一次再点【连接 Dongle】；" +
                          "若拔插后设备管理器里也没有 2657:5862，说明覆盖写入被中断，需用 Telink 烧录器恢复。",
                          2);
                }));
            });
        }

        // ---------- 后台事件 → UI ----------

        private void UiLog(string msg, int level)
        {
            if (IsHandleCreated == false)
                return;
            try
            {
                Color c = level == 2 ? UiTheme.Danger : level == 1 ? UiTheme.Warn
                        : level == 3 ? UiTheme.Success : UiTheme.TextMain;
                BeginInvoke(new Action(() => LogLine(msg, c)));
            }
            catch (Exception) { }
        }

        private void UiProgress(OtaProgressEventArgs a)
        {
            if (IsHandleCreated == false)
                return;
            try
            {
                BeginInvoke(new Action(() =>
                {
                    _prog.Value = Math.Max(0, Math.Min(100, a.Percent));
                    _lblProg.Text = string.Format("{0}%  已发 {1}/{2} PDU  {3:F0} B/s  剩余约 {4}",
                        a.Percent, a.Sent, a.Total, a.BytesPerSec,
                        a.EtaSeconds >= 0 ? a.EtaSeconds + "s" : "--");
                }));
            }
            catch (Exception) { }
        }

        private void UiFinished(bool ok, string msg)
        {
            if (IsHandleCreated == false)
                return;
            try
            {
                BeginInvoke(new Action(() =>
                {
                    lock (_runLock) { _cts = null; }
                    _otaRunning = false;           // 恢复 ATT 巡检（设备已重启的话会被判定掉线）
                    _btnStart.Enabled = true;
                    _btnStop.Enabled = false;
                    _lblProg.Text = ok ? "完成：" + msg : "失败：" + msg;
                    LogLine(ok ? "OTA 完成：" + msg : "OTA 失败：" + msg, ok ? UiTheme.Success : UiTheme.Danger);
                }));
            }
            catch (Exception) { }
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            var cts = _cts;
            if (cts != null)
            {
                try { cts.Cancel(); } catch (Exception) { }
            }
            if (_dongle != null)
            {
                try { _dongle.Dispose(); } catch (Exception) { }
            }
            _ble.Dispose();
            base.OnFormClosed(e);
        }
    }
}
