# KieeKey — performance impact of every opt-in feature (G1)

**Why this file exists (v1.3.0-beta5):** the beta4 tester asked what the
experimental features actually cost per keystroke, and the settings dialog had
no answers. Every toggle that touches the hot path now carries its measured
cost *in the dialog itself*, and this document is the single source with the
methodology behind the numbers.

**Honesty rules used here**

* Every number is a **median over 15 alternating A/B pairs** (order swapped
  each pair), 300 000 keys per round, produced by the portable bench harnesses
  in `tests/` (`bench_v130_isolation`, `bench_engine`, glyph micro-probe).
  Single-shot or 5-round numbers are explicitly *not* reported — the beta4
  campaign documented a first 5-round pass inflating one configuration to
  +9.1 % mean before the medians converged to −1.2 %.
* The **noise floor is measured, not assumed**: configuration 1 of the
  isolation bench runs byte-identical code on both sides; its spread
  (−0.1 % … +0.7 % on p50/mean/p99) is this host's noise band, and every
  feature delta below is judged against it.
* Numbers are p50 **nanoseconds per keystroke** on the Linux bench host
  (`-O3 -DNDEBUG`, pinned). Absolute values on a Windows machine differ; the
  *relative* deltas and their ordering are the decision-relevant part.
* Source of record: [`docs/bench/beta4/BENCHMARK_REPORT.md`](bench/beta4/BENCHMARK_REPORT.md)
  (base = tag `v1.3.0-beta4`). The beta5 campaign re-runs the same protocol
  against that tag: [`docs/bench/beta5/BENCHMARK_REPORT.md`](bench/beta5/BENCHMARK_REPORT.md).

## The hot path, feature by feature

| Feature (settings location) | Default | Measured cost | Evidence |
|---|---|---|---|
| Core IME decision — nothing enabled | always on | p50 **63 ns** (VN compose), **63** (mixed), **57** (delete), **53–54** (passthrough) | beta4 §Result 1 |
| Arcade Hub registered, no game running (tab *Arcade*) | always on | ≈ **+2 ns/key** (p50 64→66) | beta4 §Result 2, cfg 2 |
| Chaos Lab engine active (tab *Chaos Lab*) | **OFF** | ≈ **+21 ns/key** (p50 64→85) while active | beta4 §Result 2, cfg 3 |
| Live-effects glyph flips (tab *Hiệu ứng trực tiếp*) | **OFF** | ≈ **+1.8 ns/char** when flips are enabled (flip lookup 3.08→4.84 ns/char over the 15 360-mapping tone-mirror tables); casing-only is below the noise floor | beta4 §Result 3 |
| AI rival telemetry opt-in (tab *AI*) | **OFF** | ≈ **+11 ns/key** (p50 64→75) while opted in | beta4 §Result 2, cfg 4 |
| Diagnostics level *Full* (tab *Chẩn đoán*) | **OFF** | not separately isolated; bounded by the everything-on worst case below. Level *Off* stops all counters (cost ~0, stated in the dialog) | beta4 §Result 2, cfg 5 |
| **Everything on at once** (worst case) | — | ≈ **454 ns/key** p50 (~0.45 µs) — still under 1 % of a 60 WPM key interval | beta4 §Result 2, cfg 5 |

Determinism note: across all five isolation configurations the 64-bit FNV sink
digest was **identical between base and cur** — the features add latency
budget, they do not change what the engine emits.

## Where each number appears in the UI (beta5)

* tab *Chaos Lab* → warning static: “Chi phí khi Chaos bật: ≈ +21 ns/phím …”
* tab *Hiệu ứng trực tiếp* → hint static: persistence note + “lật glyph ≈ +1,8 ns/ký tự …”
* tab *AI* → live stats line: “… | Chi phí: ≈ +11 ns/phím (bench beta4)”
* tab *Arcade* → idle status: “Chi phí khi không chơi: ≈ +2 ns/phím …”
* tab *Chẩn đoán* → level result line points at this document.

## Re-measuring

```sh
# standardized suite (Result 1 protocol; --ab-base re-runs the A/B legs)
bash tests/run_bench_suite.sh

# feature-isolation A/B (the five configurations of Result 2): both sides with
# identical flags; harness sources: TextEngine Arcade ArcadeFrame ArcadeRender
# ChaosEngine AiRival Progression TypingAnalytics
g++ -std=c++2b -O3 -DNDEBUG -pthread -Isrc/core \
    tests/bench_v130_isolation.cpp src/core/TextEngine.cpp src/core/Arcade.cpp \
    src/core/ArcadeFrame.cpp src/core/ArcadeRender.cpp src/core/ChaosEngine.cpp \
    src/core/AiRival.cpp src/core/Progression.cpp src/core/TypingAnalytics.cpp \
    -o /tmp/bench_v130_isolation && taskset -c 0 /tmp/bench_v130_isolation
```

The beta5 acceptance rule (G4): **no configuration may regress beyond the
measured noise floor (±0.7 %)** versus the `v1.3.0-beta4` tag base; any delta
outside it must be root-caused before release.
