> **Lineage note (KieeKey v1.0 packaging):** Historical engineering
> document from the refactor lineage that produced KieeKey v1.0 (a fork of
> OpenKey, GPL-3.0). Written before the v1.0 release unification, it uses the
> pre-release working name "OpenKey NextGen" and internal milestone numbers
> (v3.0-v3.4). Published verbatim for traceability - see
> [docs/reports/README.md](README.md).

# OpenKey NextGen — Massive Vietnamese Text Correctness Benchmark Report

Date: 2026-08-29 · Phase: massive correctness benchmark (source frozen; no engine changes made during this phase)

**Primary verdict: `TextEngine` (NextGen v3) vs the clean-room oracle — the shipped engine must match its
own specification on every event.** The OpenKey 2.0.5 differential below is an OPTIONAL compatibility
reference (the legacy engine is vendored unmodified and compiled only into the test binary; run with
`--no-205` to disable it).

Models under test (fed byte-identical deterministic event streams):
1. `TextEngine` — shipped NextGen implementation (`src/core/TextEngine.{hpp,cpp}`), **unmodified**.
2. `vi_oracle.hpp` — clean-room reference oracle (independent state machine; never calls TextEngine).
3. `tests/reference/openkey-2.0.5` — the REAL OpenKey 2.0.5 engine, unmodified upstream sources, compiled via its Linux platform path.

Comparison: exact UTF-16 equality after **every** event; per-event gate is a 4-invariant O(1) checksum
(length/sum/sum-of-squares/xor); any difference is confirmed by a full exact UTF-16 comparison and the
first-diverging position is reported with UTF-16 units, code points and UTF-8.

## Per-suite results

| Suite | Cases | Events | Text mismatches | Stale-buffer defects | Over-backspace | ReplaceMacro events |
|---|---|---|---|---|---|---|
| 1-exhaustive-compose | 430591 | 2498202 | 0 | 0 | 0 | 0 |
| 2-corpus | 819240 | 12893595 | 0 | 0 | 0 | 0 |
| 3-backspace | 1100000 | 12399937 | 0 | 0 | 0 | 29 |
| 4-mode-switch | 1000000 | 9500000 | 0 | 0 | 0 | 527 |
| 5-modifiers | 500000 | 6999980 | 0 | 0 | 0 | 0 |
| 6-fuzz | 10000 | 10000000 | 0 | 0 | 0 | 437 |
| 7-long-session | 10 | 100000000 | 0 | 0 | 0 | 1445 |
| 8-sentences | 1 | 100708469 | 0 | 0 | 0 | 2318 |
| 9-rapid | 1 | 10000004 | 0 | 0 | 0 | 307 |
| 10-macros | 1000000 | 71180475 | 0 | 0 | 0 | 458334 |
| 11-metamorphic | 100000 | 10356556 | 0 | 0 | 0 | 0 |
| 13-edge-punct | 3702 | 14589 | 0 | 0 | 0 | 0 |
| 12-diff-openkey205 | 1000000 | 12499976 | 0 | 0 | 0 | 673 |
| 13-edge-punct-205 | 3342 | 12762 | 0 | 0 | 0 | 0 |
Suite 2 (`2-corpus`) note: 39417 corpus entries contain characters outside the Telex alphabet (uppercase Đ, commas, parentheses, ...) and are skipped; 215815 composition targets are untypeable because the generated keys re-type to a different valid Vietnamese string (concentrated in the doubled-word variant, e.g. junction 'a'+'a'->'â'). Both are scheme/corpus properties, not engine defects — every mappable target was typed with 0 text mismatches.
| **TOTAL** | **5966887** | **359064545** | **0** | **0** | **0** | **464070** |

## Statistical summary

- Total deterministic cases: 5966887
- Total events (key/space/backspace/break/mouse/mode ops): 359064545
- Word corpus: 73901 real Vietnamese words (expanded to ≥500k composition cases; every vowel, every tone, unusual clusters, repeated vowels)
- Text mismatches (engine vs oracle): 0
- Failure rate per million events: 0
- Failure rate per million cases: 0
- Unique mismatch signatures: 0
- Minimized reproducers saved: 0 (tests/repros/)
- Stale-buffer defect events (engine history overflow, pre-empted by oracle mirror): 0
- Over-backspace events (engine backspaceCount > committed text length, post-clamp contract): 0
- ReplaceMacro events (engine results carrying an in-result expansion; the consumer applies it — v3.1 D3 contract): 464070
- ReplaceMacro gap events (expansion missing from the result — consumer could not apply it): 0
- Run mode: FULL (all mandated budgets)

## Known defects found


## Compatibility reference — differential vs OpenKey 2.0.5 (optional)

- Cases compared: see suite 12 row (counted in suite 12)
- Agree with ideal (engine==oracle==2.0.5): 12075271
- **Cat A** — 2.0.5 tone-mark placement differences (same letters, mark on a different vowel): 675
  - sample: catA: ops='\n\n^-wjy|Vuo\b' method=VNI 2.0.5='

-ưỵu' ideal='

-ựyu'
  - sample: catA: ops='\n\n^-wjy|Vuo\b5' method=VNI 2.0.5='

-ưyu5' ideal='

-ựyu5'
  - sample: catA: ops='\n\n^-wjy|Vuo\b5\b' method=VNI 2.0.5='

-ưyu' ideal='

-ựyu'
  - sample: catA: ops='\n\n^-wjy|Vuo\b5\b]' method=VNI 2.0.5='

-ưyu]' ideal='

-ựyu]'
  - sample: catA: ops='1uomx\b' method=Telex 2.0.5='1ũo' ideal='1uõ'
  - sample: catA: ops='1uomx\b\b' method=Telex 2.0.5='1ũ' ideal='1u'
  - sample: catA: ops='1uomx\b\by' method=Telex 2.0.5='1ũy' ideal='1uy'
  - sample: catA: ops='1uomx\b\by|S' method=SimpleTelex 2.0.5='1ũy' ideal='1uy'
- **Cat B** — targeted documented 2.0.5 bugs (NextGen fixed them): 1
  - vector 'hoas': not reproduced (205='hóa' ideal='hóa')
  - vector 'quan': not reproduced (205='quan' ideal='quan')
  - vector 'hoai': not reproduced (205='hoai' ideal='hoai')
  - vector 'nguy': not reproduced (205='nguy' ideal='nguy')
  - vector 'a{s': 2.0.5=aỚ ideal=a{s — 2.0.5 brace bug: '{' (Shift+[) is treated as standalone ơ with caps -> 'Ớ' instead of a literal brace; NextGen passes it through
- **Cat C** — NextGen regression (engine output differs from ideal oracle): 0
- **Cat D** — other 2.0.5-specific behavior differences (no NextGen defect): 427324
  - sample: catD: ops='\br|Tee u^4 \b|S]' method=SimpleTelex 2.0.5='rê u4]' ideal='rê u4ư'
  - sample: catD: ops='\br|Tee u^4 \b|S]|S' method=SimpleTelex 2.0.5='rê u4]' ideal='rê u4ư'
  - sample: catD: ops='\br|Tee u^4 \b|S]|Sc' method=SimpleTelex 2.0.5='rê u4]c' ideal='rê u4ưc'
  - sample: catD: ops=' 7\b|Twy\n' method=Telex 2.0.5=' wy' ideal=' ưy
'
  - sample: catD: ops=' 7\b|Twy\ns' method=Telex 2.0.5=' wys' ideal=' ưy
s'
  - sample: catD: ops=' 7\b|Twy\ns^z' method=Telex 2.0.5=' wysZ' ideal=' ưy
sZ'
  - sample: catD: ops='1m|Sj\bij1n|Ve\b|Vy4 ' method=VNI 2.0.5='1mij1ny4' ideal='1mij1ny4 '
  - sample: catD: ops='1m|Sj\bij1n|Ve\b|Vy4 a' method=VNI 2.0.5='1mij1ny4a' ideal='1mij1ny4 a'
  - sample: catD: ops='1m|Sj\bij1n|Ve\b|Vy4 af' method=VNI 2.0.5='1mij1ny4af' ideal='1mij1ny4 af'
  - sample: catD: ops='1m|Sj\bij1n|Ve\b|Vy4 af~' method=VNI 2.0.5='1mij1ny4af' ideal='1mij1ny4 af'
  - sample: catD: ops='ayex|S ' method=SimpleTelex 2.0.5='ayex' ideal='ayex '
  - sample: catD: ops='ayex|S z' method=SimpleTelex 2.0.5='ayexz' ideal='ayex z'
  - sample: catD: ops='|S^]' method=SimpleTelex 2.0.5=']' ideal='Ư'
  - sample: catD: ops='|S^] ' method=SimpleTelex 2.0.5='] ' ideal='Ư '
  - sample: catD: ops='|S^] \b' method=SimpleTelex 2.0.5=']' ideal='Ư'
  - sample: catD: ops='|S^] \b\n' method=SimpleTelex 2.0.5=']
' ideal='Ư
'
  - sample: catD: ops='|S^] \b\n ' method=SimpleTelex 2.0.5=']
 ' ideal='Ư
 '
  - sample: catD: ops='|S[' method=SimpleTelex 2.0.5='[' ideal='ơ'
  - sample: catD: ops='|S[r' method=SimpleTelex 2.0.5='[r' ideal='ơr'
  - sample: catD: ops='|S[ro' method=SimpleTelex 2.0.5='[ro' ideal='ơro'
  - sample: catD: ops='|S[ro\n' method=SimpleTelex 2.0.5='[ro
' ideal='ơro
'
  - sample: catD: ops='|S[ro\ni' method=SimpleTelex 2.0.5='[ro
i' ideal='ơro
i'
  - sample: catD: ops='|S[ro\nit' method=SimpleTelex 2.0.5='[ro
it' ideal='ơro
it'
  - sample: catD: ops='|S[ro\nit ' method=SimpleTelex 2.0.5='[ro
it ' ideal='ơro
it '
  - sample: catD: ops='|S[ro\nit \n' method=SimpleTelex 2.0.5='[ro
it 
' ideal='ơro
it 
'
  - sample: catD: ops='|S[ro\nit \n\n' method=SimpleTelex 2.0.5='[ro
it 

' ideal='ơro
it 

'
  - sample: catD: ops='|S[ro\nit \n\n\n' method=SimpleTelex 2.0.5='[ro
it 


' ideal='ơro
it 


'
  - sample: catD: ops='|S[ro\nit \n\n\n4' method=SimpleTelex 2.0.5='[ro
it 


4' ideal='ơro
it 


4'
  - sample: catD: ops='|S[ro\nit \n\n\n4n' method=SimpleTelex 2.0.5='[ro
it 


4n' ideal='ơro
it 


4n'
  - sample: catD: ops='|S[ro\nit \n\n\n4n ' method=SimpleTelex 2.0.5='[ro
it 


4n ' ideal='ơro
it 


4n '
  - sample: catD: ops='|S[ro\nit \n\n\n4n ^4' method=SimpleTelex 2.0.5='[ro
it 


4n 4' ideal='ơro
it 


4n 4'
  - sample: catD: ops='|S[ro\nit \n\n\n4n ^4l' method=SimpleTelex 2.0.5='[ro
it 


4n 4l' ideal='ơro
it 


4n 4l'
- Cat D families (method:trigger; ~=tone key #=digit ]=bracket @=caps-key a=letter /space/enter/mouse/modeswitch):
  - ST:: 13130
  - ST:
: 10175
  - ST: : 20579
  - ST:#: 22175
  - ST:P: 74
  - ST:S: 2
  - ST:]: 40555
  - ST:a: 51087
  - ST:w: 2196
  - ST:|: 17867
  - ST:~: 19285
  - Telex:: 12793
  - Telex:
: 14326
  - Telex: : 28758
  - Telex:#: 17238
  - Telex:P: 26
  - Telex:S: 16
  - Telex:]: 3532
  - Telex:a: 39543
  - Telex:w: 1762
  - Telex:|: 17950
  - Telex:~: 15152
  - VNI:: 6263
  - VNI:
: 5228
  - VNI: : 10866
  - VNI:#: 8667
  - VNI:]: 1719
  - VNI:a: 20062
  - VNI:w: 898
  - VNI:|: 17885
  - VNI:~: 7515
- Cat A families (same scheme):
  - ST:: 45
  - ST:
: 4
  - ST: : 12
  - ST:#: 12
  - ST:]: 2
  - ST:a: 30
  - ST:w: 2
  - ST:|: 26
  - ST:~: 15
  - Telex:: 112
  - Telex:
: 7
  - Telex: : 17
  - Telex:#: 23
  - Telex:]: 7
  - Telex:a: 90
  - Telex:w: 1
  - Telex:|: 26
  - Telex:~: 31
  - VNI:: 65
  - VNI:
: 2
  - VNI: : 17
  - VNI:#: 25
  - VNI:]: 4
  - VNI:a: 47
  - VNI:w: 2
  - VNI:|: 29
  - VNI:~: 22
- **Edge-case suite (13-punct)** — punctuation & shifted-symbol battery (parentheses, braces, quotes, break chars, caps, mode switches) across Telex/VNI/SimpleTelex and both orthographies, fed as plain characters and as realistic key events. Findings:
  - Plain-char symbols ('(' ')' '{' ...) are absorbed into the pending composition buffer: a later tone key never composes across them (no wrong text), but composition begun before them is suppressed ('(aos)' -> '(aos)', not '(aos)' with tone). Engine and oracle agree exactly on this path (0 mismatches).
  - Fed as realistic key events (shift+digit etc. = word break; Pair::symbolBreak / ok205::processSymbol), both engines reproduce '(aos)' -> '(áo)' — the real-IME key path is fully consistent.
  - 2.0.5 legacy defect: '[' and ']' are Telex special keys, so Shift+[ and Shift+] ('{' / '}') are treated as standalone 'ơ'/'ư' with caps and emit 'Ớ'/'Ứ' instead of literal braces; NextGen passes them through (counted in Cat B).
  - Suite-13 Cat D samples (real-event path):
    - ops='a[ ' method=Telex 2.0.5='a[' ideal='a[ '
    - ops='a] ' method=Telex 2.0.5='a]' ideal='a] '
    - ops='a{s' method=Telex 2.0.5='aỚ' ideal='a{s'
    - ops='as{' method=Telex 2.0.5='aỚ' ideal='á{'
    - ops='{as' method=Telex 2.0.5='Ớa' ideal='{á'
    - ops='{{' method=Telex 2.0.5='[' ideal='{{'
- **Cat E** — environmental/non-deterministic: 0

## Reproducers

Repro files: `tests/repros/repro_*.txt` — each contains init options, the compact op stream
(replayable via `tests/mega_correctness.cpp` `runOps`), divergence position, and engine/oracle text
in UTF-8, UTF-16 units and code points.

## Verdict

**PASS** — zero text divergences between the shipped engine and the clean-room oracle across 5966887 cases / 359064545 events.

---
Deterministic seeds only; no timing/OS nondeterminism in the model. Per-event checksum gate has negligible collision probability and every flagged event is confirmed with an exact comparison, so the mismatch counts above are exact.
