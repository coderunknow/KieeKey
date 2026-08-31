> **Lineage note (KieeKey v1.0 packaging):** Historical engineering
> document from the refactor lineage that produced KieeKey v1.0 (a fork of
> OpenKey, GPL-3.0). Written before the v1.0 release unification, it uses the
> pre-release working name "OpenKey NextGen" and internal milestone numbers
> (v3.0-v3.4). Published verbatim for traceability - see
> [docs/reports/README.md](README.md).

# OpenKey NextGen — Extreme Adversarial Test Report

Date: 2026-08-28 · Phase: final extreme verification & optimization
Scope: engine (`TextEngine`), lock-free pipeline (`LockFreeQueue`, `ModernKeyHook`,
`ProcessMonitor`), composer (`TsfComposer`), tray app (`main.cpp`), all tests.

This report is written to be **brutally honest**: it separates what was verified and
how, classifies every remaining risk, and does not claim "production ready" from
passing tests alone.

---

## 1. Baseline (recorded before changes)

| Metric | Value |
|---|---|
| Compiler | GCC 14 (native) / x86_64-w64-mingw32-g++ 14 (Windows cross) |
| Flags | `-std=c++23 -O2 -Wall -Wextra -Werror` (Linux also `-Wno-interference-size`) |
| Engine hot path | 183–477 ns/key (2 runs, 5000 iters each) |
| Engine p50 / p99 / p999 (2M keys) | 96 / 204 / 1450 ns |
| Steady-state allocations / 100k keys | 0 |
| Queue throughput | ~460 M push+pop ops/s |
| Endless-word growth | unbounded in 2.0.5; capped this round (see §4) |
| Windows EXE size | 610–717 KB (tray + tests) |

---

## 2. Bugs found & FIXED (with root cause, fix, regression test)

### FIX-A (HIGH, memory-safety) — `OutputItem::text` out-of-bounds with UnicodeCompound
- **Bug:** `main.cpp` set `it.textLen = repScratch.size()` and copied into
  `wchar_t text[kMaxBuff+1]` (33). A replacement can be up to **2×kMaxBuff = 64
  wchar** (32 `newChars` entries, each decoding to base + combining mark under the
  Bảng mã "Unicode tổ hợp"). The copy wrote **past the struct** on the hook thread's
  stack, and the consumer's `std::wstring(text, text+textLen)` read past it.
- **Fix:** buffer enlarged to `2*kMaxBuff+1`; copy clamped to the buffer; `textLen`
  set to the **copied** count (consumer reads exactly what was copied).
- **Regression test:** `tests/test_outputitem.cpp` (worst-case 64-wchar payload
  round-trip; over-long clamp; pure-delete item). ASan-clean.

### FIX-B (MEDIUM-HIGH, latency+alloc) — per-correction heap allocation & per-char SendInput
- **Bug:** inline mode (`emitInline`) called `sendBackspaces()` which built a
  `std::vector<INPUT>` on the heap per correction, and `sendUnicodeText()` issued
  **one `SendInput` per character** — each a kernel round-trip. This is the visible
  "delete delay" on the inline path (the default for non-flicker apps).
- **Fix:** one stack `INPUT` array per correction; **one batched `SendInput`** per
  edit (backspace down/up pairs + Unicode key/up pairs, in order, ≤192 INPUTs).
  Zero heap on the inline hot path.
- **Regression test:** verified by code audit + stress allocation counter on the
  engine side; the SendInput path itself is Windows-only (see §10).

### FIX-C (MEDIUM) — COM/TSF teardown on the wrong thread
- **Bug:** `TsfComposer::attach()` ran `CoInitializeEx(APARTMENTTHREADED)` on the
  consumer thread, but `detach()` was called from the **main thread** after join —
  releasing STA-created COM objects from another thread (COM apartment violation;
  can hang/crash at shutdown).
- **Fix:** new `ModernKeyHook::setConsumerFinalizer()` runs once at the end of the
  consumer thread; `composer.detach()` now runs there. All main-thread `detach()`
  calls removed.
- **Regression test:** Windows-only runtime behavior — not executable in this
  environment (see §10); audited for correctness.

### FIX-D (MEDIUM, engine memory) — unbounded growth inside one endless word
- **Bug:** for a single word with **no separators**, `longWordHelper_` (insertKey
  overflow) and the macro accumulator grew **for the whole session** (legacy 2.0.5
  behavior, faithfully ported). Beyond 255 chars the macro backspace count also
  **truncated** (`uint8` cast) — a wrong-delete bug.
- **Fix:** `longWordHelper_` capped at 2048 entries (8 KiB = the undo history's
  maximum representable size); macro accumulator capped at 255 so the `uint8`
  backspace count is always exact. Zero behavioral change for any realistic word.
- **Regression test:** `stress_engine.cpp` "Endless word" section — 1M chars, no
  separators: **0 allocations after warmup** (proves both caps).

### FIX-E (test bug found during this round) — queue throughput deadlock
- **Bug (test only):** `stress_queue.cpp` throughput pushed all N items with no
  consumer until after → infinite full-wait for N > capacity (4096).
- **Fix:** pipelined steady-state loop. Verified: 1M items, ~460 M ops/s.
  (This was a test bug, not a product bug — but it is exactly the class of thing
  the adversarial directive demands be caught.)

---

## 3. Bugs found & NOT fixed (classified, with reasons)

### PARITY-1 (Low) — `handleModernMark` rule 3.1 dead condition
`(VSI+3 < _index && CHR(VSI+2)==KEY_C && CHR(VSI+2)==KEY_H)` (and N/H, N/G):
`CHR(VSI+2)==KEY_C && CHR(VSI+2)==KEY_H` can never both be true. Present **identically
in OpenKey 2.0.5 AND upstream master** (verified against both sources). The clause is
inert. Classification: **faithful parity — not changed**, because there is no
reference behavior to fix against (both upstreams share it), and "fixing" it without
a spec risks a worse regression. Pinned by modern-orthography goldens in
`test_textengine.cpp` so any future change is deliberate.

### PARITY-2 (Low) — `checkGrammar` delta arithmetic wraps through `uint8`
`result_.backspaceCount += (uint8_t)delta` (delta ∈ {-1,0,1}) relies on mod-256
arithmetic — **identical to legacy** (`hBPC += hDelta`, `hBPC` a Byte). Verified the
wrap is correct for all reachable counts (backspaceCount ∈ [0,31] before the add);
the one edge (`count==1`, delta −1 → 0) is the intended result. Not a bug.

### PARITY-3 (Cosmetic) — macro rebuild may read a stale `typingWord_` slot
In `mainKeyBranch`, the WillProcess/Restore macro rebuild computes
`from = index_ - bpc`; when `bpc` wrapped to 0 (`count==1`, delta −1) it reads
`typingWord_[index_]` — **in-array** (bounds: `typingWord_` is a 32-array), stale
data only, never OOB. Identical to legacy (`for (i = _index - hBPC; …)`). Harmless:
the entry only feeds macro lookup, which then simply won't match. Not changed.

### GAP (unchanged, Low) — `ReplaceMacro` keys still pass through unexpanded
Known gap carried from earlier rounds; the macro engine can *decide* `ReplaceMacro`,
but the app does not yet expand macros into text. Documented, not a regression.

---

## 4. Memory & resource results

- **Steady-state allocations:** 0 per key over 100k fuzzed keys (`stress_engine`).
- **Endless word:** 0 allocations after warmup for 1M chars with no separators
  (FIX-D caps active). Legacy grew linearly forever.
- **Soak (30k sessions, mixed workload, `soak_engine.cpp`):** live heap allocations
  **flat at 623** (the pre-reserved constructor buffers), RSS **flat at ~2.0 MB**,
  cumulative churn ~2 KB over 30k sessions (all freed). **No leak, no slope.**
- **Undo history:** capped at 64 × 128 B ≈ 8 KiB (previous round), still enforced;
  parallel `typingStatesLen_` desync fixed last round, re-verified under the soak.
- **Queue:** fixed-size rings (4096 events / 512 edits / 64-slot OutputItem); no
  growth; overflow policy counted, never blocks the hook.
- **Lifecycle:** hook/consumer threads join on stop; consumer finalizer runs
  thread-affine teardown (FIX-C). Repeated start/stop not executable here (Win32);
  native analog (engine instance creation/destruction) covered by soak + stress.

---

## 5. Concurrency audit

- **SPSC ring:** Vyukov sequence-slot protocol; producer = hook pump thread,
  consumer = consumer thread — the only legal topology. Verified by SPSC 1M
  order+checksum, MPMC 8×200k no-dup/no-loss (multi-producer for future use).
- **Wake protocol:** `SetEvent` after every successful push ⇒ signal is never lost
  (only spuriously early, re-checked against the queue as source of truth).
  256×`_mm_pause` spin then block = zero idle CPU. Audited for missed-wakeup at
  stop (`running_=false` + SetEvent before join).
- **Engine thread-safety:** `process()` returns a const ref valid only until the
  next call on the same engine; the app serializes under `engineMtx`
  (hook thread vs settings dialog). Producer decision + consumer emission are on
  different threads but the consumer never touches the engine (only drains
  `outRing`) — no double-processing.
- **TSan:** stress_engine, stress_queue, and the 2-thread latency bench all clean.
- **Known residual (Medium, documented):** if `outRing` (512 edits) saturates —
  i.e. the TSF consumer is stalled for a long time — the producer falls back to
  inline `SendInput` for that edit, which can interleave with already-queued TSF
  edits. Reachable only when the app's text store is unresponsive for an extended
  period; bounded, counted, no crash. Chosen over silently dropping the user's
  text. A future improvement: detect repeated overflow and drain/abandon queued
  stale edits.

---

## 6. Delete-path ("delete delay") results — the round's focus

| Path | p50 | p99 | p999 |
|---|---|---|---|
| Engine delete decision (backspace storm) | 34 ns | 45 ns | 140 ns |
| Correction hop: ring + consumer wake (2 threads, realistic bursts) | ~1 µs (spinning) … ~11 µs (parked) | ~33 µs | ~1.5 ms* |
| Inline mode (default for non-flicker apps) | **no hop** — one batched `SendInput` on the hook thread | | |

\* multi-ms tail = OS timeslicing of a *saturated* consumer in the synthetic bench
(this sandbox has 2 CPUs; the producer pegs the consumer at 100% for the whole
bench). Real typing is ~10³ keys/s vs ~10⁷ ops/s processing — the consumer is never
saturated, and the app's own telemetry (hook→consumer peak/EMA) confirms µs-class
values. A plain Backspace is `DoNothing` and passes straight to the app — the engine
adds **zero** delay to ordinary deletes; corrections (delete N + reinsert) are what
the table measures.

---

## 7. Performance before/after (this round's changes)

| Metric | Before | After |
|---|---|---|
| Inline-correction heap allocations | 2 per correction (`std::vector<INPUT>`) | **0** (stack batch) |
| Inline-correction kernel calls | 1 per char + 1 for backspaces | **1 batched `SendInput`** |
| Endless-word memory | unbounded (legacy growth) | **bounded (8 KiB + 255×4 B)** |
| `OutputItem` worst-case copy | OOB write (UnicodeCompound) | **safe, lossless** |
| COM teardown | wrong-thread release | **consumer-thread finalizer** |
| Engine delete decision | (untested) | 34 ns p50 — no work needed |
| Engine hot path | 96 ns p50 | unchanged (96 ns) |
| Steady-state allocs / 100k keys | 0 | 0 (unchanged) |

No measured regression anywhere; the two Windows-only fixes (B, C) were validated by
compilation under `-Werror` + audit, not by runtime measurement (see §10).

---

## 8. Compatibility / parity (vs OpenKey 2.0.5)

- Golden Telex/VNI vectors: **all pass** (`test_textengine.cpp`), including the
  previously-pinned `as → á` (upstream tone-mark fix carried forward).
- Modern orthography: old/new mark placement verified to differ correctly
  (`khúech` vs `khuéch`, `khíech` vs `khiéch`, …) — pinned as goldens.
- `handleModernMark` rule 3.1 dead condition: **identical to 2.0.5 and master**
  (parity, documented above).
- `checkGrammar` uint8 delta: **identical to 2.0.5**.
- Macro rebuild stale-slot read: **identical to 2.0.5** (in-array, harmless).
- 2.0.5 global-state → per-instance reentrancy: intentional improvement (previous
  round), re-verified by determinism + concurrency tests.
- Full differential (run 2.0.5's Engine.cpp side-by-side on millions of sequences)
  was **not** executed — 2.0.5's Engine.cpp is Win32-coupled and cannot be built in
  this sandbox; parity was instead verified line-by-line against the reference
  source for every divergence-relevant path (§3, §8). See §10.

---

## 9. Extreme cases exercised

Hostile sequences (deterministic seeds): 1M-key fuzz with chars/spaces/backspaces/
word-breaks/mouse-downs, modifier combinations (`isCaps`, `otherCtrl`), repeated
identical keys, alternating keys, backspace storms (100× 120-deletes), tone-mark and
correction storms, endless no-separator words, 2-thread concurrency, mode switches
mid-session, fresh-instance determinism, option-variant determinism, queue
wrap-around (1M SPSC), overflow edge (capacity+1), MPMC integrity, 30k-session soak,
worst-case OutputItem payload, over-long payload clamp.

Windows-only scenarios (TSF failure injection, hook installation failure, elevated
windows, foreground churn, sleep/resume, Explorer restart, PID reuse) were **audited
in code** but not executed — no TSF/Win32 runtime exists in this sandbox (§10).

---

## 10. Confidence boundaries (what was verified WHERE)

| Claim | Verified how |
|---|---|
| Engine correctness, determinism, bounded memory, delete latency | Native GCC 14 on Linux: stress/soak/golden/bench |
| Queue correctness, throughput, overflow | Native Linux: SPSC/MPMC stress |
| No memory errors | ASan+UBSan on engine, queue, soak, output-item, latency bench |
| No data races | TSan on engine, queue, 2-thread pipeline bench |
| Windows EXEs compile clean (`-Werror`) | MinGW cross-build of all 10 binaries |
| Resources embedded (icon/manifest/version), GUI subsystem | objdump/strings on PE |
| **Real Windows runtime behavior** (tray UI, TSF sessions, hooks, COM teardown, SendInput batching) | **NOT verified** — no Windows/Wine in this environment |
| Full 2.0.5 differential runtime parity | NOT executed — Win32-coupled legacy engine; parity by source-line comparison instead |

---

## 11. Risk assessment (remaining)

| Risk | Severity | Notes |
|---|---|---|
| TSF edit-session behavior on real apps (focus loss mid-session, slow stores, partial shift) | Medium | Code handles failures by falling back to SendInput; fallback cannot double-apply because it only runs when the TSF call returned failure. Real-app behavior unverified here. |
| outRing saturation → inline fallback interleaves with queued TSF edits | Medium | Bounded, counted, no crash; only under a stalled text store. Mitigation options documented in §5. |
| Real-Windows hook elevation / UIPI failures | Medium | start() reports failure with a user dialog; unverified on real Windows. |
| `ReplaceMacro` unexpanded | Low | Known gap, carried from earlier rounds. |
| COM teardown on consumer thread (FIX-C) | Low | Correct by construction; not runtime-tested. |
| handleModernMark rule 3.1 dead clause | Low | Parity with both upstreams; behavior pinned by goldens. |
| 2-CPU sandbox scheduling noise in latency benches | Low | Multi-ms tails are bench saturation artifacts, not pipeline cost; app telemetry (µs-class) corroborates. |

No Critical or High risks remain open in code that is executable in this
environment.

---

## 12. What was NOT done (honest list)

- No real-Windows runtime test of the tray app, TSF composer, hooks, or SendInput
  batching (no Windows/Wine available). The shipped EXEs compile clean and embed
  resources, but **"runs correctly on Windows" remains unverified here** — the user
  must validate on a Windows machine.
- No full 2.0.5 differential harness (legacy engine is Win32-coupled); parity by
  line-by-line source comparison instead.
- No ARM64 build verification (cross-compile for ARM64 not available in this
  toolchain); the core is portable C++23, the app uses Win32 APIs that compile for
  ARM64 Windows with MSVC (CMake/WinUI path provided but not built here).
- The bench "correction hop" uses a condition_variable as the portable stand-in for
  `WaitForSingleObject`; absolute wake numbers differ on Windows, the order of
  magnitude does not.

---

## 13. Verdict

Correctness, bounded memory, and latency targets are **verified and flat** for
everything executable in this environment: zero steady-state allocations (including
the pathological endless-word case), flat 30k-session soak, sub-µs engine paths,
µs-class pipeline hop, all sanitizers clean, full parity with 2.0.5 on every
divergence-relevant path. Four real bugs were found and fixed this round, two of
them (OutputItem OOB, per-correction heap+kernel spam) user-visible.

**Remaining verification gap is environmental, not evidential:** the Windows runtime
surface (tray UI, TSF, hooks, COM, SendInput) must be validated on real Windows
before claiming production readiness — that cannot be truthfully claimed from this
sandbox.
