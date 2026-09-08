# v1.3.0 RC1 — Optimisation plan: closing the gap to UniKey

Status: **plan** (candidate specs + measurement protocol). Written 2026-09-08 against `d94b697`,
whose release campaign `rc1-130` is the frozen v1.2.2 engine. This document does not claim any of
the savings below as achieved — each line is an estimate with a named target and a falsifier, and a
candidate becomes real only through `PROTOCOL.md`'s measurement rules. The numbers in §0 and §1 are
measured and come from `benchmark/results/rc1-130/summary.json` unless labelled otherwise.

---

## 0. What "good enough" means, in nanoseconds

Two different metrics answer the objective, and they are at very different distances from it. Both
are quoted, because picking one after seeing the other would be the exact thing this protocol exists
to prevent.

| metric | KieeKey | UniKey | gap | needed for ≤ UniKey | needed for ≤ UniKey + 7 ns |
|---|---|---|---|---|---|
| **L1** engine-decision, deciding cell `as-shipped · telex-end · prose` (ns/key, median of 60 paired rounds) | 84.70 | 73.00 | **+11.77** (CI +11.16…+12.94) | −11.8 ns (−13.9 %) | −4.8 ns (−5.6 %) |
| **L2** per-key latency, same cell, p50 (ns) | 100.0 | 99.0 | **+1.0** | already inside +7 | **already met** |
| L2 p99, same cell | 458.0 | 304.5 | +153.5 | tail work, not throughput | — |
| L2 `edit-storm` p50 | 113.5 | 102.0 | +11.5 | −11.5 | −4.5 |
| L2 `vni · pathological` p50 / p99 / full | 56.5 / 96.0 / 101.1 | 61.0 / 116.0 / 106.7 | **−4.5 / −20 / −5.6** | **already ahead** | already ahead |

Consequences the plan is built around:

* The **only** place UniKey is clearly ahead is sustained per-key throughput on long words (L1) and
  the prose p99 tail. On the median of what a user actually feels per key (L2 p50, prose) the engine
  is already at parity, and on mark-heavy short bursts it is ahead. If the objective is stated as
  "latency", it is partly met today; if it is stated as "L1 engine decision", it needs ~5 ns (the
  +7 ns clause) or ~12 ns (parity).
* The instrument can decide both: A/A band 1.47 ns, same-code null test ±0.99 % ≈ ±0.7 ns, so a
  4.8 ns target is ~3.3 bands and an 11.8 ns target is ~8 bands. Neither can be noise.
* The 18-cell shape says *where* the cost is: KieeKey regresses most on `edit-storm` (+25…+38 %) and
  `prose` (+12…+25 %), ties or wins on `pathological` (−2.7 % to +3.7 %). Those cells differ mainly
  in **how many characters belong to the word being re-analysed on each key**, not in which keys are
  typed. The gap therefore scales with word length — it is re-analysis cost, not dispatch cost.

## 1. Evidence for that diagnosis (and one thing it is not)

Share of the timing window in the release profile (`rc1-130`, `-O3`, 2 583 samples, 0 unresolved;
ns/key column = share × 74.7 ns measured in that window; the rotated L1 median is 84.70 ns/key, so
treat the ns column as a lower bound on the same cost in L1 terms):

| function | share | ≈ ns/key | what it does per key |
|---|---|---|---|
| `checkSpelling` (+ inlined `matchLeadingConsonant`) | 18.9 % | 14.1 | re-validates the leading-consonant run of the whole word |
| `mainKeyBranch` | 12.1 % | 9.0 | per-key dispatch + branch bodies |
| `handleMainKey` | 11.8 % | 8.8 | the key-append state machine |
| `findAndCalculateVowel` — `.constprop.0` + base | 13.2 % | 9.9 | **two separate backward scans of the word for the vowel span**, one per `forGrammar` value |
| `process` | 8.3 % | 6.2 | prologue, result reset, D2 clamp |
| `checkGrammar` | 7.3 % | 5.5 | orthography repair scan |
| `insertMark` / `insertKey` | 9.0 % | 6.7 | emits the replacement tail (`getCharacterCode` per char) |
| `checkCorrectVowel` | 5.7 % | 4.3 | candidate vowel-sequence search |
| `saveWord` | 2.1 % | 1.6 | per-key snapshot of word state |

UniKey's own profile on the same corpus (`profile_unikey-4.x_as-shipped.jsonl`, symbolised 2026-09-08):
`process` 12.3 %, `macroMatch` 5.4 %, `isValidVC` 4.7 %, `processTone` 4.1 %, `processAppend` 3.6 %,
`writeOutput` 2.8 %, driver glue 2.8 % — i.e. **no single stage of UniKey dominates**; its per-key
work is spread across small incremental state updates (`processAppend`, `processTone`), which is
exactly the shape a length-independent engine has.

**Harness asymmetry, checked rather than assumed.** Driver glue inside the timed region is
`KieeKeyDriver::invoke` 2.6 % + `prepare` 2.0 % ≈ 4.6 % (≈ 3.4 ns/key) against
`UniKeyDriver::invoke` 2.8 % + `prepare` (below the 1.3 % report floor) ≈ 4.1 % (≈ 2.5 ns). The
difference, ~0.9 ns, is inside the instrument's own null-test range and is *not* enough to explain
the 11.8 ns gap. The L1 timed region is `prepare()+invoke()` for every engine, and the
`apply()`/transcript work is measured separately as `full_ns_per_key` for every engine — so the
comparison is structurally symmetric. Conclusion: the gap is engine work.

## 2. Candidates, ordered by (expected gain) ÷ (risk × effort)

Every candidate below keeps the engine's contracts intact: no allocation on the hot path, no
unbounded cache, no behaviour change, `O(1)` per key retained (or improved). "Equivalence" always
means the gates: `attrib-guard` transcript identity, `diffab` per-key equality over ≥ 2 M events,
`digest-identity` at full corpus, and a sanitizer run.

### P0 — stop zero-initialising what the switch assigns anyway (`process()` prologue, driver `prepare`)
*Target:* `process()` writes `result_.newChars[0] = 0` after the note that zeroing the whole 32-slot
array cost ~15 ns/key; the same class of leftover exists in `KieeKeyDriver::prepare`, which assigns
`in_ = ok::text::TextInput{}` (whole struct) and then overwrites `kind`/`ch`/`isCaps` in the switch.
*Change:* construct the fields that are read, nothing else.
*Expected:* 0.3–0.9 ns/key, all cells, no stream dependence. *Risk:* trivial; a field read before
being written shows up immediately as a transcript mismatch.
*Why first:* it is the only candidate whose cost is known exactly from a comment in the source, and
it funds the campaign plumbing for the rest.

### P1 — fuse the two `findAndCalculateVowel` scans into one pass
*Target:* `findAndCalculateVowel(bool forGrammar)` is instantiated twice (`.constprop.0` + base =
13.2 % ≈ 9.9 ns), each scanning the word backwards from `index_-1` with its own break rules.
*Change:* one scan that advances both state machines (`forGrammar=false` rules and
`forGrammar=true` rules) and stops when both have committed; each caller keeps reading its own
`vowelStart_/vowelEnd_/vowelCount_` pair. No rule changes, one traversal instead of two.
*Expected:* 3–4.5 ns (the difference between one scan and two is the 4.6 % clone share plus its
per-call setup), concentrated in `prose`/`edit-storm` — the cells that are behind.
*Risk:* medium-low. The two variants break at different positions; the fused loop must keep both
`break` predicates exactly and stop only when neither can still move. Equivalence is mechanically
checkable because both variants' outputs are compared per key by `diffab`.

### P2 — make `checkSpelling` incremental along the append path
*Target:* 18.9 % ≈ 14.1 ns. Each key re-walks the leading-consonant rows for the whole word.
*Change:* the word is append-only between resets, so carry the validation position forward:
`spellingScan_ = {len validated up to, row state, tempDisableKey_}` advanced by one character on
append; any backspace, mark insertion, or non-append transition recomputes from scratch (the current
code path, unchanged). Fixed-size state, no hashing (a prefix hash was tried upstream and cost more
than it saved — see the v1.2.1 report's rejected memoisation, and R1/C8 in the ledger), no
allocation, and the recompute path stays the source of truth whenever state is doubtful.
*Expected:* 3–7 ns on `prose`/`edit-storm`; near 0 on `pathological` (short words recompute anyway),
which is the right direction: the cells that lose are the ones that pay.
*Risk:* this is the one candidate that can be *wrong in a way the corpus might not sample* — a stale
`spellingScan_` is a behaviour change. Mitigation, in order: the exact invalidation rule is written
down before coding; `diffab` runs at 6 seeds and `DIGEST` identity at full `--words`; a
debug-build assert recomputes from scratch on every Nth key and compares against the incremental
answer (a self-checking build that costs nothing at `-O3` because `#ifndef NDEBUG`); sanitizer run
for the new state's lifetimes.

### P3 — memoise the vowel span for the length the word currently has
*Target:* `checkCorrectVowel` (5.7 %) and `checkGrammar` (7.3 %) each re-derive vowel positions that
`findAndCalculateVowel` just computed for the same `index_`.
*Change:* one per-`process()` scratch record `{index_, vowelStart_, vowelEnd_, vowelCount_, valid}`
written by P1's fused scan and read by the two consumers; invalidated by anything that mutates
`typingWord_`/`index_` (same single choke point as P2's invalidation).
*Expected:* 1–2.5 ns. *Risk:* low-medium, same invalidation discipline as P2 but with a much smaller
state (four ints + a flag, so a mistake is visible in `diffab` immediately).
*Order note:* land after P1, because it reads what P1 writes.

### P4 — emit the replacement tail without recomposing unchanged characters
*Target:* `insertMark`/`insertKey` 9.0 % ≈ 6.7 ns, of which the `getCharacterCode(typingWord_[i])`
loop is the visible part; `edit-storm` pays it most (that stream rewrites tails constantly).
*Change:* keep the composed code per slot in `typingWord_`'s companion array (already the low-16-bit
code point — see the ledger's warning that a class-mask fold over it is a behaviour change, so this
stores the *composed* code, not a class), and start the emit loop at the first slot whose composition
inputs changed; slots below it are copied, not recomposed.
*Expected:* 1–2 ns on `edit-storm`, ~0 elsewhere. *Risk:* medium; a copy that starts one slot late is
a wrong-character bug, which the golden digest and `diffab` catch.
*Explicit non-goal:* this must not change `backspaceCount` or the emitted span, only how the same
span is produced — the differential gate compares per-key backspaces on purpose, so a payload-shrinking
variant would be caught and is out of scope here (it would be a product-visible front-end change).

### P5 — `saveWord` snapshot: dirty fields instead of whole-array copy
*Target:* 2.1 % ≈ 1.6 ns per key, plus `restoreLastTypingState` 0.8 %.
*Change:* snapshot only the arrays a key can dirty (the code array's tail beyond `index_` is
unchanged by definition), and record `index_` so restore is a length reset plus the dirty range.
*Expected:* 0.6–1.4 ns, every cell. *Risk:* low — an incomplete snapshot makes the *next* key wrong
and shows up in the first `attrib-guard` run.

### P6 — first-touch cost: the cold-start and first-round deficit
*Target:* cold `wall p50` 484.4 ms vs UniKey 418.5 ms, and first round 30.9 vs 17.5 ns/key.
*Change:* whatever the tables cost at first touch (code table, `gCharacterIndex`, per-`configure`
rebuilds in the driver) is the whole of a 66 ms gap that a user feels on the first word after login.
Needs a line-level cold profile before a fix is written — this is a measurement task first.
*Expected:* −20…−50 ms wall, −10 ns on round 1. *Risk:* low for correctness (initialisation order),
medium for "does it even belong to the engine" (part may be the harness's `configure()`).
*Why it matters for the objective:* it is a second axis where UniKey currently wins, and fixing it
turns a documented loss into a documented win without touching the hot loop.

### P7 — dispatch order in `handleMainKey`/`mainKeyBranch` (24 % ≈ 17.8 ns) — deliberately last
Both C-A (automata, `REJECT`, −5 % in the deciding cell) and C-C1 (reorder, `REJECT`, −10 % on
mark-thrash) failed *in this area*, and both failures are now explained by the same thing: the shape
of the branches encodes an assumption about the input distribution. Any P7 attempt must therefore
first show, from the corpus, which branch order wins on **both** stream families, and must be
measured with the pathological cells as the gate rather than as a footnote. Kept in the plan because
17.8 ns is the biggest single number, but it is last for a reason, and it should be considered
`DROP`ped if P1+P2 already reach the objective.

### P8 — compiler configuration (PGO): the lever that attacks diffuse cost
*Target:* the release profile shows the gap is **diffuse** — no function above 19 % and the biggest
single line worth 6.6 ns. Candidates that shave loops inside such a distribution are bounded by ~1 ns
each (P1's analysis below proves it), whereas the branch layout, inlining and register allocation of
the whole 300-line `checkSpelling` + the dispatch pair are what the compiler decides. Profile-guided
optimisation is the only lever in this class that is (a) behaviour-preserving by construction, (b)
symmetric across engines, and (c) available without touching a line of engine logic.
*Procedure, in the order that keeps it honest:*
1. Build `libkkcand.so` from the **same sources** with `-fprofile-generate`, run one training pass
   over the campaign corpus (`tput`, all 3 streams, both methods, both configurations — so the
   profile is not tuned to the deciding cell alone), merge `.gcda`, rebuild `-fprofile-use
   -fprofile-correction`.
2. Measure it as a *paired candidate*: frozen non-PGO build in `kk_base`, PGO build in `kk_cand`,
   same rounds, so the gain table is the PGO effect with the campaign's own A/A band and order
   control — the same acceptance rule as any engine candidate, no cross-campaign arithmetic.
3. Measure all four engines with PGO in a separate labelled configuration campaign and publish the
   table, so the report can say what the rivals would gain from the same flag. If UniKey gains more
   from PGO than KieeKey does, the parity claim changes and the report must follow the numbers, not
   the flag.
4. Decide the *shipped* configuration separately from the benchmark: `src/CMakeLists.txt` would gain a
   `KIEEKEY_PGO` option with the profile committed or regenerated at build time; the fallback (stale
   or missing profile) must be a plain `-O3` build, never a build that fails.
*Expected:* 2–6 % of L1 (1.7–5 ns in the deciding cell) for KieeKey; the same order for the rivals.
*Risk:* low for correctness (same sources, compiler-verified), medium for reproducibility — `-fprofile-use`
emits a warning-and-ignore on a stale profile, so the build must record the training corpus and the
`.gcda` digest. It is also the one candidate that could *move the null test*, because the shim columns
then differ in build flags rather than sources: `gate:attrib-guard` must stay inside its band, and if
PGO changes the shim/in-process ratio, the band is re-derived from a same-build A/A pair rather than
reused from this campaign.
*Why it is listed after the engine candidates in effort but before them in expected value:* it costs
one build variant, and if it alone reaches the +7 ns clause, several risky engine edits become
unnecessary. Run it second (right after P5), not last.

### P1 — result of the loop-level analysis (why it is now a *small* candidate)
The share (13.2 % across two instantiations) is real but the work is not removable: the scan stops at
the first consonant after the vowel run, so it examines ~4–7 positions for a prose word. Computing
both variants in one pass therefore saves a handful of iterations, and a per-key memo would have to
be invalidated by 26 `typingWord_` write sites plus 24 `index_` mutations — an invalidation surface
out of proportion to ~1 ns. P1 is reduced to: share the single `isConsonantAt(iii)` evaluation between
the two variants where a function calls both (a local, not a cache), expected 0.3–0.8 ns. It stays in
the plan because it is nearly free, and it is explicitly *not* the 3–4 ns it was estimated to be —
the estimate is recorded here so a future reader sees the correction, not just the outcome.

## 3. How each candidate is decided

1. **Screen** (`4 sessions × 8 rounds, keys=150000, DIFFAB_KEYS=40000`, ~18 min): if the deciding
   cell's gain is under ~1 % (the null-test floor), stop; if any cell regresses beyond the screen
   band, stop. Both of the rejected candidates would have been stopped at this stage by the same
   numbers, which is the point.
2. **Confirm** (`6 sessions × 24 rounds, keys=200000, --diffab-seeds=6`, ~50 min): the acceptance
   rule as pre-registered, plus the gates at full corpus size.
3. **Accept** → commit with the table in the ledger; **reject** → revert the same day, ledger keeps
   the numbers.
4. Land order, revised after the loop-level analysis above: **P5 → P8(PGO) → P0 → P4 → P6 → P2 → P7**,
   with P1 demoted to a side-effect of whichever change touches `findAndCalculateVowel` and P3
   dropped (its memo's invalidation surface is 50 write sites for ~1 ns). Cheap-and-safe first, the
   diffuse-cost build lever second, the state-carrying ones only if the target still is not met.
   Objective as agreed: L1 deciding cell **and** L2 p50 in all three `as-shipped` streams must reach
   ≤ UniKey + 7 ns; parity is the stretch, tails are published and not gated.
5. After the last accepted candidate, a **fresh release campaign** on the final tree is the number
   that ships (`rc1-opt` at 6 × 24), and the report is re-rendered from it. Interim campaigns are
   screening evidence only, and the report says so.
6. Never edit `campaign_rc1.sh`, `build.sh`, or `src/core` while a campaign is running — it cost this
   project a campaign already (PROTOCOL §12).

## 4. "The benchmarks must be absolute exact" — what that requires and what it means here

Non-negotiable invariants the harness must keep holding for any number in this plan to be trusted:

* Same flags for all four engines: `-std=c++17 -O3 -DNDEBUG -w` (product parity), no LTO/PGO; read
  out of the build manifest, not out of the script.
* Same timed region for every engine (`prepare()+invoke()` for L1; `feed()` for the full path), and
  the driver glue measured per engine — re-audited whenever a driver is touched (P0 touches one).
* Every engine built from verified-pristine sources except `src/core` (hash gates before/after), and
  the vendored competitor engines are never "fixed".
* Round 0 discarded, medians primary, session-clustered bootstrap CIs, tails published, A/A control
  per cell, order control (fixed-lead vs rotated) per cell, both configurations reported, and the
  same-code null test re-run on the final tree so the release campaign proves its own floor.
* Correctness gates are pass/fail and must be able to fail: each has been made to fail on purpose
  once and the failure is recorded.
* Limits that cannot be removed here and are therefore published instead: 2 logical CPUs (neighbour
  core carries host noise), `ITIMER_PROF` delivered at 249.80 Hz against a 1 000 Hz request, no
  `perf_event_open` (so no hardware counters), competitors' Windows TSF/hook path not measurable
  (NOT AVAILABLE, never estimated).

## 5. When I will call this a failure

I will report **FAIL**, with the measured numbers and the reason, if any of these is the end state:

* the confirming campaign's deciding cell still sits more than 7 ns above UniKey after P0–P6 have
  each been individually measured and accepted or rejected; or
* every candidate that could close the gap is rejected by the no-regression clause, which would mean
  the gap cannot be closed without trading one stream family for another — the honest output then is
  the trade-off table, not a re-labeled win; or
* a candidate reaches parity by weakening a contract (allocation, `O(1)`, behaviour identity). That
  is not a pass and will be reported as a rejection even if the timing improves.

What will *not* be called failure: UniKey staying ahead in `vni · pathological` (+78.8 %, CI
+14.26…+14.80 ns — a 33.3 vs 18.5 ns path where closing 14.8 ns means halving that cell) if the
cells the objective names reach target. If the objective is meant to cover all 18 cells, say so
before I start, because that changes which candidates are viable rather than how carefully they are
measured.

## 6. Other axes (the optional "better than UniKey" part), as measured today

| axis | KieeKey | UniKey | verdict today |
|---|---|---|---|
| L2 p50 (prose) | 100.0 ns | 99.0 ns | parity (within one A/A band) |
| L2 p50/p99 (vni · pathological) | 56.5 / 96.0 | 61.0 / 116.0 | **ahead** |
| L2 p999 (edit-storm) | 689.5 | 759.5 | **ahead** |
| hot-path allocations (2 M keys) | 21 | 20 | parity (one allocation), RSS 46.2 vs 46.4 MiB |
| cold start wall p50 / first round | 484.4 ms / 30.9 ns | 418.5 ms / 17.5 ns | **behind** — P6 targets it |
| correctness, example set | 24/62 exact (38.7 %) | 24/62 (38.7 %) | tie; both far ahead of OpenKey 4/62 |
| robustness (5 000-key long word, adversarial) | 40/40 ok, 0 crashes | 40/40 ok | tie |
| vs OpenKey 2.0.5 / master, all cells | 3.4–3.8× faster | — | already ahead by a wide margin |

The two axes worth *winning* rather than tying are cold start (P6) and the tail-latency set
(p99/p999 on prose), and both are cheaper to improve than L1 throughput because they are dominated
by initialisation and by the re-analysis the earlier candidates already attack.
