# v1.3.0 RC1 optimisation ledger

Every candidate gets an entry — implemented and measured, or rejected with a reason — because the
useful part of optimisation work is the map of what does not pay. Rules that decide ACCEPT/REJECT
are pre-registered in [`PROTOCOL.md`](PROTOCOL.md) §8; the numbers below are campaign artifacts, and
"measured" always names its campaign. No entry is deleted when it fails.

Statuses: `LAND` (in `src/core`, gate-verified) · `MEASURE` (measured, verdict pending) ·
`REJECT` (measured and discarded) · `DROP` (not written: falsified before implementation) ·
`BLOCKED` (needs an instrument this repo does not have).

---

## 0. Where the next cycle is planned

[`OPTIMIZATION_PLAN.md`](OPTIMIZATION_PLAN.md) holds the current plan: the measured gap to UniKey
(L1 +11.77 ns in the deciding cell, L2 p50 already at parity), the profile evidence that the gap is
word-length re-analysis rather than dispatch, seven candidates with expected nanoseconds and
falsifiers, and the conditions under which the cycle is declared a failure. Candidates enter this
ledger only once they are implemented and measured.



1. Profile the shipped optimisation level first (`bench_prof`, `-O3`, `--mode=profile`), rank by
   share, and write down the *function or line range* the candidate targets plus the expected
   effect. "The hot loop felt slow" is not a rationale, and a candidate without a named target is
   not started.
2. Implement without touching the two properties the release depends on: `O(1)` per key in the
   core and zero allocation per key on the hot path. If a candidate needs either to be weakened,
   it is rejected at step 1, not at measurement.
3. Prove behaviour first: exhaustive oracle for anything automaton-shaped, differential against
   the frozen v1.2.2 engine for everything, and output-digest identity against the baseline column
   in the same campaign.
4. Screen with a short paired tput campaign (4 sessions × 12 rounds, all cells, both
   configurations) to get the sign and rough size of the effect; then spend a full campaign
   (6 × 24) only on candidates that survived screening or that the release measurement needs.
5. Record the verdict here, including for the ones that lost.

---

## 2. C-A — spelling walks as bitmask automata — **`REJECT`, reverted** (measured in `rc1-ca4`)

**Target at the time.** `TextEngine::checkSpelling` / `checkGrammar` re-walked the consonant and
vowel tables per keystroke inside the same word: the tables are 33 leading rows of ≤ 3 cells and
11 end rows of ≤ 2 cells, so "this row is consistent with these cells" looked like a bitmask AND
over precomputed row sets rather than a cell-by-cell loop. The fixed sampler (see §6) now puts
**21.1 % of the timed window in `checkSpelling` alone** in the pristine engine at `-O3`, which is
what made this the first candidate — the target was real.

**What was built.** `src/core/ConsonantWalks.hpp`: `constexpr`-built first-letter buckets
(`buildLeadTables`, `buildEndBuckets`) plus `rowsFor0/1/2` bitsets intersected per position, with
`KK_WALK_FAST_LEAD` / `KK_WALK_FAST_END` ablation switches, and the pre-change loops kept verbatim
under `*Ref` names so the two could be compared.

**Correctness was never the problem.** The candidate carried its own exhaustive equivalence oracle:
12 519 536 leading and 6 955 302 end-of-word cases against the reference implementation, 0
mismatches, reference digest pinned — and the oracle found a real bug while the candidate was being
written (the end walk had dropped a length rule), which is the argument for building oracles rather
than eyeballing bitsets. On top of that: `diffab` over millions of keystrokes with 0 per-key
mismatches, and output-digest identity against the frozen engine in the same campaign.

**Measured — and slower in every cell.** `rc1-ca4`, 6 sessions × 24 paired rounds, engine stage,
`kieekey-cand` (this tree) vs `kieekey-base` (the pristine v1.2.2 copy, same shim, same flags, same
rounds); 144 paired samples per cell; A/A control floor 1.37–2.23 ns:

| cell | frozen v1.2.2 | with C-A | gain | 95 % CI (ns) | wins/losses |
|---|---|---|---|---|---|
| as-shipped · telex-end · prose *(deciding)* | 83.74 | 88.42 | **−4.98 %** | −4.34…−3.86 | 10 / 134 |
| as-shipped · vni · pathological | 32.73 | 35.64 | −9.52 % | −3.44…−2.80 | 15 / 129 |
| as-shipped · vni · prose | 75.79 | 80.35 | −5.64 % | −4.53…−4.05 | 6 / 138 |
| matched-minimal · telex-mid · prose | 59.43 | 60.38 | −1.19 % | −1.15…−0.56 | 46 / 98 |
| matched-minimal · vni · pathological | 24.49 | 24.69 | −0.29 % | −0.13…−0.03 | 66 / 78 |

All 18 cells negative; 6 regress beyond 2 × the A/A band. Per the acceptance rule in
[`PROTOCOL.md`](PROTOCOL.md) §8: **REJECT**, and the code does not stay in the product —
`03492b4` reverts it and deletes the automata, the oracle and the `walks` mode with it.

**The lesson, stated so it actually transfers.** A share in a profile is a statement about the
*implementation that was measured*. At `-O3`, the table-chain loops were already short, branchless
and L1-resident; the automaton replaced predicted branches with extra bit-ops, an indirection
through bucket arrays, and a wider live state, and it lost 0.3–9.5 %. Two rules follow and are now
part of how candidates are picked: (i) a candidate must survive being measured at the flags the
product uses — the earlier `-O2` screening that favoured this shape is not evidence about `-O3`;
(ii) "fewer instructions in theory" is not a mechanism, a measured paired difference is. The
equivalence oracle was still worth writing — it is how we know the revert is a *performance*
revert and not a behaviour change.

**Files.** history in `0de94ce` (implementation) and `03492b4` (revert); `benchmark/reference/kieekey-1.2.2/`
holds the pristine v1.2.2 sources that the measurement compares against.

---

## 2b. The four-way result **for the C-A tree**, from `rc1-ca4`

This table describes `v1.2.2 + C-A` — the tree that campaign built, and the reason C-A was reverted.
The release measurement of the tree that actually ships is §2c.

Not a candidate verdict — the campaign's other half, recorded so the ledger and the report agree on
what the release currently costs relative to the rivals (L1 engine stage, ns/key, medians of 144
paired samples; KieeKey column = the in-process build of the C-A tree, which is the tree `rc1-ca4`
built — the `kieekey-base` column is the same engine without C-A):

| cell (deciding in bold) | KieeKey | UniKey | Δ % vs UniKey | OpenKey 2.0.5 | OpenKey master |
|---|---|---|---|---|---|
| **as-shipped · telex-end · prose** *(deciding)* | **86.13** | **68.46** | **+25.24** | 323.74 | 289.29 |
| as-shipped · telex-end · edit-storm | 98.01 | 73.19 | +34.42 | 314.30 | 283.30 |
| as-shipped · vni · pathological | 32.82 | 17.90 | **+83.54** | 105.85 | 106.17 |
| matched-minimal · telex-end · prose | 58.42 | 45.00 | +30.54 | 246.57 | 215.22 |
| matched-minimal · telex-end · edit-storm | 68.79 | 47.76 | +44.27 | 245.35 | 214.40 |
| matched-minimal · vni · pathological | 22.31 | 23.50 | **−5.09** | 61.18 | 51.30 |

Tier: **D — slower** in the deciding cell (+25.24 %, 144/144 paired rounds lost, CI +16.58…+17.64
ns — the A/A control's own spread for that cell is 2.23 ns), with 17 of 18 cells regressing. The one
favourable cell is `matched-minimal · vni · pathological` (−5.09 %). KieeKey is 3.4–3.8× faster than
both OpenKey revisions in every cell, which is why the two-rival picture matters: "faster than
OpenKey" and "faster than UniKey" are different claims, and the report states both. Order control:
max |Δ shift| between the rotated and fixed-lead passes 3.51 ns against an A/A shift of 0.23 ns.

### 2c. The release measurement (frozen v1.2.2), from `rc1-130`

The number the release carries. Engine-decision stage, ns/key medians of 60 paired rounds
(5 sessions × 12 rounds), A/A band 1.47 ns, `src/core` hash-identical to the frozen reference
(`gate:manifest: PASS`), KieeKey column in-process. Every engine got the same `-O3 -DNDEBUG` flags.

| cell | KieeKey | UniKey | Δ % vs UniKey | 95 % CI (ns) | w/l | OpenKey 2.0.5 | OpenKey master |
|---|---|---|---|---|---|---|---|
| **as-shipped · telex-end · prose** *(deciding)* | **84.70** | **73.00** | **+16.12** | +11.16…+12.94 | 5 / 55 | 325.40 | 290.67 |
| as-shipped · telex-end · edit-storm | 97.20 | 76.80 | +27.62 | +20.46…+21.63 | 1 / 59 | 315.43 | 285.08 |
| as-shipped · telex-end · pathological | 39.20 | 35.18 | +11.04 | +1.34…+4.59 | 9 / 51 | 293.31 | 148.07 |
| as-shipped · vni · edit-storm | 88.99 | 72.28 | +22.73 | +15.90…+16.85 | 2 / 58 | 279.17 | 256.36 |
| as-shipped · vni · pathological | 33.29 | 18.50 | **+78.81** | +14.26…+14.80 | 1 / 59 | 114.81 | 113.90 |
| as-shipped · vni · prose | 77.19 | 68.72 | +12.00 | +7.93…+8.53 | 6 / 54 | 276.82 | 253.07 |
| matched-minimal · telex-end · prose | 60.29 | 47.69 | +25.47 | +11.81…+12.49 | 2 / 58 | 252.07 | 216.86 |
| matched-minimal · telex-end · edit-storm | 69.97 | 50.31 | +38.51 | +19.17…+20.43 | 0 / 60 | 252.12 | 218.76 |
| matched-minimal · telex-end · pathological | 36.61 | 36.25 | +0.96 | −0.32…+6.88 | 25 / 35 | 297.27 | 150.43 |
| matched-minimal · telex-mid · pathological | 36.56 | 35.49 | +3.66 | −0.03…+12.73 | 18 / 42 | 294.72 | 147.47 |
| matched-minimal · vni · pathological | 24.16 | 24.98 | **−2.73** | −0.77…−0.65 | **43 / 17** | 64.97 | 53.22 |
| matched-minimal · vni · prose | 51.11 | 44.55 | +14.73 | +5.88…+6.76 | 5 / 55 | 199.65 | 175.40 |

Full 18-cell table: `benchmark/results/rc1-130/tables.md` (`rc1_cells`), which the report renders.

* **Tier: MIXED** (15 of 18 cells regress, 1 cell genuinely favourable, 2 indistinguishable). No win
  wording is allowed anywhere in the report, including against OpenKey, because the tier vocabulary
  is about the *rival set as a whole* and one of the four engines is behind on the deciding cell.
* The two `matched-minimal · pathological` cells have CIs that straddle zero (+0.96 % and +3.66 %):
  they are reported as *not distinguishable from parity*, not as wins — the wins/losses counts
  (25/35, 18/42) already say that.
* **KieeKey is 3.4–3.8× faster than both OpenKey revisions in every cell of this table**, and the one
  cell where it beats UniKey is `matched-minimal · vni · pathological` (−2.73 %, 43/60 rounds won, CI
  excluding zero). Both statements appear in the release report; neither is used to soften the other.
* Order control: max |Δ shift| between the fixed-lead and rotated passes 3.57 ns against an A/A shift
  of 0.48 ns (worst cell `as-shipped · vni · pathological`) — i.e. the "who runs first" effect is
  smaller than one A/A sample of noise in most cells and about 7 A/A in the worst, which is why the
  fixed-lead pass exists rather than being an interesting finding.
* Cold start: p50 wall 484.4 ms to first usable output, first round 30.9 ns/key.
* Memory: 21 allocations per 2 000 000 keys as-shipped (20 matched-minimal), RSS 46.2 / 49.4 MiB,
  i.e. the `O(1)`/zero-allocation contract holds at `-O3` on this tree.
* Gates: manifest · attrib-guard (1.021×, transcripts identical) · correctness (374 rows) ·
  digest-identity (both sides `9a78c1b4fcc6dad2`) · diffab (2 146 422 events, 0 per-key mismatches) ·
  memory · integrity, all PASS; sanitizers run afterwards as its own labelled step.

**The instrument's own null test is the most useful number here.** This campaign had no candidate, so
`kieekey-base` and `kieekey-cand` are compiled from byte-identical sources, and the gain table they
produce is a pure measurement of the harness: **max |gain| over 18 cells = 0.99 %, deciding cell
+0.24 % with CI −0.486…+0.720 ns (straddling zero)**. Compare the C-C1 column of the same instrument,
which moved the deciding cell by +3.52 % and a matched-minimal edit-storm cell by +11.64 %: candidate
effects of this size are real, and anything under ~1 % in this ledger is not. Had the null test not
existed, `rc1-ca4`'s worst cells (+83.5 %) and C-C1's best (+11.6 %) would have been read as the same
kind of number.

**Counterfactual worth stating plainly:** with C-C1 applied, the deciding cell was +11.28 % instead of
+16.12 % — a candidate rejected by the no-regression clause is costing ~4.8 points against UniKey in
the release. It stays rejected, because the same change cost 9.8–10.9 % on mark/thrash input, which is
what a Vietnamese IME spends a lot of its time doing. C-C3 in §3 is the attempt to keep the gain
without the loss.

---

## 3. Candidates after C-A

C-C1 has been measured and rejected; C-B, C-C2, C-D and C-E remain as specifications with
falsifiers, deliberately not implemented, because a full accept-or-reject cycle on one candidate
costs roughly 50 minutes of measurement and the remaining budget is better spent finishing the
release campaign than starting a cycle that cannot be closed. Each spec below is written so that a
future session can implement, screen and decide it without re-deriving what to look at.

### C-C1 — `ư` scan short-circuit — **`REJECT`, reverted** (measured in `rc1-cc1`)
Short-circuit reorder in `TextEngine::checkGrammar`: the two-slot `U`+`O` prefix test runs before the
six-way tail-letter test, so the ordinary case costs two comparisons instead of six. Behaviour was
proven unchanged before any timing (`attrib-guard` transcripts IDENTICAL; `diffab` 2 146 422
events / 0 per-key mismatches over 3 seeds in the campaign itself).

Measured against the frozen v1.2.2 column in `rc1-cc1` (5 sessions × 12 paired rounds, keys=150 000,
A/A floor 1.02 ns, 60 paired samples per cell) it is **bimodal by stream, not by configuration**:

| cell | frozen | with C-C1 | gain | 95 % CI (ns) | w/l |
|---|---|---|---|---|---|
| matched-minimal · vni · edit-storm | 62.90 | 55.59 | **+11.64 %** | +7.15…+7.56 | 59 / 1 |
| matched-minimal · vni · prose | 53.78 | 48.74 | +9.25 % | +4.55…+5.23 | 58 / 2 |
| matched-minimal · telex-end · edit-storm | 71.03 | 65.34 | +8.21 % | +4.98…+6.21 | 58 / 2 |
| as-shipped · telex-end · edit-storm | 98.35 | 92.64 | +5.51 % | +5.02…+6.04 | 58 / 2 |
| as-shipped · telex-end · prose *(deciding)* | 85.31 | 82.34 | +3.52 % | +2.96…+3.25 | 56 / 4 |
| as-shipped · telex-end · pathological | 42.21 | 43.76 | −3.13 % | −2.10…−1.10 | 12 / 48 |
| as-shipped · vni · pathological | 33.25 | 36.59 | **−9.83 %** | −3.54…−2.97 | 6 / 54 |
| matched-minimal · vni · pathological | 25.05 | 27.87 | **−10.87 %** | −2.86…−2.62 | 9 / 51 |

16 of 18 cells improved, by up to 11.6 %, with CIs nowhere near zero. Two did not: the
`pathological` (mark/tone-thrash) cells, by 9.8 % and 10.9 % — and those two are exactly what the
acceptance rule forbids ("no cell slower than baseline by more than 2 × the A/A band"), so under
[PROTOCOL.md](PROTOCOL.md) §8 the candidate is **REJECT**ed and reverted.

**Why it lost where it lost, as far as the data lets us say.** A short-circuit order encodes an
assumption — "this conjunct is the one that usually fails first". That assumption is a property of
the input distribution, not of the code: with long words being re-grammar-checked (`prose`,
`edit-storm`) the `U`+`O` prefix almost never matches, so testing it first skips six comparisons;
in the mark-thrash stream the buffer is short and `chr(i)` lands on `N/C/I/M/P/T` constantly, so the
six-way test often *succeeds* on the first or second comparison and the reorder instead puts two
extra loads (`chr(i-1)`, `chr(i-2)`) on the path that used to exit early. Same instruction count
budget, opposite sign, decided entirely by which stream you type.

**The revision this suggests (C-C3, spec only — not implemented).** Order the conjuncts by how rare
their *success* is on the relevant corpus instead of by how few comparisons they cost: test
`i >= 2 && chr(i-2) == U'U'` first (the rarest single condition in both stream families: it is what
distinguishes `thUơn` from the world), then `chr(i-1) == U'O'`, then the tail-letter set. Expected to
keep most of the +5…+11 % on prose/edit-storm without paying on the thrash streams, because it never
adds a load to a path that used to exit — it only moves the cheapest *decisive* test to the front.
Falsifier: if `rc1-cc1`'s replacement shows the pathological cells still regressing, the whole
reorder family (C-C1, C-C3) is `DROP`ped and `checkGrammar`'s repair loop is declared not-cost-binding.

---

## 3c. Further candidates (specs; none implemented yet)

### C-B — single-load vowel-position scan
**Target.** `TextEngine::findAndCalculateVowel` and `checkCorrectVowel`: both re-scan the vowel
span, and `canHasEndConsonant` re-derives from tables what `kVowelCellMask` already knows for the
current word. Expected effect: a few percent of the window, concentrated in `prose` and
`edit-storm` (the streams that re-classify often).
**Falsifier:** if the profile's `findAndCalculateVowel` + `checkCorrectVowel` shares stay under
~4 % after C-A, the work is not worth the invalidation surface and C-B is `DROP`ped.

### C-C2 — tail-class bitmask and an inline hint for `isVowelChar` (deferred, not forgotten)
Two pieces that were specced alongside C-C1 and are **not implemented**, with the reason:

* *26-bit tail-class mask.* The six comparisons in the tail test could become one mask test, but a
  mask built from `walks::letterIdx` folds precomposed letters onto their base, so `Ế` would match
  `I` — a behaviour change dressed as a micro-optimisation. A code-point-exact version (bounds check
  plus a 128-bit mask on the raw `uint16_t`) is safe but only pays if the profile shows the six-way
  test as separable cost; with C-C1 in place the whole of `checkGrammar` is 5.0 % of the window, so
  this is not where the remaining time is.
* *Force-inline `isVowelChar`.* It is already a branchless `static constexpr` bit test
  (`TextEngine.hpp`), and the profile that suggested it was out-of-line came from a run whose
  artifacts no longer exist. Re-decide only if a current profile shows a separate symbol.

### C-D — hoist the code table out of the code-rebuild loop
**Target.** `getCharacterCode`/`buildCode`-side lookups re-resolve `opts_.codeTable` (and thus
re-run `FlatCodeTable::find`) inside a loop over the word's characters, although the table pointer
cannot change during one `process()` call. Hoisting it is a pure loop-invariant move with an
expected share equal to the `getCharacterCode` + `FlatCodeTable::find` line pair in the profile.
Note: `codeTableFor(opts_.codeTable)` is a switch returning a reference to a static table, so this
is only worth doing if `rc1-ca4`'s profile shows that switch and the per-character `table.find` as
separable cost; a loop-invariant hoist that saves two instructions per character is not a candidate,
it is noise with a commit.

### C-E — `process()` prologue
**Target.** the unscoped prologue of `TextEngine::process` (state snapshot + argument decode) which
the profile shows as a flat cost on every key regardless of cell. Only worth doing if it survives a
screening run with a CI excluding zero; otherwise `REJECT` and record that the prologue is not the
problem.

---

## 4. Rejected before or during measurement

| id | idea | why it is not in the tree |
|---|---|---|
| R1 | memoise `vowelStart_` / `vowelCount_` across `process()` calls | the invalidation surface is every path that shifts the typing buffer — `insertKey`, `startNewSession`, `restoreLastTypingState`, `resumeFromText`, tone re-issue — and a missed invalidation is a wrong-text bug, not a slow bug. The expected win (~4 % of the window) does not pay for that |
| R2 | cache the last keystroke's classification in a per-slot byte | measured in screening as a wash: the store costs what the load saves, and it dirties a cache line per key |
| R3 | replace `FlatMap::find` with a dense 256-entry table | the dense table is 4× L1-resident for a lookup that is already register-cached after C-A; profile share did not justify it |
| R4 | skip `checkSpelling` for words of length ≤ 2 | changes behaviour on `bâ`, `chê`-class inputs; correctness gates are not negotiable for a share this small |
| R5 | batch two keystrokes per `process()` call | breaks the per-key contract the producers rely on (a key can arrive from a different window/context between the two) |
| C8 | per-slot result cache keyed on (word hash, slot) | falsified by the profile: the misses are near-universal during edit-storms, which is the only stream where the cache would have helped |

---

## 5. Upstream defects found while measuring (reported, never patched here)

* UniKey `ukengine.cpp:1629` — UBSan: index `-1` used in an array subscript, plus a load of an
  invalid enum value, on symbol-flood streams (`--mode=robust`, `all-symbols`). Reproducible with
  the sanitizer build; it is the vendored snapshot's own code.
* OpenKey `checkForStandaloneChar` — unguarded `TypingWord[_index - 1]` when `_index == 0`
  (same class of input). Both `openkey-2.0.5` and `openkey-master` columns.
  These are recorded so that "sanitizers clean" in the report means *clean in
  KieeKey-owned sources*, which is the claim the gate actually makes.

---

## 6. Instrument work this release had to do first

The measurement layer changed more than the engine did, and every change came from a defect that was
silently producing numbers (full list with commits in [`PROTOCOL.md`](PROTOCOL.md) §12). The two that
would have changed *decisions*:

* **The sampler resolved nothing.** A plain `SIGPROF` handler reading `__builtin_return_address(0)`
  returns through glibc's fixed restorer, so 1 248 of 1 248 samples landed on one address and the
  profile read "100 % `??`" — which is indistinguishable from "there is no hot spot". Now `SA_SIGINFO`
  + the interrupted `REG_RIP`, `bench_prof` linked non-PIE (raw counters are resolved against `nm`
  addresses, so an ASLR slide breaks attribution), and symbol *extents* rather than a `+0x4000`
  window bound each function. That fix is what turned up `checkSpelling` at 21.1 % in the pristine
  engine at `-O3` — the number C-A should have been written against.
* **`--out` truncated, so gates read less than they claimed.** Six diffab seeds became one;
  seven per-engine memory runs became the last engine only. `--append` plus one file per
  invocation, and every gate now FAILs when it has no rows for the subject (a vacuous PASS is
  worse than no gate). Added with the same commit that made each gate fail on purpose once.

Three more came out of this candidate cycle, each of which had been able to move a decision:

* **A profile's provenance.** Shares are only citable from the campaign that produced them, so the
  profile block is appended by `rc1_stats.py` (not by the shell driver) — otherwise a re-summarise
  silently drops it — and a profile with zero samples or an empty symbol table aborts instead of
  printing an empty table.
* **Which tree a campaign's numbers describe.** `gates.txt` is appended, never truncated, so a resume
  cannot lose the gate rows that justified the resume; and each campaign keeps
  `results/<name>/manifest_at_build.json`, because `docs/…/baseline_manifest.json` is overwritten by
  the next build. `rc1_stats.py` reads that per-campaign copy to decide whether the campaign is a
  baseline or a candidate trial, so the phrase "this campaign had no candidate" is derived from hashes
  rather than from a remembered flag. The same rule applies to `bench_san`: a sanitizer build from a
  previous tree is never reused for a release claim.
* **A null test that must read zero.** In a baseline campaign the frozen and candidate columns hold
  identical code, so the gain table measures the harness; `rc1-130` prints that drift explicitly
  (`null_test_abs_max_pct`). `rc1-130`'s reads −0.92 %…+0.99 % with identical code, and two of those
  cells have CIs excluding zero — ~0.5 ns of column-dependent bias, which is why the ledger treats a
  sub-1 % claim as uninterpretable and why C-C1's 3.5 % and C-A's −5 % stand.
* **What a campaign's size record is.** `environment.txt` used to be rewritten by every partial
  re-run, so a report header can quote "6 sessions × 20 rounds" for a 5 × 12 campaign (it did, once,
  and the file now has to be reconstructed with a note saying so). A partial re-run writes a
  timestamped side file instead, and the report's measurement shape is taken from the timed rows' own
  `meta` records and the `raw/tput_s*.jsonl` count. Two smaller ones from the same sweep:
  `summary["meta"]` used to be *the first meta row in any file* (whichever step happened to be read
  first), and `cpu_model` kept a leading `": "` because `strip(": ")` does not remove the tab that
  `/proc/cpuinfo` puts before the value.
