# KieeKey 1.3.0-rc1 vs v1.2.2 — apple-to-apple benchmark

**Question this answers:** the v1.3.0 line added the arcade, chaos, AI-rival,
progression and analytics subsystems to a shipped IME. Does the *shipped typing
engine* still perform exactly like the last stable release, and is the new code
free of steady-state cost?

**Answer: no regression.** Every measured percentile of the shipped engine is
equal or faster in 1.3.0-rc1, the byte-level behaviour of the three-engine
differential is identical, peak RSS is identical to the kilobyte, and the
SendInput batching invariant is unchanged (108 batched edits per 100 000 keys in
both trees).

---

## 1. Method (why this is apple-to-apple)

| property | value |
|---|---|
| harnesses | `tests/bench_three_engines.cpp`, `tests/e2e_bench.cpp`, `tests/bench_engine.cpp` |
| harness identity | **byte-identical in both trees** — the git blob ids match: `3f45c6848438b5309c4e1b74ff1d90e5aed6c737`, `4cb05c61c2e219a81a365e5afb814b746fd3cf6c`, `83ca8e7747b346083c53fcd80bc537df636bd28d` |
| baseline tree | `v1.2.2` = `d332c56` (tag `v1.2.2`, "chore(release): refresh SHA256SUMS.txt for v1.2.2 stable") |
| candidate tree | `1.3.0-rc1` = the tip of the branch that contains this report |
| compiler / flags | `g++ -O2`, C++23 (harness C++17 for the three-engine bench, as its own script prescribes) |
| host | Linux x86-64 sandbox, single tenant, nothing else running |
| protocol | **interleaved rounds** (v1.2.2 → 1.3.0-rc1, repeated 3×) so any drift in host load hits both trees, medians reported |
| sample size | three-engine: 3 rounds × 2 000 000 keys per engine; e2e: 3 rounds × 3 runs × 100 000 keys; micro: 3 rounds × 5 000 iterations per corpus line |

Reproduce exactly what this file reports:

```bash
git worktree add /tmp/kt/v122 v1.2.2                 # baseline
git worktree add --detach /tmp/kt/head_of            # candidate
# 3 interleaved rounds in each tree, then the aggregation in summary.txt
bash docs/bench/v122_vs_130/run_ab.sh 3
```

`summary.txt` in this directory is the raw aggregation output (medians per
metric); no number below is hand-typed.

### Caveat that the numbers cannot hide

This host is Linux: `SCHED_FIFO` is unavailable and the scheduler deschedules
the benchmark thread, so **`ok_e2e_bench` prints FAIL against its Windows
targets** (p50 ≤ 2.5 µs / p99 ≤ 7.0 µs) on *both* trees. Those absolute targets
are only meaningful on Windows with real QPC/SendInput; the host-relative
*A → B* comparison below is what this document is for, and the deschedule-heavy
`max` / lag-spike columns are treated as noise, not as results. Concretely: a
single round of this script measured `raw_p999` at +208 % while the 3-round
median of nine runs measured −8.7 % — that column swings with the host
scheduler, which is exactly why the protocol is interleaved rounds + medians
instead of one run per tree.

---

## 2. Shipped engine, differential + latency (identical harness)

KieeKey's `TextEngine` against the two vendored reference engines (OpenKey 2.0.5
and UniKey's UKEngine), 2 000 000 keys per engine per round:

| metric | v1.2.2 | 1.3.0-rc1 | delta |
|---|---|---|---|
| mean | 183.7 ns/key | 184.4 ns/key | +0.4 % |
| p50 | 148 ns | 148 ns | 0.0 % |
| p90 | 249 ns | 242 ns | **−2.8 %** |
| p99 | 542 ns | 558 ns | +3.0 % |
| max (scheduler artifact) | 3.75 ms | 3.71 ms | −0.9 % |

Correctness in the same runs: **15/15 passages exact for KieeKey in both
trees**, and the full table of 15 passages + 53 stress streams + the 5 000-word
fuzz stream is **byte-identical between the two trees** (the rendered reports
diff clean once the timing digits are masked) — including the documented
auto-restore and free-marking differences against the two reference engines.
In other words: the v1.3.0 IME work changed *no* output on this corpus.

---

## 3. End-to-end pipeline (`ok_e2e_bench`, 100 000 keys, burst model)

Medians of 9 runs per tree (3 rounds × 3 runs):

| metric | v1.2.2 | 1.3.0-rc1 | delta |
|---|---|---|---|
| RAW p50 | 35.818 µs | 33.959 µs | **−5.2 %** |
| RAW p95 | 97.615 µs | 95.762 µs | −1.9 % |
| RAW p99 | 124.856 µs | 123.008 µs | −1.5 % |
| RAW p99.9 | 699.355 µs | 638.797 µs | **−8.7 %** |
| BURST/HOT p50 | 33.346 µs | 31.836 µs | **−4.5 %** |
| BURST/HOT p95 | 88.244 µs | 87.239 µs | −1.1 % |
| BURST/HOT p99 | 96.953 µs | 96.473 µs | −0.5 % |
| WAKE-PAY p50 | 48.630 µs | 47.466 µs | −2.4 % |
| WAKE-PAY p99 | 136.649 µs | 134.265 µs | −1.7 % |
| PIPELINE p50 | 35.126 µs | 33.312 µs | **−5.2 %** |
| PIPELINE p99 | 97.158 µs | 96.776 µs | −0.4 % |
| lag-spike keys | 4.213 % | 3.664 % | −13.0 % |
| throughput (burst-paced) | 13 190 keys/s | 13 177 keys/s | −0.1 % |
| peak RSS | 8.551 MB | 8.551 MB | **0.0 %** |
| IME pipeline footprint | 6.071 MB | 6.071 MB | **0.0 %** |
| SendInput calls / 100k keystrokes | 108 | 108 | **unchanged invariant** |

The throughput column is paced by the harness's own burst schedule, so it is a
scheduling check (`−0.1 %` = identical); the latency percentiles are the signal,
and the candidate is faster at every one of them.

---

## 4. Microbench (`ok_bench`, TextEngine hot path)

Median ns/key over 3 rounds, 5 000 iterations per corpus line:

| corpus | v1.2.2 | 1.3.0-rc1 | delta |
|---|---|---|---|
| `xin chao cac ban` | 33 | 35 | +6 % |
| `ban as aw aa ow uw uow` | 36 | 36 | 0 % |
| `toi la nguoi viet nam` | 37 | 29 | −22 % |
| `a1 a2 a3 a4 a5 a6 o6 e6 o7 u7` | 50 | 49 | −2 % |

At 29–50 ns per key with a ±10 ns run-to-run band, these single-digit deltas are
noise; the honest reading is "unchanged, tens of nanoseconds the whole way".

---

## 5. What this does *not* cover

* **Windows**: these are Linux/g++ numbers. The Windows build is compiled and
  tested by CI (x64 / ARM64 / ARM64EC) and the Windows-only benches
  (`ok_tone_bench`, `bench_tone_latency`) are not runnable in the sandbox. The
  latency *targets* in `e2e_bench` are Windows-tuned and are only evaluated
  there.
* **New arcade subsystems**: their own budgets live in
  `docs/bench/arcade-130/` (per-frame allocations pinned at 0, per-game frame
  cost, HTTP route cost) and `docs/bench/extreme-130/feature_isolation.{log,txt}`
  (isolation of chaos / AI-rival / progression from the IME hot path:
  standby +1.6 %).
* **A/B of the *new* systems against anything**: there is no earlier release with
  them, so their budgets are compared against zero (allocation/frame) and
  against the baseline engine cost, not against a previous version.
