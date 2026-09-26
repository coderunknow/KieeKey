#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""BS-24 — phân tích gói báo cáo hiện trường từ máy Windows 10 LTSC của user.

Usage:
    python3 handoff/analyze_report.py <kieekey-report-<thoi-gian>.zip>

Đọc nội dung zip (ui-probe/ui_probe.json, console.log, system-facts.txt)
và in một bản tóm tắt ngắn bằng tiếng Việt + Anh:

  * host line + nativeDpi (máy thật tự khai)
  * mỗi (scale, tab): CLEAN hay findings cụ thể, rect dialog/page, travel
  * harness: fuzz steps/assertions/violations, invariants, firstViolations
  * số pixel audit (screenCaptures / unavailable)
  * system-facts: LogPixels, số tiến trình KieeKeyApp + version + SHA,
    driver, DWM, cửa sổ phủ vùng dialog
  * exit code của probe (dòng cuối console.log)

Exit code:
    0  = probe báo SẠCH (0 findings, 0 violations)
    2  = có findings/violations  →  RED TRÊN MÁY THẬT (mỏ neo để vá)
    1  = input thiếu/hỏng
"""
from __future__ import annotations

import json
import sys
import tempfile
import zipfile
from pathlib import Path


def _get(d: dict, key: str, default=""):
    v = d.get(key, default)
    return default if v is None else v


def _fmt_invariants(inv) -> str:
    if isinstance(inv, dict):
        return " ".join(f"{k}:{v}" for k, v in inv.items())
    return str(inv)


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    zip_path = Path(sys.argv[1])
    if not zip_path.is_file():
        print(f"FAIL: not a file: {zip_path}")
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        z = zipfile.ZipFile(zip_path)
        z.extractall(tmp)
        root = Path(tmp)

        probe_json = root / "ui-probe" / "ui_probe.json"
        console_log = root / "ui-probe" / "console.log"
        facts = root / "system-facts.txt"

        if not probe_json.is_file():
            print("FAIL: ui-probe/ui_probe.json not found in the zip")
            print("  (did the probe finish? check ui-probe/console.log if present)")
            for p in sorted(root.rglob("*")):
                if p.is_file():
                    print("   in zip:", p.relative_to(root))
            return 1

        # ---------------- ui_probe.json ----------------
        try:
            data = json.loads(probe_json.read_text(encoding="utf-8"))
        except json.JSONDecodeError as e:
            print(f"FAIL: ui_probe.json is not valid JSON ({e})")
            return 1

        print("=" * 72)
        print("BÁO CÁO PROBE TRÊN MÁY THẬT (BS-24)")
        print("=" * 72)
        print(f"host        : {_get(data, 'host')}")
        print(f"nativeDpi   : {_get(data, 'nativeDpi')}   "
              f"(144 = 150% thật của máy LTSC)")
        total_findings = int(_get(data, "findings", 0))
        fuzz_v = int(_get(data, "fuzzViolations", 0))
        scen_v = int(_get(data, "scenarioViolations", 0))
        print(
            f"tổng findings: {total_findings} | "
            f"fuzz: {_get(data, 'fuzzSteps')} steps / "
            f"{_get(data, 'fuzzChecks')} assertions / "
            f"{fuzz_v} violations | "
            f"scenarios: {_get(data, 'scenarios')} run, {scen_v} with violations"
        )
        print(
            f"screen pixel audits: {_get(data, 'screenCaptures')} run / "
            f"{_get(data, 'screenUnavailable')} unavailable | "
            f"controls {_get(data, 'controls')} | checks {_get(data, 'checks')} "
            f"| trace {_get(data, 'traceLines')} lines"
        )
        inv = _get(data, "invariants")
        if inv:
            print(f"invariants  : {_fmt_invariants(inv)}")
        for note in _get(data, "passEntries", []):
            print(f"pass entry  : {note}")
        for note in _get(data, "handover", []):
            print(f"handover    : {note}")

        by_kind = _get(data, "findingsByKind", {})
        if isinstance(by_kind, dict) and by_kind:
            print("findings theo loại : " +
                  " ".join(f"{k}={v}" for k, v in by_kind.items()))

        tabs = data.get("tabs", [])
        print("-" * 72)
        print("TỪNG (SCALE, TAB):")
        dirty = 0
        for t in tabs:
            tab = _get(t, "tab")
            scale = _get(t, "scale")
            findings = t.get("findings", [])
            dlg = _get(t, "dialog")
            page = _get(t, "page")
            travel = _get(t, "scrollTravel")
            if isinstance(dlg, list):
                dlg = "x".join(str(x) for x in dlg)
            if isinstance(page, list):
                page = f"[{','.join(str(x) for x in page)}]"
            if findings:
                dirty += 1
                print(f"  t{tab}@{scale}%  dialog={dlg} page={page} "
                      f"travel={travel}  ->  {len(findings)} FINDING(S):")
                for f in findings:
                    detail = f.get("detail", f) if isinstance(f, dict) else f
                    kind = f.get("kind", "?") if isinstance(f, dict) else "?"
                    print(f"     [{kind}] {detail}")
            else:
                print(f"  t{tab}@{scale}%  dialog={dlg} page={page} "
                      f"travel={travel}  ->  SẠCH (0 findings)")
        print("-" * 72)
        fv = data.get("firstViolations", [])
        if fv:
            print("VI PHẠM ĐẦU TIÊN CỦA HARNESS:")
            for v in fv:
                if isinstance(v, dict):
                    print(f"  [{_get(v, 'inv')}] {_get(v, 'state')}")
                else:
                    print(f"  {v}")

        # ---------------- console.log ----------------
        exit_code = "?"
        if console_log.is_file():
            lines = [l.strip() for l in
                     console_log.read_text(encoding="utf-8", errors="replace").splitlines()
                     if l.strip()]
            print("-" * 72)
            print("PROBE CONSOLE (đầu + cuối):")
            if len(lines) > 10:
                for l in lines[:6]:
                    print("  | " + l)
                print("  | ...")
                for l in lines[-6:]:
                    print("  | " + l)
            else:
                for l in lines:
                    print("  | " + l)
            for l in reversed(lines):
                if l.startswith("probe exit code ="):
                    exit_code = l.split("=", 1)[1].strip()
                    break
        print(f"probe exit code: {exit_code}")

        # ---------------- system-facts.txt ----------------
        if facts.is_file():
            txt = facts.read_text(encoding="utf-8", errors="replace")
            print("-" * 72)
            print("SYSTEM FACTS (các dòng chính):")
            keep = (
                "collected", "machine", "LogPixels", "FontSmoothing",
                "DragFullWindows", "KieeKeyApp.exe processes", "pid=",
                "file version", "sha256", "Caption", "BuildNumber",
                "DisplayVersion", "DriverVersion", "VideoModeDescription",
                "enable_aero", "dwm process", "settings dialog on screen",
                "windows OVERLAPPING", "pid=", "class=", "NOT RUNNING",
                "Add-Type",
            )
            shown = 0
            for line in txt.splitlines():
                s = line.strip()
                if not s:
                    continue
                if any(k in s for k in keep):
                    print("  | " + s)
                    shown += 1
                if shown > 60:
                    print("  | ... (còn tiếp trong file gốc)")
                    break
        else:
            print("FAIL: system-facts.txt not found in the zip")

        print("=" * 72)
        clean = (total_findings == 0 and fuzz_v == 0 and scen_v == 0
                 and exit_code == "0")
        if clean:
            print("KẾT LUẬN PHÂN TÍCH: PROBE SẠCH TRÊN MÁY THẬT "
                  "(0 findings, 0 violations, exit 0).")
            print("  -> Theo kế hoạch BS-24: trục còn lại là TIẾN TRÌNH/MÔI "
                  "TRƯỜNG (H1/H2/H7) hoặc dialog 'sống' qua tick —")
            print("  -> xét H2 (clean boot) / Phase 1b (bản chẩn đoán khai "
                  "hiện trường). KHÔNG VA CHỒNG THEO PHỎNG ĐOÁN.")
            return 0
        if total_findings > 0 or fuzz_v > 0 or scen_v > 0:
            print("KẾT LUẬN PHÂN TÍCH: RED TRÊN MÁY THẬT — đây là mỏ neo hợp "
                  "lệ để vá có đích (Phase 2 → 3).")
            print("  -> Ghi docs/HYPOTHESES_BS24_v1.3.0-beta8fix3.md: host line,")
            print("     findings + rect/px, firstViolations, PNG tab liên quan.")
            return 2
        print("KẾT LUẬN PHÂN TÍCH: probe CHẠY NHƯNG KHÔNG XINH HOÀN TOÀN "
              "(exit code khác 0) — cần đọc console.log chi tiết.")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
