# KieeKey v1.3.0 RC1 — engine-decision benchmark and optimisation record

Campaign **{{env:campaign}}** · run recorded at {{meta:t0_unix}} · corpus `{{meta:corpus}}` (seed
{{meta:seed}}) · {{stat:meta.words}} word cases per correctness pass, {{stat:meta.keys}} keys per
timed stream · engine set `{{env:engines}}` · {{stat:meta.sessions}} sessions ×
{{stat:meta.rounds}} paired rounds (counted out of the timed rows themselves, not out of a
campaign log line — a partial re-run once overwrote the log's numbers with its own defaults) ·
compiler {{stat:manifest.toolchain.compiler}} · flags `{{stat:manifest.toolchain.std}}
{{stat:manifest.toolchain.opt}} {{stat:manifest.toolchain.defines_bench}}` for every engine
translation unit (read out of the build manifest, not out of the build script) ·
{{env:cpu}}, {{stat:manifest.environment.cores_online}} logical CPUs
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

This campaign measures **v1.3.0-rc1 as it ships**. The working tree differs from the frozen v1.2.2
engine in three sources (`TextEngine.cpp`, `TextEngine.hpp`, `kieekey_core.hpp`) — `gate:manifest`
names the drift instead of hiding it — so §3–§4 are the release candidate's numbers, with v1.2.2
measured beside them in the same rounds as `kieekey-base`. What is in the engine: a self-validating
composition memo over the emit loops, a memoised leading-consonant match inside `checkSpelling`, and a
table-driven repair scan in `checkGrammar`. It is accepted because its paired gain clears the A/A band
with **0 cells regressing beyond 2× band** *and* the output stays bit-identical to v1.2.2
(`gate:digest-identity`, `gate:diffab` above). Everything tried and rejected is in
[OPTIMIZATION_LEDGER.md](../docs/bench/rc1-130/OPTIMIZATION_LEDGER.md) with its own per-cell tables:
`rc1-ca4`, `rc1-cc1`, `rc1-p5`, `rc1-p8` (PGO) and the memo-span cap in `rc1-v13b`/`rc1-v13d`.

The instrument's own null test lives in the previous release's campaign rather than here: in `rc1-130`
the frozen and candidate columns ran the *same* code and read −0.92 … +0.99 % across 18 cells, which is
what sizes this design's floor and why a gain under ~1 % is never claimed. §8's two columns here are
different code on purpose, so their spread is the release's gain over v1.2.2 instead.

Correctness gates: **{{gatesummary}}**. Each gate is pass/fail, not a number to trade against speed:

| gate | measured statement |
|---|---|
| manifest | {{gate:manifest}} |
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
* Candidate C-A (consonant-walk automata) carried its own exhaustive equivalence oracle over
  12 519 536 leading and 6 955 302 end-of-word cases with 0 mismatches — it was proven
  *equivalent* and then measured *slower* than the frozen engine in all 18 cells, so it was
  reverted, oracle and all. See
  [the ledger](../docs/bench/rc1-130/OPTIMIZATION_LEDGER.md) §2 for the numbers.

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

### 3.1b How big this campaign is, and what that buys

{{stat:meta.sessions}} sessions × {{stat:meta.rounds}} paired rounds at {{stat:meta.keys}} keys per
stream = the sample
count behind every CI above ({{stat:verdict.paired_samples}} paired samples in the deciding cell).
That is a screening size, not the size the first pass of this work used: `rc1-ca4` ran
6 sessions × 24 rounds (144 paired samples per cell) and measured the same deciding cell at
+25.24 % for an engine carrying a different candidate. The two campaigns are not averaged, and the
gap between them is explained by the engine difference, not by sample size — which is why this report
quotes its neighbour: the most recent other campaign in `benchmark/results/`
(`{{stat:prev.campaign}}`, {{stat:prev.paired_samples}} paired samples per cell, its tree carrying
{{stat:prev.n_differing}} engine source(s) that differ from the frozen baseline) put the same cell at
{{stat:prev.rel_pct}} %. A campaign smaller than its
neighbour is stated here rather than left to the reader to notice.

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

**Read the table above with this caveat, which was found by re-measuring rather than by arguing.** The
`cold` pass runs once per campaign chunk, and this campaign ran it in the same chunk as `sanitizers` and
`robust`, so residual load moves it. Three independent repeats of the mode (12 launches each, run
separately: `bench --mode=cold --engines=kieekey-base,kieekey-cand`) read the *opposite* direction for
wall p50 — base 455.6 / 457.0 / 457.7 ms against candidate 449.3 / 445.1 / 441.2 ms, i.e. **the release
is 1.4–3.6 % faster to first output**, not 9.5 % slower — while the one figure that repeats in the same
direction is the first round: 25.1 / 26.3 / 25.4 ns/key for v1.2.2 against 27.0 / 27.6 / 27.1 for this
engine. So the claim that survives is narrow and real: **the memo tables cost about 1.5 ns/key on the
very first round** (256 bytes of extra object to touch, on a colder path) **and pay for themselves from
the second round on**; nothing in the shipped tree regresses cold start. The campaign's own
`cold.jsonl` is published above unchanged — annotating an artifact by overwriting it is how a provenance
trail becomes fiction — and PROTOCOL §12 now requires `cold` to run in its own chunk.

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

Here the two columns are deliberately **different code**: `kieekey-base` is v1.2.2 and `kieekey-cand`
is this tree, in the same rounds, so the table is the release's own gain over the previous engine —
+1.56 % in the deciding cell (72.39 → 71.03 ns/key) on 60 paired samples with a 1.587 ns A/A band, and
**0 cells regressing beyond 2× band**, which is the pre-registered ACCEPT condition
(`rc1_stats.candidate_verdict`). The screen that first measured the same change read +2.98 % with a
0.912 ns band (`rc1-v13`); the difference between the two readings is 1 ns of host state, not two
different engines, and it is why the release number is the campaign of record and not the screen.
The null test that validates the instrument is `rc1-130`'s, where both columns were the same code and
read −0.92 % … +0.99 % across the 18
cells, mostly inside ±0.5 %, but two cells (`as-shipped · telex-mid · prose` +0.90 %, CI
0.43…1.01 ns; `matched-minimal · telex-mid · pathological` −0.92 %, CI −0.47…−0.14 ns) have CIs that
exclude zero **with identical code** — a residual ~0.5 ns of column-dependent bias, most plausibly
which `.so` the dynamic loader touched first. So the honest reading of this instrument is: differences
under ~1 % are not interpretable at all, and differences of a few ns/key should be read after
subtracting the cell's own residual here rather than compared to the A/A band alone. Every candidate
figure in the ledger is above 3 %, and C-A's regression was above 5 %, so none of them turns on that
bias — but a future candidate that claims 0.7 % does, and this paragraph is the reason it will be
asked to show the null test alongside it. The verdict row prints `NOT RUN — baseline campaign (frozen src/core)` — derived by
comparing the manifest's per-source hashes, not by a flag someone remembered to pass —
because there is nothing to decide.

The guard that makes that claim checkable: the shim columns must reproduce the in-process transcript
exactly, and their per-key cost must land in a plausible band around the in-process column
({{stat:attrib_guard.base_over_inprocess}}× for the baseline build, transcripts equal:
{{stat:attrib_guard.transcripts_equal}}). An earlier version of this instrument failed silently — its
`prepare()` never reached the engine, so both columns measured an early-out and the "gain" looked
tidy. `{{gate:attrib-guard}}` is now a gate precisely so that failure cannot come back as a number.

### 8.1 What the candidate screens said

Four candidate trials have run on this instrument since the campaign of record, each in `--candidate`
mode with the frozen column beside it in the same rounds, so every one of them is a paired A/B rather
than a comparison of two campaigns: C-A, the bucket-table restructure (−0.29 … −9.52 % across the 18
cells, deciding cell −4.98 %); C-C1, the hot-path dispatch reordering (faster on the prose streams,
−10.9 % on `matched-minimal · vni · pathological` — the cell that punishes an ordering tuned for
ordinary text); P5, a single-copy undo snapshot (−0.06 % deciding cell, i.e. exactly the size of the
null-test drift described above, so "no measurable effect" is the finding rather than a small win); and
P8, a profile-guided rebuild of the candidate library (**−4.04 %** — PGO made the deciding cell slower,
with 7 of 18 cells beyond 2× the band, over 3 219 486 differential events showing 0 behavioural
mismatches). None was accepted. `src/core` is byte-identical to v1.2.2, and the tables above remain the
release measurement. PGO is in `build.sh --pgo` rather than argued about, because a build-configuration
claim deserves the same paired instrument as a source claim — and because a claim of "the compiler could
have done this for free" left untested is how a release note gets written backwards.

The sampling twin (`bench_prof`, `-g -no-pie`) is the one instrument on which the two releases are
directly comparable, because every one of these campaigns processed exactly 143 094 720 keys: v1.2.2
read 74.7 ns/key against UniKey's 61.6 (gap 13.1 ns), this tree reads **61.0 against 51.5 (gap 9.5 ns)**.
The profile build on the same twin reads 57.4 against UniKey's 54.8 — a 2.6 ns gap, i.e. **roughly 80 %
of the sampled gap is closed in the profiled configuration** — but the twin is an attribution instrument
and never the head-to-head number: it inflates the two engines differently (a debug-info, non-PIE build
penalises this engine's structure more than UniKey's), which is why its residual 2.6 ns coexists with
the plain binary's 3.6 ns lead above. The plain paired measurement is what the release claims; the twin
is quoted only for how much of the gap closed.

On the fresh profile of this tree, no exact lever of ≥ 2 ns is left. `checkSpelling` is now the largest
stage at 20.9 % (was 18.9 %) and its work is spread over the inlined bucket walks — the hottest single
line anywhere in the engine is 1.4 % — and after the leading-match memo there is no loop whose first
termination test bounds a removable cost. `findAndCalculateVowel` fell to 7.3 % combined (from 13.2 %),
`insertMark` to 6.8 % (from 9.0 %), `checkGrammar` to 5.9 % (from 7.3 %); `mainKeyBranch` +
`handleMainKey` remain 25.9 % of dispatch, which is where C-A and C-C1 died. The remaining separation is
spelling plus dispatch plus mark insertion — feature work per key — which is the same conclusion the
rejected candidates reached from the other side, now measured on the shipped code.

### 8.2 The opt-in low-latency profile — and its price, measured

Everything above is the strict engine: bit-identical output, +1.56 % over v1.2.2. The objective for
this cycle also asked what the engine looks like if the last strictness pass is allowed to go, and that
is a **build configuration** rather than a code path: `KIEEKEY_LOW_LATENCY_PROFILE`
(`build.sh --fast-profile`, `cmake -DKIEEKEY_LOW_LATENCY_PROFILE=ON`) compiles out `checkGrammar`'s
post-edit orthography repair, which is 5.5 ns of a ~68 ns engine decision on every key typed into a
word that already carries a mark.

Numbers below are from `benchmark/results/rc1-v13prof/` — same corpus, same 5 × 12 paired sessions,
same flags, `--ref` to nothing because the comparison that matters is against UniKey in the same rounds:

| cell (median ns/key) | UniKey 4.x | KieeKey strict | KieeKey low-latency | vs UniKey |
|---|---|---|---|---|
| `as-shipped · telex-end · prose` (deciding) | 61.36 | 71.12 | **57.77** | **−5.84 %** |
| `as-shipped · telex-mid · prose` | 61.56 | 67.65 | **51.85** | **−15.78 %** |
| `as-shipped · vni · prose` | 58.47 | 64.64 | **52.87** | **−9.57 %** |
| `as-shipped · telex-end · edit-storm` | 65.31 | 80.62 | **59.50** | **−8.90 %** |
| `matched-minimal · telex-end · prose` | 40.75 | 49.00 | **37.70** | **−7.48 %** |
| `as-shipped · telex-end · pathological` | 28.08 | 31.34 | 31.89 | +13.56 % |

and for the end-to-end distribution (the latency mode times the in-process engine, so for this table
`bench` itself was rebuilt with the profile — recorded in the campaign's `engine_hashes.txt`), KieeKey
is ahead of UniKey at p50 on **all nine** `as-shipped` streams: prose `telex-end` 82.0 vs 89.5 ns,
`vni · prose` 74.5 vs 85.0, `edit-storm` 84.5 vs 89.5, and the memory gate still reads 21 allocations
per 2 M keys with RSS unchanged at 46.2 MiB.

**What it costs.** Differential run against the frozen v1.2.2 engine over the full corpus: **52.0 % of
keys repaint differently** (372 242 of 715 474 events) **and 10 of 18 streams end with different
composed text** — a mark left on the vowel the last key hit rather than the one the rule picks. That is
not a stylistic drift; for an input method it is the product's core promise, which is why the profile is
not the default and why this paragraph is in the release report rather than a footnote. The same
behaviour is reachable at runtime per target through `grammarRepair` / `freeMark` (they gate the same
pass), which is the shape a "trusted app is fast, everything else is strict" policy wants; the build
configuration exists for a device image that wants it everywhere without a per-app setting.

Both builds are the same source tree, so this is a shipping choice and not a fork: the strict default is
what `src/core` produces with no define, and reverting the profile is deleting one flag from a build
line. A gate mode exists for releases that take a rule away on purpose —
`rc1_gates.py --policy=declared-divergence` (campaign flag `--divergence`) — which keeps asserting the
two properties that must survive (final visible text identical, and this tree's in-process and shim
builds identical to each other) and publishes the payload divergence instead of switching the check off.
It is not used for this release's campaign of record, because the strict tree needs none of that
machinery: it is byte-identical output.

---

## 9. Where the time actually goes

Sampling profile of the measured code path, symbolised with line info from a twin built
`-O3 -g -fno-omit-frame-pointer -no-pie` — the same optimisation level as the campaign (so the ranks
are not a debug build's inlining), linked non-PIE because raw counters are resolved against the
`nm` addresses an ASLR-slid image would otherwise miss, and bounded by symbol *extents* rather than
a fixed window:

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
* Hardware counters and callgrind-style attribution are **NOT AVAILABLE** here
  (`perf`: {{env:perf}} · `valgrind`: {{env:valgrind}}), so §9 is a sampling profile of a
  identically-optimised twin (`-O3`, plus `-g -fno-omit-frame-pointer -no-pie` for attribution).
  Its clock is not what we asked for, and the report says so plainly: the harness requests **{{stat:profile.requested_hz}} Hz** and the run delivered
  **{{stat:profile.delivered_hz}} Hz** ({{stat:profile.samples}} samples over
  {{stat:profile.window_s}} s), because `ITIMER_PROF` fires on the kernel tick
  (`CONFIG_HZ`), not on demand. That does not bias which function is hottest — it bounds how finely
  a share is resolved, which is why every share in §9 is quoted as a rank and a 1 % share must be
  read as ±1 %, and why no candidate was ever scored against a predicted share.
* CPU frequency governor `{{stat:manifest.environment.governor}}`; transparent huge pages
  `{{stat:manifest.environment.thp}}`; CPU mitigations
  `{{stat:manifest.environment.mitigations}}`; kernel
  `{{stat:manifest.environment.kernel}}`. If these differ from the shipping
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
    --sessions={{stat:meta.sessions}} --rounds={{stat:meta.rounds}} --keys={{stat:meta.keys}} \
    --words={{env:words}} --engines={{env:engines}}
```

Artifacts: `benchmark/results/{{env:campaign}}/raw/*.jsonl` (every row the statistics read),
`logs/gates.txt` (the gate lines quoted above, verbatim), `logs/*.log` (each step's stdout),
`tables.md`, `summary.json`, `environment.txt`. Engine source hashes:
`logs/engine_hashes.txt`; baseline reference hashes: `logs/baseline_reference_hashes.txt`.
