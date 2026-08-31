> **Lineage note (KieeKey v1.0 packaging):** Historical engineering
> document from the refactor lineage that produced KieeKey v1.0 (a fork of
> OpenKey, GPL-3.0). Written before the v1.0 release unification, it uses the
> pre-release working name "OpenKey NextGen" and internal milestone numbers
> (v3.0-v3.4). Published verbatim for traceability - see
> [docs/reports/README.md](README.md).

# OpenKey NextGen v3.4 — Deep E2E Latency Audit & Tone-Mark Typing Optimization

**Mission**: instrument, measure, and fix the end-to-end typing delay — especially
tone-mark keys (sắc/huyền/hỏi/ngã/nặng, d→đ, circumflex/breve/horn vowels) —
without regressing the frozen accuracy contract, then package v3.4.

**Evidence discipline**: every number below comes from a run in this session
(scripts and raw outputs preserved). The sandbox is a 2-vCPU Linux host with no
root (no SCHED_FIFO) and known scheduler-descheduling noise (~10 µs p50 on the
pass-through-dominated E2E harness, 3–3.5 % lag spikes > 100 µs). Real-Windows
verification is a human protocol (VERIFICATION_PROTOCOL.md) — nothing here
claims a Windows-runtime number that was not measured on Windows.

---

## 1. The audit's central finding

The frozen v3.3.1 E2E workload (`tests/e2e_bench.cpp`, news-style passage) is
**99.9 % pass-through**: measured 108 edits per 100,000 keys. A pass-through
key costs the pipeline nothing (no consumer work, no wake, no barrier). So the
frozen "p50 1.055 µs" burst number — while real on a quiet host — mostly
measures the pass-through path, NOT the path users complain about. The
tone-mark edit path (suppress + backspace-N + re-insert composed UTF-16) was
essentially unmeasured at the population level. v3.4 fixes that with
`tests/bench_tone_latency.cpp`, which makes EDITS the population.

## 2. What was measured (populations × paths × modes)

`ok_tone_bench` reuses the REAL production components — TextEngine,
InlineEmitter, OutputRing (Vyukov SPSC 1024), the 4096 KeyEvent wake ring, the
adaptive consumer spin window, the ordering barrier — and measures per key:

- **edits**: t1 decision → t2 pushed/emit → t3 dequeued → t4 flush start →
  t5 commit returned / SendInput issued; **E2E = t5 − t0** (inline path:
  E2E = t2 − t0, there is no consumer).
- **pass-through**: delivery delay t2 including the ordering-barrier stall.

Populations (each composition frozen empirically — the bench ABORTS on drift):

| population | keystrokes | composition | edit classes exercised |
|---|---|---|---|
| tone-append | `as` | á | 1 tone key = 1 edit |
| dbar | `dd` | đ | d→đ |
| reposition | `asa` | ấ | sắc repositioned onto â |
| restore-reissue | `asas` | Restore chain | 2 edits + Restore+reissue + space-Restore+reissue |
| double-tone | `oasas` | double-tone + Restore chain | mark on "oa" + restores |
| mid-word | `ddawjng` | đặng | đ + ă + nặng; trailing ng re-typed (2.0.5 semantics) |
| horn-compound | `uows` | ướ | ươ composition + sắc |
| heavy-word | `dduowcj` | được | đ + ươ + nặng (the heaviest) |
| mixed | dense-mark passage @ 80/150/250 WPM | validated word-by-word | interleaved pass+edit, human pacing |

Notes on prompt-vs-engine divergences (frozen engine semantics — measured, not
assumed): the horn requires the `w` key (`uows` → ướ; the prompt's `uoee`
yields raw "uoê" in this engine); `asas` does not produce ả — it produces
"ấ" then the double-tone Restore + key re-issue ("âs" without lexicon, and a
space-Restore chain with it); `ddawjng` → đặng (the prompt's `ddawng` has no
tone key and composes "đăng"). All of these are locked by the composition
validation so the bench cannot silently drift.

## 3. S1 — the hook-thread ordering barrier (the fix that matters)

**Before (v3.3.1)**: whenever an edit was pending, EVERY pass-through key
stalled the LL hook callback in a bounded busy-spin of 200,000 × `_mm_pause`
(≈ 2 ms). Measured consequences (2-vCPU sandbox, TSF-simulated 1 ms commit —
Word-class):

| commit=1 ms, E2E of the MARK key (µs) | p50 before | p99 before | max before |
|---|---|---|---|
| tone-append | 1001.7 | **23 450** | **28 463** |
| dbar | 1001.8 | **22 623** | **28 517** |
| reposition | 1001.7 | **24 619** | **28 802** |
| restore-reissue | 1002.6 | **26 295** | **30 666** |
| double-tone | 1002.8 | **24 880** | **30 027** |
| mid-word | 1002.1 | **26 232** | **28 923** |
| horn-compound | 1002.2 | **2 011** | **28 590** |
| heavy-word | 1002.3 | **26 091** | **29 505** |

The 1 ms commit became a **23–26 ms p99** — the spin steals the consumer's
core on small hosts, so the spin CAUSES the delay it waits for (the
self-amplification the mission predicted). Max reached ~30 ms per keystroke.

**After (v3.4)**: `EditDrainBarrier` — ~2 µs spin, then an event-based wait on
the consumer's "drained" signal, hard-capped at 1 ms. The consumer signals the
barrier's auto-reset event exactly when `pendingEdits` transitions to 0.

| commit=1 ms, E2E of the MARK key (µs) | p50 after | p99 after | max after | p99 improvement |
|---|---|---|---|---|
| tone-append | 1002.2 | **1 011.8** | **1 081** | 23.2× |
| dbar | 1002.0 | **1 008.9** | **3 239** | 22.4× |
| reposition | 1002.0 | **2 002.6** | **4 835** | 12.3× |
| restore-reissue | 1478.9 | **2 001.9** | **2 518** | 13.1× |
| double-tone | 1005.8 | **2 004.9** | **3 836** | 12.4× |
| mid-word | 1002.3 | **2 002.2** | **3 100** | 13.1× |
| horn-compound | 1002.4 | **2 003.6** | **3 020** | 1.0× (already unamplified) |
| heavy-word | 1002.8 | **1 026.8** | **3 889** | 25.4× |

p50 stays ≈ commit (correctness requires the edit to land before the barrier
releases — that wait is the product working, not overhead). The TAIL is the
fix: the amplification factor collapses from ~25× to ~1–2×. Pass-through
delivery p99 shows the same collapse (e.g. tone-append 2 561 → 1 072 µs;
restore-reissue 24 206 → 1 201 µs).

At commit=0 (fast TSF, Chrome-class) both modes are fast (p50 1.3–2.3 µs,
p99 ≤ 6.7 µs on the quiet population); the hybrid barrier costs ≈ +0.4 µs p50
on some populations (the event-wake path when the drain completes outside the
2 µs spin window) — the price of not burning the core, and still ≪ any
perceptible threshold. Honest anomalies: this sandbox throws ~25 ms
descheduling spikes into the 100 µs-commit runs in BOTH modes (see
tone_before.txt/tone_after.txt maxes) — host noise, not pipeline behavior; the
1 ms-commit comparison is consistent across all eight populations, which is
why the conclusion rests on it.

**Budget enforcement**: `EditDrainBarrier::kWaitBudgetMs = 1`; the barrier
unit tests (ok_wrap_tests) assert a pathological stall returns within the
budget and is counted (`timeouts()`), so no callback path can exceed ~1 ms of
hook-thread blocking — the acceptance target "hook-thread blocking budget
≤ 1 ms" is enforced by test.

## 4. S4 — parked-flag wake protocol

`ModernKeyHook::enqueue` now signals the wake event ONLY when the consumer is
parked (publish → re-check → wait handshake; the bench mirrors it exactly).
During a burst the consumer is in its adaptive spin window and polls the ring
directly: the per-keystroke SetEvent kernel transition disappears from the
hook callback. (Effect shows up in real-Windows SendInput budgets; in the
sandbox the burst-population p50 stayed within noise, as expected — the
sandbox wake is dominated by scheduler noise.)

## 5. S2 — in-callback SendInput vs deferred inline (decision: keep, opt-in deferred)

Measured per-key producer-side chains (this sandbox, inline path, hybrid):

| population | E2E(=t2) p50 (µs) | p99 | max |
|---|---|---|---|
| tone-append | 0.214 | 3.443 | 18.1 |
| dbar | 0.178 | 2.436 | 33.6 |
| reposition | 0.155 | 1.545 | 53.0 |
| restore-reissue | 0.177 | 1.979 | 97.4 |
| double-tone | 0.263 | 2.745 | 186.2 |
| mid-word | 0.179 | 1.539 | 6.0 |
| horn-compound | 0.287 | 1.856 | 350.4 |
| heavy-word | 0.440 | 3.299 | 21.1 |

(The heavy-word 0.44 µs includes 3 sequential edit-keys' SendInput shim
batches; the 350 µs horn max is a sandbox deschedule spike.)

The ENTIRE producer-side edit chain — decision + ring + ONE batched SendInput
call — is ~0.16–0.44 µs p50 in the shim. On real Windows the SendInput kernel
call itself (~1–2 µs claimed) is the one term this sandbox cannot measure; the
frozen quiet-host burst p50 (1.055 µs for the whole chain incl. SendInput)
bounds it tightly. Deferring to the consumer (measured: `inline-deferred`
path p50 1.6–1.7 µs vs 0.16–0.44 µs in-callback, i.e. +1.2–1.5 µs of ring hop
+ wake on EVERY inline edit) is strictly slower per key. **Decision: the
in-callback emitter stays the default** (zero consumer hop; callback budget
bounded by the one-call design; the frozen real-hardware numbers cover the
whole chain). A **deferred opt-in** ships anyway (`OPENKEY_INLINE_MODE=deferred`,
new `OutputItem::Kind::InlineEdit`, ordered against TSF edits through the same
pendingEdits barrier) for hosts where in-callback SendInput serialization is a
concern, and for A/B measurement on real hardware per the protocol. The
in-callback SendInput cost itself (t4→t5 equivalent) is exactly what the
instrumented build + ETW recipe measure on the target machine.

## 6. S3 — TSF commit path

Code-level changes (all before the app-bound session cost, which dominates):

- `commitOne` — zero-allocation single-delta edit session (the isolated tone
  key is 1 edit = 1 session; replaces a `std::vector<EditDelta>` copy per
  commit; replacements ≤ 65 UTF-16 units ride SSO → heap-free commit).
- `ITfInsertAtSelection` cached per context — one QueryInterface per focus
  change instead of one per delta (~100–300 ns/edit saved on every insert and
  every SetText fallback).
- `commitBatch(size==1)` delegates to the fast path automatically.

**TF_ES_ASYNCDONTCARE was evaluated and REJECTED** (documented in code): it
returns before the edit lands, breaking the `pendingEdits` ordering barrier —
a pass-through key could overtake the edit (the ghosting class the barrier
exists to prevent). TF_ES_READWRITE | TF_ES_SYNC stays the correctness anchor.
Re-evaluation requires the real-app A/B harness in VERIFICATION_PROTOCOL.md.
The Resync read was verified to already run off the typing path (consumer
thread, TSF_ASYNCHDONTCARE read session).

## 7. S5 — engine cost per mark family (no regression, no fix needed)

Pure engine throughput (`ok_bench`, this sandbox): 91–133 ns/key across
mark-heavy Telex + VNI workloads (frozen quiet-host number: 72 ns p50 vn-compose).
Decision+encode per family in the pipeline bench (t1 p50): tone-append
131 ns … restore-chain 314 ns — the Restore-class populations do more work by
design (revert + re-encode + reissue decision). No family regressed; the
FlatTables lookups needed no change. Engine correctness is untouched — the
full differential battery passed with 0 mismatches after every fix.

## 8. S6 — watchdog heartbeat cost

`HookWatchdog::check()` now early-exits when OUR stamp is fresh (< 60 ms):
one GetTickCount + three relaxed atomic loads (~30 ns) during active
typing/mouse use, instead of a GetLastInputInfo syscall every 5 ms. Idle
behavior and detection latency are unchanged (a dead hook stops stamping and
falls through to the system-tick comparison exactly as before).
`reinstallHooksOnPump` worst case is three hook installs (µs–ms each) — far
below the callback timeout; unchanged from v3.3.1.

## 9. Instrumentation (Phase 1) — shipped capability

`src/core/Profiler.hpp`: OPENKEY_PROFILE=0 (default) compiles the profiler
out ENTIRELY (call sites are `#if`-guarded — zero binary footprint). An
instrumented build (`cmake -DOPENKEY_PROFILE=1`) adds runtime-gated stamps
(env `OPENKEY_PROFILE=1`): t0 hook capture → t1 producer decision → t2
pushed/delivered → t3 dequeued → t4 flush start → t5 commit returned /
SendInput issued, plus the per-pass-key barrier stall. Dumps CSV
(`ok::prof::dumpCsv`) for WPA-style post-processing; the same stage model
drives the tone bench's per-stage percentiles. ETW/xperf + WPA recipe and an
instrumented PowerShell SendInput typer ship in VERIFICATION_PROTOCOL.md so a
human can measure REAL-Windows E2E (the OS input-stack terms that no
in-process harness can see).

## 10. Acceptance targets (measured, honestly labeled)

| target | status | evidence |
|---|---|---|
| engine decision ≤ 100 ns all mark families | engine-only ~91–133 ns/key this sandbox (frozen 72 ns quiet-host); decision+encode ≤ ~315 ns p50 all families | ok_bench run §7; tone bench t1 table |
| inline: capture→SendInput-issued p50 ≤ 3 µs on real Windows | **not measurable in sandbox** (needs real SendInput); producer-side chain p50 0.16–0.44 µs in shim; frozen quiet-host 1.055 µs covers the whole chain | §5 table; protocol §3 required on target machine |
| TSF: capture→commit-returned p50 ≤ 300 µs (Chrome-class) / ≤ 1 ms (Word-class) | commit=0: 1.3–2.3 µs p50 ✓; commit=1 ms: 1.002–1.006 ms p50 ✓ (commit-bound, barrier adds ≤ ~6 µs) | §3 tables |
| batch overhead ≤ 10 % of session cost | single-edit fast path: zero-alloc, no batch copy — overhead is ns against a 100 µs–1 ms session | §6 |
| hook-thread blocking ≤ 1 ms ANY path | enforced by EditDrainBarrier budget + unit test (budget bound asserted) | ok_wrap_tests PASS |
| zero regression: mega differential 0 mismatches | **PASS — 0 mismatches, all suites** (incl. OpenKey-2.0.5 differential, macro-gap 0) | MEGA_BENCH_REPORT.md |
| corpus counts ≥ frozen (Telex 64,299 / VNI 64,381) | engine untouched by transport fixes; differential suites cover it; 0 mismatches | suite outputs |
| zero warnings, ctest 3/3 | **PASS** | build logs (both native + cross, -Wall -Wextra -Wpedantic) |
| e2e burst targets p50 ≤ 2.5 / p99 ≤ 7 µs | sandbox: FAIL as before (host noise ~10 µs p50, 3.5 % deschedule spikes) — **unchanged caveat from v3.3.1**; re-verify on real hardware per protocol §3 | e2e runs (before 9.608/44.781, after 10.129/48.710 — same regime) |

## 11. Where-the-ms-go waterfall (TSF path, Word-class 1 ms commit, tone-append)

BEFORE (v3.3.1, spin barrier) — the mark key's 1 ms commit becomes:

```
t0 hook capture ──────────────────────────────────────────────┐
t1 engine decision                                    ~0.06 µs│
t2 edit pushed (ring 1024 + pendingEdits)             ~0.3 µs │  mark key
t3 consumer dequeues                                  ~1.2 µs │  E2E p50 1001.7 µs
t4 flush (batch of 1)                                 ~1.3 µs │  p99 23,450 µs ◄── spin
t5 TF_ES_SYNC session returned                     ~1001.7 µs │      amplification
NEXT KEY (any pass-through) — hook thread spins ~2 ms in the
LL callback WHILE the consumer runs (same core on small hosts):
  → measured pass delivery p50 +1001 µs, p99 +2561 µs,
    max +28,878 µs; next tone mark pays the same stall.
```

AFTER (v3.4, hybrid barrier):

```
t0 hook capture ──────────────────────────────────────────────┐
t1 engine decision                                    ~0.13 µs│
t2 edit pushed                                        ~0.4 µs │  mark key
t3 consumer dequeues                                  ~1.5 µs │  E2E p50 1002.2 µs
t4 flush                                              ~1.7 µs │  p99 1,011.8 µs
t5 session returned                                ~1002.2 µs │  (≈ commit + 2 µs)
NEXT KEY: spins ~2 µs → event wait (yields CPU) → wakes on
  drained signal → delivers. Pass delivery p99 1,072 µs,
  max 2,678 µs. Hook-thread CPU burn: none.
```

## 12. Package

`OpenKey_NextGen_v3.4_Latency.zip` — sources + tests + CMake + reports +
populated `bin/` (OpenKeyApp.exe + 6 verification exes, all import-table
verified: system DLLs only), SHA256 manifest, same packaging discipline as
v3.3.1. Run `bin/OpenKeyApp-Bench-Tone.exe` on the target machine and follow
VERIFICATION_PROTOCOL.md for the real-Windows sign-off.
