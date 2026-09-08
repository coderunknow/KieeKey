# KieeKey v1.3.0 RC1 — engine-decision benchmark and optimisation record

Campaign **{{env:campaign}}** · run recorded at {{meta:t0_unix}} · corpus `{{meta:corpus}}` (seed
{{meta:seed}}) · {{env:words}} word cases per correctness pass, {{env:keys}} keys per timed stream ·
engine set `{{env:engines}}` · {{env:sessions}} sessions × {{env:rounds}} paired rounds ·
compiler {{stat:manifest.toolchain.compiler}} · flags `{{stat:manifest.toolchain.std}}
{{stat:manifest.toolchain.opt}} {{stat:manifest.toolchain.defines_bench}}` for every engine
translation unit (read out of the build manifest, not out of the build script) ·
{{stat:manifest.environment.cpu_model}}, {{stat:manifest.environment.cores_online}} logical CPUs
({{stat:manifest.environment.affinity}} usable by this container), affinity: {{env:pin_note}}.

Everything between the rules is generated. Numbers are spliced out of
`benchmark/results/{{env:campaign}}/` by `benchmark/scripts/make_report.py`; nothing is typed by
hand, and the generator refuses to write the file if any figure fails to resolve. Method and the
pre-registered decision rules: [docs/bench/rc1-130/PROTOCOL.md](../docs/bench/rc1-130/PROTOCOL.md).
Candidate-by-candidate verdicts (including the ones that were rejected):
[docs/bench/rc1-130/OPTIMIZATION_LEDGER.md](../docs/bench/rc1-130/OPTIMIZATION_LEDGER.md).

---

## 1. Result

**{{stat:verdict.label}}** against UniKey in the deciding cell
`{{stat:deciding_cell}}`: **{{stat:verdict.rel_pct}} %** (positive = KieeKey slower), from
{{stat:verdict.paired_samples}} paired round samples. **{{statcount:verdict.regressing_cells}}** of
the measured cells regress against UniKey: {{regressing:6}}.

Against the frozen v1.2.2 engine, measured in the same rounds of the same campaign, the current
tree's change is **{{stat:candidate_verdict.deciding_gain_pct}} %** in the deciding cell
({{stat:candidate_verdict.deciding_gain_ns}} ns/key saved, 95 % CI
{{stat:candidate_verdict.deciding_ci_ns.0}}…{{stat:candidate_verdict.deciding_ci_ns.1}} ns against a
noise floor of {{stat:candidate_verdict.band_ns}} ns), and
{{statcount:candidate_verdict.cells_regressing_beyond_band}} cell(s) regress beyond that floor.
Candidate decision by the pre-registered rule: **{{stat:candidate_verdict.decision}}**.

Correctness gates: **{{gatesummary}}**. Each gate is pass/fail, not a number to trade against speed:

| gate | measured statement |
|---|---|
| manifest | {{gate:manifest}} |
| walk oracle | {{gate:walks}} |
| attribution guard | {{gate:attrib-guard}} |
| output digest identity | {{gate:digest-identity}} |
| corpus correctness | {{gate:correctness}} |
| differential vs frozen engine | {{gate:diffab}} |
| memory budget | {{gate:memory}} |
| sanitizers | {{gate:sanitizers}} |

### 1.1 What this is a measurement of — and what it is not

| Question | Instrument | Status |
|---|---|---|
| How long does the engine take to decide one keystroke, on a corpus, with the same configuration a user would ship? | L1 `--mode=tput`: `prepare()` + one `process()` per key, all four engines in the same process, same round, rotated order | measured below |
| What does a user feel per keystroke, including the producer→consumer hop and text replacement? | L2 `--mode=latency`: per-key sample distribution (p50…p999, worst sample), KieeKey only for the pipeline part, with the `prep_ns_per_key` split published separately | measured below |
| How much does the IME cost inside a real Windows session (TSF, hook chain, terminal/editor behaviour)? | — | **NOT MEASURED.** The competitors' production path is Windows-only and is not exercised here; no number in this document may be read as an end-to-end product comparison, and none is extrapolated from engine timing |
| Same question for KieeKey's own product pipeline | its own harness | measured separately, reported as a KieeKey-side figure only, never compared against the competitors' unmeasured path |
| Hardware counters, callgrind-style attribution | `perf`: {{env:perf}} · `valgrind`: {{env:valgrind}} | **NOT AVAILABLE** here. Attribution uses a `SIGPROF` sampling twin instead, and §9 states what that can and cannot resolve |

Two instruments in this repository report a quantity called "ns per key" and they are **not**
comparable: L1 is throughput of the decision call inside one process; the v1.2.2-era per-key
"engine ns/key" in `benchmark/REPORT.md` is a different stage definition (it includes the harness's
replacement bookkeeping). This report never merges them, never subtracts one from the other, and
does not use one to check the other.

---

## 2. Correctness

### 2.1 Output is unchanged by the optimisation work

The strongest available statement is a *paired* one: the same corpus, the same campaign, two builds —
the current tree measured in-process, and the pristine v1.2.2 sources (
`benchmark/reference/kieekey-1.2.2/`, integrity-checked against
{{gate:manifest}}) measured through the `kieekey-base` column.

* {{stat:correctness_totals.rows}} example rows across configs {{statlist:correctness_totals.configs}}
  and methods {{statlist:correctness_totals.methods}}; every KieeKey output in every row is
  bit-identical to the frozen engine's (`{{gate:digest-identity}}`).
* Differential keystroke stream: {{stat:diffab_totals.events}} events compared key by key —
  produced code, backspaces, replacement spans, final visible text — with
  {{stat:diffab_totals.per_key_mismatches}} mismatches; final texts equal:
  {{stat:diffab_totals.final_text_equal}}; builds {{statlist:diffab_totals.builds}}.
* Consonant-walk automata (the new structures in this tree) are checked against an exhaustive
  reference over {{stat:walks.leading_cases}} leading and {{stat:walks.end_cases}} end-of-word
  cases: {{stat:walks.mismatches}} mismatches.

{{table:rc1_diffab}}

### 2.2 Absolute correctness of all four engines

"`exact`" means the engine's output equals the *intended* text — not that it equals KieeKey's
output. Several of these corpora exercise behaviour where the engines legitimately disagree (e.g.
`bata` → `bât`, `atlas` → `atlá`, `box` → `bõ`), so a low exact rate on a cell is not by itself a
bug claim against one engine; the agreement column is shown next to it for exactly that reason and
must not be read as correctness.

{{table:rc1_corr}}

{{table:rc1_corr_cells}}

---

## 3. L1 — engine-decision throughput (the release measurement)

Design: one process per session, all engines timed in the **same round** against the **same** key
stream; round 0 is warm-up and is discarded; every statistic is paired by round and clustered by
session (a session is the unit of independence, so 24 rounds inside one session are 24 samples of
*that* session, not 24 independent trials — the bootstrap resamples whole sessions).

Medians over all sessions and rounds, ns/key, engine stage only:

{{table:rc1_l1}}

Per-cell detail (subject vs rival, with the bootstrap CI, how many rounds favoured the subject, and
the rival's p05/p95 so a reader can see the spread rather than one number):

{{table:rc1_cells}}

### 3.1 Does "who runs first" decide any of this?

A paired, order-rotated design is a claim, so it is tested rather than asserted: a second pass runs
the engines in a **fixed** order for the same cells, and the shift between the two passes is
published. The A/A control column (a second instance of the same engine, identical code) is the
reference for what a shift means.

* {{stat:order_effect.cells}} cells compared; median |shift| {{stat:order_effect.median_abs_shift_ns}} ns,
  max {{stat:order_effect.max_abs_shift_ns}} ns (worst cell `{{stat:order_effect.worst_cell}}`),
  against an A/A median |shift| of {{stat:order_effect.aa_median_abs_shift_ns}} ns.

{{table:rc1_order}}

### 3.2 Noise floor

The tier boundary uses 2 × the A/A **median** absolute difference, deliberately not the A/A p99: a
p99-sized band (tens of ns on this host) would relabel a real 10 % loss as a tie. The tails are
published in full in §4 and in the tables, so nothing is hidden by that choice — it only decides
the wording of the verdict.

{{table:rc1_noise}}

* clock-pair overhead (the cost of timing itself): {{stat:timer.pair_p50_ns}} ns median,
  {{stat:timer.pair_max_ns}} ns worst of {{stat:timer.reps}} reps, clock {{stat:timer.clock}},
  resolution {{stat:timer.resolution}}.
* load average when the build was recorded: {{stat:manifest.environment.loadavg_at_manifest}}; the
  campaign refuses to start a measurement pass above a load average of 2.5 and prints the value it
  waited for.

---

## 4. L2 — per-key latency distribution

Same streams, but every keystroke is timed individually and the distribution is reported.
`engine core ns/key` is the decision call alone; `prep ns/key` is the harness producing the key
stream, published separately so a reader can see how much of a "per key" number is engine.

{{table:rc1_l2}}

---

## 5. Cold start

First-round cost after process start, and the wall time to first usable output — the path an IME
takes when a user opens a document and types immediately, before any table has been touched twice.

{{table:rc1_cold}}

---

## 6. Memory

`bench_mem` counts allocations across the whole run and a 2 000 000-key soak, one engine per process
(multi-engine RSS in one process would attribute the sum to everyone). KieeKey's budget is a
constant per run, not per key — that is a design property of the core and this gate is what keeps
it true.

{{table:rc1_mem}}

* KieeKey, as-shipped: {{stat:memory.kieekey.allocs_as_shipped}} allocations over the whole soak
  ({{stat:memory.kieekey.allocs_per_megakey_as_shipped}} per megakey), and
  {{stat:memory.kieekey.rss_mib_as_shipped}} MiB resident after {{stat:memory.kieekey.soak_keys}}
  keys. The matched-minimal configuration: {{stat:memory.kieekey.allocs_matched_minimal}}
  allocations, {{stat:memory.kieekey.rss_mib_matched_minimal}} MiB.
* Note on comparability: {{stat:memory_note}}.

---

## 7. Robustness

{{stat:robust_totals.rows}} adversarial streams (long words, tone storms, backspace floods, symbol
floods, 1 000 000-key fuzz) × every engine; outcomes: {{statlist:robust_totals.outcomes}}
({{stat:robust_totals.ok}} ok, {{stat:robust_totals.crashes}} non-ok) over
{{stat:robust_totals.keys}} keys. No engine's crash here is treated as a benchmark bug: the upstream
defects found by the sanitizer pass are recorded in §10 and reported upstream, never patched inside
this repository.

---

## 8. Gain over the frozen engine, and how it is attributed

Speed work is measured against a **frozen** copy of the previous release built with the same flags in
the same binary, not against a number remembered from last month's campaign. Two `.so` builds —
`libkkbase.so` (v1.2.2 sources) and `libkkcand.so` (this tree) — go through the identical shim, so
the only difference between the columns is the engine code.

{{table:rc1_gain}}

The guard that makes that claim checkable: the shim columns must reproduce the in-process transcript
exactly, and their per-key cost must land in a plausible band around the in-process column
({{stat:attrib_guard.base_over_inprocess}}× for the baseline build, transcripts equal:
{{stat:attrib_guard.transcripts_equal}}). An earlier version of this instrument failed silently — its
`prepare()` never reached the engine, so both columns measured an early-out and the "gain" looked
tidy. `{{gate:attrib-guard}}` is now a gate precisely so that failure cannot come back as a number.

---

## 9. Where the time actually goes

Sampling profile of the same binary the campaign times, symbolised with line info from a
`-g -fno-omit-frame-pointer` twin (so the ranks come from the measured optimisation level, not from
a debug build's inlining):

{{table:rc1_profile}}

The optimisation ledger turns these shares into candidates with expected effects, and records what
each one measured — including the ones that lost, because a record of only successes teaches nothing.

---

## 10. Unavoidable limits of this campaign

* {{stat:manifest.environment.cores_online}} logical CPUs are exposed to this container
  ({{stat:manifest.environment.affinity}} usable by it) and the campaign pins to one
  ({{env:pin_note}}); the neighbour core carries the host's noise. A 2-core box cannot give the
  isolation a dedicated machine would, which is why the A/A control and the order control are
  published next to every effect rather than a claim that noise is negligible.
* `{{env:perf}}` and `{{env:valgrind}}`: hardware counters and callgrind attribution are
  **NOT AVAILABLE**; §9 is a `SIGPROF` sample (kernel-side ~250 Hz cap on `ITIMER_PROF` is bypassed
  with `setitimer(ITIMER_PROF)` at the requested rate, but sample counts remain an estimate — treat a
  1 % share as ±1 %).
* CPU frequency governor `{{stat:manifest.environment.governor}}`; transparent huge pages
  `{{stat:manifest.environment.thp}}`; CPU mitigations
  `{{stat:manifest.environment.mitigations}}`; kernel
  `{{stat:manifest.environment.kernel}}`. If these differ from the shipping If these differ from the shipping
  environment, absolute ns move; the *paired* differences in §3 and §8 are the robust part.
* KieeKey's `TextEngine` is compiled here as the library the product builds; no benchmark-only
  source is added to `src/core`, and the two vendored competitor engines are verified pristine by
  hash before and after every campaign.

## 11. Fairness, and how to falsify this report

* Every engine gets the same flags, the same streams, the same number of rounds, the same rotation,
  and the same treatment of warm-up; nothing is excluded per engine.
* Both shipping and feature-matched configurations are reported for all engines (§3 tables contain
  both), so the flattering configuration cannot be quoted alone.
* The rival set is UniKey's latest publicly available engine source (the vendored 2015 snapshot,
  labelled as a source snapshot rather than as the shipped 4.6.x binary, which is Windows-only and
  not obtainable here) and *two* OpenKey revisions — the 2.0.5 release and current master — because
  master changes the input-type model relative to 2.0.5 (`vSimpleTelex1/2`, the macro predicates that
  moved from `== vTelex` to `!= vVNI`, the new vowel patterns), and quoting only the release would
  understate the latest OpenKey behaviour. Both are reported, never averaged.
* To falsify any claim here: re-run `benchmark/scripts/campaign_rc1.sh` (the exact invocation is in
  §12) and diff `tables.md`. Every figure is a function of the committed raw artifacts; if a number
  in this document cannot be reproduced by the generator, the generator refuses to produce the file.

## 12. Reproducibility

```
git rev-parse HEAD                     # {{stat:manifest.git.git_head}}
dirty src/                             # {{env:dirty_src}}
./benchmark/scripts/build.sh --sanitizers
python3 benchmark/scripts/baseline_manifest.py
./benchmark/scripts/campaign_rc1.sh --name={{env:campaign}} --candidate \
    --sessions={{env:sessions}} --rounds={{env:rounds}} --keys={{env:keys}} \
    --words={{env:words}} --engines={{env:engines}}
```

Artifacts: `benchmark/results/{{env:campaign}}/raw/*.jsonl` (every row the statistics read),
`logs/gates.txt` (the gate lines quoted above, verbatim), `logs/*.log` (each step's stdout),
`tables.md`, `summary.json`, `environment.txt`. Engine source hashes:
`logs/engine_hashes.txt`; baseline reference hashes: `logs/baseline_reference_hashes.txt`.
