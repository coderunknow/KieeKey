> **Lineage note (KieeKey v1.0 packaging):** Historical engineering
> document from the refactor lineage that produced KieeKey v1.0 (a fork of
> OpenKey, GPL-3.0). Written before the v1.0 release unification, it uses the
> pre-release working name "OpenKey NextGen" and internal milestone numbers
> (v3.0-v3.4). Published verbatim for traceability - see
> [docs/reports/README.md](README.md).

# Design: ghosting/sticking + accent-delay fixes + 3-engine benchmark

## Bug 1 — "ghosting character / sticking" when typing and deleting quickly

### Root cause (two mechanisms, both on the TSF output path)

**(a) The accent conversion can be re-applied to already-composed text (TSF).**

`onHookEvent` (src/app/main.cpp) is the producer on the hook thread. For an
accent key like `s` in `as`:
1. Engine decides: consumed, backspace=1, replacement=`á`.
2. Producer pushes `OutputItem{Edit, backspace=1, text="á"}` to `outRing` and
   returns `PD{suppress=true, wakeConsumer=true}`.
3. Hook enqueues a KeyEvent and SetEvents the consumer.
4. Consumer pops the edit, calls `TsfComposer::commit(1, "á")`.

Meanwhile the user has already typed the NEXT key (fast typing) — its
`KeyDown` is a *separate* hook callback. If that key's engine decision also
suppresses (another accent, a vowel, or Backspace), the producer pushes a
SECOND `OutputItem{Edit,...}` into the ring. The consumer processes the two
edits **in order** — but the two `commit()` calls each open their own TSF edit
session against the app's text store. Between them, the app has received the
raw `KeyUp` of the first key (KeyUps are NOT suppressed — `onHookEvent`
returns `PD{}` for `KeyUp`), so the application's text store is
**not synchronized** with the engine's view when the second edit session runs.

Concretely, `chà` + `f` (both suppressed):
- Edit A: `commit(1, "f")`-ish? No — `chàf` is a composition:
  engine on `f` returns backspace=2, replacement=`chaf`... the point is the
  SECOND edit session's `backspaceCount` counts UTF-16 units from the caret
  in the CURRENT document, but the document's caret is exactly where the
  FIRST edit left it (correct) — *unless* the app received an interleaved raw
  key. That interleaving is the ghost: the raw `KeyUp` of a suppressed key
  (or the app's own auto-reflow) shifts the caret, and the second edit's
  backspace deletes the WRONG characters — the composed text stays and a
  phantom copy is inserted → "ghosting character / sticking".

**(b) Backspace races the pending accent edit.**

Fast "type and delete": `as` (composes `á` via a queued edit) then immediate
`Backspace`. The producer fed `Backspace` to the engine too:
- If the engine was already past `á`, its buffer is `á` and Backspace pops it
  → `Edit{backspace=1, text=""}`.
- But if the user's Backspace reached the app BEFORE the consumer applied the
  `á` edit, the app deleted the raw `s` it had on screen; then the queued `á`
  edit lands and re-inserts `á` on top of the now-shortened text → the "sticky"
  composed char that survives the delete.

Both are the same family: **the TSF consumer applies engine edits to a
document whose state may have changed since the engine decided them, and
nothing re-syncs/re-checks.** Inline mode (SendInput) is immune because the
backspaces+text are emitted atomically in one `SendInput` call.

### Fix (clean, not hardcoded)

1. **Serialize the edit application with the app's input**: the consumer must
   not apply an edit while the app may still be processing the suppressed
   key's KeyUp or an interleaved keystroke. The hook already has
   `LLKHF_INJECTED` and a magic `dwExtraInfo` — but the robust, general fix is
   to **coalesce consecutive suppressed edits** into a single TSF edit session
   and to **verify the document before applying** a backspace-heavy edit.

2. **Coalescing**: in `onConsumerEvent`, instead of one `commit()` per ring
   item, batch consecutive `Edit` items: sum their backspaces and concatenate
   their texts into ONE `OutputItem`-level edit. This is exactly what the
   engine intends (each keystroke's edit is a delta on the engine's own
   buffer; applying them together is equivalent) and it removes the window
   where an intermediate raw KeyUp can interleave between two edit sessions.
   The batch is bounded by the ring drain loop, so it stays O(1) and can't
   starve the app.

3. **Sticky-state guard**: remember the last `Edit` the engine produced. If a
   subsequent `Edit`'s backspace would delete more UTF-16 units than the
   document actually has before the caret (read via `textBeforeCaret`), the
   document and engine have desynced → queue a `Resync` before applying
   instead of blindly backspacing. This makes the "type-delete quickly"
   ghost impossible: the engine re-syncs to the visible text instead of
   deleting into thin air.

## Bug 2 — "delay when typing accents"

### Root cause

The producer→consumer hop. Every suppressed keystroke:
1. Engine decision on the hook thread (fast).
2. `OutputItem` pushed to the ring.
3. `KeyEvent` enqueued + `SetEvent` (a kernel call).
4. Consumer thread wakes from `WaitForSingleObject` (or ends its pause-spin),
   drains, builds a `std::wstring`, calls `RequestEditSession` + COM
   `InsertTextAtSelection`.

Even with the 4096-pause spin (~6-8µs), a TSF commit is a **COM call into the
target application's text store** — the dominant latency is the TSF edit
session round-trip, which can be 100s of µs on some apps (Office, browsers),
and it happens on EVERY accent (the common case: the character that gets a
tone mark is the one that pays the hop).

### Fix

1. **Coalescing (above)** also cuts the number of TSF edit sessions: a whole
   word's suppression (`as` → `á`) becomes ONE `commit()` instead of two
   (raw `a` pass-through + suppressed `s`). Fewer edit sessions = fewer
   round-trips.

2. **Reduce the sync edit to the changed span only**: the engine already gives
   the minimal delta; the TSF `commit` fast path (backspace==0) is a single
   `InsertTextAtSelection`. Keep that.

3. **Inline fast-path for the common accent case**: when the app is not
   TSF-sensitive (the policy already chooses inline for many apps), accents
   already go through `emitInline` — one `SendInput`, zero hop. The fix is to
   make sure the **TSF path doesn't double-hop**: currently the producer
   pushes to the ring even in TSF mode; the consumer then re-reads the ring.
   With coalescing, a rapid burst of accents inside one word is one edit
   session. That is the "delay" fix.

4. **Pause-spin tune**: raise the consumer pause-spin to cover the realistic
   inter-keystroke gap of a fast typist hitting accents (the window is the
   latency between the hook decision and the consumer draining). The current
   4096 pauses ≈ 6-8µs. The accent delay is dominated by the TSF session,
   not the spin, so the spin is NOT the main lever — but the coalescing is.

## Benchmark — NextGen vs OpenKey 2.0.5 vs UniKey

- Engine sources: `tests/reference/openkey-2.0.5/engine` (vendored, untouched)
  and `tests/reference/unikey` (vendored, untouched, GPL — the authentic
  UKEngine + vnconv charset lib).
- Driver harnesses (Linux): `tests/engine205.hpp/cpp` (existing 2.0.5 wrapper)
  and `tests/uk_driver.cpp` (new UniKey driver, faithful preedit model).
- One benchmark binary `tests/bench_three_engines.cpp` feeds byte-identical
  deterministic key streams (real passages, tone-toggle stress, backspace
  storms, fuzz) to all three engines and reports:
  - **Correctness**: final text agreement (NextGen vs 2.0.5 vs UniKey),
    per-word agreement.
  - **Latency**: per-key decision time (engine-only), per-edit output time,
    and (on Windows, optional) end-to-end hook→consumer.
  - **Memory**: peak RSS per engine (engine state is small; reports steady-state).
- Build script `tests/run_three_bench.sh` (or Makefile target) compiles all
  three and prints a table + writes `THREE_ENGINE_BENCH_REPORT.md`.
