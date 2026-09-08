# Raw benchmark artifacts

Directories are named `<side>[-<version>]`. The **`-122` suffix marks the
v1.2.2 campaign**; the bare `rc1/`, `rc2/`, `rc3/` trees are the older
v1.2.1 campaign and must not be overwritten by v1.2.2 runs.

| directory | campaign | documented in |
|---|---|---|
| `rc1-130/` | **v1.3.0 RC1** engine-decision benchmark + optimisation campaign (four-way, frozen-engine attribution pair) | [`rc1-130/PROTOCOL.md`](rc1-130/PROTOCOL.md) |
| `stable-122/` | v1.2.2 Stable release campaign (independent-host re-verification + three-way 1.2.1/RC4/Stable) | this file, below |
| `rc4-122/` | v1.2.2 RC4 stable-qualification campaign | this file, below |
| `rc1-122/` | v1.2.2 RC1 engine A/B vs v1.2.1 Stable | this file, below |
| `rc2-122/` | v1.2.2 RC2 throughput floor + option matrix | this file, below |
| `rc3-122/` | v1.2.2 RC3 end-to-end pipeline campaign | [`rc3-122/README.md`](rc3-122/README.md) |
| `rc1/`, `rc2/` | v1.2.1 RC1-vs-RC2 | this file, below |
| `rc3/` | v1.2.1 RC2-vs-RC3 | this file, below |
| `stable/` | v1.2.1 RC3-vs-Stable | this file, below |

## v1.3.0 RC1 (engine-decision benchmark + optimisation campaign)

`rc1-130/` is a campaign directory with a different shape from the v1.2.2 ones: its raw
artifacts live in [`benchmark/results/`](../../benchmark/results) (one directory per campaign
run, JSON Lines + per-step logs + `tables.md`/`summary.json`), and what lives *here* is the
method and the verdicts:

| file | role |
|---|---|
| `PROTOCOL.md` | the pre-registered rules: passes, estimators, noise bands, tier vocabulary, gates, exclusions, deviations log |
| `OPTIMIZATION_LEDGER.md` | every candidate with its ACCEPT/REJECT, including the rejected ones, and the rejections' reasons |
| `OPTIMIZATION_PLAN.md` | the forward plan: measured gap arithmetic, profile-grounded candidate specs with expected nanoseconds and falsifiers, and the conditions for declaring the objective unreachable |
| `baseline_manifest.json` | what a campaign measured: tree hashes, frozen-baseline hashes, flags, host facts |
| `baseline_environment.txt` | the same facts in `k=v` form for the report generator |
| `rc1-130/`, `rc1-cc1/`, `rc1-ca4/` | per-campaign released trails, each holding `tables.md`, `summary.json`, `gates.txt`, `environment.txt`, `manifest_at_build.json`, `build.log`, `attrib-guard.log` — copied by `benchmark/scripts/rc1_trail.sh`, which refuses empty artifacts. `rc1-ca4` (C-A) and `rc1-cc1` (C-C1) are candidate trials, both REJECT; `rc1-130` is the release measurement of the frozen engine. Raw JSON Lines for all three stay in [`benchmark/results/`](../../benchmark/results) |
| `rc1-130/PROTOCOL.md` §12 | the deviations log: what broke in the instruments, in order, with consequences — including the profile shares that were withdrawn |

The instruments are in [`benchmark/`](../../benchmark) — `scripts/campaign_rc1.sh` runs a
campaign, `scripts/rc1_gates.py` decides pass/fail, `scripts/rc1_stats.py` produces the
tables, and `benchmark/REPORT.rc1.md` is generated from those artifacts and refuses to
render a number it cannot resolve.

## v1.2.2 Stable (release campaign)

`stable-122/` holds the Stable evidence tree, produced on an **independent
host/compiler** (Debian 12, g++ 12.2 — the RC4 campaign host was Debian 13,
g++ 14.2, so cross-host number comparisons are HISTORICAL / NOT STRICT):
`environment.txt` (host/toolchain freeze), the RC4 baseline re-runs on this
host (`baseline_ctest_rc4.log`, `native_suite_rc4.log`,
`asan_ubsan_rc4_thishost.log`, `tsan_rc4_thishost.log`,
`single_core_rc4_thishost.log`), the deep option matrix (92.4 M events,
0 failures), `determinism/` (option-matrix ×3 digests identical to the RC4
campaign's cross-host value; seeded-stress clean-vs-ASan diffs fully
enumerated), the frozen three-way benchmark (`suite_121_vs_rc4/` +
`ab_121_vs_rc4_*` analysis, `ab_rc4_vs_stable_*` + 8-pair e2e campaign,
`floor_121_vs_rc4.txt`, `floor_rc4_vs_stable.txt` with the A/A floor
control, `aa_control_noise_band.txt` — the same-binary host noise band that
classifies the E2E latency deltas), `cpu_rss_floor.txt`, the competitor
benchmark, and the Windows cross-build logs (`zig_cross_rc4.log`,
`zig_cross_stable.log`, `windows-cross_sha256.txt`). Reports:
[`V1.2.2_STABLE_RELEASE_REPORT.md`](../reports/V1.2.2_STABLE_RELEASE_REPORT.md),
[`V1.2.2_STABLE_PERFORMANCE_REPORT.md`](../reports/V1.2.2_STABLE_PERFORMANCE_REPORT.md).

---

## v1.2.2 RC4 (stable-qualification campaign)

`rc4-122/` holds the RC4 evidence tree:
`environment.txt` (host/toolchain capture), `baseline_ctest_rc3.log` (frozen
RC3 baseline re-run), `asan_ubsan_rc4.log` + `tsan_rc4.log` (sanitizer
campaigns), `single_core_rc4.log`, `determinism/` (triple-run digests +
clean-vs-ASan explanation), `ab/` (strict RC3↔RC4 e2e/tone/tput/real-world
A/B + analysis JSON), `cross-version/` (v1.2.0/1.2.1/RC1/RC2 re-measurements
with comparability labels), `windows-cross/` (KieeKeyApp.exe PE32+ + SHA256),
`competitor_three_engines.log` (5 × 2M-key runs vs OpenKey 2.0.5 / UniKey),
option-matrix deep-run JSON. Reports:
[`docs/reports/V1.2.2_RC4_PERFORMANCE_REPORT.md`](../reports/V1.2.2_RC4_PERFORMANCE_REPORT.md),
[`docs/reports/V1.2.2_RC4_RELEASE_REPORT.md`](../reports/V1.2.2_RC4_RELEASE_REPORT.md).

---

## v1.2.2 RC3 (end-to-end pipeline campaign)

`rc3-122/` holds the RC3 evidence tree and is **self-documented** — see
[`rc3-122/README.md`](rc3-122/README.md) for the full layout, the host
record and the per-question decision tables (including every rejected
optimization candidate). Summary of the top-level entries:

| path | what |
|---|---|
| `rc3-122/rc2-suite/` | RC2 baseline artifacts on the same host/session (env, summary, `e2e_cur_1..3`, `perf_cur_1..3`, gate/tone, `bin/` = the frozen binaries actually run) |
| `rc3-122/rc3-pair/` | RC3 A/B evidence, earlier `e2e_bench.v3` schema (model, spin-cap, contention, paced variants) |
| `rc3-122/final/` | FINAL `e2e_bench.v4` runs (adds p95): production / all-model / spin100 / spin1000, ×3 at 100 k keys |
| `rc3-122/rc3-suite/` | final-tree gates: option-matrix full/asan/clean, floor-rc3, tsan + native-suite logs, CPU trade-off |
| `rc3-122/floor-rc2.txt` | RC2 throughput floor re-measured on this host (comparison point) |
| `rc3-122/tools/cpu_tradeoff.py` | CPU% sampler (mean/peak) used for the spin-cap trade-off |

Reports: [`docs/reports/V1.2.2_RC3_PERFORMANCE_REPORT.md`](../reports/V1.2.2_RC3_PERFORMANCE_REPORT.md),
[`docs/reports/V1.2.2_RC3_ENGINEERING_LOG.md`](../reports/V1.2.2_RC3_ENGINEERING_LOG.md).

---

## v1.2.2 RC2 (throughput floor + option matrix)

| path | what |
|---|---|
| `rc2-122/floor-rc2.txt` | RC2 throughput floor, quiet host (`tests/bench_tput_floor.cpp`, 20 M keys, mode=1) |
| `rc2-122/floor-paired-ab.txt` | fair interleaved RC1-vs-RC2 floor runs ×5 with the shared output sink (identical sinks = decision-identical) |
| `rc2-122/option-matrix-full-120k.json` | `tests/test_option_matrix.cpp` full run, 120 k events/config |
| `rc2-122/option-matrix-full-300k.json` | same harness, 300 k events/config (deep run) |

Report: [`docs/reports/V1.2.2_RC2_PERFORMANCE_REPORT.md`](../reports/V1.2.2_RC2_PERFORMANCE_REPORT.md).

---

## v1.2.2 RC1 (engine A/B vs v1.2.1 Stable)

`rc1-122/` is the frozen v1.2.1 Stable baseline **plus** the v1.2.2 RC1
candidate on this host (g++ 12.2 −O2). It is a new directory on purpose:
`rc1/` below is the **v1.2.1 RC1** baseline from the RC1-vs-RC2 campaign
and must not be overwritten.

| path | what |
|---|---|
| `rc1-122/environment_host.json` | compiler, OS, CPU, RAM, baseline commit `715c527` / tag `v1.2.1-Stable` |
| `rc1-122/tput_driver.cpp` | clock-free 20 M-key driver (`g++ -std=c++20 -O2 -Isrc/core tput_driver.cpp src/core/TextEngine.cpp`) |
| `rc1-122/tput_baseline.txt` | uninstrumented 3-run, frozen tree (median 54.41 ns/key) |
| `rc1-122/tput_candidate.txt` | uninstrumented candidate runs |
| `rc1-122/tput_ab.txt` | fair interleaved orig-vs-cand numbers + sinks |
| `rc1-122/gprof_flat_baseline.txt` | gprof flat profile of v1.2.1 Stable |
| `rc1-122/gprof_flat_candidate.txt` | gprof flat profile of v1.2.2 RC1 |
| `rc1-122/tput_pg_*.log` | instrumented (`-pg`) 20 M-key logs (same sink) |

Reports: [`docs/reports/V1.2.2_RC1_PERFORMANCE_REPORT.md`](../reports/V1.2.2_RC1_PERFORMANCE_REPORT.md),
[`docs/reports/V1.2.2_RC1_ENGINEERING_LOG.md`](../reports/V1.2.2_RC1_ENGINEERING_LOG.md).

---

## v1.2.1 RC1 baseline vs v1.2.1 RC2

Committed on purpose (exception to the "regenerable evidence is not
shipped" policy) because the RC2 release claims are made *against* these
files. Nothing here was edited after the runs.

| path | what |
|---|---|
| `rc1/suite/`, `rc2/suite/` | `tests/run_bench_suite.sh --side=<rc>`: `env.json` (compiler, flags, OS, CPU, RAM, timer), gate log + rc, `bench_perf` ×3 (json+log), `e2e_bench` ×3 (json+log+rc), `bench_tone_latency` log, `summary.{txt,json}` |
| `rc1/realworld.log`, `rc2/realworld.log` | `tests/bench_real_world_typing --keys=50000 --runs=3` |
| `rc1/profiles_*.{log,json}` | `tests/bench_profiles.cpp` compiled against the **frozen RC1 tree** (`-DBENCH_RC1_ENGINE`), Balanced row = "RC1 / default" |
| `rc2/profiles_*.{log,json}` | same harness, RC2 tree, all profiles; `burst` = unpaced 100 k keys, `paced` = 1000 keys/s with word pauses, 20 k keys |
| `rc2/gprof_flat.txt` | gprof flat profile of the RC2 engine on the 20 M-key throughput driver |
| `rc2/tput_driver.cpp` | that driver (clock-free; `g++ -std=c++20 -O2 -Isrc/core tput_driver.cpp src/core/TextEngine.cpp`) |
| `rc2/compare_tables.md` | auto-generated RC1-vs-RC2 tables with per-row faster/unchanged/slower verdicts |

RC1 side = commit `0884c64` (`v1.2.1-RC1`); RC2 side = commit recorded in
`rc2/suite/env.json`. Same host, same compiler, same flags, same corpora,
same seeds; runs were interleaved-by-side where the script supports it.
Binaries (`suite/bin/`, `realworld`, `bench_profiles*`) are not committed.

---

## v1.2.1 RC2 baseline vs v1.2.1 RC3

| path | what |
|---|---|
| `rc3/suite/` | `tests/run_bench_suite.sh`: `env.json`, `perf_base_1`/`perf_cur_1`, `e2e_base_1`/`e2e_cur_1`, `summary.{txt,json}` — base side = RC2, cur side = RC3 |
| `rc3/compare_tables.md` | auto-generated RC2-vs-RC3 tables with per-row faster/unchanged/slower verdicts |

Report: [`docs/reports/V1.2.1_RC3_RELEASE_REPORT.md`](../reports/V1.2.1_RC3_RELEASE_REPORT.md).

---

## v1.2.1 RC3 vs v1.2.1 Stable

The Stable release touched the Windows surface only (the engine was
untouched), so this tree is a *no-regression* record rather than a
performance campaign.

| path | what |
|---|---|
| `stable/env.json`, `stable/environment.txt` | host, compiler, flags, commit record |
| `stable/rc3_baseline_summary.txt` | the frozen RC3 baseline side |
| `stable/ab_rc3_vs_stable_summary.{txt,json}` | RC3-vs-Stable A/B summary (byte-identical sinks → UNCHANGED, evidenced) |
| `stable/tput_20m_ab.txt` | 20 M-key throughput A/B |
| `stable/diff_6seeds.txt` | 6-seed differential, 7.21 M events, 0 mismatches |
| `stable/sanitizer_summary.txt` | ASan/UBSan/TSan run summary |
| `stable/zig_cross.txt` | Windows x64 + ARM64 PE cross-build gate log |

Report: [`docs/reports/V1.2.1_STABLE_RELEASE_REPORT.md`](../reports/V1.2.1_STABLE_RELEASE_REPORT.md).

The [`rc1-130/`](rc1-130/) directory now covers a shipped release, not only a measurement: its
`REPORT`-side documents (PROTOCOL §14, OPTIMIZATION_PLAN §5, OPTIMIZATION_LEDGER §3d) carry the
v1.3.0-RC1 engine change (+1.56 % per key over v1.2.2 with bit-identical output, campaign `rc1-v13rel`)
and the opt-in low-latency profile that measures −5.84 % against UniKey on the deciding cell with its
behavioural price published (campaign `rc1-v13prof`).
