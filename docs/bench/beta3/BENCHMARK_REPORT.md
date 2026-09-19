# KieeKey v1.3.0-beta3 — Benchmark Report

Apples-to-apples, non-regression benchmark of the beta3 candidate against the
branch-point baseline (`1e82503`, the merge of PR #22). Every number below was
produced by the **same** benchmark binary built from each tree, run **interleaved**
so thermal/frequency drift hits both sides equally.

## What changed in beta3 that could affect the hot path

| Change | Hot path? | Why |
|---|---|---|
| `planOutput()` refactor (bug #3, live external effects) | Only when live effects are **ON** (default **OFF**) | Unified, allocation-free output decision in `LiveEffects.hpp` |
| `VnComposer` (bug #2, typing games) | **No** — game windows only | Not on the IME keystroke path at all |
| `HookCounters` (bug #5) | Yes — but a few relaxed atomics | Replaces the mislabeled beta2 counter |
| `visibleAccount_` reset (D2 over-erase fix) | Yes — one store on context change | Correctness fix, not per-key work |
| Chaos Lab font / `DialogLayout` solver (bug #1) | **No** — UI thread, window creation | Never runs per keystroke |

The core IME decision path is therefore expected to be **unchanged**; this report
proves it rather than asserting it.

## Environment

* Host: Linux sandbox, 2 vCPU, ~3.6 GB RAM, `g++ -std=c++2b -O2`.
* Each run pinned with `taskset -c 0` (single core, no migration).
* Corpus: the benchmark's built-in **deterministic** keystroke stream (no RNG in the
  measured loop), so both sides process byte-identical input.
* Harness: `tests/bench_v130_isolation.cpp` ("v1.3.0 Feature Isolation &
  Apples-to-Apples Hot-Path Benchmark"), 300 000 keys per round for the A/B, 1 000 000
  keys for the isolation table.

## Methodology — paired interleaved A/B

`baseline, candidate, baseline, candidate, …` × 5 rounds, same binary flags, same
core, back-to-back. Reported value is the **median** of the 5 rounds per side
(median rejects the occasional scheduler spike). Per-key latency is a
`steady_clock` delta around the full `process()` + feature pipeline.

## Result 1 — Core IME latency: NO regression

Median of 5 interleaved rounds, 300 000 keys each (nanoseconds / key):

| Metric | Baseline `1e82503` | Candidate beta3 | Δ |
|---|---|---|---|
| **p50** | **55 ns** | **55 ns** | **+0.0 %** |
| p99 | 100 ns | 97 ns | −3.0 % |
| mean | 58.9 ns | 58.2 ns | −1.2 % |

Raw per-round p50 was `55` on **every** round for **both** sides (one baseline round
showed 56); mean stayed in 57.4–59.9 ns and p99 in 97–103 ns. The spread is a few
nanoseconds — i.e. the measurement is **consistent**, so no noise investigation was
warranted. Candidate is equal-or-marginally-better on every percentile.

**Conclusion: beta3 adds zero measurable per-key latency to the core IME path.**

## Result 2 — Feature isolation (candidate, 1 000 000 keys)

Cost of each v1.3.0 subsystem layered onto the core, measured in one process:

| Configuration | Mean (ns) | p50 (ns) | p90 (ns) | p99 (ns) | Δ vs core |
|---|---|---|---|---|---|
| 1. Pure IME baseline (core only) | 58.9 | 55 | 69 | 102 | BASELINE |
| 2. + Inactive Arcade (standby) | 60.0 | 57 | 70 | 108 | +3.6 % |
| 3. + Active Chaos Engine | 90.1 | 88 | 105 | 136 | +60.0 % |
| 4. + Active AI telemetry | 69.2 | 65 | 78 | 123 | +18.2 % |
| 5. + Full v1.3.0 suite | 503.5 | 484 | 516 | 680 | +780.0 % |

Reading: an idle Arcade hub costs ~2 ns/key (+3.6 %). The Chaos transform (only when
the user explicitly enables live effects) costs ~33 ns/key. The "full suite" row turns
**every** subsystem on at once — a worst case no real user runs — and still completes a
keystroke in ~0.5 µs, far below the ~1 ms budget of a human keypress.

## Result 3 — Correctness (not a claim: a gate)

Latency is meaningless if the output is wrong. The beta3 tree passes the native
correctness/regression gates (Linux, no Windows needed):

* `tests/test_vn_composer.cpp` — Telex/VNI → diacritics (incl. `nước`, `cơm`, capitals).
* `tests/test_arcade_vn.cpp` — the **real game objects** driven with the **real Telex**
  keystrokes compose the whole VN passage, finish, score 100 %, and EN mode still 1:1.
* `tests/test_live_output_plan.cpp` — shipped `planOutput()` decision + 40 k fuzz.
* `tests/test_dialog_layout.cpp` — layout solver over the real authored rectangles.
* `tests/test_hook_counters.cpp`, `tests/test_diagnostics.cpp`, plus the existing
  differential / option-matrix / real-passage / gate-correctness suite.

## Reproducing on a real Windows machine

The harness is portable C++. On Windows (MSVC or MinGW-w64), from a developer shell:

```
:: build the isolation benchmark (x64)
cl /O2 /EHsc /std:c++latest /I src\core /I tests ^
   tests\bench_v130_isolation.cpp src\core\Arcade.cpp src\core\ArcadeFrame.cpp ^
   src\core\ArcadeRender.cpp src\core\ChaosEngine.cpp src\core\AiRival.cpp ^
   src\core\Progression.cpp src\core\TypingAnalytics.cpp src\core\TextEngine.cpp ^
   /Fe:bench_iso.exe
bench_iso.exe --keys=1000000
```

For the full layered suite (correctness gate → micro → integration → tone), run
`tests/run_bench_suite.sh` under Git-Bash/MSYS2. To A/B against the baseline, check
out `1e82503` in a second worktree, build the same binary there, and alternate runs
exactly as above.
