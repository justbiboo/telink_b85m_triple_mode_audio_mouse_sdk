using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using NAudio.Wave;

namespace BibooUxTool
{
    /// <summary>
    /// 翻译模块（框架版）：两种工作模式，共用同一条「音频 → 识别 → 翻译 → 字幕」链路。
    ///   ① 按录音（默认）：点【开始录音】累积 PCM，点【停止并翻译】把整段一次送模型——
    ///      不依赖 VAD 断句，结果稳定，适合当前阶段。录音同时存成 WAV（exe 同目录 records\）。
    ///   ② 实时字幕（勾选框）：USB Audio 采集 → VAD 分段 → 逐段送引擎，边说边出字幕。
    /// 当前引擎为桩实现（不加载模型），先把「采集→分段→识别→翻译→字幕」链路与 UI 搭好；
    /// 后续接 Whisper.net（ASR）+ Argos/OPUS-MT（MT）时只换 ITranslationEngine 实现，UI 不动。
    /// 音频源二选一：USB Audio（鼠标当 USB 声卡 / 普通麦克风均走标准 WaveIn），
    /// 或「蓝牙输入（BLE 直连）」—— 经语音流特征 0xFFF0/0xB03E 收 57B mSBC 帧，
    /// MsbcDecoder 解成 240B PCM 后喂同一 VAD/识别/翻译管线（与 SDK ux_voice_dec 同口径）。
    /// 鼠标语音键（K4）自动录音：两个触发场景自动选择（鼠标 BLE 链路独占，天然互斥）——
    ///   ① 鼠标连 Dongle：K4 边沿经 SPP 0x33F4 → dongle 0xE3 → DongleLink，自动开始/停止 USB 录音；
    ///   ② PC 蓝牙直连鼠标：K4 边沿直接从 0x33F4 到 BleDirectLink.SppReceived，自动启用/停用蓝牙语音。
    /// </summary>
    public sealed class TranslatorForm : Form
    {
        // ---------- 音频输入 ----------
        private ComboBox _cmbDevice;
        private GradientButton _btnRefreshDev;
        private GradientButton _btnStart;
        private GradientButton _btnStop;
        private ProgressBar _barLevel;
        private Label _lblLevel;

        // ---------- 翻译引擎 ----------
        private LangItem _srcLang;
        private LangItem _dstLang;
        private GradientButton _btnSrcLang;
        private GradientButton _btnDstLang;
        private GradientButton _btnSwap;
        private ComboBox _cmbEngine;
        private ComboBox _cmbModel;
        private Label _lblEngineNote;
        private Label _lblWarmup;          // 预热状态（进页面即后台加载模型 / 拉起本地服务）

        // ---------- 预热（进入页面就把模型准备好，避免首次翻译干等） ----------
        private readonly object _warmGate = new object();
        private ITranslationEngine _warmEngine;   // 已预热/正在预热的引擎
        private Task<bool> _warmTask;             // 该引擎的预热任务（失败已吞成 false）
        private bool _shown;                      // 窗口已显示：之后才允许触发预热

        // ---------- 实时字幕 ----------
        private ListView _lstSubtitle;
        private Label _lblStats;

        // ---------- 日志 ----------
        private RichTextBox _txtLog;
        private GradientButton _btnClearLog;      // 清空日志（右上角，与标题同行）

        // ---------- pipeline ----------
        private readonly UsbAudioCapture _capture = new UsbAudioCapture();
        private readonly SimpleVad _vad = new SimpleVad();
        private ITranslationEngine _engine;
        private bool _running;

        // ---------- 录音模式（默认）：按下开始 → 累积 PCM → 停止后整段送模型 ----------
        private CheckBox _chkRealtime;      // 勾上才走旧的「VAD 自动断句实时字幕」

        // ---------- 鼠标语音键（K4）自动录音 ----------
        // 硬件端 BLE 模式下按 K4：开麦推流 + 发 SPP 0x23 按下事件；松开停麦 + 发松开事件。
        // 本页经 Dongle Host Link 收这两个边沿：按下 → 自动开始录音；松开 → 停止 → 翻译 → 朗读。
        private CheckBox _chkPtt;           // 勾上才启用鼠标语音键自动录音
        private Label _lblPtt;              // 监听/录音状态行
        // K4 语音键自动录音（2026-10-23 起私有协议版）：鼠标 SPP 0x33F4 发 0x23 边沿事件
        // → dongle 转为 0xE3 分片 → 本页 DongleLink 重组 → 开始/停止录音。
        // 替代已取消的 K5 Ctrl+Win/Enter HID 键值触发（KeyboardPttWatcher 已退役，类保留）。
        private DongleLink _pttLink;                        // dongle Host Link（K4 事件通道，独占）
        private System.Windows.Forms.Timer _pttLinkTimer;   // 50ms Pump 排空 + 断线重连
        private int _pttLinkRetryTick;                      // 上次重连尝试（TickCount）
        private bool _pttRecording;         // 当前这段录音是否由语音键触发（决定松开 K4 是否收尾）
        private bool _pttFromBle;           // 本次语音键录音由 BLE 直连触发（dongle 断链兜底不误伤）

        // 译文朗读（TTS：sherpa-onnx Kokoro int8，中 + 英，103 音色）
        private TtsEngine _tts;
        private readonly SynchronizationContext _uiCtx;  // 构造时抓 UI 线程上下文，后台线程回调用
        private bool _ttsWarmStarted;
        private CheckBox _chkSpeak;          // 勾上才朗读译文
        private ComboBox _cmbOut;            // 输出设备（NAudio WaveOut）
        private TextBox _numSid;             // 音色 id（0-102）
        private bool _sidUserSet;            // 用户是否手动改过音色（改过就不再按语言自动选）
        private Label _lblTts;               // TTS 状态
        private GradientButton _btnTestTts;  // 试听一句
        private Label _lblMode;
        private readonly object _recGate = new object();
        private readonly List<byte> _recBuf = new List<byte>();
        private long _recBytes;             // 已录字节数（16bit/mono → /2/16000 = 秒）
        private bool _busy;                 // 已停止录音，但整段仍在识别/翻译
        private float _lastRms;
        private double _lastRecSec;
        private double _lastRecUiSec;      // 仅音频回调线程使用：录音时长 UI 刷新节流
        private const int MaxRecordSeconds = 300;   // 单次录音上限，防内存无限增长
        private const double MinRecordSeconds = 0.2; // 短于此视为误触，不送模型

        // ---------- 蓝牙输入（BLE 直连 · 配对连接）----------
        // 第一步只做「配对/连接」：交互与判据完全对齐「固件升级」页 OtaForm 的 BLE Direct 子面板
        // 与「蓝牙连接」页 BleScannerForm —— 已配对设备列表 → 选中【连接】/双击回连 →
        // GattSession 维持 + 2.5s ATT 活性巡检揪假连接 → 断开/系统配对/扫描附近旁路。
        // 第二步（已落地）：连上后经语音流特征 0xFFF0/0xB03E 收 57B mSBC 帧 → MsbcDecoder 解成
        // 240B PCM（120 样本 @16kHz/mono）→ 直接喂 OnAudioData 进现有 VAD/识别/翻译管线。
        // 音源二选一（用户 2026-10-22 确认）：蓝牙语音启用时停 USB Audio 采集，反之亦然。
        private readonly BleDirectLink _ble = new BleDirectLink();
        private readonly MsbcDecoder _msbc = new MsbcDecoder();  // 57B mSBC → 240B PCM；会话内状态跨帧保持
        private volatile bool _bleVoiceActive;   // 蓝牙语音是否为当前音源（二选一）
        private int _bleVoiceFrames;             // 已入管 PCM 帧数（状态行/日志统计）
        private int _bleVoiceErr;                // 解码失败/帧长异常计数（限流日志）
        private ListBox _lstBle;
        private Label _lblBleState;
        private GradientButton _btnBleModePaired;
        private GradientButton _btnBleModeScan;
        private GradientButton _btnBleRefresh;
        private GradientButton _btnBleSysPair;
        private GradientButton _btnBleConnect;
        private GradientButton _btnBleDisconnect;
        private GradientButton _btnBleVoiceOn;   // 启用蓝牙语音（订阅 0xB03E notify）
        private GradientButton _btnBleVoiceOff;  // 停用蓝牙语音（退订，USB 音频随即可用）
        private readonly Dictionary<string, BleDeviceInfo> _blePairedItems = new Dictionary<string, BleDeviceInfo>();
        private readonly Dictionary<ulong, BleDeviceInfo> _bleFound = new Dictionary<ulong, BleDeviceInfo>();
        private bool _blePairedMode = true;      // true = 系统已配对设备（默认，免扫描）；false = 扫描附近
        private bool _uiBleConnected;            // UI 口径的连接态（只由 ATT 活性校验给出）
        private bool _bleBusyConnect;            // 连接进行中（防重入）
        private int _bleVerifyBusy;              // 活性巡检重入保护
        private System.Windows.Forms.Timer _bleLinkTimer;   // 2.5s 一次 ATT 活性读，揪出假连接

        /// <summary>语言选项：中文名（Name，列表主行）+ 母语名（Native，列表副行）+ 引擎用 Code（BCP-47 风格）。
        /// 面板可搜索（中文名 / 母语名 / Code 均可命中），双语名对照显示。</summary>
        private sealed class LangItem : IEquatable<LangItem>
        {
            public string Code { get; }
            public string Name { get; }   // 中文名（列表主行）
            public string Native { get; } // 母语名（列表副行）
            public LangItem(string code, string name, string native)
            {
                Code = code;
                Name = name;
                Native = native;
            }
            public override string ToString() => Name;
            public bool Equals(LangItem other) => other != null && other.Code == Code;
            public override bool Equals(object obj) => Equals(obj as LangItem);
            public override int GetHashCode() => Code.GetHashCode();
        }

        // 严格对齐混元 Hy-MT2 官方支持的 38 种语言：界面里多给一项，翻译时引擎就会拒绝
        // （日志报「混元翻译不支持目标语种」）。换用支持更多语种的引擎时，按 Code 另建一份表格即可。
        private static readonly LangItem[] LangItems = new[]
        {
            new LangItem("zh",    "中文（简体）", "中文简体"),
            new LangItem("zh-TW", "中文（繁体）", "中文繁體"),
            new LangItem("yue",   "粤语",       "粵語"),
            new LangItem("en",    "英语",       "English"),
            new LangItem("ja",    "日语",       "日本語"),
            new LangItem("ko",    "韩语",       "한국어"),
            new LangItem("fr",    "法语",       "Français"),
            new LangItem("de",    "德语",       "Deutsch"),
            new LangItem("es",    "西班牙语",     "Español"),
            new LangItem("pt",    "葡萄牙语",     "Português"),
            new LangItem("ru",    "俄语",       "Русский"),
            new LangItem("it",    "意大利语",     "Italiano"),
            new LangItem("ar",    "阿拉伯语",     "العربية"),
            new LangItem("th",    "泰语",       "ไทย"),
            new LangItem("vi",    "越南语",      "Tiếng Việt"),
            new LangItem("id",    "印度尼西亚语",   "Bahasa Indonesia"),
            new LangItem("ms",    "马来语",      "Bahasa Melayu"),
            new LangItem("tl",    "菲律宾语",     "Filipino"),
            new LangItem("hi",    "印地语",      "हिन्दी"),
            new LangItem("bn",    "孟加拉语",     "বাংলা"),
            new LangItem("ur",    "乌尔都语",     "اردو"),
            new LangItem("fa",    "波斯语",      "فارسی"),
            new LangItem("tr",    "土耳其语",     "Türkçe"),
            new LangItem("nl",    "荷兰语",      "Nederlands"),
            new LangItem("pl",    "波兰语",      "Polski"),
            new LangItem("cs",    "捷克语",      "Čeština"),
            new LangItem("uk",    "乌克兰语",     "Українська"),
            new LangItem("he",    "希伯来语",     "עברית"),
            new LangItem("ta",    "泰米尔语",     "தமிழ்"),
            new LangItem("te",    "泰卢固语",     "తెలుగు"),
            new LangItem("mr",    "马拉地语",     "मराठी"),
            new LangItem("gu",    "古吉拉特语",    "ગુજરાતી"),
            new LangItem("km",    "高棉语",      "ខ្មែរ"),
            new LangItem("my",    "缅甸语",      "မြန်မာ"),
            new LangItem("bo",    "藏语",       "བོད་སྐད"),
            new LangItem("kk",    "哈萨克语",     "Қазақ"),
            new LangItem("mn",    "蒙古语",      "Монгол"),
            new LangItem("ug",    "维吾尔语",     "ئۇيغۇرچە"),
        };

        /// <summary>按 Code 取语言项（大小写不敏感）；找不到返回 null。默认语言用它定位，避免改表顺序后索引错位。</summary>
        private static LangItem FindLang(string code)
        {
            if (string.IsNullOrEmpty(code)) return null;
            foreach (var it in LangItems)
                if (string.Equals(it.Code, code, StringComparison.OrdinalIgnoreCase)) return it;
            return null;
        }

        // 段队列 + 单消费泵：WaveIn 回调线程只入队，处理在后台线程串行执行，
        // 避免高语速时多段并发打进引擎（也避免 UI 线程被阻塞）。
        private readonly object _queueGate = new object();
        private readonly Queue<short[]> _pendingPcm = new Queue<short[]>();
        private readonly Queue<double> _pendingSec = new Queue<double>();
        private bool _pumping;

        // 统计
        private int _segmentCount;
        private double _audioSeconds;
        private long _lastCostMs;
        private double _avgCostMs;

        /// <summary>内容列卡片（位置与尺寸由 LayoutContent 统一管理：随窗口宽高自适应）。</summary>
        private readonly List<RoundedPanel> _contentCards = new List<RoundedPanel>();

        // 四张卡片的直接引用：LayoutContent 需要按顺序做纵向重排（字幕区/日志区吸收多余高度）
        private RoundedPanel _cardAudio;
        private RoundedPanel _cardBle;
        private RoundedPanel _cardEngine;
        private RoundedPanel _cardSub;
        private RoundedPanel _cardLog;

        // 布局常量：卡片外边距与纵向间隙
        private const int CardMargin = 12;
        private const int CardGap = 12;
        private const int AudioCardHeight = 204;   // 176（原「朗读译文」行）+ 28（鼠标语音键自动录音行）

        /// <summary>蓝牙输入卡高度：标题(30) + 模式按钮行(34) + 状态行(18) + 设备列表(84) + 连接/断开
        /// + 启用/停用语音两枚按钮 + 内边距（第二步加语音开关，180 → 212）。</summary>
        private const int BleCardHeight = 212;

        /// <summary>暂时屏蔽引擎卡里的两行说明（用户红框圈出的「预热状态」+「引擎推荐说明」）。
        /// 改回 true 即两行恢复显示，引擎卡高度也会自动还原，无需再改别处。</summary>
        private static readonly bool ShowEngineHints = false;

        /// <summary>引擎/模型两个下拉是否显示。false = 界面上不暴露，但**引擎选型逻辑照常执行**
        /// （PopulateEngines / RefreshModels 仍按 TranslationConfig 落到「SenseVoice + 混元 Hy-MT2」），
        /// 用户看不到也改不了，等于锁定默认配置。改回 true 即恢复下拉，卡片高度自动还原。</summary>
        private static readonly bool ShowEngineSelectors = false;

        /// <summary>引擎卡高度按可见行数累加：标题+语言行 74、引擎行 +30、说明两行 +62。
        /// 当前（两个开关都关）74；只开下拉 104；全开 166。</summary>
        private static readonly int EngineCardHeight =
            74 + (ShowEngineSelectors ? 30 : 0) + (ShowEngineHints ? 62 : 0);

        /// <summary>内容列设计宽度（比设计基准略宽：字幕表需要横向空间）。</summary>
        private const int ContentDesignWidth = 780;

        public TranslatorForm()
        {
            _uiCtx = SynchronizationContext.Current;   // 后台线程回调要靠它切回 UI
            BuildUi();
            RefreshDevices();
            PopulateEngines();
            RefreshModels();
            LogLine("翻译模块就绪（当前为「按录音」模式）：选音频输入设备 →【开始录音】，"
                  + "说完点【停止并翻译】，整段 PCM 一次送模型；想边说边出字幕就勾上「实时字幕」。",
                UiTheme.TextSub);
            LogLine("引擎下拉含：桩（仅验证链路）/ SenseVoice+混元 Hy-MT2（33 语种）/ SenseVoice+OPUS-MT（中↔英）/ "
                  + "SenseVoice ASR / Whisper ASR / Whisper 直译→英语 / Whisper+Argos。", UiTheme.TextSub);
            LogLine("混元需 Models\\llm\\Hy-MT2-1.8B-Q4_K_M.gguf + llama\\llama-server.exe（首次翻译会自动拉起，全离线）；"
                  + "Whisper 需 Models\\ggml-base.bin；Argos MT 需本机起本地服务（见 TranslationConfig）。", UiTheme.TextSub);
            LogLine("新增「蓝牙输入（BLE 直连 · 配对连接）」卡：已配对设备 →【连接】/双击回连，"
                  + "或【扫描附近】直连未配对设备；未配对先用【系统配对】。"
                  + "连上后点【启用语音】即把鼠标 mSBC 语音流（57B/帧）解成 PCM 送入本管线，"
                  + "与 USB 音频采集二选一。", UiTheme.TextSub);
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            _running = false;
            _capture.DataAvailable -= OnAudioData;
            _capture.Failed -= OnCaptureFailed;
            _vad.SegmentReady -= OnSegmentReady;
            _vad.LevelUpdated -= OnLevelUpdated;
            _capture.Dispose();
            if (_tts != null) { _tts.Stop(); _tts.Dispose(); _tts = null; }
            // K4 语音键通道：停 Pump → 摘事件 → 释放 dongle HID 句柄（与其它模态页互斥的独占通道）
            try { if (_pttLinkTimer != null) _pttLinkTimer.Stop(); } catch (Exception) { }
            if (_pttLink != null)
            {
                try { _pttLink.SppFrameEvent -= OnPttSppFrame; } catch (Exception) { }
                _pttLink.Dispose();
                _pttLink = null;
            }
            // 蓝牙输入：停巡检 → 摘语音/ SPP 事件 → 关 notify（含 0xB03E CCCD）+ 释放 PC 侧对象 → Dispose
            try { if (_bleLinkTimer != null) _bleLinkTimer.Stop(); } catch (Exception) { }
            try { _ble.VoiceReceived -= OnBleVoiceFrame; } catch (Exception) { }
            try { _ble.SppReceived -= OnBleSppFrame; } catch (Exception) { }
            _bleVoiceActive = false;
            _running = false;
            try { _ble.Disconnect(); } catch (Exception) { }
            try { _ble.Dispose(); } catch (Exception) { }
            // 关窗口时收掉我们自己拉起的 llama-server（只杀本进程启动的那个，用户手起的不动）
            LlmServerLauncher.Stop();
            base.OnFormClosed(e);
        }

        // ---------- 蓝牙输入：BLE 直连配对/连接（交互与判据对齐「固件升级」页 BLE Direct 面板）----------

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
        /// 【系统配对】：一键打开 Windows「蓝牙和设备」设置页（与 OTA /「蓝牙连接」页同一做法）。
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

        /// <summary>已配对模式下：把扫描到的固件版本补到同 MAC 的已配对行上。
        /// 版本号只在广播 Manufacturer Data 里，AEP 列表拿不到，故连接前跑一次短扫描。</summary>
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

        /// <summary>连接态 UI。只认 ATT 活性校验结果（BleDirectLink.IsLinkVerified），不给假连接。</summary>
        private void UpdateBleConnUi(bool connected)
        {
            _uiBleConnected = connected;
            if (_lblBleState == null)
                return;

            if (connected)
            {
                if (_bleVoiceActive)
                    _lblBleState.Text = "● 已连接；蓝牙语音采集中（mSBC 57B → PCM 240B 已入翻译管线，"
                                     + "与 USB 音频二选一）";
                else if (_ble.HasVoiceChar)
                    _lblBleState.Text = "● 已连接（ATT 活性校验通过）；语音通道就绪，点【启用语音】接入翻译管线";
                else
                    _lblBleState.Text = "● 已连接（ATT 活性校验通过）；未发现语音特征 0xFFF0/0xB03E"
                                     + "（该固件不支持 BLE 语音输入，SPP/OTA 不受影响）";
                _lblBleState.ForeColor = UiTheme.Success;
            }
            else
            {
                // 掉线/手动断开：蓝牙语音随链路一起收尾（不停 USB——它本来就没开）。
                // K4 触发的录音正按住时断链：松开边沿随链路一起丢（到不了 0x33F4），
                // 走 HandlePttReleased → OnStop 提前收尾（整段送模型 + 统一落状态），
                // 与 dongle 通道断链兜底（OnPttLinkTimer）对称，防 300s 挂起。
                // （订阅失败也是 _pttRecording 挂 true 的一种来路，故不判 _bleVoiceActive；
                //   HandlePttReleased 内部有 _running 门槛，不会误碰没在跑的采集。）
                if (_pttRecording && _pttFromBle)
                {
                    LogLine("自动录音：蓝牙链路断开，本段录音提前收尾。", UiTheme.Warn);
                    HandlePttReleased();
                }
                else if (_bleVoiceActive)
                    StopBleVoiceSync("BLE 链路断开");

                _lblBleState.Text = _blePairedMode
                    ? "○ 未连接：选中已配对设备后点【连接】（或双击条目）；未配对先用【系统配对】"
                    : "○ 未连接：点【扫描附近】开始扫描，选中后点【连接】";
                _lblBleState.ForeColor = UiTheme.Danger;
            }

            _btnBleConnect.Enabled = !connected && _lstBle.SelectedItem != null;
            _btnBleDisconnect.Enabled = connected;
            _btnBleRefresh.Enabled = _blePairedMode;
            _btnBleVoiceOn.Enabled = connected && !_bleVoiceActive && _ble.HasVoiceChar;
            _btnBleVoiceOff.Enabled = _bleVoiceActive;
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
                    UpdateBleConnUi(up);
                    LogLine("BLE 连接状态：" + (up ? "已连接（往返校验通过）" : "已断开"),
                        up ? UiTheme.Success : UiTheme.Warn);
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
                if (_ble.IsLinkUp)
                    await _ble.VerifyAsync(1200);

                // 判据用 IsLinkVerified（只要求链路 + 活性读）：本页后续语音流走 0xFFF0/0xB03E，
                // 与 SPP 0x33F3/0x33F4、OTA 特征都无关，不能因某个特征缺失每 2.5s 被判掉线。
                bool up = _ble.IsLinkVerified;
                if (up == _uiBleConnected)
                    return;

                UpdateBleConnUi(up);
                LogLine(up
                    ? "链路巡检：ATT 活性读通过 → 已连接。"
                    : "链路巡检：ATT 活性读失败 → 判定未连接。"
                      + (string.IsNullOrEmpty(_ble.LastDropReason) ? "" : "原因：" + _ble.LastDropReason),
                    up ? UiTheme.Success : UiTheme.Danger);
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
                // 第三参 requireSpp=false：本页后续只收语音流特征，SPP 缺失不应判成连接失败
                // （8208 量产固件实测存在枚举不到 SPP 服务的情况）。
                string err = dev.IsPairedItem
                    ? await _ble.ConnectByIdAsync(dev.DeviceId, 10000, false)
                    : await _ble.ConnectAsync(dev.BluetoothAddress, dev.AddressType, false);
                if (err != null)
                {
                    LogLine("连接失败：" + err, UiTheme.Danger);
                    UpdateBleConnUi(false);
                    return;
                }
                LogLine("已连上；链路活性校验：" + (_ble.LastLivenessNote ?? "真实空口读取成功"),
                    UiTheme.Success);
                if (string.IsNullOrEmpty(dev.FwVersion))
                    LogLine("设备固件版本：未知（已配对设备已连系统后不再广播；"
                        + "可切「扫描附近」看能否补到）", UiTheme.TextSub);
                else
                    LogLine("设备固件版本：" + dev.FwVersion, UiTheme.Success);
                if (!string.IsNullOrEmpty(_ble.LastSppCmdNote))
                    LogLine(_ble.LastSppCmdNote, UiTheme.TextSub);
                // 读鼠标当前固件版本号（DevInf_aaa，GATT 只读，无需配对/加密/广播即可读）
                if (_ble.HasDevInfoChar)
                {
                    BleDevInf di = await _ble.ReadDevInfoAsync(1200);
                    if (di != null)
                    {
                        dev.FwVersion = di.FwText;
                        LogLine("GATT 读回鼠标固件版本：" + di.FwText, UiTheme.Success);
                    }
                }
                if (_ble.HasVoiceChar)
                    LogLine("语音流特征就绪（0xFFF0/0xB03E notify，57B mSBC/帧）：点【启用语音】"
                          + "即把蓝牙语音接入翻译管线（与 USB 音频二选一）。", UiTheme.Success);
                else
                    LogLine("未发现语音流特征（0xFFF0/0xB03E）：该设备/固件不支持 BLE 语音输入，"
                          + "只能继续用 USB 音频采集（SPP/OTA 不受影响）。", UiTheme.Warn);
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

        private void OnBleDisconnect(object sender, EventArgs e)
        {
            _btnBleDisconnect.Enabled = false;
            LogLine("正在断开…", UiTheme.TextSub);
            _ = DisconnectBleAsync();
        }

        private async Task DisconnectBleAsync()
        {
            try
            {
                string note = await _ble.DisconnectAsync();
                UpdateBleConnUi(false);
                foreach (var line in note.Split('；'))
                {
                    string t = line.Trim();
                    if (t.Length > 0)
                        LogLine(t + "；", UiTheme.TextSub);
                }
            }
            catch (Exception ex)
            {
                LogLine("断开异常（" + ex.GetType().Name + "）：" + ex.Message, UiTheme.Warn);
                UpdateBleConnUi(false);
            }
        }

        // ---------- 蓝牙语音（第二步）：0xFFF0/0xB03E mSBC → PCM → 翻译管线 ----------
        // 与 SDK 口径对齐（vendor/user/voice_dec/ux_voice_dec.c）：鼠标每 7.5ms 推一帧 57B mSBC，
        // 解出 120 样本（240B）16kHz/mono/16bit PCM；notify 边界即帧边界（MTU 158 下单帧不分片）。
        // 音源二选一：本路径启用时 USB 采集必须停（UpdateButtons 已把【开始采集】/设备下拉禁用），
        // 反之 USB 采声中点【启用语音】会被拒并提示，不会出现两个音源同时喂 VAD。

        /// <summary>【启用语音】：订阅 0xB03E notify，把蓝牙语音设为当前音源。</summary>
        private async void OnBleVoiceOn(object sender, EventArgs e)
        {
            await TryStartBleVoiceAsync();
        }

        /// <summary>
        /// 启用蓝牙语音的核心异步路径（可等待）：前置检查 → CCCD 订阅 → 接管线。
        /// 返回是否启用成功（含“已在采集”视为成功，幂等）。
        /// K4 自动录音也走这里：await 返回后可判“松开沿是否已先到”。
        /// </summary>
        private async Task<bool> TryStartBleVoiceAsync()
        {
            if (_bleVoiceActive)
                return true;
            if (!_ble.IsLinkVerified)
            {
                LogLine("BLE 未连接（或活性校验未过），无法启用语音。", UiTheme.Danger);
                return false;
            }
            if (_running)
            {
                LogLine("USB 音频采集中：输入源二选一，请先点【停止采集】再启用蓝牙语音。", UiTheme.Warn);
                return false;
            }

            string err = await _ble.StartVoiceAsync();
            if (err != null)
            {
                LogLine("启用蓝牙语音失败：" + err, UiTheme.Danger);
                return false;
            }
            StartBleVoiceSync();
            return true;
        }

        /// <summary>【停用语音】：与【停止采集】同一条收尾路径（录音模式下照样送模型）。</summary>
        private void OnBleVoiceOff(object sender, EventArgs e)
        {
            if (!_bleVoiceActive)
                return;
            OnStop(this, EventArgs.Empty);
        }

        /// <summary>
        /// 启用蓝牙语音（同步部分）：重置解码器会话状态 → 接管 _running → 挂 VAD 事件。
        /// 与 OnStart 的 USB 分支完全对称，保证两种音源走同一条 VAD/字幕/录音管线。
        /// </summary>
        private void StartBleVoiceSync()
        {
            _msbc.Reset();               // 新会话：合成滤波器历史(V)/offset 清零，否则音质撕裂
            _bleVoiceActive = true;
            _bleVoiceFrames = 0;
            _bleVoiceErr = 0;
            _lastRms = 0f;
            _lastRecSec = 0;
            _lastRecUiSec = 0;
            lock (_recGate)
            {
                _recBuf.Clear();
                _recBytes = 0;
            }

            bool rt = IsRealtime();
            _running = true;
            _vad.LevelUpdated += OnLevelUpdated;          // 两种模式都用它刷电平条
            _vad.Reset();
            if (rt)
                _vad.SegmentReady += OnSegmentReady;      // 只有实时模式才自动断句

            UpdateButtons();
            UpdateBleConnUi(true);
            LogLine("蓝牙语音已启用：mSBC(57B/帧, 7.5ms) → 120 样本 PCM → 翻译管线"
                  + "（与 USB 音频二选一）。对鼠标说话试试。", UiTheme.Success);
        }

        /// <summary>
        /// 停用蓝牙语音（同步收尾，不写 CCCD）：解 VAD 事件、清队列、恢复 _running=false。
        /// CCCD 退订由调用方异步补（<see cref="BleDirectLink.StopVoiceAsync"/>）。
        /// 录音缓冲不清空——调用方随后可按 USB 同款逻辑决定是否送模型。
        /// </summary>
        private void StopBleVoiceSync(string reason)
        {
            if (!_bleVoiceActive)
                return;
            _bleVoiceActive = false;
            _running = false;
            _vad.SegmentReady -= OnSegmentReady;
            _vad.LevelUpdated -= OnLevelUpdated;
            lock (_queueGate)
            {
                _pendingPcm.Clear();
                _pendingSec.Clear();
            }
            SetLevelUi(0f);
            UpdateButtons();
            // 刷新语音按钮可用态与状态行（从 UpdateBleConnUi(false) 进来时二次进入无副作用：
            // 此时 _bleVoiceActive 已为 false，不会再次走收尾逻辑）。
            UpdateBleConnUi(_uiBleConnected);
            LogLine("蓝牙语音已停用（" + reason + "）：本段共入管 " + _bleVoiceFrames + " 帧 PCM"
                  + (_bleVoiceErr > 0 ? "，丢弃异常帧 " + _bleVoiceErr + " 个" : "") + "。", UiTheme.TextSub);
        }

        /// <summary>
        /// 语音 notify 回调（WinRT 线程池线程）：每帧 57B mSBC → 240B PCM → OnAudioData。
        /// 长度≠57 = 链路/MTU 异常（帧边界即 notify 边界，不存在跨 notify 分片），
        /// 丢弃并计数，等下一个 notify 自然重新对齐。
        /// </summary>
        private void OnBleVoiceFrame(byte[] data)
        {
            if (data == null || data.Length == 0)
                return;
            if (!_bleVoiceActive || !_running)
                return;                      // 未启用 / USB 采集中：直接丢弃，不解码

            if (data.Length != MsbcDecoder.FrameLen)
            {
                _bleVoiceErr++;
                LogVoiceErr("语音帧长度异常：" + data.Length + "B（期望 57B，疑似 MTU 未协商到 158），丢弃");
                return;
            }

            byte[] pcm = new byte[MsbcDecoder.PcmBytesPerFrame];
            if (!_msbc.TryDecodeFrame(data, 0, pcm, 0))
            {
                _bleVoiceErr++;
                LogVoiceErr("mSBC 帧解码失败（sync 0xAD 或 CRC8 不符），丢弃该帧");
                return;
            }

            _bleVoiceFrames++;
            if (_bleVoiceFrames == 1)
                LogLine("收到首个蓝牙语音帧并解码成功（57B → 240B PCM @16kHz/mono）。", UiTheme.Success);

            OnAudioData(pcm);   // 与 USB 采集同一入口：VAD 电平/断句/录音缓冲全复用
        }

        /// <summary>语音异常限流日志：前 3 次 + 之后每 100 次记一条（7.5ms 一帧，不刷屏）。</summary>
        private void LogVoiceErr(string msg)
        {
            if (_bleVoiceErr <= 3 || _bleVoiceErr % 100 == 0)
                LogLine(msg + "（第 " + _bleVoiceErr + " 次）", UiTheme.Warn);
        }

        // ---------- UI 构建 ----------

        private void BuildUi()
        {
            Text = "Biboo OS - 实时翻译";
            BackColor = UiTheme.FormBg;
            Font = UiTheme.FontBody;
            ClientSize = new Size(820, 740);
            MinimumSize = new Size(700, 660);
            StartPosition = FormStartPosition.CenterParent;

            // 卡1：音频输入
            var cardAudio = new RoundedPanel();
            _cardAudio = cardAudio;
            cardAudio.SetBounds(12, 12, ContentDesignWidth - 24, AudioCardHeight);
            cardAudio.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            cardAudio.Controls.Add(MakeTitle("音频输入（USB Audio / 蓝牙语音 · 二选一）"));

            var lblDev = new Label();
            lblDev.Text = "设备";
            lblDev.ForeColor = UiTheme.TextSub;
            lblDev.SetBounds(16, 36, 36, 22);
            lblDev.TextAlign = ContentAlignment.MiddleLeft;

            _cmbDevice = new ComboBox();
            _cmbDevice.SetBounds(56, 34, 380, 22);
            _cmbDevice.DropDownStyle = ComboBoxStyle.DropDownList;
            _cmbDevice.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _cmbDevice.SelectedIndexChanged += delegate { UpdateButtons(); };

            _btnRefreshDev = new GradientButton();
            _btnRefreshDev.SetBounds(444, 32, 72, 26);
            _btnRefreshDev.Text = "刷新";
            _btnRefreshDev.IsPrimary = false;
            _btnRefreshDev.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _btnRefreshDev.Click += delegate { RefreshDevices(); };

            _btnStart = new GradientButton();
            _btnStart.SetBounds(524, 32, 104, 26);
            _btnStart.Text = "开始采集";
            _btnStart.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _btnStart.Click += delegate { _pttRecording = false; OnStart(_btnStart, EventArgs.Empty); };

            _btnStop = new GradientButton();
            _btnStop.SetBounds(636, 32, 104, 26);
            _btnStop.Text = "停止采集";
            _btnStop.IsPrimary = false;
            _btnStop.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _btnStop.Click += OnStop;

            var lblFmt = new Label();
            lblFmt.Text = "请求格式：16 kHz / 单声道 / 16-bit PCM（USB Audio 设备在 Windows 中即标准录音设备，"
                        + "原生格式不同时由系统自动重采样）";
            lblFmt.ForeColor = UiTheme.TextSub;
            lblFmt.SetBounds(16, 68, ContentDesignWidth - 56, 20);
            lblFmt.AutoSize = false;
            lblFmt.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            lblFmt.TextAlign = ContentAlignment.MiddleLeft;

            var lblLv = new Label();
            lblLv.Text = "电平";
            lblLv.ForeColor = UiTheme.TextSub;
            lblLv.SetBounds(16, 100, 36, 22);
            lblLv.TextAlign = ContentAlignment.MiddleLeft;

            _barLevel = new ProgressBar();
            _barLevel.SetBounds(56, 102, 480, 20);
            _barLevel.Minimum = 0;
            _barLevel.Maximum = 100;
            _barLevel.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

            _lblLevel = new Label();
            _lblLevel.SetBounds(548, 100, 192, 22);
            _lblLevel.ForeColor = UiTheme.TextSub;
            _lblLevel.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _lblLevel.Text = "RMS 0.000";
            _lblLevel.TextAlign = ContentAlignment.MiddleLeft;

            _chkRealtime = new CheckBox();
            _chkRealtime.Text = "实时字幕（VAD 自动断句）";
            _chkRealtime.ForeColor = UiTheme.TextSub;
            _chkRealtime.SetBounds(16, 124, 184, 18);
            _chkRealtime.Checked = false;                 // 默认「按录音」模式
            _chkRealtime.CheckedChanged += delegate { UpdateModeUi(); UpdateButtons(); };

            // 朗读译文（TTS）：Kokoro int8 中英双语，103 音色；句内串行，新句子会打断上一句
            _chkSpeak = new CheckBox();
            _chkSpeak.Text = "朗读译文";
            _chkSpeak.ForeColor = UiTheme.TextSub;
            _chkSpeak.SetBounds(16, 148, 86, 18);
            _chkSpeak.Checked = true;          // 进入卡片默认勾选「朗读译文」（默认就出声）
            _chkSpeak.CheckedChanged += delegate
            {
                if (_chkSpeak.Checked)
                    StartTtsWarmup();          // 勾上才真正加载，省内存
                UpdateTtsStatus();
            };

            var lblSid = new Label();
            lblSid.Text = "音色";
            lblSid.ForeColor = UiTheme.TextSub;
            lblSid.SetBounds(112, 148, 28, 20);
            lblSid.TextAlign = ContentAlignment.MiddleLeft;

            _numSid = new TextBox();
            _numSid.SetBounds(142, 146, 40, 22);
            _numSid.Text = TtsEngine.DefaultSpeakerId.ToString();
            _numSid.TextAlign = HorizontalAlignment.Center;
            _numSid.BackColor = UiTheme.InputBg;
            _numSid.ForeColor = UiTheme.TextMain;
            _numSid.TextChanged += delegate { _sidUserSet = true; };

            var lblOut = new Label();
            lblOut.Text = "输出";
            lblOut.ForeColor = UiTheme.TextSub;
            lblOut.SetBounds(196, 148, 30, 20);
            lblOut.TextAlign = ContentAlignment.MiddleLeft;

            _cmbOut = new ComboBox();
            _cmbOut.SetBounds(230, 144, 200, 22);
            _cmbOut.DropDownStyle = ComboBoxStyle.DropDownList;
            _cmbOut.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

            _btnTestTts = new GradientButton();
            _btnTestTts.SetBounds(636, 142, 104, 26);
            _btnTestTts.Text = "试听一句";
            _btnTestTts.IsPrimary = false;
            _btnTestTts.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _btnTestTts.Click += OnTestTts;

            _lblTts = new Label();
            _lblTts.SetBounds(440, 148, 180, 20);
            _lblTts.ForeColor = UiTheme.TextSub;
            _lblTts.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _lblTts.TextAlign = ContentAlignment.MiddleRight;
            _lblTts.Text = "○ 未启用";

            _lblMode = new Label();
            _lblMode.ForeColor = UiTheme.TextSub;
            _lblMode.SetBounds(206, 124, ContentDesignWidth - 238, 18);
            _lblMode.AutoSize = false;
            _lblMode.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _lblMode.TextAlign = ContentAlignment.MiddleLeft;

            // 鼠标语音键自动录音：勾上后经 Dongle 私有协议收 K4 按下/松开边沿
            // （鼠标 SPP 0x33F4 → dongle 0xE3 → 本页 DongleLink），不要求本窗口在前台——
            // 用户可以在微信/游戏里直接按住 K4 说话。
            _chkPtt = new CheckBox();
            _chkPtt.Text = "鼠标语音键自动录音";
            _chkPtt.ForeColor = UiTheme.TextSub;
            _chkPtt.SetBounds(16, 174, 168, 18);
            _chkPtt.Checked = true;
            _chkPtt.CheckedChanged += delegate { OnPttToggleChanged(); };

            _lblPtt = new Label();
            _lblPtt.ForeColor = UiTheme.TextSub;
            _lblPtt.SetBounds(190, 174, ContentDesignWidth - 222, 18);
            _lblPtt.AutoSize = false;
            _lblPtt.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _lblPtt.TextAlign = ContentAlignment.MiddleLeft;
            _lblPtt.Text = "○ 等待启动";

            cardAudio.Controls.AddRange(new Control[] {
                lblDev, _cmbDevice, _btnRefreshDev, _btnStart, _btnStop,
                lblFmt, lblLv, _barLevel, _lblLevel, _chkRealtime, _lblMode,
                _chkSpeak, lblSid, _numSid, lblOut, _cmbOut, _btnTestTts, _lblTts,
                _chkPtt, _lblPtt });
            _contentCards.Add(cardAudio);

            // 卡1b：蓝牙输入（BLE 直连 · 配对连接）
            // 布局与交互照抄「固件升级」页 BLE Direct 子面板：模式按钮 + 状态行 + 设备列表 + 连接/断开。
            // 第二步已落地：【启用语音】订阅 0xFFF0/0xB03E → 57B mSBC 帧 → MsbcDecoder → 240B PCM
            // → OnAudioData 进现有 VAD/识别/翻译管线（与 USB Audio 二选一）。
            var cardBle = new RoundedPanel();
            _cardBle = cardBle;
            cardBle.SetBounds(12, 228, ContentDesignWidth - 24, BleCardHeight);
            cardBle.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            cardBle.Controls.Add(MakeTitle("蓝牙输入（BLE 直连 · 配对连接）"));

            _btnBleModePaired = MakeBleButton("已配对设备", 16, 34, 96, 26, true, OnBleModePaired);
            _btnBleModeScan = MakeBleButton("扫描附近", 116, 34, 96, 26, false, OnBleModeScan);
            _btnBleRefresh = MakeBleButton("刷新", 216, 34, 60, 26, false, OnBleRefresh);
            _btnBleSysPair = MakeBleButton("系统配对", 280, 34, 88, 26, false, OnBleSysPair);

            _lblBleState = new Label();
            _lblBleState.Text = "○ 未连接";
            _lblBleState.ForeColor = UiTheme.TextSub;
            _lblBleState.SetBounds(16, 62, ContentDesignWidth - 56, 18);
            _lblBleState.AutoSize = false;
            _lblBleState.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _lblBleState.TextAlign = ContentAlignment.MiddleLeft;

            _lstBle = new ListBox();
            _lstBle.SetBounds(16, 84, 540, 84);
            _lstBle.Font = new Font("Consolas", 9f);
            _lstBle.BackColor = UiTheme.InputBg;
            _lstBle.ForeColor = UiTheme.TextMain;
            _lstBle.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _lstBle.SelectedIndexChanged += delegate { UpdateBleConnUi(_uiBleConnected); };
            _lstBle.DoubleClick += OnBleListDoubleClick;

            // 右侧按钮列统一右锚定：窗口拉宽时整列跟随右移，不与列表框相交
            _btnBleConnect = MakeBleButton("连接", 566, 84, 96, 28, true, OnBleConnect);
            _btnBleConnect.Enabled = false;
            _btnBleConnect.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _btnBleDisconnect = MakeBleButton("断开", 566, 116, 96, 28, false, OnBleDisconnect);
            _btnBleDisconnect.Enabled = false;
            _btnBleDisconnect.Anchor = AnchorStyles.Top | AnchorStyles.Right;

            // 语音开关（第二步）：订阅/退订 0xB03E notify。与 USB 音频采集二选一 ——
            // 启用中【开始采集】与设备下拉一律禁用；USB 采集中【启用语音】点了会被拒并提示。
            _btnBleVoiceOn = MakeBleButton("启用语音", 566, 148, 96, 26, true, OnBleVoiceOn);
            _btnBleVoiceOn.Enabled = false;
            _btnBleVoiceOn.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _btnBleVoiceOff = MakeBleButton("停用语音", 566, 178, 96, 26, false, OnBleVoiceOff);
            _btnBleVoiceOff.Enabled = false;
            _btnBleVoiceOff.Anchor = AnchorStyles.Top | AnchorStyles.Right;

            cardBle.Controls.AddRange(new Control[] {
                _btnBleModePaired, _btnBleModeScan, _btnBleRefresh, _btnBleSysPair,
                _lblBleState, _lstBle, _btnBleConnect, _btnBleDisconnect,
                _btnBleVoiceOn, _btnBleVoiceOff });
            _contentCards.Add(cardBle);

            _ble.DeviceDiscovered += OnBleDiscovered;
            _ble.ConnectionChanged += OnBleConnChanged;
            _ble.PairedDeviceChanged += OnBlePairedChanged;
            _ble.PairedWatchStateChanged += OnBlePairedWatchState;
            _ble.ScanCompleted += OnBleScanCompleted;
            _ble.VoiceReceived += OnBleVoiceFrame;   // 第二步：mSBC 帧 → PCM → 翻译管线
            _ble.SppReceived += OnBleSppFrame;       // K4 边沿（BLE 直连场景：0x33F4 上的 0x23 帧）

            _bleLinkTimer = new System.Windows.Forms.Timer();
            _bleLinkTimer.Interval = 2500;   // 与「蓝牙连接」页一致：2.5s 一次 ATT 活性读
            _bleLinkTimer.Tick += OnBleLinkTimer;
            _bleLinkTimer.Start();

            // 卡2：翻译引擎
            var cardEngine = new RoundedPanel();
            _cardEngine = cardEngine;
            cardEngine.SetBounds(12, 172, ContentDesignWidth - 24, EngineCardHeight);
            cardEngine.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            cardEngine.Controls.Add(MakeTitle("翻译引擎（源/目标语言可搜索选择，后续接 Whisper.net + Argos）"));

            var lblSrc = new Label();
            lblSrc.Text = "源语言";
            lblSrc.ForeColor = UiTheme.TextSub;
            lblSrc.SetBounds(16, 38, 48, 22);
            lblSrc.TextAlign = ContentAlignment.MiddleLeft;

            _srcLang = FindLang("zh") ?? LangItems[0];    // 默认源语言：中文（简体）
            _dstLang = FindLang("en") ?? LangItems[1];    // 默认目标语言：英语

            _btnSrcLang = new GradientButton();
            _btnSrcLang.SetBounds(68, 34, 150, 26);
            _btnSrcLang.Text = _srcLang.Name;
            _btnSrcLang.IsPrimary = false;
            _btnSrcLang.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            _btnSrcLang.Click += delegate { PickLang(ref _srcLang, _btnSrcLang); };

            _btnSwap = new GradientButton();
            _btnSwap.SetBounds(222, 34, 28, 26);
            _btnSwap.Text = "⇄";
            _btnSwap.IsPrimary = false;
            _btnSwap.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            _btnSwap.Click += OnSwapLang;

            var lblDst = new Label();
            lblDst.Text = "目标语言";
            lblDst.ForeColor = UiTheme.TextSub;
            lblDst.SetBounds(258, 38, 56, 22);
            lblDst.TextAlign = ContentAlignment.MiddleLeft;

            _btnDstLang = new GradientButton();
            _btnDstLang.SetBounds(318, 34, 150, 26);
            _btnDstLang.Text = _dstLang.Name;
            _btnDstLang.IsPrimary = false;
            _btnDstLang.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            _btnDstLang.Click += delegate { PickLang(ref _dstLang, _btnDstLang); };

            var lblEng = new Label();
            lblEng.Text = "引擎";
            lblEng.ForeColor = UiTheme.TextSub;
            lblEng.SetBounds(16, 70, 36, 22);
            lblEng.TextAlign = ContentAlignment.MiddleLeft;

            _cmbEngine = new ComboBox();
            _cmbEngine.SetBounds(56, 68, 300, 22);
            _cmbEngine.DropDownStyle = ComboBoxStyle.DropDownList;
            _cmbEngine.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            _cmbEngine.SelectedIndexChanged += OnEngineChanged;

            var lblModel = new Label();
            lblModel.Text = "模型";
            lblModel.ForeColor = UiTheme.TextSub;
            lblModel.SetBounds(372, 70, 36, 22);
            lblModel.TextAlign = ContentAlignment.MiddleLeft;

            _cmbModel = new ComboBox();
            _cmbModel.SetBounds(412, 68, 300, 22);
            _cmbModel.DropDownStyle = ComboBoxStyle.DropDownList;
            _cmbModel.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _cmbModel.SelectedIndexChanged += OnModelChanged;

            // 引擎/模型行：默认不显示（ShowEngineSelectors=false）。控件仍然创建并完成初始化，
            // 所以 _cmbEngine.SelectedIndex / _cmbModel.SelectedIndex 照常驱动 _engine 与模型路径，
            // 只是用户看不到；隐藏的 ComboBox 设 SelectedIndex 依旧会走 OnEngineChanged/OnModelChanged。
            lblEng.Visible = ShowEngineSelectors;
            _cmbEngine.Visible = ShowEngineSelectors;
            lblModel.Visible = ShowEngineSelectors;
            _cmbModel.Visible = ShowEngineSelectors;

            // 说明两行的纵向起点：引擎行隐藏时上移填满空位（与 EngineCardHeight 的分段高度一致）
            int hintsY = ShowEngineSelectors ? 102 : 68;

            _lblEngineNote = new Label();
            _lblEngineNote.Text = "推荐「SenseVoice + 离线翻译」：识别 + 中↔英翻译全在本机跑，不联网、不需要本地服务（单句译文 0.2~0.4s）。"
                               + "语言面板支持按中文名 / 母语名 / 代码搜索（如输入「ri」「日本語」「ja」均可命中日语）。";
            _lblEngineNote.ForeColor = UiTheme.TextSub;
            _lblEngineNote.SetBounds(16, hintsY + 24, ContentDesignWidth - 56, 32);
            _lblEngineNote.AutoSize = false;
            _lblEngineNote.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _lblEngineNote.TextAlign = ContentAlignment.TopLeft;

            // 预热状态行：进入页面就在后台加载模型 / 拉起本地服务，把首次翻译的等待挪到这里
            _lblWarmup = new Label();
            _lblWarmup.Text = "○ 预热未开始";
            _lblWarmup.ForeColor = UiTheme.TextSub;
            _lblWarmup.SetBounds(16, hintsY, ContentDesignWidth - 56, 20);
            _lblWarmup.AutoSize = false;
            _lblWarmup.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _lblWarmup.TextAlign = ContentAlignment.MiddleLeft;

            // 暂时屏蔽用户红框圈出的两行说明（预热状态 + 引擎推荐）；
            // ShowEngineHints 改回 true 即恢复，卡片高度会一起还原（见 EngineCardHeight）。
            _lblWarmup.Visible = ShowEngineHints;
            _lblEngineNote.Visible = ShowEngineHints;

            cardEngine.Controls.AddRange(new Control[] {
                lblSrc, _btnSrcLang, _btnSwap, lblDst, _btnDstLang,
                lblEng, _cmbEngine, lblModel, _cmbModel, _lblWarmup, _lblEngineNote });
            _contentCards.Add(cardEngine);

            // 卡3：实时字幕（高度在 LayoutContent 中按剩余空间动态分配）
            var cardSub = new RoundedPanel();
            _cardSub = cardSub;
            cardSub.SetBounds(12, 324, ContentDesignWidth - 24, 216);
            cardSub.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            cardSub.Controls.Add(MakeTitle("实时字幕"));

            _lstSubtitle = new ListView();
            _lstSubtitle.SetBounds(16, 34, ContentDesignWidth - 56, 148);
            _lstSubtitle.View = View.Details;
            _lstSubtitle.FullRowSelect = true;
            _lstSubtitle.GridLines = false;
            _lstSubtitle.BackColor = UiTheme.InputBg;
            _lstSubtitle.ForeColor = UiTheme.TextMain;
            _lstSubtitle.Font = new Font("Microsoft YaHei UI", 9F);
            _lstSubtitle.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            _lstSubtitle.Columns.Add("时间", 90);
            _lstSubtitle.Columns.Add("原文（ASR）", 300);
            _lstSubtitle.Columns.Add("译文（MT）", 300);

            _lblStats = new Label();
            _lblStats.SetBounds(16, 188, ContentDesignWidth - 56, 20);
            _lblStats.ForeColor = UiTheme.TextSub;
            _lblStats.AutoSize = false;
            _lblStats.Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            _lblStats.Text = "段数 0 · 音频 0.0s · 平均耗时 0ms";
            _lblStats.TextAlign = ContentAlignment.MiddleLeft;

            cardSub.Controls.AddRange(new Control[] { _lstSubtitle, _lblStats });
            _contentCards.Add(cardSub);

            // 卡4：日志（高度在 LayoutContent 中按剩余空间动态分配）
            var cardLog = new RoundedPanel();
            _cardLog = cardLog;
            cardLog.SetBounds(12, 552, ContentDesignWidth - 24, 164);
            cardLog.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            cardLog.Controls.Add(MakeTitle("日志"));

            _txtLog = new RichTextBox();
            _txtLog.SetBounds(16, 32, ContentDesignWidth - 56, 118);
            _txtLog.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            _txtLog.ReadOnly = true;
            _txtLog.BorderStyle = BorderStyle.None;
            _txtLog.BackColor = UiTheme.InputBg;
            _txtLog.ForeColor = UiTheme.TextMain;
            _txtLog.Font = new Font("Consolas", 9f);
            _txtLog.DetectUrls = false;

            // 清空日志：放标题行右侧（右锚定），样式与「蓝牙连接」页日志卡那颗一致
            _btnClearLog = new GradientButton();
            _btnClearLog.SetBounds(ContentDesignWidth - 24 - 16 - 96, 6, 96, 22);
            _btnClearLog.Text = "清空日志";
            _btnClearLog.IsPrimary = false;
            _btnClearLog.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _btnClearLog.Click += OnClearLog;

            cardLog.Controls.Add(_txtLog);
            cardLog.Controls.Add(_btnClearLog);
            // ⚠ 本工程 RoundedPanel 的 z-order：Controls 索引 0 = 最上层，Add 追加到末尾 = 最底层。
            // 标题标签（14,8,宽 ContentDesignWidth-60）会盖住按钮左大半，只剩右边缘 6px 露出来
            // → 必须显式提到索引 0，否则界面上只看到一道细弧、而且点不到。
            cardLog.Controls.SetChildIndex(_btnClearLog, 0);
            _contentCards.Add(cardLog);

            Controls.AddRange(new Control[] { cardAudio, cardBle, cardEngine, cardSub, cardLog });

            LayoutContent();          // 按当前 ClientSize 归一化一次，避免首帧仍用设计坐标
            UpdateModeUi();
            UpdateButtons();
        }

        private static Label MakeTitle(string text)
        {
            var l = new Label();
            l.Text = text;
            l.SetBounds(14, 8, ContentDesignWidth - 60, 20);
            l.Font = UiTheme.FontTitle;
            l.ForeColor = UiTheme.TextMain;
            l.TextAlign = ContentAlignment.MiddleLeft;
            l.AutoSize = false;
            l.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            return l;
        }

        /// <summary>蓝牙输入卡的小按钮工厂（与 OtaForm.MakeButton 同款，仅尺寸/文案不同）。</summary>
        private static GradientButton MakeBleButton(string text, int x, int y, int w, int h,
                                                    bool primary, EventHandler onClick)
        {
            var b = new GradientButton();
            b.SetBounds(x, y, w, h);
            b.Text = text;
            b.IsPrimary = primary;
            b.Click += onClick;
            return b;
        }

        /// <summary>
        /// 内容列自适应：宽度铺满可用区（不再限制 ContentDesignWidth 上限——旧版限宽会让
        /// 最大化后卡片仍停在 756px，右侧大片留白），高度上把多余空间分给「实时字幕」与「日志」两卡。
        /// 卡片内部的控件靠锚点（Left|Right / Top|Bottom）跟随伸展，无需逐个重排。
        /// </summary>
        private void LayoutContent()
        {
            int availW = ClientSize.Width - CardMargin * 2;
            if (availW < 240)
                availW = 240;

            int y = CardMargin;

            if (_cardAudio != null)
            {
                _cardAudio.SetBounds(CardMargin, y, availW, AudioCardHeight);
                y += AudioCardHeight + CardGap;
            }
            if (_cardBle != null)
            {
                _cardBle.SetBounds(CardMargin, y, availW, BleCardHeight);
                y += BleCardHeight + CardGap;
            }
            if (_cardEngine != null)
            {
                _cardEngine.SetBounds(CardMargin, y, availW, EngineCardHeight);
                y += EngineCardHeight + CardGap;
            }

            // 剩余高度：字幕区 + 日志区。二者按比例分配，字幕区略多（主要视线区）。
            int rest = ClientSize.Height - y - CardMargin;
            const int MinRest = 200;
            if (rest < MinRest)
                rest = MinRest;

            int subH = (int)Math.Round(rest * 0.58);
            if (subH < 140)
                subH = 140;
            int logH = rest - subH - CardGap;
            if (logH < 100)
                logH = 100;

            if (_cardSub != null)
                _cardSub.SetBounds(CardMargin, y, availW, subH);
            if (_cardLog != null)
                _cardLog.SetBounds(CardMargin, y + subH + CardGap, availW, logH);

            LayoutSubtitleColumns(availW);
        }

        /// <summary>字幕表列宽随卡片宽度等比分配：时间列定宽，原文/译文按 45:55（译文通常更长）。</summary>
        private void LayoutSubtitleColumns(int cardWidth)
        {
            if (_lstSubtitle == null || _lstSubtitle.Columns.Count < 3)
                return;
            const int timeW = 90;
            int inner = cardWidth - 32 - 4;           // 卡片左右内边距 + ListView 边框
            if (inner < timeW + 120)
                inner = timeW + 120;
            int remain = inner - timeW;
            int srcW = (int)Math.Round(remain * 0.45);
            int dstW = remain - srcW;
            _lstSubtitle.Columns[0].Width = timeW;
            _lstSubtitle.Columns[1].Width = srcW;
            _lstSubtitle.Columns[2].Width = dstW;
        }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            LayoutContent();
        }

        // ---------- 设备与引擎 ----------

        private void RefreshDevices()
        {
            var devs = UsbAudioCapture.ListDevices();
            _cmbDevice.Items.Clear();
            foreach (var d in devs)
                _cmbDevice.Items.Add(d);
            if (_cmbDevice.Items.Count > 0)
                _cmbDevice.SelectedIndex = 0;
            else
                LogLine("未发现任何录音设备（USB Audio 设备是否已插好/装好驱动）。", UiTheme.Warn);

            // 输出设备（TTS 朗读用，NAudio WaveOut）
            if (_cmbOut == null)
                return;
            try
            {
                _cmbOut.Items.Clear();
                for (int i = 0; i < WaveOut.DeviceCount; i++)
                {
                    var cap = WaveOut.GetCapabilities(i);
                    _cmbOut.Items.Add(i + " · " + cap.ProductName);
                }
                if (_cmbOut.Items.Count > 0)
                    _cmbOut.SelectedIndex = 0;
                else
                    LogLine("未发现音频输出设备，TTS 朗读将不可用。", UiTheme.Warn);
            }
            catch (Exception ex)
            {
                LogLine("枚举输出设备失败：" + ex.Message, UiTheme.Warn);
            }
            UpdateTtsStatus();
        }

        private void PopulateEngines()
        {
            _cmbEngine.Items.Clear();
            _cmbEngine.Items.Add(new StubTranslationEngine());
            // ★ 全离线首选：SenseVoice ASR + 混元 Hy-MT2（1.1GB 覆盖 33 语种任意方向，质量最好）
            _cmbEngine.Items.Add(new SenseVoiceHyMtEngine());
            // 全离线备选：SenseVoice ASR + Marian/OPUS-MT（中↔英，进程内、200~500ms，最快）
            _cmbEngine.Items.Add(new SenseVoiceMtEngine());
            // 离线识别：SenseVoice（非自回归小模型，CPU 上远快于 Whisper）
            _cmbEngine.Items.Add(new SenseVoiceAsrEngine());
            _cmbEngine.Items.Add(new SenseVoiceArgosEngine());
            _cmbEngine.Items.Add(new WhisperAsrEngine());
            _cmbEngine.Items.Add(new WhisperDirectTranslateEngine());
            _cmbEngine.Items.Add(new WhisperArgosEngine());
            _cmbEngine.SelectedIndex = 0;
            // 默认选「SenseVoice + 混元翻译」（gguf 模型在 Models\llm\ 下时）——全离线、33 语种
            for (int i = 0; i < _cmbEngine.Items.Count; i++)
            {
                var hy = _cmbEngine.Items[i] as SenseVoiceHyMtEngine;
                if (hy != null && hy.IsModelReady)
                {
                    _cmbEngine.SelectedIndex = i;
                    break;
                }
            }
            if (_cmbEngine.SelectedIndex == 0)
            {
                // 没有混元模型时退回 OPUS-MT（中↔英）
                for (int i = 0; i < _cmbEngine.Items.Count; i++)
                {
                    var mt = _cmbEngine.Items[i] as SenseVoiceMtEngine;
                    if (mt != null && MtPair.AnyModelInstalled)
                    {
                        _cmbEngine.SelectedIndex = i;
                        break;
                    }
                }
            }
            if (_cmbEngine.SelectedIndex == 0)
            {
                // 连 MT 模型都没有时退回 SenseVoice 纯识别
                for (int i = 0; i < _cmbEngine.Items.Count; i++)
                {
                    var sv = _cmbEngine.Items[i] as SenseVoiceAsrEngine;
                    if (sv != null && sv.IsModelReady)
                    {
                        _cmbEngine.SelectedIndex = i;
                        break;
                    }
                }
            }
            _engine = (ITranslationEngine)_cmbEngine.SelectedItem;
        }

        private void OnEngineChanged(object sender, EventArgs e)
        {
            var eng = _cmbEngine.SelectedItem as ITranslationEngine;
            if (eng == null || ReferenceEquals(eng, _engine))
                return;
            _engine = eng;
            LogLine("已切换引擎：" + eng.DisplayName, UiTheme.TextSub);

            var hy = eng as SenseVoiceHyMtEngine;
            if (hy != null)
            {
                if (!hy.IsModelReady)
                {
                    LogLine("⚠ 混元翻译模型缺失：需要 " + TranslationConfig.LlmMtModelPath
                          + "（tencent/Hy-MT2-1.8B-GGUF 里的 Hy-MT2-1.8B-Q4_K_M.gguf，1.05GiB，Apache-2.0）。",
                        UiTheme.Danger);
                }
                else
                {
                    LogLine("混元 Hy-MT2 就绪（" + HyMtLang.Count + " 语种任意方向）："
                          + "识别 SenseVoice → 翻译 llama-server（" + TranslationConfig.LlmMtBaseUrl + "，只监听本机，不联网），"
                          + "单句约 1.5~2.5s。", UiTheme.TextSub);
                    if (!LlmServerLauncher.ExeExists)
                        LogLine("⚠ 未找到 llama\\llama-server.exe，且不会自动拉起服务；可自行启动后本引擎即可用。", UiTheme.Warn);
                }
                RefreshModels();
            }

            var svmt = eng as SenseVoiceMtEngine;
            if (svmt != null)
            {
                if (!MtPair.AnyModelInstalled)
                {
                    LogLine("⚠ 未找到离线翻译模型：需要 Models\\mt\\opus-mt-zh-en\\ 与 opus-mt-en-zh\\（各含 3 个 int8 onnx + source.spm + vocab.json）。",
                        UiTheme.Danger);
                }
                else
                {
                    LogLine("离线翻译就绪（" + MtPair.InstalledSummary + "）：识别 SenseVoice → 翻译 OPUS-MT，全部本机跑，"
                        + "单句译文约 0.2~0.4s，不联网、不需要本地服务。", UiTheme.TextSub);
                    RefreshModels();
                }
            }

            var sv = eng as SenseVoiceAsrEngine;
            if (sv != null)
            {
                if (!sv.IsModelReady)
                    LogLine("⚠ " + eng.DisplayName + "：Models\\sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17\\ 下缺 model.int8.onnx 或 tokens.txt。",
                        UiTheme.Danger);
                else
                    LogLine("SenseVoice：中/粤/英/日/韩离线识别，自带标点与 ITN，速度远快于 Whisper。", UiTheme.TextSub);
                RefreshModels();
            }

            var sva = eng as SenseVoiceArgosEngine;
            if (sva != null)
                LogLine("提示：SenseVoice+Argos = 离线 ASR + 本机 Argos MT（" + TranslationConfig.ArgosTranslateUrl + "）；没起 MT 服务则译文=原文。",
                    UiTheme.TextSub);

            var wasr = eng as WhisperAsrEngine;
            if (wasr != null && !wasr.IsModelReady)
                LogLine("⚠ " + eng.DisplayName + " 缺少模型文件，运行时该引擎会报错；请先下载 ggml 模型到 Models\\。", UiTheme.Danger);

            var warg = eng as WhisperArgosEngine;
            if (warg != null)
                LogLine("提示：Whisper+Argos 需①ggml 模型 ②本机 Argos/LibreTranslate 服务可达（" + TranslationConfig.ArgosTranslateUrl + "）。",
                    UiTheme.TextSub);

            var wdir = eng as WhisperDirectTranslateEngine;
            if (wdir != null)
            {
                if (!wdir.IsModelReady)
                    LogLine("⚠ " + eng.DisplayName + " 缺少模型文件。", UiTheme.Danger);
                LogLine("提示：Whisper 直译把语音直接译成【英语】（目标语言固定英语，无需本地服务）。", UiTheme.TextSub);
            }

            // 换了引擎就重新预热（不同引擎加载不同模型；同一引擎已预热过则秒返回）
            StartWarmup();
        }

        /// <summary>下拉里模型项 = 文件名 + 速度提示；取真实路径时用 ModelFileOf 去掉提示后缀。</summary>
        private const string ModelHintSep = "  (";
        private static string ModelLabel(string file)
        {
            if (string.IsNullOrEmpty(file)) return file;
            var n = file.ToLowerInvariant();
            string hint = n.Contains("tiny") ? "最快，质量一般"
                        : n.Contains("base") ? "推荐，速度/质量平衡"
                        : n.Contains("small") ? "慢，约 4×base"
                        : (n.Contains("medium") || n.Contains("large")) ? "很慢，不宜实时"
                        : "";
            return string.IsNullOrEmpty(hint) ? file : file + ModelHintSep + hint + ")";
        }

        private static string ModelFileOf(string label)
        {
            if (string.IsNullOrEmpty(label)) return label;
            int i = label.IndexOf(ModelHintSep, StringComparison.Ordinal);
            return i > 0 ? label.Substring(0, i) : label;
        }

        private static bool IsWhisperEngine(ITranslationEngine e)
        {
            return e is WhisperAsrEngine || e is WhisperDirectTranslateEngine || e is WhisperArgosEngine;
        }

        /// <summary>在下拉项里按文件名定位当前配置的模型（大小写不敏感）：保证「模型」默认落到
        /// TranslationConfig 里配置的那个文件，而不是磁盘实际大小写与配置不一致时误选第一项。
        /// 找不到就回退第 0 项（占位提示行也在 0）。</summary>
        private static int FindItemIndex(ComboBox.ObjectCollection items, string fileName)
        {
            if (!string.IsNullOrEmpty(fileName))
            {
                for (int k = 0; k < items.Count; k++)
                {
                    var s = items[k] as string;
                    if (s != null && string.Equals(s, fileName, StringComparison.OrdinalIgnoreCase))
                        return k;
                }
            }
            return 0;
        }

        /// <summary>「模型」下拉随所选引擎变化：
        /// Whisper* → 列 Models\ggml*.bin，可在界面里切换 tiny/base/small；
        /// SenseVoice* → 显示实际在用的小模型（下拉禁用，仅作提示，免得看到 ggml-base.bin 误以为在用 Whisper）；
        /// 桩引擎 → 不使用模型。</summary>
        private void RefreshModels()
        {
            try
            {
                _cmbModel.SelectedIndexChanged -= OnModelChanged;   // 填表期间不触发切换逻辑
                _cmbModel.Items.Clear();

                if (IsWhisperEngine(_engine))
                {
                    _cmbModel.Enabled = true;
                    var current = System.IO.Path.GetFileName(TranslationConfig.WhisperModelPath);
                    var dir = System.IO.Path.GetDirectoryName(TranslationConfig.WhisperModelPath);
                    if (!string.IsNullOrEmpty(dir) && System.IO.Directory.Exists(dir))
                    {
                        var files = System.IO.Directory.GetFiles(dir, "ggml*.bin");
                        Array.Sort(files);
                        foreach (var f in files)
                            _cmbModel.Items.Add(ModelLabel(System.IO.Path.GetFileName(f)));
                    }
                    if (_cmbModel.Items.Count == 0)
                        _cmbModel.Items.Add("（Models 目录下无 ggml*.bin）");
                    _cmbModel.SelectedIndex = FindItemIndex(_cmbModel.Items, ModelLabel(current));
                }
                else if (_engine is SenseVoiceHyMtEngine)
                {
                    // 混元：允许在 Models\llm\*.gguf 之间切换（换模型必须重启 llama-server，见 OnModelChanged）
                    _cmbModel.Enabled = true;
                    var cur = System.IO.Path.GetFileName(TranslationConfig.LlmMtModelPath);
                    var dir = System.IO.Path.GetDirectoryName(TranslationConfig.LlmMtModelPath);
                    if (!string.IsNullOrEmpty(dir) && System.IO.Directory.Exists(dir))
                    {
                        var files = System.IO.Directory.GetFiles(dir, "*.gguf");
                        Array.Sort(files);
                        foreach (var f in files)
                            _cmbModel.Items.Add(System.IO.Path.GetFileName(f));
                    }
                    if (_cmbModel.Items.Count == 0)
                        _cmbModel.Items.Add("（Models\\llm 目录下无 *.gguf）");
                    _cmbModel.SelectedIndex = FindItemIndex(_cmbModel.Items, cur);
                }
                else if (_engine is SenseVoiceMtEngine)
                {
                    _cmbModel.Enabled = false;
                    _cmbModel.Items.Add("SenseVoice 小模型 + OPUS-MT 离线翻译");
                    _cmbModel.SelectedIndex = 0;
                }
                else if (_engine is SenseVoiceAsrEngine || _engine is SenseVoiceArgosEngine)
                {
                    _cmbModel.Enabled = false;
                    _cmbModel.Items.Add("SenseVoice 小模型（sherpa-onnx，中/粤/英/日/韩）");
                    _cmbModel.SelectedIndex = 0;
                }
                else
                {
                    _cmbModel.Enabled = false;
                    _cmbModel.Items.Add("（桩引擎：不加载模型）");
                    _cmbModel.SelectedIndex = 0;
                }

                _cmbModel.SelectedIndexChanged += OnModelChanged;
            }
            catch (Exception ex)
            {
                LogLine("扫描模型目录失败：" + ex.Message, UiTheme.Danger);
            }
        }

        private void OnModelChanged(object sender, EventArgs e)
        {
            // 混元：切换 gguf。模型由 llama-server 在启动时用 -m 载入，换了文件必须重启服务才生效。
            if (_engine is SenseVoiceHyMtEngine)
            {
                var gguf = _cmbModel.SelectedItem as string;
                if (string.IsNullOrEmpty(gguf) || gguf.StartsWith("（"))
                    return;
                var ldir = System.IO.Path.GetDirectoryName(TranslationConfig.LlmMtModelPath);
                var newPath = System.IO.Path.Combine(ldir, gguf);
                if (string.Equals(newPath, TranslationConfig.LlmMtModelPath, StringComparison.OrdinalIgnoreCase))
                    return;

                TranslationConfig.LlmMtModelPath = newPath;
                bool restarted = false;
                if (LlmServerLauncher.IsOursAlive)   // 只重启我们自己拉起的那个；用户手起的不动
                {
                    LlmServerLauncher.Stop();
                    restarted = true;
                }

                int keepHy = _cmbEngine.SelectedIndex;
                PopulateEngines();
                if (keepHy >= 0 && keepHy < _cmbEngine.Items.Count)
                    _cmbEngine.SelectedIndex = keepHy;

                LogLine("已切换混元模型：" + gguf
                      + (restarted ? "（已停止旧的 llama-server，下次翻译会自动用新模型重启）" : "（下次翻译生效）"),
                        UiTheme.TextSub);
                return;
            }

            if (!IsWhisperEngine(_engine))
                return;                                  // 非 Whisper 引擎的下拉只是展示，不做切换

            var label = _cmbModel.SelectedItem as string;
            if (string.IsNullOrEmpty(label) || label.StartsWith("（"))
                return;
            var name = ModelFileOf(label);
            var dir = System.IO.Path.GetDirectoryName(TranslationConfig.WhisperModelPath);
            TranslationConfig.WhisperModelPath = System.IO.Path.Combine(dir, name);

            int keep = _cmbEngine.SelectedIndex;
            PopulateEngines();                       // 重建引擎（丢弃旧模型工厂），确保立即换模型
            if (keep >= 0 && keep < _cmbEngine.Items.Count)
                _cmbEngine.SelectedIndex = keep;

            LogLine("已切换模型：" + name + "（下次【开始录音】生效）", UiTheme.TextSub);
        }

        /// <summary>true = 旧的实时链路（VAD 自动断句）；false = 按录音（停止后整段送模型，默认）。</summary>
        private bool IsRealtime()
        {
            return _chkRealtime != null && _chkRealtime.Checked;
        }

        /// <summary>模式说明文案随「实时字幕」复选框切换。</summary>
        private void UpdateModeUi()
        {
            if (IsDisposed || _lblMode == null)
                return;
            _lblMode.Text = IsRealtime()
                ? "VAD：静音 600ms 断句 / 最短语音 250ms / 最长段 8s（参数见 TranslatorCore.SimpleVad）"
                : "按录音：点【开始录音】说话，点【停止并翻译】把整段 PCM 一次送模型（上限 "
                  + MaxRecordSeconds + "s，录音自动存 exe 同目录 records\\）";
        }

        private void UpdateButtons()
        {
            if (IsDisposed)
                return;
            bool hasDevice = _cmbDevice.SelectedItem is AudioInputDevice;
            bool rt = IsRealtime();
            _btnStart.Text = rt ? "开始采集" : "开始录音";
            _btnStop.Text = rt ? "停止采集" : "停止并翻译";
            _btnStart.Enabled = !_running && !_busy && hasDevice;
            _btnStop.Enabled = _running && !_busy;
            _cmbDevice.Enabled = !_running && !_busy;
            _btnRefreshDev.Enabled = !_running && !_busy;
            if (_chkRealtime != null)
                _chkRealtime.Enabled = !_running && !_busy;
            UpdatePttUi();
        }

        // ---------- 鼠标语音键（K4：经 Dongle 私有协议 SPP 0x23 边沿） ----------

        /// <summary>
        /// 进入页面后打开 Dongle Host Link 通道：K4 按下/松开经鼠标 SPP(0x33F4)
        /// → dongle 0xE3 分片 → 本页 SppFrameEvent 到达（不经产测模式，日常可用）。
        /// 与产测页等模态页互斥（HID 通道独占），页面关闭即释放。
        /// </summary>
        private void InitPttLink()
        {
            if (_pttLink != null || IsDisposed)
                return;

            _pttLink = new DongleLink();
            _pttLink.SppFrameEvent += OnPttSppFrame;

            _pttLinkTimer = new System.Windows.Forms.Timer();
            _pttLinkTimer.Interval = 50;   // 与产测页一致：50ms 排空上行帧
            _pttLinkTimer.Tick += OnPttLinkTimer;
            _pttLinkTimer.Start();
            UpdatePttUi();
        }

        /// <summary>50ms：已连接则循环排空上行帧；未连接每 2s 静默重连一次（dongle 可后插）。</summary>
        private void OnPttLinkTimer(object sender, EventArgs e)
        {
            if (_pttLink == null || IsDisposed)
                return;

            if (!_pttLink.IsConnected)
            {
                int now = Environment.TickCount;
                if (now - _pttLinkRetryTick < 2000)
                    return;
                _pttLinkRetryTick = now;

                string err = _pttLink.Connect();
                if (err == null)
                {
                    string path = _pttLink.GetOpenedPath();
                    LogLine("自动录音已就绪：按住鼠标 K4 说话，松开自动停止并翻译朗读。本窗口不在前台也有效。"
                          + (string.IsNullOrEmpty(path) ? "" : "（Dongle " + ShortDevicePath(path) + "）"),
                        UiTheme.TextSub);
                }
                // 连接失败（dongle 未插/被其它页面占用）静默重试，不打扰用户（状态行有提示）
                UpdatePttUi();
                return;
            }

            for (int i = 0; i < 64; i++)
            {
                int r = _pttLink.Pump(0);
                if (r < 0)
                {
                    // dongle 拔出等：断开进入重连节奏；若这段录音由 dongle 通道触发（非 BLE
                    // 直连），提前收尾防 300s 挂起——鼠标 BLE 链路独占，两来源不会同时活跃，
                    // 但 PC 侧两个通道可并存，须按来源区分，BLE 触发的录音不受 dongle 断链影响。
                    _pttLink.Disconnect();
                    if (_pttRecording && !_pttFromBle)
                    {
                        LogLine("自动录音：Dongle 通道断开，本段录音提前收尾。", UiTheme.Warn);
                        HandlePttReleased();
                    }
                    UpdatePttUi();
                    return;
                }
                if (r == 0)
                    break;
            }
        }

        /// <summary>SppFrameEvent（UI 线程）：0x23 K4 边沿 → 开始/结束录音（复用原 PTT 录音链路）。</summary>
        private void OnPttSppFrame(byte[] spp)
        {
            bool pressed;
            if (!UxFtProto.ParseK4(spp, out pressed))
                return;

            if (pressed)
                OnPttPressed();
            else
                OnPttReleased();
        }

        // ---------- K4 语音键（BLE 直连场景：鼠标 → PC 的 0x33F4 SPP 帧） ----------
        // 鼠标固件不区分中心设备：ux_ft_dut_k4_report 只要 BLE 连着就发 0x23 帧，
        // 所以 dongle 场景（0x33F4 → dongle 0xE3 → DongleLink）与直连场景
        // （0x33F4 → BleDirectLink.SppReceived）是同一段帧的两个入口，互斥共存：
        // 鼠标 BLE 链路独占，连 PC 就不连 dongle，反之亦然。
        // 直连场景的目标音源是蓝牙语音流（0xB03E mSBC，与 USB 采集二选一），
        // 故按下沿的动作是自动【启用语音】而不是 USB 录音。

        /// <summary>
        /// SppReceived 回调（WinRT 线程池线程）：只认 0x23 K4 边沿帧，
        /// 其它 SPP 帧（鉴权 0x77/0x78 等）与本页无关，切回 UI 线程后再动采集状态。
        /// </summary>
        private void OnBleSppFrame(byte[] frame)
        {
            if (IsDisposed)
                return;

            bool pressed;
            if (!UxFtProto.ParseK4(frame, out pressed))
                return;

            try
            {
                if (pressed)
                    BeginInvoke(new Action(OnBleK4Pressed));
                else
                    BeginInvoke(new Action(OnBleK4Released));
            }
            catch { /* 窗口关闭竞态 */ }
        }

        /// <summary>K4 按下（直连）：空闲时自动【启用语音】，mSBC 流入翻译管线。</summary>
        private async void OnBleK4Pressed()
        {
            if (IsDisposed || _chkPtt == null || !_chkPtt.Checked)
                return;

            if (_bleVoiceActive)
            {
                // 蓝牙语音已在跑（手动启用/实时字幕）：流已在管线里，直接说话即可
                LogLine("自动录音：蓝牙语音已在采集中，本次 K4 无须再启用（直接说话即可）。", UiTheme.TextSub);
                return;
            }
            if (_running)
            {
                LogLine("自动录音：USB 音频采集中，本次 K4 忽略（蓝牙语音与 USB 采集二选一）。", UiTheme.Warn);
                return;
            }
            if (_busy)
            {
                LogLine("自动录音：上一段还在识别/翻译，本次未开始（说完再按一次 K4 即可）。", UiTheme.Warn);
                return;
            }

            // 先掐掉正在朗读的上一句：否则麦克风会把 TTS 的声音录进去。
            if (_tts != null)
                _tts.Stop();

            _pttRecording = true;
            _pttFromBle = true;    // BLE 直连触发：dongle 断链兜底不误伤本段
            LogLine("自动录音：K4 → 启用蓝牙语音（mSBC 流入翻译管线，松开自动结束）。", UiTheme.Success);
            bool ok = await TryStartBleVoiceAsync();

            // 极短按竞态：松开沿在 CCCD 订阅完成前到达（OnBleK4Released 兜底已落 _pttRecording），
            // 语音刚启起来就没人收尾了 → 立即按“松开”处理（送模型 + 统一收尾）。
            if (ok && !_pttRecording && _bleVoiceActive)
                OnBleVoiceOff(this, EventArgs.Empty);
            UpdatePttUi();
        }

        /// <summary>K4 松开（直连）：自动【停用语音】→ OnStop 统一收尾（录音模式整段送模型）。</summary>
        private void OnBleK4Released()
        {
            if (!_pttRecording)
                return;

            if (_bleVoiceActive)
            {
                LogLine("自动录音：K4 松开 → 停用蓝牙语音并翻译。", UiTheme.Success);
                OnBleVoiceOff(this, EventArgs.Empty);   // → OnStop：CCCD 退订 + 统一收尾（内部会落 _pttRecording）
            }
            else
            {
                // 启用失败的兜底（OnStop 没走到）：只落标志，不碰采集状态
                _pttRecording = false;
                UpdatePttUi();
            }
        }

        private void OnPttToggleChanged()
        {
            // 取消勾选时如果正由语音键录音，先把这一段收尾（停止 → 翻译），
            // 否则 K4 松开边沿会被 HandlePttPressed 入口拦下，只能等 300s 上限。
            // （Dongle 通道本身不停：重新勾上立即可用，与其它页面的模态互斥不受影响。）
            if (_chkPtt != null && !_chkPtt.Checked && _pttRecording)
                HandlePttReleased();

            UpdatePttUi();
        }

        /// <summary>SPP 帧事件在 UI 线程的 Pump 定时器里触发；延后一拍执行，避免在回调中重入采集。</summary>
        private void OnPttPressed()
        {
            if (IsDisposed)
                return;
            try { BeginInvoke(new Action(HandlePttPressed)); }
            catch { /* 窗口关闭竞态 */ }
        }

        private void OnPttReleased()
        {
            if (IsDisposed)
                return;
            try { BeginInvoke(new Action(HandlePttReleased)); }
            catch { /* 窗口关闭竞态 */ }
        }

        /// <summary>K4 按下：自动开始一段录音（等同于点【开始录音】）。</summary>
        private void HandlePttPressed()
        {
            if (IsDisposed || _chkPtt == null || !_chkPtt.Checked)
                return;

            if (_bleVoiceActive)
            {
                // 蓝牙语音正在采集（实时字幕/翻译已在跑）：语音键自动录音无须再插一层
                LogLine("自动录音：蓝牙语音采集中，本次 K4 忽略（说完的话已在进字幕）。", UiTheme.Warn);
                return;
            }
            if (_running)
            {
                LogLine("自动录音：已在录音中，忽略本次 K4。", UiTheme.Warn);
                return;
            }
            if (_busy)
            {
                LogLine("自动录音：上一段还在识别/翻译，本次未开始（说完再按一次 K4 即可）。", UiTheme.Warn);
                return;
            }

            var dev = ResolvePttDevice();
            if (dev == null)
            {
                LogLine("自动录音：没有可用的录音设备（Dongle 的 USB Audio 是否已插好/装好驱动）。", UiTheme.Warn);
                return;
            }

            // 先掐掉正在朗读的上一句：否则麦克风会把 TTS 的声音录进去。
            if (_tts != null)
                _tts.Stop();

            _pttRecording = true;
            _pttFromBle = false;   // dongle 触发：断链兜底生效
            OnStart(this, EventArgs.Empty);

            string device = string.Empty;
            if (_pttLink != null && _pttLink.IsConnected)
            {
                string p = _pttLink.GetOpenedPath();
                if (!string.IsNullOrEmpty(p))
                    device = " · " + ShortDevicePath(p);
            }
            LogLine("自动录音：K4 → 开始（" + dev.Name + device + "）", UiTheme.Success);
            UpdatePttUi();
        }

        /// <summary>K4 松开：自动停止 → 整段识别翻译 → 朗读译文（沿用现有链路）。</summary>
        private void HandlePttReleased()
        {
            if (!_pttRecording)
                return;
            _pttRecording = false;

            if (_running)
            {
                LogLine("自动录音：K4 松开 → 停止并翻译。", UiTheme.Success);
                OnStop(this, EventArgs.Empty);
            }
            UpdatePttUi();
        }

        /// <summary>
        /// 语音键录音时优先用 dongle 的 USB Audio（名字里带 Dongle / BLE Remote / KMA；
        /// 固件现用产品名「Biboo OS Audio Dongle」，命中的是 "Dongle"）。
        /// 下拉框选中别的麦克风也没关系：这里会临时切到 dongle 声卡，保证录到的是鼠标麦克风。
        /// </summary>
        private AudioInputDevice ResolvePttDevice()
        {
            var current = _cmbDevice != null ? _cmbDevice.SelectedItem as AudioInputDevice : null;
            if (current != null && IsDongleAudioName(current.Name))
                return current;

            if (_cmbDevice == null || _cmbDevice.Items.Count == 0)
            {
                RefreshDevices();
                if (_cmbDevice == null || _cmbDevice.Items.Count == 0)
                    return null;
                current = _cmbDevice.SelectedItem as AudioInputDevice;
            }

            for (int i = 0; i < _cmbDevice.Items.Count; i++)
            {
                var it = _cmbDevice.Items[i] as AudioInputDevice;
                if (it != null && IsDongleAudioName(it.Name))
                {
                    _cmbDevice.SelectedIndex = i;
                    LogLine("自动录音：输入设备切换到 " + it.Name, UiTheme.TextSub);
                    return it;
                }
            }

            // 没有 dongle 声卡（比如用普通麦克风测试）：沿用用户当前选择。
            return current;
        }

        private static bool IsDongleAudioName(string name)
        {
            if (string.IsNullOrEmpty(name))
                return false;
            string n = name.ToLowerInvariant();
            // "biboo os" 是兜底：WinMM 的 szPname 只有 32 字节，多设备时 Windows 会加
            // "(2- " 前缀，新名「麦克风 (2- Biboo OS Audio Dongle)」正好顶到 31 字上限，
            // 尾部 "e" 可能被截掉 → "dongle" 匹配不到。产品名前缀不会先被截。
            return n.Contains("dongle") || n.Contains("ble remote") || n.Contains("kma")
                || n.Contains("biboo os");
        }

        /// <summary>设备接口路径太长，日志里只留 VID/PID 段（便于确认是哪台设备触发的）。</summary>
        private static string ShortDevicePath(string path)
        {
            if (string.IsNullOrEmpty(path))
                return string.Empty;
            int v = path.IndexOf("VID_", StringComparison.OrdinalIgnoreCase);
            int p = path.IndexOf("PID_", StringComparison.OrdinalIgnoreCase);
            if (v >= 0 && p > v)
            {
                int end = path.IndexOf('&', p);
                if (end < 0)
                    end = Math.Min(path.Length, p + 9);
                return path.Substring(v, end - v);
            }
            return path.Length <= 64 ? path : path.Substring(0, 64) + "…";
        }

        private void UpdatePttUi()
        {
            if (IsDisposed || _lblPtt == null)
                return;

            if (_chkPtt == null || !_chkPtt.Checked)
            {
                _lblPtt.Text = "○ 自动录音已关闭（仍可用按钮手动录音）";
                _lblPtt.ForeColor = UiTheme.TextSub;
                return;
            }
            if (_running && _pttRecording)
            {
                _lblPtt.Text = "● 语音键录音中：松开 K4 自动结束并翻译"
                             + (_pttFromBle ? "（蓝牙直连）" : "（Dongle）");
                _lblPtt.ForeColor = UiTheme.Success;
                return;
            }
            if (_busy)
            {
                _lblPtt.Text = "◐ 正在识别/翻译上一段…";
                _lblPtt.ForeColor = UiTheme.Warn;
                return;
            }

            // 就绪态按可用通道显示：BLE 直连（含语音特征）优先，其次 dongle（USB 录音链路），
            // 都没连上时等待重连（两通道后台都在尝试，任一可用即切换到对应场景）。
            if (_ble.IsLinkVerified && _ble.HasVoiceChar)
            {
                _lblPtt.Text = "◐ 已就绪：按住 K4 说话 · 松开结束（蓝牙直连，自动用蓝牙语音）";
                _lblPtt.ForeColor = UiTheme.TextSub;
                return;
            }
            if (_pttLink != null && _pttLink.IsConnected)
            {
                _lblPtt.Text = "◐ 已就绪：按住 K4 说话 · 松开结束（经 Dongle，自动用 Dongle 声卡）";
                _lblPtt.ForeColor = UiTheme.TextSub;
                return;
            }
            _lblPtt.Text = "○ 等待通道：蓝牙直连或 Dongle 任一连上即可用（后台自动重连）";
            _lblPtt.ForeColor = UiTheme.TextSub;
        }

        // ---------- 采集控制 ----------

        private void OnStart(object sender, EventArgs e)
        {
            if (_bleVoiceActive)
            {
                // 二选一：蓝牙语音正在采集，USB 音频不能再同时开（否则两个音源都喂 VAD）
                LogLine("蓝牙语音采集中：请先【停用语音】（或点【停止采集】）再启动 USB 音频。", UiTheme.Warn);
                return;
            }

            var dev = _cmbDevice.SelectedItem as AudioInputDevice;
            if (dev == null)
            {
                LogLine("请先选择音频输入设备。", UiTheme.Warn);
                return;
            }

            bool rt = IsRealtime();
            _running = true;
            _lastRms = 0f;
            _lastRecSec = 0;
            _lastRecUiSec = 0;
            lock (_recGate)
            {
                _recBuf.Clear();
                _recBytes = 0;
            }
            _capture.DataAvailable += OnAudioData;
            _capture.Failed += OnCaptureFailed;
            _vad.LevelUpdated += OnLevelUpdated;          // 两种模式都用它刷电平条
            _vad.Reset();
            if (rt)
                _vad.SegmentReady += OnSegmentReady;      // 只有实时模式才自动断句
            _capture.Start(dev.Index);
            UpdateButtons();

            if (rt)
                LogLine("开始采集：" + dev.Name + "（16kHz/mono/16bit）。对麦克风说话试试。",
                    UiTheme.Success);
            else
                LogLine("开始录音：" + dev.Name + "（16kHz/mono/16bit）。说完点【停止并翻译】，"
                      + "整段 PCM 一次送模型，最长 " + MaxRecordSeconds + "s。", UiTheme.Success);
        }

        private void OnStop(object sender, EventArgs e)
        {
            if (!_running)
                return;

            _pttRecording = false;      // 手动停止 / 语音键收尾都走这里，统一落状态

            if (_tts != null)
                _tts.Stop();          // 停止采集时顺手打断正在念的译文

            bool rt = IsRealtime();
            bool fromBle = _bleVoiceActive;   // 停的是哪个音源（二选一，二者不会同时为真）
            _running = false;

            if (fromBle)
            {
                // 停蓝牙语音：先退订 CCCD（鼠标不再推 mSBC），再同步收尾（解 VAD/清队列）。
                // 录音模式下后面与 USB 路径走同一段「整段送模型」逻辑。
                _ = StopBleVoiceWithCccdAsync();
                StopBleVoiceSync("用户停止");
            }
            else
            {
                _capture.Stop();
                _capture.DataAvailable -= OnAudioData;
                _capture.Failed -= OnCaptureFailed;
                _vad.SegmentReady -= OnSegmentReady;
                _vad.LevelUpdated -= OnLevelUpdated;
            }
            lock (_queueGate)
            {
                _pendingPcm.Clear();
                _pendingSec.Clear();
            }
            SetLevelUi(0f);

            if (rt)
            {
                UpdateButtons();
                LogLine(fromBle ? "已停止蓝牙语音采集。" : "已停止采集。", UiTheme.TextSub);
                return;
            }

            FlushRecordedSegment();
        }

        /// <summary>用户主动停用：退订语音 CCCD（幂等；失败只记日志，不影响本地收尾）。</summary>
        private async Task StopBleVoiceWithCccdAsync()
        {
            try
            {
                string err = await _ble.StopVoiceAsync();
                if (err != null)
                    LogLine("退订语音 notify 未完全成功：" + err
                          + "（本地已停止解码；鼠标侧最坏情况多推几帧，无副作用）", UiTheme.Warn);
            }
            catch (Exception ex)
            {
                LogLine("退订语音 notify 异常：" + ex.Message, UiTheme.Warn);
            }
        }

        /// <summary>录音模式收尾：把累积的整段 PCM 一次送模型（USB 与蓝牙语音两条音源共用）。</summary>
        private void FlushRecordedSegment()
        {
            byte[] buf;
            lock (_recGate)
            {
                buf = _recBuf.ToArray();
                _recBuf.Clear();
                _recBytes = 0;
            }
            double sec = BytesToSeconds(buf.Length);
            if (sec < MinRecordSeconds)
            {
                UpdateButtons();
                LogLine("录音过短（" + sec.ToString("0.00") + "s），未送模型。", UiTheme.Warn);
                return;
            }

            _busy = true;
            UpdateButtons();
            LogLine("录音结束：" + sec.ToString("0.0") + "s / " + (buf.Length / 1024) + " KB，正在识别翻译…",
                UiTheme.TextSub);
            string saved = SaveWav(buf);
            if (saved != null)
                LogLine("录音已存：" + saved, UiTheme.TextSub);

            short[] pcm = ToShortArray(buf);
            ThreadPool.QueueUserWorkItem(delegate { RunRecorded(pcm, sec); });
        }

        private void RunRecorded(short[] pcm, double seconds)
        {
            try
            {
                ProcessSegmentAsync(pcm, seconds).GetAwaiter().GetResult();
            }
            catch (Exception ex)
            {
                LogLine("整段处理异常：" + ex.Message, UiTheme.Danger);
            }
            finally
            {
                _busy = false;
                try
                {
                    if (!IsDisposed)
                        BeginInvoke(new Action(UpdateButtons));
                }
                catch
                {
                    // 窗口关闭竞态：忽略。
                }
            }
        }

        private static double BytesToSeconds(long bytes)
        {
            return bytes / 2.0 / UsbAudioCapture.SampleRate;   // 16bit = 2 字节/采样点
        }

        private static short[] ToShortArray(byte[] buf)
        {
            var s = new short[buf.Length / 2];
            Buffer.BlockCopy(buf, 0, s, 0, s.Length * 2);
            return s;
        }

        /// <summary>把录到的 PCM 存成 16kHz/mono/16bit 的 WAV，便于复现测试与对比引擎。返回路径，失败返回 null。</summary>
        private string SaveWav(byte[] pcm)
        {
            try
            {
                string dir = Path.Combine(Application.StartupPath, "records");
                Directory.CreateDirectory(dir);
                string path = Path.Combine(dir, "rec_" + DateTime.Now.ToString("yyyyMMdd_HHmmss") + ".wav");

                const short bits = 16;
                const short channels = 1;
                int rate = UsbAudioCapture.SampleRate;
                using (var fs = new FileStream(path, FileMode.Create, FileAccess.Write))
                using (var bw = new BinaryWriter(fs))
                {
                    bw.Write(new[] { (byte)'R', (byte)'I', (byte)'F', (byte)'F' });
                    bw.Write(36 + pcm.Length);
                    bw.Write(new[] { (byte)'W', (byte)'A', (byte)'V', (byte)'E' });
                    bw.Write(new[] { (byte)'f', (byte)'m', (byte)'t', (byte)' ' });
                    bw.Write(16);
                    bw.Write((short)1);                          // PCM
                    bw.Write(channels);
                    bw.Write(rate);
                    bw.Write(rate * channels * bits / 8);        // byte rate
                    bw.Write((short)(channels * bits / 8));      // block align
                    bw.Write(bits);
                    bw.Write(new[] { (byte)'d', (byte)'a', (byte)'t', (byte)'a' });
                    bw.Write(pcm.Length);
                    bw.Write(pcm);
                }
                return path;
            }
            catch (Exception ex)
            {
                LogLine("录音存盘失败：" + ex.Message, UiTheme.Warn);
                return null;
            }
        }

        private void OnCaptureFailed(string msg)
        {
            if (IsDisposed)
                return;
            BeginInvoke(new Action(() =>
            {
                LogLine(msg, UiTheme.Danger);
                _running = false;
                UpdateButtons();
            }));
        }

        private void OnAudioData(byte[] pcm)
        {
            if (!_running)
                return;
            _vad.Feed(pcm);            // 电平条（LevelUpdated）在两种模式下都有效

            if (IsRealtime())
                return;                // 实时模式：分段由 OnSegmentReady 负责

            bool over;
            double sec;
            lock (_recGate)
            {
                _recBuf.AddRange(pcm);
                _recBytes += pcm.Length;
                sec = BytesToSeconds(_recBytes);
                over = sec >= MaxRecordSeconds;
            }
            if (sec - _lastRecUiSec >= 0.1)      // 每 0.1s 刷一次即可，别把 UI 消息队列灌满
            {
                _lastRecUiSec = sec;
                UpdateRecUi(sec);
            }

            if (over)
            {
                try
                {
                    if (!IsDisposed)
                        BeginInvoke(new Action(() =>
                        {
                            LogLine("已达录音上限 " + MaxRecordSeconds + "s，自动停止并送模型。", UiTheme.Warn);
                            OnStop(this, EventArgs.Empty);
                        }));
                }
                catch
                {
                    // 窗口关闭竞态：忽略。
                }
            }
        }

        private void UpdateRecUi(double seconds)
        {
            if (IsDisposed)
                return;
            try
            {
                BeginInvoke(new Action(() =>
                {
                    _lastRecSec = seconds;
                    RefreshLevelUi();
                }));
            }
            catch
            {
                // 窗口关闭竞态：忽略。
            }
        }

        private void OnLevelUpdated(float rms)
        {
            if (IsDisposed)
                return;
            try
            {
                BeginInvoke(new Action(() => SetLevelUi(rms)));
            }
            catch
            {
                // 窗口关闭竞态：忽略。
            }
        }

        private void SetLevelUi(float rms)
        {
            _lastRms = rms;
            RefreshLevelUi();
        }

        private void RefreshLevelUi()
        {
            int pct = (int)Math.Round(Math.Min(1.0, _lastRms * 8.0) * 100);   // ×8 放大显示，日常说话能到 60%+
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            _barLevel.Value = pct;
            _lblLevel.Text = IsRealtime()
                ? "RMS " + _lastRms.ToString("0.000")
                : "已录 " + _lastRecSec.ToString("0.0") + "s · RMS " + _lastRms.ToString("0.000");
        }

        // ---------- 模型预热（进入页面即后台加载，别让用户在第一句上干等） ----------

        /// <summary>
        /// 页面一显示就预热：SenseVoice 模型（228MB int8）与 llama-server（1.1GB gguf + 首句空跑）
        /// 原本都是「第一次翻译时才加载」，用户要等好几秒；现在把这些开销提前到进页面时，
        /// 后台线程执行、界面照常操作（可以立刻开始录音），录完点【停止并翻译】时模型通常已就绪。
        /// </summary>
        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            _shown = true;
            // 句柄就绪后再启「已配对设备」枚举（AEP watcher 在线程池线程回调，必须能 BeginInvoke 到 UI）
            if (_blePairedMode)
                _ble.StartPairedWatch();
            InitPttLink();       // 先挂 Dongle 通道：用户进页面后随时可以按 K4
            StartWarmup();
            // TTS 也一进页面就后台加载（2.8s）：等用户勾「朗读译文」再录完一句，
            // 模型早就绪了——否则首句要现场付 2.8s 加载 + 争 CPU，首音拖到 8s+。
            StartTtsWarmup();
        }

        /// <summary>取（必要时启动）该引擎的预热任务。同一引擎只做一次；换引擎/换模型会重新预热。</summary>
        private Task<bool> EnsureWarmupAsync(ITranslationEngine eng)
        {
            if (eng == null)
                return Task.FromResult(true);

            string src = _srcLang != null ? _srcLang.Code : "zh";
            string dst = _dstLang != null ? _dstLang.Code : "en";

            lock (_warmGate)
            {
                if (ReferenceEquals(_warmEngine, eng) && _warmTask != null)
                    return _warmTask;

                _warmEngine = eng;
                var w = eng as IWarmupEngine;
                if (w == null)
                {
                    _warmTask = Task.FromResult(true);    // 桩引擎等：无需预热
                    return _warmTask;
                }

                // Task.Run 再包一层：WarmupAsync 内部虽有 Task.Run，但个别实现可能在首个 await 前干活，
                // 这里保证调用瞬间立刻返回，绝不卡住 UI 线程。
                _warmTask = Task.Run(async () =>
                {
                    try { return await w.WarmupAsync(src, dst).ConfigureAwait(false); }
                    catch { return false; }
                });
                return _warmTask;
            }
        }

        /// <summary>界面侧预热：更新状态行 + 日志。进页面 / 换引擎 / 换模型时触发。</summary>
        private async void StartWarmup()
        {
            if (!_shown || IsDisposed)
                return;

            var eng = _engine;
            if (eng == null)
                return;

            if (!(eng is IWarmupEngine))
            {
                SetWarmupUi("— 当前引擎无需预热（桩引擎不加载模型）", UiTheme.TextSub);
                return;
            }

            var sw = Stopwatch.StartNew();
            SetWarmupUi("◐ 正在预热：" + eng.DisplayName + "（后台加载，页面可正常操作）", UiTheme.Warn);
            LogLine("开始预热：" + eng.DisplayName + "（后台加载模型 / 拉起本地服务）", UiTheme.TextSub);

            bool ok;
            try { ok = await EnsureWarmupAsync(eng); }
            catch { ok = false; }
            sw.Stop();

            if (IsDisposed || !ReferenceEquals(_engine, eng))
                return;   // 期间切了引擎：这条结果作废

            if (ok)
            {
                SetWarmupUi("● 模型已就绪 · 预热 " + sw.ElapsedMilliseconds + " ms（之后每句只付推理时间）",
                    UiTheme.Success);
                LogLine("预热完成：" + eng.DisplayName + " → " + sw.ElapsedMilliseconds + " ms，可以直接说话翻译了。",
                    UiTheme.Success);
            }
            else
            {
                SetWarmupUi("○ 预热未成功（模型缺失 / 本地服务起不来）：首次翻译会现场加载并报原因", UiTheme.Danger);
                LogLine("预热未成功：" + eng.DisplayName + "，请检查模型文件与 llama\\llama-server.exe 是否存在。",
                    UiTheme.Danger);
            }
        }

        private void SetWarmupUi(string text, Color color)
        {
            if (IsDisposed || _lblWarmup == null)
                return;
            if (InvokeRequired)
            {
                try { BeginInvoke(new Action(() => SetWarmupUi(text, color))); }
                catch { /* 关窗竞态 */ }
                return;
            }
            _lblWarmup.Text = text;
            _lblWarmup.ForeColor = color;
        }

        // ---------- 分段 → 引擎 ----------

        private void OnSegmentReady(short[] pcm, double seconds)
        {
            if (!_running)
                return;
            lock (_queueGate)
            {
                _pendingPcm.Enqueue(pcm);
                _pendingSec.Enqueue(seconds);
                if (_pumping)
                    return;
                _pumping = true;
            }
            // 单消费泵：串行处理，避免多段并发打进引擎
            ThreadPool.QueueUserWorkItem(delegate { PumpSegments(); });
        }

        private async void PumpSegments()
        {
            try
            {
                while (true)
                {
                    short[] pcm;
                    double sec;
                    lock (_queueGate)
                    {
                        if (_pendingPcm.Count == 0)
                        {
                            _pumping = false;
                            return;
                        }
                        pcm = _pendingPcm.Dequeue();
                        sec = _pendingSec.Dequeue();
                    }

                    await ProcessSegmentAsync(pcm, sec);
                }
            }
            finally
            {
                lock (_queueGate)
                {
                    if (_pendingPcm.Count > 0 && !_pumping)
                    {
                        _pumping = true;
                        ThreadPool.QueueUserWorkItem(delegate { PumpSegments(); });
                    }
                }
            }
        }

        /// <summary>
        /// 一段 PCM → 识别 → 翻译 → 上字幕。两种模式共用：
        /// 实时模式下由 VAD 逐段调用；录音模式下被整段（可能几十秒）调用一次。
        /// 异常与「识别为空」都在此内部消化，不向上抛。
        /// </summary>
        private async Task ProcessSegmentAsync(short[] pcm, double sec)
        {
            var sw = Stopwatch.StartNew();
            var engine = _engine;
            string src = _srcLang != null ? _srcLang.Code : "zh";
            string dst = _dstLang != null ? _dstLang.Code : "en";
            string text;
            string translated;
            try
            {
                // 预热未完成就在这里等（正常路径：进页面就开始预热，录完音时早已就绪 → 零等待）。
                // 也避免「预热线程 + 翻译线程」同时首次加载同一个模型。
                if (engine is IWarmupEngine)
                    await EnsureWarmupAsync(engine);

                if (engine.IsDirectTranslate)
                {
                    // 直译引擎（如 Whisper 直译→英语）：一次推理直接产出译文，「原文」列为空
                    translated = await engine.TranscribeAsync(pcm, src);
                    text = string.Empty;
                }
                else
                {
                    text = await engine.TranscribeAsync(pcm, src);
                    translated = await engine.TranslateTextAsync(text, src, dst);
                }
            }
            catch (Exception ex)
            {
                LogLine("引擎处理异常（" + ex.GetType().Name + "）：" + ex.Message, UiTheme.Danger);
                return;
            }
            sw.Stop();

            // 原文与译文都为空（多为噪声段）：不占字幕行，仅记一条日志，保持字幕干净
            if (string.IsNullOrWhiteSpace(text) && string.IsNullOrWhiteSpace(translated))
            {
                LogLine("段 " + sec.ToString("0.0") + "s → 引擎 " + sw.ElapsedMilliseconds
                      + "ms：识别为空（疑似噪声，已跳过）", UiTheme.TextSub);
                return;
            }

            AddSubtitleRow(DateTime.Now, sec, text, translated, sw.ElapsedMilliseconds);

            // 勾了「朗读译文」就把译文送 TTS（fire-and-forget，内部串行播放）
            SpeakText(translated);
        }

        private void AddSubtitleRow(DateTime ts, double seconds, string text, string translated, long costMs)
        {
            if (IsDisposed)
                return;
            try
            {
                BeginInvoke(new Action(() =>
                {
                    var item = new ListViewItem(ts.ToString("HH:mm:ss"));
                    item.SubItems.Add(text ?? string.Empty);
                    item.SubItems.Add(translated ?? string.Empty);
                    _lstSubtitle.Items.Add(item);
                    _lstSubtitle.EnsureVisible(_lstSubtitle.Items.Count - 1);

                    _segmentCount++;
                    _audioSeconds += seconds;
                    _lastCostMs = costMs;
                    _avgCostMs = _avgCostMs == 0
                        ? costMs
                        : _avgCostMs * 0.8 + costMs * 0.2;
                    _lblStats.Text = string.Format("段数 {0} · 音频 {1:0.0}s · 平均耗时 {2:0}ms",
                        _segmentCount, _audioSeconds, _avgCostMs);

                    LogLine(string.Format("段 {0:0.0}s → 引擎 {1}ms：{2}",
                        seconds, costMs, text), UiTheme.Primary);
                }));
            }
            catch
            {
                // 窗口关闭竞态：忽略。
            }
        }

        // ---------- 译文朗读（TTS：sherpa-onnx Kokoro int8，中 + 英，103 音色） ----------

        /// <summary>当前音色 id：用户手动改过就用填的，否则按目标语言自动选
        /// （中文/粤语 → 中文音色 45，其余 → 英文音色 0）。</summary>
        private int CurrentSpeakerId()
        {
            if (_sidUserSet && _numSid != null)
            {
                int sid;
                if (int.TryParse(_numSid.Text.Trim(), out sid) && sid >= 0)
                    return sid;
            }
            string dst = _dstLang != null ? _dstLang.Code : "en";
            if (dst == "zh" || dst == "yue" || dst == "zh-TW")
                return TtsEngine.DefaultSpeakerId;
            return TtsEngine.DefaultEnglishSpeakerId;
        }

        /// <summary>当前输出设备号（NAudio WaveOut 索引；-1 = 系统默认）。</summary>
        private int CurrentOutDevice()
        {
            return _cmbOut != null && _cmbOut.SelectedIndex >= 0 ? _cmbOut.SelectedIndex : -1;
        }

        /// <summary>后台预热 TTS 模型。模型不存在/未勾开关都不加载，只把状态行显示出来。</summary>
        private void StartTtsWarmup()
        {
            if (_ttsWarmStarted)
                return;
            _ttsWarmStarted = true;

            if (!TtsEngine.IsModelPresent)
            {
                LogLine("TTS：未找到模型（" + TtsEngine.ModelDir + "），朗读不可用；"
                      + "把 kokoro-int8-multi-lang-v1_1 整个目录放到那里即可启用。", UiTheme.Warn);
                UiInvoke(UpdateTtsStatus);
                return;
            }

            Task.Run(() =>
            {
                try
                {
                    var t = _tts ?? (_tts = new TtsEngine());
                    string msg;
                    if (!t.Warmup(out msg, CurrentSpeakerId()))
                    {
                        LogLine("TTS：" + msg, UiTheme.Warn);
                        UiInvoke(UpdateTtsStatus);
                        return;
                    }
                    LogLine("TTS 已预热：" + msg + "（音频卡勾「朗读译文」才会出声）", UiTheme.TextSub);
                }
                catch (Exception ex)
                {
                    LogLine("TTS 预热异常：" + ex.Message, UiTheme.Warn);
                }
                UiInvoke(UpdateTtsStatus);
            });
        }

        /// <summary>刷新 TTS 状态行。</summary>
        private void UpdateTtsStatus()
        {
            if (_lblTts == null)
                return;
            bool on = _chkSpeak != null && _chkSpeak.Checked;

            if (!TtsEngine.IsModelPresent) { _lblTts.Text = "⚠ 缺模型"; return; }
            if (_tts == null) { _lblTts.Text = on ? "◐ 加载中" : "○ 未加载"; return; }
            if (_tts.LoadFailed) { _lblTts.Text = "⚠ 加载失败"; return; }
            if (_tts.IsReady)
                _lblTts.Text = (on ? "● " : "○ ") + _tts.NumSpeakers + " 音色";
            else
                _lblTts.Text = on ? "◐ 加载中" : "○ 未加载";
        }

        /// <summary>朗读文本长度上限：流式后首音只等第一片（~4-6s），但整句播完仍要
        /// RTF≈2.17×音频时长，且期间占用朗读串行锁，超长就截断防队列积压。</summary>
        private const int MaxSpeakChars = 200;

        /// <summary>截断时优先在这些句末标点后断开（不在单词/子句中间硬切）。</summary>
        private static readonly char[] SpeakCutPunct = "。！？!?；;".ToCharArray();

        /// <summary>把超长文本压到朗读上限内：优先在句末标点处整句断开（不切断单词/句子，
        /// 也避免只因卡在 200 字整而把一句话劈成两半），前 1/2 内没有合适标点才硬切到上限。
        /// 配合 MaxSpeakChars 防止单句占用串行锁过久、把后面的句子全堵成队列积压。</summary>
        private static string TruncateForSpeak(string text, int max)
        {
            if (text == null || text.Length <= max) return text;
            int floor = max / 2;   // 断点太靠前会丢掉大半个句子，宁可贵点硬切
            for (int i = max - 1; i >= floor; i--)
            {
                if (Array.IndexOf(SpeakCutPunct, text[i]) >= 0)
                    return text.Substring(0, i + 1);
            }
            return text.Substring(0, max);   // 整段无句末标点：硬切到上限
        }

        /// <summary>送一段文本去朗读（勾了开关才播；试听用 force 强制播）。fire-and-forget。</summary>
        private void SpeakText(string text, bool force = false)
        {
            bool allow = force || (_btnTestTts != null && _btnTestTts.Focused);   // 试听不受开关限制
            if (_chkSpeak != null && !_chkSpeak.Checked && !allow)
                return;
            if (string.IsNullOrWhiteSpace(text))
                return;

            if (text.Length > MaxSpeakChars)
            {
                string full = text;
                text = TruncateForSpeak(text, MaxSpeakChars);
                if (text.Length < full.Length)
                    LogLine("TTS：文本过长（" + full.Length + " 字），只读前 " + text.Length
                        + " 字（合成比实时慢，防等待过久）。", UiTheme.TextSub);
            }

            try
            {
                if (_tts == null)
                    _tts = new TtsEngine();
                if (!_tts.IsReady)
                    StartTtsWarmup();

                if (_tts.IsReady)
                    _tts.Stop();                       // 打断上一句，新句子立刻接上

                var t = _tts;
                // ⚠ 不能用 TaskScheduler.FromCurrentSynchronizationContext()：本方法会被翻译管线
                // 的后台线程调用（ProcessSegmentAsync → SpeakText），那里 SynchronizationContext.Current
                // 为 null，直接抛 InvalidOperationException「当前的 SynchronizationContext 不能用作
                // TaskScheduler」——SpeakAsync 已启动所以照样出声，但续跑全丢、日志报错。
                // 改走 TaskScheduler.Default，UI 更新用 UiInvoke 切回去。
                t.SpeakAsync(text.Trim(), CurrentSpeakerId(), 1.0f, 1.0f, CurrentOutDevice())
                 .ContinueWith(prev =>
                 {
                     if (prev.Exception == null)
                         return;
                     var ex = prev.Exception.InnerException;
                     UiInvoke(() =>
                     {
                         LogLine("TTS 朗读失败：" + (ex != null ? ex.Message : "未知错误"), UiTheme.Warn);
                         UpdateTtsStatus();
                     });
                 }, TaskScheduler.Default);
            }
            catch (Exception ex)
            {
                LogLine("TTS 朗读异常：" + ex.Message, UiTheme.Warn);
            }
        }

        /// <summary>把动作切回 UI 线程执行。Translate 管线/预热都在后台线程回调 UI，
        /// 那里拿不到 SynchronizationContext，统一走这里；句柄没了就静默丢弃。</summary>
        private void UiInvoke(Action act)
        {
            if (act == null)
                return;
            if (IsHandleCreated)
            {
                try { BeginInvoke(act); return; }
                catch (Exception) { return; }   // 窗口正在关闭，状态行更新丢就丢了
            }
            if (_uiCtx != null)
            {
                _uiCtx.Post(_ => act(), null);
                return;
            }
            act();
        }

        /// <summary>试听一句：按目标语言给样例文本。Kokoro 只有中/英音色，其他语种会用英语音素念。</summary>
        private void OnTestTts(object sender, EventArgs e)
        {
            string dst = _dstLang != null ? _dstLang.Code : "en";
            string sample;
            if (dst == "zh" || dst == "yue" || dst == "zh-TW")
                sample = "你好，这是译文朗读测试，一二三四五。";
            else if (dst == "en")
                sample = "Hello, this is a speech synthesis test.";
            else
            {
                LogLine("Kokoro 模型只有中/英音色，" + (_dstLang != null ? _dstLang.Name : dst)
                      + " 会按英语音素念出来，仅供试听。", UiTheme.Warn);
                sample = "This model supports Chinese and English only.";
            }
            SpeakText(sample, true);
        }

        // ---------- 语言选择（可搜索面板） ----------

        /// <summary>弹出可搜索语言选择面板，选定后写回 field 并更新对应按钮文案。</summary>
        private void PickLang(ref LangItem field, Control anchor)
        {
            using (var picker = new LanguagePickerForm(LangItems, field))
            {
                var p = anchor.PointToScreen(new Point(0, anchor.Height + 2));
                picker.StartPosition = FormStartPosition.Manual;
                picker.Location = p;
                if (picker.ShowDialog(this) == DialogResult.OK && picker.Selected != null)
                {
                    field = picker.Selected;
                    if (ReferenceEquals(anchor, _btnSrcLang))
                        _btnSrcLang.Text = field.Name;
                    else
                        _btnDstLang.Text = field.Name;
                    LogLine("语言已选：" + field.Name + " (" + field.Code + ")", UiTheme.TextSub);
                }
            }
        }

        /// <summary>一键互换源/目标语言。</summary>
        private void OnSwapLang(object sender, EventArgs e)
        {
            var t = _srcLang;
            _srcLang = _dstLang;
            _dstLang = t;
            _btnSrcLang.Text = _srcLang.Name;
            _btnDstLang.Text = _dstLang.Name;
            LogLine("已互换源/目标语言：" + _srcLang.Name + " → " + _dstLang.Name, UiTheme.TextSub);
        }

        // ---------- 日志 ----------

        /// <summary>【清空日志】：清空日志面板内容。只清显示，不影响录音/翻译状态与统计。</summary>
        private void OnClearLog(object sender, EventArgs e)
        {
            if (_txtLog != null && !_txtLog.IsDisposed)
                _txtLog.Clear();
        }

        private void LogLine(string msg, Color color)
        {
            if (_txtLog == null || IsDisposed)
                return;
            if (_txtLog.InvokeRequired)
            {
                try
                {
                    BeginInvoke(new Action(() => LogLine(msg, color)));
                }
                catch
                {
                    // 窗口关闭竞态：忽略。
                }
                return;
            }
            if (_txtLog.TextLength > 60000)
                _txtLog.Clear();
            _txtLog.SelectionStart = _txtLog.TextLength;
            _txtLog.SelectionColor = UiTheme.Timestamp;
            _txtLog.AppendText(DateTime.Now.ToString("HH:mm:ss.fff") + "  ");
            _txtLog.SelectionColor = color;
            _txtLog.AppendText(msg + Environment.NewLine);
            _txtLog.ScrollToCaret();
        }

        /// <summary>可搜索语言选择面板（对标竞品 165 语言页）：搜索框 + 计数 + 双语名列表 + 已选打勾。</summary>
        private sealed class LanguagePickerForm : Form
        {
            private readonly LangItem[] _all;
            private readonly ListBox _list;
            private readonly Label _lblCount;
            private readonly TextBox _txtSearch;
            private LangItem _current;

            public LangItem Selected { get; private set; }

            public LanguagePickerForm(LangItem[] all, LangItem current)
            {
                _all = all;
                _current = current;
                Selected = null;

                FormBorderStyle = FormBorderStyle.None;
                BackColor = Color.White;
                StartPosition = FormStartPosition.Manual;
                Width = 320;
                Height = 380;
                ShowInTaskbar = false;

                Paint += (s, e) =>
                {
                    using (var pen = new Pen(UiTheme.CardBorder))
                        e.Graphics.DrawRectangle(pen, 0, 0, Width - 1, Height - 1);
                };

                _txtSearch = new TextBox();
                _txtSearch.SetBounds(10, 10, Width - 20, 24);
                _txtSearch.BorderStyle = BorderStyle.FixedSingle;
                _txtSearch.Font = UiTheme.FontBody;
                _txtSearch.TextChanged += (s, e) => Filter(_txtSearch.Text);
                _txtSearch.KeyDown += OnSearchKeyDown;

                _lblCount = new Label();
                _lblCount.SetBounds(10, 38, Width - 20, 18);
                _lblCount.ForeColor = UiTheme.TextSub;
                _lblCount.Font = new Font(UiTheme.FontBody.FontFamily, 9f);
                _lblCount.TextAlign = ContentAlignment.MiddleCenter;

                _list = new ListBox();
                _list.SetBounds(10, 60, Width - 20, Height - 70);
                _list.BorderStyle = BorderStyle.None;
                _list.BackColor = Color.White;
                _list.ForeColor = UiTheme.TextMain;
                _list.DrawMode = DrawMode.OwnerDrawFixed;
                _list.ItemHeight = 40;
                _list.Font = UiTheme.FontBody;
                _list.DoubleClick += (s, e) => Choose();
                _list.DrawItem += OnListDrawItem;

                Controls.AddRange(new Control[] { _txtSearch, _lblCount, _list });
                Filter("");
                Load += (s, e) => _txtSearch.Focus();
            }

            private void OnSearchKeyDown(object sender, KeyEventArgs e)
            {
                if (e.KeyCode == Keys.Escape) { DialogResult = DialogResult.Cancel; Close(); }
                else if (e.KeyCode == Keys.Enter) { if (_list.SelectedItem != null) Choose(); }
                else if (e.KeyCode == Keys.Down)
                {
                    int i = Math.Min(_list.Items.Count - 1, _list.SelectedIndex + 1);
                    if (i >= 0) { _list.SelectedIndex = i; _list.TopIndex = i; }
                    e.Handled = true;
                }
                else if (e.KeyCode == Keys.Up)
                {
                    if (_list.Items.Count > 0)
                    {
                        int i = Math.Max(0, _list.SelectedIndex - 1);
                        _list.SelectedIndex = i; _list.TopIndex = i;
                    }
                    e.Handled = true;
                }
            }

            private void Filter(string q)
            {
                _list.Items.Clear();
                if (string.IsNullOrWhiteSpace(q))
                {
                    _list.Items.AddRange(_all);
                }
                else
                {
                    var ql = q.Trim().ToLowerInvariant();
                    foreach (var it in _all)
                    {
                        if (it.Name.ToLowerInvariant().Contains(ql)
                            || it.Native.ToLowerInvariant().Contains(ql)
                            || it.Code.ToLowerInvariant().Contains(ql))
                            _list.Items.Add(it);
                    }
                }
                _lblCount.Text = "--- 总共 " + _list.Items.Count + " 种语言 ---";
                int idx = _list.Items.IndexOf(_current);
                _list.SelectedIndex = idx >= 0 ? idx : (_list.Items.Count > 0 ? 0 : -1);
            }

            private void OnListDrawItem(object sender, DrawItemEventArgs e)
            {
                e.DrawBackground();
                if (e.Index < 0) return;
                var it = _list.Items[e.Index] as LangItem;
                if (it == null) return;

                bool isCurrent = it.Equals(_current);
                bool selected = (e.State & DrawItemState.Selected) != 0;

                if (selected)
                {
                    using (var b = new SolidBrush(UiTheme.FormBg))
                        e.Graphics.FillRectangle(b, e.Bounds);
                }

                var fBold = new Font(UiTheme.FontBody, FontStyle.Bold);
                var fSub = new Font(UiTheme.FontBody.FontFamily, 9f);
                Color main = isCurrent ? UiTheme.Primary : UiTheme.TextMain;
                Color sub = UiTheme.TextSub;

                string head = isCurrent ? ("✓ " + it.Name) : it.Name;
                e.Graphics.DrawString(head, fBold, new SolidBrush(main), e.Bounds.X + 8, e.Bounds.Y + 4);
                e.Graphics.DrawString(it.Native + "  (" + it.Code + ")", fSub, new SolidBrush(sub),
                    e.Bounds.X + 8 + (isCurrent ? 20 : 0), e.Bounds.Y + 22);

                if ((e.State & DrawItemState.Focus) != 0)
                    e.DrawFocusRectangle();
            }

            private void Choose()
            {
                var it = _list.SelectedItem as LangItem;
                if (it == null) return;
                Selected = it;
                DialogResult = DialogResult.OK;
                Close();
            }
        }
    }
}
