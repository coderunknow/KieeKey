> **Lineage note (KieeKey v1.0 packaging):** Historical engineering
> document from the refactor lineage that produced KieeKey v1.0 (a fork of
> OpenKey, GPL-3.0). Written before the v1.0 release unification, it uses the
> pre-release working name "OpenKey NextGen" and internal milestone numbers
> (v3.0-v3.4). Published verbatim for traceability - see
> [docs/reports/README.md](README.md).

# OpenKey NextGen — Hotfix Report: caret/edit desync + Ctrl+Shift auto-off + tone-key Restore

**Date:** 2026-08-29 · **Scope:** three user-reported bug families, root-caused, fixed
in the engine and/or the app consumer, verified by new regression tests and all existing
suites (including the full mega differential).

---

## 1. "I delete 'gsn' and type 'sng' to get 'chúng' — it just appears as 'chusng'"

### Reproduction (the user's exact words)
Typing a wrong word, deleting part of it, and retyping the tone letters produced raw
text instead of composing:

```
type  c h u g s n        (visible: chugsn)
delete g s n             (user's method: selection delete / mouse, or backspace)
type  s n g              expected "chúng", got "chusng"
```

### Root cause — two independent mechanisms

**(a) Engine re-sync loss after app-level edits (the core bug).**
When the user deletes a chunk with a **selection** (mouse drag or Shift/Ctrl+Shift+arrows)
or **clicks** to reposition the caret, the deletion happens directly in the application —
the IME engine never sees it. The engine's raw word buffer still holds `chugsn` (or, after a
`MouseDown`, is reset to empty), while the visible text is `chu`. Typing `s` then cannot
compose: Telex `s` only makes sense on top of a base word the engine knows about. The raw
`s n g` keys pass through → `chusng`.

Verified at engine level in all three models (NextGen, clean-room oracle, vendored 2.0.5):

| scenario | NextGen | oracle | 2.0.5 |
|---|---|---|---|
| plain 3×Backspace then `sng` | `chúng` ✓ | `chúng` ✓ | `chúng` ✓ |
| mouse click / selection delete then `sng` | `chusng` ✗ | `chusng` ✗ | `chusng` ✗ |

The plain-backspace path was already correct; the selection/click path is a classic IME
desync (OpenKey 2.0.5 behaves identically — but the user asked us to fix it).

**(b) The IME silently toggled OFF (see §2) — any Ctrl+Shift+arrows selection also
switched the engine off**, so even correctly-typed letters went through raw.

### The fix

1. **Engine — `TextEngine::resumeFromText(word)`** (new, additive):
   when the caret moved / text changed outside the keystroke stream, the app reads the
   visible raw word before the caret and quietly replays it through the engine's own state
   machine (outputs discarded). The next keystroke then composes onto the visible word.
   Safe by construction: only pure ASCII letters are replayable; already-composed or
   non-letter text returns `false` and leaves a clean session (raw typing, no corruption).

2. **App (win32 tray + WinUI)** — after `MouseDown` and on caret/edit keys
   (arrows, Delete, Home/End/PgUp/PgDn, Ctrl+Backspace, Ctrl+arrows), the consumer thread
   queries the TSF document (`TsfComposer::textBeforeCaret`) and calls `resumeFromText`.

### Verified
`tests/test_hotfix.cpp` (new regression suite):
- `chugsn` + app-delete `gsn` + MouseDown + `resumeFromText("chu")` + `sng` → **`chúng`** ✓
- `khong` + `resumeFromText("khong")` + `s` → `khóng` ✓
- composed / empty / punctuation context → not resumable, raw typing (safe) ✓

---

## 2. "Sometimes it turns off with unknown causes — I already disabled auto turn-off"

### Root cause
The global **Ctrl+Shift toggle hotkey** fired on **any** Ctrl+Shift chord. The producer
treated the *second modifier's* key-down while the other was held as the toggle — so every
common application shortcut silently switched Vietnamese input off:

- `Ctrl+Shift+S` (Save As), `Ctrl+Shift+T`, `Ctrl+Shift+Esc`, `Ctrl+Shift+Tab` …
- **`Ctrl+Shift+arrows` (word/text selection)** — extremely common while editing,
  including exactly the delete-a-chunk workflow in §1.

The icon turned gray and the engine stopped processing — the "unknown causes". The
auto-exclude settings (Ứng dụng tab) were unrelated; there was no way to disable the hotkey.

### The fix
New pure state machine `ok::hotkey::CtrlShiftChord` (`src/core/CtrlShiftChord.hpp`):
the toggle fires **only on a clean bare chord** — Ctrl+Shift pressed and released with
**no other key in between**. Any non-modifier key between them (S, arrows, Esc, Tab, …)
cancels the chord, so application shortcuts never toggle the IME.

### Verified
`tests/test_hotfix.cpp`:
- bare Ctrl+Shift press+release → **Toggle** ✓
- Ctrl+Shift+S / Ctrl+Shift+arrows / Ctrl+Shift+Esc / Ctrl+Shift+Alt → **no toggle** ✓
- a clean chord after a cancelled one still toggles ✓
- lone Ctrl or Shift taps never toggle ✓

---

## 3. Tone-key Restore — "I must press 'f' twice to get 'chaof'", "delete all and retype gives no tone marks", "'chàf' instead of 'chaf'"

### Reproduction (the user's exact words)
```
type  c h a o f              (visible: chào)
press f                      expected "chaof", got "chao"     (tone mark deleted, key eaten)
press f again                only now "chaof" — must touch 2 times

type  c h a o f   (chào)  →  Backspace (deletes o)  →  press f
                             expected "chaf", got "chàf" or "cha"

type  c a s   (cá)  →  press s
                             expected "cas", got "ca"         (tone toggled off, key eaten)
```
Plus: "sometimes, when I delete all characters and type again, it does not become have
tone marks for unknown reasons."

### Root cause — the consumer dropped the typed key after an engine `Restore`
When a tone key is pressed while that exact tone mark is already on the vowel, the engine
answers with **`EngineCode::Restore`**: it reverts the word to its bare spelling
(backspace + replacement — the "toggle the tone off" half of the operation). OpenKey
2.0.5's hook then **re-sends the typed key** to the application, so the tone is toggled
off *and* the character still lands. NextGen's consumer applied only the backspace +
replacement and **never re-issued the typed key** — one press silently deleted the tone
mark and swallowed the letter.

Three-way differential (NextGen / clean-room oracle / vendored 2.0.5, exact app-visible
strings, full commit semantics):

| sequence | NextGen (old) | oracle (old) | 2.0.5 |
|---|---|---|---|
| `chaof` then `f` | `chao` ✗ | `chao` ✗ | `chaof` ✓ |
| `chaof`, Backspace, `f` | `cha` ✗ | `cha` ✗ | `chaf` ✓ |
| `cas` then `s` | `ca` ✗ | `ca` ✗ | `cas` ✓ |
| `chào`, delete-all, retype `chaof` | `chào` ✓ | `chào` ✓ | `chào` ✓ |

The engine and the oracle were already in lockstep; the *consumer* (shipped app) was the
only place the semantics diverged. Note the fourth row: the delete-all-and-retype flow is
**correct at the model level** in all three implementations — the live complaint matches
the already-fixed spontaneous Ctrl+Shift toggle (old build) or an app-side race; the new
regression suite pins the model-correct behavior so any future live regression is
isolated immediately.

### The fix — one small consumer change, exactly the legacy-hook contract
`src/app/main.cpp` (the low-level hook's producer decision): when the engine answers a
**character** input with `Restore` / `RestoreAndStartNewSession`, the emitted edit now
appends the typed character after the backspace + replacement — mirroring 2.0.5's
`SendKeyCode(_keycode|CAPS_MASK)` re-send. Space / Backspace / word-break / mouse
Restores keep their existing semantics (no re-issue) — verified against 2.0.5.

This closes the gap between the shipped app and the mega harness, which already modelled
the re-issue (its 0-mismatch differential vs 2.0.5 is unchanged).

### Verified
`tests/test_hotfix.cpp` §3 (new — 21 checks, all green):
- `chaof`+`f` → **`chaof`** (one press), `chaof`+`f`+`f` → `chaoff` ✓
- `chào`+Backspace → `chà`, then `f` → **`chaf`** (not `chàf`, not `cha`), +`f` → `chaff` ✓
- `cas`+`s` → **`cas`**, then `s`→`cass`, then `s`→`casss` (tone-toggle family) ✓
- Space after a toggle → exactly one space (2.0.5 semantics, no re-issue) ✓
- delete-all+retype (`chaof`→BS×5→`chaof`→**`chào`**), toggle+delete-all+retype,
  select-all+delete (app-level) + retype → **`chào`** ✓
- mixed `chaof` toggle → space → `cas` toggle → **`chaof cas`** ✓

Every sequence is also verified equal to the unmodified 2.0.5 engine and the clean-room
oracle at engine level (3-way agreement), and the full mega differential (5.9 M cases /
259 M events) re-ran with **0 text mismatches** — the engine/oracle/2.0.5 lockstep is
unchanged by the consumer fix.

---

## 4. "Typing and deleting quickly leaves ghosting/stuck characters" — long-word+backspace undo-history overflow

### Reproduction
Typing a long word (longer than the 32-entry undo-history scratch), immediately
backspacing it, and typing another long word could crash the engine or leave a stuck
(preview) character: the undo-history scratch was not cleared between saved words, and
its flush trigger (`== kMaxBuff`) never fired once the scratch was already full, so
`pushTypingState()` wrote past its fixed 32-entry array (stack-buffer-overflow, ASan).

### Root cause
`TextEngine::saveWord()` reuses a shared scratch buffer (`typingStatesData_`). After a
fast backspace-restore cycle the scratch already held 32 entries; a subsequent long-word
flush grew it to 39 and `pushTypingState()` overflowed its `std::array`.

### The fix — general, not per-case
`src/core/TextEngine.cpp`:
- `typingStatesData_.clear()` unconditionally at the start of `saveWord()` (and of the
  macro/replace path), so each saved word starts from an empty scratch;
- every flush trigger changed from `== kMaxBuff` to `>= kMaxBuff`, so the buffer can
  never be overrun regardless of prior state.

### Verified
- New `tests/test_hotfix.cpp` Bug 4 block: 40-char word + space → 41; backspace → 40;
  same long word + space → 81, all clean (previously an ASan WRITE at
  `TextEngine.cpp:554`). The old code fails this test with a stack-buffer-overflow; the
  fixed code passes under ASan/UBSan.
- All existing suites re-run unchanged (see table below).

---

## Regression status (all suites re-run after the fixes)

| Suite | Result |
|---|---|
| `tests/test_hotfix.cpp` (new) | ALL PASSED (incl. §3 tone-key re-issue, 21 checks, + Bug 4 long-word regression) |
| `tests/test_textengine.cpp` (golden) | ALL TEXTENGINE TESTS PASSED |
| `tests/dirty_input.cpp` (symbol regression) | 77 PASS / 0 ENGINE-DEFECT / 3 205-QUIRK, engine==oracle 80/80 |
| `tests/real_passages.cpp` | engine==oracle==2.0.5 32/32 |
| `tests/edge_behaviors.cpp` | 32/34 PASS, 0 ENGINE-DEFECT, 2 BOTH-DIFF (documented shared), WPM 8/8, per-prefix 1440/1440 |
| `tests/mega_correctness.cpp` | 0 text mismatches, full run (5,966,887 cases / 259,320,318 events) |

The vendored OpenKey 2.0.5 engine is untouched and remains the gold reference.
