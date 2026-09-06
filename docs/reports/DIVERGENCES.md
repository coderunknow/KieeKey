> **Lineage note (KieeKey v1.0 packaging):** Historical engineering
> document from the refactor lineage that produced KieeKey v1.0 (a fork of
> OpenKey, GPL-3.0). Written before the v1.0 release unification, it uses the
> pre-release working name "OpenKey NextGen" and internal milestone numbers
> (v3.0-v3.4). Published verbatim for traceability - see
> [docs/reports/README.md](README.md).

# DIVERGENCES — OpenKey-NextGen v3.1 vs the vendored references

Every intentional behavioral divergence from the unmodified OpenKey 2.0.5 reference (and from
the v3.0 lockstep) is listed here with a reproducer. Differences that are counted (not silently
absorbed) in the mega differential's Cat A/B/D are marked.

## 1. D4 — the space after a wrong-spelling Restore is re-issued (v3.1 contract)

- **Reproducer:** `arbit hoi doai ` (Telex, spelling ON, restore ON) — pre-v3.1 rendered
  `arbithối đoái ` (the space that triggered the wrong-spelling Restore was eaten);
  v3.1 renders `arbit hối đoái `.
- **Mechanism:** consumer contract only — the engine-level delta for a Space-Restore
  (`backspaceCount = index_`, replacement = raw keystrokes) is byte-identical to 2.0.5's
  engine delta (probe-verified via the ok205 wrapper). The shipped Win32 producer and the
  verification consumer now append the space after the restore edit, exactly mirroring the
  typed-character re-issue of hotfix §3.
- **Rationale:** the 3-way benchmark found the eaten space as a NextGen-specific defect
  (defect D4); UniKey's front-end passes the break char through.
- **Counted in:** suite-12 Cat D growth (~+16.5k of the 427,192) — the 2.0.5 wrapper model
  eats the space where v3.1 keeps it.

## 2. D2-emission — restored/transformed entries always render visibly

- **Reproducer:** `wiaas ` — the `s` event rewrites `ưiâ` → `ưiấ` (v3.1) where v3.0 emitted
  only `iấ` (the standalone `ư` resolved to 0 and vanished from the screen while
  `backspaceCount` still counted it); `asaa` walks `á → ấ → á` where v3.0 lost the `á`.
- **Mechanism (engine + oracle mirror):**
  1. `resolveChar` resolves standalone ư/ơ entries (including tone-stripped ones) to their
     vowel, and any raw entry to its key + caps (the legacy hook's `SendKeyCode` semantics) —
     a stray mark mask on a consonant can no longer blank the character.
  2. The tone-toggle restore paths (`insertAOE`, `insertW`) emit `getCharacterCode(entry)` —
     the still-marked letter (`ấ` toggles to `á`) — instead of the raw coded entry.
  3. ASCII `[` / `]` (the standalone-bracket keys, stored as characters in the normalized
     buffer) resolve to themselves via identity table entries.
- **Rationale:** the display must match the engine buffer — a dropped character desynchronized
  the committed text from the engine's view and produced the over-backspace family (1,670
  events). The 2.0.5 wrapper model renders some of these entries as literal non-characters;
  v3.1 renders the intended letter — counted in Cat D.

## 3. D2-policy — `backspaceCount` never exceeds the committed text

- **Mechanism:** the engine tracks the consumer-visible account (`visibleAccount_`, mirrors
  the verification consumer exactly and is a conservative lower bound for real consumers) and
  clamps `backspaceCount` in `finalizeResult`; debug asserts pin the invariant. The
  consumer-side clamp stays as belt-and-braces.
- **Rationale:** without it, a consumer without its own clamp deletes text the user never
  asked to delete (observed as the 1,670-event family). Where the clamp engages, the emitted
  `backspaceCount` is smaller than the legacy request; the replacement payload is unchanged,
  so consumers that clamped identically (the harness, the shipped ring consumers) produce
  byte-identical text.

## 4. D3 — the macro expansion rides in the result and the consumer applies it

- **Reproducer:** `bt ` with the harness macro table — v3.1: `ReplaceMacro` with
  `macroExpansion = "bình thường"`, the consumer deletes 2 chars and types the expansion;
  pre-v3.1 the raw space+keys passed through and the expansion was dropped (462,627 gap
  events).
- **Mechanism:** `EngineResult::macroExpansion` (final Unicode code points, resolver-supplied);
  the shipped Win32 producer suppresses the break key and applies delete+expansion (chunked
  inline SendInput when the expansion exceeds the ring item's capacity); the TSF path receives
  it as a normal edit. The break key itself is consumed (macro + space → expansion, no extra
  space) — unchanged from the wrapper's model of 2.0.5.

## 5. D1 — the restore scratch is consumed

- **Mechanism:** `restoreLastTypingState()` (engine + oracle mirror) clears
  `typingStatesData_` after distributing the restored entry; the save* paths pre-clear and
  flush at `>= kMaxBuff`; `pushTypingState` asserts the bound. No observable text change —
  this closes the suite-7 stack-buffer-overflow precondition (10 pre-empted events).

## 6. P1 — dictionary-gated restore (feature-gated, OFF in matched config)

- **Mechanism:** with `useDictionaryRestore` ON and a lexicon resolver installed, the
  word-break restore decision becomes: revert the pending composition to the raw keystrokes
  **only when the composed form differs from the raw typing AND is not a lexicon word**. The
  lexicon supersedes the structural restore (a composed word the dictionary knows — e.g. the
  compound `đăngten` — survives even though one syllable is structurally invalid) and adds a
  veto for structurally-valid non-words (`bata → bât` reverts to `bata`).
- **With the feature OFF the engine is byte-identical to v3.0** — the matched-config runs
  (restore=OFF ×3, dict OFF) stay exactly equal to the vendored 2.0.5 engine (64,862 / 65,107
  result sets, both methods).
- **Lexicon provenance:** the general Vietnamese word list built from the corpus vocabulary,
  committed as data (`imebench_kit/results` pipeline + the app's lexicon data file); it is a
  *veto* only — it never injects text, and correctness holds with it disabled.
- **Residual losses RESOLVED in v3.3.1 (lexicon-gated, see V331_FIX_REPORT.md):**
  - `huơ` (and the `khuơ`/`huơ tay` family): when the composed form (`hươ` — the W-section
    hook u+o → ươ) is NOT a lexicon word, the engine now offers the partial-composition
    candidate (the u keeps its W-hook cleared: `huơ`) and emits it when the lexicon approves
    — corpus-verified: `huow` → `huơ `, `khuow` → `khuơ ` (UniKey itself fails `khuơ`,
    writing `khươ`).
  - `mono`: a NON-adjacent vowel toggle (insertAOE crossing a final consonant — the
    `kVowel['O']` `{O,N}` pattern family) is flagged mid-word; at the word break, when BOTH
    the composed form and the raw keystrokes are lexicon words, the raw keystrokes win
    (UniKey's mid-word strictness, lexicon-gated). Corpus-verified: `mono` → `mono ` while
    `moon` → `môn ` and `moong` → `mông ` (adjacent toggles) are unchanged.
  - Both refinements live inside `useDictionaryRestore` only — the matched config stays
    byte-identical to v3.0/2.0.5 (re-verified: 0 diffs vs the frozen v3.1 matched runs on
    the full 66,552-row corpus, both methods).

## 7. Pre-existing documented differences (carried forward)

- Cat A (~675): 2.0.5 tone-mark placement differences (same letters, mark on a different
  vowel) — the corpus's orthographic variants.
- Cat B: the 2.0.5 tag's tone-mark bug fixed from upstream master; the brace bug.
- Cat D (~427k): the documented rule differences (free-marking semantics, standalone-key
  boundaries, VNI digit handling, quick-consonant behaviour, plus items 1–2 above).
- The shipped default differs from 2.0.5's: `restoreIfWrongSpelling = true` (that is the
  as-shipped config the 3-way benchmark measured; matched-config parity is preserved with it
  off).

## 8. v1.2.2 RC2 — VIQR precedence over legacy code tables (sanctioned render change)

`OutputEncoding::Viqr` is defined on the precomposed-Unicode pipeline. Combined with a
LEGACY `CodeTable` (TCVN3 / VNI-Windows / UnicodeCompound / CP1258) the table rendering
now wins outright: `replacementUtf16()` applies the VIQR mnemonic map **only** when
`codeTable == Unicode`. Pre-RC2 the map was layered on top of whatever the table
resolver emitted, which by luck was self-consistent for TCVN3/VNI-Windows (their code
points miss the map and passed through untouched) but produced MIXED content for
UnicodeCompound/CP1258 (precomposed entries became ASCII mnemonics while base+mark and
pre-mark entries leaked raw combining sequences) — violating the pure-ASCII channel
contract that the v1.2.0 macro-parity fix established on the macro path.

`kViqrMap` was also audited for completeness against the FULL value domain of the Unicode
code tables (FlatTables data): exactly one reachable non-ASCII value missed the map —
U+0168 Ũ, present lowercase-only (U+0169 → "u~"). RC2 adds Ũ → "U~". The pre-existing
double mapping of Ũ's other code point (U+1EE6 → "U?", the historical row) is kept frozen:
it is what the shipped pre-RC2 engine emitted and the differential pins it.

Reachability: the shipped app never sets `outputEncoding` (library-only option), so
there is no product-visible change; the differential `diff_engine_ab` reports the
exempted cell count (`viqrPrecedenceRenderSkips`, 15 026 / 1.2 M events — all in the
legacy-table × VIQR matrix cells) and verifies decision-identity everywhere else.
Pinned by `ok_option_matrix` tier 1 (projection at Unicode, table-precedence lockstep
at every legacy table) and tier 3 RENDER (per-event projection identity + pure-ASCII
channel assertion). `macroExpansionUtf16()` remains table-independent by design: the
expansion payload is final Unicode code points, not table-resolved glyphs.

## 9. v1.2.2 RC2 — configuration-change reset vs startNewSession()

`startNewSession()` keeps its RC1 contract (word-break / backspace flows legitimately
read `specialChar_`, `spaceCount_` and the undo ring back AFTER it returns — clearing
there would corrupt the commit path). A RECONFIGURATION has no such continuation and
must not inherit pre-switch scratch: `resetForConfigurationChange()` is the dedicated,
full reset for that pattern (keeps resolvers, `opts_`, and the visible-character
account, which mirrors the consumer document, not the session). The Win32 settings
dialogs and all WinUI reconfigure handlers (including the code-table combo, which had
no reset at all pre-RC2) call it immediately after `setOptions()`. Repro families and
coverage: `ok_option_matrix` tier 3 CONTRACT mode (hot switch + app-contract reset ≡
fresh engine, 141 pairs × 4 switch points).

## 10. Audit: `getCharacterCode` and the VIQR output encoding (RESOLVED — no defect)

A review note flagged that `getCharacterCode` "overrides VIQR with Unicode" at
`TextEngine.cpp` near the function body. Verified this pass: the function is **character-
identical to the frozen 1.2.1 implementation** (`difflib` byte-compare of the extracted
bodies → equal). There is no silent override: `getCharacterCode` decodes the internal
*codepoint model* (Unicode is the engine's canonical internal representation), and
VIQR encoding is applied at render time in `replacementUtf16` only — which is exactly
the layering the sanctioned §8 render-contract changes also live at. The two callers'
families (old-backspace re-emits) feed the result through the normal output path, so
they re-acquire VIQR correctly. Nothing to fix; documented here so the note dies with
evidence instead of folklore.
