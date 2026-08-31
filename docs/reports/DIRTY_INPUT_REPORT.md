> **Lineage note (KieeKey v1.0 packaging):** Historical engineering
> document from the refactor lineage that produced KieeKey v1.0 (a fork of
> OpenKey, GPL-3.0). Written before the v1.0 release unification, it uses the
> pre-release working name "OpenKey NextGen" and internal milestone numbers
> (v3.0-v3.4). Published verbatim for traceability - see
> [docs/reports/README.md](README.md).

# Real-input dirty differential test — results

Symbol keys are fed **exactly like the shipped NextGen app** (`MainWindow::produceChar`):
an `InputKind::Char` event carrying the **symbol character** with the shift flag set —
NOT an artificial `WordBreak` event. Consumers use the real hook semantics
(consumed = suppressed + backspace/replacement; not consumed = key reaches the app).

Models: **NextGen TextEngine** (shipped) vs **oracle** (clean-room state machine) vs
**OpenKey 2.0.5** (vendored legacy engine). Every case carries the intended text.

**Verdict classes:** PASS (engine==intended and 2.0.5==intended) ·
**ENGINE-DEFECT** (engine loses data, 2.0.5 correct) · **205-QUIRK** (2.0.5 loses data,
engine correct) · BOTH-DIFF (both differ).

## Per-case results

| # | case | cat | intended | engine | oracle | 2.0.5 | verdict |
|---|---|---|---|---|---|---|---|
| 1 | A-composed+! | A-symbol-after-composed | `thôi! ` | `thôi! ` | `thôi! ` | `thôi! ` | **PASS** |
| 2 | A-composed+@ | A-symbol-after-composed | `thôi@ ` | `thôi@ ` | `thôi@ ` | `thôi@ ` | **PASS** |
| 3 | A-composed+# | A-symbol-after-composed | `thôi# ` | `thôi# ` | `thôi# ` | `thôi# ` | **PASS** |
| 4 | A-composed+$ | A-symbol-after-composed | `thôi$ ` | `thôi$ ` | `thôi$ ` | `thôi$ ` | **PASS** |
| 5 | A-composed+% | A-symbol-after-composed | `thôi% ` | `thôi% ` | `thôi% ` | `thôi% ` | **PASS** |
| 6 | A-composed+^ | A-symbol-after-composed | `thôi^ ` | `thôi^ ` | `thôi^ ` | `thôi^ ` | **PASS** |
| 7 | A-composed+& | A-symbol-after-composed | `thôi& ` | `thôi& ` | `thôi& ` | `thôi& ` | **PASS** |
| 8 | A-composed+* | A-symbol-after-composed | `thôi* ` | `thôi* ` | `thôi* ` | `thôi* ` | **PASS** |
| 9 | A-composed+( | A-symbol-after-composed | `thôi( ` | `thôi( ` | `thôi( ` | `thôi( ` | **PASS** |
| 10 | A-composed+) | A-symbol-after-composed | `thôi) ` | `thôi) ` | `thôi) ` | `thôi) ` | **PASS** |
| 11 | A-composed+_ | A-symbol-after-composed | `thôi_ ` | `thôi_ ` | `thôi_ ` | `thôi_ ` | **PASS** |
| 12 | A-composed++ | A-symbol-after-composed | `thôi+ ` | `thôi+ ` | `thôi+ ` | `thôi+ ` | **PASS** |
| 13 | A-composed+{ | A-symbol-after-composed | `thôi{ ` | `thôi{ ` | `thôi{ ` | `thooi[` | **205-QUIRK** |
| 14 | A-composed+} | A-symbol-after-composed | `thôi} ` | `thôi} ` | `thôi} ` | `thôi} ` | **PASS** |
| 15 | A-composed+| | A-symbol-after-composed | `thôi| ` | `thôi| ` | `thôi| ` | `thôi| ` | **PASS** |
| 16 | A-composed+: | A-symbol-after-composed | `thôi: ` | `thôi: ` | `thôi: ` | `thôi: ` | **PASS** |
| 17 | A-composed+" | A-symbol-after-composed | `thôi" ` | `thôi" ` | `thôi" ` | `thôi" ` | **PASS** |
| 18 | A-composed+< | A-symbol-after-composed | `thôi< ` | `thôi< ` | `thôi< ` | `thôi< ` | **PASS** |
| 19 | A-composed+> | A-symbol-after-composed | `thôi> ` | `thôi> ` | `thôi> ` | `thôi> ` | **PASS** |
| 20 | A-composed+? | A-symbol-after-composed | `thôi? ` | `thôi? ` | `thôi? ` | `thôi? ` | **PASS** |
| 21 | A-composed+~ | A-symbol-after-composed | `thôi~ ` | `thôi~ ` | `thôi~ ` | `thôi~ ` | **PASS** |
| 22 | B-plain+! | B-symbol-after-plain | `xong! ` | `xong! ` | `xong! ` | `xong! ` | **PASS** |
| 23 | B-plain+@ | B-symbol-after-plain | `xong@ ` | `xong@ ` | `xong@ ` | `xong@ ` | **PASS** |
| 24 | B-plain+# | B-symbol-after-plain | `xong# ` | `xong# ` | `xong# ` | `xong# ` | **PASS** |
| 25 | B-plain+$ | B-symbol-after-plain | `xong$ ` | `xong$ ` | `xong$ ` | `xong$ ` | **PASS** |
| 26 | B-plain+% | B-symbol-after-plain | `xong% ` | `xong% ` | `xong% ` | `xong% ` | **PASS** |
| 27 | B-plain+^ | B-symbol-after-plain | `xong^ ` | `xong^ ` | `xong^ ` | `xong^ ` | **PASS** |
| 28 | B-plain+& | B-symbol-after-plain | `xong& ` | `xong& ` | `xong& ` | `xong& ` | **PASS** |
| 29 | B-plain+* | B-symbol-after-plain | `xong* ` | `xong* ` | `xong* ` | `xong* ` | **PASS** |
| 30 | B-plain+( | B-symbol-after-plain | `xong( ` | `xong( ` | `xong( ` | `xong( ` | **PASS** |
| 31 | B-plain+) | B-symbol-after-plain | `xong) ` | `xong) ` | `xong) ` | `xong) ` | **PASS** |
| 32 | B-plain+_ | B-symbol-after-plain | `xong_ ` | `xong_ ` | `xong_ ` | `xong_ ` | **PASS** |
| 33 | B-plain++ | B-symbol-after-plain | `xong+ ` | `xong+ ` | `xong+ ` | `xong+ ` | **PASS** |
| 34 | B-plain+{ | B-symbol-after-plain | `xong{ ` | `xong{ ` | `xong{ ` | `xong{ ` | **PASS** |
| 35 | B-plain+} | B-symbol-after-plain | `xong} ` | `xong} ` | `xong} ` | `xong} ` | **PASS** |
| 36 | B-plain+| | B-symbol-after-plain | `xong| ` | `xong| ` | `xong| ` | `xong| ` | **PASS** |
| 37 | B-plain+: | B-symbol-after-plain | `xong: ` | `xong: ` | `xong: ` | `xong: ` | **PASS** |
| 38 | B-plain+" | B-symbol-after-plain | `xong" ` | `xong" ` | `xong" ` | `xong" ` | **PASS** |
| 39 | B-plain+< | B-symbol-after-plain | `xong< ` | `xong< ` | `xong< ` | `xong< ` | **PASS** |
| 40 | B-plain+> | B-symbol-after-plain | `xong> ` | `xong> ` | `xong> ` | `xong> ` | **PASS** |
| 41 | B-plain+? | B-symbol-after-plain | `xong? ` | `xong? ` | `xong? ` | `xong? ` | **PASS** |
| 42 | B-plain+~ | B-symbol-after-plain | `xong~ ` | `xong~ ` | `xong~ ` | `xong~ ` | **PASS** |
| 43 | C-report-thoi | C-report | `thôi! ` | `thôi! ` | `thôi! ` | `thôi! ` | **PASS** |
| 44 | C-report-emoi | C-report | `em ôi! ` | `em ôi! ` | `em ôi! ` | `em ôi! ` | **PASS** |
| 45 | C-report-khong | C-report | `không? ` | `không? ` | `không? ` | `không? ` | **PASS** |
| 46 | C-report-dung | C-report | `đúng! ` | `đúng! ` | `đúng! ` | `đúng! ` | **PASS** |
| 47 | C-report-ban | C-report | `chào bạn! ` | `chào bạn! ` | `chào bạn! ` | `chào bạn! ` | **PASS** |
| 48 | C-report-dep | C-report | `em đẹp lam, đúng không? ` | `em đẹp lam, đúng không? ` | `em đẹp lam, đúng không? ` | `em đẹp lam, đúng không? ` | **PASS** |
| 49 | C-report-sao | C-report | `sao vày? toi không hieu` | `sao vày? toi không hieu` | `sao vày? toi không hieu` | `sao vày? toi không hieu` | **PASS** |
| 50 | C-report-thanks | C-report | `cam on bạn nhiều! ` | `cam on bạn nhiều! ` | `cam on bạn nhiều! ` | `cam on bạn nhiều! ` | **PASS** |
| 51 | C-report-chat | C-report | `di an trua di! nhanh len` | `di an trua di! nhanh len` | `di an trua di! nhanh len` | `di an trua di! nhanh len` | **PASS** |
| 52 | C-glue-word | C-glue | `thôi!di` | `thôi!di` | `thôi!di` | `thôi!di` | **PASS** |
| 53 | C-glue-word2 | C-glue | `sao vày?toi` | `sao vày?toi` | `sao vày?toi` | `sao vày?toi` | **PASS** |
| 54 | C-glue-word3 | C-glue | `oK!xong` | `oK!xong` | `oK!xong` | `oK!xong` | **PASS** |
| 55 | C-glue-compound | C-glue | `đẹp!đẹp! ` | `đẹp!đẹp! ` | `đẹp!đẹp! ` | `đẹp!đẹp! ` | **PASS** |
| 56 | C-double-bang | C-double | `thôi!! ` | `thôi!! ` | `thôi!! ` | `thôi!! ` | **PASS** |
| 57 | C-double-quest | C-double | `không?? ` | `không?? ` | `không?? ` | `không?? ` | **PASS** |
| 58 | C-double-mixed | C-double | `thôi!? ` | `thôi!? ` | `thôi!? ` | `thôi!? ` | **PASS** |
| 59 | C-double-plain | C-double | `xong!! ` | `xong!! ` | `xong!! ` | `xong!! ` | **PASS** |
| 60 | C-quote | C-punct | `nói "chào bạn" nhe` | `nói "chào bạn" nhe` | `nói "chào bạn" nhe` | `nói "chào bạn" nhe` | **PASS** |
| 61 | C-paren | C-punct | `(ghi chu) o day` | `(ghi chu) o day` | `(ghi chu) o day` | `ghi chu o day` | **205-QUIRK** |
| 62 | C-smiley | C-punct | `hehe :) ` | `hehe :) ` | `hehe :) ` | `hehe :) ` | **PASS** |
| 63 | C-smiley2 | C-punct | `sao the? :( ` | `sao the? :( ` | `sao the? :( ` | `sao the? : ` | **205-QUIRK** |
| 64 | C-symbol-start | C-punct | `!important` | `!important` | `!important` | `!important` | **PASS** |
| 65 | C-symbol-start2 | C-punct | `?sao lai vay` | `?sao lai vay` | `?sao lai vay` | `?sao lai vay` | **PASS** |
| 66 | C-percent | C-num | `giam gia 50% nha` | `giam gia 50% nha` | `giam gia 50% nha` | `giam gia 50% nha` | **PASS** |
| 67 | C-time | C-num | `hen gap luc 9:30 sang nhe` | `hen gap luc 9:30 sang nhe` | `hen gap luc 9:30 sang nhe` | `hen gap luc 9:30 sang nhe` | **PASS** |
| 68 | C-email | C-num | `gui email den abc@gmail.com nha` | `gui email den abc@gmail.com nha` | `gui email den abc@gmail.com nha` | `gui email den abc@gmail.com nha` | **PASS** |
| 69 | C-url | C-num | `xem trang ww.facebook.com/nam` | `xem trang ww.facebook.com/nam` | `xem trang ww.facebook.com/nam` | `xem trang ww.facebook.com/nam` | **PASS** |
| 70 | C-decimal | C-num | `can nang 5,5kg thoi` | `can nang 5,5kg thoi` | `can nang 5,5kg thoi` | `can nang 5,5kg thoi` | **PASS** |
| 71 | C-date | C-num | `sinh nhat 20/10/2026 nha` | `sinh nhat 20/10/2026 nha` | `sinh nhat 20/10/2026 nha` | `sinh nhat 20/10/2026 nha` | **PASS** |
| 72 | C-bs-symbol | C-bs | `thôi` | `thôi` | `thôi` | `thôi` | **PASS** |
| 73 | C-bs-retype-sym | C-bs | `thôi!` | `thôi!` | `thôi!` | `thôi!` | **PASS** |
| 74 | C-bs-retype-word | C-bs | `thôi thôi! ` | `thôi thôi! ` | `thôi thôi! ` | `thôi thôi! ` | **PASS** |
| 75 | C-caps-symbol | C-caps | `HELLO! ` | `HELLO! ` | `HELLO! ` | `HELLO! ` | **PASS** |
| 76 | C-caps-ok | C-caps | `OK! ` | `OK! ` | `OK! ` | `OK! ` | **PASS** |
| 77 | C-tone-symbol | C-dirty | `tôi không muon! ` | `tôi không muon! ` | `tôi không muon! ` | `tôi không muon! ` | **PASS** |
| 78 | C-tone-symbol2 | C-dirty | `đúng rồi? ` | `đúng rồi? ` | `đúng rồi? ` | `đúng rồi? ` | **PASS** |
| 79 | C-wrongword-sym | C-dirty | `speling! sai chinh ta roi` | `speling! sai chinh ta roi` | `speling! sai chinh ta roi` | `speling! sai chinh ta roi` | **PASS** |
| 80 | C-eng-symbol | C-dirty | `WELLdone! ` | `WELLdone! ` | `WELLdone! ` | `WELLdone! ` | **PASS** |

## Totals

- **PASS: 77 / 80**
- **ENGINE-DEFECT: 0** — NextGen loses the composed word and/or the symbol
- 205-QUIRK: 3 — legacy 2.0.5 loses data, engine correct
- BOTH-DIFF: 0
- engine == oracle: 80 / 80 (the oracle mirrors the engine)

## Category breakdown

| category | pass |
|---|---|
| A-symbol-after-composed | 20/21 |
| B-symbol-after-plain | 21/21 |
| C-report | 9/9 |
| C-glue | 4/4 |
| C-double | 4/4 |
| C-punct | 4/6 |
| C-num | 6/6 |
| C-bs | 3/3 |
| C-caps | 2/2 |
| C-dirty | 4/4 |

## The user-reported data loss — FOUND, ROOT-CAUSED, FIXED

**Symptom (matched the real-world report):** typing a shifted-symbol character with NO space
right after a composed Vietnamese word, then pressing Space, deleted the symbol AND
reverted the composed word to its raw keystrokes:

    keys:  t h o o i ! <space>      (intent: "thôi! ")
    NextGen engine (before fix)  ->  "thooi"   (composed word reverted, symbol deleted)
    oracle (before fix)          ->  "thooi"   (mirrored the engine)
    OpenKey 2.0.5                ->  "thôi! "  (correct)
    NextGen engine (after fix)   ->  "thôi! "  (correct)

**Scope (before fix):** all 21 shifted-symbol characters (`! @ # $ % ^ & * ( ) _ + { } | : \" < > ? ~`)
typed after a word that actually composed (has tone marks), when a Space follows — plus
variants: double symbols, glued composed-word-symbol-composed-word, quotes, and a
backspace+space+retype sequence. Symbols after a plain word (no tones), at word start, or
with more text but no space were always fine (categories B, C-glue, C-punct, C-num).

**Root cause (TextEngine):** `TextEngine::isWordBreakChar` only listed the legacy
`_breakCode` printable subset (`, . / ; ' \\ - = \``). The shifted-symbol characters were
NOT in that set, and the legacy shifted-digit check (`isNumberKey(c) && chCaps`) can never
fire for them because the app delivers the RESOLVED character (`'!'`) rather than the raw
digit (`'1'`). So `!`, `?`, … were routed to `mainKeyBranch()` and inserted into the
composition buffer as if they were word letters. The following Space then ran
`checkSpelling`, found the polluted word invalid, set `tempDisableKey_`, and
`checkRestoreIfWrongSpelling` issued a Restore that popped the whole buffer (symbol included)
and re-issued the raw keystrokes — destroying the user's composed text and symbol.

**Fix (applied):** `TextEngine::isWordBreakChar` now classifies the 21 shifted-symbol
characters as word breaks, matching the 2.0.5 hook which delivers the raw key with the shift
bit. The clean-room oracle (`vi_oracle.hpp`) was updated in lockstep. After the fix this suite
reports **0 ENGINE-DEFECT** — it is the permanent regression suite for this bug.

### 2.0.5 quirks captured (engine is correct here)

- `(ghi chu) o day` — legacy 2.0.5 drops the parentheses entirely; the engine keeps them.
- `sao the? :(` — legacy 2.0.5 drops the closing `(`; the engine keeps it.
- `thôi{ ` — legacy 2.0.5 emits `thooi[` for `{` (its shift+[ is the Telex `ơ` key); the
  engine keeps the literal `{`.

## Verdict

**PASS** — engine == intended and 2.0.5 == intended on every case where the legacy engine is
correct: **77/80 PASS**, 0 ENGINE-DEFECT (was 35 before the fix), 3 205-QUIRK,
0 BOTH-DIFF. The realistic harness now matches the gold legacy engine on the entire
dirty corpus while keeping every symbol on screen.
