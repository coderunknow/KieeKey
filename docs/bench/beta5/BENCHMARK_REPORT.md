# KieeKey v1.3.0-beta5 — no-regression benchmark campaign (G4)

Protocol identical to beta4's: **paired, interleaved, medians over many
alternating rounds, with a byte-identical noise-control leg.** Single-shot and
short-round numbers are never reported as evidence (beta4 documented a 5-round
pass inflating one configuration to +9.1 % mean before 15-round medians
converged to −1.2 %).

* Host: Linux sandbox (shared KVM), 2 vCPU, `g++ (Debian 12.2.0) 12.2`.
  The host was visibly noisier than during the beta4 campaign — quantified
  below, not hidden.
* Sides: `base` = clean worktree of tag **v1.3.0-beta4** (1f3ac16);
  `cur` = the beta5 tree. Identical flags on both sides
  (`-std=c++2b -O3 -DNDEBUG -pthread`; the standardized suite uses its own
  deterministic flags). Harness sources are unchanged since the tag
  (`git diff v1.3.0-beta4 -- tests/bench_* tests/e2e_bench.cpp
  tests/gate_correctness.cpp tests/run_bench_suite.sh` is empty), so every A/B
  isolates the **engine/app sources**, never the measuring code.
* Raw artifacts: `/tmp/bench_runs/beta5` (suite; regenerable, not committed
  per `docs/reports/README.md` policy) and `/tmp/iso_rounds` (isolation).

## What actually changed on hot paths since beta4 (source audit)

| Changed since `v1.3.0-beta4` | On the per-key path? | Why |
|---|---|---|
| `TextEngine.*`, `ModernKeyHook.*`, `ChaosEngine.*`, `AiRival.*`, `Progression.*`, `TypingAnalytics.*` | — | **UNCHANGED** (empty git diff) |
| `win32_wrapper.hpp` (+7) | **No** | one unused inline accessor `counters()` (bug B3 readout); adds no instruction anywhere |
| `LiveEffects.hpp` (+50) | **No** | `GateBlocker` pure model + `liveGateBlocker()`; consumed by the UI timer/diagnostics readouts, not per keystroke |
| `Arcade.cpp/.hpp`, `ArcadeFrame.hpp` (+373) | Only while a game runs | B5/B6/B7 game logic + frame rendering; the IME hot path only ever reads `isConsumingKeyboard()` (one atomic load, unchanged) |
| `ProcessMonitor.*`, new `ProcessNameUtil.hpp` | On foreground-change only (Windows) | B4 process identity; never per keystroke |
| `main.cpp`, `ArcadeWindow.cpp`, `DialogLayout.hpp` | **No** | UI thread: dialog creation, click handlers, chrome |

## Result 1 — Standardized suite (`tests/run_bench_suite.sh --ab-base=<beta4 worktree>`)

**Suite verdict: PASS.** Correctness gate (differential oracle, viet74k +
macros + tone pops): **PASS on both sides, identical totals** —
`cases=137878 events=2059419 mismatches=0 stale=0 expectDrift=0 macroGap=0`.

Micro decision latency (`bench_perf.v2`, 2 M keys/workload × 3 interleaved
runs, medians, `T1-decision` p50, ns/key):

| Workload | base (beta4) | cur (beta5) | Δ |
|---|---|---|---|
| vn-compose | 56 | 56 | **0.0 %** |
| mixed | 54 | 54 | **0.0 %** |
| delete | 45 | 45 | **0.0 %** |
| passthrough | 43 | 43 | **0.0 %** |
| T0-control (harness floor) | 23 | 23 | 0.0 % |

Exactly what the source audit predicts: the engine decision layer is
byte-identical, so it measures identically.

Tone layer (`bench_tone_latency`, self-gating composition): **PASS on both
sides** (cur mixed@250wpm t1 p50 4.53 µs vs base 5.10 µs — cur nominally
faster, within host noise; the layer's own gates passed every run on both
sides).

E2E shim pipeline (`e2e_bench`, OutputRing + wake ring + InlineEmitter):
**rc=1 on BOTH sides in every run** — the leg's absolute burst targets are
not met on this shared host (environmental; identical on base, so not a
regression signal by itself). Pooled medians over 9 alternating runs per side
(3 suite + 6 extra pairs):

| Metric (p50, µs) | base | cur | Δ |
|---|---|---|---|
| raw | 31.4 | 32.2 | +2.4 % |
| burst-hot | 30.1 | 31.6 | +5.0 % |

**Attribution, honestly:** the only code delta inside this leg's translation
units is the unused `counters()` accessor above — it cannot add instructions
to the emit path. The same host, the same day, produced per-round
interference up to ±60 % in the isolation campaign below (9 of 30 rounds
tripped a single-round gate that the 15-round medians refute). The first
suite pass showed +12…+18 % on this leg and converged to +2…+5 % with six
more alternating pairs — the signature of host drift, not code. We flag it
rather than bury it: **no code-attributable regression, re-verification on
quiet hardware (CI) is owed** and the leg's absolute gate fails on base too.

## Result 2 — Feature isolation A/B (`bench_v130_isolation`, 15 alternating pairs)

Both sides compiled with identical flags from the sources listed above
(`TextEngine Arcade ArcadeFrame ArcadeRender ChaosEngine AiRival Progression
TypingAnalytics`), 300 000 deterministic keys per round, order swapped each
pair, single pinned core (`taskset -c 1`), medians over the 15 rounds:

| Configuration | base p50 | cur p50 | Δ p50 | Δ mean | Δ p99 | sink digest |
|---|---|---|---|---|---|---|
| 1. Pure IME baseline (control) | 57 | 56 | −1.8 % | −1.2 % | +17.2 % | identical |
| 2. + Inactive Arcade (standby) | 59 | 57 | −3.4 % | −1.0 % | −4.0 % | identical |
| 3. + Active Chaos Engine | 77 | 76 | −1.3 % | −1.5 % | −3.3 % | identical |
| 4. + Active AI telemetry | 68 | 66 | −2.9 % | −1.8 % | −2.7 % | identical |
| 5. + Full v1.3.0 suite | 477 | 477 | **+0.0 %** | +0.8 % | +0.7 % | identical |

**Noise control:** configuration 1 executes byte-identical engine code on
both sides, so its deltas are this campaign's measured noise band
(p50 −1.8 %, mean −1.2 %, p99 +17.2 % — the host's tail was rough). Every
feature configuration lands **inside or better than that band in the
worsening direction**: no configuration regressed beyond the control.

**Determinism:** the 64-bit FNV sink digest is identical between base and cur
in all five configurations, in every round — bit-level behavioral equivalence
on this corpus (the Arcade changes are gameplay-side; the composed output of
the IME corpus is untouched).

**Single-round gates vs medians (the beta4 lesson, repeated):** 9 of the 30
rounds exited with the binary's own single-round verdict FAIL (e.g. round 1
measured cfg-2 at p50 100 ns, "Inactive Arcade overhead 75 %" — one
whole-round interference event). The 15-round medians put the same
configuration at −3.4 % (cur *faster*, inside the control band). This is
exactly why the campaign reports medians of alternating pairs and never a
single round.

## Result 3 — Glyph-flip micro-probe

`ChaosEngine.cpp/.hpp` is **byte-identical to beta4** (empty git diff), so
beta4's measurement stands unchanged and is not re-run: flips ON transform
Vietnamese at ~**+1.8 ns/char** versus pass-through (flip lookups
3.08→4.84 ns/char over the 15 360-mapping tone-mirror tables); flips OFF
(default) keep the functions off the output path entirely — consistent with
cfg-3 above showing no regression.

## Result 4 — Correctness & regression gates (this tree)

* `tests/run_all_tests.sh --jobs=2`: **ALL NATIVE TESTS PASSED** — 46 run
  targets (new: `test_arcade_beta5` 66 checks, `test_settings_wiring` 31
  checks; extended: `test_arcade_window` with the B8 hub Chaos-Lab-row test,
  `test_live_effects_chain`, `test_hook_counters`, `test_process_monitor`,
  `test_dialog_layout`) + 9 source-contract checks (new: `settings wiring` —
  `scripts/audit_settings_wiring.py`, 37 interactive controls × 6 layers).
* `scripts/audit_layout.py`, `audit_controls.py`, `audit_telemetry_rows.py`,
  `check_version.py`, `check_input_isolation.py`, SHA256SUMS: all OK.
* Windows compile: `tools/crossbuild_windows.sh` 6/6 TUs + `.rc` ok;
  `scripts/build_windows_exe.sh --check` — every Windows TU of the full
  product compiles (main.cpp, ArcadeWindow.cpp, ChaosLabWindow.cpp included).

## Verdict

**No code-attributable regression beyond the measured noise floor.** The
engine decision layer measures identical (0.0 % on all four workloads,
identical digests); every feature configuration is inside the byte-identical
control band; the one flagged leg (e2e shim) fails its absolute environmental
gate on base and cur alike, contains no changed code on its path, and
converged from +12…+18 % to +2…+5 % under six additional alternating pairs on
a demonstrably noisy host — recorded above with all raw numbers, owed a
re-check on quiet hardware.
