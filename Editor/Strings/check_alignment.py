#!/usr/bin/env python3
"""i18n 翻译文件对齐校验：en/ 与 zh-CN/ 各模块 json 的 key 集合必须一致，
且同一模块两语言的 key 一一对应。退出码非零 = 有漂移（可挂 CI/冒烟）。"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MISSING = 0
for module in sorted(p.name for p in (ROOT / "en").glob("*.json")):
    en = json.loads((ROOT / "en" / module).read_text(encoding="utf-8"))
    zh_path = ROOT / "zh-CN" / module
    if not zh_path.exists():
        print(f"[FAIL] zh-CN 缺模块文件 {module}")
        MISSING += 1
        continue
    zh = json.loads(zh_path.read_text(encoding="utf-8"))
    only_en = sorted(set(en) - set(zh))
    only_zh = sorted(set(zh) - set(en))
    for k in only_en:
        print(f"[FAIL] {module}: en 有 zh-CN 无：{k}")
    for k in only_zh:
        print(f"[FAIL] {module}: zh-CN 有 en 无：{k}")
    MISSING += len(only_en) + len(only_zh)
    print(f"[ok] {module}: {len(en)} keys 对齐" if not (only_en or only_zh) else "")
for module in sorted(p.name for p in (ROOT / "zh-CN").glob("*.json")):
    if not (ROOT / "en" / module).exists():
        print(f"[FAIL] en 缺模块文件 {module}")
        MISSING += 1
sys.exit(1 if MISSING else 0)
