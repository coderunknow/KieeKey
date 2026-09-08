# KieeKey v1.3.0 RC1 — measurement protocol

Written before the campaign it governs, because a rule adopted after the numbers
exist is a rationalisation. Nothing in this file is a measurement; the numbers live in
`benchmark/results/<campaign>/raw/*.jsonl` and are rendered by `benchmark/scripts/make_report.py`.

Version 1.3.0 RC1 · branch `arena/01a07b1c-kieekey` · instruments: `benchmark/harness/`
`benchmark/scripts/` · report: `benchmark/REPORT.rc1.md` (template
`benchmark/REPORT.rc1.narrative.md`) · candidate record:
[`OPTIMIZATION_LEDGER.md`](OPTIMIZATION_LEDGER.md).

---

## 1. Questions, in the order they may be answered

1. **What does the optimisation work cost or save in the engine's per-key decision
   path?** Answered by the paired L1 pass and the frozen-engine attribution pair (§7, §8).
2. **Is KieeKey's engine faster than the latest UniKey and the latest OpenKey, measured
   the same way?** Answered by L1 against `unikey-4.x`, `openkey-2.0.5`, `openkey-master`
   in both shipping configurations (§6), with the tier vocabulary of §6.4.
3. **What does a user feel per keystroke?** Partly answered by L2 (per-key sample
   distribution, §6.3) and by KieeKey's own producer→consumer pipeline measurement.
   Competitors' production path is **NOT MEASURED** and must not be inferred (§2).
4. **Did correctness change?** Gates only — pass/fail, never traded against speed (§9).

If a question cannot be answered with the instruments in this repository, the report writes
`NOT AVAILABLE` and stops there.

## 2. Deliberate exclusions

* Windows TSF, low-level keyboard hooks, per-app behaviour, injection latency, UI. KieeKey
  ships a Linux engine path here; the competitors' shipped products are Windows TSF apps.
  Their engine decision cost is comparable; their end-to-end cost is not measurable here
  and is therefore never quoted.
* `perf_event_open` hardware counters and valgrind/callgrind: unavailable in this container
  (seccomp / not installed). Attribution uses a `SIGPROF` sampling twin instead (§10), with
  its resolution stated rather than hidden.
* Any engine's debug-only code paths: `-DNDEBUG` is set so the `#ifndef NDEBUG` diagnostics
  inside `TextEngine.cpp` are compiled out, matching the product's CMake Release build.
  Measuring a debug build's bookkeeping would be measuring a binary nobody ships.

## 3. Engines (identical treatment is the whole point)

| column | source | how built |
|---|---|---|
| `kieekey` | `src/core/TextEngine.cpp` (this tree) | static, `-O3 -DNDEBUG` |
| `kieekey-aa` | the **same** code, second instance | A/A control: what the instrument cannot explain |
| `kieekey-base` | `benchmark/reference/kieekey-1.2.2/` (pristine v1.2.2, hash-checked) | `libkkbase.so`, dlopen'd, `-fvisibility=hidden`, `RTLD_LOCAL` |
| `kieekey-cand` | `src/core` | `libkkcand.so`, same shim source, same flags |
| `unikey-4.x` | `tests/reference/unikey/UKEngine.cpp` (2015 CVS snapshot = latest publicly available UniKey engine source) | static |
| `openkey-2.0.5` | vendored OpenKey 2.0.5 release engine | `libok205.so` |
| `openkey-master` | vendored OpenKey master @ `89c2fd3` (2026-06-15) | `libokmaster.so` |

`kieekey-base` / `kieekey-cand` exist so that "gain over the previous release" is measured in
the same rounds of the same process instead of being subtracted across campaigns; they are the
only columns whose *difference* is a candidate's effect. UniKey's shipped Windows binary is not
part of this: the vendored snapshot is labelled as a source snapshot, not as the release.

## 4. Build rules

* Every engine translation unit: `-std=c++17 -O3 -DNDEBUG -w`. Override for ablations only via
  `BENCH_OPT=` / `BENCH_DEF=`, and say so in the artifact.
* No LTO anywhere (a dlopen boundary makes cross-unit inlining an asymmetry between columns).
  No PGO: a profile trained on the benchmark corpus is benchmark overfitting by construction.
* The three vendored sources are integrity-checked against recorded hashes
  (`benchmark/scripts/build.sh::verify_pristine`, FATAL on drift) — including
  `benchmark/reference/kieekey-1.2.2/`, which defines what "baseline" means.
* No benchmark-only source is added to `src/core`; benchmark-only switches live in
  `benchmark/harness/`. (The `KK_WALK_FAST_LEAD/END` ablation switches in
  `src/core/ConsonantWalks.hpp` are preprocessor-only and default to the shipped path.)

## 5. Corpus, streams and configurations

* Word corpus `tests/data/viet74k.txt` (74 000 lines), plus prose passages, an
  "invariant" set and macro expansions for correctness.
* Timed streams per cell: `prose`, `edit-storm` (heavy backspace/re-edit),
  `pathological` (tone/mark thrash, long words, symbol floods). Streams are generated from a
  per-session seed index (`--seed-idx=N`) with `std::rotate` on the corpus order — there is no
  `seed` field in `viet::EncodeCfg`, so rotation *is* the variation mechanism.
* Two configurations, **both reported for every engine and every cell, never one alone**:
  `as-shipped` (each engine's own default product configuration) and `matched-minimal`
  (the same reduced feature set across all engines: no auto-capitals, no smart
  switch-key, no macro, minimal tone/mark handling). A tier claim that holds in only one of
  these is a tier claim about a configuration, and is worded as such.
* Input methods: `telex-end`, `telex-mid`, `vni`.

## 6. Timing and statistics

### 6.1 Passes

* **L1 `--mode=tput`** — per engine per cell per round: `prepare()` + `keys` keystrokes, with
  `invoke` (engine decision only) and `full` (including the harness's replacement bookkeeping)
  both recorded as ns/key. Round 0 of each session is warm-up and is dropped from the
  statistics. `--rotate=1` alternates the engine order every round; the `order` index is in
  every row so the alternation is auditable rather than asserted.
* **L1 fixed-lead pass** — the same cells with `--rotate=0 --order-offset=(N%2)`, into
  `tput_lead_*.jsonl`. Its only job is to test whether "who runs first" moves a result (§6.4).
  These rows are excluded from the L1 medians by source file, never averaged into them.
* **L2 `--mode=latency`** — per-key samples; publishes p50/p90/p99/p999/mean/worst of the
  *sample distribution*, plus `prep_ns_per_key` (harness cost) and `engine_core_net_ns`
  (engine cost after the measured clock-pair overhead) so the two are never conflated.
* **`--mode=cold`** — fresh process per repetition; wall time to first output and the
  first-round ns/key, per engine.
* **`--mode=mem`** (`bench_mem`, one engine per process) — allocations at init and over a
  2 000 000-key soak, bytes, RSS.
* **`--mode=robust`** — adversarial streams, per engine.

### 6.2 Host hygiene

* The campaign refuses to start a measurement pass until the 1-minute load average is ≤ 2.5,
  and prints the value it waited for.
* Affinity: `taskset` to the last core the container exposes (`--pin=auto`, printed in
  `environment.txt` as `pin_note=`). On this box that means one core for measurement, one for
  the host; if pinning is impossible, the campaign says so in the artifact instead of claiming isolation.
* One session = one `bench` process = one unit of independence.

### 6.3 Estimators

* **Medians of per-round, per-session values are primary** (p50-family). Tails
  (p90/p99/p999 and the single worst sample) are published in the same tables and are never
  used to set a noise band.
* Effects are **paired by (session, round, config, method, stream, order-rotation position of
  the pair)**; the bootstrap resamples whole sessions (cluster bootstrap) for the 95 % CI,
  because rounds within a session share cache and thermal state.
* No aggregation across configurations, input methods or streams without the cell label;
  a "geomean over cells" figure is not computed, because it hides which cell regressed.

### 6.4 Bands, tiers, and the wording they license

* `band_ns` = median |`kieekey` − `kieekey-aa`| over all cells — the instrument's floor.
* A cell **regresses** when its paired median Δ > `band_ns` and |Δ %| > 1 %.
  (1×, not 2× — see the docstring of `verdict_for`; the candidate rule in §8 uses 2× and says why.)
* Tiers, evaluated in the deciding cell `as-shipped|telex-end|prose`:
  **A** ≥ 5 % faster with CI excluding 0 *and* no regressing cell anywhere;
  **B** faster with CI excluding 0 but under 5 % or with a regressing cell;
  **C** statistical tie (|Δ %| ≤ 1 % or CI spans 0);
  **MIXED** faster on some cells, slower on others;
  **D** slower.
* Wording rules: only A and B may use "faster", and only with the cell and configuration
  named. A tier below A must state the regressing cells explicitly in the result paragraph.
  "Faster on prose, slower on edit-storm" is reported as MIXED, not as a win with a footnote.
  The fixed-lead pass may not be used to explain away a regression; if the two passes
  disagree beyond the A/A shift, the report says so.

## 7. The attribution pair and its guard

Because a dlopen'd column differs from a static column by plumbing, the pair is only
meaningful with a guard. `--mode=attrib-guard` fails unless:

1. the shim's transcript equals the in-process driver's **exactly** (same `displayChar`
   contract, same backspace pop, same re-issue on restore), and
2. `kieekey-base`'s ns/key lands within 0.25×–4.0× of the in-process column.

Both conditions are gates (`rc1_gates.py attrib-guard`), and `rc1_stats.py` re-publishes the
row so a report cannot quote a gain from a campaign whose guard tripped. This exists because
the first version of the instrument silently measured a 8.7 ns/key early-out in both columns
and reported a tidy, meaningless 1.5 % "gain".

## 8. Candidate acceptance rule (pre-registered)

A candidate is **ACCEPT**ed only if, in the deciding cell, it is faster than the frozen
`kieekey-base` column with a session-cluster bootstrap CI excluding zero, **and** no cell is
slower than baseline by more than 2 × `band_ns`. Otherwise **REJECT** — and the code, the
numbers and the reason go in the ledger anyway. Rejected work is not deleted; a ledger of
only successes would teach nothing.

Never weakened as a side effect of a candidate: the `O(1)`-per-key core property and the
zero-allocation-per-key hot path (§9 `memory` gate), the transcript digest identity (§9
`digest-identity`), and the engine's own golden corpus.

## 9. Gates (`benchmark/scripts/rc1_gates.py`, all pass/fail, fail-closed)

| gate | rule |
|---|---|
| `manifest` | a baseline campaign must measure the tree the manifest hashed; if `src/core` differs from the frozen v1.2.2 reference, `--candidate` is required and the differing files are named |
| `walks` | the walk automata equal the exhaustive reference oracle (12 519 536 leading + 6 955 302 end cases), 0 mismatches, reference digest recorded |
| `attrib-guard` | §7 |
| `digest-identity` | every KieeKey output row is bit-identical to the frozen-engine column of the same campaign; a missing oracle (file or column) is a FAIL, never a free pass |
| `correctness` | all example rows evaluated; absolute per-engine exact rates published, no baseline oracle required |
| `diffab` | keystroke-by-keystroke differential vs the frozen engine: 0 mismatches over ≥ 8 × seeds × keys events (the floor scales with what the campaign asked for) |
| `memory` | allocations over the 2 M-key soak within budget (as-shipped ≤ 21, matched-minimal ≤ 20) and RSS within 96 MiB; no rows for the subject engine = FAIL |
| `sanitizers` | ASan+UBSan+LSan clean in KieeKey-owned sources; findings inside vendored reference code are recorded as upstream defects, not fixed here |

Units are stated in the gate output (RSS is bytes in `bench_mem`, printed as MiB) because a
KiB limit once failed a 46 MiB run that was well inside budget. A gate that only prints is
worse than no gate: every gate was made to FAIL, on purpose, before being trusted.

## 10. Profiling rules

* Profile first, optimise second. A candidate must name the function or line range it targets
  and the expected share; "the hot loop felt slow" is not a rationale.
* `bench_prof` is a `-g -fno-omit-frame-pointer` twin of the measured binary, same `-O3`
  flags — so ranks come from the shipped optimisation level. `ITIMER_PROF` at the requested
  rate (`--hz=1000`); a ~250 Hz kernel cap applies to `perf`, not to us here, and the sample
  count is published next to the shares so a 1 % share is read as ±1 %.
* Shares are ranks. A candidate is not scored by its predicted share; it is scored by the L1
  gain against the frozen column.

## 11. NOT AVAILABLE register

| item | why | what is done instead |
|---|---|---|
| Competitors' end-to-end IME latency | Windows-only production path | excluded (§2) |
| UniKey latest shipped binary | only the 2015 engine snapshot is public; SourceForge SVN unreachable | labelled as a source snapshot; OpenKey master included as the "latest code" column |
| `perf` counters, hardware stalls | seccomp | `SIGPROF` sampling (§10) |
| callgrind / massif | valgrind not installed | `bench_mem` allocator tracking + RSS |
| Frequency, boost and idle-state telemetry | not exposed in this container | recorded as read from sysfs (usually `NOT AVAILABLE`), paired design carries the load |
| Cross-OS comparability | out of scope | stated in every report header |

## 12. Deviations and instrument changelog

A protocol that never admits what broke is not auditable. Fixed in this order, each with its
consequence:

* `-O2` vs `-O3` (campaign flags): the product builds `-O3 -DNDEBUG`; `build.sh` used to default
  `-O2` for engines. All comparisons are `-O3`; earlier `-O2`-era shares are not comparable
  (§4). `0de94ce`/`c7f982c`.
* **Attribution pair measured nothing** (both columns early-out at 8.7 ns/key): fixed by wiring
  `prepare()` *and* making the shim's `apply()` a line-for-line mirror of the in-process driver,
  then adding the §7 guard. `c7f982c`.
* `--out` **truncated** multi-invocation artifacts, so `diffab` inspected only the last seed's
  rows and the memory gate saw one engine: added `--append` and per-engine memory files
  (`72fa5ce`). Every gate was then re-checked by making it fail.
* `Agg::pct()` read percentiles out of an unsorted vector (a cold row printed p95 below p50):
  `pct()` now sorts on demand (`72fa5ce`). The L1/L2 tables were unaffected — they use their own
  sorted medians.
* An unrecognised `--engines=` token fell through to UniKey, which could drop the A/A control
  silently: unknown names now abort with the legal set (`72fa5ce`).
* The A/A control was not wired into the verdict (the band printed empty) and the fixed-lead pass
  was missing from the summary: added `--aa-engine`, `rc1_order` (`72fa5ce` and later in this session).
* `docs/bench/rc1-130/` earlier contained narrative from a run whose artifacts did not survive;
  this protocol and the ledger are now written against the campaign directories in
  `benchmark/results/` only. Numbers quoted in prose always name their campaign.

## 13. Reproducing a campaign

```bash
git rev-parse HEAD                       # must match environment.txt / manifest
./benchmark/scripts/build.sh --sanitizers       # the campaign's own gate step does this
python3 benchmark/scripts/baseline_manifest.py  # hashes tree + frozen baseline + flags
./benchmark/scripts/campaign_rc1.sh --name=rc1-ca4 --candidate \
    --sessions=6 --rounds=24 --keys=200000 --words=74000 \
    --engines=contest,ctl,attrib --diffab-seeds=6 --l2rounds=10
python3 benchmark/scripts/make_report.py --campaign=rc1-ca4 \
    --narrative=benchmark/REPORT.rc1.narrative.md --out=benchmark/REPORT.rc1.md --strict-stat
```

Never rebuild while a campaign is running (`build.sh` recompiles the engine `.so`s from the live
tree, which would swap an engine mid-round). The full campaign above takes roughly 1.5–2 h on the
two cores this container exposes; a tput-only screening pass (`--steps=gate,selftest,tput,tput-lead,summarize
--sessions=4 --rounds=12 --diffab-seeds=1`) is about 25 min and is what §8 decisions are screened
with before a full campaign is spent.
