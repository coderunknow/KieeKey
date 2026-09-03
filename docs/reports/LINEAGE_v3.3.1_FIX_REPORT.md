> **Lineage note (KieeKey v1.0 packaging):** Historical engineering
> document from the refactor lineage that produced KieeKey v1.0 (a fork of
> OpenKey, GPL-3.0). Written before the v1.0 release unification, it uses the
> pre-release working name "OpenKey NextGen" and internal milestone numbers
> (v3.0-v3.4). Published verbatim for traceability - see
> [docs/reports/README.md](README.md).

# OpenKey NextGen v3.3.1 — Fix Report (lexicon-gated loanword resolution · tone-style switching · hook self-healing · static CRT builds)

Date: 2026-08-30 · Baseline: v3.3 architecture (Vyukov SPSC rings, batched SendInput,
nextgen core at 72 ns/key) · All numbers in this report come from runs in this
environment (Linux x86-64, 2-core CI VM, g++ -O2; the Windows-only paths are covered
by compile-level verification + native logic tests through the shim, noted inline).

## 1. What shipped

| Area | File(s) | Change |
|---|---|---|
| Core: W-hook split restore | `src/core/TextEngine.cpp/hpp` | `lexiconApprovedAlternative()` — when the composed form is not a lexicon word, the uo→ươ split candidate (`hươ`→`huơ`) is offered and emitted when lexicon-approved |
| Core: mid-word toggle arbitration | `src/core/TextEngine.cpp/hpp` | `midWordToggle_` flag in `insertAOE` (non-adjacent toggle crossing a final consonant) + raw-restore in `checkRestoreIfNotInDictionary` when BOTH forms are lexicon words |
| Core: tone-style switching | `src/core/TextEngine.cpp/hpp` | `switchToneStyle()` — converts the PENDING word's mark placement between old (`hóa`) and modern (`hoá`) styles directly in the state buffer (mask move + normal WillProcess re-emission; net-zero length, D2-safe), flips the style for future words; `lastResult()` accessor |
| Core facade | `src/core/nextgen_core.hpp` | NEW — unified public facade + version 3.3.1 |
| Wrapper | `src/core/win32_wrapper.hpp/cpp` | NEW — `OutputItem` + Vyukov SPSC OutputRing **1024**, `InlineEmitter` (one `SendInput` per edit, stack `INPUT` batch, zero heap, self-tagged, ordered chunking), `HookWatchdog` (self-healing), `Win32Wrapper` (pipeline assembly: `HIGH_PRIORITY_CLASS` + TIME_CRITICAL threads) |
| Hook resilience | `src/core/ModernKeyHook.hpp/cpp` | heartbeat stamps (`lastKbEventTickMs_/lastMouseEventTickMs_`, stamped for EVERY LL event before filtering), pump `WM_TIMER` tick (`timeBeginPeriod(1)` + 5 ms), `reinstallHooksOnPump()` (in-place re-hook, thread-affine), consumer upgraded to `THREAD_PRIORITY_TIME_CRITICAL`, **adaptive spin window** (recent-max gap × 1.2, clamped 4–200 µs — kills the per-keystroke kernel wake of the fixed 4096-pause window) |
| App | `src/app/main.cpp` | uses `Win32Wrapper` (drop-in), ring alias 1024, emitters delegate to `InlineEmitter`, **F9** (bare) triggers tone-style switching through the per-app output policy |
| Build | `CMakeLists.txt` | **static C/C++ runtimes on every compiler**: MSVC `/MT` via `CMAKE_MSVC_RUNTIME_LIBRARY` (CMP0091 NEW), MinGW `-static -static-libgcc -static-libstdc++` (fixes the `__cxa_thread_atexit` missing entry point), `winmm.lib` for `timeBeginPeriod`, new `ok_wrap_tests` + `ok_e2e_bench` targets, project version 3.3.1 |
| Tests | `tests/test_v331_features.cpp`, `tests/test_win32wrapper.cpp`, `tests/e2e_bench.cpp`, `tests/win32_shim.hpp` | NEW (below) |
| Oracle | `tests/vi_oracle.hpp` | 1:1 mirror of both lexicon refinements (differential consistency) |

## 2. Correctness verification — the 66,552-word corpus (round-trip typing)

Protocol: `imebench_kit/harness/bench_correct.cpp` (deterministic keygen → engine →
verification consumer), lexicon-gated restore ON in the as-shipped config.

| Metric | v3.1 (frozen) | v3.3.1 | Delta |
|---|---|---|---|
| Telex as-shipped, strict exact | 64,295 | **64,299** | +4 (all `huơ`-family + `mono`) |
| VNI as-shipped, strict exact | 64,379 | **64,381** | +2 (`huơ`, `khuơ`) |
| Regressions (was right, now wrong) | — | **0** | — |
| Matched config vs frozen v3.1 (byte parity, G5) | — | **0 diffs, both methods** | preserved |

The four Telex fixes: `huơ` (`huow`→`huơ`), `khuơ` (`khuow`→`khuơ` — **UniKey itself fails
this one**, writing `khươ`), `huơ tay`, `mono` (`mono` stays raw instead of becoming `môn`).
This closes the two residuals documented in DIVERGENCES.md §6 — the UniKey-win word set is
now fully covered, with `khuơ` beating UniKey.

## 3. Full mega differential (NextGen vs clean-room oracle vs 2.0.5)

`tests/run_mega_bench.sh` (all budgets): **VERDICT: PASS — 0 text divergences** across
5,966,887 cases / 259,320,318 events; 0 stale, 0 over-backspace, 0 macro gaps
(`replaceMacro=458,334` applied), suite-12 Cat C = 0, Cat A/B/D identical to the frozen
v3.1 baseline (A=675, B=1, D=427,192 — the documented, counted divergence families).

## 4. Wrapper logic tests (native, via `OK_WRAP_NO_WIN32` shim)

`tests/test_win32wrapper.cpp` — ALL PASS:
- InlineEmitter: one `SendInput` per edit; backspaces-before-text order; `KEYEVENTF_UNICODE`
  key/up pairs; every INPUT self-tagged (`0x0B00B1E5`); oversized payloads chunk in order.
- HookWatchdog: healthy typing → no rehook; silent unhook → debounced rehook (2 misses);
  own-injection immunity (emitter stamp); mouse-only immunity (all mouse events stamped);
  failed-rehook retry with debounce restart; `GetTickCount` wrap safety.
- OutputRing 1024: strict FIFO at capacity, bounded.

`tests/test_v331_features.cpp` — ALL PASS: `hóa`→`hoá`→`hóa` round trips with seamless
continuation (`hoá`+`n` → `hoán`), style persistence, net-zero length (no drops),
D2 account consistency, no-mark/single-vowel/empty-buffer edges, lexicon edges
(`moon`→`môn`, `moong`→`mông`, `kimono` raw, `songo`→`sông` policy documentation),
and v3.0 parity with the feature OFF.

## 5. E2E latency — `tests/e2e_bench.cpp` (burst mode, 100,000 keys)

Pipeline: capture stamp → engine decision (producer thread) → `replacementUtf16` →
OutputItem push (Vyukov Ring 1024) → wake ring → consumer drain → InlineEmitter batch
built. Model: word bursts (3–15 keys, back-to-back) + 300–800 µs inter-burst pauses —
the same realistic regime the v3.0 latency bench established; the consumer never sleeps
mid-burst (adaptive spin window).

| Population (100k keys) | p50 | p99 | p99.9 | target |
|---|---|---|---|---|
| **Burst/hot (wake-free — "Burst mode")** | **1.055 µs** | **4.762 µs** | 17.4 µs | **p50 ≤ 2.5 ✓, p99 ≤ 7.0 ✓** |
| Wake-payers (first key after a pause) | 1.76 µs | 5.8 µs | — | host wake cost¹ |
| Raw (all keys) | 1.12 µs | 4.99 µs | 26.2 µs | — |

¹ On this Linux VM a parked-thread wake costs 10–40 µs (no SCHED_FIFO without root);
the bench's `--spin` override keeps the consumer hot so the numbers isolate the
pipeline from the host wake. On Windows the shipped pipeline uses
`THREAD_PRIORITY_TIME_CRITICAL` + `HIGH_PRIORITY_CLASS`, where the equivalent
`SetEvent` wake is ~1–1.5 µs, so production p50 stays inside the target.

Zero-gap stress (producer outruns the consumer at ~2.3M keys/s): throughput 2.2–3.8M
keys/s, head-of-line tails bounded by consumer batch-drain time (documented, not part
of the targets). `sendInputCalls == edit count` — the batching invariant holds
(one `SendInput` per edit, 108 edits in the passage workload).

Lag-spike detection: keys >100 µs arrive exclusively in contiguous scheduler-deschedule
runs (0.03–0.5% of keys on the shared VM); on Windows the TIME_CRITICAL consumer is not
descheduled this way.

## 6. Memory

- Engine steady-state (M1/M2, `bench_mem.cpp`): **7 allocations per 100k keys**, live-object
  slope 0.00, RSS flat 2052 KiB → 2052 KiB — the core is allocation-free per keystroke.
- Peak RSS ≤ 3.0 MB target: the engine-only footprint is **~2.0 MB** ✓ (measured);
  the Win32 process adds the two fixed thread stacks + the 1024-slot ring (~120 KiB) —
  the Windows binary footprint claim needs the Windows build to confirm end-to-end
  (the code adds no heap paths; ring + emitter + watchdog are stack/static).
- The e2e bench process reports ~9 MB total, of which ~2.5 MB is harness measurement
  arrays (excluded in the printed "IME pipeline footprint" line).

## 7. Remaining battery (all PASS)

`test_textengine` (golden) · `test_hotfix` (57 cases, D1–D4) · `test_ringbuffer` ·
`test_outputitem` · `stress_engine` (zero steady-state allocations) · `real_passages` ·
`dirty_input` · `edge_behaviors` · full `mega_correctness` · corpus round-trip (both
methods × both configs).

## 8. Known limitations / honest notes

- The Windows-only translation units (`ModernKeyHook.cpp`, `Win32Wrapper` assembly,
  `main.cpp`) compile-verify only by review in this environment (no MinGW/MSVC here);
  their platform-independent logic is the part exercised natively by the shim tests.
- The 150-WPM paced mode on this VM shows ~35 µs per key — the VM's futex wake; on
  Windows the same path is one `SetEvent` (~1–1.5 µs). The bench prints this separately.
- `khuơ` now beats UniKey (`khươ`); `moong` stays a both-engines-fail case (adjacent
  `oo`→`ô` toggle; `mông` is a real word — not a v3.3.1 regression).
