# v1.3.0 RC1 optimisation ledger

Every candidate gets an entry — implemented and measured, or rejected with a reason — because the
useful part of optimisation work is the map of what does not pay. Rules that decide ACCEPT/REJECT
are pre-registered in [`PROTOCOL.md`](PROTOCOL.md) §8; the numbers below are campaign artifacts, and
"measured" always names its campaign. No entry is deleted when it fails.

Statuses: `LAND` (in `src/core`, gate-verified) · `MEASURE` (measured, verdict pending) ·
`REJECT` (measured and discarded) · `DROP` (not written: falsified before implementation) ·
`BLOCKED` (needs an instrument this repo does not have).

---

## 1. How a candidate gets here

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

## 2. C-A — spelling walks as bitmask automata — `LAND` (verdict: pending `rc1-ca4`)

**Target.** `TextEngine::checkSpelling` / `checkGrammar` walked the vowel and consonant tables
per keystroke, repeatedly, inside the same word: every character of a typing buffer was re-classified
by table lookups (`FlatMap::find`, `FlatCodeTable::find`) even though a Vietnamese word is at most
~30 characters and its classification is a small finite problem. This was the largest single share
of the profile window before the change.

**Mechanism.** `src/core/ConsonantWalks.hpp` — a compile-time-built automaton (`buildLeadTables`,
`buildEndTables`, both `constexpr`) representing legal leading-consonant and end-consonant/vowel
patterns as bitmask walks over a 26- or 64-entry alphabet, plus
`kVowelCellMask` / `kToneWMask`-style masks so a classification question becomes a mask test
against a per-word bitset rather than a table chain. `TextEngine.cpp` consults the automata where
it previously looped. The ablation switches `KK_WALK_FAST_LEAD` / `KK_WALK_FAST_END` allow either
half to be turned off for measurement without editing code.

**Correctness argument (verified, not asserted).**

* Exhaustive equivalence: `kk_walks_selftest` compares every automaton decision against a
  reference implementation over 12 519 536 leading and 6 955 302 end-of-word cases; the digest of
  the reference set is pinned (`2914537616136238717`) so a regression in the *oracle* is caught too.
  Driver: `benchmark/harness/rc1.hpp::modeWalks`, gate `walks`.
* Behavioural identity: per-key differential against the frozen v1.2.2 engine (0 mismatches) and
  output-digest identity against the `kieekey-base` column of the same campaign.

**Measured.** Pending `rc1-ca4` — the L1 table, the `kieekey-cand` vs `kieekey-base` gain block
and the candidate ACCEPT/REJECT decision will be quoted here from
`benchmark/results/rc1-ca4/{tables.md,summary.json}` when the campaign finishes, together with the
correctness gate lines. Screening runs before the campaign indicated the automata are a small
fraction of the window while the *surrounding* scans (`ư` handling in `checkGrammar`,
`isVowelChar`, the vowel-position search) dominate; that is what C-B/C-C below target, and the
`rc1-ca4` profile is the artifact that decides whether C-A is scored as a win on its own merits or
as enabling work for them.

**Files.** `src/core/ConsonantWalks.hpp` (new), `src/core/TextEngine.cpp`, `src/core/TextEngine.hpp`,
`benchmark/harness/{engines.hpp,rc1.hpp}` (oracle driver), commit `0de94ce`.

---

## 3. Next candidates (specs; none implemented yet)

### C-B — single-load vowel-position scan
**Target.** `TextEngine::findAndCalculateVowel` and `checkCorrectVowel`: both re-scan the vowel
span, and `canHasEndConsonant` re-derives from tables what `kVowelCellMask` already knows for the
current word. Expected effect: a few percent of the window, concentrated in `prose` and
`edit-storm` (the streams that re-classify often).
**Falsifier:** if the profile's `findAndCalculateVowel` + `checkCorrectVowel` shares stay under
~4 % after C-A, the work is not worth the invalidation surface and C-B is `DROP`ped.

### C-C — `ư` scan reorder (`C-C1`, implemented) + the parts deferred on evidence
**Target.** Three verified hot spots: (i) the `ư` reconstruction test in `checkGrammar` compares
tone-mask residues of two adjacent slots before it has established that the pair is `U`+`O` at all —
reordering to `i >= 2 && chr(i-1)==U'O' && chr(i-2)==U'U'` first turns a mask XOR into a cheap
mismatch exit; (ii) the tail-consonant question (C I M N P T) becomes a 26-bit
`kTailClassMask` test; (iii) `isVowelChar` is out-of-line in the `-O3` build (visible as a separate
symbol in the profile), which a `KK_FORCE_INLINE` on the mask-test variant removes.
**Status.** `C-C1` is implemented (short-circuit reorder of the double-`ư` repair loop in
`TextEngine::checkGrammar`: the `chr(i-1) == U'O' && chr(i-2) == U'U'` test now runs before the
six-way tail-letter test, so the ordinary case costs two comparisons instead of six, and `i >= 2`
still precedes every `i - 1` / `i - 2` read). It changes no predicate, only their order.
`C-C2` (force-inline `isVowelChar`) and `C-C3` (a 26-bit `kTailClassMask` replacing the six
comparisons) are **deferred, not forgotten**: the profile that motivated them was measured by a run
whose artifacts are no longer in the repository, and `isVowelChar` is already a branchless
`static constexpr` bit test in this tree — re-measuring it against the `rc1-ca4` profile has to come
before adding an inline hint or a table. A mask built from `letterIdx` would also be a *behaviour*
change, because `letterIdx` folds precomposed letters onto their base while the current test
compares raw code points (`U'Ế'` must not match `I`) — that variant is only admissible with a
differential that proves the fold away, so it stays out until it is worth that risk.

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

The measurement layer changed more than the engine did during this cycle, and each change came
from a defect that was silently producing numbers: the attribution pair measured an early-out
(PROTOCOL §7), `--out` truncated multi-invocation artifacts, percentiles were read unsorted, and
an unknown engine name fell through to the rival. See PROTOCOL §12 for the list with commits. The
campaign's `--mode=attrib-guard`, `walks`, and `digest-identity` gates exist so these cannot return
as a report instead of as a bug.
