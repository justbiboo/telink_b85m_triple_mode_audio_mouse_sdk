# -*- coding: utf-8 -*-
"""Verify OTA-related string literals landed in the built exe (UTF-16LE #US heap)."""
import re, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "bin", "Debug", "net48", "Biboo OS.exe")

data = open(EXE, "rb").read()

# targets expressed as \u escapes -> immune to shell encoding issues
targets = {
    "SLOT_BEGIN \u88ab\u62d2":              "SLOT_BEGIN 被拒",
    "\u5f15\u64ce\u542f\u52a8\u5931\u8d25":  "引擎启动失败",
    "BLE \u63a8\u5305\u505c\u6ede":          "BLE 推包停滞",
    "OTA_FLOW_ERR":                          "OTA_FLOW_ERR",
    "\u65ad\u7535\u91cd\u542f":              "断电重启",
    "master OTA \u5df2\u5c31\u7eea":         "master OTA 已就绪",
    "START \u88ab\u62d2":                    "START 被拒",
    "\u4f1a\u8bdd\u5df2\u88ab Dongle \u4e2d\u6b62": "会话已被 Dongle 中止",
}

def utf16le_variants(s):
    """Both even and odd alignment of the UTF-16LE byte pattern."""
    b = s.encode("utf-16le")
    return [b]

ok = True
for esc, human in targets.items():
    b = esc.encode("utf-16le")
    n = data.count(b)
    status = "FOUND x%d" % n if n else "MISSING"
    if not n:
        ok = False
    print("%-12s %s" % (status, human))

# method name lives in metadata as UTF-8
print("%-12s %s" % ("FOUND" if b"QueryStatusText" in data else "MISSING", "QueryStatusText (utf8 method name)"))
if b"QueryStatusText" not in data:
    ok = False

sys.exit(0 if ok else 1)
