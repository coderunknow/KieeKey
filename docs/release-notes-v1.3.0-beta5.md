# KieeKey v1.3.0-beta5

All nine defect clusters from the beta4 Windows tester report fixed at the root, plus the performance-documentation and Chaos-Lab-discoverability asks (file build **1.3.0.6**). Every fix was reproduced first (failing portable probe, mechanical source read, or seeded fuzz), fixed at its root cause, then pinned by a native regression test that runs on Linux.

## Fixed (tester bug dossier B1–B9)
- **B1 — Settings dialog overlap/clip, no scroll** — three root causes: the layout solver never re-fit *page* children (long labels still collided at 125 % DPI), `scrollChildRect()` clipped children to a one-pixel sliver in thin visible bands, and tab-page 6 had no footer budget. The solver now re-fits page children with the full grow→push→stretch→grow-window cascade; pinned by new `test_dialog_layout` DPI and static-clip fixtures that fail on the beta4 layout.
- **B2 — Live effects "never reach external apps"** — the full-chain portable test proves the hook→ring→emitter path *does* deliver with the gate on. What was broken was the diagnosis: the status tab reported the process-level gate as if it were the live-effects gate and had no model of what could block it. New pure `GateBlocker` model enumerates every blocker (master off, app excluded, session paused, …) with one human sentence each, re-evaluated on a timer — a silently-blocked gate now says *why*. UI-path only; zero hot-path cost (benchmark-verified).
- **B3 — "Keyboard events processed" climbed on mouse drags** — the readout displayed the *ring* counter (keyboard + mouse + foreground events) under a keyboard-only label. It now uses `HookCounters::keyboardEvents()` exclusively; a test pins that mouse-drag sequences leave it untouched.
- **B4 — Diagnostics tab mostly zeros / "Ứng dụng hiện tại: unknown"** — two generations of diagnostics confused: legacy rows read fields the current `ProcessMonitor` never populated. `ProcessMonitor` now resolves the real foreground process name (new `ProcessNameUtil`, query-full-data growth retry, locale-independent fallbacks) and feeds live values to every telemetry row; `audit_telemetry_rows.py` + `test_process_monitor` pin each row to a producer that actually writes it.
- **B5 — Typing Race / Fishing: backspace after a wrong word "doesn't work"** — the divergent composed tail was invisible, so backspace repaired state the player couldn't see. Per the tester's chosen design, the tail is now rendered **in red** with the hint "Backspace để sửa" (no auto-rewind); the composed line is added only when actually diverged.
- **B6 — No-Mistake: wrong word sometimes didn't end the run; WPM/accuracy stale** — the fail check ran before the word-final commit in some key orders and stats only updated on clean completion. Found via seeded wrong-word fuzz; the run now ends deterministically on the divergent word and stats update on every word.
- **B7 — WasdRace steering in Vietnamese mode** — new per-game "Phím lái" option: **Mũi tên** (default) / **WASD** / **Cả hai**. In WASD-involving modes the keys steer *and* still feed the composer; `W` always steers in VN mode but is never swallowed from the IME; persists and applies live; EN mode unchanged. All three modes × both languages pinned by tests.
- **B8 — Chaos Lab not discoverable** — the hub now paints a dedicated 🧪 **Chaos Lab** row below the game catalog (clickable, can never start a game — pinned by `test_arcade_window`), README gained a 3-spot tour, and a failed lab open surfaces a message box with the Win32 error code instead of failing silently.
- **B9 — "Bàn phím" tab: many options did nothing** — a systematic 3-layer audit (control wiring → engine effect → live-apply) over all 37 interactive controls found the dead ones: missing `WM_COMMAND` live-apply cases and effects not consumed. All fixed, and the audit is now a **CI gate** (`audit_settings_wiring.py` runs inside the test suite): any control that loses a layer again fails the build.

## Added
- `tests/test_arcade_beta5.cpp` — 66 checks (B5 red tail, B6 fail/stats determinism + fuzz, B7 all steering modes, live-apply persistence).
- `tests/test_settings_wiring.cpp` — 31 checks; `scripts/audit_settings_wiring.py` (37 controls × 6 layers).
- `docs/PERFORMANCE.md` (**G1**) — per-feature hot-path costs measured in the beta4/beta5 campaigns; the settings live-effect hint now names the cost of what you enable.
- `docs/bench/beta5/BENCHMARK_REPORT.md` (**G4**) — the standardized A/B campaign below.

## Benchmarks — no regression (paired, interleaved, vs tag v1.3.0-beta4)
- Correctness gate (differential oracle): **PASS on both sides**, identical totals — 2 059 419 events, 0 mismatches.
- Engine decision p50 (2 M keys × 3 interleaved runs): vn-compose 56=56 ns, mixed 54=54, delete 45=45, passthrough 43=43 — **0.0 % on every workload** (engine sources unchanged since beta4; the audit table in the report shows exactly what moved and why none of it is per-key).
- Feature isolation, 15 alternating 300 k-key rounds, medians: all five configurations inside the byte-identical control leg's noise band; **sink digests identical in every configuration, every round**.
- Glyph flips: `ChaosEngine` byte-identical to beta4 → beta4's measurement stands (+1.8 ns/char when ON, off the output path when OFF/default).
- One honestly-flagged leg: the environment-dependent e2e shim pipeline fails its *absolute* burst gate on this shared host on **both** sides; pooled medians over 9 alternating runs show cur +2…+5 % with no changed code on that path (full attribution and raw numbers in the report; a re-check on quiet hardware is owed).

## Release verification
- `dist/KieeKeyApp.exe` rebuilt from this tree (Zig cross-build, PE **1.3.0.6**, UTF-16 version probe verified).
- `tests/run_all_tests.sh --jobs=2`: **ALL NATIVE TESTS PASSED** (46 run targets + 9 source-contract checks). Input-isolation, version-consistency, SHA256SUMS, layout/controls/telemetry/settings-wiring audits: all OK.
- Version bumped in every carrier: About box, `.rc` (1,3,0,6), manifest, core `VERSION_STRING`, `check_version.py` (channel beta5, build 6), README EN/VN.

## Verification owed on Windows (honest residuals)
- B1's solver and B8's hub row are proven by portable tests against the GDI recording stub; the *pixel* result at exotic DPI/font combinations can ultimately only be confirmed on a real Windows session.
- B4's process-name resolution uses the documented Win32 growth-retry pattern and is logic-tested portably; the actual QueryFullProcessImageName behaviour on the tester's machine is the final judge.
- The e2e burst-gate re-check above.

## Downloads
Use the CI-built archives below (**KieeKey-x64.zip**, ARM64, ARM64EC) — or the quick-test `dist/KieeKeyApp.exe` from the repository at this tag.

**Full Changelog**: https://github.com/coderunknow/KieeKey/compare/v1.3.0-beta4...v1.3.0-beta5
