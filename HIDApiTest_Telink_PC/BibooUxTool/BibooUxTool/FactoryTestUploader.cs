using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace BibooUxTool
{
    /// <summary>
    /// 产线测试记录上报到中央服务器（demo：HTTP REST，局域网内，无鉴权）。
    ///
    /// 服务器约定见 FactoryTestServer（Python demo，HIDApiTest_Telink_PC/FactoryTestServer/server.py）：
    ///   POST /api/result  { sn, dutName, dutMac, station, time, items[], pass, fail, total }
    ///   GET  /api/ping    健康检查
    ///
    /// 上位机侧只做 HTTP 客户端，不挑服务器实现（Python demo / .NET 服务 / 任何 OpenAI 之外的 REST 都行）。
    /// </summary>
    public static class FactoryTestUploader
    {
        private static readonly HttpClient _http = new HttpClient();

        static FactoryTestUploader()
        {
            _http.Timeout = TimeSpan.FromSeconds(15);
        }

        /// <summary>服务器是否在线（GET /api/ping，2s 超时）。</summary>
        public static async Task<bool> PingAsync(string baseUrl)
        {
            try
            {
                using (var cts = new CancellationTokenSource(2000))
                using (var r = await _http.GetAsync(baseUrl.TrimEnd('/') + "/api/ping", cts.Token)
                           .ConfigureAwait(false))
                    return r.IsSuccessStatusCode;
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// 上报一批产测记录。
        /// rows 为 FactoryTestForm._results 的同构：每条 { Time, Item, Value, Result }（string[4]）。
        /// 返回服务器是否成功落库（HTTP 200 且 body 含 "ok":true）。
        /// </summary>
        public static async Task<bool> UploadAsync(string baseUrl, string station, string sn,
            string dutName, string dutMac, List<string[]> rows, int pass, int fail)
        {
            if (rows == null || rows.Count == 0)
                return false;

            var items = new List<Dictionary<string, string>>(rows.Count);
            foreach (var r in rows)
            {
                items.Add(new Dictionary<string, string>
                {
                    { "time",   r.Length > 0 ? (r[0] ?? string.Empty) : string.Empty },
                    { "item",   r.Length > 1 ? (r[1] ?? string.Empty) : string.Empty },
                    { "value",  r.Length > 2 ? (r[2] ?? string.Empty) : string.Empty },
                    { "result", r.Length > 3 ? (r[3] ?? string.Empty) : string.Empty },
                });
            }

            var payload = new Dictionary<string, object>
            {
                { "sn",      sn ?? string.Empty },
                { "dutName", dutName ?? string.Empty },
                { "dutMac",  dutMac ?? string.Empty },
                { "station", station ?? string.Empty },
                { "time",    DateTime.Now.ToString("yyyy-MM-ddTHH:mm:ss") },
                { "items",   items },
                { "pass",    pass },
                { "fail",    fail },
                { "total",   rows.Count },
            };

            var json = JsonSerializer.Serialize(payload);
            try
            {
                using (var content = new StringContent(json, Encoding.UTF8, "application/json"))
                using (var resp = await _http.PostAsync(baseUrl.TrimEnd('/') + "/api/result", content)
                           .ConfigureAwait(false))
                {
                    var body = await resp.Content.ReadAsStringAsync().ConfigureAwait(false);
                    return resp.IsSuccessStatusCode
                        && body.IndexOf("\"ok\"", StringComparison.Ordinal) >= 0
                        && body.IndexOf("true", StringComparison.Ordinal) >= 0;
                }
            }
            catch
            {
                return false;
            }
        }
    }
}
