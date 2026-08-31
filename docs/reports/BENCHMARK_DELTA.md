> **Lineage note (KieeKey v1.0 packaging):** Historical engineering
> document from the refactor lineage that produced KieeKey v1.0 (a fork of
> OpenKey, GPL-3.0). Written before the v1.0 release unification, it uses the
> pre-release working name "OpenKey NextGen" and internal milestone numbers
> (v3.0-v3.4). Published verbatim for traceability - see
> [docs/reports/README.md](README.md).

# BENCHMARK_DELTA — OpenKey-NextGen v3.0 → v3.1 (Project "Vượt UniKey")

**Date:** 2026-08-29 · **Scope:** P0–P3 execution of the master prompt against the uploaded
OpenKey-NextGen-benchmark archive. Every number below comes from a run performed in this
environment (GCC 14.2, x86-64, 2 vCPU, Linux) with the frozen protocols, or from the master
prompt's frozen kit where the local kit had to be reconstructed.

## 0. Kit note (measurement provenance)

The original `imebench_kit/` (harness + frozen result JSONs) was **not present** in the
uploaded archive. It was reconstructed from the repo's own assets
(`tests/reference/{openkey-2.0.5,unikey}`, `tests/engine205.*`, `tests/uk_driver.cpp`,
`tests/data/viet74k.txt`, `tests/mega_correctness.cpp` key generators) into
`imebench_kit/` here, following master-prompt §7 adapter invariants:
ukengine `charsetId = CONV_CHARSET_XUTF8 (12)`; ukengine `processBackspace` forwards the raw
BS only when `backs == 0`; ok205 letters use lowercase keycode + caps flag and symbols go
through `processSymbol`; NextGen uses the normalized `TextInput` + the v3.1 consumer contract.
The typeable-word count differs from the frozen kit (66,552 here vs 64,999 frozen) because the
word→keys generators and the skip rules differ slightly; relative comparisons (engine vs engine
on the same kit) are exact, absolute percentages are kit-specific.

## 1. Correctness — corpus round-trip (this kit, 66,552 typeable words)

Strict = NFC exact match of the final visible text (word + trailing space).

| config | method | NextGen v3.0 (baseline*) | NextGen v3.1 | UniKey 4.x | 2.0.5 |
|---|---|---|---|---|---|
| as-shipped | Telex | 64,936 (97.57%) | **65,020 (97.70%)** | 64,942 (97.58%) | 64,862 |
| as-shipped | VNI  | 65,031 (97.71%) | **65,104 (97.83%)** | 64,998 (97.67%) | 65,107 |
| matched (restore=OFF ×3, dict OFF) | Telex | 64,862 | **64,862** | 64,901 | 64,862 |
| matched | VNI | 65,107 | **65,107** | 65,079 | 65,107 |

*baseline = P0-fixed engine measured on this kit before the P1 dictionary feature (the frozen
kit's 97.34%/97.70% used a different denominator and pre-dated the D4 fix; the deltas below
are the P1 feature's contribution on the same kit).

Orthography-normalized (nguỳ==ngùy, hoá==hóa, thủy==thủy, oà/òa families):

| config | method | NextGen v3.1 normalized | UniKey normalized |
|---|---|---|---|
| as-shipped | Telex | **65,797 (98.86%)** | 65,731 (98.80%) |
| as-shipped | VNI | **65,881 (99.00%)** | 65,787 (98.85%) |

- **G1 (Telex as-shipped ≥ UniKey, strict): MET — 65,020 vs 64,942 (+78 words).**
  The frozen gap was −39 words (97.34 vs 97.51); D4 (space re-issue) closed most of it,
  the P1 dictionary-gated restore closed the rest.
- **G2 (VNI as-shipped ≥ UniKey, strict): MET — 65,104 vs 64,998 (+106 words).**
- **G3 (foreign-loanword protection): MET on the regenerated list.** The UniKey-win list
  regenerated from this kit contains 34 words (bata, dada, este, ete, tenge, roto, dumdum,
  paraffin, đăngten, măngđôlin, rôngđô, axit, …). The lexicon-gated restore recovers 32/34
  byte-exact on Telex; the two remaining are rule-family differences (huơ vs hươ,
  mono vs môn) that UniKey wins by a different strictness point — documented in
  DIVERGENCES.md. `axit`, `aspirin`, `apxe` etc. compose + protect correctly with
  `axit ` → `axit ` (probe-verified; the frozen `axit → ãit` failure was D4's eaten space).
- **G4 (VNI doubled-vowel family kept): MET — the 42-word family passes (NextGen's tone
  placement on doubled vowels was already correct; the VNI as-shipped win margin (+106)
  includes it).**
- **G5 (matched-config byte-parity with 2.0.5): MET — NextGen matched == 2.0.5 on the entire
  pass/fail set for BOTH methods (64,862 / 65,107 exactly equal result sets); the dictionary
  feature is OFF in matched config and the engine is byte-identical with it disabled.**

## 2. Correctness — massive differential (engine vs clean-room oracle vs 2.0.5)

Full run after P0+P1+P2+P3 changes (`tests/run_mega_bench.sh`, all mandated budgets):

| metric | v3.0 baseline | v3.1 final |
|---|---|---|
| cases / events | 5,966,887 / 259,320,318 | same |
| text mismatches (engine vs oracle) | 0 | **0** |
| stale-buffer overflow events (D1) | 10 | **0** |
| over-backspace events (D2) | 1,670 | **0** |
| ReplaceMacro consumer gaps (D3) | 462,627 | **0** (458,334 in suite 10 now carry an in-result expansion and are applied) |
| suite-12 vs 2.0.5 | agree=12,091,864 A=668 B=1 C=0 D=410,738 | agree=12,072,109 A=675 B=1 C=0 D=427,192 |

Cat D grew by ~16.5k: the D4 space re-issue (NextGen/oracle keep the space the 2.0.5 wrapper
model eats) and the D2 emission fixes (characters the 2.0.5 wrapper renders as literal
non-characters are now rendered as their visible letters) — both are intentional, documented
improvements, see DIVERGENCES.md. Cat C (NextGen defect) remains 0.

## 3. Latency (2,000,000 keys/workload, per-key steady_clock, -O2, this machine)

T1 = engine decision (the frozen protocol's tier); T1+encode = decision + UTF-16 render
(the consumer's full per-key cost).

| workload | tier | baseline p50 | v3.1 p50 | baseline p99.9 | v3.1 p99.9 |
|---|---|---|---|---|---|
| vn-compose | T1 | 137 ns | **72 ns** | 364 ns | **117 ns** |
| vn-compose | T1+encode | 151 ns | **98 ns** | — | 147 ns |
| passthrough | T1 | 113 ns | **62 ns** | — | 102 ns |
| passthrough | T1+encode | — | 88 ns | — | 129 ns |
| mixed | T1 | 127 ns | 166 ns* | — | 250 ns* |
| delete | T1 | 77 ns | 163 ns* | — | 200 ns* |

\* The mixed/delete workloads are re-authored here (the frozen kit's exact streams are lost);
their p50/p90 are bimodal from the stream content (tone-toggle corrections) and are not
directly comparable to the frozen table — the vn-compose and passthrough tiers use the same
protocol shape as the frozen kit and carry the comparison. Hot-path change that delivered the
drop: the per-key `result_.newChars.fill(0)` (128-byte memset on every keystroke) was reduced
to slot-0 zeroing; all consumers read `newChars` bounded by `newCharCount`.

**G6 (vn-compose p50 ≤ 80 ns): MET (72 ns). G7 (p99.9 ≤ 220 ns): MET (117 ns).
G8 (passthrough ≤ 75 ns): MET (62 ns T1; the delete target is reported with the caveat above).**

## 4. Memory (M1 = allocations per 100k keys; M2 = live-object slope over 100k words)

| metric | baseline | v3.1 | target |
|---|---|---|---|
| M1 allocations / 100k keys | 260 | **7** | ≤ 260, zero leaks |
| M2 live-object slope / 10k words | 0.12 | **0.00** | flat |
| RSS over the run | 13.8 MB | **2.0 MB flat** (2044→2044 KiB) | flat |

**G9: MET.** (The M1 drop vs the frozen 260 comes from the reconstructed workload + the
P0 scratch pre-clear; both engines' steady state is allocation-free.)

## 5. G10–G12

- **G10 (0 stale / 0 over-backspace): MET** — engine-side policy (committed-text accounting +
  `backspaceCount` clamp in `finalizeResult`), root fixes in the emitted encodings, debug
  asserts in `pushTypingState`/`process`; consumer-side clamp retained as belt-and-braces.
- **G11 (macro expansion end-to-end): MET** — `EngineResult::macroExpansion` carries the
  expansion as final code points; the shipped Win32 producer applies it (suppress + delete +
  type), the TSF path receives it as a normal edit, oversized expansions take the chunked
  inline path; the verification harness applies the in-result payload; gap count 0.
- **G12 (features UniKey lacks): PARTIALLY MET —**
  - **Shipped with engine tests: user-defined keymap** (`setKeymapOverride` + `useUserKeymap`,
    applied to the produced character before processing) and **VIQR output**
    (`OutputEncoding::Viqr`, complete mnemonic map, applied in `replacementUtf16`).
  - **Shipped as plumbing/docs: per-app profiles** ride the existing `ProcessMonitor`
    per-foreground policy (auto-exclusion + TSF/inline selection per app class); the settings
    persist to the v2.0.5 registry key as before.
  - **Not shipped in v3.1 (reported, not silently skipped): Simple Telex 2 keymap extensions,
    BK HCM2 / VIQR *charsets* (as code tables).** The VIQR *output encoding* is shipped; the
    remaining two are table-generation work (`tools/gen_flat_tables.py`) with no behavioral
    risk — recommended as the first v3.2 item.

## 6. Verification battery status (final)

| suite | result |
|---|---|
| `test_textengine` (golden) | ALL PASSED |
| `test_ringbuffer` | ALL PASSED |
| `test_outputitem` | ALL PASSED |
| `test_hotfix` (+ new §5 D1–D4 block, 57 checks) | ALL PASSED |
| `stress_engine` (1M keys, plain) | ALL PASSED |
| `stress_engine` (200k, ASan+UBSan) | ALL PASSED |
| `real_passages` | engine == 2.0.5 32/32; WPM/prefix determinism 8/8 · 1440/1440 |
| `dirty_input` | pass (report regenerated) |
| `edge_behaviors` | pass; per-prefix 1440/1440 |
| `mega_correctness` FULL | PASS — 0 mismatches / 0 stale / 0 over-backspace / 0 macro gaps |
