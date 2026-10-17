#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FactoryTestServer - 产线测试数据汇总（demo，最小可用）

职责（仅打通通路 / demo，无鉴权，仅限局域网内）：
  POST /api/result   接收上位机产测记录，追加写入按日期的 CSV（data/results_YYYYMMDD.csv）
  GET  /api/ping     健康检查，返回 {"ok": true, "time": ...}
  GET  /            极简 HTML 看板，展示今日记录与最近 50 条

协议（与上位机 FactoryTestUploader 对齐）：
  上报 body（JSON）：
    {
      "sn":      "",            # 唯一标识（demo 留空，后续接 SN 录入）
      "dutName": "BIBOO...",    # DUT 蓝牙名（可选）
      "dutMac":  "AA:BB:...",   # DUT MAC（可选）
      "station": "ST-01",       # 工位号
      "time":    "ISO8601",     # 上位机本地时间
      "items":   [ {"time":"..","item":"PING","value":"..","result":"PASS"}, ... ],
      "pass":    N, "fail": M, "total": K
    }
  响应：{"ok": true, "seq": <递增序号>}

生产化提示：需加鉴权（token / 内网 VPN）+ 持久化到 SQLite + 失败重试。

运行：
  python server.py            # 监听 0.0.0.0:8081
  python server.py --host 127.0.0.1 --port 8082
  python server.py --datadir /vol1/1000/FactoryTest        # 数据落盘到指定目录（NAS/容器挂卷用）

环境变量（优先级：命令行 > 环境变量 > 默认值）：
  FT_HOST / FT_PORT / FT_DATA_DIR
"""
import argparse
import csv
import json
import os
import threading
from datetime import datetime

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

ROOT = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DATA_DIR = os.environ.get("FT_DATA_DIR") or os.path.join(ROOT, "data")

# 实际落盘目录（main() 里按 --datadir / FT_DATA_DIR 覆盖；容器部署时指向挂卷）
STORE_DIR = DEFAULT_DATA_DIR
os.makedirs(STORE_DIR, exist_ok=True)

_lock = threading.Lock()
_seq = 0

COLUMNS = ["seq", "recv_time", "sn", "dutName", "dutMac", "station",
           "pass", "fail", "total", "items_json"]


def csv_path():
    return os.path.join(STORE_DIR, "results_" + datetime.now().strftime("%Y%m%d") + ".csv")


def append_record(rec):
    global _seq
    with _lock:
        _seq += 1
        rec["seq"] = _seq
        rec["recv_time"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        path = csv_path()
        new = not os.path.exists(path)
        with open(path, "a", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=COLUMNS)
            if new:
                w.writeheader()
            w.writerow({k: rec.get(k, "") for k in COLUMNS})
    return _seq


class Handler(BaseHTTPRequestHandler):
    # ---------- 工具 ----------
    def _send_json(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _serve_dashboard(self):
        path = csv_path()
        rows = []
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                rows = list(csv.DictReader(f))
        html = "<!doctype html><html lang=zh><meta charset=utf-8>"
        html += "<title>产测看板 demo</title>"
        html += "<style>body{font-family:Microsoft YaHei,Segoe UI,sans-serif;margin:24px}"
        html += "table{border-collapse:collapse;font-size:13px}"
        html += "th,td{border:1px solid #ccc;padding:4px 8px;text-align:left}"
        html += "th{background:#f0f4fb}h2{color:#26408b}</style>"
        html += "<h2>产线测试看板（demo）</h2>"
        html += "<p>今日记录 <b>%d</b> 条 · 存储：<code>%s</code></p>" % (len(rows), path)
        html += "<table><tr><th>seq</th><th>时间</th><th>SN</th><th>DUT</th>"
        html += "<th>MAC</th><th>工位</th><th>PASS</th><th>FAIL</th><th>项数</th></tr>"
        for r in rows[-50:]:
            html += ("<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td>"
                     "<td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>") % (
                r.get("seq", ""), r.get("recv_time", ""), r.get("sn", ""),
                r.get("dutName", ""), r.get("dutMac", ""), r.get("station", ""),
                r.get("pass", ""), r.get("fail", ""), r.get("total", ""))
        html += "</table>"
        body = html.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    # ---------- 路由 ----------
    def do_GET(self):
        p = urlparse(self.path)
        if p.path == "/api/ping":
            self._send_json(200, {"ok": True, "time": datetime.now().isoformat()})
        elif p.path == "/":
            self._serve_dashboard()
        else:
            self._send_json(404, {"ok": False, "error": "not found"})

    def do_POST(self):
        p = urlparse(self.path)
        if p.path != "/api/result":
            self._send_json(404, {"ok": False, "error": "not found"})
            return
        try:
            length = int(self.headers.get("Content-Length", 0) or 0)
            raw = self.rfile.read(length) if length else b"{}"
            data = json.loads(raw.decode("utf-8"))
        except Exception as e:
            self._send_json(400, {"ok": False, "error": "bad json: " + str(e)})
            return

        items = data.get("items", []) or []
        rec = {
            "sn": data.get("sn", "") or "",
            "dutName": data.get("dutName", "") or "",
            "dutMac": data.get("dutMac", "") or "",
            "station": data.get("station", "") or "",
            "pass": data.get("pass", 0),
            "fail": data.get("fail", 0),
            "total": data.get("total", len(items)),
            "items_json": json.dumps(items, ensure_ascii=False),
        }
        try:
            seq = append_record(rec)
            self._send_json(200, {"ok": True, "seq": seq})
        except Exception as e:
            self._send_json(500, {"ok": False, "error": str(e)})

    def log_message(self, fmt, *args):
        pass  # 静默，避免刷屏


def main():
    global STORE_DIR
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=os.environ.get("FT_HOST") or "0.0.0.0")
    ap.add_argument("--port", type=int, default=int(os.environ.get("FT_PORT") or 8081))
    ap.add_argument("--datadir", default=os.environ.get("FT_DATA_DIR") or os.path.join(ROOT, "data"),
                    help="CSV 落盘目录（容器部署请挂卷到这里）")
    args = ap.parse_args()

    STORE_DIR = args.datadir
    os.makedirs(STORE_DIR, exist_ok=True)

    srv = ThreadingHTTPServer((args.host, args.port), Handler)
    print("FactoryTestServer demo 已启动: http://%s:%d" % (args.host, args.port))
    print("  看板: http://%s:%d/   上报: POST http://%s:%d/api/result"
          % (args.host, args.port, args.host, args.port))
    print("  数据目录: %s   (Ctrl+C 停止)" % STORE_DIR)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        srv.shutdown()
        print("\n已停止。")


if __name__ == "__main__":
    main()
