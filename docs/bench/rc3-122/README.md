# KieeKey v1.2.2 RC3 — bench evidence tree

Measured on the RC3 candidate (`arena/01a074b5-kieekey`, base `bc0a20a`):
Linux 6.1.158+, Intel Xeon @ 2.60 GHz, **2 logical CPUs**, 4 GiB RAM,
virtualized shared host (hypervisor details unavailable), g++ 12.2.0
(`-std=c++20 -O2 -DOK_WRAP_NO_WIN32` for e2e; `-std=c++23 -O2` for tone;
`-std=c++20 -O3 -DNDEBUG` for the floor driver). Full environment record:
`rc2-suite/env.json` (RC2 baseline, same host/session).

## Layout

```
README.md                this file
floor-rc2.txt            RC2 floor (median of 5, 20 M keys, mode=1)
rc2-suite/               RC2 baseline artifacts (bc0a20a; env, summary,
                         e2e_cur_1..3, perf_cur_1..3, gate_cur, tone_cur,
                         bin/ = frozen binaries actually run)
rc3-pair/                RC3 A/B evidence (earlier v3 schema runs):
                         e2e_prod_model_100 / _all_model_100 (100 k × 3),
                         _spin100/_spin200/_spin1000 (all-model A/B),
                         _prod_spin100/_prod_spin1000 (production A/B),
                         _1cpu/_2cpu (contention), _paced/_paced_spin100
final/                   FINAL v4 runs (adds p95): e2e-v4-prod-{1,2,3},
                         e2e-v4-all-{1,2,3}, e2e-v4-prod-spin100-{1,2,3},
                         e2e-v4-prod-spin1000-{1,2,3} (100 k keys each)
rc3-suite/               final-tree gates: option-matrix full/asan/clean
                         JSON+logs, floor-rc3, tsan logs, native-suite
                         logs, tone-rc3-final, e2e-final-prod-*, CPU
                         trade-off (cpu-tradeoff-v4.txt) + floor-rc3.txt
tools/cpu_tradeoff.py    CPU% sampler (mean/peak)
```

## Perf decision tables (final v4 evidence, medians of 3 × 100 k keys)

| Question | Evidence | Verdict |
|---|---|---|
| Real user-facing pipeline (shipped Win32 wiring) | `final/e2e-v4-prod-*` | **p50 0.183 / p95 0.594 / p99 0.980 / p99.9 55.0 µs**; burst-hot p999 17.7 µs; wake-pay p50 0.451 / p99 1.74 µs; 0.038–0.063 % scheduler deschedule keys |
| Same methodology as RC2 (all-keys model) | `final/e2e-v4-all-*` vs `rc2-suite/e2e_cur_*` | p50 30.38 vs 27.75 µs (+9.5 %), p99 70.51 vs 71.41 µs (−1.3 %) — **within host noise**; no regression, no win |
| Spin cap 100 vs 1000 (production) | `final/e2e-v4-prod-spin100-*` vs `-spin1000-*` | raw p99.9 49.4 → 18.8 µs, burst p99.9 19.2 → 3.6 µs — real but confined to the bench's 300–800 µs word-burst gaps; **REJECT** (no human-cadence transfer; CPU peak 59.6 → 139 % on the consumer-callback wiring) |
| 1-CPU vs 2-CPU contention (production) | `rc3-pair/e2e_1cpu/_2cpu` | raw p999 22.9 vs 27.3 µs; no starvation cliff; spin/wait safe single-core |
| Engine throughput floor | `floor-rc2.txt` vs `rc3-suite/floor-rc3.txt` | 72.19 → 70.68 ns/key (median 5 × 20 M), same sink `2905520108639366859`; interleaved pairs 71.80 → 70.49 (median), −1.8 %, noise band |
| Engine semantics preserved | full option matrix + diff_engine_ab | 19,080,215 events / 0 mismatch; 0/1,202,051 vs frozen RC1 |

## Verdict

**SHIP WITH LIMITATIONS** — full narrative in
`docs/reports/V1.2.2_RC3_PERFORMANCE_REPORT.md` and
`docs/reports/V1.2.2_RC3_ENGINEERING_LOG.md`.
