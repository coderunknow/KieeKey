> **Lineage note (KieeKey v1.0 packaging):** Historical engineering
> document from the refactor lineage that produced KieeKey v1.0 (a fork of
> OpenKey, GPL-3.0). Written before the v1.0 release unification, it uses the
> pre-release working name "OpenKey NextGen" and internal milestone numbers
> (v3.0-v3.4). Published verbatim for traceability - see
> [docs/reports/README.md](README.md).

# OpenKey NextGen v3.4 — Real-Windows Verification Protocol (Phase 4)

Purpose: a human on the target Windows machine can execute everything this
sandbox could not — real SendInput costs, real TSF sessions in Word/Chrome,
real hook timing, and the subjective typing feel. Every step says what to run
and what output to expect. Report which steps were executed and paste the
outputs into the release notes.

**Sandbox-verified in this release** (Linux, 2 vCPU, shim + MinGW cross):
ctest 3/3, full mega differential battery 0 mismatches, e2e + tone bench
before/after numbers (LATENCY_AUDIT_REPORT.md), import-table verification of
every bin/ exe (system DLLs only), PE32+ GUI subsystem.
**Requires real hardware**: §3 (E2E/tone numbers on Windows), §4 (manual feel
test), §5 (TSF in Word/Chrome), §6 (ETW).

---

## 1. Install & smoke

1. Unzip `OpenKey_NextGen_v3.4_Latency.zip`, keep the folder anywhere on disk.
2. Run `bin\OpenKeyApp.exe`. Expect: green tray icon, welcome balloon.
   `bin\OpenKeyApp.exe` → Properties → Details: File version 3.4.0.0.
3. Right-click tray icon → Phương thức gõ → Telex. Double-click → Cài đặt →
   Chẩn đoán tab shows live telemetry (keys, hook→consumer latency).

## 2. Correctness battery on Windows (5 minutes)

From a terminal in `bin\`:

```
OpenKeyApp-Tests-Core.exe        & echo CORE PASS
OpenKeyApp-Tests-Ring.exe        & echo RING PASS
OpenKeyApp-Tests-Wrapper.exe     & echo WRAPPER PASS
```

Expected: each prints its suite and ends with ALL … TESTS PASSED, exit code 0.
The wrapper suite includes the v3.4 EditDrainBarrier tests
(pending==0 fast exit, spin-window catch, notify-before-wait not lost,
**1 ms budget bound**, 200-cycle consistency).

Full differential battery (optional, needs the source tree + a toolchain):

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure        # 3/3 must pass
bash tests/run_mega_bench.sh                      # VERDICT: PASS (0 text divergences)
```

## 3. Latency numbers on real Windows

```
OpenKeyApp-Bench-Engine.exe
OpenKeyApp-Bench-E2E.exe --keys=100000
OpenKeyApp-Bench-Tone.exe --barrier=hybrid --path=inline --iters=2000
OpenKeyApp-Bench-Tone.exe --barrier=hybrid --path=tsf --commit=100us --iters=2000
OpenKeyApp-Bench-Tone.exe --barrier=hybrid --path=tsf --commit=1ms --iters=2000
```

Acceptance (real hardware, quiet host):

- Engine bench: every workload ≤ ~150 ns/key.
- E2E burst (wake-free population): **p50 ≤ 2.5 µs, p99 ≤ 7.0 µs** (frozen
  quiet-host baseline: 1.055 / 4.762 µs).
- Tone bench inline: E2E p50 ≤ 3 µs for every population (this includes the
  REAL SendInput call — the number the sandbox could not produce).
- Tone bench tsf commit=100us: mark-key E2E p50 ≈ 100–115 µs and p99 ≤ ~1.3×
  the commit cost (no amplification).
- Tone bench tsf commit=1ms: mark-key E2E p50 ≈ 1.00–1.01 ms, p99 ≤ ~1.05 ms;
  PASS delivery p99 ≤ ~1.1 ms. If p99 exceeds ~1.2× the commit cost, re-run
  with `--barrier=spin` to demonstrate the old amplification, then file a bug.

For a before/after A/B on the same machine, re-run the three tone commands
with `--barrier=spin` — expect the commit=1 ms p99 to inflate 10–25× (the
v3.3.1 behavior this release fixes).

## 4. Manual typing feel test (the user-facing sign-off)

1. **Notepad (inline path)**: type at speed — a dense-mark passage at ~150
   WPM, e.g.:
   ```
   hôm nay trời đẹp, chúng tôi đi dạo quanh hồ Tây. rồi ngồi uống cà phê và
   nói chuyện về dự án mới. tiếng Việt có nhiều dấu thanh: sắc, huyền, hỏi,
   ngã, nặng. người dùng gõ nhanh thì độ trễ phải nhỏ hơn hai phần trăm micro giây.
   ```
   Type the RAW Telex (e.g. `hoofm nayf trooir ddepf, chungs tooi di ddaoj
   quanh hoof Taay. ...`). Expected: no flash of raw ASCII, no ghost/double
   letters, no lost marks, backspace storms clean. Repeat after deleting a
   whole word and retyping.
2. **Word / WordPad (TSF path)**: same passage. Expected: no flicker, no
   synthetic backspaces, marks land instantly. With the Auto policy, Word
   uses TSF — the commit should feel app-bound (Word is the 100 µs–1 ms
   class), not laggy at the mark keys.
3. **Chrome (TSF path)**: type in the address bar and a Google Doc. Expected:
   flicker-free composition, `ddawjng` → đặng, `dduowcj` → được.
4. **Speed ladder**: hold a key rhythm at ~250 WPM for 10 s in Notepad — no
   dropped characters (ring 4096 ≫ burst), no stuck modifiers.
5. **Ctrl+Shift toggle** and **F9 tone-style switch** (hoá ↔ hóa) still
   behave; shortcuts like Ctrl+Shift+S must NOT toggle the IME.

## 5. Real TSF session cost (the app-bound term)

The sandbox simulates TSF with commit costs; on real hardware, measure the
real thing:

1. Build the instrumented app (once, with MSVC or MinGW):
   `cmake -S . -B build-prof -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENKEY_PROFILE=1`
   → copy `OpenKeyApp.exe` out of `build-prof`.
2. Run it with profiling enabled while typing the §4 passages in Word and
   Chrome:
   ```
   set OPENKEY_PROFILE=1
   set OPENKEY_PROFILE_DUMP=%TEMP%\ok_profile_hook.csv
   OpenKeyApp-prof.exe
   ```
   (profiling builds stamp t0..t5 per key; the CSV lands in %TEMP%).
3. Interpret: for the TSF path, `t5_ns - t0_ns` per edit = the REAL
   capture→commit-returned time in that app; `t5_ns - t4_ns` ≈ the session
   cost (Word: expect 100 µs–1 ms; Chrome: tens of µs). Compare the p50 with
   the acceptance targets in §3. `t2_ns - t1_ns` on inline keys ≈ the real
   in-callback SendInput cost (the S2 number).

## 6. ETW / xperf + WPA recipe (system-level view)

1. Admin terminal:
   ```
   wpr -start GeneralProfile -start CPU -filemode
   ```
2. Type the §4 passage for ~20 s in Notepad, then in Word.
3. Stop and trace: `wpr -stop C:\temp\ok_typing.etl`
4. Open in Windows Performance Analyzer; load symbols; profiles of interest:
   - **CPU Usage (Precise)**: `OpenKeyApp.exe` threads — the pump
     (TIME_CRITICAL) and consumer. Verify: no thread of ours accumulates
     > 1 ms of uninterrupted runtime in one dispatch (the S1 budget), and the
     consumer is NOT spinning while idle (parked in WaitForSingleObject).
   - **Generic Events / Input**: keyboard latency from input queue to app
     delivery, our SendInput batches (KEYEVENTF_UNICODE pairs, one call per
     edit).
   - **ReadyThread**: wake latency of the consumer after SetEvent from the
     hook thread (the WAKE-PAY population on real hardware; expect low
     single-digit µs on an idle machine).
5. Optional instrumented PowerShell SendInput typer (scripted, no human
   variance) — save as `type_telex.ps1` and run while tracing:
   ```powershell
   Add-Type -Namespace W -Name K -MemberDefinition @"
   [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] p, int s);
   public struct INPUT { public uint type; public KI ki; }
   public struct KI { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr extra; }
   "@
   $text = "hoofm nay troif ddepf chungs tooi "
   foreach ($ch in $text.ToCharArray()) {
     $down = New-Object W.INPUT; $down.type=1; $down.ki.wScan=[int][char]$ch; $down.ki.dwFlags=4
     $up   = New-Object W.INPUT; $up.type=1;   $up.ki.wScan=[int][char]$ch; $up.ki.dwFlags=4+2
     [void][W.K]::SendInput(1, $down, [System.Runtime.InteropServices.Marshal]::SizeOf($down))
     Start-Sleep -Milliseconds 80       # 150 WPM
     [void][W.K]::SendInput(1, $up,   [System.Runtime.InteropServices.Marshal]::SizeOf($up))
     Start-Sleep -Milliseconds 80
   }
   ```
   In WPA, measure per-character latency from the typer's SendInput to the
   final text appearing (Keyboard events + our t-stamps if the instrumented
   build runs).

## 7. Import-table sanity (1 minute, already done in-sandbox)

```
dumpbin /dependents bin\OpenKeyApp.exe        (MSVC)  — or —
objdump -p bin\OpenKeyApp.exe | findstr "DLL Name"
```
Expected: system DLLs only (KERNEL32, USER32, GDI32, SHELL32, OLE32,
COMCTL32, ADVAPI32, dwmapi, WINMM, msvcrt). Any libstdc++/libgcc/libwinpthread
entry = packaging failure.

## 8. Report-back checklist

- [ ] §2 three test exes: PASS outputs
- [ ] §3 five bench commands: outputs pasted (engine, e2e, tone × 3)
- [ ] §3 vs §LATENCY_AUDIT_REPORT.md §10 targets: PASS/FAIL per line
- [ ] §4 feel test: Notepad / Word / Chrome / speed ladder / hotkeys
- [ ] §5 CSV percentiles for Word + Chrome (t5−t0, t5−t4 per edit)
- [ ] §6 WPA observations (no >1 ms dispatch, idle CPU ≈ 0)
- [ ] S2 A/B (optional): OPENKEY_INLINE_MODE=deferred vs default feel + t-stamps
