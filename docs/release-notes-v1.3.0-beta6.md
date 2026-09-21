# KieeKey v1.3.0-beta6 — release notes

**Self-audit hardening release, preparing v1.3.0-rc1.**
Windows file version **1.3.0.7** · tag `v1.3.0-beta6` · GPL-3.0.

There was **no new tester report** for this release — KieeKey audited itself.
The campaign ran five committed workstreams (V1–V5) plus the full
no-regression benchmark campaign (G4). Every finding below is labelled:

* `[VERIFIED]` — reproduced by a failing probe/fuzz/test or mechanical source
  evidence **before** the fix, pinned by a test afterwards.
* `[HYPOTHESIS]` — observed in source, never reproduced; carries a concrete
  repro plan instead of a claim.

Platform honesty, as always: everything below marked *portable* was proven on
Linux by the native suite; everything marked *Win32* compiles under the zig
full-TU gate (x64 + ARM64) and/or MSVC CI but has never *run* outside a
Windows build — the tester's machine remains the judge (see the rc1 checklist).

---

## V1 — CI-gate gap closed `[VERIFIED]`

**Root cause.** `audit_layout.py --strict` (authored rectangles cannot
clip/overlap/escape their page) and `audit_controls.py` (every settings
control id that is READ is also CREATED in `WM_CREATE`) passed at beta5 only
because a human remembered to run them by hand. `run_all_tests.sh` never
invoked them, so CI could ship a clipped dialog or a missing control.
(`check_input_isolation.py` was already gated — the gap was exactly these
two.)

**Evidence.** Both scripts are now `[check]` steps of
`tests/run_all_tests.sh`, following the settings-wiring check pattern.

**Red-proof (each gate fails on a seeded violation, then restored):**
* layout: shrank the Telex radio button to 8 px wide → `AUDIT FAIL — 1 hard
  finding(s) · [DEFINITE] settings clip IDC_RADIO_TELEX at line 4106: needs
  ~28px, has 8px`;
* controls: added `IsDlgButtonChecked(g.hSettings, IDC_SEEDED_NEVER_CREATED)`
  → `FAIL — READ BUT NEVER CREATED: IDC_SEEDED_NEVER_CREATED`;
* both trees restored byte-identical afterwards (clean `git diff`).

**Test pin.** The two `[check]` steps themselves; full suite green with them.

## V2 — Web-frontend parity audit

### B5 divergent tail + Backspace hint DO render in the web path `[VERIFIED]`

**Evidence.** `tests/test_arcade_server.cpp::testDivergentTailReachesWebJson`
starts a fresh Vietnamese wasd-race over the real `ArcadeServer`, types
`aaaaa` through `/api/text` (diverges from every real passage within a few
keys), and asserts the `/api/state` JSON carries:
* the composed-buffer label `Bạn đã gõ`,
* the corrective hint `Sai — nhấn Backspace … để sửa` in frame stats,
* the divergent tail as its OWN text run in the `kBad` palette color
  (`#FF5A5AFF`) — the same styled runs the GDI renderer consumes.

The runs are built at frame level (`addComposedLine`, `Arcade.cpp`) and reach
the web through `renderListToJson` → `drawFrame` unchanged. Additionally,
`@napi-rs/canvas` rasterizes the captured frames into real PNGs
(`tests/render_web_frames.js`) — 8 games + contact sheet.

### Web HUD hid the hint — fixed `[VERIFIED]`

**Root cause.** `web/arcade.js` rendered `state.frame.status ||
state.frame.hint` while the win32 hub footer renders **hint first, status as
fallback** (`ArcadeWindow.cpp`). Typing games always set a non-empty status,
so the B5 hint never reached the web player — the feature was *delivered* but
*invisible*.

**Fix + pin.** Precedence now matches win32; pinned by
`tests/web_render_test.js` (hint wins while present, status returns when the
hint clears; the old fixture assertion was updated to set `hint: ''` for its
status-only case).

### B7 steering choice in the web player `[VERIFIED]` (user decision: implement)

**Root cause.** The per-game "Phím lái" choice (Arrows / WASD / Both, bug B7)
existed only in the win32 settings dialog; web sessions were stuck on the
engine default.

**Fix.** `/api/config` accepts `wasdSteering` (0–2, clamped — out-of-range
ignored) and echoes the applied value; `web/index.html` gained the select
(labels identical to the win32 combo) and `pushConfig()` sends + reflects it.
Steering is live-safe (`applyLiveConfigToGame` reinterprets the next key), so
no relaunch is triggered.

**Pins.** `test_arcade_server.cpp::testWasdSteeringConfig` (default echo,
live apply without relaunch, stickiness, invalid-value rejection) and
`web_render_test.js::testSteeringConfigPush` (boot push carries the select
value; changing the select pushes the new value).

### B2 GateBlocker readout — documented win32-only (user decision)

The web bridge (`arcade_serve`) is a standalone process with no keyboard
hook, no IME gate and no external-app emission; a GateBlocker readout there
would have nothing real to read. This is now documented as intentional in
these notes rather than faked in the web UI.

## V3 — Chaos Lab 3-layer audit

All **17** interactive controls of `ChaosLabWindow.cpp` audited across the
same layers B9 used for the settings dialog: created → consumed →
engine-connected → persisted.

* **L1/L2 wiring clean** — every control has a creation site and a consumer
  (own `WM_COMMAND` branch, `WM_HSCROLL` for the trackbar, or a documented
  `BM_GETCHECK` read inside another handler for the two modifier checkboxes;
  the three passive output/data controls are documented in the audit's
  exemption table).
* **`[FIXED][VERIFIED]` intensity slider truncation.** `syncChaosControls()`
  positioned the trackbar with `static_cast<LPARAM>(cfg.randomCaseIntensity *
  100.0f)` — float 0.57 is `0.5699999…`, so the slider visibly settled at
  **56** for a stored 57 %. Reproduced arithmetically (IEEE-754 float32),
  fixed with `+ 0.5f` rounding (the same rounding `ArcadeServer`'s JSON echo
  already used), so the Lab UI now agrees with the bridge.
* **`[FIXED][VERIFIED]` chaos knobs lost on restart.** Only the three
  checkboxes (`ChaosMaster`/`ChaosCase`/`ChaosGlyph`) were persisted; the
  Lab's intensity, glyph mode and granularity reset to engine defaults on
  every restart. `loadSettings`/`saveSettings` now round-trip
  `ChaosIntensityPercent`, `ChaosGlyphIntensityPercent` (the two intensities
  are independent — the web bridge exposes both), `ChaosGlyphMode`
  (clamped 0–6) and `ChaosCaseGranularity`. Defaults mirror the engine
  defaults (50 / 100 / None / ByChar). Load/save symmetry re-verified
  mechanically: 35 registry keys, zero orphans either direction.
* **Gate.** New `scripts/audit_chaos_lab.py` (wired into the suite as
  `[check] chaos lab wiring`) enforces all layers for every `kId*` control
  plus get+set presence of each persistence key. Proven red twice (a dropped
  `setDword`, a dead handler branch) and restored.

## V4 — Windows proof tooling for the beta5 residuals

The tester's machine is the only judge of B1/B2/B4/B8. beta6 gives it
something hard to send back: machine-readable lines in the diagnostics
report, filled from live Win32 state.

* **`emit-chain` (B2).** The last **32** output deliveries, each line:
  seq, timestamp, channel (`TSF`/`SendInput`), the live-effects **gate
  verdict at emit time** (pure `liveGateBlocker` model — `none`,
  `ime-disabled`, `app-excluded`, `master-off`, `non-unicode-table`), target
  PID, process name and window class, UTF-16 units delivered. Recorded at
  every delivery point: TSF batch commit, TSF-fallback SendInput, inline
  SendInput (producer sink, consumer InlineEdit, OOM degrade) — gated at
  `Level::Basic` so an `Off` profile keeps its one-relaxed-load budget.
* **`process-resolution` (B4).** PID, resolved display name, **which Win32
  step produced it** (`QueryFullProcessImageNameW` / `UWP-AUMID` /
  `fallback-label`), the error of the last failed query, elevation. The
  monitor stamps `ForegroundInfo::nameApi` at resolution time — the honest
  "pid N (lỗi X)" label is distinguishable from a resolved name.
* **`display-metrics` (B1).** Monitor DPI of the dialog, `GetDpiForSystem`,
  created-face height, `DwmIsCompositionEnabled`, per-monitor awareness
  (shcore probe), screen size — all dynamic `GetProcAddress` lookups, so the
  exe builds against older SDKs and runs on a clean machine.
* **One-click copy.** New "Sao chép báo cáo" button on the Chẩn đoán tab
  copies the full report to the clipboard as `CF_UNICODETEXT` (correct
  `GlobalAlloc`/`SetClipboardData` ownership semantics); the file export
  refreshes the evidence blocks first, so both paths carry numbers from the
  moment of the report.

**Pins.** All portable logic (ring semantics, sequence stamping,
overwrite-oldest, formatting of every gate token / API name / out-of-range
enum, report sections, reset) is unit-tested in
`tests/test_diagnostics.cpp::testTesterEvidence` (Linux, no Windows).
The Win32 plumbing is compile-gated by the zig full-TU build — **x64 AND
aarch64-windows-gnu** — 18/18 translation units. *Nothing here has run on a
real Windows machine yet; the lines above describe what the tester will see
once it does* (see rc1 checklist).

### G4/e2e re-check on GitHub Actions

The beta5 report flagged one environment-dependent e2e-shim leg (+2…+5 %
with no changed code on its path) and owed a re-check on quiet hardware.
beta6 moves the FULL campaign to CI: `tests/run_isolation_ab.sh` (the beta5
hand-run procedure as code — same harness compiled into BOTH binaries,
alternating pairs, pinned core, medians, FNV digest comparison) plus the
existing suite runner, dispatched manually (`workflow_dispatch`, input
`base_tag`) on an `ubuntu-24.04` runner with artifact upload and a job
summary. See `docs/bench/beta6/BENCHMARK_REPORT.md` for the verdict once the
campaign run completes.

## V5 — Deep sweep for NEW bugs

Reproduce-first; areas never audited before. Results:

* **Tray icon menu `[VERIFIED clean]`.** All 20 `IDM_*` items appended
  (including the popup submenus) have `case` handlers with real bodies; the
  method radio group maps `cmd - IDM_METHOD_TELEX` onto the `InputMethod`
  enum (ids 401–403 contiguous, enum order matches); checkmarks reflect live
  state; the shared toggle path persists + balloons. Mechanical cross-check:
  appended-set == case-set == defined-set.
* **Settings round-trip `[VERIFIED clean]`.** Every one of the 35 persisted
  registry values appears in BOTH `loadSettings` and `saveSettings` with
  matching encoding and clamping (mechanical diff of get/set pairs). The
  single `ArcadeFailMode` key is consistent because the dialog's apply path
  writes both fail modes identically (verified at the apply button).
* **Macros `[VERIFIED clean]`.** `loadMacros` handles UTF-8-BOM and
  UTF-16-LE-BOM files (byte-wise decode, alignment-safe) and seeds a
  commented template on first run; `writeMacrosFile` writes UTF-8 with BOM;
  the table swap stays under `engineMtx` while the disk write stays out of
  it (the v1.1.0-audit fix is intact).
* **OnlineGhost `[VERIFIED clean]`.** Local-only by architecture invariant,
  mutex-guarded, suite-green; no persistence across restarts by design
  (backend deferred).
* **Diagnostics level persistence `[VERIFIED clean]`.** File-backed
  (`loadDiagLevel`/`saveDiagLevel`), applied at startup, validated against
  `'0'..'2'` with `Basic` fallback; quick-check battery 6/6 matches its
  declared total.
* **TSF path `[VERIFIED clean by source]`.** Multi-delta batch semantics: a
  mid-batch delta failure makes the session report failure with the applied
  count, and the consumer re-emits only the UNAPPLIED suffix via SendInput —
  no duplication, no silent loss. (Windows-only; execution still owed.)
* **Rhythm regeneration `[VERIFIED clean]`.** `setPassageLanguage` /
  `setBpm` / `setNoteCount` each regenerate the chart and reset; makeGame
  applies language FIRST and the final seed last, so seeded charts are
  stable; live BPM/language changes correctly route through
  `configNeedsRelaunch`/`applyNow`.
* **ARM64/ARM64EC compile gates.** Everything touched this release compiles
  for `aarch64-windows-gnu` via the zig gate (6/6 TUs); x64 full-TU 18/18.
* **Hub keyboard navigation `[HYPOTHESIS]`.** Game control by keyboard works
  (`WM_KEYDOWN` → `handleKey`, `DLGC_WANTALLKEYS`, focus restored on
  launch); the catalog itself is mouse-only — there is no keyboard game
  selection. No claim of keyboard navigation was ever made or regressed, so
  this stays an observation with a repro plan (tab/arrow through the sidebar
  in a future build) rather than a bug.

## What is still only stub-proven / tester-owed (honest section)

* **Every Win32 behavior in V4** (emit-chain lines with real window classes,
  the clipboard button, DPI/DWM numbers) — compiled and unit-tested at the
  logic layer only; first live observation belongs to the tester's machine.
* **B2/B4/B1 residuals remain OPEN** until a tester report with the new
  lines comes back; beta6 provided the evidence path, not the evidence.
* **B8 (MessageBox path)** — unchanged this release; the beta5 fix stands.
* **The e2e bench leg** — verdict pending the GitHub Actions campaign run.
* **Web player visuals at exotic DPRIs** — rasterized PNGs exist for the
  shipped frames only.

## rc1 readiness checklist (the beta6 goal)

For `v1.3.0-rc1` to ship as a **non-prerelease**, each row must be true.
beta6's own verdict per row is stated bluntly.

| # | Requirement | Evidence that would close it | beta6 status |
|---|---|---|---|
| 1 | V1 gates in CI and red-proof | suite output + seeded proofs | **CLOSED by beta6** |
| 2 | Web parity decisions implemented/documented | tests above + these notes | **CLOSED by beta6** |
| 3 | Chaos Lab controls wired + persisted + gated | audit + suite + symmetry check | **CLOSED by beta6** (portable); registry writes themselves run only on Windows — low risk, still a tester first |
| 4 | B2: live effects transform text in external apps | tester report containing `emit-chain … gate=none channel=…` lines | **UNKNOWN until tester runs it** (tooling shipped) |
| 5 | B4: real process-name resolution | `process-resolution … api=QueryFullProcessImageNameW` (or honest fallback) in the report | **UNKNOWN until tester runs it** (tooling shipped) |
| 6 | B1: layout at exotic DPI | `display-metrics` line + visual confirmation at 125/150/175 % | **UNKNOWN until tester runs it** (tooling shipped) |
| 7 | B8: MessageBox path | tester confirmation | **UNKNOWN** (unchanged since beta5) |
| 8 | e2e leg on quiet hardware | CI campaign artifact vs beta5 flag | **PENDING the campaign run** |
| 9 | No regression beyond noise floor | `docs/bench/beta6/BENCHMARK_REPORT.md` | **PENDING the campaign run** |
| 10 | CI fully green (x64 /W4 /WX, ARM64, ARM64EC, native) | PR checks | **PENDING this PR** |

Rows 4–7 require the Windows tester; "unknown until tester runs it" is the
required, honest answer for them. Rows 8–10 close in this campaign's CI.

## Links

* PR: (filled at PR creation)
* Tag: `v1.3.0-beta6` on the merge commit
* Release: GitHub release with x64 / ARM64 / ARM64EC zips
* Benchmark report: `docs/bench/beta6/BENCHMARK_REPORT.md`
