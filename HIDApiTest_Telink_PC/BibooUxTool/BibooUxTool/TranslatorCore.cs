using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using NAudio.Wave;
using SherpaOnnx;
using Whisper.net;

namespace BibooUxTool
{
    /// <summary>录音设备信息（USB Audio 鼠标/麦克风在 Windows 中就是标准 WaveIn 设备，无需专用驱动识别）。</summary>
    public sealed class AudioInputDevice
    {
        public int Index { get; set; }
        public string Name { get; set; }

        public override string ToString()
        {
            return (Name ?? ("设备 " + Index));
        }
    }

    /// <summary>
    /// USB Audio 采集（NAudio WaveIn 封装）：请求 16kHz / 单声道 / 16-bit PCM。
    /// 设备原生格式不同时由 Windows 做 SRC 重采样；个别老旧设备不接受该格式时
    /// 构造即失败，通过 <see cref="Failed"/> 如实上报，绝不静默。
    /// </summary>
    public sealed class UsbAudioCapture : IDisposable
    {
        public const int SampleRate = 16000;
        public const int Channels = 1;
        public const int Bits = 16;

        /// <summary>采集到的 PCM16 小端缓冲（长度 = BytesRecorded）。</summary>
        public event Action<byte[]> DataAvailable;

        /// <summary>采集启动/运行失败（设备被占用、格式不支持等）。</summary>
        public event Action<string> Failed;

        private WaveInEvent _wave;

        public bool IsRecording
        {
            get { return _wave != null; }
        }

        /// <summary>枚举系统全部录音设备（USB Audio 设备插上即出现在这里）。</summary>
        /// <remarks>WinMM 的 ProductName 最长 31 字符，长名字会被系统截断（NAudio/Windows 限制）。</remarks>
        public static List<AudioInputDevice> ListDevices()
        {
            var list = new List<AudioInputDevice>();
            for (int i = 0; i < WaveIn.DeviceCount; i++)
            {
                var cap = WaveIn.GetCapabilities(i);
                list.Add(new AudioInputDevice
                {
                    Index = i,
                    Name = string.IsNullOrEmpty(cap.ProductName) ? ("设备 " + i) : cap.ProductName,
                });
            }
            return list;
        }

        public void Start(int deviceIndex)
        {
            Stop();
            try
            {
                var wave = new WaveInEvent
                {
                    DeviceNumber = deviceIndex,
                    WaveFormat = new WaveFormat(SampleRate, Bits, Channels),
                    BufferMilliseconds = 40,
                };
                wave.DataAvailable += OnDataAvailable;
                wave.RecordingStopped += OnRecordingStopped;
                wave.StartRecording();
                _wave = wave;
            }
            catch (Exception ex)
            {
                Failed?.Invoke("启动采集失败（" + ex.GetType().Name + "）：" + ex.Message
                    + "（排查：设备是否被其它程序独占、是否支持 16kHz 单声道）");
            }
        }

        public void Stop()
        {
            var wave = _wave;
            _wave = null;
            if (wave == null)
                return;
            try
            {
                wave.DataAvailable -= OnDataAvailable;
                wave.RecordingStopped -= OnRecordingStopped;
                wave.StopRecording();
            }
            catch
            {
                // 停止失败不阻塞调用方（设备可能已被拔出）。
            }
        }

        private void OnDataAvailable(object sender, WaveInEventArgs e)
        {
            if (e.BytesRecorded <= 0)
                return;
            var buf = new byte[e.BytesRecorded];
            Buffer.BlockCopy(e.Buffer, 0, buf, 0, e.BytesRecorded);
            DataAvailable?.Invoke(buf);
        }

        private void OnRecordingStopped(object sender, StoppedEventArgs e)
        {
            if (e.Exception != null)
                Failed?.Invoke("采集中断：" + e.Exception.Message);
        }

        public void Dispose()
        {
            Stop();
        }
    }

    /// <summary>
    /// 能量门限 VAD（语音活动检测）：把连续 PCM 流切成「语音段」。
    /// 20ms 帧 RMS 超过门限判为语音；静音持续 SilenceMs 判段结束；
    /// 短于 MinSpeechMs 的段丢弃（噪声/咳嗽），超过 MaxSegmentMs 强切（防一句话说太久）。
    /// 段尾会裁掉静音部分，只把有效语音交给下游引擎。
    /// </summary>
    public sealed class SimpleVad
    {
        private const int FrameMs = 20;

        // 16kHz × 20ms = 320 样本/帧
        private const int FrameSamples = 320;

        private readonly object _gate = new object();
        private List<short> _speech = new List<short>();
        private int _silenceFrames;
        private int _speechFrames;
        private int _voicedFrames;
        private int _tailSilenceSamples;
        private bool _inSpeech;

        /// <summary>RMS 门限（0..1）。安静办公室 0.010~0.020 可用；内置阵列麦/有底噪场景建议 0.020~0.030，
        /// 否则噪声会被切成小段喂给引擎（表现为空文本噪段）。</summary>
        public float Threshold { get; set; }

        /// <summary>静音持续多少 ms 判为一段结束。</summary>
        public int SilenceMs { get; set; }

        /// <summary>该段内“真正有声”的累计时长短于此值即丢弃（ms）。</summary>
        public int MinSpeechMs { get; set; }

        /// <summary>单段最长时长（ms），超出强切。</summary>
        public int MaxSegmentMs { get; set; }

        /// <summary>一段语音就绪：PCM16 样本 + 时长（秒）。</summary>
        public event Action<short[], double> SegmentReady;

        /// <summary>当前帧 RMS（0..1，UI 电平条用）。</summary>
        public event Action<float> LevelUpdated;

        public SimpleVad()
        {
            Threshold = 0.015f;
            SilenceMs = 600;
            MinSpeechMs = 250;
            MaxSegmentMs = 8000;
        }

        public void Reset()
        {
            lock (_gate)
            {
                _speech = new List<short>();
                _silenceFrames = 0;
                _speechFrames = 0;
                _voicedFrames = 0;
                _tailSilenceSamples = 0;
                _inSpeech = false;
            }
        }

        /// <summary>喂入一段 PCM16 小端缓冲（任意长度，内部按 20ms 帧切分）。</summary>
        public void Feed(byte[] pcm16)
        {
            if (pcm16 == null || pcm16.Length < 2)
                return;
            int frames = pcm16.Length / 2 / FrameSamples;
            for (int f = 0; f < frames; f++)
            {
                double sum = 0;
                for (int i = 0; i < FrameSamples; i++)
                {
                    int off = (f * FrameSamples + i) * 2;
                    short s = (short)(pcm16[off] | (pcm16[off + 1] << 8));
                    double v = s / 32768.0;
                    sum += v * v;
                }
                float rms = (float)Math.Sqrt(sum / FrameSamples);
                LevelUpdated?.Invoke(rms);

                lock (_gate)
                {
                    bool voiced = rms >= Threshold;
                    if (voiced || _inSpeech)
                    {
                        // 收帧（阈值判定帧本身也收，保证段首不丢字）
                        for (int i = 0; i < FrameSamples; i++)
                        {
                            int off = (f * FrameSamples + i) * 2;
                            _speech.Add((short)(pcm16[off] | (pcm16[off + 1] << 8)));
                        }
                    }

                    if (voiced)
                    {
                        _inSpeech = true;
                        _silenceFrames = 0;
                        _speechFrames++;
                        _voicedFrames++;
                        _tailSilenceSamples = 0;
                    }
                    else if (_inSpeech)
                    {
                        _silenceFrames++;
                        _speechFrames++;
                        _tailSilenceSamples += FrameSamples;   // 记录尾部静音长度，段末裁掉
                        if (_silenceFrames * FrameMs >= SilenceMs)
                            FlushLocked();
                    }

                    if (_speechFrames * FrameMs >= MaxSegmentMs)
                        FlushLocked();
                }
            }
        }

        private void FlushLocked()
        {
            // 裁掉尾部静音（只留有效语音）
            if (_tailSilenceSamples > 0 && _tailSilenceSamples < _speech.Count)
                _speech.RemoveRange(_speech.Count - _tailSilenceSamples, _tailSilenceSamples);

            double ms = _speech.Count / 16.0;   // 16 samples/ms
            // 只按“真正有声”的帧（_voicedFrames）判最短语音：否则底部噪声触发几帧、其余全是静音时
            // 也会被当成有效段交给引擎（表现为空文本噪段）。这一条比单纯调门限更能干掉空段。
            if (_speech.Count > 0 && _voicedFrames * FrameMs >= MinSpeechMs)
            {
                var seg = _speech.ToArray();
                SegmentReady?.Invoke(seg, ms / 1000.0);
            }

            _speech = new List<short>();
            _silenceFrames = 0;
            _speechFrames = 0;
            _voicedFrames = 0;
            _tailSilenceSamples = 0;
            _inSpeech = false;
        }
    }

    /// <summary>
    /// 翻译引擎接口：ASR（音频→原文文本）+ MT（原文→译文）。
    /// 框架阶段用桩实现跑通链路；后续换 Whisper.net（ASR）+ Argos/OPUS-MT（MT）即可，
    /// UI 与 pipeline 不用动。
    /// </summary>
    public interface ITranslationEngine
    {
        string DisplayName { get; }

        /// <summary>true = 直译引擎：TranscribeAsync 直接产出「译文」（如 Whisper 直译→英语），
        /// 不再调 TranslateTextAsync；此时字幕的「原文」列为空。</summary>
        bool IsDirectTranslate { get; }

        /// <summary>语音识别：16kHz/mono PCM16 → 文本。</summary>
        Task<string> TranscribeAsync(short[] pcm16, string lang);

        /// <summary>文本翻译。</summary>
        Task<string> TranslateTextAsync(string text, string srcLang, string dstLang);
    }

    /// <summary>
    /// 桩引擎：不加载任何模型，只如实返回占位文本，用于验证
    /// 「采集 → VAD → 引擎 → 字幕」整条框架链路。
    /// </summary>
    public sealed class StubTranslationEngine : ITranslationEngine
    {
        public string DisplayName
        {
            get { return "桩引擎（未接模型）"; }
        }

        public bool IsDirectTranslate
        {
            get { return false; }
        }

        // ComboBox 直接显示对象：ToString 返回友好名，否则下拉里会是类名
        public override string ToString()
        {
            return DisplayName;
        }

        public Task<string> TranscribeAsync(short[] pcm16, string lang)
        {
            double sec = (pcm16 == null ? 0 : pcm16.Length) / 16000.0;
            return Task.FromResult("[桩] 未接 ASR：" + sec.ToString("0.0") + "s / " + (pcm16 == null ? 0 : pcm16.Length) + " 样本");
        }

        public Task<string> TranslateTextAsync(string text, string srcLang, string dstLang)
        {
            return Task.FromResult("[桩] 未接 MT（" + srcLang + "→" + dstLang + "）：" + text);
        }
    }

    /// <summary>
    /// 翻译模块运行配置（框架阶段集中放这里，后续可下沉到配置文件/设置页）。
    /// </summary>
    public static class TranslationConfig
    {
        /// <summary>Whisper ggml 模型路径（多语种，如 ggml-base.bin / ggml-small.bin）。
        /// 下载：https://huggingface.co/ggerganov/whisper.cpp （whisper.cpp 的 .bin 模型，base 及以上默认多语种）。
        /// 默认放 exe 同目录的 Models\ 下。
        /// ⚠ 换模型型号（tiny/base/small/medium）时要同步改这里的文件名，否则仍报「模型缺失」。
        /// 默认 base：本机（Ryzen 7 PRO 4750U，纯 CPU）实测 2s 音频耗时 tiny≈1.6s / base≈2.5s / small≈8.2s，
        /// small 约为实时的 4 倍，已不适合「实时」字幕，故默认取 base（速度与质量平衡）。</summary>
        public static string WhisperModelPath { get; set; } =
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Models", "ggml-base.bin");

        /// <summary>SenseVoice 小模型（sherpa-onnx，离线 ASR，Apache-2.0）的 onnx 路径。
        /// 非自回归小模型：中/英/日/韩/粤，CPU 上速度远快于 Whisper（Whisper small 在本机 ~4× 实时，跑不动实时字幕）。
        /// 下载：https://github.com/k2-fsa/sherpa-onnx/releases/tag/asr-models
        ///       sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2（int8 版约 228MB）。</summary>
        public static string SenseVoiceModelPath { get; set; } =
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Models",
                "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17", "model.int8.onnx");

        /// <summary>SenseVoice 词表 tokens.txt（与 onnx 同目录）。</summary>
        public static string SenseVoiceTokensPath { get; set; } =
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Models",
                "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17", "tokens.txt");

        /// <summary>SenseVoice 识别语言：auto=自动检测，也可写死 zh / en / ja / ko / yue 提升准确率。</summary>
        public static string SenseVoiceLanguage { get; set; } = "auto";

        /// <summary>本地 Argos Translate / LibreTranslate 服务的 /translate 接口地址（HTTP MT，纯离线）。</summary>
        public static string ArgosTranslateUrl { get; set; } = "http://localhost:5000/translate";

        /// <summary>离线 NMT（Marian / OPUS-MT ONNX int8）模型根目录：Models\mt\opus-mt-&lt;src&gt;-&lt;dst&gt;\。
        /// 每个方向 5 个文件：encoder_model_int8.onnx / decoder_model_int8.onnx /
        /// decoder_with_past_model_int8.onnx / source.spm / vocab.json（见 MarianMt 类注释里的下载来源）。
        /// 这是「中↔英」离线互译的实际执行者：无需 Python、无需 5000 端口的本地服务，200~350ms/句。</summary>
        public static string MtModelsDir { get; set; } =
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Models", "mt");

        /// <summary>单句译文的最大 token 数（防跑飞；中英短句一般 10~20 就结束）。</summary>
        public static int MtMaxNewTokens { get; set; } = 128;

        /// <summary>是否在启动翻译时由本程序自动拉起本地 Argos 服务（需目标机已装 Python + argos-translate-server 或 libretranslate）。</summary>
        public static bool AutoLaunchArgos { get; set; } = false;

        // ---------------- 混元 Hy-MT2（llama.cpp，本机 127.0.0.1，全离线） ----------------

        /// <summary>本地 llama-server 的地址（llama.cpp 自带 OpenAI 兼容接口）。只监听 127.0.0.1，不联网。</summary>
        public static string LlmMtBaseUrl { get; set; } = "http://127.0.0.1:8080";

        /// <summary>混元翻译模型 GGUF 路径。
        /// 官方仓库 tencent/Hy-MT2-1.8B-GGUF 的 Hy-MT2-1.8B-Q4_K_M.gguf（1.05 GiB，Apache-2.0）。
        /// ⚠ 官方另有 2bit / 1.25bit 版（440MB），但那需要 AngelSlim 自研 STQ 内核 + llama.cpp 特定分支，
        /// 标准 llama.cpp 加载不了，别用。</summary>
        public static string LlmMtModelPath { get; set; } =
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Models", "llm", "Hy-MT2-1.8B-Q4_K_M.gguf");

        /// <summary>llama-server.exe 路径（放在 exe 同目录的 llama\ 下）。
        /// ⚠ llama.cpp 预编译包需要新版 VC 运行时（msvcp140.dll / vcruntime140.dll ≥ 14.4x）；
        /// 若目标机 System32 里还是 2016 年的 14.0.24215.1，程序会一启动就 0xC0000005 崩。
        /// 解决办法是把新版运行时 DLL 复制到 llama\ 同目录（app-local 部署，不动系统文件）。</summary>
        public static string LlmServerExePath { get; set; } =
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "llama", "llama-server.exe");

        /// <summary>翻译服务不可达时是否自动拉起 llama-server（拉起后模型加载约 3s）。</summary>
        public static bool AutoLaunchLlmServer { get; set; } = true;

        /// <summary>解码温度：0 = 贪心（确定性强、字幕前后一致，实测质量已足够好）。</summary>
        public static float LlmMtTemperature { get; set; } = 0f;
    }

    /// <summary>
    /// Whisper.net ASR 引擎（离线、MIT）：16kHz/mono PCM16 → 源语言文本。
    /// 模型懒加载：首个语音段才 Build WhisperFactory，避免下拉里只显示名字就占用数百 MB 内存。
    /// </summary>
    public sealed class WhisperAsrEngine : ITranslationEngine
    {
        private readonly string _explicitPath;
        private WhisperFactory _factory;
        private string _factoryPath;
        private readonly object _gate = new object();

        /// <summary>当前模型路径：构造时显式指定的优先，否则跟随 TranslationConfig.WhisperModelPath（UI 可切换）。</summary>
        private string ModelPath
        {
            get { return _explicitPath ?? TranslationConfig.WhisperModelPath; }
        }

        public WhisperAsrEngine(string modelPath = null)
        {
            _explicitPath = modelPath;
        }

        public bool IsModelReady
        {
            get { return File.Exists(ModelPath); }
        }

        public string DisplayName
        {
            get
            {
                var name = Path.GetFileName(ModelPath);
                return IsModelReady ? ("Whisper ASR（" + name + "）")
                                    : ("Whisper ASR（模型缺失：" + name + "）");
            }
        }

        public bool IsDirectTranslate
        {
            get { return false; }
        }

        public override string ToString() => DisplayName;

        private WhisperFactory GetFactory()
        {
            var p = ModelPath;
            lock (_gate)
            {
                // 路径变了（UI 切换模型）就丢弃旧工厂重建，否则会一直用旧模型
                if (_factory == null || _factoryPath != p)
                {
                    if (!File.Exists(p))
                        throw new FileNotFoundException("Whisper 模型未找到：" + p, p);
                    var old = _factory as IDisposable;
                    if (old != null) old.Dispose();
                    _factory = WhisperFactory.FromPath(p);
                    _factoryPath = p;
                }
                return _factory;
            }
        }

        public async Task<string> TranscribeAsync(short[] pcm16, string lang)
        {
            if (pcm16 == null || pcm16.Length == 0)
                return string.Empty;

            // PCM16 → 归一化 float[-1,1]；峰值过低（近静音）返回 null → 直接当空结果，不喂引擎
            var floats = PcmPrep.ToNormalizedFloats(pcm16);
            if (floats == null)
                return string.Empty;

            var factory = GetFactory();
            using var processor = factory.CreateBuilder()
                .WithLanguage(string.IsNullOrEmpty(lang) || lang == "auto" ? "auto" : lang)
                .WithNoContext()
                .WithTemperature(0f)
                .WithTemperatureInc(0f)   // 关掉温度回退循环：单次贪心解码，显著提速并大幅减少幻觉
                .WithThreads(Math.Max(1, Environment.ProcessorCount - 1))
                .Build();

            var sb = new StringBuilder();
            await foreach (var seg in processor.ProcessAsync(floats))
                sb.Append(seg.Text);
            // Whisper 对中文倾向输出繁体（且不受语言参数控制），这里统一归一为简体。
            return ZhText.ToSimplified(sb.ToString().Trim());
        }

        public Task<string> TranslateTextAsync(string text, string srcLang, string dstLang)
        {
            // 仅 ASR 引擎：不做 MT，原样返回（UI 字幕原文=译文）。
            return Task.FromResult(text);
        }
    }

    /// <summary>
    /// 本地 Argos Translate / LibreTranslate HTTP 客户端（离线 MT，MIT/CC-BY，避开 NLLB-200 的 CC-BY-NC 红线）。
    /// 接口形态：POST {url}  body={"q","source","target","format":"text"}  →  {"translatedText":"..."}
    /// </summary>
    public sealed class ArgosClient
    {
        private readonly string _url;
        private readonly HttpClient _http = new HttpClient();

        public ArgosClient(string url = null)
        {
            _url = url ?? TranslationConfig.ArgosTranslateUrl;
        }

        public string EndpointLabel => _url;

        public async Task<string> TranslateAsync(string text, string srcLang, string dstLang)
        {
            if (string.IsNullOrWhiteSpace(text))
                return text;

            var payload = new { q = text, source = NormalizeCode(srcLang), target = NormalizeCode(dstLang), format = "text" };
            var json = JsonSerializer.Serialize(payload);
            var content = new StringContent(json, Encoding.UTF8, "application/json");
            using var resp = await _http.PostAsync(_url, content).ConfigureAwait(false);
            resp.EnsureSuccessStatusCode();
            var body = await resp.Content.ReadAsStringAsync().ConfigureAwait(false);
            using var doc = JsonDocument.Parse(body);
            if (doc.RootElement.TryGetProperty("translatedText", out var t))
                return t.GetString() ?? body;
            return body;
        }

        // Argos/LibreTranslate 用 ISO-639-1（zh/en/ja...）；我们的 Code 已是该风格，裁掉区域后缀直传。
        private static string NormalizeCode(string code)
        {
            if (string.IsNullOrEmpty(code)) return "auto";
            int p = code.IndexOf('-');
            return (p > 0 ? code.Substring(0, p) : code).ToLowerInvariant();
        }
    }

    /// <summary>
    /// 组合引擎：Whisper ASR（识别源语言）+ Argos HTTP MT（源→目标）。
    /// Whisper 的 ggml 模型默认多语种，按 src Code 识别；Argos 按 ISO 代码翻译。
    /// </summary>
    public sealed class WhisperArgosEngine : ITranslationEngine
    {
        private readonly WhisperAsrEngine _asr;
        private readonly ArgosClient _mt;

        public WhisperArgosEngine(string modelPath = null, string argosUrl = null)
        {
            _asr = new WhisperAsrEngine(modelPath);
            _mt = new ArgosClient(argosUrl);
        }

        public string DisplayName =>
            "Whisper + Argos（" + (_asr.IsModelReady ? "ASR✓" : "ASR缺模型") + " / MT:" + _mt.EndpointLabel + "）";

        public bool IsDirectTranslate
        {
            get { return false; }
        }

        public override string ToString() => DisplayName;

        public Task<string> TranscribeAsync(short[] pcm16, string lang)
            => _asr.TranscribeAsync(pcm16, lang);

        public Task<string> TranslateTextAsync(string text, string srcLang, string dstLang)
            => _mt.TranslateAsync(text, srcLang, dstLang);
    }

    /// <summary>
    /// SenseVoice 离线 ASR 引擎（sherpa-onnx，Apache-2.0，纯离线）：16kHz/mono PCM16 → 源语言文本。
    /// 非自回归小模型，CPU 上速度远快于 Whisper（本机实测 Whisper small ≈4× 实时，根本跑不动实时字幕）；
    /// 支持 中/粤/英/日/韩，自带标点与 ITN（口语数字/日期转书面）。模型懒加载（int8 约 228MB）。
    /// ⚠ 原生库 sherpa-onnx-c-api.dll / onnxruntime.dll 由 NuGet 运行时包提供，
    ///    net48 不会自动从 runtimes\ 解析 → 见 csproj 的 CopySherpaNative 目标。
    /// </summary>
    public sealed class SenseVoiceAsrEngine : ITranslationEngine
    {
        private OfflineRecognizer _rec;
        private string _recModelPath;
        private readonly object _gate = new object();

        public bool IsModelReady
        {
            get
            {
                return File.Exists(TranslationConfig.SenseVoiceModelPath)
                    && File.Exists(TranslationConfig.SenseVoiceTokensPath);
            }
        }

        public string DisplayName
        {
            get
            {
                return IsModelReady
                    ? "SenseVoice ASR（中/粤/英/日/韩·离线·快）"
                    : "SenseVoice ASR（模型缺失：model.int8.onnx / tokens.txt）";
            }
        }

        public bool IsDirectTranslate
        {
            get { return false; }
        }

        public override string ToString() => DisplayName;

        private OfflineRecognizer GetRecognizer()
        {
            var p = TranslationConfig.SenseVoiceModelPath;
            lock (_gate)
            {
                if (_rec == null || _recModelPath != p)
                {
                    if (!File.Exists(p))
                        throw new FileNotFoundException("SenseVoice 模型未找到：" + p, p);
                    var tokens = TranslationConfig.SenseVoiceTokensPath;
                    if (!File.Exists(tokens))
                        throw new FileNotFoundException("SenseVoice 词表未找到：" + tokens, tokens);

                    if (_rec != null)
                    {
                        try { _rec.Dispose(); } catch { }
                        _rec = null;
                    }

                    var cfg = new OfflineRecognizerConfig();
                    cfg.ModelConfig.Tokens = tokens;
                    cfg.ModelConfig.NumThreads = Math.Max(1, Environment.ProcessorCount - 1);
                    cfg.ModelConfig.Provider = "cpu";
                    cfg.ModelConfig.Debug = 0;
                    cfg.ModelConfig.SenseVoice.Model = p;
                    cfg.ModelConfig.SenseVoice.Language =
                        string.IsNullOrEmpty(TranslationConfig.SenseVoiceLanguage)
                            ? "auto" : TranslationConfig.SenseVoiceLanguage;
                    cfg.ModelConfig.SenseVoice.UseInverseTextNormalization = 1;   // ITN：口语数字/日期→书面
                    cfg.DecodingMethod = "greedy_search";

                    _rec = new OfflineRecognizer(cfg);
                    _recModelPath = p;
                }
                return _rec;
            }
        }

        public Task<string> TranscribeAsync(short[] pcm16, string lang)
        {
            if (pcm16 == null || pcm16.Length == 0)
                return Task.FromResult(string.Empty);

            var floats = PcmPrep.ToNormalizedFloats(pcm16);
            if (floats == null)
                return Task.FromResult(string.Empty);      // 近静音/纯底噪，不喂引擎

            var rec = GetRecognizer();
            return Task.Run(() =>
            {
                lock (_gate)                              // 识别器非并发安全，串行解码
                {
                    using (var s = rec.CreateStream())
                    {
                        s.AcceptWaveform(16000, floats);
                        rec.Decode(s);
                        var text = s.Result.Text ?? string.Empty;
                        return ZhText.ToSimplified(ZhText.StripSpecialTags(text));
                    }
                }
            });
        }

        public Task<string> TranslateTextAsync(string text, string srcLang, string dstLang)
        {
            // 仅 ASR 引擎：不做 MT，原样返回（UI 字幕原文=译文）。
            return Task.FromResult(text);
        }
    }

    /// <summary>
    /// 组合引擎：SenseVoice ASR（识别源语言）+ Argos HTTP MT（源→目标）。全链路离线。
    /// </summary>
    public sealed class SenseVoiceArgosEngine : ITranslationEngine
    {
        private readonly SenseVoiceAsrEngine _asr;
        private readonly ArgosClient _mt;

        public SenseVoiceArgosEngine(string argosUrl = null)
        {
            _asr = new SenseVoiceAsrEngine();
            _mt = new ArgosClient(argosUrl);
        }

        public string DisplayName =>
            "SenseVoice + Argos（" + (_asr.IsModelReady ? "ASR✓" : "ASR缺模型") + " / MT:" + _mt.EndpointLabel + "）";

        public bool IsDirectTranslate
        {
            get { return false; }
        }

        public override string ToString() => DisplayName;

        public Task<string> TranscribeAsync(short[] pcm16, string lang)
            => _asr.TranscribeAsync(pcm16, lang);

        public Task<string> TranslateTextAsync(string text, string srcLang, string dstLang)
            => _mt.TranslateAsync(text, srcLang, dstLang);
    }

    /// <summary>
    /// Whisper 直译引擎：用 Whisper 自带的 translate 任务，把**任意源语言语音直接译成英文文本**，
    /// 不需要任何额外 MT 服务/模型（Whisper 原生能力）。限制：译文只能是英语。
    /// 目标语言若不选英语，本引擎仍输出英语（引擎名已注明）。
    /// </summary>
    public sealed class WhisperDirectTranslateEngine : ITranslationEngine
    {
        private readonly string _explicitPath;
        private WhisperFactory _factory;
        private string _factoryPath;
        private readonly object _gate = new object();

        /// <summary>当前模型路径：构造时显式指定的优先，否则跟随 TranslationConfig.WhisperModelPath（UI 可切换）。</summary>
        private string ModelPath
        {
            get { return _explicitPath ?? TranslationConfig.WhisperModelPath; }
        }

        public WhisperDirectTranslateEngine(string modelPath = null)
        {
            _explicitPath = modelPath;
        }

        public bool IsModelReady
        {
            get { return File.Exists(ModelPath); }
        }

        public bool IsDirectTranslate
        {
            get { return true; }
        }

        public string DisplayName
        {
            get
            {
                var name = Path.GetFileName(ModelPath);
                return IsModelReady ? "Whisper 直译（任意语言→英语，免本地服务）"
                                    : ("Whisper 直译（模型缺失：" + name + "）");
            }
        }

        public override string ToString() => DisplayName;

        private WhisperFactory GetFactory()
        {
            var p = ModelPath;
            lock (_gate)
            {
                // 路径变了（UI 切换模型）就丢弃旧工厂重建，否则会一直用旧模型
                if (_factory == null || _factoryPath != p)
                {
                    if (!File.Exists(p))
                        throw new FileNotFoundException("Whisper 模型未找到：" + p, p);
                    var old = _factory as IDisposable;
                    if (old != null) old.Dispose();
                    _factory = WhisperFactory.FromPath(p);
                    _factoryPath = p;
                }
                return _factory;
            }
        }

        public async Task<string> TranscribeAsync(short[] pcm16, string lang)
        {
            if (pcm16 == null || pcm16.Length == 0)
                return string.Empty;

            var floats = PcmPrep.ToNormalizedFloats(pcm16);
            if (floats == null)
                return string.Empty;

            var factory = GetFactory();
            // WithTranslate() = Whisper 的 translate 任务：语音 → 英语文本（语言自动检测，故不设 WithLanguage）
            using var processor = factory.CreateBuilder()
                .WithTranslate()
                .WithNoContext()
                .WithTemperature(0f)
                .WithTemperatureInc(0f)   // 关掉温度回退循环：单次贪心解码，显著提速并大幅减少幻觉
                .WithThreads(Math.Max(1, Environment.ProcessorCount - 1))
                .Build();

            var sb = new StringBuilder();
            await foreach (var seg in processor.ProcessAsync(floats))
                sb.Append(seg.Text);
            return sb.ToString().Trim();
        }

        public Task<string> TranslateTextAsync(string text, string srcLang, string dstLang)
        {
            // 直译已在 TranscribeAsync 完成，这里原样返回
            return Task.FromResult(text);
        }
    }

    /// <summary>
    /// 中文文本后处理：Whisper 对 zh 倾向输出繁体（其實/環境/電腦…），这里统一归一为简体。
    /// 用的是 .NET Framework 内置的 LCMapString 封装（Microsoft.VisualBasic.Strings.StrConv），
    /// 无需第三方库；非 Windows 或转换失败时原样返回，绝不因后处理中断识别。
    /// </summary>
    internal static class ZhText
    {
        public static string ToSimplified(string text)
        {
            if (string.IsNullOrEmpty(text))
                return text;
            try
            {
                // 2052 = zh-CN；该映射只作用于汉字，ASCII/标点/其它语言文字不受影响
                return Microsoft.VisualBasic.Strings.StrConv(
                    text, Microsoft.VisualBasic.VbStrConv.SimplifiedChinese, 2052);
            }
            catch
            {
                return text;
            }
        }

        /// <summary>
        /// 去掉 SenseVoice 等模型可能带出的特殊标记，如 &lt;|zh|&gt; &lt;|NEUTRAL|&gt; &lt;|Speech|&gt; &lt;|nospeech|&gt;。
        /// 这些是模型的语言/情感/事件标签，不是说话内容，不能进字幕。
        /// </summary>
        public static string StripSpecialTags(string text)
        {
            if (string.IsNullOrEmpty(text) || text.IndexOf("<|") < 0)
                return text;

            var sb = new StringBuilder(text.Length);
            int i = 0;
            while (i < text.Length)
            {
                if (text[i] == '<' && i + 1 < text.Length && text[i + 1] == '|')
                {
                    int end = text.IndexOf("|>", i + 2, StringComparison.Ordinal);
                    if (end < 0) break;
                    i = end + 2;
                    continue;
                }
                sb.Append(text[i]);
                i++;
            }
            return sb.ToString().Trim();
        }
    }

    /// <summary>
    /// PCM16 预处理：① 峰值过低（近静音/纯底噪）判为无语音，返回 null 让调用方跳过（省时且避免 Whisper 幻觉）；
    /// ② 否则做增益归一化到目标峰值，改善小音量麦克风的识别率。假设 16kHz/mono/16bit。
    /// </summary>
    internal static class PcmPrep
    {
        public static float[] ToNormalizedFloats(short[] pcm16,
            float peakFloor = 0.02f, float targetPeak = 0.8f, float maxGain = 6f)
        {
            if (pcm16 == null || pcm16.Length == 0)
                return null;

            int peak = 0;
            for (int i = 0; i < pcm16.Length; i++)
            {
                int a = pcm16[i] == short.MinValue ? 32768 : Math.Abs((int)pcm16[i]);
                if (a > peak) peak = a;
            }
            float peakF = peak / 32768f;
            if (peakF < peakFloor)
                return null;   // 近静音/纯底噪 → 不喂引擎

            float gain = targetPeak / peakF;
            if (gain > maxGain) gain = maxGain;

            var f = new float[pcm16.Length];
            for (int i = 0; i < pcm16.Length; i++)
            {
                float v = (float)(pcm16[i] / 32768.0 * gain);
                if (v > 1f) v = 1f; else if (v < -1f) v = -1f;
                f[i] = v;
            }
            return f;
        }
    }
}
