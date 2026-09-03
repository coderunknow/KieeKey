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
| [V1.1.0_BENCHMARK_REPORT.md](V1.1.0_BENCHMARK_REPORT.md) | **v1.1.0** | v1.0.1 → v1.1.0 correctness suites, engine micro-bench, E2E/tone latency comparison + regression-test inventory |

Frozen benchmark evidence (`imebench_kit/results*`, `benchmark_runs/`) is
**not shipped** in the source release — it is regenerable with the
[`imebench_kit`](../../imebench_kit) harness and was generated under the
pre-release `*_nextgen_*` naming during the lineage runs.
