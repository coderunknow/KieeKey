# KieeKey v1.3.0-beta4 — Benchmark Report

Apples-to-apples, non-regression benchmark of the beta4 candidate against the
previous release tag `v1.3.0-beta3` (`07b9475`). Every number below was produced
by benchmark binaries built from each tree, run **interleaved** so
thermal/frequency/scheduler drift hits both sides equally, and reduced to
medians. A first 5-round attempt produced what looked like +9–26 % tail
"regressions" in two subsystems; more rounds plus a byte-identical control leg
(shown below) proved those were host-interference artifacts — the analysis of
*why* is included, because a wrong "no regression" conclusion is worse than
none.

## What changed in beta4 that could affect measured paths

| Change | Hot path? | Why |
|---|---|---|
| `VnComposer::feedBackspace()` rewrite | **No** — game windows only | Not on the IME keystroke path |
| `ArcadeManager` `m_gameMtx` (race fix) | Game calls only | IME hot path checks `isConsumingKeyboard()` — one atomic load, unchanged |
| ChaosEngine 116-pair VN tone-mirror tables | Only when glyph flips are **ON** (default **OFF**) | Bigger switch in `getFlipped*Glyph`; measured directly below |
| Diagnostics recorders (hook/engine/consumer) | One relaxed atomic load + branch when **Off** | `atLeast()` gate hoisted per event; all recording inside `if (diagOn)` — verified in source; Windows-only call sites |
| Settings `WM_CREATE` layout solver, tab re-fit | **No** — UI thread, dialog creation | Never runs per keystroke |
| 75 % intensity, VN prompts, Rhythm arrows | **No** — game content | Configuration-time only |

## Environment

* Host: Linux sandbox (shared KVM), 2 vCPU, `g++ (Debian 12.2.0) 12.2`.
* Latency legs pinned with `taskset -c 0` (single core, no migration).
* Deterministic corpora only (no RNG inside measured loops); identical
  benchmark sources on both sides — harness files were copied into the base
  tree so the A/B isolates the **engine**.
* Sides: `base` = clean checkout of tag `v1.3.0-beta3`; `cur` = beta4 tree.

## Result 1 — Standardized suite (`tests/run_bench_suite.sh --ab-base`)

Correctness gate (differential oracle, 2 059 419 events over viet74k + macros
+ tone pops): **VERDICT PASS on both sides, 0 mismatches** — identical totals
(`cases=137878 events=2059419 mismatch=0 stale=0 expectDrift=0 macroGap=0`).

Micro decision latency, median of 3 interleaved runs × 2 M keys per workload
(nanoseconds / key, p50 of `T1-decision`):

| Workload | base (beta3) | cur (beta4) | Δ |
|---|---|---|---|
| vn-compose | 63 | 63 | 0 |
| mixed | 63 | 63 | 0 |
| delete | 57 | 57 | 0 |
| passthrough | 53 | 54 | +1 ns |
| T0-control (harness floor) | mean 33.5 / p50 32 | mean 33.5 / p50 32 | 0 |

Integration (e2e shim pipeline) and tone-population legs moved a few percent
(e2e pipeline p50 34.93→36.79 µs; tone pops ≤ ±3 %, `mixed` 8.42→8.29 µs on
the spin path). These legs compile **only** `TextEngine.cpp` +
`win32_wrapper.cpp`, which are byte-identical between the two trees (verified:
`git diff` empty for both files) — so by construction those deltas measure
host noise, not code. This is exactly why the suite interleaves and takes
medians.

## Result 2 — Feature isolation A/B (`bench_v130_isolation`)

Both sides compiled with the same flags (`-std=c++2b -O3 -DNDEBUG -pthread`;
sources: `TextEngine Arcade ArcadeFrame ArcadeRender ChaosEngine AiRival
Progression TypingAnalytics` — the beta4-changed Arcade/ChaosEngine included).
**15 alternating pairs** (order swapped each pair to cancel ordering bias),
300 000 keys per round, medians over the 15 rounds:

| Configuration | base p50 | cur p50 | Δ p50 | Δ mean | Δ p99 |
|---|---|---|---|---|---|
| 1. Pure IME baseline | 64 | 64 | **0.0 %** | −0.1 % | +0.7 % |
| 2. + Inactive Arcade (standby) | 66 | 66 | **0.0 %** | +0.4 % | +0.7 % |
| 3. + Active Chaos Engine | 85 | 84 | **−1.2 %** | −0.8 % | −1.9 % |
| 4. + Active AI telemetry | 75 | 76 | +1.3 % | +0.6 % | +0.7 % |
| 5. + Full v1.3.0 suite | 454 | 454 | **0.0 %** | +0.0 % | −2.2 % |

**Noise control:** configuration 1 executes byte-identical code on both sides,
so its deltas (−0.1 %…+0.7 %) are the measured noise floor of this host.
Every changed subsystem lands inside that band. Pairwise per-round mean deltas
(cur−base within the same round): medians −0.8 %…+0.7 % across all five
configurations.

**Determinism:** the 64-bit FNV sink digest is **identical between base and
cur for all five configurations** (bit-level behavioral equivalence on this
corpus).

**The false alarm, dissected:** a first 5-round pass showed cfg-3 mean +9.1 %,
p90 +25.9 %. Per-round inspection found cur's round 2 ran with the *core-only*
config inflated to 106.5 ns mean (vs 70–72 in its clean rounds) — a
whole-round host-interference event — and 2 of cur's 5 rounds were noisy vs 1
of base's. The same interference produced the only verdict-FAIL of the
campaign (`Inactive Arcade overhead 17 %` in that round; all other 29 rounds
PASS). With 15 alternating pairs the medians converge to the table above.
This is why single-shot or unpaired comparisons are not reported as evidence.

## Result 3 — Glyph-flip micro-probe (the one genuinely new hot-path code)

`ChaosEngine::getFlippedVerticalGlyph` + `getFlippedHorizontalGlyph` over a
33 152-char corpus containing every Vietnamese vowel in every tone (both
cases) + ASCII mix, median of 15 reps, `-O2`, pinned:

| Side | ns / char (both lookups) | chars mapped V / H |
|---|---|---|
| base (beta3, VN passes through) | 3.08 | 4 096 / 1 024 |
| cur (beta4, tone-mirror tables) | **4.84** | 15 360 / 12 288 |

Reading: with flips **enabled**, Vietnamese text now actually transforms
(mapping counts prove the tables engage) at ~**+1.8 ns/char** versus beta3's
pass-through. With flips disabled (the default) the functions are not on the
output path at all — Result 2's active-Chaos configuration shows no delta.
Per keystroke this is ~2 % of the 70 ns core decision, on an opt-in feature.

## Result 4 — Correctness & regression gates

* `tests/run_all_tests.sh --jobs=2`: **ALL NATIVE TESTS PASSED** — 41 run
  targets including the new `test_arcade_recovery` (43 checks: desync repro,
  300-seed recovery fuzz, WasdRace Backspace both wire shapes, Fishing VN,
  No-Mistake word-strict, Rhythm arrows/glyphs, two-thread manager hammer),
  `test_chaos` with `testVietnameseGlyphFlips`, plus the full pre-existing
  suite (soak, stress, option matrix, lifecycle, ASan hotfix leg, …).
* Source-contract gates: input isolation **ok**, version consistency **ok**
  (1.3.0-beta4 / PE 1.3.0.5), SHA256SUMS manifest **ok**, layout audit
  `--strict` **ok**, controls audit **ok**.

## Defects found in the benchmark tooling itself (and fixed)

1. `tests/run_extreme_bench.sh` Step 7 could not link since
   `ArcadeFrame.cpp`/`ArcadeRender.cpp` were split out of `Arcade.cpp` —
   **144 undefined references on the unmodified beta3 tree** (pre-existing,
   not a beta4 regression). Fixed: the translation-unit list and `-pthread`
   now match the suite's Arcade link lines.
2. `scripts/check_input_isolation.py` false-positived on the new Vietnamese
   settings hint: a literal `");"` inside the string truncated the
   non-greedy `mkCtl()` match, misreporting a named, tab-registered control
   as anonymous. Fixed with comment/literal-aware scanning; the gate was
   proven to still catch real violations (deregistered id; anonymous
   control; anonymous control adjacent to a stray in-comment quote) before
   the fix was accepted.

## Honest limits

* The Windows-only surfaces (the `WM_CREATE` solver block, diagnostics UI
  handlers, `.rc`/`.manifest`) are **compile-verified** by the full-TU Zig
  cross-build (`scripts/build_windows_exe.sh`, all 18 TUs + resources +
  PE32+ link; it caught three Windows-only compile errors in the new
  diagnostics code before tagging). Runtime behavior on real Windows is
  still CI/manual acceptance territory.
* The diagnostics recorders' Off-state cost (one relaxed load + branch) is
  verified by source inspection and the `ok::diag` unit tests; the Windows
  hook thread itself is not measurable on this host.
* Raw artifacts live in `bench_runs/` (regenerable, not committed, per the
  `docs/reports/README.md` policy).
