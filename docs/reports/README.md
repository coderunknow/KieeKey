# Historical Engineering Reports (KieeKey lineage)

The documents in this folder were written during the engineering lineage
that produced **KieeKey v1.0** — a fork of [OpenKey](https://github.com/tuyenvm/OpenKey)
(GPL-3.0) by Tuyen Mai. They predate the v1.0 release unification: they use
the pre-release working name **"OpenKey NextGen"** and internal milestone
numbers **v3.0–v3.4**. They are published **verbatim** (no content rewriting)
for full traceability; the first public release is KieeKey v1.0.

| Document | Lineage milestone | Content |
|---|---|---|
| [TECHNICAL_REFACTORING_PLAN.md](TECHNICAL_REFACTORING_PLAN.md) | v3.0 | Full port/refactor execution plan from OpenKey 2.0.5 |
| [FIX_DESIGN.md](FIX_DESIGN.md) | v3.0.x | Design notes: ghosting/sticking, accent-delay fixes |
| [HOTFIX_REPORT.md](HOTFIX_REPORT.md) | v3.0.x | Root-caused hotfixes (restore re-issue families) |
| [DIVERGENCES.md](DIVERGENCES.md) | — | Intentional behavioral divergences vs unmodified OpenKey 2.0.5 |
| [EXTREME_TEST_REPORT.md](EXTREME_TEST_REPORT.md) | v3.0 phase | Fuzz + invariant + determinism extreme verification |
| [EDGE_BEHAVIORS_REPORT.md](EDGE_BEHAVIORS_REPORT.md) | v3.0 phase | Edge-case battery, WPM/chunking determinism |
| [DIRTY_INPUT_REPORT.md](DIRTY_INPUT_REPORT.md) | v3.0 phase | Dirty/symbol input path verification |
| [REAL_PASSAGES_REPORT.md](REAL_PASSAGES_REPORT.md) | v3.0 phase | 32 real mixed EN–VN passages, key-by-key |
| [BENCHMARK_DELTA.md](BENCHMARK_DELTA.md) | v3.0.x | P0–P3 execution deltas of the master plan |
| [MEGA_BENCH_REPORT.md](MEGA_BENCH_REPORT.md) | v3.0.x | ~5.97M-case 3-way differential (KieeKey engine vs clean-room oracle vs vendored engines) |
| [THREE_ENGINE_BENCH_REPORT.md](THREE_ENGINE_BENCH_REPORT.md) | v3.0.x | Engine vs OpenKey 2.0.5 vs UniKey comparison |
| [ACCENT_AB_REPORT.md](ACCENT_AB_REPORT.md) | v3.0.x | Consumer grouping A/B (accent session cost) |
| [LINEAGE_v3.3.1_FIX_REPORT.md](LINEAGE_v3.3.1_FIX_REPORT.md) | **v3.3.1** | Fix report for milestone v3.3.1 (originally `V331_FIX_REPORT.md`) |
| [LATENCY_AUDIT_REPORT.md](LATENCY_AUDIT_REPORT.md) | v3.4 | Deep E2E latency audit (tone-mark path) |
| [VERIFICATION_PROTOCOL.md](VERIFICATION_PROTOCOL.md) | v3.4 | Real-Windows sign-off protocol |
| [TONG_KET_VI.md](TONG_KET_VI.md) | v3.0.x | Tổng kết tiếng Việt của giai đoạn refactor |

**Current-release reports** (not part of the verbatim lineage record):

| Document | Release | Content |
|---|---|---|
| [V1.2.2_STABLE_RELEASE_REPORT.md](V1.2.2_STABLE_RELEASE_REPORT.md) | **v1.2.2 Stable** | RC4 → Stable: the qualified RC4 tree promoted byte-identical (release identity + evidence only, zero runtime changes); full independent-host re-verification (ctest 20/20, native 23/23, oracle 137,878/2,059,419/0, deep option matrix 92.4 M events/0 failures, ASan+UBSan+LSan and TSan clean, single-core 7/7, cross-host determinism digests identical), frozen three-way benchmark with binary-identity proof and A/A noise controls, honest Windows matrix (x64+ARM64 PE cross-build PASS; ARM64EC/real-Windows NOT AVAILABLE/NOT EXECUTED); verdict RELEASED |
| [V1.2.2_STABLE_PERFORMANCE_REPORT.md](V1.2.2_STABLE_PERFORMANCE_REPORT.md) | **v1.2.2 Stable** | Strict three-way performance: floor 79.58→72.71→72.75 ns/key (1.2.1/RC4/Stable; −8.6 % vs 1.2.1, +0.06 % RC4→Stable) with byte-identical sink digests on every run, micro p50 flat (0.0 % RC4→Stable, section-identical binaries), E2E deltas classified as host noise via same-binary A/A controls (±10–21 %), +0.28 MB consistent IME footprint vs 1.2.1 explained, competitor benchmark re-run; raw artifacts in `docs/bench/stable-122/` |
| [V1.2.2_RC4_RELEASE_REPORT.md](V1.2.2_RC4_RELEASE_REPORT.md) | **v1.2.2 RC4** | RC3 → RC4 stable-qualification campaign: TSF double-Release P0 (UAF/double-free on every batch commit) fixed, producer-side fault isolation + modifier-toggle race fixed, lifecycle/OOM hardening, release-engineering batch (generators/presets/CI-SHA-gate/dead code), full gate re-runs (native 20/20, ASan/UBSan/LSan + TSan clean, single-core, determinism, strict RC3↔RC4 A/B, cross-version + competitor benchmarks) and the honest Windows-validation matrix; verdict READY FOR STABLE with documented limitations |
| [V1.2.2_RC4_PERFORMANCE_REPORT.md](V1.2.2_RC4_PERFORMANCE_REPORT.md) | **v1.2.2 RC4** | Strict RC3↔RC4 A/B (hot p50 +0.15 %, throughput −0.04 % — noise; engine byte-identical), 20M-key throughput floor with identical sink digests, cross-version + competitor engine benchmarks, optimization log with rejected list; raw artifacts in `docs/bench/rc4-122/` |
| [V1.2.2_RC4_RISK_INVENTORY.md](V1.2.2_RC4_RISK_INVENTORY.md) | **v1.2.2 RC4** | The complete RC4 release-risk inventory: every audit finding (P0–P3) with evidence, fix or acceptance rationale, and the audited-clean area list |
| [V1.2.2_RC3_PERFORMANCE_REPORT.md](V1.2.2_RC3_PERFORMANCE_REPORT.md) | **v1.2.2 RC3** | RC2 → RC3: the full keyboard→visible-text pipeline measured end-to-end *before* optimizing (production wiring p50 0.183 / p99 0.980 µs), the WinUI-3 dead-input P0 (`ProducerDecision{}` never woke the consumer), Tier 6 option torture, and the complete list of A/B-measured **rejected** candidates; raw artifacts in `docs/bench/rc3-122/` |
| [V1.2.2_RC3_ENGINEERING_LOG.md](V1.2.2_RC3_ENGINEERING_LOG.md) | **v1.2.2 RC3** | The RC3 campaign in order: hypothesis → baseline → measurement → verdict, including every dead end and why no hot-path change was adopted |
| [V1.2.2_RC2_PERFORMANCE_REPORT.md](V1.2.2_RC2_PERFORMANCE_REPORT.md) | **v1.2.2 RC2** | RC1 → RC2: option-matrix hardening + performance-preference surface lockdown — VIQR/legacy-table precedence fix, `resetForConfigurationChange()`, two new always-on test layers, RC1 throughput floor re-verified against the changed engine; raw artifacts in `docs/bench/rc2-122/` |
| [V1.2.2_RC1_PERFORMANCE_REPORT.md](V1.2.2_RC1_PERFORMANCE_REPORT.md) | **v1.2.2 RC1** | v1.2.1 Stable → RC1: second-gen engine hot-path (sequence buckets, bitmask classifiers), this-host A/B 54.41 → 43.85 ns/key (−19.4 %), host-scaled exceptional bar cleared, honest no-E2E / no-40.2-claim; raw artifacts in `docs/bench/rc1-122/` |
| [V1.2.2_RC1_ENGINEERING_LOG.md](V1.2.2_RC1_ENGINEERING_LOG.md) | **v1.2.2 RC1** | Investigation trail: gprof #1 `handleMainKey`, KEEP `emitMtx`, rejected in-class `static_assert` (MSVC/`/WX` footgun), dead ends |
| [V1.2.1_RC1_PERFORMANCE_REPORT.md](V1.2.1_RC1_PERFORMANCE_REPORT.md) | **v1.2.1 RC1** | v1.2.0 Stable → RC1 performance investigation: profiling, the candidate set and the measured verdict per candidate |
| [V1.1.3_HARDENING_REPORT.md](V1.1.3_HARDENING_REPORT.md) | **v1.1.3** | v1.1.2 → v1.1.3 quality/performance hardening: accuracy (P1), latency (P2), stability (P3), real-world robustness, regression protection. No new features |
| [V1.1.2_MEGA_BENCH_REPORT.md](V1.1.2_MEGA_BENCH_REPORT.md) | **v1.1.2-r2** | FULL-budget mega correctness benchmark (all mandated budgets — the lineage `MEGA_BENCH_REPORT.md` above ran the previous harness generation at FAST/1 % budgets) |
| [V1.1.0_TOGGLE_FIX_REPORT.md](V1.1.0_TOGGLE_FIX_REPORT.md) | **v1.1.0** | Root cause + fix for the "it suddenly turns off and I can't turn it back on" toggle-reliability report |
| [POST_RELEASE_AUDIT_REPORT.md](POST_RELEASE_AUDIT_REPORT.md) | **v1.1.0** | Full-source post-release audit of the v1.1.0 drop (engine, hooks, TSF composer, tray app, tests) for edge cases, correctness, consistency, stability and speed |
| [V1.1.0_BENCHMARK_REPORT.md](V1.1.0_BENCHMARK_REPORT.md) | **v1.1.0** | v1.0.1 → v1.1.0 correctness suites, engine micro-bench, E2E/tone latency comparison + regression-test inventory |
| [V1.2.0_BENCHMARK_REPORT.md](V1.2.0_BENCHMARK_REPORT.md) | **v1.2.0** | v1.1.3 → v1.2.0 audited benchmark corpus, hardened micro/integration/tone latency layers, differential correctness gate, external-engine comparison |
| [V1.2.0_STABLE_BASELINE_REPORT.md](V1.2.0_STABLE_BASELINE_REPORT.md) | **v1.2.0 Stable** | The FROZEN pre-work baseline every stability change is measured against (commit `d2453db`): micro/E2E/tone numbers, resource figures, and the audit gaps the baseline exposed |
| [V1.2.1_RC2_PERFORMANCE_REPORT.md](V1.2.1_RC2_PERFORMANCE_REPORT.md) | **v1.2.1 RC2** | RC1 → RC2: profiled engine optimizations (decision-identical, −28…−61 % decision latency), RC1-vs-RC2 tables with faster/unchanged/slower verdicts, Performance Preference Profiles measured, notification framework, 3 bugs + regression tests, rejected strategies; raw artifacts in `docs/bench/` |
| [V1.2.0_STABLE_RELEASE_REPORT.md](V1.2.0_STABLE_RELEASE_REPORT.md) | **v1.2.0 Stable** | Stability / correctness / real-world-UX release report: root-cause + fix for each P1–P3, new test suites, performance vs the frozen baseline (with the same-tree A/B methodology correction), release gate, deferred issues |
| [V1.2.1_RC3_RELEASE_REPORT.md](V1.2.1_RC3_RELEASE_REPORT.md) | **v1.2.1 RC3** | RC2 → RC3: pending-counter underflow fix (post-wedge stall family, reproduced by the RC2 gate on a contended host), bucket-indexed consonant scans (decision-identical on 4.8 M lockstep events, −23…−46 % p50), RC2-vs-RC3 tables with honest UNCHANGED verdicts, version-carrier close; raw artifacts in `docs/bench/rc3/` |
| [V1.2.1_STABLE_RELEASE_REPORT.md](V1.2.1_STABLE_RELEASE_REPORT.md) | **v1.2.1 Stable** | RC3 → Stable: whole-repository bug-hunt — TSF use-after-free (P0), 2 cross-thread handle reads, WinUI 3 macro-drop / TSF-affinity / stale-version fixes, build portability; NEW Windows x64+ARM64 PE cross-build gate (clang -Wall -Wextra -Werror); 6-seed differential 7.21 M events 0 mismatches; RC3-vs-Stable A/B with byte-identical sinks (engine untouched → UNCHANGED, evidenced); honest NOT MEASURED boundaries; raw artifacts in `docs/bench/stable/` |

Frozen benchmark evidence (`imebench_kit/results*`, `benchmark_runs/`) is
**not shipped** in the source release — it is regenerable with the
[`imebench_kit`](../../imebench_kit) harness and was generated under the
pre-release `*_nextgen_*` naming during the lineage runs.
