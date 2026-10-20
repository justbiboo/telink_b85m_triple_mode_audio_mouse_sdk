using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace BibooUxTool
{
    /// <summary>
    /// 混元 Hy-MT2（tencent/Hy-MT2-1.8B，Apache-2.0）的语种名表与语种归一。
    ///
    /// 提示词形态（官方推荐，已在本机实测通过）：
    ///   将以下文本翻译为{目标语言}，注意只需要输出翻译后的结果，不要额外解释：{原文}
    /// 其中「目标语言」要写全称（简体中文 / English / 日本語 均可，实测中文全称最稳），
    /// 因为提示词本身是中文，这里统一用中文全称，避免中英混排时模型跑偏。
    /// </summary>
    public static class HyMtLang
    {
        private static readonly Dictionary<string, string> _names =
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                { "zh",    "简体中文" },
                { "zh-TW", "繁体中文" },
                { "yue",   "粤语" },
                { "en",    "英语" },
                { "ja",    "日语" },
                { "ko",    "韩语" },
                { "fr",    "法语" },
                { "de",    "德语" },
                { "es",    "西班牙语" },
                { "pt",    "葡萄牙语" },
                { "ru",    "俄语" },
                { "it",    "意大利语" },
                { "ar",    "阿拉伯语" },
                { "th",    "泰语" },
                { "vi",    "越南语" },
                { "id",    "印度尼西亚语" },
                { "ms",    "马来语" },
                { "tl",    "菲律宾语" },
                { "hi",    "印地语" },
                { "bn",    "孟加拉语" },
                { "ur",    "乌尔都语" },
                { "fa",    "波斯语" },
                { "tr",    "土耳其语" },
                { "nl",    "荷兰语" },
                { "pl",    "波兰语" },
                { "cs",    "捷克语" },
                { "uk",    "乌克兰语" },
                { "he",    "希伯来语" },
                { "ta",    "泰米尔语" },
                { "te",    "泰卢固语" },
                { "mr",    "马拉地语" },
                { "gu",    "古吉拉特语" },
                { "km",    "高棉语" },
                { "my",    "缅甸语" },
                { "bo",    "藏语" },
                { "kk",    "哈萨克语" },
                { "mn",    "蒙古语" },
                { "ug",    "维吾尔语" },
            };

        /// <summary>把界面里的语言 Code 归一成 Hy-MT2 的键。
        /// zh-TW / zh-Hant 归到「繁体中文」，zh-CN / zh-Hans 归到「简体中文」，其余裁掉区域后缀。</summary>
        public static string Normalize(string code)
        {
            if (string.IsNullOrWhiteSpace(code)) return string.Empty;
            var c = code.Trim().Replace('_', '-');
            var lower = c.ToLowerInvariant();

            if (lower == "zh-tw" || lower == "zh-hk" || lower == "zh-mo" || lower == "zh-hant" || lower == "zh-cht")
                return "zh-TW";
            if (lower == "zh-cn" || lower == "zh-sg" || lower == "zh-hans")
                return "zh";
            if (lower.StartsWith("yue") || lower == "zh-yue")
                return "yue";

            int p = c.IndexOf('-');
            return (p > 0 ? c.Substring(0, p) : c).ToLowerInvariant();
        }

        /// <summary>取 Hy-MT2 用的中文全称；不支持则返回 null。</summary>
        public static string NameOf(string code)
        {
            var k = Normalize(code);
            string n;
            return _names.TryGetValue(k, out n) ? n : null;
        }

        public static bool IsSupported(string code)
        {
            return NameOf(code) != null;
        }

        public static int Count { get { return _names.Count; } }
    }

    /// <summary>
    /// llama-server 进程的拉起/停止（本机进程，只监听 127.0.0.1，全程离线）。
    /// 只在「服务不可达 + TranslationConfig.AutoLaunchLlmServer」时才拉起，且只拉一次。
    /// </summary>
    public static class LlmServerLauncher
    {
        private static Process _proc;
        private static readonly object _gate = new object();

        public static string ExePath
        {
            get { return TranslationConfig.LlmServerExePath; }
        }

        public static bool ExeExists
        {
            get { return File.Exists(ExePath); }
        }

        /// <summary>本进程拉起的那个 llama-server 还活着吗。</summary>
        public static bool IsOursAlive
        {
            get
            {
                lock (_gate)
                {
                    try { return _proc != null && !_proc.HasExited; }
                    catch { return false; }
                }
            }
        }

        public static bool Start()
        {
            lock (_gate)
            {
                if (IsOursAlive) return true;
                if (!ExeExists) return false;
                if (!File.Exists(TranslationConfig.LlmMtModelPath)) return false;

                var exe = Path.GetFullPath(ExePath);
                var model = Path.GetFullPath(TranslationConfig.LlmMtModelPath);

                var psi = new ProcessStartInfo
                {
                    FileName = exe,
                    WorkingDirectory = Path.GetDirectoryName(exe),
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    // -ngl 0：本机只有 512MB 核显，纯 CPU 跑（模型 1.1GB，CPU 反而更稳）
                    // -t     ：留 1 个核给 UI
                    Arguments = string.Format(
                        "-m \"{0}\" --host 127.0.0.1 --port {1} -c 4096 -t {2} -ngl 0",
                        model, PortOf(TranslationConfig.LlmMtBaseUrl),
                        Math.Max(1, Environment.ProcessorCount - 1)),
                };

                try
                {
                    _proc = Process.Start(psi);
                    return _proc != null;
                }
                catch
                {
                    _proc = null;
                    return false;
                }
            }
        }

        /// <summary>只停我们自己拉起的那个进程（用户手动起的另一个 llama-server 不会被误杀）。</summary>
        public static void Stop()
        {
            lock (_gate)
            {
                try
                {
                    if (_proc != null && !_proc.HasExited)
                    {
                        _proc.Kill();
                        _proc.WaitForExit(3000);
                    }
                }
                catch { }
                finally
                {
                    _proc = null;
                }
            }
        }

        private static string PortOf(string baseUrl)
        {
            try { return new Uri(baseUrl).Port.ToString(); }
            catch { return "8080"; }
        }
    }

    /// <summary>
    /// llama.cpp 的 OpenAI 兼容接口客户端（chat/completions）。
    /// 接口完全在本机 127.0.0.1 上，不联网、不依赖任何云服务。
    /// </summary>
    public sealed class LlamaCppMtClient
    {
        private static readonly HttpClient _http = new HttpClient();

        static LlamaCppMtClient()
        {
            _http.Timeout = TimeSpan.FromSeconds(120);
        }

        private readonly string _base;

        public LlamaCppMtClient(string baseUrl = null)
        {
            _base = (baseUrl ?? TranslationConfig.LlmMtBaseUrl).TrimEnd('/');
        }

        public string EndpointLabel { get { return _base; } }

        public string ChatUrl { get { return _base + "/v1/chat/completions"; } }
        public string HealthUrl { get { return _base + "/health"; } }

        /// <summary>服务是否在跑且模型已加载（llama-server 加载完才返回 200）。</summary>
        public async Task<bool> IsUpAsync()
        {
            try
            {
                using (var cts = new CancellationTokenSource(TimeSpan.FromMilliseconds(1500)))
                using (var resp = await _http.GetAsync(HealthUrl, cts.Token).ConfigureAwait(false))
                    return resp.IsSuccessStatusCode;
            }
            catch
            {
                return false;
            }
        }

        /// <summary>必要时自动拉起本地 llama-server 并等它就绪（模型加载约 3s）。</summary>
        public async Task<bool> EnsureServerAsync()
        {
            if (await IsUpAsync().ConfigureAwait(false)) return true;
            if (!TranslationConfig.AutoLaunchLlmServer) return false;
            if (!LlmServerLauncher.Start()) return false;

            var sw = Stopwatch.StartNew();
            while (sw.Elapsed < TimeSpan.FromSeconds(90))
            {
                await Task.Delay(700).ConfigureAwait(false);
                if (await IsUpAsync().ConfigureAwait(false)) return true;
            }
            return false;
        }

        /// <summary>
        /// 预热：确保 llama-server 在跑（必要时拉起并等 /health 就绪），再打一次极短译文。
        /// 第二次那步很关键——llama-server 启动后**第一次**请求还要付 prompt 计算、KV 缓存分配、
        /// 分词器首次构建等一次性开销（本机实测 1~2s），空跑一句「你好」就能把这笔开销提前付掉，
        /// 用户正式翻译第一句就不会卡。
        /// </summary>
        public async Task<bool> WarmupAsync()
        {
            if (!await EnsureServerAsync().ConfigureAwait(false))
                return false;
            try
            {
                await TranslateAsync("你好", "zh", "en").ConfigureAwait(false);
            }
            catch
            {
                // 空跑失败不影响「服务已就绪」这个结论
            }
            return true;
        }

        public async Task<string> TranslateAsync(string text, string srcLang, string dstLang)
        {
            if (string.IsNullOrWhiteSpace(text))
                return text ?? string.Empty;

            string tgt = HyMtLang.NameOf(dstLang);
            if (tgt == null)
            {
                return "⚠ 混元翻译不支持目标语种：" + dstLang
                     + "（支持 " + HyMtLang.Count + " 种：中英日韩法德西葡俄意阿泰越…）";
            }

            if (!await EnsureServerAsync().ConfigureAwait(false))
            {
                return "⚠ 本地翻译服务不可达（" + _base + "）：需要 llama\\llama-server.exe 与 Models\\llm\\ 下的 gguf 模型。";
            }

            var prompt = string.Format(
                "将以下文本翻译为{0}，注意只需要输出翻译后的结果，不要额外解释：{1}", tgt, text);

            var payload = new Dictionary<string, object>
            {
                { "messages", new[] { new Dictionary<string, string> { { "role", "user" }, { "content", prompt } } } },
                { "temperature", TranslationConfig.LlmMtTemperature },
                { "max_tokens", TranslationConfig.MtMaxNewTokens },
                { "stream", false },
            };

            var json = JsonSerializer.Serialize(payload);
            using (var content = new StringContent(json, Encoding.UTF8, "application/json"))
            using (var resp = await _http.PostAsync(ChatUrl, content).ConfigureAwait(false))
            {
                var body = await resp.Content.ReadAsStringAsync().ConfigureAwait(false);
                if (!resp.IsSuccessStatusCode)
                    return "⚠ 翻译服务返回 " + (int)resp.StatusCode + "：" + Trim(body, 160);

                using (var doc = JsonDocument.Parse(body))
                {
                    var msg = doc.RootElement.GetProperty("choices")[0]
                                              .GetProperty("message")
                                              .GetProperty("content").GetString();
                    return Clean(msg);
                }
            }
        }

        private static string Trim(string s, int n)
        {
            if (string.IsNullOrEmpty(s)) return s;
            return s.Length <= n ? s : s.Substring(0, n) + "…";
        }

        /// <summary>去掉控制字符与模型偶尔吐出的 &lt;|...|&gt; 控制符残留。</summary>
        private static string Clean(string s)
        {
            if (string.IsNullOrEmpty(s)) return string.Empty;

            var sb = new StringBuilder();
            foreach (var ch in s)
            {
                if (ch == '\r') continue;
                if (ch < ' ' && ch != '\n' && ch != '\t') continue;
                sb.Append(ch);
            }

            var r = sb.ToString().Trim();
            int i;
            while ((i = r.IndexOf("<|", StringComparison.Ordinal)) >= 0)
            {
                int j = r.IndexOf("|>", i + 2, StringComparison.Ordinal);
                if (j < 0) { r = r.Substring(0, i); break; }
                r = r.Remove(i, j - i + 2);
            }
            return r.Trim();
        }
    }

    /// <summary>
    /// 组合引擎：SenseVoice ASR（离线识别）+ 混元 Hy-MT2（离线翻译，33 语种任意方向）。
    ///
    /// 与 SenseVoiceMtEngine（OPUS-MT）的取舍：
    ///   ・OPUS-MT：进程内、200~500ms/句，但一个方向一套模型（437MB），目前只装了中↔英；
    ///   ・Hy-MT2 ：需本机 llama-server 进程、1.5~2.5s/句，但 **一个 1.1GB 文件覆盖 33 语种任意方向**，
    ///             且译文质量明显更好（官方 FLORES-200 口径已超 Microsoft Translator / 豆包翻译 API）。
    /// 两者都全离线：llama-server 只监听 127.0.0.1，不联网。
    /// </summary>
    public sealed class SenseVoiceHyMtEngine : ITranslationEngine, IWarmupEngine
    {
        private readonly SenseVoiceAsrEngine _asr = new SenseVoiceAsrEngine();
        private readonly LlamaCppMtClient _mt = new LlamaCppMtClient();

        public bool IsModelReady
        {
            get { return File.Exists(TranslationConfig.LlmMtModelPath); }
        }

        public string DisplayName
        {
            get
            {
                var f = Path.GetFileName(TranslationConfig.LlmMtModelPath);
                return IsModelReady
                    ? "SenseVoice + 混元翻译（" + f + "·" + HyMtLang.Count + " 语种）"
                    : "SenseVoice + 混元翻译（模型缺失：" + f + "）";
            }
        }

        public bool IsDirectTranslate
        {
            get { return false; }
        }

        public string EndpointLabel { get { return _mt.EndpointLabel; } }

        public override string ToString()
        {
            return DisplayName;
        }

        public Task<string> TranscribeAsync(short[] pcm16, string lang)
        {
            return _asr.TranscribeAsync(pcm16, lang);
        }

        public Task<string> TranslateTextAsync(string text, string srcLang, string dstLang)
        {
            return _mt.TranslateAsync(text, srcLang, dstLang);
        }

        /// <summary>
        /// 预热两条腿并行：SenseVoice 模型加载（读 228MB int8 + 建 ORT 会话）与
        /// llama-server 拉起 + 首句空跑（读 1.1GB gguf + prompt 计算）。
        /// 两者互不依赖，并行能把「进页面 → 可用」的总等待压到较慢的那条（llama-server ≈3~5s）。
        /// </summary>
        public async Task<bool> WarmupAsync(string srcLang, string dstLang)
        {
            Task<bool> asrTask = Task.Run(() => _asr.WarmupAsync(srcLang, dstLang));
            Task<bool> mtTask = Task.Run(() => _mt.WarmupAsync());

            bool asrOk = false, mtOk = false;
            try { asrOk = await asrTask.ConfigureAwait(false); } catch { }
            try { mtOk = await mtTask.ConfigureAwait(false); } catch { }
            return asrOk && mtOk;
        }
    }
}
