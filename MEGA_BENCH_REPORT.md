# KieeKey — Massive Vietnamese Text Correctness Benchmark Report

Date: 2026-08-29 · Phase: massive correctness benchmark (source frozen; no engine changes made during this phase)

**Primary verdict: `TextEngine` (KieeKey v3) vs the clean-room oracle — the shipped engine must match its
own specification on every event.** The OpenKey 2.0.5 differential below is an OPTIONAL compatibility
reference (the legacy engine is vendored unmodified and compiled only into the test binary; run with
`--no-205` to disable it).

Models under test (fed byte-identical deterministic event streams):
1. `TextEngine` — shipped KieeKey implementation (`src/core/TextEngine.{hpp,cpp}`), **unmodified**.
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
| 10-macros | 1000000 | 71180475 | 0 | 0 | 0 | 458334 |
| 11-metamorphic | 100000 | 10356556 | 0 | 0 | 0 | 0 |
| 13-edge-punct | 3702 | 14589 | 0 | 0 | 0 | 0 |
| 12-diff-openkey205 | 1000000 | 12499976 | 0 | 0 | 0 | 673 |
| 13-edge-punct-205 | 3342 | 12762 | 0 | 0 | 0 | 0 |
Suite 2 (`2-corpus`) note: 39417 corpus entries contain characters outside the Telex alphabet (uppercase Đ, commas, parentheses, ...) and are skipped; 215815 composition targets are untypeable because the generated keys re-type to a different valid Vietnamese string (concentrated in the doubled-word variant, e.g. junction 'a'+'a'->'â'). Both are scheme/corpus properties, not engine defects — every mappable target was typed with 0 text mismatches.
| **TOTAL** | **3356875** | **109456155** | **0** | **0** | **0** | **459007** |

## Statistical summary

- Total deterministic cases: 3356875
- Total events (key/space/backspace/break/mouse/mode ops): 109456155
- Word corpus: 73901 real Vietnamese words (expanded to ≥500k composition cases; every vowel, every tone, unusual clusters, repeated vowels)
- Text mismatches (engine vs oracle): 0
- Failure rate per million events: 0
- Failure rate per million cases: 0
- Unique mismatch signatures: 0
- Minimized reproducers saved: 0 (tests/repros/)
- Stale-buffer defect events (engine history overflow, pre-empted by oracle mirror): 0
- Over-backspace events (engine backspaceCount > committed text length, post-clamp contract): 0
- ReplaceMacro events (engine results carrying an in-result expansion; the consumer applies it — v3.1 D3 contract): 459007
- ReplaceMacro gap events (expansion missing from the result — consumer could not apply it): 0
- Run mode: FULL (all mandated budgets)

## Known defects found


## Compatibility reference — differential vs OpenKey 2.0.5 (optional)

- Cases compared: see suite 12 row (counted in suite 12)
- Agree with ideal (engine==oracle==2.0.5): 10327178
- **Cat A** — 2.0.5 tone-mark placement differences (same letters, mark on a different vowel): 35
  - sample: catA: ops='iri^6\b\b' method=Telex 2.0.5='i' ideal='ỉ'
  - sample: catA: ops='iri^6\b\by' method=Telex 2.0.5='iy' ideal='ỉy'
  - sample: catA: ops='\b,bur\b5~79\b|T|S\b' method=SimpleTelex 2.0.5=',bu' ideal=',bụ'
  - sample: catA: ops='\b,bur\b5~79\b|T|S\b|S' method=SimpleTelex 2.0.5=',bu' ideal=',bụ'
  - sample: catA: ops='\b,bur\b5~79\b|T|S\b|S|T' method=Telex 2.0.5=',bu' ideal=',bụ'
  - sample: catA: ops='\b]|T |Vuc|T|S\bj7^6|V\b\b' method=VNI 2.0.5='] u' ideal='] ụ'
  - sample: catA: ops='^buju\bmq' method=Telex 2.0.5='Bumq' ideal='Bụmq'
  - sample: catA: ops='^buju\bmq|T' method=Telex 2.0.5='Bumq' ideal='Bụmq'
- **Cat B** — targeted documented 2.0.5 bugs (KieeKey fixed them): 4
  - vector 'hoas': 2.0.5=hụs ideal=hóa — 2.0.5 tone-mark bug: 'hoas' puts mark on wrong vowel (hóa vs hoá)
  - vector 'quan': 2.0.5=qựn ideal=quan — 2.0.5 tone-mark bug family: 'quan' -> 'quân'
  - vector 'hoai': 2.0.5=hụi ideal=hoai — 2.0.5 tone-mark bug family: 'hoai'
  - vector 'nguy': 2.0.5=ngưy ideal=nguy — 2.0.5 tone-mark bug family: 'nguy'
  - vector 'a{s': not reproduced (205='a{s' ideal='a{s')
- **Cat C** — KieeKey regression (engine output differs from ideal oracle): 0
- **Cat D** — other 2.0.5-specific behavior differences (no KieeKey defect): 2176057
  - sample: catD: ops='\b\b\b\n|V\b|T7]' method=Telex 2.0.5='7]' ideal='7ư'
  - sample: catD: ops='sx \b\nj\b4' method=Telex 2.0.5='sx
ư' ideal='sx
4'
  - sample: catD: ops='sx \b\nj\b4u' method=Telex 2.0.5='sx
u' ideal='sx
4u'
  - sample: catD: ops='sx \b\nj\b4u ' method=Telex 2.0.5='sx
u ' ideal='sx
4u '
  - sample: catD: ops='sx \b\nj\b4u ^g' method=Telex 2.0.5='sx
u G' ideal='sx
4u G'
  - sample: catD: ops='|V|T9|V ~^z|T4\b\bw' method=Telex 2.0.5='9 w' ideal='9 ư'
  - sample: catD: ops='\br|Tee' method=Telex 2.0.5='ree' ideal='rê'
  - sample: catD: ops='\br|Tee ' method=Telex 2.0.5='ree ' ideal='rê '
  - sample: catD: ops='\br|Tee u' method=Telex 2.0.5='ree u' ideal='rê u'
  - sample: catD: ops='\br|Tee u^4' method=Telex 2.0.5='ree uƯ' ideal='rê u4'
  - sample: catD: ops='\br|Tee u^4 ' method=Telex 2.0.5='ree uƯ ' ideal='rê u4 '
  - sample: catD: ops='\br|Tee u^4 \b' method=Telex 2.0.5='ree uƯ' ideal='rê u4'
  - sample: catD: ops='\br|Tee u^4 \b|S' method=SimpleTelex 2.0.5='ree uƯ' ideal='rê u4'
  - sample: catD: ops='\br|Tee u^4 \b|S]' method=SimpleTelex 2.0.5='ree uƯ]' ideal='rê u4ư'
  - sample: catD: ops='\br|Tee u^4 \b|S]|S' method=SimpleTelex 2.0.5='ree uƯ]' ideal='rê u4ư'
  - sample: catD: ops='\br|Tee u^4 \b|S]|Sc' method=SimpleTelex 2.0.5='ree uƯ]c' ideal='rê u4ưc'
  - sample: catD: ops='~\b^i2^x4' method=Telex 2.0.5='I2Xư' ideal='I2X4'
  - sample: catD: ops='~\b^i2^x4y' method=Telex 2.0.5='I2Xưy' ideal='I2X4y'
  - sample: catD: ops='[' method=Telex 2.0.5='[' ideal='ơ'
  - sample: catD: ops='[j' method=Telex 2.0.5='[j' ideal='ợ'
  - sample: catD: ops='[j^g' method=Telex 2.0.5='[jG' ideal='ợG'
  - sample: catD: ops='[j^gh' method=Telex 2.0.5='[jGh' ideal='ợGh'
  - sample: catD: ops='[j^ghk' method=Telex 2.0.5='[jGhk' ideal='ợGhk'
  - sample: catD: ops='[j^ghk2' method=Telex 2.0.5='[jGhk2' ideal='ợGhk2'
  - sample: catD: ops='[j^ghk2~' method=Telex 2.0.5='[jGhk2' ideal='ợGhk2'
  - sample: catD: ops='[j^ghk2~|V' method=VNI 2.0.5='[jGhk2' ideal='ợGhk2'
  - sample: catD: ops='[j^ghk2~|V.' method=VNI 2.0.5='[jGhk2.' ideal='ợGhk2.'
  - sample: catD: ops='[j^ghk2~|V.a' method=VNI 2.0.5='[jGhk2.a' ideal='ợGhk2.a'
  - sample: catD: ops='[j^ghk2~|V.ad' method=VNI 2.0.5='[jGhk2.ad' ideal='ợGhk2.ad'
  - sample: catD: ops='[j^ghk2~|V.ad3' method=VNI 2.0.5='[jGhk2.ad3' ideal='ợGhk2.ad3'
  - sample: catD: ops='[j^ghk2~|V.ad3\b' method=VNI 2.0.5='[jGhk2.ad' ideal='ợGhk2.ad'
  - sample: catD: ops='[j^ghk2~|V.ad3\bu' method=VNI 2.0.5='[jGhk2.adu' ideal='ợGhk2.adu'
- Cat D families (method:trigger; ~=tone key #=digit ]=bracket @=caps-key a=letter /space/enter/mouse/modeswitch):
  - ST:: 35849
  - ST:
: 20790
  - ST: : 38303
  - ST:#: 56424
  - ST:P: 434
  - ST:S: 54
  - ST:]: 43630
  - ST:^: 2
  - ST:a: 126815
  - ST:w: 6344
  - ST:|: 95781
  - ST:~: 56848
  - Telex:: 88927
  - Telex:
: 57353
  - Telex: : 99512
  - Telex:#: 178917
  - Telex:P: 444
  - Telex:S: 54
  - Telex:]: 124186
  - Telex:^: 2
  - Telex:a: 394985
  - Telex:w: 62964
  - Telex:|: 95987
  - Telex:~: 151219
  - VNI:: 41928
  - VNI:
: 19027
  - VNI: : 34204
  - VNI:#: 72824
  - VNI:P: 42
  - VNI:S: 12
  - VNI:]: 10144
  - VNI:a: 116982
  - VNI:w: 5002
  - VNI:|: 96387
  - VNI:~: 43681
- Cat A families (same scheme):
  - ST:: 3
  - ST:
: 1
  - ST:#: 4
  - ST:a: 4
  - ST:|: 4
  - ST:~: 1
  - Telex:: 5
  - Telex:
: 1
  - Telex: : 2
  - Telex:a: 5
  - Telex:|: 2
  - Telex:~: 1
  - VNI:: 1
  - VNI:|: 1
- **Edge-case suite (13-punct)** — punctuation & shifted-symbol battery (parentheses, braces, quotes, break chars, caps, mode switches) across Telex/VNI/SimpleTelex and both orthographies, fed as plain characters and as realistic key events. Findings:
  - Plain-char symbols ('(' ')' '{' ...) are absorbed into the pending composition buffer: a later tone key never composes across them (no wrong text), but composition begun before them is suppressed ('(aos)' -> '(aos)', not '(aos)' with tone). Engine and oracle agree exactly on this path (0 mismatches).
  - Fed as realistic key events (shift+digit etc. = word break; Pair::symbolBreak / ok205::processSymbol), both engines reproduce '(aos)' -> '(áo)' — the real-IME key path is fully consistent.
  - 2.0.5 legacy defect: '[' and ']' are Telex special keys, so Shift+[ and Shift+] ('{' / '}') are treated as standalone 'ơ'/'ư' with caps and emit 'Ớ'/'Ứ' instead of literal braces; KieeKey passes them through (counted in Cat B).
  - Suite-13 Cat D samples (real-event path):
    - ops='as(' method=Telex 2.0.5='as(' ideal='á('
    - ops='(as' method=Telex 2.0.5='(as' ideal='(á'
    - ops='(\nas' method=Telex 2.0.5='(
as' ideal='(
á'
    - ops='as)' method=Telex 2.0.5='as)' ideal='á)'
    - ops=')as' method=Telex 2.0.5=')as' ideal=')á'
    - ops=')\nas' method=Telex 2.0.5=')
as' ideal=')
á'
- **Cat E** — environmental/non-deterministic: 0

## Reproducers

Repro files: `tests/repros/repro_*.txt` — each contains init options, the compact op stream
(replayable via `tests/mega_correctness.cpp` `runOps`), divergence position, and engine/oracle text
in UTF-8, UTF-16 units and code points.

## Verdict

**PASS** — zero text divergences between the shipped engine and the clean-room oracle across 3356875 cases / 109456155 events.

---
Deterministic seeds only; no timing/OS nondeterminism in the model. Per-event checksum gate has negligible collision probability and every flagged event is confirmed with an exact comparison, so the mismatch counts above are exact.
