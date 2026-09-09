# rc1-m6 benchmark-accounting audit

This artifact audits the reviewer-reported inconsistency in the old `rc1_gain` table. Raw timing JSONL files were preserved; only derived `summary.json` and `tables.md` were regenerated with explicit statistic names.

## Root cause

The old table displayed two **descriptive per-engine medians** next to columns named `gain ns` and `gain %`. Those `gain` columns were **not** computed by subtracting the displayed medians. They were the campaign's paired estimator: for each matched `(session, round)` sample, compute `candidate - frozen`, take the median of those paired differences, then negate it so positive means the candidate is faster.

That paired estimator is defensible for the ACCEPT/REJECT rule because it preserves the randomized paired design and cancels same-round drift. The presentation was misleading because the table header did not state that `gain` was paired, and it did not also show the descriptive median subtraction implied by the two adjacent medians.

Classification: **statistically valid but poorly presented**. The old values were not the difference of displayed medians and should not have been labelled simply `gain ns/%`.

## Deciding-cell worked example

Cell: `as-shipped | telex-end | prose`

Raw source: `benchmark/results/rc1-m6/raw/tput_s*.jsonl`, non-warm rows, engines `kieekey-base` and `kieekey-cand`, matched by `(session, round)`.

| quantity | formula | value |
|---|---|---:|
| baseline median B | median of frozen v1.2.2 `engine_ns_per_key` samples | 240.90780 ns/key |
| candidate median C | median of candidate `engine_ns_per_key` samples | 80.75605 ns/key |
| A. descriptive median improvement | `B - C` | 160.15175 ns/key |
| B. descriptive median improvement % | `(B - C) / B * 100` | 66.47844 % |
| C. paired delta median | `median(candidate_i - baseline_i)` over 60 matched rounds | -2.54665 ns/key |
| paired median gain | `-paired_delta_median` | 2.54665 ns/key |
| D. paired relative effect | `paired_median_gain / B * 100` | 1.05711 % |
| paired gain 95% CI | bootstrap CI for paired median gain, resampling whole sessions | 1.30400…6.46955 ns/key |

Why `240.91 → 80.76` could previously appear beside `2.55 ns / 1.06%`: the latter was the paired median gain, not the descriptive median subtraction. The old row mixed two different estimators without naming them.

## Corrected table semantics

`rc1_gain` now publishes both concepts:

| concept | label in regenerated table | sign convention | use |
|---|---|---|---|
| descriptive latency medians | `v1.2.2 median ns/key`, `candidate median ns/key` | smaller is faster | observed per-engine distributions |
| descriptive median subtraction | `median improvement ns/%` | positive means candidate median is lower | explains the displayed medians |
| paired estimator | `paired median gain ns`, `paired relative effect %` | positive means candidate faster in matched rounds | inferential candidate ACCEPT/REJECT |
| CI | `95 % CI for paired gain ns` | applies only to paired median gain | bootstrap over sessions |

## Before / after for the reviewer row

| version | displayed medians | old ambiguous columns | corrected descriptive improvement | corrected paired estimator |
|---|---|---:|---:|---:|
| before | `240.91 → 80.76` | `gain ns=2.55`, `gain %=1.06` | not shown | not explicitly labelled |
| after | `240.91 → 80.76` | removed | `160.15 ns`, `66.48%` | `paired median gain=2.55 ns`, `paired relative effect=1.06%` |

## Validation

`benchmark/scripts/check_rc1_stats_consistency.py --results=benchmark/results/rc1-m6` independently recomputes every `rc1_gain` row from raw `tput_s*.jsonl` and fails if:

* displayed medians differ from raw per-engine medians;
* descriptive median improvement is not `B - C`;
* paired median gain is not the negated median of matched per-round deltas;
* the table regresses to the ambiguous `gain ns/gain %` header;
* the methodology block is missing.

Validation output for this campaign:

```text
[stats-consistency] example as-shipped|telex-end|prose: A diff-of-medians=160.15175 ns; B median-improvement%=66.47844%; C paired_delta_median=-2.54665 ns (cand-base); D paired_relative_effect=1.05711% over baseline median; base_median=240.90780; cand_median=80.75605; pairs=60
[stats-consistency] OK: recomputed 18 cells from raw tput artifacts in benchmark/results/rc1-m6
```

## Performance-result impact

The underlying raw benchmark measurements did **not** change. Correctness results did **not** change. The candidate ACCEPT decision still uses the same paired median estimator and CI, now explicitly named. The fix changes reporting/statistical accounting only: it makes descriptive and paired quantities auditable instead of conflated.
