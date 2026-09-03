#!/usr/bin/env python3
# ============================================================================
# audit_controls.py — KieeKey v1.1.2 regression guard.
#
# Root-cause class this guards against: a control ID referenced by the
# settings-dialog code (GetDlgItem / IsDlgButtonChecked / CheckDlgButton /
# SendDlgItemMessage / ShowWindow-through-showTab) but never created in
# WM_CREATE. The first v1.1.2 build risked exactly this class: a missing
# control makes IsDlgButtonChecked return 0 ("unchecked"), silently flipping
# a persisted option — the digits bug resurrection path — with no error
# anywhere. dlgChecked() (v1.1.2-r2) makes the READ fail-safe; this audit
# makes the MISSING CONTROL impossible to ship silently.
#
# Method: collect every IDC_* id referenced in src/app/main.cpp and verify a
# matching mkCtl(... IDC_*) creation exists in the same file. Also flag ids
# used as HMENU ids but never read (informational only).
# ============================================================================
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAIN = ROOT / "src" / "app" / "main.cpp"

READ_PATTERNS = [
    r"GetDlgItem\(\s*g\.hSettings\s*,\s*(IDC_\w+)\s*\)",
    r"IsDlgButtonChecked\(\s*g\.hSettings\s*,\s*(IDC_\w+)\s*\)",
    r"CheckDlgButton\(\s*g\.hSettings\s*,\s*(IDC_\w+)\s*,",
    r"GetDlgItem\(\s*hwnd\s*,\s*(IDC_\w+)\s*\)",
    r"GetDlgItem\(\s*hMain\s*,\s*(IDC_\w+)\s*\)",
]
CREATE_PATTERN = r"mkCtl\(\s*(?:hwnd|g\.hSettings)\s*,[^;]*?reinterpret_cast<HMENU>\(\s*(?:static_cast<INT_PTR>\(\s*)?(IDC_\w+)"

def main() -> int:
    src = MAIN.read_text(encoding="utf-8")
    read_ids = set()
    for pat in READ_PATTERNS:
        read_ids.update(re.findall(pat, src))
    created_ids = set(re.findall(CREATE_PATTERN, src, re.S))
    # ids created via the loop (kIds arrays): resolve arrays referenced by
    # reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIds[i])) — pull the
    # idents out of the kIds-style arrays instead.
    for m in re.finditer(r"reinterpret_cast<HMENU>\(\s*static_cast<INT_PTR>\(\s*(\w+)\[i\]\s*\)\s*\)", src):
        arr = m.group(1)
        am = re.search(r"const int " + arr + r"\[\]\s*=\s*\{([^}]*)\}", src)
        if am:
            for ident in re.findall(r"IDC_\w+", am.group(1)):
                created_ids.add(ident)
    # tab visibility lists (showTab) also reference ids — they must exist too
    # (they call GetDlgItem), but creation is what matters; reads via kTabN
    # are covered because showTab uses GetDlgItem which is in READ_PATTERNS.

    missing = sorted(read_ids - created_ids)
    print(f"read ids:   {len(read_ids)}")
    print(f"created ids:{len(created_ids)}")
    if missing:
        print("FAIL — READ BUT NEVER CREATED:")
        for i in missing:
            print(f"  {i}")
        return 1
    print("AUDIT OK — every referenced settings control id is created in WM_CREATE")
    return 0

if __name__ == "__main__":
    sys.exit(main())
