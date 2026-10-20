using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using Microsoft.ML.OnnxRuntime;
using Microsoft.ML.OnnxRuntime.Tensors;
using Microsoft.ML.Tokenizers;

namespace BibooUxTool
{
    /// <summary>
    /// 离线神经机器翻译（NMT）：Marian / OPUS-MT 的 ONNX int8 导出，纯 CPU、纯离线、无本地服务。
    ///
    /// 为什么不用「ASR 直译」（Whisper translate）：Whisper 直译只能出英语、且慢；OPUS-MT 是
    /// 专做翻译的 seq2seq 小模型（本机实测中英互译 **200~350ms/句**，词级准确度明显更好）。
    ///
    /// 每个方向一个模型目录，目录内需要 5 个文件（取自 HuggingFace 的 Xenova/opus-mt-*-* 仓库）：
    ///   encoder_model_int8.onnx      编码器
    ///   decoder_model_int8.onnx      解码器首步（无 past，吃 encoder_hidden_states）
    ///   decoder_with_past_model_int8.onnx  解码器后续步（只吃 past，encoder K/V 复用首步结果）
    ///   source.spm                   SentencePiece unigram 分词模型（32000 词表）
    ///   vocab.json                   模型真正的 **65001 词表**（piece → id）
    ///
    /// ⚠ 两个坑（都踩过，别再动）：
    ///   1) **source.spm 的 id 空间 ≠ 模型的 id 空间**。spm 有 32000 个 piece，其 `</s>`=2；
    ///      而模型词表 65001，`</s>`=0、`<unk>`=1、`<pad>`=65000、`decoder_start_token_id`=65000。
    ///      必须「用 spm 切词拿到 piece 字符串 → 再查 vocab.json 得到模型 id」，直接用 spm 的 id 喂模型
    ///      会译出完全无关的句子（实测：'我觉得你这个不错' → 'it's for...'）。
    ///      解码同理：用 vocab.json 反查 piece 再拼回文本。
    ///   2) **不能用 decoder_model_merged.onnx**（单文件 KV-cache 合并图）：其 If 子图在 ORT 1.28
    ///      下报 "Reshape ... dimension with value zero"，喂空/占位 past 都不行。用拆分的
    ///      decoder + decoder_with_past 两张图（上面 3 个 onnx 文件）就没这问题。
    /// </summary>
    public sealed class MarianMt : IDisposable
    {
        private readonly InferenceSession _enc;
        private readonly InferenceSession _decFirst;
        private readonly InferenceSession _decPast;
        private readonly SentencePieceTokenizer _tok;
        private readonly Dictionary<string, int> _pieceToId = new Dictionary<string, int>(StringComparer.Ordinal);
        private readonly Dictionary<int, string> _idToPiece = new Dictionary<int, string>();
        private readonly string[] _pastInputNames;
        private readonly object _gate = new object();

        private const int Heads = 8;
        private const int HeadDim = 64;

        /// <summary>Marian 的 decoder_start_token_id / pad_token_id（vocab.json 空间）。</summary>
        private const int StartTokenId = 65000;
        /// <summary>Marian 的 eos_token_id（vocab.json 空间）。</summary>
        private const int EosTokenId = 0;
        private const int UnkTokenId = 1;
        private const int PadTokenId = 65000;

        public string ModelDir { get; private set; }

        public MarianMt(string modelDir)
        {
            ModelDir = modelDir;
            string spm = Path.Combine(modelDir, "source.spm");
            string enc = Path.Combine(modelDir, "encoder_model_int8.onnx");
            string d0 = Path.Combine(modelDir, "decoder_model_int8.onnx");
            string dp = Path.Combine(modelDir, "decoder_with_past_model_int8.onnx");
            string vocab = Path.Combine(modelDir, "vocab.json");

            foreach (var f in new[] { spm, enc, d0, dp, vocab })
                if (!File.Exists(f))
                    throw new FileNotFoundException("离线翻译模型文件缺失：" + Path.GetFileName(f), f);

            using (var fs = File.OpenRead(spm))
                _tok = SentencePieceTokenizer.Create(fs, false, false, null);

            using (var doc = JsonDocument.Parse(File.ReadAllBytes(vocab)))
            {
                foreach (var p in doc.RootElement.EnumerateObject())
                {
                    int id = p.Value.GetInt32();
                    _pieceToId[p.Name] = id;
                    _idToPiece[id] = p.Name;
                }
            }

            _enc = new InferenceSession(enc);
            _decFirst = new InferenceSession(d0);
            _decPast = new InferenceSession(dp);
            _pastInputNames = _decPast.InputMetadata.Keys
                .Where(k => k.StartsWith("past_key_values.", StringComparison.Ordinal))
                .OrderBy(k => k, StringComparer.Ordinal)
                .ToArray();
        }

        /// <summary>spm 切词 → 查 vocab.json 映射到模型 id 空间，末尾补 &lt;/s&gt;（对齐 HF MarianTokenizer）。</summary>
        public List<long> EncodeToIds(string text)
        {
            var ids = new List<long>();
            string _;
            var pieces = _tok.EncodeToTokens(text, out _, false, false, true, true);
            foreach (var p in pieces)
            {
                int id;
                if (p.Value != null && _pieceToId.TryGetValue(p.Value, out id))
                    ids.Add(id);
                else
                    ids.Add(UnkTokenId);
            }
            ids.Add(EosTokenId);
            return ids;
        }

        /// <summary>模型 id → piece → 文本（▁ 还原为空格）。</summary>
        public string DecodeIds(IEnumerable<int> ids)
        {
            var sb = new StringBuilder();
            var bytes = new List<byte>();
            foreach (var id in ids)
            {
                if (id == EosTokenId || id == StartTokenId || id == PadTokenId) continue;
                string pc;
                if (!_idToPiece.TryGetValue(id, out pc) || pc == null) continue;

                // SentencePiece 的字节回退 piece（<0xE6>）：攒够一个完整 UTF-8 序列再输出
                if (pc.Length == 6 && pc.StartsWith("<0x", StringComparison.Ordinal) && pc.EndsWith(">", StringComparison.Ordinal))
                {
                    int b;
                    if (int.TryParse(pc.Substring(3, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out b))
                    {
                        bytes.Add((byte)b);
                        continue;
                    }
                }
                if (bytes.Count > 0)
                {
                    sb.Append(Encoding.UTF8.GetString(bytes.ToArray()));
                    bytes.Clear();
                }
                sb.Append(pc);
            }
            if (bytes.Count > 0) sb.Append(Encoding.UTF8.GetString(bytes.ToArray()));

            return sb.ToString().Replace('\u2581', ' ').Trim();
        }

        /// <summary>贪心解码（greedy，无需 beam search：质量已够，且快）。</summary>
        public string Translate(string text, int maxNewTokens)
        {
            if (string.IsNullOrWhiteSpace(text)) return string.Empty;
            if (maxNewTokens <= 0) maxNewTokens = 128;

            lock (_gate)
            {
                var encIds = EncodeToIds(text);
                int n = encIds.Count;

                var idTensor = new DenseTensor<long>(new[] { 1, n });
                for (int i = 0; i < n; i++) idTensor[0, i] = encIds[i];
                var maskTensor = new DenseTensor<long>(new[] { 1, n });
                for (int i = 0; i < n; i++) maskTensor[0, i] = 1;

                // ---- 编码器 ----
                DenseTensor<float> hidden;
                using (var res = _enc.Run(new[]
                {
                    NamedOnnxValue.CreateFromTensor("input_ids", idTensor),
                    NamedOnnxValue.CreateFromTensor("attention_mask", maskTensor),
                }))
                {
                    var t = res.First(r => r.Name == "last_hidden_state").AsTensor<float>();
                    hidden = new DenseTensor<float>(t.ToArray(), t.Dimensions.ToArray());
                }

                // ---- 解码器：KV cache 逐 token ----
                var past = new Dictionary<string, DenseTensor<float>>(StringComparer.Ordinal);
                var outIds = new List<int>();
                long cur = StartTokenId;

                for (int step = 0; step < maxNewTokens; step++)
                {
                    var inputs = new List<NamedOnnxValue>();
                    var curIds = new DenseTensor<long>(new[] { 1, 1 });
                    curIds[0, 0] = cur;
                    inputs.Add(NamedOnnxValue.CreateFromTensor("input_ids", curIds));
                    inputs.Add(NamedOnnxValue.CreateFromTensor("encoder_attention_mask", maskTensor));

                    if (step == 0)
                    {
                        inputs.Add(NamedOnnxValue.CreateFromTensor("encoder_hidden_states", hidden));
                    }
                    else
                    {
                        // 后续步只需要 past：encoder 的 K/V 用首步算出的那份，一直复用
                        foreach (var nm in _pastInputNames)
                        {
                            DenseTensor<float> v;
                            if (!past.TryGetValue(nm, out v))
                                v = new DenseTensor<float>(new float[Heads * HeadDim], new[] { 1, Heads, 1, HeadDim });
                            inputs.Add(NamedOnnxValue.CreateFromTensor(nm, v));
                        }
                    }

                    using (var res = step == 0 ? _decFirst.Run(inputs) : _decPast.Run(inputs))
                    {
                        var logits = res.First(r => r.Name == "logits").AsTensor<float>();   // [1,1,65001]
                        int vlen = logits.Dimensions[2];
                        int best = -1;
                        float bestScore = float.NegativeInfinity;
                        for (int v = 0; v < vlen; v++)
                        {
                            if (v == StartTokenId || v == PadTokenId) continue;          // bad_words_ids=[[65000]]
                            float x = logits[0, 0, v];
                            if (x > bestScore) { bestScore = x; best = v; }
                        }
                        if (best < 0 || best == EosTokenId) break;
                        outIds.Add(best);
                        cur = best;

                        // 缓存更新：decoder_with_past 只回 present.N.decoder.*，encoder 那份沿用首步的
                        foreach (var r in res)
                        {
                            if (!r.Name.StartsWith("present.", StringComparison.Ordinal)) continue;
                            var t = r.AsTensor<float>();
                            past["past_key_values." + r.Name.Substring("present.".Length)] =
                                new DenseTensor<float>(t.ToArray(), t.Dimensions.ToArray());
                        }
                    }
                }

                return DecodeIds(outIds);
            }
        }

        public void Dispose()
        {
            try { _enc.Dispose(); } catch { }
            try { _decFirst.Dispose(); } catch { }
            try { _decPast.Dispose(); } catch { }
        }
    }

    /// <summary>离线翻译的语种对管理：目前只有中↔英两套 OPUS-MT 模型（可扩展成任意对有模型的语种对）。</summary>
    public static class MtPair
    {
        /// <summary>把界面里的语言代码归一成模型用的两字母码（zh-CN / zh-TW / en-US → zh / en）。</summary>
        public static string Normalize(string code)
        {
            if (string.IsNullOrEmpty(code)) return string.Empty;
            string c = code.Trim().ToLowerInvariant();
            int dash = c.IndexOf('-');
            if (dash > 0) c = c.Substring(0, dash);
            if (c == "zh-hans" || c == "zh-hant" || c == "cmn") c = "zh";
            return c;
        }

        /// <summary>取某方向的模型目录；不支持则返回 null。</summary>
        public static string DirFor(string srcLang, string dstLang)
        {
            string s = Normalize(srcLang), d = Normalize(dstLang);
            if (s.Length == 0 || d.Length == 0 || s == d) return null;
            string name = "opus-mt-" + s + "-" + d;
            string dir = Path.Combine(TranslationConfig.MtModelsDir, name);
            return Directory.Exists(dir) ? dir : null;
        }

        /// <summary>目录里该有的 5 个文件是否齐全。</summary>
        public static bool IsDirComplete(string dir)
        {
            if (string.IsNullOrEmpty(dir) || !Directory.Exists(dir)) return false;
            foreach (var f in new[]
            {
                "encoder_model_int8.onnx", "decoder_model_int8.onnx", "decoder_with_past_model_int8.onnx",
                "source.spm", "vocab.json",
            })
                if (!File.Exists(Path.Combine(dir, f))) return false;
            return true;
        }

        public static bool AnyModelInstalled
        {
            get { return IsDirComplete(DirFor("zh", "en")) || IsDirComplete(DirFor("en", "zh")); }
        }

        /// <summary>已装的方向，给界面做提示文案。</summary>
        public static string InstalledSummary
        {
            get
            {
                var list = new List<string>();
                if (IsDirComplete(DirFor("zh", "en"))) list.Add("中→英");
                if (IsDirComplete(DirFor("en", "zh"))) list.Add("英→中");
                return list.Count == 0 ? "未安装（需 Models\\mt\\opus-mt-<src>-<dst>）" : string.Join("、", list);
            }
        }
    }

    /// <summary>
    /// 组合引擎：SenseVoice ASR（离线识别）+ Marian/OPUS-MT（离线翻译），全链路离线、无本地服务。
    /// 这就是「对着鼠标说中文 → 出英文」的那条链路；反之英译中同理。
    /// 目前 MT 只有中↔英两套模型，选到别的语种会如实回一句提示，不会假装翻译成功。
    /// </summary>
    public sealed class SenseVoiceMtEngine : ITranslationEngine, IWarmupEngine
    {
        private readonly SenseVoiceAsrEngine _asr = new SenseVoiceAsrEngine();
        private readonly Dictionary<string, MarianMt> _models = new Dictionary<string, MarianMt>(StringComparer.Ordinal);
        private readonly object _gate = new object();

        public string DisplayName
        {
            get
            {
                return MtPair.AnyModelInstalled
                    ? "SenseVoice + 离线翻译（" + MtPair.InstalledSummary + "·全离线）"
                    : "SenseVoice + 离线翻译（MT 模型缺失）";
            }
        }

        public bool IsDirectTranslate
        {
            get { return false; }
        }

        public override string ToString()
        {
            return DisplayName;
        }

        public Task<string> TranscribeAsync(short[] pcm16, string lang)
        {
            return _asr.TranscribeAsync(pcm16, lang);
        }

        private MarianMt GetModel(string dir)
        {
            lock (_gate)
            {
                MarianMt m;
                if (_models.TryGetValue(dir, out m)) return m;
                m = new MarianMt(dir);
                _models[dir] = m;      // 缓存：换方向回来时不必再花 ~2s 重新加载
                return m;
            }
        }

        public Task<string> TranslateTextAsync(string text, string srcLang, string dstLang)
        {
            if (string.IsNullOrWhiteSpace(text))
                return Task.FromResult(string.Empty);

            string dir = MtPair.DirFor(srcLang, dstLang);
            if (string.IsNullOrEmpty(dir))
            {
                return Task.FromResult(
                    "⚠ 离线翻译当前只装了：" + MtPair.InstalledSummary
                    + "（本次 " + srcLang + "→" + dstLang + " 未翻译）");
            }

            return Task.Run(() =>
            {
                var mt = GetModel(dir);
                return mt.Translate(text, TranslationConfig.MtMaxNewTokens);
            });
        }

        /// <summary>
        /// 预热：并行加载 SenseVoice（228MB int8）与当前方向的 OPUS-MT（3 张 onnx int8），
        /// 再空跑一句「你好」，把 ORT 会话首帧开销也提前付掉（OPUS-MT 单句仅 200~500ms，空跑很便宜）。
        /// </summary>
        public async Task<bool> WarmupAsync(string srcLang, string dstLang)
        {
            Task<bool> asrTask = Task.Run(() => _asr.WarmupAsync(srcLang, dstLang));
            string dir = MtPair.DirFor(srcLang, dstLang);

            Task<bool> mtTask = Task.Run(() =>
            {
                if (string.IsNullOrEmpty(dir)) return false;
                try
                {
                    var mt = GetModel(dir);
                    mt.Translate("你好", TranslationConfig.MtMaxNewTokens);
                    return true;
                }
                catch
                {
                    return false;
                }
            });

            bool asrOk = false, mtOk = false;
            try { asrOk = await asrTask.ConfigureAwait(false); } catch { }
            try { mtOk = await mtTask.ConfigureAwait(false); } catch { }
            return asrOk && mtOk;
        }
    }
}
