# KieeKey 4-way engine benchmark

Apples-to-apples measurement of **KieeKey v1.2.2 Stable** (`src/core/TextEngine.cpp`,
this working tree) against **OpenKey 2.0.5**, **OpenKey latest code (master)** and
**UniKey 4.x `UKEngine`**, on Linux, headless, with an explicit noise budget.

Everything here is additive: the benchmark never modifies `src/`, `tests/`, the
vendored reference engines or the project build. Nothing in this directory is
built by the top-level `CMakeLists.txt`.

```
benchmark/
  harness/            C++ harness (one binary, five engine instances)
  reference/          pristine copies of competitor sources + provenance
  scripts/            build, campaign runner, statistics, report generation
  results/<campaign>/ raw JSONL artifacts, logs, tables.md, summary.json
  REPORT.md           GENERATED report — numbers spliced from results/
  REPORT.narrative.md the prose REPORT.md is assembled from (placeholders only)
```

## What is measured

| axis | how |
|---|---|
| **Correctness** | every engine is fed the *same* keystroke stream, per case; the resulting visible text is compared with the human-written intended text |
| **Latency** | engine-decision cost per key, headless, three nested stages (adapter / +engine call / +consumer apply) |
| **Memory** | RSS after init and after a 2 M-key soak; allocation counters in `bench_mem` |
| **Robustness** | 11 hostile streams in forked children, 200 000-key fuzz run, digest agreement |
| **Behaviour invariants** | lossless-edit invariant, macro expansion, stress-stream agreement |
| **Sanitizers** | ASan+UBSan+LSan build of *all four* engines on the same streams, per-engine logs |

## What is deliberately **not** measured

* **Competitors' OS text path.** UniKey's Windows hook and OpenKey's TSF/UI layer
  are not measured and never estimated — they cannot run here. Only the engine
  each product is built on is compared, plus KieeKey's own `TextEngine`.
  KieeKey's producer→consumer pipeline (Windows-only) is *not* re-measured here
  either; the repository's own figures live in
  [`../docs/bench/stable-122/ab_rc4_vs_stable_e2e_8runs.txt`](../docs/bench/stable-122/ab_rc4_vs_stable_e2e_8runs.txt)
  and are quoted in the report as repo data with their own A/A band
  ([`aa_control_noise_band.txt`](../docs/bench/stable-122/aa_control_noise_band.txt)).
* **End-to-end keystroke-to-screen latency** of any engine. It is not measurable
  without the respective GUI, so no column has it.
* Any tuning of KieeKey. This benchmark only compares; it does not optimise.

## Engines and provenance

| column | source | version evidence |
|---|---|---|
| `kieekey` | `src/core/TextEngine.{hpp,cpp}` of this checkout | `CMakeLists.txt` `VERSION 1.2.2`, HEAD `3047143` |
| `kieekey-aa` | same engine, second instance in the same process | A/A control for the noise band |
| `openkey-2.0.5` | `tests/reference/openkey-2.0.5/engine` (pristine) | GitHub release `2.0.5` — latest published release |
| `openkey-master` | `reference/openkey-master/engine` (pristine copy, `sha256sum -c` verified) | upstream `master` @ `89c2fd3`, 2026-06-15 — newer than 2.0.5 (`SimpleTelex1/2` input types, macro and vowel-rule changes) |
| `unikey-4.x` | `tests/reference/unikey` (pristine) | UKEngine from the 2015 CVS snapshot; the newest UniKey engine source published anywhere (latest Windows release is 4.6.250531, closed source). Reported as *engine source snapshot*, never as "the latest binary" |

`results/<campaign>/logs/input_hashes.txt` holds a sha256 of every input file;
`reference/openkey-master/PROVENANCE.md` records where the master copy came from.

## Build

```sh
./benchmark/scripts/build.sh                 # bench, bench_mem, libok205.so, libokmaster.so
./benchmark/scripts/build.sh --sanitizers    # + bench_san (ASan+UBSan+LSan)
```

Same `-std=c++17 -O2 -Wall -Wextra` for every engine, one compiler, one
translation-unit layout; the two OpenKey generations are packaged as two `.so`s
loaded with `dlopen(RTLD_LOCAL)` and driven by the *same* shim source
(`harness/ok_shim.cpp`), because both define identical global engine state.

## Run

```sh
./benchmark/scripts/run_campaign.sh --campaign=campaign-a            # full campaign
./benchmark/scripts/run_campaign.sh --campaign=campaign-b --steps=encoder,selftest,correctness,latency
python3 benchmark/scripts/summarize.py  --results=benchmark/results/campaign-a
python3 benchmark/scripts/make_report.py --campaign=campaign-a --ref=campaign-b
```

The runner pins to one core (`taskset`), records the host freeze (CPU model,
MHz, governor, ASLR, cgroup limits, load) and writes one JSON-lines artifact per
mode. Every statistic in `REPORT.md` is recomputed from those artifacts — the
report contains no hand-typed number.

## Method

### Key model (`harness/viet.hpp`)

Intended text (corpus `tests/data/viet74k.txt`, and the 21 human-authored
passages in `harness/corpus.hpp`) is turned into keystrokes by a table built
from Unicode's own letter decomposition (`scripts/gen_table.py`, 148 entries:
the six vowels and `y` with circumflex/breve/horn and the five tones, plus
`d`/`đ`). Two conventions are exercised, both used by real users:

* `telex-end` — tone key at the end of the word (`bàn` → `banf`),
* `telex-mid` — tone key right after the vowel that carries it (`bàn` → `bafn`),
* `vni` — digit keys; the digit tables differ per engine (KieeKey and OpenKey
  2.0.5: `6` â/ô, `7` ê, `8` ơ/ư **and** ă, `9` đ, `0` clear; UniKey's
  `VniMethodMapping`: `6` roof-all, `7` horn, `8` bowl, `9` dd). One shared
  stream is typed for all engines per method, so a table difference shows up as
  behaviour difference, not as a hidden re-encoding.

The model is *cross-checked, not trusted*: `scripts/check_encoder.py`
re-implements it in Python from `unicodedata` and compares both the table and
9 000 generated streams against the C++ encoder; the campaign aborts if the two
disagree (`results/*/logs/encoder_check.txt`).

### Feeding

`harness/corpus.hpp` parses a stream into `Event`s (kind, code point, shift
state, caps) once; all five drivers consume the identical vector. Each driver is
three stages so the timed region can be nested:

* `prepare(e)` — adapter work only,
* `invoke()` — the engine's own entry point,
* `apply(e)` — materialise the engine's edit and update the app-visible text
  (KieeKey `replacementUtf16`, OpenKey wrapper `Delta`, UniKey `(backs, span)`).
  All three use the same `applyEdit` tail-edit helper.

Consumers are modelled as their own products' front-ends do (documented in
`harness/engines.hpp`): KieeKey `TextInput`/`EngineResult`; the OpenKey wrapper
API through the shim; UniKey `UkEngine::process(keyCode, backs, out, outSize,
outType)` with its own word-break classifier, charset XUTF8, `reset()`-free —
i.e. the engine is fed exactly the key codes its tables are indexed by, and the
edit it asks for is applied verbatim to the span its word buffer owns.

### Configurations

Both are reported for every measure — nothing is quoted from one config only.

| | `as-shipped` (each product's own defaults) | `matched-minimal` (features equalised, all off unless both have them) |
|---|---|---|
| KieeKey | `checkSpelling=1, restoreIfWrong=1, dictionaryRestore=0, digitsAreLiteral=1, macro=1, freeMark=0` | flags from the `matched-minimal` row of the Cfg (spell-check off, restore off, macro off, digits literal per method) |
| OpenKey | wrapper defaults: `checkSpelling=1, restoreIfWrongSpelling=0, useMacro=1, freeMark=0, modern=0` | same switches as KieeKey |
| UniKey | `freeMarking=1, strictSpellCheck=0, spellCheckEnabled=1, autoNonVnRestore=1, macroEnabled=1, alwaysMacro=0, modernStyle=0`, empty macro table | mirrored from the matched-minimal Cfg |

### Latency protocol

* one core, `taskset`; every process is separate per mode;
* all three input methods are timed (`telex-end`, `telex-mid`, `vni`) and all
  three streams (`prose`, `edit-storm`, `pathological`); rows are keyed
  `(config, method, stream)`. Skipping a method because one engine treats it as
  internally identical would have quietly biased the *other* engines;
* warm-up run per engine per stream (discarded), then `rounds=7` measured rounds;
* **engine order rotated per round**, so no engine is always cold or always last;
* per-key samples are recorded (p50/p90/p99/p99.9/max) *and* batched means; the
  headline is `full ns/key` = median-of-round-medians, with best/worst round and
  σ published;
* clock pair overhead is measured and published with the numbers (tens of ns, so
  sub-10 ns claims are not made);
* `--mode=overhead` measures the one real asymmetry inside the timed region — the
  indirect `dlopen` call the two OpenKey columns go through and the other three do
  not — and the report publishes it (`shim_overhead` table) rather than assuming
  it away; it is deliberately *not* subtracted from anyone's number;
* stage nesting lets the reader see `adapter`, `engine`, `engine-core-net`
  (`engine − adapter`) and `full` instead of one opaque number.

### Noise and significance (pre-registered before measuring)

* `kieekey-aa` — a second instance of the *same* engine in the *same* process —
  gives the in-process band: **the p99 of `|kieekey − kieekey-aa|`** over every
  round × method × stream pair. The median and the worst case are published next
  to it, and `summary.json → noise_band.band_rule` states the definition.
  Using the *maximum* was tried and rejected: on this 2-vCPU VM one scheduler spike
  in `126` round pairs once pushed the "band" to `81 ns` and swamped every real
  effect. The band is a property of the run that produced the numbers, so it is
  recomputed per campaign and never borrowed from a quieter run (observed so far:
  `10`–`56 ns/key`).
* a full repeat campaign gives the campaign-to-campaign drift of the same engine's
  own headline (`paired` table in the report).
* a difference between two engines is reported as a difference **only** if
  `|Δ| > max(2 × A/A band, 2 × cross-campaign drift, 1 ns)`, with the threshold
  derived from the artifacts per stream — never asserted in prose. Otherwise the
  report must say "within noise". Correctness is deterministic (the A/A column
  reproduces `kieekey`'s output digests exactly, and the campaigns agree on every
  row including digests), so correctness rates are not band-limited.

### Memory, robustness, sanitizers

* **Memory** (`--mode=mem`): allocation counters are the primary metric (they are
  immune to page-attribution effects); RSS is reported from a *separate process per
  engine* (`--engine=<name>`), because measuring several engines in one process
  charges first-touch pages to whichever engine ran first — that artifact invented a
  `20 MiB` gap between two identical KieeKey instances.
* **Robustness** (`--mode=robust`): each hostile stream runs in a forked child that
  execs a filtered binary, so a crash, a hang or an OOB access in one engine cannot
  take the campaign with it; the parent records `ok`/`exit`/`timeout`/`signal`.
* **Sanitizers** (`build.sh --sanitizers`, `--steps=sanitizers`): ASan+UBSan+LSan
  over the hostile streams and a correctness subset, **one run per engine**. The
  sanitizer build loads `libok205_san.so`/`libokmaster_san.so` (the instrumented
  vendored engines) — otherwise the dlopened columns would silently be the
  un-instrumented ones. Findings are attributed by the source tree of the top
  frame, so a report in a competitor's code is not charged to KieeKey and a report
  in KieeKey's code is not excused. `load()` deliberately ignores `san_*.jsonl` and
  `sanfix_*.jsonl` when building the main tables, so a per-engine subset can never
  overwrite a campaign row.

### Fairness rules the harness follows

1. no engine gets a private optimisation, a different `-O` level, or extra warm-up;
2. no source file outside `benchmark/` is touched; competitors' engines are
   compiled unmodified (UniKey needs only the repository's existing
   `uk_fix.h` prologue and `-DLINUX -fpermissive`);
3. both engine configurations are reported, and the *shipped* defaults of each
   product are used for the headline — including the ones where KieeKey loses;
4. every divergence is published, including cases where a competitor is faster
   or more correct: the digest of each engine's full output chain is in the raw
   artifacts, and `tables.md` lists the pairs that differ;
5. upstream defects found while measuring are *reported, not fixed* (see
   `results/*/logs/san_*.log`), and their effect on the numbers is stated;
6. raw artifacts are committed so any figure can be recomputed:
   `python3 benchmark/scripts/summarize.py --results=benchmark/results/<campaign>`.

## Reading the artifacts

`results/<campaign>/raw/*.jsonl` — one flat JSON object per line:
`meta`, `selftest`, `correctness` (counters + digests per engine/config/method/cat),
`corpus` (filter accounting: how many corpus lines were used and why the rest were
dropped), `example` (worked divergences), `text`/`stress-digest`, `invariant`,
`fuzz`, `macro`, `latency` (per round × method × stream × engine), `mem` /
`mem-<engine>` (isolated RSS runs), `overhead` (shim call cost), `robust`, plus
`logs/san_<engine>.log`, `logs/sanfix_<engine>.log` and `logs/encoder_check.txt`.
`tables.md` holds every generated table between `<<<TABLE:name>>>` markers and
`summary.json` the machine-readable numbers the report is built from.

`tables.md` is generated; `summary.json` is the machine-readable form of the same
numbers that `make_report.py` splices into `REPORT.md`.

## v1.3.0 RC1 — the optimisation campaign

The v1.3.0 work is measured with a second, stricter layer in this directory, because "is the
engine faster than the competitors" and "did *this change* make it faster" need different
instruments:

| file | what it is |
|---|---|
| `../docs/bench/rc1-130/PROTOCOL.md` | the pre-registered rules: passes, estimators, noise bands, tier vocabulary, gates, what is not measured |
| `../docs/bench/rc1-130/OPTIMIZATION_LEDGER.md` | every candidate with its verdict, including the rejected ones |
| `scripts/campaign_rc1.sh` | one campaign, fixed order, aborts on the first failed gate |
| `scripts/rc1_gates.py` | pass/fail gates; every line lands in `results/<name>/logs/gates.txt` |
| `scripts/rc1_stats.py` | paired-by-round, session-clustered statistics → `tables.md` + `summary.json` |
| `scripts/baseline_manifest.py` | hashes the tree *and* the frozen v1.2.2 reference, plus the flags read out of `build.sh` |
| `harness/rc1.hpp` | modes `tput`, `diffab`, `profile`, `timer`, `cold`, `attrib-guard` (a `walks` mode existed for one candidate and was retired with it — [ledger §2](../docs/bench/rc1-130/OPTIMIZATION_LEDGER.md)) |
| `harness/kk_shim.cpp` | the attribution pair: `libkkbase.so` (frozen v1.2.2) vs `libkkcand.so` (this tree) through one shim |
| `reference/kieekey-1.2.2/` | pristine previous-release engine sources, hash-verified — what "baseline" means |

```bash
# release measurement: the campaign of record, run on the frozen tree
DIFFAB_KEYS=40000 ./benchmark/scripts/campaign_rc1.sh --name=rc1-130 \
    --sessions=5 --rounds=12 --keys=150000 --words=74000 --l2rounds=6 --diffab-seeds=3
./benchmark/scripts/build.sh --sanitizers                      # only for the sanitizers step
./benchmark/scripts/campaign_rc1.sh --name=rc1-130 --steps=sanitizers
python3 benchmark/scripts/rc1_stats.py --results=benchmark/results/rc1-130
python3 benchmark/scripts/make_report.py --campaign=rc1-130 \
    --narrative=benchmark/REPORT.rc1.narrative.md --out=benchmark/REPORT.rc1.md --strict-stat

# a candidate trial is the same campaign with --candidate, which also relaxes the manifest
# gate from "the tree must equal the frozen reference" to "name the drift you are measuring"
./benchmark/scripts/campaign_rc1.sh --name=rc1-ca4 --candidate --sessions=6 --rounds=24 \
    --keys=200000 --words=74000 --engines=contest,ctl,attrib
```

### Screens run after the release campaign

Each is a candidate trial of the same campaign, with its own gates and raw artifacts, so that "no
effect" is a measurement rather than an impression:

| campaign | candidate | verdict |
|---|---|---|
| `results/rc1-ca4` | C-A, bucket-table restructure | REJECT — −0.29 … −9.52 % across the 18 cells |
| `results/rc1-cc1` | C-C1, hot-path dispatch reordering | REJECT — −10.9 % on `matched-minimal\|vni\|pathological` |
| `results/rc1-p5` | P5, single-copy undo snapshot | REJECT — −0.06 % deciding cell, i.e. the harness's own drift |
| `results/rc1-p8` | P8, profile-guided `libkkcand.so` | REJECT — **−4.04 %**, 7/18 cells beyond 2× band |

`build.sh --pgo` is what made the last row measurable: it instruments the engine TU, trains with
`--mode=tput`/`latency`/`correctness`/`robust` on a corpus window the campaign never measures
(`--seed-idx=7`), then rebuilds **`libkkcand.so` only** with `-fprofile-use -Werror=coverage-mismatch`,
and fails if no `.gcda` was written — because a silently missing profile falls back to plain `-O3` and
would be labelled a measurement. Everything else stays plain `-O3` by design, so the campaign's paired
gain table *is* the build-configuration difference, read in the same rounds against the same A/A band:
the matched-parity rule applied in the other direction. A candidate's own verdict wording is derived
from `results/<name>/manifest_at_build.json` (snapshotted at gate time), falling back — loudly — to the
`candidate=` flag in `environment.txt`, which is what caught `rc1-p5` being mislabelled "baseline".

**Absolute ns figures are campaign-internal.** Re-measuring the same binary on the same flags hours
later moved the frozen engine's deciding-cell median from 84.70 to 68.73 ns/key, with UniKey moving
with it (73.00 → 60.20); the ratio held (16.1 % → 13.1 %). Cross-campaign subtractions are therefore not
allowed anywhere in these documents, and a target stated in absolute ns — the "+7 ns" clause — can only
be adjudicated inside one campaign: the campaign of record decides, and the ratio is what generalises.

---

New row kinds in the RC1 artifacts: `tput` (and `tput-lead`, fixed order), `diffab`,
`attrib-guard`, `timer`, `cold`, `profile-meta`/`profile-sample`, plus a
`build` field on every timed row (`in-process`, `kk_base`, `kk_cand`) which is how a stale
`.so` becomes visible instead of becoming a number.

Three rules that are easy to break and hard to notice: **never run `build.sh` while a campaign is
running** (it rebuilds the engine `.so`s from the live tree and would swap an engine mid-round),
**never edit `campaign_rc1.sh` while it is running** (bash re-reads the open script at a byte offset
and parses garbage — it cost one campaign its first pass, see [PROTOCOL §12](../docs/bench/rc1-130/PROTOCOL.md)),
and **multi-invocation artifacts need `--append`** (the default `--out` truncates, which is how a
108-row differential once became an 18-row one).
