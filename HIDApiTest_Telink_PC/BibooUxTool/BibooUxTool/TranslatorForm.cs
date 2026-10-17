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
    /// 实时翻译模块（框架版）：USB Audio 采集 → VAD 分段 → 翻译引擎 → 实时字幕。
    /// 当前引擎为桩实现（不加载模型），先把「采集→分段→识别→翻译→字幕」链路与 UI 搭好；
    /// 后续接 Whisper.net（ASR）+ Argos/OPUS-MT（MT）时只换 ITranslationEngine 实现，UI 不动。
    /// 音频源暂用 USB Audio（鼠标当 USB 声卡 / 普通麦克风均走标准 WaveIn），不走 BLE。
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

        // ---------- 实时字幕 ----------
        private ListView _lstSubtitle;
        private Label _lblStats;

        // ---------- 日志 ----------
        private RichTextBox _txtLog;

        // ---------- pipeline ----------
        private readonly UsbAudioCapture _capture = new UsbAudioCapture();
        private readonly SimpleVad _vad = new SimpleVad();
        private ITranslationEngine _engine;
        private bool _running;

        /// <summary>语言选项：中文名（Name，列表主行）+ 母语名（Native，列表副行）+ 引擎用 Code（BCP-47 风格）。
        /// 当前为框架占位列表，后续接 Whisper（ASR，99+ 语言）+ Argos/OPUS-MT（MT，55+ 语言）时按 Code 增删即可，
        /// 引擎侧按 Code 加载对应模型包。参考竞品「语言设置」页（165 种、可搜索、双语名对照）。</summary>
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

        private static readonly LangItem[] LangItems = new[]
        {
            new LangItem("zh",    "中文（简体）", "中文简体"),
            new LangItem("zh-TW", "中文（繁体）", "中文繁體"),
            new LangItem("en",    "英语",       "English"),
            new LangItem("ja",    "日语",       "日本語"),
            new LangItem("ko",    "韩语",       "한국어"),
            new LangItem("fr",    "法语",       "Français"),
            new LangItem("es",    "西班牙语",     "Español"),
            new LangItem("de",    "德语",       "Deutsch"),
            new LangItem("pt",    "葡萄牙语",     "Português"),
            new LangItem("ru",    "俄语",       "Русский"),
            new LangItem("it",    "意大利语",     "Italiano"),
            new LangItem("ar",    "阿拉伯语",     "العربية"),
            new LangItem("th",    "泰语",       "ไทย"),
            new LangItem("vi",    "越南语",      "Tiếng Việt"),
            new LangItem("id",    "印度尼西亚语",   "Indonesia"),
            new LangItem("ms",    "马来语",      "Melayu"),
            new LangItem("tl",    "菲律宾语",     "Filipino"),
            new LangItem("hi",    "印地语",      "हिन्दी"),
            new LangItem("bn",    "孟加拉语",     "বাংলা"),
            new LangItem("ur",    "乌尔都语",     "اردو"),
            new LangItem("fa",    "波斯语",      "فارسی"),
            new LangItem("tr",    "土耳其语",     "Türkçe"),
            new LangItem("nl",    "荷兰语",      "Nederlands"),
            new LangItem("pl",    "波兰语",      "Polski"),
            new LangItem("uk",    "乌克兰语",     "Українська"),
            new LangItem("cs",    "捷克语",      "Čeština"),
            new LangItem("sv",    "瑞典语",      "Svenska"),
            new LangItem("da",    "丹麦语",      "Dansk"),
            new LangItem("fi",    "芬兰语",      "Suomi"),
            new LangItem("no",    "挪威语",      "Norsk"),
            new LangItem("el",    "希腊语",      "Ελληνικά"),
            new LangItem("he",    "希伯来语",     "עברית"),
            new LangItem("hu",    "匈牙利语",     "Magyar"),
            new LangItem("ro",    "罗马尼亚语",    "Română"),
            new LangItem("bg",    "保加利亚语",    "Български"),
            new LangItem("hr",    "克罗地亚语",    "Hrvatski"),
            new LangItem("sr",    "塞尔维亚语",    "Српски"),
            new LangItem("sk",    "斯洛伐克语",    "Slovenčina"),
            new LangItem("sl",    "斯洛文尼亚语",   "Slovenščina"),
            new LangItem("lt",    "立陶宛语",     "Lietuvių"),
            new LangItem("lv",    "拉脱维亚语",    "Latviešu"),
            new LangItem("et",    "爱沙尼亚语",    "Eesti"),
            new LangItem("ca",    "加泰罗尼亚语",   "Català"),
            new LangItem("eu",    "巴斯克语",     "Euskara"),
            new LangItem("gl",    "加利西亚语",    "Galego"),
            new LangItem("cy",    "威尔士语",     "Cymraeg"),
            new LangItem("sw",    "斯瓦希里语",    "Kiswahili"),
            new LangItem("am",    "阿姆哈拉语",    "አማርኛ"),
            new LangItem("ne",    "尼泊尔语",     "नेपाली"),
            new LangItem("si",    "僧伽罗语",     "සිංහල"),
            new LangItem("km",    "高棉语",      "ខ្មែរ"),
            new LangItem("lo",    "老挝语",      "ລາວ"),
            new LangItem("my",    "缅甸语",      "မြန်မာ"),
            new LangItem("ka",    "格鲁吉亚语",    "ქართული"),
            new LangItem("az",    "阿塞拜疆语",    "Azərbaycan"),
            new LangItem("kk",    "哈萨克语",     "Қазақ"),
            new LangItem("uz",    "乌兹别克语",    "Oʻzbek"),
            new LangItem("mn",    "蒙古语",      "Монгол"),
            new LangItem("ta",    "泰米尔语",     "தமிழ்"),
            new LangItem("te",    "泰卢固语",     "తెలుగు"),
            new LangItem("ml",    "马拉雅拉姆语",   "മലയാളം"),
            new LangItem("kn",    "卡纳达语",     "ಕನ್ನಡ"),
            new LangItem("mr",    "马拉地语",     "मराठी"),
            new LangItem("pa",    "旁遮普语",     "ਪੰਜਾਬੀ"),
            new LangItem("af",    "南非荷兰语",    "Afrikaans"),
            new LangItem("is",    "冰岛语",      "Íslenska"),
        };

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

        /// <summary>内容列卡片（宽度由 LayoutContent 统一管理：限宽 + 居中）。</summary>
        private readonly List<RoundedPanel> _contentCards = new List<RoundedPanel>();

        /// <summary>内容列设计宽度（比设计基准略宽：字幕表需要横向空间）。</summary>
        private const int ContentDesignWidth = 780;

        public TranslatorForm()
        {
            BuildUi();
            RefreshDevices();
            PopulateEngines();
            RefreshModels();
            LogLine("实时翻译框架就绪：选音频输入设备 →【开始采集】，对麦克风说话即可。",
                UiTheme.TextSub);
            LogLine("引擎下拉含：桩（仅验证链路）/ SenseVoice+混元 Hy-MT2（33 语种）/ SenseVoice+OPUS-MT（中↔英）/ "
                  + "SenseVoice ASR / Whisper ASR / Whisper 直译→英语 / Whisper+Argos。", UiTheme.TextSub);
            LogLine("混元需 Models\\llm\\Hy-MT2-1.8B-Q4_K_M.gguf + llama\\llama-server.exe（首次翻译会自动拉起，全离线）；"
                  + "Whisper 需 Models\\ggml-base.bin；Argos MT 需本机起本地服务（见 TranslationConfig）。", UiTheme.TextSub);
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            _running = false;
            _capture.DataAvailable -= OnAudioData;
            _capture.Failed -= OnCaptureFailed;
            _vad.SegmentReady -= OnSegmentReady;
            _vad.LevelUpdated -= OnLevelUpdated;
            _capture.Dispose();
            // 关窗口时收掉我们自己拉起的 llama-server（只杀本进程启动的那个，用户手起的不动）
            LlmServerLauncher.Stop();
            base.OnFormClosed(e);
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
            cardAudio.SetBounds(12, 12, ContentDesignWidth - 24, 148);
            cardAudio.Anchor = AnchorStyles.Top;
            cardAudio.Controls.Add(MakeTitle("音频输入（USB Audio / 麦克风）"));

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
            _btnStart.Click += OnStart;

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
            _barLevel.SetBounds(56, 102, 560, 20);
            _barLevel.Minimum = 0;
            _barLevel.Maximum = 100;
            _barLevel.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

            _lblLevel = new Label();
            _lblLevel.SetBounds(628, 100, 112, 22);
            _lblLevel.ForeColor = UiTheme.TextSub;
            _lblLevel.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            _lblLevel.Text = "RMS 0.000";
            _lblLevel.TextAlign = ContentAlignment.MiddleLeft;

            var lblVad = new Label();
            lblVad.Text = "VAD：静音 600ms 断句 / 最短语音 250ms / 最长段 8s（参数见 TranslatorCore.SimpleVad）";
            lblVad.ForeColor = UiTheme.TextSub;
            lblVad.SetBounds(16, 124, ContentDesignWidth - 56, 18);
            lblVad.AutoSize = false;
            lblVad.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            lblVad.TextAlign = ContentAlignment.MiddleLeft;

            cardAudio.Controls.AddRange(new Control[] {
                lblDev, _cmbDevice, _btnRefreshDev, _btnStart, _btnStop,
                lblFmt, lblLv, _barLevel, _lblLevel, lblVad });
            _contentCards.Add(cardAudio);

            // 卡2：翻译引擎
            var cardEngine = new RoundedPanel();
            cardEngine.SetBounds(12, 172, ContentDesignWidth - 24, 140);
            cardEngine.Anchor = AnchorStyles.Top;
            cardEngine.Controls.Add(MakeTitle("翻译引擎（源/目标语言可搜索选择，后续接 Whisper.net + Argos）"));

            var lblSrc = new Label();
            lblSrc.Text = "源语言";
            lblSrc.ForeColor = UiTheme.TextSub;
            lblSrc.SetBounds(16, 38, 48, 22);
            lblSrc.TextAlign = ContentAlignment.MiddleLeft;

            _srcLang = LangItems[0];   // 默认 中文（简体, zh）
            _dstLang = LangItems[2];   // 默认 英语(en)

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

            _lblEngineNote = new Label();
            _lblEngineNote.Text = "推荐「SenseVoice + 离线翻译」：识别 + 中↔英翻译全在本机跑，不联网、不需要本地服务（单句译文 0.2~0.4s）。"
                               + "语言面板支持按中文名 / 母语名 / 代码搜索（如输入「ri」「日本語」「ja」均可命中日语）。";
            _lblEngineNote.ForeColor = UiTheme.TextSub;
            _lblEngineNote.SetBounds(16, 104, ContentDesignWidth - 56, 32);
            _lblEngineNote.AutoSize = false;
            _lblEngineNote.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            _lblEngineNote.TextAlign = ContentAlignment.TopLeft;

            cardEngine.Controls.AddRange(new Control[] {
                lblSrc, _btnSrcLang, _btnSwap, lblDst, _btnDstLang,
                lblEng, _cmbEngine, lblModel, _cmbModel, _lblEngineNote });
            _contentCards.Add(cardEngine);

            // 卡3：实时字幕
            var cardSub = new RoundedPanel();
            cardSub.SetBounds(12, 324, ContentDesignWidth - 24, 216);
            cardSub.Anchor = AnchorStyles.Top;
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

            // 卡4：日志（唯一四边锚定卡片：窗口拉高时吸收多余高度）
            var cardLog = new RoundedPanel();
            cardLog.SetBounds(12, 552, ContentDesignWidth - 24, 164);
            cardLog.Anchor = AnchorStyles.Top | AnchorStyles.Bottom;
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

            cardLog.Controls.Add(_txtLog);
            _contentCards.Add(cardLog);

            Controls.AddRange(new Control[] { cardAudio, cardEngine, cardSub, cardLog });

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
            return l;
        }

        /// <summary>内容列居中：宽于设计宽度时限宽居中，窄于设计宽度时等比收窄。</summary>
        private void LayoutContent()
        {
            const int margin = 12;
            int avail = ClientSize.Width - margin * 2;
            if (avail < 240)
                avail = 240;
            int w = Math.Min(ContentDesignWidth - 24, avail);
            int x = (ClientSize.Width - w) / 2;
            foreach (var card in _contentCards)
                card.SetBounds(x, card.Top, w, card.Height);
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
                    var idx = _cmbModel.Items.IndexOf(ModelLabel(current));
                    _cmbModel.SelectedIndex = idx >= 0 ? idx : 0;
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
                    var idx = _cmbModel.Items.IndexOf(cur);
                    _cmbModel.SelectedIndex = idx >= 0 ? idx : 0;
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

            LogLine("已切换模型：" + name + "（下次【开始采集】生效）", UiTheme.TextSub);
        }

        private void UpdateButtons()
        {
            if (IsDisposed)
                return;
            bool hasDevice = _cmbDevice.SelectedItem is AudioInputDevice;
            _btnStart.Enabled = !_running && hasDevice;
            _btnStop.Enabled = _running;
            _cmbDevice.Enabled = !_running;
            _btnRefreshDev.Enabled = !_running;
        }

        // ---------- 采集控制 ----------

        private void OnStart(object sender, EventArgs e)
        {
            var dev = _cmbDevice.SelectedItem as AudioInputDevice;
            if (dev == null)
            {
                LogLine("请先选择音频输入设备。", UiTheme.Warn);
                return;
            }

            _running = true;
            _capture.DataAvailable += OnAudioData;
            _capture.Failed += OnCaptureFailed;
            _vad.SegmentReady += OnSegmentReady;
            _vad.LevelUpdated += OnLevelUpdated;
            _vad.Reset();
            _capture.Start(dev.Index);
            UpdateButtons();

            LogLine("开始采集：" + dev.Name + "（16kHz/mono/16bit）。对麦克风说话试试。",
                UiTheme.Success);
        }

        private void OnStop(object sender, EventArgs e)
        {
            _running = false;
            _capture.Stop();
            _capture.DataAvailable -= OnAudioData;
            _capture.Failed -= OnCaptureFailed;
            _vad.SegmentReady -= OnSegmentReady;
            _vad.LevelUpdated -= OnLevelUpdated;
            lock (_queueGate)
            {
                _pendingPcm.Clear();
                _pendingSec.Clear();
            }
            SetLevelUi(0f);
            UpdateButtons();
            LogLine("已停止采集。", UiTheme.TextSub);
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
            _vad.Feed(pcm);
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
            int pct = (int)Math.Round(Math.Min(1.0, rms * 8.0) * 100);   // ×8 放大显示，日常说话能到 60%+
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            _barLevel.Value = pct;
            _lblLevel.Text = "RMS " + rms.ToString("0.000");
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

                    var sw = Stopwatch.StartNew();
                    string src = _srcLang != null ? _srcLang.Code : "zh";
                    string dst = _dstLang != null ? _dstLang.Code : "en";
                    string text;
                    string translated;
                    try
                    {
                        if (_engine.IsDirectTranslate)
                        {
                            // 直译引擎（如 Whisper 直译→英语）：一次推理直接产出译文，「原文」列为空
                            translated = await _engine.TranscribeAsync(pcm, src);
                            text = string.Empty;
                        }
                        else
                        {
                            text = await _engine.TranscribeAsync(pcm, src);
                            translated = await _engine.TranslateTextAsync(text, src, dst);
                        }
                    }
                    catch (Exception ex)
                    {
                        LogLine("引擎处理异常（" + ex.GetType().Name + "）：" + ex.Message, UiTheme.Danger);
                        continue;
                    }
                    sw.Stop();

                    // 原文与译文都为空（多为噪声段）：不占字幕行，仅记一条日志，保持字幕干净
                    if (string.IsNullOrWhiteSpace(text) && string.IsNullOrWhiteSpace(translated))
                    {
                        LogLine("段 " + sec.ToString("0.0") + "s → 引擎 " + sw.ElapsedMilliseconds + "ms：识别为空（疑似噪声，已跳过）", UiTheme.TextSub);
                        continue;
                    }

                    AddSubtitleRow(DateTime.Now, sec, text, translated, sw.ElapsedMilliseconds);
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
