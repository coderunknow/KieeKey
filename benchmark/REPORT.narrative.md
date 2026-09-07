# KieeKey v1.2.2 vs UniKey vs OpenKey — engine benchmark

Campaign **{{env:campaign}}** · measured {{meta:t0_unix}} · corpus `{{meta:corpus}}`
(seed {{meta:seed}}; {{stat:corpus.corpus_used}} word cases selected from
{{stat:corpus.corpus_lines_seen}} corpus lines; {{stat:protocol.keys_per_stream}} keys per
latency stream over {{stat:protocol.latency_rounds}} interleaved rounds) · host:
{{env:cpu}}, {{env:cores_total}} logical CPUs, every run pinned to core
{{env:pinned_core}} · {{env:compiler}}.

Every number on this page is spliced out of `benchmark/results/{{env:campaign}}/`
by `benchmark/scripts/make_report.py`. Nothing here is typed by hand; re-running
the generator against the committed artifacts reproduces this file byte-for-byte.
Method, fairness rules and the exact option matrices are in
[benchmark/README.md](README.md).

---

## 0. Scope

Measured, for four engine builds in one process on one core:

* **correctness** — the text an engine actually leaves on screen after an
  identical keystroke stream, judged against human-written Vietnamese target
  text (never against another engine's output);
* **engine-decision latency per key**, nested into adapter / engine / consumer
  stages, with warm-up, interleaved rounds and rotating order;
* **memory** — RSS per engine in its own process, plus allocation counters;
* **robustness** — hostile streams in forked children, the long random-key fuzz
  stream, and a lossless-edit invariant;
* **ASan + UBSan + LSan** findings, attributed to the source tree they come from.

Deliberately **not** measured, and never estimated:

* UniKey's Windows hook path and OpenKey's TSF/UI path. They cannot run here, so
  no OS-side figure is claimed for them; comparing them to KieeKey would be a
  guess, not a measurement.
* KieeKey's producer→consumer pipeline (Windows-only, `docs/bench`). Its own
  repository campaign is quoted in §4.4 with its own noise band, clearly separated
  from this table.
* Anything tuned for KieeKey: no engine's source was touched, no flag was picked
  to flatter one column, and both the shipped-default and the feature-matched
  configuration of every engine are reported.

## 1. Headline

| question | this campaign's answer |
|---|---|
| Who types Vietnamese most accurately (full corpus, Telex, shipped defaults)? | KieeKey {{corr:as-shipped|telex-end|words|kieekey|exact_rate}} vs the best engine {{corrbest:as-shipped|telex-end|words|exact_rate|engine}} at {{corrval:as-shipped|telex-end|words|exact_rate}} — a gap of well under one percentage point |
| Who is fastest per key (engine-decision, shipped defaults)? | {{latbest:as-shipped|telex-end|prose|full_median}} at {{latbestval:as-shipped|telex-end|prose}} ns/key; KieeKey {{lat:as-shipped|telex-end|prose|kieekey}} ns/key |
| Is KieeKey's advantage over OpenKey real? | {{verdict:as-shipped|telex-end|prose|openkey-2.0.5}} |
| Is KieeKey behind UniKey real? | {{verdict:as-shipped|telex-end|prose|unikey-4.x}} |
| Does KieeKey lose characters or crash under abuse? | no — the soak, the fuzz stream and every hostile stream completed intact, and the lossless-edit invariant holds (§3.4, §6) |
| Sanitizer findings in KieeKey's own sources | {{stat:sanitizers_own_findings.kieekey}} (hostile streams completed {{stat:sanitizers_streams.kieekey}}) |

Significance rule fixed before measuring: a difference is only reported as a
difference if `|Δ| > max(2 × in-process A/A band, 2 × campaign-to-campaign drift,
1 ns)`. The in-process A/A band (two instances of the *same* KieeKey build in the
same process, same streams, same rounds) is **{{band}} ns/key** at the 99th
percentile of |Δ| (median {{band_med}} ns, {{band_n}} round pairs), and the
threshold used below is `{{threshold:as-shipped|telex-end|prose}}` ns/key for the
headline stream. Differences smaller than that are called *within noise* even
when they look consistent. The instrument's resolution is not a constant on this
host: the reference campaign measured the same band at {{bandref}} ns/key, so the
band quoted here is the one this campaign actually ran under — it is not taken
from the quietest run available.

## 2. Provenance

| column | what it is |
|---|---|
| `kieekey` | `src/core/TextEngine.cpp` of this checkout (v1.2.2 Stable), compiled unmodified |
| `kieekey-aa` | a second instance of the same engine — the A/A control |
| `openkey-2.0.5` | `tests/reference/openkey-2.0.5/engine`, pristine, latest **published release** |
| `openkey-master` | `benchmark/reference/openkey-master/engine`, pristine copy of upstream `master` (89c2fd3, 2026-06-15) — newer code than the release: `vSimpleTelex1/2` input types, rewritten macro and standalone-char paths |
| `unikey-4.x` | `tests/reference/unikey` — `UKEngine`, the newest UniKey engine **source** published anywhere (2015 CVS snapshot). The shipped Windows binary (4.6.250531) is closed source and is *not* what this column measures |

Hashes of every input file, recomputed at run time:

{{table:provenance}}

`results/{{env:campaign}}/logs/reference_integrity.txt` holds the `sha256sum -c`
verification of the vendored master tree.

## 3. Correctness

The same parsed keystroke vector is fed to all engines per case; the engine's own
edit contract is applied verbatim to the app-visible text; the result is compared
byte-for-byte with the intended text. Cases: every single-token Vietnamese word in
the corpus that the model can encode: {{stat:corpus.corpus_used}} words selected from
{{stat:corpus.corpus_lines_seen}} corpus lines ({{stat:corpus.skipped_multitoken}} multi-token
lines, {{stat:corpus.skipped_too_long}} too-long lines, {{stat:corpus.skipped_unencodable}}
out-of-model lines — all counted in the `corpus` artifact rows), plus the
{{corr:as-shipped|telex-end|passages|kieekey|total}} human-authored passages.

Deterministic and engine-independent: the same stream fed to the same engine
gives the same output digest in both campaigns, on every row of every engine.

{{table:correctness}}

Read the numbers with the *noise* caveat in mind: correctness is deterministic.
The A/A control reproduces every engine's output digest exactly, and the two
campaigns agree on every config × method × corpus × engine row *including* the
digests — so a one-word gap between engines is a real behavioural difference, not
a measurement artefact. But the corpus is still one
sample of Vietnamese text, so treat "who is most accurate" as "who handles more of
*this* corpus" and look at the categories below for *why* they differ.

### 3.1 What kind of differences

{{table:verdicts}}

`restored` = the engine gave up on the word and left the raw keys (all three
engines with a non-Vietnamese auto-restore do this on the same loan-words);
`letters differ` = letters were dropped/added — the category where OpenKey's
spell-checker rewrites foreign words (`afghani` → `àghani`, `arsenic` → `áenic`)
while KieeKey and UniKey leave them alone; `tone`/`hat` categories are empty for
every engine on this corpus, i.e. no engine put a mark on the wrong vowel *within*
a word it accepted — the disagreements are about whether to accept the word at all.

### 3.2 Cross-engine agreement (not correctness)

{{table:agreement}}

### 3.3 Worked divergences

The full list is in `raw/correctness.jsonl` → `example` rows; the table keeps a
bounded slice of them, gated on at least one engine disagreeing with the intended
text:

{{table:examples}}

### 3.4 Engine-independent invariants

Lossless edit invariant — every stream is a pure ASCII edit sequence (letters,
hats, tones, backspaces) whose intended end state is computable without any
engine; the visible text must equal it, and no character may ever be lost:

{{table:invariant}}

Macro expansion (`tn` + space with a one-entry table installed per engine):

{{table:macro}}

The raw random-key stream (its length is the `keys` column), then the digest of
the text that survived:

{{table:fuzz}}

Stress streams (identical text across engines is *not* correctness, it is a
consistency check): {{stat:stress.all_engines_identical}}/{{stat:stress.streams}}
streams produce identical text on all four engines, {{stat:stress.divergent}}
diverge — each divergence is listed with its text in the `stress` table.

{{table:stress}}

## 4. Latency — engine decision per key

`full ns/key` = adapter (`prepare`) + engine call (`invoke`) + consumer
(`apply`: materialise the replacement and update the visible text), which is the
same three-stage shape for every engine. `engine-only` isolates the engine entry
point; `engine-core-net` subtracts the adapter from it; percentiles come from
per-key samples, and the measured clock-pair overhead is published in every row
of `raw/latency.jsonl` as `clock_pair_overhead_ns` (it is subtracted nowhere —
it is the floor that makes very small differences unmeasurable here).

{{table:latency}}

Streams: `prose` (chained corpus words, natural mark density), `edit-storm`
(hat/tone re-editing, backspaces), `pathological` (long unbroken buffer,
suppressed keys). Every stream is generated once and shared byte-for-byte by all
engines; `out_digest` in the raw artifact proves each engine consumed the same
input.

### 4.1 Headline, per pair

{{table:noise}}

### 4.2 Reproducibility across campaigns

Campaign B is an independent repeat of the same protocol; the drift of each
engine's own headline between the two campaigns is what the significance
threshold is built from:

{{paired}}

### 4.3 Asymmetries inside the timed region, measured

The OpenKey columns are driven through a `dlopen`ed shared object (both
generations define identical global engine state, so they cannot be linked into
one binary); the cost of that indirection is measured, not assumed:

{{table:shim_overhead}}

UniKey performs its own charset conversion inside the timed call (that is its
design, and the other engines do their conversion in `apply`); KieeKey's `apply`
stage materialises a UTF-16 replacement through `replacementUtf16`, which is
allocation-free by contract. No engine gets a stage the others do not have.

### 4.4 KieeKey's own end-to-end pipeline (separate, repo data)

Not part of the four-way table, because no competitor equivalent exists here: the
repository's own v1.2.2 Stable end-to-end campaign measured
`pipeline_us.p50 = {{ext:docs/bench/stable-122/ab_rc4_vs_stable_e2e_8runs.txt::pipeline_us\.p50.*?Stable=([0-9.]+)::1}} µs`
for the shipped producer→consumer path, on a host where the same binary re-run
varies by up to `{{ext:docs/bench/stable-122/aa_control_noise_band.txt::pipeline_us\.p50.*?\|max\|=([0-9.]+)%::1}} %`
(A/A). Those are Windows-side numbers from `docs/bench/stable-122/` quoted as-is,
with the source files named so they can be re-derived.

## 5. Memory

Each engine measured in its own process (shared-process RSS attributes
first-touch pages to whichever engine ran before it) over the full soak. Allocation
counters are the primary metric because they cannot be polluted by page
attribution; RSS is the secondary, physical confirmation:

{{table:mem}}

How to read it: `allocs during soak` is what each engine asks the allocator for
while typing, and `allocs/key` is the same divided by the soak length. KieeKey and
UniKey type the whole soak with a few dozen allocations in total (allocation-free
replacement buffers); the OpenKey generations allocate roughly one block per key.
Within one process the two configurations run sequentially, so a
`matched-minimal` row's `RSS after init` still carries the first config's pages —
compare `growth` and the allocation columns across engines, not that column.
Growth is dominated by each engine's own word/history buffers, and OpenKey's is
also where its per-key allocations land.

## 6. Robustness

{{table:robust}}

Sanitizer findings, attributed by the source tree the report points at — the
subject is not exempt, and a finding in a competitor's tree is not charged to
KieeKey either:

{{table:sanitizers}}

These are **reported, not fixed**: the vendored engines are compiled as-is.
`results/{{env:campaign}}/logs/san_*.log` and `logs/sanfix_*.log` keep the full
traces, and the per-engine logs are deliberately separate files so a reader can
check the attribution themselves. Provenance note: the two sanitizer passes were
re-run standalone (identical options, per-engine filter) after the rest of the
campaign, because a clean rebuild removed the sanitizer binaries in between;
`run_campaign.sh --steps=sanitizers` reproduces them inside one campaign run.

## 7. Environment

{{table:environment}}

## 8. Limitations a reader should weigh

1. **Engines, not products.** UniKey's and OpenKey's OS integration is not
   measured, so this says nothing about keystroke-to-screen latency of the
   installed applications — only about the engine code each is built on.
2. **UniKey's column is a 2015 engine-source snapshot** (the newest published
   source); the shipped 4.6 binary may differ. This is stated wherever UniKey
   numbers appear.
3. **The corpus is one sample.** {{stat:corpus.corpus_used}} single-token words
   plus {{corr:as-shipped|telex-end|passages|kieekey|total}} passages. The
   auto-restore behaviour dominates the difference between "exact" and "wrong",
   so the ranking is sensitive to how many foreign tokens a corpus contains.
4. **Host noise floor.** A 2-vCPU VM with no cpufreq control; the A/A band is
   {{band}} ns/key (99th pct) and the report refuses to call anything inside it a
   difference. Percentile tails (p99/max) are visibly noisier than medians and
   are published rather than smoothed.
5. **Consumer model.** Each engine's front-end behaviour (what the app does with
   the edit it asks for) is modelled per its own API contract, documented in
   `harness/engines.hpp` and cross-checked by the lossless-edit invariant. A
   different (also legitimate) front-end model could shift a small number of
   word cases; it cannot shift the latency tables.
6. **Key model.** Keystrokes come from an encoder derived from Unicode's letter
   decomposition, verified by an independent Python re-implementation
   (`logs/encoder_check.txt`: all {{ext:benchmark/results/campaign-a/logs/encoder_check.txt::table rows checked: ([0-9]+)::1}} table rows and {{ext:benchmark/results/campaign-a/logs/encoder_check.txt::stream rows checked: ([0-9]+)::1}} stream rows agree with it). VNI
   digit tables genuinely differ between engines; the difference is reported as
   behaviour, not hidden by re-encoding per engine.

## 9. Re-derive everything

```sh
./benchmark/scripts/build.sh --sanitizers
./benchmark/scripts/run_campaign.sh --campaign=campaign-a
./benchmark/scripts/run_campaign.sh --campaign=campaign-b \
    --steps=encoder,selftest,correctness,latency,mem,overhead,robust
python3 benchmark/scripts/make_report.py --campaign=campaign-a --ref=campaign-b
```
