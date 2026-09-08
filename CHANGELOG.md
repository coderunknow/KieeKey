# Changelog

All notable changes to KieeKey are documented here. Format based on
Keep a Changelog; versioning: SemVer.

## [1.3.0-RC1] — 2026-09-08

Release candidate built on the cross-engine measurement campaign in
[`docs/bench/rc1-130/`](docs/bench/rc1-130/) and [`benchmark/`](benchmark/README.md). Two things
ship: an engine hot-path change that is bit-identical in output, and an opt-in low-latency build
profile whose behavioural price is published rather than buried.

### Engine — faster per-key decision, identical output

* **Self-validating composition memo** (`TextEngine::composeCached`) over all 21 emit loops: moving one
  mark used to recompose the whole pending word, so 4–7 unchanged code points were recomposed per key.
  The cache key *is* the raw word slot, so an entry cannot go stale and no write site has to
  invalidate anything; `setOptions()` drops the caches because the code table is the only input not in
  the key. 256 bytes of state, still zero allocation on the hot path.
* **Memoised leading-consonant match** inside `checkSpelling`: `matchLeadingConsonant`'s longest row is
  3 cells (`NGH`), so once `spellingEndIndex_ >= 3` the answer is a pure function of slots 0..2 plus the
  two option masks — which is the key.
* **Table-driven repair scan** in `checkGrammar`: six compares per position over up to 32 slots replaced
  by a 96-byte table holding exactly the code points the chain tested, so the accept/reject set is
  unchanged (this is the worst case of a long word containing none of them, which used to scan the whole
  buffer every key).
* **Measured** (`rc1-v13rel`, 5 sessions × 12 paired rounds, 60 samples, A/A band 1.587 ns): deciding
  cell `as-shipped · telex-end · prose` **72.39 → 71.03 ns/key, +1.56 %** over v1.2.2, best cells
  +3.3…+9.3 %, **0 cells regressing beyond 2× band** → pre-registered **ACCEPT**.
* **Gates, all 8 PASS**: manifest (drift named: 3 sources), attrib-guard, correctness (374 subject rows),
  **digest-identity `9a78c1b4fcc6dad2` == `9a78c1b4fcc6dad2`** (every output bit-identical to v1.2.2),
  diffab (**0 mismatches / 2 146 422 events**, per-key code, backspaces, replacement text and final
  visible text), memory (21 allocs per 2 M keys, RSS 46.2 MiB), **sanitizers (0 findings, 7 runs)**,
  integrity (27 artifacts / 20 590 rows). Native suite all PASS; `scripts/check_version.py` OK.
* **Disclosed regression**: `as-shipped · vni · pathological` reads −6.0 % (that stream rewrites marks
  across the whole word every key, so a memo misses everywhere and pays its bookkeeping). A cap to the
  8 stable head slots was screened twice (`rc1-v13b`, `rc1-v13d`) and made all six pathological cells
  *worse* (−3.5…−9.9 %) by breaking inlining at the 21 sites, so it was reverted and `composeCached` is
  `always_inline`. The cell is a stress case KieeKey already loses (26.7 vs UniKey's 16.7 ns), not a win
  being traded.
* **Cold start moved the wrong way and is published**: wall p50 403.1 → 441.5 ms, first round
  23.86 → 27.71 ns/key (`cold`, unexplained at this size — P6 territory in the plan).

### New: low-latency build profile (opt-in, `KIEEKEY_LOW_LATENCY_PROFILE`)

* Compiles out `checkGrammar`'s post-edit orthography repair — 5.5 ns of a ~68 ns decision on every key
  into a marked word. `build.sh --fast-profile` and `cmake -DKIEEKEY_LOW_LATENCY_PROFILE=ON`; also
  `cmake --build` for the product. `EngineOptions::grammarRepair` (and `freeMark`, which gates the same
  pass) reach the same behaviour at runtime per target.
* Measured (`rc1-v13prof`, same instrument): **faster than UniKey** on the L1 cells that decide —
  `telex-end · prose` 57.77 vs 61.36 ns/key (**−5.84 %**), `telex-mid · prose` −15.78 %, `vni · prose`
  −9.57 %, `telex-end · edit-storm` −8.90 %, `matched-minimal · telex-end · prose` −7.48 % — and ahead
  at **p50 on all nine** `as-shipped` L2 streams (e.g. prose 82.0 vs 89.5, `vni · prose` 74.5 vs 85.0 ns),
  with the memory gate unchanged at 21 allocs / 2 M keys.
* **Price, measured**: 52.0 % of keys (372 242 / 715 474 events) repaint differently and **10 of 18
  streams end with different composed text** — a mark left on the vowel the last key hit instead of the
  one the rule picks. That is wrong Vietnamese, so the profile is never the default of a release whose
  claim is orthographic correctness. It ships as a labelled configuration with this paragraph attached.
* `rc1_gates.py --policy=declared-divergence` / `campaign_rc1.sh --divergence`: for a release that
  trades a rule away on purpose, the gates still assert final visible text identity and this tree's
  in-process/shim identity, and publish the payload divergence instead of turning the check off.
  Not needed by this release's campaign of record — the strict tree is byte-identical output.

### Tooling and measurement rules

* `build.sh --pgo`: instrumented-train → `-fprofile-use` rebuild of the candidate library, failing
  loudly if no `.gcda` was written. Screened (`rc1-p8`): **−4.04 %** — profile-guided codegen makes the
  deciding cell slower here, so it stays available for re-testing and is not used.
* A campaign now snapshots `baseline_manifest.json` into its own results directory and `rc1_stats`
  derives baseline-vs-candidate wording from it (falling back to the campaign header), so a candidate
  trial can no longer be labelled "NOT RUN — baseline campaign".
* **Absolute ns is campaign-internal**: the same binary read 84.70 ns/key in one campaign and 68.73 in
  another hours later (UniKey with it: 73.00 → 60.20; ratio stable 16.1 % → 13.1 %). No cross-campaign
  subtraction anywhere; only paired same-round deltas and ratios are published.
* The release campaign ran at v1.2.2's instrument size (5 × 12, keys 150 000) rather than the plan's
  larger 6 × 24, so its bands are directly comparable with `rc1-130`'s — a bigger, differently-sized
  campaign would have compared instruments, not engines. Documented in PROTOCOL §14.

### Version carriers

`CMakeLists.txt` 1.3.0 · `KieeKeyApp.rc` `1,3,0,0` + `1.3.0.0` · `KieeKeyApp.manifest` `1.3.0.0` ·
`main.cpp` `1.3.0` / `1.3.0 RC1` / title · `kieekey_core.hpp` macros + `OPENKEY_KIEEKEY_VERSION_STRING`
"1.3.0 RC1" · README headline and newest section · `scripts/check_version.py` expect/channel ·
`SHA256SUMS.txt` regenerated.

## [1.2.2-Stable] — 2026-09-07

### Stable release — RC4 promoted unchanged, full re-verification on an independent host

> **Scope:** v1.2.2 Stable ships the qualified RC4 tree as-is. `git diff
> v1.2.2-RC4 v1.2.2` touches **release identity and release evidence only**
> (version carriers, CHANGELOG/README, the Stable report set, benchmark
> artifacts, SHA256SUMS) — zero runtime-semantics changes; the engine hot
> path and every shipped source file are byte-identical to RC4. Full
> report: `docs/reports/V1.2.2_STABLE_RELEASE_REPORT.md`.

### Changed

* Version carriers synchronized to `1.2.2 Stable` (`kieekey_core.hpp`
  VERSION_STRING, app title/version strings, `check_version.py` CHANNEL,
  README headline + What's-new, this entry, the Stable report set, the
  bench index). PE VERSIONINFO / manifest stay `1.2.2.0`; CMake project
  stays `1.2.2` (unchanged-by-policy, gate-verified).
* New evidence tree `docs/bench/stable-122/` — the complete independent
  re-verification campaign (frozen three-way baseline: v1.2.1 Stable /
  v1.2.2 RC4 / v1.2.2 Stable on one host, one compiler, one harness set).

### Verified (Stable campaign, independent host: Debian 12, g++ 12.2, CMake 4.4.3)

* RC4 baseline re-run BEFORE any change: CMake ctest **20/20**, native
  suite **23/23**, correctness gate **137,878 cases / 2,059,419 events /
  0 mismatches / 0 stale / 0 expectDrift**, deep option matrix
  **92,392,535 events / 350 configs / 375 transitions / 0 failures**,
  ASan+UBSan+LSan **20/20 / 0 findings**, TSan **20/20 / 0 warnings**,
  single-core (taskset -c 0) **7/7**, option-matrix determinism digests
  identical ×3 (and equal to the RC4 campaign's digest on a different
  host/compiler — cross-host determinism), seeded-stress clean-vs-ASan
  semantic output byte-identical (documented sanitizer-skip sections only),
  table-generator round-trip byte-identical.
* Strict interleaved three-way benchmarks (same host, flags, SHA-synced
  harness): throughput floor **73.18 ns/key (RC4) vs 79.58 (v1.2.1),
  −8.0 %**, with the 20M-key sink digest **identical across every run and
  both versions** (`2905520108639366859`); E2E throughput 13,557 → 13,570
  keys/s (+0.1 %); E2E hot-p50 paired deltas inside the host's ±7 %
  run-to-run band; peak RSS +0.28 MB (1.2.2's engine tables/batch state —
  consistent, explained). RC4↔Stable: engine byte-identical; deltas within
  noise (see the performance report).
* Windows PE cross-build (Zig/MinGW-w64): full `KieeKeyApp.exe` built for
  **x64 and ARM64** with resources + VERSIONINFO, binary carries
  `1.2.2 Stable`; ARM64EC cross-build NOT AVAILABLE on this host (no
  arm64ec target in Zig, MSVC download blocked) — the MSVC CI
  (windows-2022, x64/ARM64/ARM64EC, ctest on x64) is the authority and
  runs on push/tag.
* Real-Windows runtime (hooks/TSF/tray in a live session): NOT EXECUTED —
  no Windows host in this environment; unchanged, explicitly documented
  limitation inherited from RC4.

## [1.2.2-RC4] — 2026-09-06

### Stable-qualification campaign — correctness hardening, release engineering

> **Scope:** the RC4 stable-qualification campaign over the frozen RC3
> baseline (`dd3c42a`). The engine hot path is byte-identical to RC3; every
> accepted change is a correctness, stability or release-engineering fix
> with re-run evidence. Full report:
> `docs/reports/V1.2.2_RC4_RELEASE_REPORT.md`.

### Fixed

* **P0 — TsfComposer::commitBatch double Release (UAF/double-free).** The
  v1.2.1 use-after-free fix was left half-applied: the caller-reference
  `Release()` still ran BEFORE the `appliedCount()` snapshot, destroying the
  session (`delete this` inside `Release()` with `TF_ES_SYNC`) and then
  double-releasing it on every multi-delta batch commit. Snapshot once,
  release once — the `commitOne` shape.
* **P1 — producer handler exceptions could cross the Win32 hook boundary**
  (UB / process fail-fast): all three LL/WinEvent callbacks now route the
  producer handler through `runProducerHandler()` (catch → count into
  `producerExceptions_` → pass-through + wake consumer). The consumer side
  had isolation since v1.2.0; the producer side was missed.
* **P1 — `resyncModifiersFromOs()` data race.** The UI thread may call it
  (documented, and real: in-app on/off) while the pump thread RMWs the
  toggle-edge bitmap; `toggleKeyDown_` is now an array of relaxed atomics.
* **P2 — noexcept TSF entry points no longer terminate on OOM**
  (`commitOne` / `commitBatch` / `textBeforeCaret` / both `DoEditSession`
  COM entries degrade to a failed session → the documented SendInput
  fallback).
* **P2 — silent degradation surfaced:** foreground WinEvent-hook install
  failures tracked (`fgHookInstalled` / `fgHookFailureCount`); a failed
  ProcessMonitor start raises the new `AutoExcludeUnavailable` balloon
  instead of silently disabling auto-exclusion for the session.
* **P2 — restart hygiene:** `start()` after `stop()` resets the SPSC ring
  (`reset_quiescent`) and producer bitmaps — no stale keystroke replays
  into a new session; `keyboardProc` passes through once `running_` is
  false.
* **P2 — ProcessMonitor PID-reuse revalidation** before publishing the
  foreground snapshot; header claim now describes the real guarantee.

### Release engineering

* Table generators emit the GPL-3 header + version-free banner; forward
  regeneration proven byte-identical; the rotted v1.1.2-era reverse bridge
  (`flat_to_legacy.py`) fails loudly with an actionable message instead of
  a mid-file KeyError.
* CMake presets default to `KIEEKEY_BUILD_UI=none` (dependency-free first
  build; WinUI 3 enablement documented in `bin/README.txt`).
* CI: `gen_sha256sums.sh --check` enforced on the x64 leg; stale metadata
  fixed (CI header, ctest list, dead `dev` trigger, CMake STATUS message,
  cross-build script standard, tool docstrings).
* Dead code removed (`HookWatchdog::latestSeenTick`, `QueueStats::popped`).
* Version carriers synchronized to `1.2.2 RC4` (macros, app strings,
  README, gate CHANNEL); `tests/test_option_matrix.cpp` banner now derives
  from the version macro (was a hard-coded RC3 string the gate could not
  see).
* `.gitignore` keeps `docs/bench/**/*.log` trackable — bench evidence is a
  release artifact.

### Evidence (RC4 campaign re-runs)

* Native suite 20/20 PASS (2 hosts of evidence: clean + sanitizer builds).
* ASan + UBSan + LSan: 20/20 PASS, 0 findings. TSan: 20/20 PASS,
  0 warnings. Single-core (`taskset -c 0`) stress/lifecycle/option: PASS.
* Determinism: option-matrix triple run digests identical; clean-vs-ASan
  semantic output byte-identical (documented sanitizer-skip sections only).
* Correctness gate re-run: 137,878 cases / 2,059,419 events / 0 mismatches.
* Strict RC3↔RC4 A/B (5 interleaved e2e runs, identical harness):
  hot-path p50 +0.15 %, throughput −0.04 %, RSS −0.21 % — within noise and
  inside the 1 % regression policy.
* Windows x64 cross-build (MinGW-w64 GCC 14, static runtime): KieeKeyApp.exe
  + 8 test/bench executables compile warning-clean. Real-Windows runtime
  validation: NOT AVAILABLE on this host (documented limitation).

## [1.2.2-RC3] — 2026-09-06

### Pipeline measurement, WinUI consumer-callback fix, option-torture gate

> **Scope:** the RC3 latency/runtime campaign. The full keyboard→visible-text
> pipeline was measured end-to-end before any optimization was attempted; the
> result is that the shipped Win32 wiring is already sub-µs (p50 0.183 /
> p99 0.980 µs, real-user p99.9 55 µs dominated by OS scheduler deschedules),
> so **no hot-path optimization was adopted** — every candidate (spin-cap
> 100/200/1000 both ways, queue/wake/batching/copy removals, engine changes)
> was A/B-measured and rejected with evidence. See
> `V1.2.2_RC3_PERFORMANCE_REPORT.md` and `V1.2.2_RC3_ENGINEERING_LOG.md`.

#### Fixed — hook (`src/core/ModernKeyHook.cpp`)

* **WinUI 3 front-end received NO events.** When no producer handler is
  registered (the WinUI front-end's consumer-callback-only wiring), the
  hook's default `ProducerDecision{}` meant `wakeConsumer=false`, so every
  key/mouse/foreground event was counted pass-through and the consumer
  callback never ran — the front-end was inert. The no-handler default is
  now `ProducerDecision{false, true}` at all three entry points; the
  producer handler remains an optional optimization for apps that register
  one (the Win32 app is unaffected — it always registers `onHookEvent`).

#### Added — regression gate (`tests/test_option_matrix.cpp`)

* **Tier 6 option torture**: a long-lived engine hammered with seeded
  full-space option flips (every `EngineOptions` field, including
  non-oracle-modeled ones), verified three independent ways per round —
  FRESH (post-`setOptions`+`resetForConfigurationChange` decision-identity
  with a brand-new engine), ORACLE (live lockstep), INVAR (D1/D2/CNT under
  in-flight flips and temp-off toggles) — plus DETERM (two seeded engines
  fold identical digests). Also fixed the matrix JSON writer (truncation).

#### Measurement — `tests/e2e_bench.cpp`

* `--model=production|all`: production mirrors the shipped hook (pass-through
  keys counted, not enqueued); all is the consumer-callback-only wiring.
  p95 added to the output; JSON schema `e2e_bench.v4`.

#### Version

* 1.2.2 RC2 → **1.2.2 RC3** (all version carriers; gate PASS).
* Windows/WinUI builds not executed on this host (no cross-toolchain);
  CI's Windows jobs compile the Win32 target. Real-Windows SendInput/TSF
  costs remain unmeasured (inherited caveat).

## [1.2.2-RC2] — 2026-09-05

### Option-matrix hardening + performance-preference surface lockdown

> **Scope:** correctness follow-through of RC1's performance work. Two
> engine-level coherence fixes, one new public reset API wired into every
> reconfiguration path, and two new always-on test layers. No hot-path
> behaviour changed for any shipped configuration; the throughput floor
> re-verified (see V1.2.2_RC2_PERFORMANCE_REPORT.md).

#### Changed — engine (`src/core/TextEngine.*`)

* **VIQR output encoding precedence.** `OutputEncoding::Viqr` is defined on
  the precomposed-Unicode pipeline: when a LEGACY code table (TCVN3 /
  VNI-Windows / UnicodeCompound / CP1258) is selected, the table rendering
  wins and no mnemonic conversion is layered on top. Pre-RC2 the conversion
  ran over whatever the table resolver produced — accidentally coherent for
  TCVN3/VNI (their bytes miss the map), but UnicodeCompound/CP1258 emitted
  MIXED content (an â became ASCII `a^` while a compound ă+tilde stayed raw
  combining marks — breaking VIQR's pure-ASCII channel contract, the same
  class the v1.2.0 macro-parity fix closed). Decisions are untouched;
  Unicode+VIQR output is byte-identical to before. This is the one
  map completion: `kViqrMap` gained its one missing entry, **U+0168 Ũ →
  "U~"** (lowercase ũ was present; deep option-matrix streams caught the
  raw-Ũ leak into the supposedly pure-ASCII channel). The two sanctioned
  render divergences are the engine's own VIQR feature only (never legacy
  behaviour): `diff_engine_ab` exempts VIQR renderings and reports the
  count (`viqrPrecedenceRenderSkips`); every decision, code table render
  without VIQR, accounting field and scratch size stays compared exactly.
* **`TextEngine::resetForConfigurationChange()`** — full session reset for
  *reconfiguration* (clears the undo ring, pending special chars, space run,
  grammar/spelling scratch and the result record; keeps resolvers and the
  visible-character account). `startNewSession()` deliberately keeps its
  narrower contract: word-break and backspace flows legitimately read those
  fields back AFTER it returns, so they cannot move into it. The RC2
  option-matrix CONTRACT tier caught both repro families this fixes: a hot
  `checkSpelling` OFF->ON and a hot `useDictionaryRestore` ON switch let the
  next composed word inherit pre-switch state. All Win32-dialog and WinUI
  reconfigure paths now call the new reset (the WinUI code-table combo had
  NO reset at all — now it has the right one).

#### Added — tests

* **`tests/test_option_matrix.cpp`** (registered in `run_all_tests.sh` +
  CTest as `ok_option_matrix`): method × code-table × output-encoding
  Cartesian (30 configs, oracle lockstep incl. per-event VIQR projection),
  pairwise-complete covering array over the 11 semantic booleans (grammar,
  macros, dict), runtime-transition matrices (contract / live-switch /
  render / roundtrip / ABA), queue hot-option stress, index/mask/cache
  staleness probe, and the documented-difference goldens. Quick mode: 318
  configs, 3.49 M events, 265 live switches, 0 failures.
* **`tests/test_perf_profiles.cpp`** (`ok_perf_profiles`): pins the whole
  `PerfProfile` strategy model — base table field-by-field, hybrid-flag
  semantics and collisions, the adaptive telemetry ladder with
  off-by-one boundary sweeps and rule ORDER, registry encoding clamps,
  per-field `operator==` completeness (the app's skip-reapply fast path
  depends on it), the one-way `dictionaryRestore` engine mapping, and a
  2 560-point invariant sweep. 10 639 checks.
* `--events` now truly drives **every** tier of `test_option_matrix` (T1-only
  before; the per-tier literals silently overrode it) — CI depth 20k/config,
  deep runs use the flag.
* **`tests/bench_tput_floor.cpp`**: the throughput-floor driver is now
  versioned in-repo (RC1's was ad-hoc) — 20 M-key mixed stream, decisions +
  renders folded into one sink, floor re-measured against it.

#### Fixed — reference oracle (`tests/vi_oracle.hpp`)

* quick-telex now carries the v1.2.0 **syllable-initial guard** (engine:
  `index_ == 1`) — the oracle over-fired `pp→ph`-style expansions at any
  repeated trailing consonant; and quick-END consonant re-emission emits
  the rewritten PAIR (`newCharCount` units), not the whole word, matching
  the engine buffer + every canonical consumer. Both were reference-side
  gaps: the live engine was self-consistent and legacy-exact.

## [1.2.2-RC1] — 2026-09-05

### Performance & Runtime Efficiency

> **Scope:** second-gen engine hot-path only (`src/core/TextEngine.*`).
> v1.2.1 Stable is the frozen baseline, re-measured on this host (g++ 12.2
> −O2, 20 M-key driver) **before** any candidate existed. Decision-identical;
> dual-emitter `emitMtx` unchanged. Full evidence:
> [`docs/reports/V1.2.2_RC1_PERFORMANCE_REPORT.md`](docs/reports/V1.2.2_RC1_PERFORMANCE_REPORT.md),
> trail: [`docs/reports/V1.2.2_RC1_ENGINEERING_LOG.md`](docs/reports/V1.2.2_RC1_ENGINEERING_LOG.md);
> raw artifacts in `docs/bench/rc1-122/` (does not overwrite `docs/bench/rc1/`).

#### Changed — engine (`src/core/TextEngine.*`)

* **`handleMainKey` candidate scans use constexpr last-character sequence
  buckets.** After RC2's `tailMatches` pre-filter the three charset walks
  (`kConsonantD` 79, `kVowelForMark` 79, `kVowel` ≤32) were still #1 in
  gprof (13.55 ns/call). Indexing by the last cell under `seqMask` visits
  only sequences that can match, in original table order. Equivalence:
  membership **is** `tailMatches`; first winner is the same `i` /
  `(group, seq)`; `checkCorrectVowel` call count unchanged (2 022 462 /
  20 M keys).
* **Bitmask classifiers:** `isWordBreakChar` (two 64-bit masks, every Char
  event), `isVowelChar` (A E I O U Y), Telex `isMarkKey` (S F R J X).
  `[` / `]` stay non-breaks (standalone ơ/ư).
* **`checkSpelling` length-1 cheap exit** — QU/GI / vowel-combine /
  end-consonant walks cannot fire on a one-letter word.
* **`vowelCombineFor`** — direct A/E/I/O/U/Y index replaces
  `kVowelCombine.find` on the force-vowel and `canHasEndConsonant` paths.

#### Measured (this host, not the RC3 g++14.2 40.2 ns/key)

* Frozen v1.2.1 Stable 20 M-key median **54.41 ns/key** → RC1 median
  **43.85 ns/key** (**−19.4 %**). Fair interleaved 5-run paired Δ ≈ **−21 %**.
  Output sink **5436924** both sides.
* gprof self ns/call: `handleMainKey` 13.55 → 10.00; `checkSpelling`
  10.94 → 6.02; `process` 4.50 → 3.00.
* Host-scaled exceptional bar (34/40.2 × 54.41 ≈ 46.0 ns/key): cleared.
  Absolute 40.2 → 38 is **not** claimed.

#### Unchanged — on purpose

* Engine semantics, options, profiles, encodings, hook, TSF, lifecycle,
  `emitMtx`. E2E shim / tone / real SendInput **not re-measured** (timer
  floor; engine is ~0.04 µs of ~10 µs p50 here — no fake E2E claim).

#### Validation

* 21/21 native suites PASS; `gate_correctness` 2.06 M events PASS;
  `diff_engine_ab` vs frozen RC1 engine PASS; version gate `1.2.2 RC1`.
* Compile warning-clean `-Wall -Wextra -Werror` at C++17/20/23. MSVC
  windows-2022 CI is the PE gate (not executed on this host).

## [1.2.1-Stable] — 2026-09-05

### Whole-repository hardening: the Windows surface passes a real gate

> **Scope:** 8 bugs found and fixed — every one in the Windows-only surface
> (TSF composer, hook/monitor lifecycles, WinUI 3 front-end, build
> portability), none in the engine core (6-seed × 1.2 M lockstep
> differential vs the frozen RC1 engine: **7.21 M events, 0 mismatches**).
> New permanent gate: the complete Windows app now **compiles AND links**
> as x64 + ARM64 PE under clang `-Wall -Wextra -Werror` (llvm-mingw), which
> RC3 could not do at all. Engine byte-identical → benchmarks **UNCHANGED**
> with byte-identical output sinks (20 M-key driver, median 43.47 ns/key on
> both sides). Full evidence:
> [`docs/reports/V1.2.1_STABLE_RELEASE_REPORT.md`](docs/reports/V1.2.1_STABLE_RELEASE_REPORT.md).

#### Fixed — Windows correctness

* **Use-after-free in the shipped TSF batch-commit path (P0).**
  `TsfComposer::commitBatch` read `appliedCount()` AFTER the final
  `session->Release()` — which, with TF_ES_SYNC, had already dropped the
  last reference and deleted the session object. Benign-looking in retail,
  undefined by the standard, and a wrong count here makes the SendInput
  fallback re-emit the WRONG suffix of a batch (duplicated/missing text).
  The count is now snapshotted before the release.
* **Cross-thread handle reads in hook/monitor start (P2, ×2).**
  `ModernKeyHook::start()` re-checked installation by reading the
  pump-owned `keyboardHook_` handle cross-thread — the exact pattern the
  v1.2.0 comment on `hooksInstalled_` calls undefined and claims to have
  replaced. `start()` now polls the atomic handshake. `ProcessMonitor` had
  the same unfixed pattern on `fgEvent_` and gained the same handshake
  (published by the pump, re-armed in `start()`, cleared on unwind).
* **Build portability (P1).** `ProcessMonitor.cpp` used `std::abs` on
  `LONG` without `<cstdlib>` — MSVC's header chain hides this; a
  conforming toolchain fails to compile the file. Found by the new
  Windows cross-build gate on its first run.
* **Strict-warnings cleanliness (P3).** Dead app-local `kMaxInlineInputs`
  constant deleted; documented version carriers annotated
  `[[maybe_unused]]` — the whole Windows surface is now clean under
  `-Wall -Wextra -Werror`.

#### Fixed — WinUI 3 front-end (UI)

* **Macro expansions were silently dropped.** The consumer returned early
  for `ReplaceMacro`, so "gõ tắt" abbreviations never expanded in this
  front-end (the pre-v3.1 defect class the Win32 app fixed long ago).
  Expansions are now applied per the D3 contract.
* **TSF apartment affinity violated.** The composer attached on the UI
  thread while every commit ran on the hook's consumer thread; STA objects
  must live on one thread (the Win32 app documents exactly this). Attach
  now happens lazily on the consumer thread and detach via the consumer
  finalizer — also fixing a COM-init reference leak on failed hook start.
* **Stale About version.** The About line said "v1.2.0 Stable" through the
  whole 1.2.1 RC cycle. It now derives from the public version macro, and
  `scripts/check_version.py` covers the file so it cannot drift again.

#### Added — release infrastructure

* **Windows PE cross-build gate** (`scripts/build_windows_cross.sh`):
  x64 + ARM64 `KieeKeyApp.exe` — engine, hook, wrapper, monitor, TSF, app,
  resources (VERSIONINFO 1.2.1.0 + manifest) — compile AND link clean with
  clang/llvm-mingw under `-Wall -Wextra -Werror`. Complements the MSVC CI
  (different standard library, different headers → different bug classes);
  does not replace it.
* **Version gate hardened**: stale-carrier scan now covers the WinUI 3
  front-end and the `v1.2.0` pattern; channel set to `Stable`.
* **Sanitizer battery**: ASan+UBSan+LSan on the oracle gate (2.06 M
  events), the RC1 lockstep differential (leak detection), the full
  lifecycle suite, a 500 K-iteration pipeline soak and a 1 M-key stress —
  all clean.
* **Repo tooling bug fixed** (`tools/crossbuild_windows.sh`): an absolute
  `--out` path was double-prefixed with the repo root, crashing the
  resource step with FileNotFound; the tool now completes compile +
  resource + link on the Stable tree (a third independent toolchain
  confirming the build).

#### Unchanged — on purpose

* The engine and transport layers differ from RC3 **only in release-identity
  comment banners** (zero code lines changed — identical compiled output). Measured: micro decision p50 44–51 ns on all
  four workloads (identical medians), 20 M-key throughput 43.47 ns/key on
  both sides with byte-identical output sink, E2E/tone deltas within the
  host noise band (the A/B shim sources are identical, so those deltas are
  noise by construction). Known limitations and the honest Windows
  validation boundary are in the release report.

## [1.2.1-RC3] — 2026-09-05

### Stability, Correctness & a Leaner Engine

> **Scope:** one real pipeline bug fixed with a permanent regression test
> (found by re-running the RC2 release gate on a contended 2-CPU host), one
> high-value engine optimization (decision-identical, proven on 4.8 M
> lockstep differential events), one stale version carrier closed, and a
> flaky-by-host test made mechanism-strict. Full evidence, RC2-vs-RC3
> tables, profiling and the honest regression analysis in
> [`docs/reports/V1.2.1_RC3_RELEASE_REPORT.md`](docs/reports/V1.2.1_RC3_RELEASE_REPORT.md).

#### Fixed — pipeline correctness (`src/core/win32_wrapper.hpp`)

* **PendingEditCounter underflow after a forced lifecycle quiesce.**
  `consume()`/`rollback()` used a bare `fetch_sub`. A lifecycle
  `forceQuiesce()` (engine off, foreground auto-exclusion, power resume)
  clears the count on its own thread, but the consumer can be mid-batch: it
  already popped n edits off the ring and is about to call `consume(n)`.
  The subtraction wrapped to ~4.29 × 10⁹, the value read as "hugely
  pending", `releaseIfDrained()` never fired, and every following
  pass-through keystroke burned the full ordering-barrier budget and could
  overtake edits still in flight — the same post-wedge stall family the
  class was built to prevent. Reproduced deterministically (5/5 runs,
  `maxPending 4294967203+`, forced quiesces ≥ 2) by `tests/soak_pipeline`
  on a 2-CPU host; fixed with a saturating CAS-clamped release that can
  never lose a concurrent publish, and pinned by the new deterministic +
  hammered case 7 in `tests/test_lifecycle.cpp`. Post-fix the soak's
  pending count stays at the legitimate bound (ring + one batch) in every
  run.
* The pipeline soak gate itself: RC2's release gate passed 20/21 on the
  release host; on a contended 2-CPU host `soak_pipeline` fails 5/5 — RC3
  runs 21/21 PASS.

#### Changed — performance (`src/core/TextEngine.cpp`)

* **checkSpelling consonant scans use constexpr first-letter row buckets.**
  After RC2's own optimization pass the two table walks were still 40 % of
  engine time (gprof, 20 M-key driver): the leading scan visited all 33
  rows and the end scan all 11, although ~90 % can never match the word's
  first letter under the active option masks. RC3 visits at most 4 of 33
  (letter N) and 3 of 11 rows — equivalence proven row-by-row in the source
  (a row outside the bucket is exactly a row the RC2 loop rejected at cell
  0; on total rejection the RC2 loop always exited at j = 0), backed by
  `diff_engine_ab` at 4 seeds × 1.2 M events = **4.8 M lockstep events,
  0 mismatches**, `gate_correctness` 2.06 M events PASS, and a
  byte-identical output sink on the 20 M-key throughput driver.
* Measured: micro benchmark p50 −23 % … −46 % across all four workloads
  (vn-compose 65 → 50 ns, mixed 77 → 51 ns, passthrough 82 → 44 ns,
  delete 65 → 46 ns); 20 M-key throughput driver 57.2 → 40.2 ns/key
  (−29.5 %). checkSpelling self time 22.4 → 12.0 ns/call. E2E shim
  latency is timer-floor-bound on the benchmark host and is reported
  UNCHANGED (see the report — no fake end-to-end claims).
* No engine semantics, options, or output bytes changed. Rejected after
  profiling: further vowel-machinery restructuring (risk ≫ single-digit-ns
  gain), double-copy elimination in saveWord/pushTypingState (~0.08 ns/key
  — noise), any change to profile defaults.

#### Fixed — release identity

* `OPENKEY_KIEEKEY_VERSION_*` in `src/core/kieekey_core.hpp` still said
  `1.2.0` (missed by RC1 and RC2); now `1.2.1 RC3`, and
  `scripts/check_version.py` covers the macros so the class of drift fails
  the gate instead of shipping.
* All version carriers agree on `1.2.1 RC3`: app title/full version
  (main.cpp), PE VERSIONINFO, manifest, CMake, README, file headers.
* `SHA256SUMS.txt` regenerated for the first time since the v1.1.3 source
  import — it had been stale through v1.2.0, RC1 and RC2 (246 entries for
  a tree that no longer matched). Now 318 entries, every shipped file,
  verified with `sha256sum -c` from the parent directory; the manifest
  excludes itself (a file cannot contain its own hash) and the volatile
  build output (`build/`, `bench_runs/`, `gmon.out`).

#### Fixed — test robustness (product code unchanged)

* `tests/test_lifecycle.cpp` case 6 asserted "the 1 ms barrier budget
  always suffices", which is a host-scheduling claim: on a shared 2-CPU VM
  the freshly created consumer thread can be descheduled past the whole
  budget (observed 2/15 failures on unmodified RC2 code). The mechanism
  assertions stay strict (count released; waiter released after drain);
  only the within-1 ms part is now a reported host-slowness count,
  mirroring the documented `waitDrainedHooked` rationale in the shim.

#### Validation

* 21/21 native suites PASS (`tests/run_all_tests.sh`); differential
  decision-identity at 4 seeds; correctness gate PASS; ASan+UBSan clean on
  the engine driver, pipeline soak, differential and lifecycle harnesses;
  version-consistency gate PASS with extended coverage.
* Not executable in the release environment (documented in the report):
  Windows x64/ARM64 cross-compilation (no clang/MinGW toolchain available
  on the host this time — the RC2 gate ran it), real SendInput/TSF
  latency, CI matrix.

## [1.2.1-RC2] — 2026-09-04

### Performance, Optimization & Real-World Hardening

> **Scope:** measured engine hot-path optimization (decision-identical to
> RC1, proven by a lockstep differential against the frozen RC1 engine),
> Performance Preference Profiles, an intelligent notification framework
> with a QuickTelex unwanted-correction detector, three bug fixes with
> permanent regression tests, and a 7-scenario real-world stress battery.
> Full evidence, methodology, rejected strategies and RC1-vs-RC2 tables in
> [`docs/reports/V1.2.1_RC2_PERFORMANCE_REPORT.md`](docs/reports/V1.2.1_RC2_PERFORMANCE_REPORT.md);
> raw benchmark artifacts in `docs/bench/{rc1,rc2}/`.

#### Performance (engine — `src/core/TextEngine.*`)

* **Engine decision latency −28 % … −61 %** on every real-world workload and
  percentile (200 WPM burst p50 161 → 75 ns, p99.9 456 → 191 ns); micro
  bench −30 % … −52 %; E2E shim pipeline burst p50 −9.9 %, p99 −6.9 %.
* `HistoryRing`: bounded 64-entry ring replaces `std::vector` +
  erase-from-front for the undo history — O(1), zero hot-path heap traffic.
* `checkSpelling`: leading/end-consonant matchers fold the option masks
  once per call (were per cell); row order, short-circuit and final index
  preserved exactly.
* `handleMainKey`: QU-tail guard and sequence-tail pre-filter hoisted before
  `checkCorrectVowel` (~90 % fewer candidate calls, identical outcome).
* Rejected (measured, documented): unbounded history, `-O3`/`-march=native`
  for the product, lock-free engine access, callback/QPC trimming, pipeline
  redesign, prefix memoisation, changing the default spin/barrier values.

#### Added — Performance Preference Profiles (`src/core/PerfProfile.hpp`)

* Balanced (= RC1 behaviour, default), Fastest, Least Flicker, Maximum
  Correctness, Auto/Adaptive, plus OR-able hybrids (Prefer inline / Prefer
  TSF / Extra correctness / Low CPU). One centralized `Strategy` drives:
  output preference, consumer spin floor/cap, ordering-barrier budget/spin,
  TSF batch cap (1…64), deferred inline, dictionary restore, per-key layout
  re-check, slow-TSF downgrade. Adaptive re-resolves every second from
  telemetry (slow TSF commits, barrier misses, typing rate, idle, battery,
  CPU count). Settings tab "Chế độ xuất & hiệu năng"; persisted in the
  registry (`PerfProfile`, `PerfHybrid`).
* `ModernKeyHook::setConsumerSpinBounds`, `EditDrainBarrier::setTuning`
  (runtime; RC1 defaults unchanged).
* `tests/bench_profiles.cpp` — measures every profile through the shim
  pipeline (RC1/default, RC2/default, Fastest, Balanced, Max Correctness,
  Least Flicker, Adaptive, two hybrids).

#### Added — Intelligent notifications (`src/core/Notifications.hpp`)

* `NotificationCenter`: stable ids, confidence thresholds, per-id cooldown,
  hourly caps, global 60 s gap, dedup, session mute, persisted "Don't show
  again" (`NotifySuppress_<id>`). Lock-free producer slot; UI polls once per
  second — nothing on the input path.
* `QuickTelexDetector`: general heuristic over all quick-Telex pairs
  (`cc gg kk nn qq pp tt`), confirmed-undo scoring with acceptance decay;
  raises "Telex nhanh đang sửa nhầm từ của bạn?" with **Turn off / Keep /
  Don't show again** (tray balloon → message box). Also raises: TSF slow
  downgrade, hook re-installed, barrier timeouts.

#### Fixed

* **ODR / link failure at `-O1` and under ASan**: `FlatTables.hpp::codeTableFor`
  was `inline` while referencing internal-linkage tables → `static inline`.
  Regression: `test_hotfix_asan` (-O1 + ASan/UBSan build in the suite).
* **`gate_correctness --max-words=N` rejected** → `run_all_tests.sh --quick`
  had been failing (rc=2) since v1.2.0. Fixed.
* **Ctrl/Alt chords consumed by a wrong-spelling Restore** (Ctrl+`,`
  Ctrl+`.` Ctrl+Enter, Ctrl+Tab, Alt+Enter after a non-Vietnamese word such
  as `wolf`/`wifi`): the shortcut never reached the app and the word was
  retyped. Hook-level guard: a chord is never suppressed, never rewrites
  text. Regression: `stress_rc2` scenario 4. Found by the new stress battery.

#### Added — Tests

* `tests/diff_engine_ab.cpp` + frozen RC1 engine under
  `tests/reference/kieekey-1.2.1-rc1/` (lockstep differential, 1.2 M events).
* `tests/test_notifications.cpp`, `tests/stress_rc2.cpp` (burst
  determinism, 2 M-key leak check, correction-heavy + detector, mixed
  mode-switching + chords, Unicode/emoji, 3 h notification-cap simulation,
  concurrent producer/UI). 21 native suites in `run_all_tests.sh` and CTest.

#### Changed

* Version `1.2.1 RC1` → `1.2.1 RC2` in every carrier (incl. the demo, which
  had been left at 1.2.0).
* Slow-TSF telemetry is always counted; the automatic downgrade is now
  profile-gated (Least Flicker keeps TSF) and raises a notification.

## [1.2.1-RC1] — 2026-09-04

### Real-world performance and smoothness release

> **Scope:** a real-world performance, latency smoothness, CPU efficiency,
> and resource-usage release on top of v1.2.0 Stable. **Not** a feature
> release. Every change targets a measurable real-world typing improvement;
> no change was accepted that improved a microbenchmark while degrading
> user-perceived smoothness. Full investigation detail in
> [`docs/reports/V1.2.1_RC1_PERFORMANCE_REPORT.md`](docs/reports/V1.2.1_RC1_PERFORMANCE_REPORT.md).

#### Changed — Consumer spin algorithm (EWMA + idle decay)

* **Replaced recent-max adaptive spin with EWMA + idle decay.** The v1.2.0
  algorithm used `recentMaxGap` over a 32-slot window with a 200 µs cap.
  A single unusual inter-keystroke gap (user pausing to think, then
  resuming typing) inflated the spin budget for all subsequent keystrokes
  until 32 new gaps overwrote it. After idle, the consumer continued
  spinning for up to 200 µs on every empty dequeue cycle based on stale
  burst data, burning CPU for no benefit.

  The new algorithm uses an Exponential Weighted Moving Average (EWMA)
  with asymmetric α: fast attack (α=¼) when gaps shrink (burst starting),
  slow decay (α=¹⁄₁₆) when gaps grow (single pause). An idle-decay check
  resets the EWMA to floor when more than 50 ms have elapsed since the
  last batch arrival. The spin cap is reduced from 200 µs to 100 µs.

  **Result:** During a burst the consumer stays hot (low wake cost). After
  idle the consumer parks immediately (zero CPU waste). A single outlier
  gap cannot inflate the spin budget for future bursts.

#### Changed — Thread and process priority policy

* **Hook pump thread: TIME_CRITICAL → HIGHEST.** The hook pump installs
  hooks and pumps messages. Its LL callbacks are O(1) (build KeyEvent,
  try_push, return). TIME_CRITICAL on this thread preempted the foreground
  application's own UI thread for no measurable benefit. HIGHEST is
  sufficient for the message pump.

* **Process: HIGH_PRIORITY_CLASS → ABOVE_NORMAL_PRIORITY_CLASS.**
  HIGH_PRIORITY_CLASS elevated EVERY thread in the process (UI thread,
  monitor thread, watchdog, settings dialog) above normal applications.
  Combined with TIME_CRITICAL on both worker threads, this created
  scheduler contention with the foreground application's text rendering
  thread — the user-visible symptom was "fast microbenchmarks but typing
  feels laggy under load". ABOVE_NORMAL keeps the IME ahead of background
  work while yielding to the foreground app's own threads.

  The CONSUMER thread (which actually applies edits) stays at
  TIME_CRITICAL. That is the thread whose responsiveness directly
  determines user-perceived latency.

#### Added — Real-world typing benchmark

* **`tests/bench_real_world_typing.cpp`** — models human-like typing
  patterns (variable inter-key intervals, bursts followed by pauses,
  backspace corrections, spaces, punctuation, single keys separated by
  long idle periods). Reports both latency (p50/p90/p95/p99/p99.9/max)
  AND resource cost (CPU time per key, wall-clock throughput) for each
  workload. Six workloads: burst_200wpm, normal_60wpm, slow_30wpm,
  burst_pause, single_keys, mixed.

#### Verified and deliberately unchanged

* **TextEngine composition semantics** — byte-identical to v1.2.0 Stable.
  No engine change in this release.
* **Lock-free queue (SPSCRing)** — unchanged. The Vyukov ring is correct
  and not a bottleneck.
* **EditDrainBarrier** — unchanged. The hybrid spin+event barrier is
  correct and its 1 ms budget is appropriate.
* **TSF commit path** — unchanged. No TSF regression.
* **All v1.2.0 correctness tests** — pass without modification.

## [1.2.0] — 2026-09-04

### Feature + performance release on top of the v1.1.3 hardening pass

* **Macro expansion at printable punctuation (opt-in).**
  `EngineOptions::macroExpandsOnPunctuation` (default `false`, preserving
  OpenKey-2.0.5 byte parity). When enabled, a printable macro-break
  character typed with kind `Char` (`, . ; / ' \\ - =`) expands the pending
  abbreviation AND stays visible in the output (`xl,` → `xin lỗi,`), and
  the macro-key accumulator is reset after the match so consecutive
  `xl, xl,` both expand. Legacy default keeps the 2.0.5 semantics: the
  trigger is swallowed and the poisoned accumulator blocks the next match.
* **VIQR output parity for macro expansions.**
  `EngineResult::macroExpansionUtf16` renders through the VIQR pipeline when
  `OutputEncoding::Viqr` is selected — precomposed Vietnamese no longer
  leaks as raw Unicode into pure-ASCII VIQR output.
* **Grammar-gate fast path.** `wordHasTransform_` records whether the
  current word ever received a transform; `checkGrammar()` cannot repair a
  word with no transform mask, so the per-key re-scan is skipped for those
  words (monotone within a word, conservative on restore).
* **Benchmark & verification tooling:** hardened `imebench_kit` perf
  harness (warm-up, per-workload session reset, sorted percentiles, no-op
  timing-overhead control, multi-run JSON with full decile+tail
  distributions), multi-run `--json` mode in `tests/e2e_bench.cpp`, and a
  deterministic differential correctness gate (engine vs clean-room
  oracle; 2.06M events in ~1 s). Evidence in
  `docs/reports/V1.2.0_BENCHMARK_REPORT.md`.

### 1.2.0 Stable — stability / correctness / real-world-UX hardening pass

> **Scope:** a stability, correctness, smoothness, robustness and real-world
> UX release on top of the v1.2.0 feature/perf tree. **Not** an optimization
> campaign — two changes were accepted that cost a sub-nanosecond amount of
> throughput because they remove a user-visible failure mode. Full detail in
> [`docs/reports/V1.2.0_STABLE_RELEASE_REPORT.md`](docs/reports/V1.2.0_STABLE_RELEASE_REPORT.md);
> the reference it is measured against is
> [`docs/reports/V1.2.0_STABLE_BASELINE_REPORT.md`](docs/reports/V1.2.0_STABLE_BASELINE_REPORT.md).

#### Fixed — P1 (no known P0/P1 remains at release)

* **An escaping exception killed the IME process mid-keystroke.**
  `ModernKeyHook::consumerThreadMain()` is `noexcept`, so any exception out
  of the consumer handler (a plain `std::bad_alloc` under memory pressure is
  enough) called `std::terminate()`. The IME vanished while the tray icon
  stayed behind — every window then produced raw, uncomposed Vietnamese with
  no visible signal. The handler call is now fault-isolated: one bad event
  is dropped and counted (`handlerExceptionCount()`), the pipeline keeps
  running, and no further edits can be produced while the engine is off.
* **`resumeFromText()` inflated the backspace clamp — silent data loss.**
  Every caret-moving keystroke (arrows, Home/End, PgUp/PgDn, Ins,
  Ctrl+Backspace) and every mouse click queues a resync; each one replayed
  the visible word through `process()` and counted every replayed letter as
  newly committed, inflating `visibleAccount_` by `k x len(word)`. That
  counter is the **only** clamp on how many characters a correction may
  delete, so a later edit could delete text to the LEFT of the word that the
  engine never committed — silently, with no error anywhere. Repro: click
  into a partially typed word, press Right a few times, finish the word.
  Fixed by (a) early-returning from `process()` when `rawReplay_` is set, so
  replayed letters are never counted, and (b) setting
  `visibleAccount_ = rawWord.size()` at the end of `resumeFromText()`, which
  is the exact upper bound on any legitimate backspace.

#### Fixed — P1 (continued): keystroke correctness false positive

* **"Typing 'p' twice turns it into 'ph'" — with "gõ tắt" (quickTelex)
  enabled, every doubled consonant was expanded into a Vietnamese cluster at
  ANY position in the word.** `isQuickTelexKey()` only checked "the key equals
  the last character of the pending word", so real Latin/English words were
  silently mangled: `happy→haphy`, `apple→aphle`, `letter→lether`,
  `account→achount`, `running→runging`, `success→suches`, `tattoo→tathoo`,
  `supper→supher`, `copper→copher`, `attitude→athitude`, `button→buthon`,
  `cc→ch`. Vietnamese is syllabic: every one of those clusters (ch, gi, kh,
  ng, ph, qu, th) is **syllable-initial** and no Vietnamese syllable contains a
  doubled consonant, so the rule now requires the pair to be the first two
  letters of the word. 100% of the legitimate use is preserved
  (`ppongf→phòng`, `ccaof→chào`) and every word-medial false positive is
  impossible. 37 real words are now pinned byte-for-byte in
  `tests/test_key_correctness.cpp`, and the suite is proven to fail when the
  fix is reverted.
* **Test-fidelity bug found while investigating it:** the consumer model in
  `tests/test_state_transitions.cpp` applied `EngineResult::backspaceCount`
  unconditionally, which does **not** match the shipped hook. The real rule
  (src/app/main.cpp) only applies an edit when the result is *consumed*, so a
  `DoNothing` result with a non-zero backspaceCount (the grammar-repair path)
  is a pass-through. The model was emitting `work→ửokk` where the real IME
  emits `work→ửok`. Fixed; the suite still passes all 15 877 checks.

#### Verified as CORRECT and deliberately left alone (not bugs)

Investigated exhaustively (every 1-, 2- and 3-letter lowercase word = 18 278
sequences, plus a curated corpus) and confirmed byte-identical to upstream
OpenKey 2.0.5:

* **A doubled tone key undoes the mark** — `ass→as`, `chess→ches`,
  `coffee→cofee`, `cass→cas`. Documented legacy-hook semantics (see the comment
  in `tests/engine205.cpp`).
* **`z` removes the tone mark** — `mà`+`z`→`ma`. `handleMainKey`'s "Z key
  removes mark" branch, identical to upstream `Engine.cpp:1056`.
* **`w`→`ư`, `oo`→`ô`, `aa`→`â`, doubled vowels** — standard Telex.

These are pinned as expectations in case 5 of `tests/test_key_correctness.cpp`
so a future "fix" cannot silently change them.

#### Changed — developer workflow speed

* `tests/run_all_tests.sh` was **84 s → 35 s** (17 targets). Two real defects:
  `--jobs` was parsed but **never used** (every build and every run was
  strictly sequential), and `src/core/TextEngine.cpp` — the dominant
  translation unit at ~1.7 s — was recompiled from scratch for all 13
  engine-linked targets. It is now compiled once per language dialect and
  linked, and builds and runs are dispatched with `xargs -P`.
* `tests/bench_tone_latency.cpp` was **866 s → 134 s** by default. Its "mixed"
  population types a real passage at a real typing pace (it sleeps between
  keystrokes — that is the point of a paced simulation), so the exhaustive
  3-pace × 3-rep × 3-path × 2-barrier matrix was inherently wall-clock-bound.
  The default is now the two bookend paces at one rep; **`--fast`** gives one
  pace/one rep (~50 s) and **`--full`** restores the complete matrix for a
  release run. The `--help` output documents all three.

#### Fixed — P2

* **Stranded pending-edit count: a 1 ms stall on every keystroke.** A
  lifecycle transition that stops the producer (engine off, foreground app
  auto-excluded, pipeline restart, power resume) left the barrier's count
  armed with nothing left to wake the consumer, so every following
  pass-through keystroke paid the full 1 ms barrier budget and was delivered
  out of order. `PendingEditCounter` was extracted from `src/app/main.cpp`
  into `win32_wrapper.hpp` (now unit-testable on every platform),
  `ModernKeyHook::pokeConsumer()` can wake the consumer from any thread, and
  `drainPendingEditsForLifecycle()` (poke -> bounded wait -> poke -> bounded
  wait -> `forceQuiesce()`) replaces every bare barrier wait at a lifecycle
  point.
* **Dropping the wake signal on ring overflow.** The `KeyEvent` payload is
  only a signal — the real work was already pushed and already published.
  Dropping the wake stranded that edit. Overflow now always wakes, counted in
  `overflowWakeCount()`.
* **Data race on the pump-tick callback.** `stop()` on the detach path
  assigned `pumpTick_ = nullptr` — a non-atomic write to a `std::function`
  the pump thread may be reading (torn read = use-after-free of its
  captures). Replaced with an atomic liveness flag the pump checks before
  invoking the callback.
* **Data race on the hook-installation handshake.** `start()`'s polling loop
  read a handle member that the pump thread writes (UB; would fail any future
  TSan build). Replaced with an atomic `hooksInstalled_` flag, re-armed in
  `start()` and cleared on the pump's exit path.
* **Version-bearing singleton/wake names — a real upgrade hazard.**
  `KieeKey_1.1.0_Singleton` / `KieeKey_1.1.0_Wake`: the moment the name
  changed, a running old instance and a newly launched new one no longer
  shared a mutex, so **both** started, **both** installed a low-level
  keyboard hook, and every keystroke was composed twice. Now version-free
  (`KieeKey_Singleton`, `KieeKey_Wake`), enforced by
  `scripts/check_version.py`.
* **Stale hook thread id after shutdown** — a later `stop()` could
  `PostThreadMessage(WM_QUIT)` to a recycled id and deliver it to an
  unrelated thread. Cleared on the pump's exit path.
* **Producer-side fault isolation** — the hook producer callback isolates
  exceptions and sets a deferred repair flag instead of leaving the pipeline
  half-updated.

#### Fixed — P3 (real-world UX)

* **Power / session / display lifecycle.** `WM_POWERBROADCAST` (suspend,
  resume), `WM_WTSSESSION_CHANGE` (lock, unlock, fast-user switch, remote
  connect/disconnect) and `WM_DISPLAYCHANGE` were ignored, so the state
  carried across a resume was whatever happened to be cached: tracked
  Shift/Ctrl/CapsLock bits (a modifier released while the secure desktop
  owned the keyboard is never seen as a key-up), the cached HKL, and the
  per-app exclusion / TSF-vs-inline policy for a foreground that no longer
  exists. All three are now handled: session reset, modifier re-seed, HKL and
  exclusion re-probe, and settings-dialog re-scale on display change.
* **DPI: the settings dialog only resized its frame**, leaving every child
  control and font at the old DPI (clipped text, misplaced controls).
  `refreshSettingsDpi()` / `rescaleChild()` now re-scale position, size and
  font, backed by a per-DPI font cache that never evicts an in-use font (the
  old one deleted fonts still attached to live controls).
* Stale single-instance state on shutdown; a foreground-probe race (the
  probed HWND is now carried in `wParam` and validated).

#### Changed — build, tests, tooling

* **CTest: 3 -> 14 targets.** Twelve harnesses existed in `tests/` and were
  never executed by any automated gate — "full suite green" was true only in
  the weak sense of "everything that ran, passed". All are now registered,
  with a **300 s hard timeout** each so a future hang fails the job in
  minutes instead of burning the CI budget.
* **New suites:** `tests/test_state_transitions.cpp` (engine state matrix —
  15 877 checks, 0 failures, with a dedicated regression property for the
  `visibleAccount_` bug), `tests/test_lifecycle.cpp` (pipeline lifecycle +
  wake protocol — 2 511 checks), `tests/soak_pipeline.cpp` (1.28 M-edit
  transport soak: conservation, strict ordering, boundedness, liveness, flat
  RSS).
* **New tooling:** `tests/run_all_tests.sh` (one-command suite runner),
  `tools/crossbuild_windows.sh` (reproducible Windows cross-build),
  `scripts/check_version.py` (version-identifier gate, now also a CTest
  target).
* Version bumped to **1.2.0 Stable** across every carrier, including the
  previously-stale `scripts/*`, `imebench_kit/harness/*`, the WinUI 3 about
  banner and `THIRD-PARTY-NOTICES.md`.

#### Performance vs the frozen baseline

Same-tree A/B (the authoritative method — see the report, section 4.1, for
why comparing binaries built from different trees measures code-layout
variance): **mean delta p50 +0.28 %, mean delta p99 +0.19 %** — well under
one nanosecond per keystroke. No regression on the tone populations that
carry the real cost (`mixed`/TSF 101.94 -> 101.98 us) or on pipeline
throughput (13 502 -> 13 531 keys/s). Memory flat.

#### Deferred

* **D-1 (P2)** the optional WinUI 3 front-end (`KIEEKEY_BUILD_UI=winui3`, not
  built by CI) does not yet use the pending-edit counter / drain barrier.
  Shipping uncompiled, untested concurrency code there is a worse trade than
  documenting it; its version string was corrected.
* **D-2 (P2)** the new lifecycle/soak suites model the Win32 `SetEvent` /
  `WaitForSingleObject` pair with a condvar — they prove the *protocol*, not
  the Win32 calls. Real-Windows sign-off remains a manual step.
* **D-3 / D-4 (P3)** CI-host noise at p99; no automated real-Windows UI test.
* **D-5 (P3)** the D2 over-backspace policy still has exactly one clamp.
  It is now correct and regression-tested; a second independent clamp would
  change composition semantics and is recommended for v1.3.

## [1.1.3] — 2026-09-03

### Quality/performance hardening release — accuracy, latency, stability,
### robustness of the core typing pipeline (no new features)

> Scope: audit the entire input pipeline (LL hook → lock-free ring →
> engine → TSF composer) for root causes, fix them without trading away
> correctness, and close the differential suite at 0 divergences.

### Fixed — accuracy (P1)

* **CapsLock/NumLock/ScrollLock toggle tracker desynchronized by
  auto-repeat** (`ModernKeyHook.cpp`). The tracker XORed the toggle bit on
  EVERY KeyDown, but the low-level hook receives auto-repeat key-downs while
  the key is held: holding CapsLock for half a second XORed the tracked bit
  15–30× and left it OUT OF PHASE with the OS — every later tone-mark / đ /
  â transform re-emitted its word with the wrong case ("vợ" → "vỢ"). The
  tracker now flips on the up→down EDGE only (256-bit edge bitmap, hook-
  thread-affine), re-arms on modifier resync, and uses atomic RMW so a
  concurrent resync can no longer clobber it.
* **36 engine↔oracle divergences closed (mega differential: 36 → 0 at the
  full 268M-event budget; Cat C "KieeKey regression" count 77 → 0).** The
  independent oracle (`tests/vi_oracle.hpp`) had missed the documented
  v1.1.0 engine contracts and was re-aligned to them: (1) the
  `vniVowelEndValid_` validity-tracked VNI vowel scan (a digit 6 with no
  O/A/E in the word used to compose onto a STALE vowel from a previous
  word — the engine already refused it, matching upstream 2.0.5); (2) the
  D3 macro-visibility model (expansion replaces raw keys, backspaces walk
  the expansion down, a fresh letter re-arms the macro bookkeeping);
  (3) the `checkQuickConsonant` buffer-limit guard. Arbitration for every
  class was done against the vendored upstream 2.0.5 engine, not by taste.
* **Quick-consonant transform at the buffer limit** (`TextEngine.cpp`):
  at `index_ == kMaxBuff-1` the transform computed `newCharCount = 32`
  while the guarded insert no longer ran, emitting a stale 32nd character
  and a backspace/insert count mismatch. It now refuses the transform
  beyond the bound (mirrored in the oracle; unit test added).
* **TSF stale-focus commits** (`TsfComposer::ensureContext`): the cached
  `ITfContext` is now verified against the thread manager's CURRENT focus
  on every commit (in-process read, ~ns). This closes the cross-app text
  injection hole where the first commit after the v3.5 hang-probe gate
  re-used the PREVIOUS application's document, plus same-window focus
  changes (browser tab switch, page ↔ omnibox) that no foreground event
  observes.
* **Truncated context re-sync** (`ReadBeforeCaretSession`): the caret
  read-back replays raw ASCII keystrokes; when a composed Vietnamese
  letter (or digit) sits directly before the ASCII run, the visible word
  continues beyond what the read can represent. Replaying the truncated
  tail seeded the engine with a SHORT word and the next tone key then
  deleted the WRONG characters at the caret. The session now fails and
  the safe empty-buffer degradation applies.
* **Toggling OFF now drains queued edits** (`main.cpp`): with TSF edits
  still pending against a slow foreground, disabled-mode letters reached
  the app immediately while the stale edit committed later and deleted
  the wrong characters. OFF is now a clean cut-off point (bounded drain),
  and an in-flight keystroke re-checks the enabled flag under the engine
  lock so a decision cannot emit after the OFF transition.

### Fixed — latency (P2)

* **Phantom pending-edit race removed** (`main.cpp`): the producer
  incremented `pendingEdits` AFTER publishing the item (relaxed) while the
  consumer's "reached zero" test compared against the fetch_sub previous
  value. A preemption in that window left a PHANTOM pending count armed
  forever — every later pass-through keystroke then paid the full 1 ms
  barrier wait. The count is now published BEFORE the push (acq_rel) and
  the zero-test re-reads the counter.
* **Wheel events no longer take the ordering barrier** (`main.cpp`): a
  wheel notch does not move the caret, but the wait ran on the SHARED hook
  pump thread — scrolling while edits were in flight stalled keystrokes
  for up to the 1 ms budget per notch.
* **Settings dialog no longer holds the engine lock across control
  reads** (`main.cpp`): all ~25 cross-thread control reads are hoisted
  above `engineMtx`; only the state swap remains in the critical section.
  Typing during OK/Apply no longer blocks on dialog controls.
* **Foreground-change cache staleness**: the in-window keyboard-layout
  cache now re-reads the foreground thread's HKL on Space/WordBreak
  (two user-mode reads, no syscall), so Win+Space / Ctrl+Shift layout
  switches WITHIN one window compose correctly by the next word instead
  of after the next app switch.

### Fixed — stability (P3)

* **Elevated foreground passes keystrokes through** (`ProcessMonitor` +
  `main.cpp`): a non-elevated IME can never reach an elevated target —
  TSF cannot marshal across UIPI and SendInput fails SILENTLY; composing
  "á" swallowed the user's letters outright in elevated cmd/PowerShell/
  Task Manager. The foreground's integrity level is now probed on app
  switch and an elevated target joins the auto-exclusion set (raw
  pass-through + engine reset) instead of eating keystrokes.
* **Hook watchdog per-hook detection + active probe** (`win32_wrapper`):
  the old fast path collapsed keyboard/mouse/self stamps into one max, so
  a silently-removed WH_KEYBOARD_LL stayed invisible while the mouse was
  in use. The keyboard stamp is now evaluated INDEPENDENTLY and the
  ambiguous "system input fresh, keyboard stamp stale" state is resolved
  by an ACTIVE PROBE (self-tagged VK 0xFF down+up — a live hook stamps and
  filters it, the app never sees it). Dead-hook detection now works during
  mouse-only sessions; the missed stamp scenario self-heals in ~10–25 ms.
* **`SendInput` failures are counted and no longer mask the watchdog**:
  the emitter stamped the self-heal tick BEFORE the call, feeding the
  heartbeat with input that never reached the system; failed batches also
  dropped the user's edit silently. Only accepted input stamps now, and
  rejections are counted for diagnostics.
* **Inline emission serialized** (`InlineEmitter::sendEdit`): the hook
  thread's ring-full fallback and the consumer's fallback could emit two
  interleaved SendInput streams; SendInput is atomic per call, not across
  calls. Cold paths only — no cost on the hot inline path.
* **`ProcessMonitor::start()` refuses restart after a stuck-detach**
  (mirrors `ModernKeyHook`): the wedged pump's fallback timer re-observed
  `running_ == true` and resumed, giving two pumps fighting over one hook.
* **OOM hardening at `noexcept` boundaries** (`ProcessMonitor::
  updateFromWindow`, `Win32Wrapper::enableSelfHealing`, the consumer drain
  loop): an allocation failure previously `std::terminate`d the whole IME
  mid-keystroke ("suddenly stops"). The monitor keeps the previous
  snapshot and retries on the next event; self-healing starts disabled;
  the drain loop degrades to the SendInput fallback.
* **Composer attach latch resets on failure** — a failed
  `TsfComposer::attach()` no longer keeps TSF dead for the whole process
  lifetime.
* **`RPC_E_CHANGED_MODE` now fails the composer attach** cleanly (MTA
  thread cannot host TSF focus machinery) — the caller falls back to
  inline SendInput instead of unpredictable degradation.
* **Mid-session TSF failure no longer duplicates pure-insert deltas**:
  the edit session records how many deltas were committed; the SendInput
  fallback delivers only the unapplied suffix (previously "t" + "to" came
  out as "tto" when a mid-batch delta failed).
* **`WM_ENDSESSION` quits cleanly** — previously the teardown left a live
  tray icon over a dead IME with no restart path.
* **Settings-dialog exception safety** — `WM_CREATE` wraps its body (a
  `bad_alloc` unwinding through `CreateWindowExW` is UB), the handle is
  published only for a real window, and `GetMessageW(-1)` no longer runs
  the full shutdown path.
* **Stuck-detach pump callback nulled** — the detached pump's WM_TIMER
  tick can no longer reach the watchdog after owner teardown begins.
* **`DropOldest` overflow policy removed** — the producer-side `try_pop`
  violated the SPSC contract (the tail is consumer-owned); the app never
  selected it, and the policy is now unconditionally DropNewest.

### Tests

* Watchdog: new regression test for the dead-keyboard-hook-during-mouse
  case; the two immunity tests re-modeled for per-hook detection.
* Engine: new golden vectors pinning the VNI no-vowel digit pass-through,
  the macro D3 backspace model (expansion walk-down + fresh-letter
  re-arm), standalone-bracket composition, and the quick-consonant
  buffer-limit guard.
* Harness: deterministic `--repro <file>` replay mode with per-event
  engine/oracle tracing, plus `KIEEKEY_SUITES` chunked runs for CI.
* Mega differential re-verified at the FULL budget (13 suites, ~268M
  events): **0 text mismatches, 0 stale-buffer defects, 0 over-backspace,
  0 macro gaps, Cat C = 0**.

### Performance

* Engine micro-benchmark unchanged vs 1.1.2 (99–101 ns/key vs 99–105
  ns/key on the same host); E2E burst latency unchanged (p50 ≈ 9.8 µs,
  p99 ≈ 52 µs through the shim, host-bound). The barrier and lock fixes
  only remove wait time from paths that USED to block.

## [1.1.2] — 2026-09-03

### Fixed — "typing a number produced a tone mark or changed the word"
### (the digits-are-numbers fix)

> v1.1.2-r3 root-cause closure (same version, re-verified build): the
> report STILL came back after r2, so every remaining place the bug could
> hide — including outside the fixed binary — was hunted down and closed:
> - **The WinUI 3 front-end (`src/ui`) had none of the fix.** It created
>   its own `TextEngine` with legacy options (digits compose in VNI),
>   never read `DigitsLiteral`, and never even applied the saved input
>   method to the engine at startup. Any VNI user of that build kept
>   seeing digits → tone marks no matter what the Win32 app fixed. The
>   engine is now created FROM the persisted options (digits policy +
>   the same one-time `SettingsMigration` self-heal — the front-ends
>   share one registry key), the saved method/table actually reach the
>   engine, and a "Số 0–9 luôn là chữ số" checkbox was added.
> - **The library default contradicted the product promise.**
>   `EngineOptions::digitsAreLiteral` now defaults to TRUE — every
>   `TextEngine{}` consumer gets digits-are-numbers out of the box; the
>   legacy-parity harnesses pin `false` explicitly, and new test vectors
>   fail if the default is ever flipped back.
> - **Hook-layer NUMBER-SAFETY GUARD (defense in depth):** when the
>   digits policy is ON, the Win32 hook discards any engine decision for
>   a bare digit Char event and passes the digit through untouched
>   (counted for diagnostics — expected to stay 0). No engine path,
>   current or future, can edit text on a digit again.
> - **External causes are now detected and surfaced** — the two reasons
>   the symptom survives even a perfect patch: (1) another Vietnamese
>   IME running alongside (EVKey/UniKey/OpenKey/GoTiengViet/LabanKey)
>   converts digits itself; (2) the Windows 10/11 built-in "Vietnamese -
>   Telex" / "Vietnamese - Number Key-Based" keyboard layouts convert
>   digits at OS level. KieeKey scans for both at startup and on every
>   settings open, and names the culprit in the welcome balloon and on
>   the Information tab.
> - **Build proof:** the welcome balloon and the new live diagnostics
>   block (Information tab) state the exact running version + engine
>   state + digits policy + conflict verdict — a machine still
>   auto-starting an old pre-fix exe is now immediately identifiable.
>
> v1.1.2-r2 hardening (same version, re-verified build): the report kept
> coming back on machines that had run earlier builds, so the fix is now
> armored at every layer that could silently lose it:
> - The APP default for "Số 0–9 luôn là chữ số" is now ON in the app layer
>   itself (`AppState`), not only in the registry read — a denied/corrupted
>   HKCU key can no longer ship digits-as-composition out of the box.
> - One-time settings self-heal (`SettingsMigration` registry marker): any
>   install that never ran this release gets the digits policy re-asserted
>   to ON and persisted once; afterwards the user's own checkbox choice is
>   respected forever.
> - Fail-safe settings-dialog reads (`dlgChecked` + guarded radio/combo
>   groups): a missing or half-built control can never silently flip a
>   persisted option (the silent-zero class that could re-enable digit
>   composition through a plain OK click).
> - `scripts/audit_controls.py` regression guard: every settings control
>   id read by the dialog code is verified to be created in WM_CREATE.
> - New exhaustive digit battery in `tests/test_textengine.cpp` (every
>   digit × every context × every mode, restore/macro/policy-switch
>   interplay) — ASan/UBSan clean; digit-path benchmark
>   `tests/bench_digits.cpp` shows 36–40 ns/key, no regression.

The reported fatal bug: typing a number mid-word composed Vietnamese
instead of typing the digit — "nhan5" became "nhạn", "d9" became "đ",
"xong<1" (backspace then 1) became "xón", an identifier like "1a2b3"
became "1àb3". Root cause: in **VNI mode every digit 1–0 is a composition
key** (1–5 = tones, 6/7/8 = vowel marks, 9 = đ, 0 = tone removal) and the
engine treats a mid-word digit as a mark/vowel key. That is correct
classic-VNI behavior — and exactly wrong for everyone who does not type
Vietnamese by the digit convention.

* New `EngineOptions::digitsAreLiteral` (engine): when TRUE, digits 0–9
  are never special keys in any input method — `isSpecialKey()` no longer
  reports VNI digit keys, so every digit inserts literally (word-start
  digits and Shift+digit symbols already passed through unchanged).
* New registry value `HKCU\Software\TuyenMai\OpenKey\DigitsLiteral`
  (DWORD, **default 1**) persisted at every change point, wired to a new
  always-visible checkbox on the Bàn phím tab: "Số 0–9 luôn là chữ số —
  không dùng số để gõ dấu tiếng Việt (VNI)". Turning it OFF restores
  classic VNI digit composition for VNI typists.
* Telex and Simple Telex are byte-identical to v1.1.1 (digits already
  passed through there); with the option OFF the VNI path is byte-identical
  to the legacy engine (verified by the mega differential below).
* Regression tests: literal-digit goldens for Telex and VNI, session-health
  (a digit-bearing word cannot poison the next word), word-break restore
  re-emitting raw keys with digits, and legacy VNI composition kept when
  the option is off (`a1`→á, `d9`→đ, `a10`→a).

### Added — Information tab (in-app introduction)

The settings dialog gained a fifth tab, "Thông tin", introducing the app
inside the app: a display-size name + version, tagline, an about paragraph
(what KieeKey is, what the modernized core does differently), the feature
list, a quick-start guide (how to toggle, how to switch methods, what F9
does, how macros expand), origin & GPLv3 licensing text, and a SysLink
control opening the repository in the browser.

* Tray menu gained "Thông tin & giới thiệu" (`IDM_ABOUT`) opening the
  dialog straight on the Information tab.
* The settings window now has ONE creation path (`openSettingsDialog(tab)`)
  used by the tray menu (Cài đặt… and Thông tin), the tray double-click and
  the second-instance wake — the requested tab is selected before the
  window is created (no post-create flicker).

### Changed — modernized settings dialog

* Always-visible header on top of the tabs: application icon, app name +
  version (display-size font) and a live status line — engine on/off,
  input method and the digits policy at a glance, refreshed with the
  existing 500 ms timer.
* The Bàn phím tab is grouped into "Phương thức gõ" (with a one-line
  explainer of how each method types marks), "Tùy chọn gõ" (the digits
  option first) and "Chế độ xuất" group boxes; every tab gained the wider
  layout that fits the new 560×608 dialog.
* The in-app ON/OFF button is rendered in a semibold font and its label is
  only rewritten when it actually changes (fewer needless SetWindowText
  flushes on the 500 ms tick).
* Bold/display font variants of the dialog font are DPI-cached exactly
  like the regular face (no clipped text on mixed-DPI setups).

### Changed — consistency, correctness hygiene, performance

* Test CHECK macros in `tests/test_hotfix.cpp` / `tests/test_v331_features.cpp`
  no longer use `if constexpr` with runtime conditions — non-conforming
  C++ (a constexpr-if condition must be a constant expression) and a hard
  error on GCC/clang. Plain `if` + the project-wide `/wd4127` is correct
  on every compiler.
* All release-identity strings bumped to 1.1.2 (`kAppVersion`/`kAppTitle`,
  `.rc` VERSIONINFO 1,1,2,0, manifest assemblyIdentity, CMake
  `project(VERSION 1.1.2)`, `OPENKEY_KIEEKEY_VERSION_STRING`, WinUI banner);
  the v1.1.1 Windows `WM_APP+2`/singleton naming and historical fix
  annotations are intentionally preserved.
* Re-verified with the full harness: unit suites (textengine, hotfix,
  ringbuffer, outputitem, win32wrapper, v3.3.1 features), the mega
  differential against the 2.0.5 engine (~4.7M events, **0 divergences**),
  edge-behavior determinism (byte-identical across typing speeds), steady-
  state stress (**zero heap allocations per keystroke**) and the latency
  benchmark (~100 ns/key — the digitsAreLiteral check is a single bool
  test off the special-key path).

## [1.1.1] — 2026-09-03

### Removed — the global Ctrl+Shift toggle hotkey (root cause of "the IME
### suddenly turns off for no reason")

The final user report pinned the residual trigger: touching Ctrl+Shift
switched the input method OFF with nothing shown and no obvious way back.
Across 1.0.x–1.1.0 the chord state machine was hardened repeatedly (bare
chord detection, injected-event filtering, resync-dirtying), but the
fundamental conflict remains: a bare Ctrl+Shift press+release is ALSO the
Windows "between input languages" chord and the prefix of dozens of
application shortcuts, third-party software injects it (RDP clients, key
remappers, automation tools), and a missed modifier key-up (UIPI/secure
desktop, RDP, another hook's swallow) can make chords the user never typed
fire. A keyboard-level toggle for an input method is therefore inherently
phantom-toggle-prone. v1.1.1 removes the mechanism entirely:

* `src/core/CtrlShiftChord.hpp` deleted; the hook-side chord tracker and the
  whole hotkey branch in `onHookEvent` are gone. The keyboard hook can no
  longer change the on/off state AT ALL.
* `tests/extreme_toggle.cpp` (the chord fuzz harness) removed together with
  the feature it tests; the chord section in `tests/test_hotfix.cpp`
  dropped as well.
* On/off is now EXPLICIT and IN-APP only:
  * Tray menu first item states the action: "Tắt gõ tiếng Việt" (running) /
    "Bật gõ tiếng Việt" (stopped).
  * Settings dialog: an always-visible toggle button on the button row
    ("Bộ gõ: ĐANG BẬT — bấm để TẮT" / "ĐANG TẮT — bấm để BẬT"), live-refreshed
    every 500 ms, reachable from every tab.
  * Both paths share one `toggleEngineFromUi()` helper: it drops stale
    engine word state, re-seeds the hook's modifier tracker when re-enabling
    (safe — single atomic store), updates the tray icon, PERSISTS the state
    immediately and shows a confirming tray balloon ("Đã BẬT/TẮT gõ tiếng
    Việt"). No toggle can happen silently anymore.
* `WM_APP_TOGGLE` retired (the hook thread no longer notifies state
  changes); tooltips/balloons/startup balloon no longer mention any hotkey.

### Fixed — stale on/off state after exit and relaunch

The v1.1.0 persistence model wrote `Enabled` at every change point, but the
only writer besides the UI was the hotkey toggle itself — a phantom Ctrl+Shift
silently saved Enabled=0, so the next launch "showed the old configuration"
(the IME off, or a state the user never chose). With the hotkey removed,
state changes can only come from visible UI actions; on top of that:

* `saveSettings()` now also runs on the clean-exit path (`WM_DESTROY`) and
  on a real session end (`WM_ENDSESSION` with wParam TRUE) — the registry
  always mirrors the last state the user saw, never a stale value.

### Changed — version consistency v1.1.1

All release-identity strings bumped and consistent:
`kAppVersion`/`kAppTitle` (about + tray tooltip), `.rc` VERSIONINFO
(1,1,1,0 / FileVersion / ProductVersion), application manifest
assemblyIdentity 1.1.1.0, CMake `project(KieeKey VERSION 1.1.1)`, the public
`OPENKEY_KIEEKEY_VERSION_STRING` ("1.1.1"), the WinUI 3 surface (window
caption badge, status line) and every source-file release banner. Historical
inline annotations (`v1.1.0:`) and the verbatim lineage reports under
`docs/reports/` intentionally keep their original version labels.

## [1.1.0] — 2026-09-02

A broad reliability, correctness and UX release: engine edge cases, hook
modifier tracking, TSF lifetime management, shutdown hardening and a
DPI-aware settings dialog with a real macro feature.

### Consumer-layer + toggle-reliability amendments (2026-09-02, same 1.1.0 drop)

Fixes the "tone marks make my letters uppercase" / "English word replaced
or mangled after an accidental tone key" family AND the "the IME suddenly
turns off for no reason and I don't know how to open it again" family
reported by users. The core engine (TextEngine) is byte-identical
throughout — verified by re-running the full Linux test suite (`ALL
TEXTENGINE TESTS PASSED`), a ~660k-sequence brute/differential fuzz against
the vendored 2.0.5 engine (Telex + VNI, incl. backspace and mixed-case
contract sequences: no over-backspace, no committed-text drift, no
uppercase output from lowercase input), and the golden vectors.

* **Fixed — stale tracked modifiers composed the WRONG CASE into the word
  buffer (the reported "tone marks make letters uppercase" bug).** The
  hook's delta-tracked `modifierBits_` (feeding `layoutChar()` /
  `produceChar()` and therefore `TextInput::isCaps` → the engine's
  `kCapsMask`) desynchronized from the real OS state whenever Shift or
  CapsLock key-ups were missed: a UAC / secure-desktop / elevated-window
  focus period (UIPI delivers no LL-hook events), an RDP transition, or
  another application's hook swallowing modifier key-ups. While stale,
  every PASS-THROUGH letter rendered with the true (lowercase) system
  state, but the engine's word buffer carried `kCapsMask` — the next tone
  mark / đ / â transform re-emitted the letter from that buffer with the
  wrong case: typing "vo" + `j` showed "vỌ", "as" showed "Á", "chaof"
  showed "chÀO". Two fixes:
  * `ModernKeyHook::resyncModifiersFromOs()` (new) re-seeds the tracker
    from `GetAsyncKeyState`; the app calls it on every ForegroundChanged —
    the same treatment the Ctrl+Shift chord tracker received in 1.1.0 for
    the same missed-key-up class — and on the Ctrl+Shift toggle (covers
    re-enabling after a desync window while the IME was off). Hook pump
    thread only, serialized with `applyModifierDelta`.
  * `applyModifierDelta` now tracks INJECTED CapsLock/NumLock/ScrollLock
    keydowns too. An injected toggle (On-Screen Keyboard, RDP, macro
    tools) DOES flip the real system state; the previous `!ev.injected`
    guard skipped the XOR and left the tracker on the old state. KieeKey
    itself only ever injects VK_BACK and KEYEVENTF_UNICODE (wVk == 0), so
    tracking injected toggles cannot self-feed.

* **Fixed — three phantom-toggle paths fired the Ctrl+Shift hotkey without
  the user pressing it (the reported "it suddenly turns off for no reason
  and I don't know how to open it again").** All three were reproduced by
  the extreme toggle harness (`scripts/extreme_toggle_tests.cpp`, 600k
  fuzzed event segments + 9 scripted scenarios) on the pre-fix tree and
  pass on the fixed tree; the pristine 1.1.0 tree fails 3 of them:
  * *Contamination erasure hole*: the `Dirty` marker was reset by the same
    event that set it whenever the second modifier was not yet held, so
    `Shift↓ H↓ H↑ Ctrl↓ Ctrl↑ Shift↑` — typing a CAPITAL and then tapping
    Ctrl while Shift was still held — TOGGLED the IME (the everyday
    "typing normally and it turned off"), and `Ctrl↓ S↓ Shift↓ Shift↑`
    still toggled despite the earlier audit fix. The chord state machine
    now keeps contamination CUMULATIVE until both modifiers are released
    (`CtrlShiftChord.hpp`, rewritten).
  * *Injected chords*: third-party INJECTED bare Ctrl+Shift (RDP clients
    forwarding keystrokes, key remappers, automation tools) armed and
    fired the toggle. Injected modifier events are now filtered out in
    `onHookEvent` before feeding the chord (KieeKey itself injects only
    VK_BACK / KEYEVENTF_UNICODE, so a real chord can never be missed);
    injected non-modifier keydowns still cancel, so a remapped key between
    the modifiers remains "a key in between".
  * *Resync arming*: a foreground change while both modifiers were held
    re-seeded the chord as CLEAN, so releasing them toggled a chord the
    app never observed starting. `resync()` now marks an unobserved chord
    Dirty — only chords pressed AND released entirely between two resyncs
    can toggle.
* **Changed — toggle feedback (the "don't know how to open it again"
  half).** The hotkey toggle was completely silent. It now shows a tray
  balloon on every toggle ("Đã TẮT/BẬT gõ tiếng Việt — nhấn Ctrl+Shift để
  bật/tắt lại"), and the startup balloon states the ACTUAL persisted state
  (previously it claimed the IME was ready even when launching in the
  persisted OFF state) and teaches the recovery chord. The persisted
  on/off state itself is kept (OpenKey parity).

### Post-release audit amendments (2026-09-02, same 1.1.0 drop)

Full-source audit (edge cases, correctness, consistency, stability,
speed) with the complete regression battery re-run after every fix —
see [docs/reports/POST_RELEASE_AUDIT_REPORT.md](docs/reports/POST_RELEASE_AUDIT_REPORT.md).
The version is unchanged; the source drop is amended in place.

* **Fixed — macro re-expansion regression (introduced by the v1.1.0 D3
  model).** After a space-triggered ReplaceMacro, `hasHandledMacro_` was
  never re-armed (`spaceCount_` stayed 0, so the legacy "next letter
  restarts the session" trigger never fired): typing the same
  abbreviation twice produced raw letters the second time ("xl xl " →
  "xin lỗixl " instead of "xin lỗi xin lỗi ", verified against the real
  2.0.5 engine). The expansion now resets the full word state (mirroring
  the word-break tail) and the flag re-arms when the next word's first
  letter arrives; `macroExpandLen_` is dropped once new text follows the
  expansion (backspaces then delete the new text, not the committed
  expansion).
* **Fixed — `handleModernMark` rule 3.2.** v1.1.0 activated 2.0.5's dead
  `ia/ya/ua` branch; the activation forced the tone mark onto the FIRST
  vowel of every `U+A` group, diverging from both the real 2.0.5 engine
  and the clean-room oracle exactly where the dead code never fired
  ("ua" + end consonant — the quán family — and 3-vowel "uai…" groups).
  The branch is dead again, exactly as upstream shipped it; rules 4/7
  already produce every documented placement.
* **Fixed — oracle (test-side spec) mirrored** to the engine's corrected
  semantics (break-key macro state reset + fresh-letter re-arm); the
  oracle previously kept 2.0.5's `hasHandledMacro` leak, which silently
  gated the spelling-restore path for the word following a comma/enter
  macro expansion.
* **Fixed — `ProcessMonitor::start()` failure path** left the pump thread
  joinable with `running_` cleared, so `stop()` never joined and
  `~ProcessMonitor()` called `std::terminate()` (abort-at-exit whenever
  `SetWinEventHook` fails). Bounded join/detach added.
* **Fixed — `TsfComposer::textBeforeCaret()`** dropped the
  `TF_ES_ASYNCDONTCARE` retry: a deferred `DoEditSession` wrote through a
  dangling stack `HRESULT*` and a possibly-released `ITfContext*`
  (stack-use-after-return + COM use-after-free); the retry could never
  produce a usable result. Sync request only, graceful "unavailable".
* **Fixed — COM init balance in `TsfComposer`**: `ownsComInit_` tracks
  whether `CoInitializeEx` actually took a reference (S_OK/S_FALSE do;
  `RPC_E_CHANGED_MODE` does not) — failure paths no longer unbalance
  another owner's reference and `detach()` now balances its own.
* **Fixed — singleton takeover startup race**: a second launch inside the
  first instance's window (mutex held, main window not yet created) was
  misread as a zombie and terminated; the probe now retries the window
  find for 3 s before concluding.
* **Fixed — `WM_ENDSESSION` honors `wParam`** (a *canceled* end-session no
  longer stops the hook/monitor mid-session).
* **Fixed — macros.txt file write moved out of `engineMtx`** (disk I/O
  under the hook-thread lock could stall typing past the LL-hook timeout).
* **Fixed — `ModernKeyHook`**: start() failure now rolls back the full
  lifecycle; restart after a stuck-detach is refused (two consumers on
  the SPSC ring corrupt it); the consumer µs clock no longer overflows
  at ~9–21 days uptime; `reinstallHooksOnPump()` re-seeds the modifier
  tracker (a release missed during a dead hook no longer sticks "held").
* **Fixed — `CtrlShiftChord`** dirties on a non-modifier key-down while
  EITHER modifier is held (Ctrl↓, key↓, Shift↓, Shift↑ no longer toggles).
* **Fixed — TSF `applyDelta`** returns `E_FAIL` for a selection-less
  context (was `S_OK`: keystrokes silently vanished with no fallback).
* **Fixed — `Win32RAII`**: dedicated `FindHandle` with the correct
  `INVALID_HANDLE_VALUE` invalid value; move-assignments use
  `std::addressof` (the out-param `operator&` overload hijacked `&other`).
* **Fixed — app hygiene**: tray menu posts the KB135880 `WM_NULL`;
  foreground-change tooltip refresh no longer persists 13 registry values
  per app switch (new `WM_APP_UPDATE_TIP` handler); the settings font is
  cached per DPI; the macro editor is seeded from the raw macros.txt
  content (user comments survive Open→OK); `ifstream/ofstream` take
  `.c_str()` so the app compiles with the project's own MinGW toolchain
  path, not only MSVC.
* **Verified**: full regression battery + differential harness
  (`run_mega_bench.sh --fast`, 3.29 M events) ends at
  **VERDICT: PASS (0 text divergences)**; three-engine bench
  216.6 ns/key mean (p99 546 ns); Windows exe built fully static
  (MinGW-w64 x64).

### Fixed — engine correctness (src/core/TextEngine)
* **Out-of-bounds read at word start** — `checkForStandaloneChar` indexed
  `typingWord_[index_ - 1]` before the `index_ == 0` guards, i.e.
  `typingWord_[SIZE_MAX]` on an empty word (inherited from the legacy signed
  `-1` port). The reverse-to-ư path still behaves identically for every
  non-empty word.
* **Dead (impossible) conditions in `handleModernMark`** — carried verbatim
  from OpenKey 2.0.5 (`Engine.cpp:671-689`): rule 3.1 compared `chr(vS+2)`
  against two different letters for the CH/NH/NG end clusters and rule 3.2
  compared `chr(vS)` against two letters for the ia/ya/ua groups. Both were
  dead code, so modern orthography placed the mark wrongly for iêch/iênh/iêng
  endings. The conditions now compare the intended positions
  (`chr(vS+2)=='C' && chr(vS+3)=='H'`, `chr(vS)=='I' && chr(vS+1)=='A'`, …);
  the rule-4 G/Q refinements still run afterwards exactly as before.
* **Context-resync desync (click / caret-edit)** — `resumeFromText` replayed
  the visible raw word through the FULL state machine, storing Telex
  transform masks in the buffer (`as`, `dd`, `aw` …). The next word break
  then "restored" composed text that was never displayed — duplicated/ghost
  letters. Resync now replays in a dedicated raw mode (`rawReplay_`) that
  mirrors the visible letters verbatim (buffer == screen).
* **Stale VNI vowel index** — the VNI branch scans the buffer for the last
  O/A/E to map digit 6/7/8; when nothing matched, the PREVIOUS word's index
  stayed in `vowelEnd_` and digit 6 composed onto a phantom vowel. The scan
  is now validity-tracked (`vniVowelEndValid_`) and the transform is skipped
  when there is no vowel to attach to (the digit passes through raw).
* **Macro/backspace bookkeeping desync** — after a `ReplaceMacro` the engine
  incremented `spaceCount_` although the space key is consumed by the
  expansion (D3 contract: no space is on screen). The first backspace after
  any macro expansion therefore ran the wrong restore path and composed onto
  phantom raw keys. New visibility model: `spaceCount_ = 0`, pending
  bracket/space bookkeeping cleared, and the expansion's UTF-16 length is
  tracked in `macroExpandLen_` so backspaceBranch walks it down like
  ordinary text (previous-word state resumes afterwards, as with normal
  words). The word-break ReplaceMacro tail now resets the full word state
  (`stateIndex_`/`tempDisableKey_`/`longWordHelper_`), not only `index_`.

### Fixed — hook / input pipeline (src/core, src/app)
* **CapsLock/NumLock/ScrollLock tracked live** — the toggle bits were seeded
  once at `start()` and never updated, so toggling CapsLock after launching
  KieeKey composed the wrong case for every keystroke (and the layout
  resolver read stale toggle state). Toggle keys now XOR their bit on each
  non-injected KeyDown; the seed uses `GetAsyncKeyState` (the documented
  async table for non-UI threads).
* **Ctrl+Shift chord self-toggle** — the chord tracker was fed purely from
  the event stream, so a missed modifier key-up (LL-hook timeout removal,
  UIPI transition, RDP switch) left it permanently "held" and the next bare
  Ctrl tap silently switched the IME off mid-work. `CtrlShiftChord::resync()`
  re-seeds from the OS key state on every foreground change.
* **Mouse wheel now breaks the word** — `WM_MOUSEWHEEL`/`WM_MOUSEHWHEEL`
  previously fell through the mouse hook, so scrolling moved the caret
  without a word break or re-sync and the next keystroke composed onto the
  pre-scroll word.
* **AltGr composes normally** — on AltGr layouts the LL stream reports
  Ctrl+Alt for plain characters; treating that as a ctrl-combo word-broke
  every AltGr character. Ctrl+Alt now composes (UniKey parity); plain Ctrl
  or plain Alt still bypass composition.
* **Dead-key layouts flush correctly** — the dead-key path repeated the same
  key, leaving the dead state armed on the hook thread (the NEXT letter was
  then composed into an accented character by the layout resolver). The
  pending dead state is now flushed with a space (the documented clear
  sequence) and the spacing dead character is returned so '^' alone still
  types '^'.
* **Numpad digits mapped** — `VK_NUMPAD0..9` previously fell through the US
  fallback map; numpad entry now participates in VNI digit marks and macro
  capture.

### Fixed — TSF composer (src/tsf)
* **Per-keystroke COM leak** — edit-session objects were created with
  refcount 1 and never released after `RequestEditSession` returned, leaking
  one session object (+ its copied payload) per committed edit — fastest in
  browsers/Office, the Auto-TSF set. The caller's initial reference is now
  released after the synchronous request (Microsoft sample semantics) on all
  three session paths.
* **Dangling-stack read-back session** — `textBeforeCaret` requested the
  session WITHOUT `TF_ES_SYNC` while pointing it directly at the caller's
  stack `std::wstring`: a deferred `DoEditSession` wrote into a returned
  frame (stack-use-after-return) and even the benign case returned an empty
  word. The session now owns its result buffer, requests
  `TF_ES_READ | TF_ES_SYNC` (with one `TF_ES_ASYNCDONTCARE` fallback that
  degrades gracefully when the document lock is busy), and the caller copies
  the result out afterwards.
* **Slow-commit watchdog** — every synchronous commit is now timed; ≥ 100 ms
  (normal is single-digit µs) marks the foreground as starving and the app
  downgrades THAT foreground to inline SendInput until the next app switch —
  the same "hung app wedges the consumer" family the v3.5 probe guards at
  switch time, now also covered after the switch.

### Fixed — stability / shutdown
* **Static-destruction race over detached workers** — when the bounded
  shutdown had to detach a wedged hook/monitor thread, the app could still
  run static destruction over it (the detached worker touches the queue and
  handles when its blocked COM call finally returns). Both pumps now publish
  `stuckThreadsDetached()`; the app exits via `ExitProcess` on that path
  (kills every thread atomically before any teardown code runs).
* **Ordering-barrier deadline uses QPC** — the 1 ms drain-barrier budget was
  previously measured with `GetTickCount()`, which only advances at the
  clock interrupt (15.6 ms default): without a successful
  `timeBeginPeriod(1)` the LL-callback stall could silently inflate to one
  full tick. The deadline is now QPC-backed (monotonic, high-resolution
  regardless of timer policy).
* `ProcessMonitor::enableCrashRecovery` now honors the caller's
  `RegisterApplicationRestart` flags verbatim (the old code silently ORed
  `RESTART_NO_CRASH | RESTART_NO_HANG` into every call, contradicting the
  documented contract).
* The `KIEEKEY_PROFILE=1` build no longer fails with C2065
  (`t_lastBarrierNs` used before its declaration).

### Added — features / UX
* **Real macro feature ("Gõ tắt")** — the 1.0.x settings checkbox was a
  silent no-op (no resolver was ever installed). v1.1.0 ships a macro table:
  `%APPDATA%\KieeKey\macros.txt` (`abbr=expansion` per line, `#` comments,
  UTF-8/UTF-16LE with BOM detection; a commented template is written on
  first run), wired into the engine `MacroResolver` contract — type the
  abbreviation, press Space, the expansion replaces it and consumes the
  space (D3). Matching folds case; the table is parsed and swapped under the
  engine lock (race-free with the hook thread).
* **Macro editor tab** — the settings dialog gained a fourth tab ("Gõ tắt")
  with a multiline editor synced to `macros.txt` (loaded on open, saved on
  OK/Apply).
* **DPI-aware settings dialog** — every coordinate is expressed in 96-dpi
  logical pixels and scaled to the dialog's per-monitor DPI at creation (the
  manifest already opted into PerMonitorV2); `WM_DPICHANGED` follows the
  monitor and the frame is sized via `AdjustWindowRect`. Fixes the
  **"Luôn SendInput" radio clipped outside the window** (placed at x=390
  with width 200 in a 480 px window — half the label was cut off at every
  DPI) by moving the three output radios onto two rows, and the font is now
  DPI-scaled.
* **Richer diagnostics tab** — new rows for ordering-barrier timeouts, hook
  self-healing reinstalls, slow TSF commits (the watchdog) and the current
  foreground application with its live auto-exclusion state; the latency
  PEAK now resets when the dialog opens (previously a process-lifetime
  outlier dominated the display) and the WPM gauge decays to zero after
  ~2 s of silence (previously it froze on the last rate forever).
* **Persisted Vietnamese on/off state** — the Ctrl+Shift / tray toggle now
  survives restarts (`Enabled` registry value; OpenKey parity — 1.0.x
  always relaunched enabled).
* **Tray tooltip explains auto-exclusion** — when the engine is paused for
  an IDE/game/shell window, the tooltip names the app ("tạm tắt trong
  …") instead of silently doing nothing (the classic "KieeKey suddenly
  stopped" report).
* `ESC`/`Enter` handling in the settings dialog now relies solely on the
  `IsDialogMessage` routing (the duplicate `WM_KEYDOWN` path raced the
  code-table combo dropdown across common-control versions).

### Changed
* Version unified to **1.1.0** everywhere: CMake `project()`, `.rc`
  VERSIONINFO (`FILEVERSION`/`PRODUCTVERSION 1,1,0,0`, strings `1.1.0.0`),
  manifest (`1.1.0.0`), first-run balloon + window titles, singleton mutex
  (`KieeKey_1.1.0_Singleton`), wake event, `kieekey_core.hpp` version
  macros (previously still the pre-release lineage `3.3.1`), file banners,
  benchmark banner strings, README. Historical documents under
  `docs/reports/` and the 1.0.x changelog sections are intentionally left
  verbatim (lineage record).
* New v1.1.0 regression tests (engine suite): OOB word-start standalone,
  raw resync replay, macro backspace model, VNI no-vowel digit pass-through.

## [1.0.1] — 2026-08-31

### Changed
* Official logo refresh: the color "kie" keycap mark (uploaded 500×500 alpha
  PNG) is now the application icon (`KieeKeyApp.ico` — green/ON state) and
  the README hero image (`KieeKeyApp-preview.png` — a byte-identical copy of
  the uploaded artwork, SHA-256-verified); the monochrome keycap mark
  becomes the gray/OFF state icon (`KieeKeyApp-off.ico`). Both `.ico` files
  embed NATIVE 16/24/32/48/64/128/256 px frames — the previous files carried
  a single 256 px frame, which Windows downscaled to a blurry tray icon.
  The ON icon's frames are exact LANCZOS resizes of the uploaded composition
  (classic 32-bit BMP frames plus a PNG-compressed 256 px frame, the
  maximally compatible layout for Explorer/shortcuts/taskbar); the OFF icon
  artwork is alpha-trimmed and centered with a small margin so its keycaps
  fill the canvas at tray size.
* Version bumped to **1.0.1** everywhere it appears: CMake `project()`,
  `.rc` VERSIONINFO (`FILEVERSION`/`PRODUCTVERSION 1,0,1,0`, version
  strings `1.0.1.0`), first-run balloon + settings-dialog title, singleton
  mutex (`KieeKey_1.0.1_Singleton`), file banners, demo console title and
  output, README. Historical documents under `docs/reports/` and the 1.0.0
  changelog section above are intentionally left verbatim (lineage record).

### Fixed
* **Reliability — "KieeKey suddenly stops and cannot be opened again"** (all
  four root causes closed):
  * Bounded shutdown: `ModernKeyHook::stop()` and `ProcessMonitor::stop()`
    now wait at most 3 s / 2 s and **detach** a stuck worker instead of
    joining forever. Previously, if the consumer thread was wedged inside a
    synchronous TSF edit session (`RequestEditSession` `TF_ES_SYNC`
    marshals into the focused app's STA — a hung target never returns),
    exit hung the UI thread and the process survived as a zombie **holding
    the single-instance mutex**, so every relaunch reported "KieeKey đang
    chạy" until a reboot. Both pumps also arm a 1 s fallback timer so a
    lost `WM_QUIT` can never stall shutdown.
  * Second-instance takeover: a relaunch now pings the earlier instance
    (`SMTO_ABORTIFHUNG`). A healthy instance is asked (named wake event) to
    restore its tray icon and confirm with a balloon; a **hung** one is
    terminated (registry PID validated against our own image name) and the
    new process takes over the singleton — no reboot / Task Manager needed.
  * Tray-icon resurrection: `TaskbarCreated` is handled, so an Explorer
    restart no longer leaves KieeKey running invisibly ("suddenly stopped").
  * Foreground-hang gate: after an app switch, TSF output is enabled only
    after the UI thread's bounded `WM_NULL` probe (150 ms,
    `SMTO_ABORTIFHUNG`) proves the new foreground is pumping; while gated —
    or when the probe fails — edits use inline SendInput, which cannot
    wedge the pipeline. The consumer also skips TSF focus re-resolution
    while gated. Recovery is automatic when a temporarily hung app resumes.
* (carried from the post-1.0 CI hardening) MSVC duplicate-manifest link
  failure (CVT1100/LNK1123) — `/MANIFEST:NO`; deterministic
  `EditDrainBarrier` tests (exact-iteration hook injection + handshakes
  replace scheduler-dependent sleeps); Win32 `timeouts()` diagnostics
  counter now actually counts; `timeBeginPeriod(1)` makes the documented
  1 ms hook-thread wait budget real; ctest `TIMEOUT 120` hang protection.

## [1.0.0] — 2026-08-31

### Project / branding
* Fork renamed to **KieeKey**; copyright held by **coderunknow**
  (https://github.com/coderunknow). KieeKey is a modified version based on
  [OpenKey](https://github.com/tuyenvm/OpenKey) (GPL-3.0) — the full upstream
  lineage and engineering history is preserved verbatim in `docs/reports/`
  (documents written under the pre-release working name "OpenKey NextGen",
  milestone numbers v3.0–v3.4; e.g. `V331_FIX_REPORT.md` is now
  `LINEAGE_v3.3.1_FIX_REPORT.md`).
* Project version unified to **1.0.0** (CMake project, .rc VERSIONINFO,
  tray/UI strings, singleton mutex, CI artifact names).
* Every fork-authored C++ source file now carries a GPLv3 copyright header
  retaining the upstream OpenKey copyright plus the KieeKey modification
  notice and the GPL "no warranty" statement
  (`SPDX-License-Identifier: GPL-3.0-or-later`).
* Added: root `LICENSE` (verbatim GNU GPLv3), `THIRD-PARTY-NOTICES.md`,
  `.gitignore` for C++/CMake/VS source releases.
* Vendored third-party reference sources under `tests/reference/`
  (OpenKey 2.0.5, UniKey) are kept **verbatim** under their original
  licenses for differential testing.

### Engine (inherited from the refactor lineage, finalized in v1.0)
* Port of the OpenKey 2.0.5 Telex/VNI engine to modern C++ as a per-instance
  state machine (`TextEngine`) with UTF-16 output and constexpr tables.
* Carried the upstream master fix for the tone-mark insertion bug
  (`_vowelForMark`): golden test `"as" -> "á"`.
* Lock-free Vyukov SPSC/MPMC queue between the low-level hook thread and the
  consumer thread; async, non-blocking keyboard/mouse hook pipeline.
* TSF text-store composer (no synthetic backspaces) and event-driven
  foreground process monitor with auto-exclusion.
* Flat generated phonetics tables for a cache-friendly hot path
  (`tools/gen_flat_tables.py` -> `src/core/FlatTables.hpp`).
* Deep E2E latency audit: event-driven ordering barrier, parked-flag wake
  protocol, zero-alloc TSF single-edit fast path (see
  `LATENCY_AUDIT_REPORT.md`).

### Fixed
* MSVC/CI link failure — `CVTRES : fatal error CVT1100: duplicate resource.
  type:MANIFEST, name:1, language:0x0409` + cascade `LNK1123: failure during
  conversion to COFF: file invalid or corrupt` on all three matrix jobs
  (x64/ARM64/ARM64EC). Root cause: under the Visual Studio generator, MSBuild
  exe targets default to `GenerateManifest` + `EmbedManifest`
  (`/MANIFEST:EMBED`), so link.exe auto-embedded its own `RT_MANIFEST` ID 1
  next to the identical resource compiled from `KieeKeyApp.rc` — two
  application manifests in one binary. Fix: `/MANIFEST:NO` on the openkey
  MSVC link line; the .rc remains the single manifest source on every
  toolchain (MinGW/windres never auto-embeds, which is why local cross
  builds passed while MSVC CI failed). A configure-time guard now fails CMake
  if a `.manifest` file is ever listed as a target source (MSBuild would
  embed it as an Additional Manifest File a second time).

### Packaging
* CI workflow hardened: CI performs NO NuGet source registration at all
  (`nuget sources add` for a source name the runner image already registers
  fails with "The name specified has already been added to the list of
  available package sources") and NO Developer Command Prompt step — the
  CMake Visual Studio generator selects the per-platform toolset itself,
  which re-enables **ARM64EC** in the matrix via `-A ARM64EC`
  (vcvarsall.bat has no `arm64ec` argument and cannot set it up). Unit
  tests (ctest) run only on the native-arch x64 job: x64 Windows cannot
  execute ARM64/ARM64EC binaries. CI builds the dependency-free default
  configuration (`KIEEKEY_BUILD_UI=none`) so
  `find_package(Microsoft.WindowsAppSDK)` cannot break configure;
  `actions/checkout` bumped to v5.
* ARM64/ARM64EC portability + `/W4 /WX` hygiene: the spin-wait paths use
  the new shared `src/core/CpuPause.hpp` (PAUSE on x86/x64/ARM64EC, YIELD
  on ARM64/ARM) instead of unconditional `<immintrin.h>`/`<emmintrin.h>`
  includes (x86-family-only headers that hard-error C1189 on ARM64); the
  intentional cache-line padding warning is disabled (`/wd4324`);
  constant-condition test assertions use `if constexpr` (C4127); the
  foreground-HWND diagnostic stamp is explicitly truncated to its 32
  significant bits (C4244). Added an `arm64ec-release` CMake preset
  mirroring the CI matrix.
* Renamed remaining lineage identifiers for consistency:
  `OPENKEY_PROFILE` → `KIEEKEY_PROFILE`, target `openkey_winui3` →
  `kieekey_winui3`.
* Source-only release: prebuilt binaries are no longer shipped in the
  repository (build via CMake presets or the included GitHub Actions
  workflow — x64 + ARM64; `bin/README.txt` explains how to obtain
  binaries). Regenerable frozen benchmark evidence (`imebench_kit/results*`,
  `benchmark_runs/`, ≈68 MB) is not shipped; reports in `docs/reports/`
  summarize the runs and the harness reproduces them.

### Versioning note

Code comments and CMake targets may reference internal lineage milestones
(`v3.0`–`v3.4`, phase codes `S1`–`S3`, `D2`–`D4`). These denote milestones
of the engineering lineage that produced KieeKey v1.0 (the pre-release
working name was "OpenKey NextGen") — they are historical engineering
annotations, not release versions. The first public release is **v1.0**.
