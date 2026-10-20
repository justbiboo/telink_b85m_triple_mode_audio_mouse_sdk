using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.ExceptionServices;
using System.Security;
using System.Threading;
using System.Threading.Tasks;
using NAudio.Wave;
using SherpaOnnx;

namespace BibooUxTool
{
    /// <summary>
    /// 译文朗读（TTS）：sherpa-onnx Kokoro int8 多语言模型（中文 + 英文，103 个音色）。
    ///
    /// 依赖全部已存在，无需新增任何 NuGet：
    ///   · org.k2fsa.sherpa.onnx 1.13.8 托管包（已含 OfflineTts / OfflineTtsKokoroModelConfig）
    ///   · 原生 sherpa-onnx-c-api.dll（CopySherpaNative 已把它和 onnxruntime.dll 拷到 exe 同目录）
    ///   · NAudio 2.2.1（WaveOutEvent + BufferedWaveProvider 流式播放 float 采样）
    ///
    /// 模型目录：exe\Models\tts\kokoro-int8-multi-lang-v1_1\
    ///   model.int8.onnx(109MB) / voices.bin(51MB) / tokens.txt / lexicon-{zh,us-en,gb-en}.txt /
    ///   dict\ / espeak-ng-data\ / date-zh.fst / number-zh.fst / phone-zh.fst
    ///
    /// 流式管线（2026-10-18 重写，替代原来的「整句合成完再播」）：
    ///   1. 按标点（，。！？；、：,.!?;:）把文本切成分片，标点留在前一片末尾；
    ///   2. 分片有界并行合成：最多 MaxSynthWorkers 路同时 Generate。实测同一 OfflineTts
    ///      实例并发 Generate 安全（结果 rms 一致、反复跑不坏），4 片并行 12.6s vs 顺序 29.3s；
    ///      ORT 线程数保持 4 —— 实测 8 线程无增益、16 线程更慢（RTF 2.69~3.07）；
    ///   3. 合成结果按分片顺序喂 BufferedWaveProvider，缓冲 ≥ PreRollSeconds(0.2s) 即开播，
    ///      不必等整句合成完；缓冲被抽干时 NAudio 补零静音继续播（不会自动停），
    ///      即后续分片没合成出来时就地静音，合成好了自动接上。
    /// 本机实测（Ryzen 7 PRO 4750U）：RTF≈2.17（合成时间≈2.17×音频时长，几乎无固定开销）；
    /// 首音延迟≈第一片合成时间 + 加载（若未预热）。2026-10-19 延迟专项实测：
    ///   模型加载 2.8s；旧预热的「空跑一句」干跑 3.2s 且无收益（RTF 严格线性）已移除；
    ///   进翻译页即预热（OnShown），勾框/首句时早已就绪；
    ///   首片单飞（FirstFragSolo）可把首音从 5.2s 压到 3.7s（单飞 RTF 1.84），
    ///   但长句缓冲会出缝——是否启用按句长权衡，见 SpeakFragmentsAsync 里的策略开关。
    ///
    /// 线程模型：一句一念（SemaphoreSlim 串行整句，不重叠）；合成在池线程并行，
    /// 播放在 NAudio 自己的驱动回调线程；Stop() 取消当前整句（停播 + 放弃剩余分片）。
    /// </summary>
    public sealed class TtsEngine : IDisposable
    {
        private readonly object _gate = new object();
        private OfflineTts _tts;
        private bool _loadFailed;

        // 一句一念：合成 + 播放整体串行，避免两句叠着说
        private readonly SemaphoreSlim _serial = new SemaphoreSlim(1, 1);

        // 并行合成的并发上限（跨 utterance 全局生效，避免同时开太多 ORT 会话内线程）
        private readonly SemaphoreSlim _synthGate = new SemaphoreSlim(MaxSynthWorkers, MaxSynthWorkers);
        private int _synthInflight;   // 正在 Generate 中的任务数（Dispose 要等它归零）

        private WaveOutEvent _out;
        private CancellationTokenSource _playCts;

        /// <summary>并行合成最大并发。实测 4 路时总耗时约为顺序的 43%（2.33x），
        /// 再加大收益递减且与 ORT 内部线程争抢（8 路实测反而略慢）。</summary>
        private const int MaxSynthWorkers = 4;

        /// <summary>缓冲满这么久就开始播，不必等整句合成完。</summary>
        private const double PreRollSeconds = 0.2;

        /// <summary>首片优先：先单独合成第一片（不被后续分片抢 CPU，首音≈首片单飞耗时），
        /// 第一片合成完再放行其余分片并行。代价是后续分片晚起步，长句缓冲可能被抽干出缝。
        /// 只对「分片数 ≥ SoloMinFrags」的句子启用：剩余分片够多、4 路并行追得上播放
        /// （实测 9 片短句首音 4.0→2.96s，总时长只多 0.9s）；
        /// 3 片以内的长句反而多 3.3s 中段静音，那种句子保持全部并行保连续。</summary>
        internal static bool FirstFragSolo = true;

        /// <summary>首片单飞的最小分片数（少于这个数就全部并行，保播放连续不出缝）。</summary>
        internal const int SoloMinFrags = 4;

        /// <summary>其余分片延迟起步毫秒数（0 = 不延迟）：给首片让出前半段 CPU 的折中方案。
        /// 实测被上面的自适应规则压制（首音收益不如单飞、总时长收益不如并行），留 0 备用。</summary>
        internal static int RestStaggerMs = 0;

        /// <summary>片段间静音：逗号类 120ms / 句号类 220ms。分片各自独立合成，
        /// 不补静音会听得出一句被切成几截。</summary>
        private const int GapMsComma = 120;
        private const int GapMsSentence = 220;

        /// <summary>播放结束后多等一会，让设备里末段缓冲播完再 Dispose（防切尾音）。</summary>
        private const int TailWaitMs = 250;

        /// <summary>默认音色 id。Kokoro v1_1 共 103 个音色（sid 0-102）：
        /// 0-44 是英文/多语言音色（0 = af_alloy 女声），45 起是中文音色（45 = zf_xiaobei）。
        /// 界面可改；没改过时按目标语言自动选（中文 45 / 其他 0）。</summary>
        public const int DefaultSpeakerId = 45;

        /// <summary>英文音色 id（af_alloy）。目标语言非中文时用它，避免中文音色念英文的违和感。</summary>
        public const int DefaultEnglishSpeakerId = 0;

        /// <summary>合成线程数。Kokoro-82M int8 属小模型，实测 4 线程 RTF≈2.17 为最优：
        /// 8 线程无增益（RTF 2.18）、16 线程更慢（RTF 2.69~3.07，同步开销反噬）。</summary>
        public static int NumThreads { get; set; } = 4;

        /// <summary>模型根目录。</summary>
        public static string ModelDir
        {
            get
            {
                return Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Models",
                    "tts", "kokoro-int8-multi-lang-v1_1");
            }
        }

        /// <summary>模型是否齐备（只查文件，不加载）。</summary>
        public static bool IsModelPresent
        {
            get
            {
                try
                {
                    var d = ModelDir;
                    return File.Exists(Path.Combine(d, "model.int8.onnx"))
                        && File.Exists(Path.Combine(d, "voices.bin"))
                        && File.Exists(Path.Combine(d, "tokens.txt"))
                        && Directory.Exists(Path.Combine(d, "espeak-ng-data"))
                        && Directory.Exists(Path.Combine(d, "dict"));
                }
                catch { return false; }
            }
        }

        public bool IsReady
        {
            get { lock (_gate) return _tts != null; }
        }

        /// <summary>加载失败（模型缺失 / 原生库缺失 / 配置错误）后不再反复重试。</summary>
        public bool LoadFailed
        {
            get { lock (_gate) return _loadFailed; }
        }

        public int SampleRate
        {
            get { lock (_gate) return _tts != null ? _tts.SampleRate : 0; }
        }

        public int NumSpeakers
        {
            get { lock (_gate) return _tts != null ? _tts.NumSpeakers : 0; }
        }

        /// <summary>懒加载模型。成功返回 true；失败返回 false 并把原因写进 message。</summary>
        public bool TryLoad(out string message)
        {
            // sherpa 的 C# 封装在 config 校验失败时不会抛异常，而是返回 handle 为空的实例，
            // 之后访问 NumSpeakers/SampleRate 就是一次 AccessViolation（会直接崩进程）。
            // 这里显式接住「损坏状态异常」，保证最多是功能不可用，不带走整个程序。
            try
            {
                return TryLoadCore(out message);
            }
            catch (AccessViolationException ex)
            {
                lock (_gate)
                {
                    _tts = null;
                    _loadFailed = true;
                }
                message = "TTS 模型加载失败（原生库拒绝了当前配置）：" + ex.Message;
                return false;
            }
        }

        [HandleProcessCorruptedStateExceptions]
        [SecurityCritical]
        private bool TryLoadCore(out string message)
        {
            lock (_gate)
            {
                if (_tts != null) { message = null; return true; }
                if (_loadFailed) { message = "模型此前加载失败，本次跳过"; return false; }

                if (!IsModelPresent)
                {
                    _loadFailed = true;
                    message = "缺少 TTS 模型：请把 kokoro-int8-multi-lang-v1_1 放到 " + ModelDir;
                    return false;
                }

                try
                {
                    var d = ModelDir;
                    var cfg = new OfflineTtsConfig();
                    cfg.Model.NumThreads = NumThreads > 0 ? NumThreads : 1;
                    cfg.Model.Provider = "cpu";
                    cfg.Model.Debug = 0;

                    var ko = new OfflineTtsKokoroModelConfig();
                    ko.Model = Path.Combine(d, "model.int8.onnx");
                    ko.Voices = Path.Combine(d, "voices.bin");
                    ko.Tokens = Path.Combine(d, "tokens.txt");
                    ko.DataDir = Path.Combine(d, "espeak-ng-data");
                    ko.DictDir = Path.Combine(d, "dict");
                    // 三个词典用逗号分隔：中文分词走 lexicon-zh + dict 里的 FST，英文走 espeak-ng
                    ko.Lexicon = Path.Combine(d, "lexicon-zh.txt") + ","
                               + Path.Combine(d, "lexicon-us-en.txt") + ","
                               + Path.Combine(d, "lexicon-gb-en.txt");
                    // ⚠ 必须是 "en"：设成 "zh" 时纯英文句子会被送去 espeak-ng 的 zh voice，
                    //   而 espeak-ng-data 里没有 zh 语音数据 → Failed to set eSpeak-ng voice → 合成失败。
                    //   设 "en" 时中文词仍会命中 lexicon-zh + jieba 分词，两边都能念。
                    ko.Lang = "en";
                    ko.LengthScale = 1.0f;
                    cfg.Model.Kokoro = ko;

                    cfg.MaxNumSentences = 1;  // 一次一句，实时性优先
                    cfg.RuleFsts = Path.Combine(d, "date-zh.fst") + ","
                                 + Path.Combine(d, "number-zh.fst");
                    cfg.SilenceScale = 0.5f;

                    var tts = new OfflineTts(cfg);
                    _tts = tts;
                    message = "TTS 模型已就绪（" + tts.NumSpeakers + " 音色，"
                            + tts.SampleRate + " Hz）";
                    return true;
                }
                catch (Exception ex)
                {
                    _loadFailed = true;
                    message = "TTS 模型加载失败：" + ex.GetType().Name + " " + ex.Message;
                    return false;
                }
            }
        }

        /// <summary>预热：只加载模型。
        /// ⚠ 曾经这里是「加载 + 空跑一句」，实测干跑要 3.2s 却买不到任何东西
        /// （RTF 对音频时长严格线性、无固定首帧开销，3 字句 3.6s ≈ 2.17×1.69s），
        /// 而且旧实现 lock(_gate) 握着锁 Generate，会把紧随其后的真朗读堵死 3s+。
        /// 模型加载 2.8s 已由「进翻译页即预热」提前付掉，这里不需要再烧 CPU。</summary>
        public bool Warmup(out string message, int speakerId = DefaultSpeakerId)
        {
            return TryLoad(out message);
        }

        /// <summary>朗读一段文本。返回是否真正发声。
        /// ⚠ 必须整体丢到线程池：加载模型要 3~4s、合成要几秒、播放要几秒，
        /// 而且 `await _serial.WaitAsync()` 在信号量立即可用时会同步继续（不 Yield），
        /// 直接在 UI 线程调用会让界面冻结。Task.Run 包一层彻底避免。</summary>
        public Task<bool> SpeakAsync(string text, int speakerId = DefaultSpeakerId,
                                    float speed = 1.0f, float volume = 1.0f, int deviceNumber = -1)
        {
            if (string.IsNullOrWhiteSpace(text))
                return Task.FromResult(false);
            return Task.Run(() => SpeakCoreAsync(text, speakerId, speed, volume, deviceNumber));
        }

        private async Task<bool> SpeakCoreAsync(string text, int speakerId, float speed,
                                                float volume, int deviceNumber)
        {
            string m;
            if (!TryLoad(out m)) throw new InvalidOperationException(m);

            var frags = SplitFragments(text);
            if (frags.Count == 0) return false;

            await _serial.WaitAsync().ConfigureAwait(false);
            var cts = new CancellationTokenSource();
            try
            {
                _playCts = cts;
                return await SpeakFragmentsAsync(frags, speakerId, speed, volume,
                                                 deviceNumber, cts).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                return false;   // Stop() 主动取消，不算失败
            }
            finally
            {
                _playCts = null;
                cts.Dispose();
                _serial.Release();
            }
        }

        /// <summary>流式朗读：并行合成分片 + 按序喂 BufferedWaveProvider + 到点开播。
        /// 返回是否真正发声（至少一片有效音频且播完/被取消前播过）。</summary>
        private async Task<bool> SpeakFragmentsAsync(List<string> frags, int speakerId, float speed,
                                                     float volume, int deviceNumber,
                                                     CancellationTokenSource cts)
        {
            var ct = cts.Token;
            int sr;
            lock (_gate)
            {
                if (_tts == null) return false;
                sr = _tts.SampleRate;
            }
            if (sr <= 0) return false;

            // 1) 有界并行合成：每个分片一个 Task，结果存进按下标对应的槽位，消费端按序取。
            //    首片延迟直接决定「多久出声」，提供两种让路策略（默认值按实测数据选）：
            //      · FirstFragSolo  —— 首片单独合成完再放行其余：首音最快（−26%），
            //        但只对分片多的句子启用（SoloMinFrags）：剩余分片 4 路并行追得上播放；
            //        3 片以内的长句用这招会多出秒级中段静音，那种句子全部并行保连续；
            //      · RestStaggerMs  —— 其余分片晚一点起步：折中（实测两边不讨好，默认关）。
            bool soloFirst = FirstFragSolo && frags.Count >= SoloMinFrags;
            var slotTasks = new Task<float[]>[frags.Count];
            Func<int, Task<float[]>> launch = idx => Task.Run(async () =>
            {
                try { await _synthGate.WaitAsync(ct).ConfigureAwait(false); }
                catch (OperationCanceledException) { return null; }
                try
                {
                    ct.ThrowIfCancellationRequested();
                    return GenerateSamples(frags[idx], speakerId, speed);
                }
                catch (OperationCanceledException) { return null; }
                finally { _synthGate.Release(); }
            }, ct);

            if (soloFirst)
            {
                // 首片单飞：不与后续分片抢 CPU，首音≈首片单飞耗时
                slotTasks[0] = launch(0);
                await slotTasks[0].ConfigureAwait(false);
                for (int i = 1; i < frags.Count; i++) slotTasks[i] = launch(i);
            }
            else if (RestStaggerMs > 0 && frags.Count > 1)
            {
                slotTasks[0] = launch(0);
                for (int i = 1; i < frags.Count; i++)
                {
                    int idx = i;
                    slotTasks[i] = Task.Run(async () =>
                    {
                        try { await Task.Delay(RestStaggerMs, ct).ConfigureAwait(false); }
                        catch (OperationCanceledException) { return null; }
                        try { await _synthGate.WaitAsync(ct).ConfigureAwait(false); }
                        catch (OperationCanceledException) { return null; }
                        try
                        {
                            ct.ThrowIfCancellationRequested();
                            return GenerateSamples(frags[idx], speakerId, speed);
                        }
                        catch (OperationCanceledException) { return null; }
                        finally { _synthGate.Release(); }
                    }, ct);
                }
            }
            else
            {
                for (int i = 0; i < frags.Count; i++) slotTasks[i] = launch(i);
            }

            // 2) 播放器：Init 时可以是空缓冲，到点再 Play
            var fmt = WaveFormat.CreateIeeeFloatWaveFormat(sr, 1);
            var prov = new BufferedWaveProvider(fmt);
            // ⚠ BufferDuration 默认只有 5s：并行合成会短时间内堆积超过 5s 的音频，
            //   超了直接抛 "Buffer full"。长句（200 字≈60s 音频）按 120s 给，
            //   再设丢弃兜底——真到顶了丢新数据（静音缺口）也好过整句朗读失败。
            prov.BufferDuration = TimeSpan.FromSeconds(120);
            prov.DiscardOnBufferOverflow = true;
            var wo = new WaveOutEvent();
            try
            {
                try { wo.DeviceNumber = deviceNumber; } catch { /* 设备号非法则用默认 */ }
                try { wo.Volume = Clamp01(volume); } catch { }
                wo.Init(prov);
                _out = wo;

                bool any = false, started = false;
                try
                {
                    for (int i = 0; i < frags.Count; i++)
                    {
                        ct.ThrowIfCancellationRequested();
                        float[] samples = await slotTasks[i].ConfigureAwait(false);
                        if (samples == null || samples.Length == 0) continue;
                        AddGapSilence(prov, frags[i], sr, i == 0);
                        var bytes = new byte[samples.Length * 4];
                        Buffer.BlockCopy(samples, 0, bytes, 0, bytes.Length);
                        prov.AddSamples(bytes, 0, bytes.Length);
                        any = true;
                        if (!started && prov.BufferedDuration.TotalSeconds >= PreRollSeconds)
                        {
                            wo.Play();
                            started = true;
                        }
                    }
                }
                catch
                {
                    cts.Cancel();   // 某片合成炸了：剩余分片别跑了
                    throw;
                }

                if (!any) return false;
                if (!started) wo.Play();   // 整句不足 PreRollSeconds 也播

                // 3) 等播放自然结束。⚠ BufferedWaveProvider 抽干时 NAudio 补零静音继续播、
                //    不触发 PlaybackStopped，所以只能轮询 BufferedDuration 归零判断播完；
                //    万一被停过（PlaybackState 非 Playing 而缓冲还有货），补一次 Play。
                while (true)
                {
                    if (ct.IsCancellationRequested) return false;
                    if (wo.PlaybackState != PlaybackState.Playing
                        && prov.BufferedDuration.TotalSeconds > 0.02)
                    {
                        try { wo.Play(); } catch { return false; }
                    }
                    if (prov.BufferedDuration.TotalSeconds <= 0.0005) break;
                    Thread.Sleep(20);
                }
                Thread.Sleep(TailWaitMs);   // 设备里还有末段缓冲，等它播完
                return true;
            }
            finally
            {
                _out = null;
                try { wo.Stop(); } catch { }
                try { wo.Dispose(); } catch { }
            }
        }

        /// <summary>合成单个分片，返回 float 采样副本（副本与原生对象生命周期解耦，可安全带走）。
        /// 并发安全：实测同一 OfflineTts 实例多线程同时 Generate 无问题。</summary>
        private float[] GenerateSamples(string text, int speakerId, float speed)
        {
            OfflineTts inst;
            lock (_gate)
            {
                inst = _tts;
                if (inst != null) Interlocked.Increment(ref _synthInflight);
            }
            if (inst == null) return null;
            try
            {
                var audio = inst.Generate(text, speed, speakerId);
                try
                {
                    if (audio == null) return null;
                    int n = audio.NumSamples;
                    if (n <= 0) return null;
                    var src = audio.Samples;
                    if (src == null || src.Length == 0) return null;
                    var copy = new float[src.Length];
                    Array.Copy(src, copy, src.Length);
                    return copy;
                }
                finally { audio.Dispose(); }
            }
            finally { Interlocked.Decrement(ref _synthInflight); }
        }

        /// <summary>合成并存成 WAV（试听/调试用），不播放。</summary>
        public string SaveToWav(string text, string path, int speakerId = DefaultSpeakerId, float speed = 1.0f)
        {
            string m;
            if (!TryLoad(out m)) throw new InvalidOperationException(m);
            OfflineTtsGeneratedAudio audio;
            lock (_gate) { audio = _tts.Generate(text, speed, speakerId); }
            try { audio.SaveToWaveFile(path); return path; }
            finally { audio.Dispose(); }
        }

        /// <summary>打断当前朗读（没有在播也无害）：停播 + 取消剩余分片合成。</summary>
        public void Stop()
        {
            var cts = _playCts;
            if (cts != null)
            {
                try { cts.Cancel(); }
                catch (ObjectDisposedException) { }
                catch (AggregateException) { }
            }
            var o = _out;
            if (o == null) return;
            try { o.Stop(); } catch { }
        }

        public void Dispose()
        {
            Stop();
            lock (_gate)
            {
                // 等正在飞的合成收尾再释放 _tts，避免原生句柄被释放时还有线程在用
                // （合成任务在 _gate 内登记 _synthInflight，不会漏登记）
                var sw = Stopwatch.StartNew();
                while (Volatile.Read(ref _synthInflight) > 0 && sw.ElapsedMilliseconds < 15000)
                    Thread.Sleep(20);
                try { if (_tts != null) _tts.Dispose(); }
                catch { }
                _tts = null;
            }
            try { _serial.Dispose(); } catch { }
            try { _synthGate.Dispose(); } catch { }
        }

        // ---------- 分片与静音 ----------

        private static readonly char[] SplitAfterChars = "，。！？；、：,.!?;:".ToCharArray();
        private static readonly char[] CommaLevelChars = "，、：,;:".ToCharArray();

        /// <summary>按标点切分片（标点留在前一片末尾，保 prosody 线索）；无标点则整句一片。
        /// sherpa 内部只按 。！？； 切分，逗号不切——单句长句就没法流式，所以自己切。
        /// 纯标点/空白的「片」直接丢弃（如 "，，，" 不产生任何分片）。</summary>
        internal static List<string> SplitFragments(string text)
        {
            var frags = new List<string>();
            if (string.IsNullOrEmpty(text)) return frags;
            var cur = new System.Text.StringBuilder();
            foreach (char c in text)
            {
                cur.Append(c);
                if (Array.IndexOf(SplitAfterChars, c) >= 0)
                {
                    var f = cur.ToString().Trim();
                    if (f.Length > 0 && HasSpeakable(f)) frags.Add(f);
                    cur.Clear();
                }
            }
            var tail = cur.ToString().Trim();
            if (tail.Length > 0 && HasSpeakable(tail)) frags.Add(tail);
            return frags;
        }

        /// <summary>是否含有可发音内容（非空白、非标点——全角标点 IsPunctuation 也为 true）。</summary>
        private static bool HasSpeakable(string s)
        {
            foreach (char c in s)
                if (!char.IsWhiteSpace(c) && !char.IsPunctuation(c)) return true;
            return false;
        }

        /// <summary>片段间接静音：逗号类 120ms、句号类 220ms；第一片前不加。</summary>
        private static void AddGapSilence(BufferedWaveProvider prov, string frag, int sr, bool first)
        {
            if (first || frag.Length == 0) return;
            char last = frag[frag.Length - 1];
            int gapMs = Array.IndexOf(CommaLevelChars, last) >= 0 ? GapMsComma : GapMsSentence;
            int n = sr * gapMs / 1000;
            if (n <= 0) return;
            prov.AddSamples(new byte[n * 4], 0, n * 4);   // float 零就是静音
        }

        private static float Clamp01(float v)
        {
            return v < 0f ? 0f : (v > 1f ? 1f : v);
        }
    }
}
