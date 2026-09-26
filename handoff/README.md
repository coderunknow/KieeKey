# handoff/ — Phase-0 field package for v1.3.0-beta8fix3 (BS-24)

Local hand-off artefacts for the user's Windows 10 LTSC machine. These are
NOT release assets (the release probe is built by MSVC CI); they are the
Phase-0 measurement package described in
`docs/PLAN_BS24_v1.3.0-beta8fix3.md`.

| file | purpose |
|---|---|
| `kieekey_ui_probe.exe` | cross-built probe (zig, exact beta8fix2 tree) — see `PROBE_SHA256.txt` for provenance |
| `kieekey_ui_probe.pdb` | symbols for the probe (crash diagnostics) |
| `run_probe_and_report.bat` | one-click: facts + probe + zip |
| `collect_report.ps1` | the collector (ASCII on purpose — PS 5.1 code pages) |
| `HUONG_DAN_thu_thap_bang_chung.txt` | 1-page guide (Vietnamese) |
| `BUOC_CHAY_PROBE_chi_tiet.txt` | step-by-step run guide (Vietnamese) |
| `BIEN_BAN_HIENTRUONG_BS24.txt` | 1-page field record template (Vietnamese) |
| `PROBE_SHA256.txt` | provenance + SHA-256 of the probe exe |
| `build_probe.sh` | reproducible rebuild of the probe from the current tree |

`*.exe` / `*.pdb` are git-ignored (repo convention: binaries are not
committed); the rest of this folder is tracked as measurement tooling.
