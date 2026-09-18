# v1.3.0-beta2 — Windows x64 test handoff

This is a stabilization candidate, not a claim that every IME bug is fixed.
Merge is intentionally left to the maintainer after testing the Windows binary.

## Build identity and installation

- UI version: **1.3.0-beta2**; Windows file/manifest version: **1.3.0.3**.
- Use `KieeKey-x64.zip` from the PR's **build → x64** artifact. Extract it and
  run `KieeKeyApp.exe`. The CI build uses the Win32 UI and a static runtime.
- Exit the stable/beta1 process first. Do not test with UniKey/OpenKey or another
  Vietnamese IME simultaneously. Keep stable v1.2.2 available for comparison.
- Do not disable antivirus or run elevated merely to bypass a warning. This is
  an unsigned development build. Only trust the artifact associated with the PR.
- Settings are shared with previous releases; record your current input method,
  digit policy, output mode and exclusions before changing them.

## Manual acceptance checklist (not yet verified in this Linux workspace)

Use Windows 10/11 x64; record the exact OS version, keyboard layout and app
version. Test in Notepad first, then the browser/editor where keys disappeared.
Use the same options for stable and beta2 comparisons.

### Normal typing / lost keys

- [ ] Type fast and slowly; verify spaces, punctuation, numbers and Shift/CapsLock.
- [ ] Telex: `tieengs Vieetj` → `tiếng Việt`, `ddawng` → `đăng`,
  `Tooi ddang gox tieengs Vieetj.` → `Tôi đang gõ tiếng Việt.` (standard Telex,
  spelling enabled). Try Backspace/retype around each vowel and tone.
- [ ] VNI, with **digits are literal OFF**: `tie6ng1 Vie6t5` → `tiếng Việt`.
  With that option ON, digits must remain literal; do not treat this as a bug.
- [ ] Check English restoration, your saved macros, F9 tone-style switching,
  cursor movement, selected-text replacement and repeated Backspace.
- [ ] Repeat using Auto output and SendInput output. Note per-app exclusions;
  an excluded editor intentionally receives unmodified keys.
- [ ] Start each Arcade game, **Alt-Tab to Notepad**, and type a paragraph.
  No game may consume those keys, even when paused or finished.
- [ ] Close/reopen settings and Lab mid-composition, return to the editor and
  type a new word. No stale word should rewrite unrelated text.
- [ ] Toggle IME off/on, switch keyboard layout, and test Ctrl/Alt/Win shortcuts.

### Desktop UI

- [ ] Visit all nine settings tabs. Arcade controls and labels must not overlap
  other pages; return to Arcade and confirm the controls are still present.
- [ ] At 100%, 125%, 150% and 200% display scale, click each Arcade catalog row;
  hover and selected game must match the visible row. Check window fit/clipping.
- [ ] Alt+F4 closes the Hub normally; Ctrl+F1 does not pause a game; bare F1 does.
- [ ] Enable Chaos/AI, close and reopen settings/Lab; checkboxes reflect the
  running configuration. Optional feature settings are session-scoped unless
  explicitly persisted by that module; this release does not add persistence.
- [ ] Opt in to AI, type in another app, reopen settings. Samples should be trained
  on the UI thread. Coach text should display Vietnamese, not UTF-8 mojibake.

### Chaos / Flexing safety

**Revised beta2 (file build 3):** Lab and live output are separate opt-ins.
Live effects: tray → **Hiệu ứng gõ bên ngoài** → **Phòng Chaos** → **Hiệu ứng
gõ trực tiếp**. Turn on its checkbox, select random casing/glyph/intensity,
then return to an external editor. Requires IME ON and Unicode; excluded apps
still bypass. Defaults OFF, session-only, with **Ctrl+Alt+F12** emergency off.
Do not enable while entering passwords or sensitive text. Windows password-field
detection is not implemented; this is not a privacy filter.

- [ ] With Lab closed, test plain letters, `tieengs vieetj`, VNI tones, repeated
  Backspace and English restore at 25/50/100% intensity. Case choices should not
  flicker when the engine replaces a vowel to add a tone.
- [ ] Enable per-glyph upside-down/mirror/random and confirm text changes immediately.
  Not all Vietnamese glyphs have lookalikes; unsupported glyphs stay unchanged.
  This preserves character order; it does not reverse sentences or rotate a whole
  document. 90°/270° geometric rotation is not supported in external plain text.
- [ ] Ctrl+Alt+F12 stops effects without disabling Vietnamese input. Turning the
  IME OFF also stops live effects. Reopening settings reflects the live switch.
- [ ] Switch apps, click/move the caret and use shortcuts. A context boundary
  starts a new composition: styled Unicode is not reverse-replayed into the
  engine. Editing an old styled word therefore starts afresh, rather than
  promising reversible transformations of arbitrary document contents.
- [ ] All four passage games remain readable as colored runs change on each key;
  test 100/125/150/200% scale and a long passage near its end.
- [ ] Wrong keys visibly highlight the current slot; Typing Race Backspace works;
  No-Mistake navigation keys do not count as mistakes; Fishing restarts with F2.
- [ ] Pause/finish messages have an opaque panel, not text over the Hub title.

- [ ] Open Lab from an external editor via the tray. Type Vietnamese and emoji;
  the preview preserves valid Unicode. Adjust intensity: the real slider works.
- [ ] Open Lab again while already focused; it must retain its external target.
- [ ] Load a short Flexing passage. A normal edit notification must advance once,
  not twice. Changing granularity keeps the cursor rather than restarting it.
- [ ] Finish a passage and load another, including an empty passage. No old text
  or completed state should survive the explicit reload.
- [ ] AutoStream runs at 15 characters/second independent of display refresh.
- [ ] Send explicitly to a disposable document. Chunked send stays responsive;
  switch focus between chunks and confirm the remaining send is cancelled.
- [ ] A denied activation, closed target or KieeKey-owned target must not receive
  text. Windows UIPI can still reject injection into an elevated application;
  this build does not bypass that protection or guarantee cross-integrity input.
- [ ] Close Lab with Flexing active; no invisible Lab-owned run remains.

### Web player

- [ ] Start the packaged bridge with `run_web_bridge.ps1` and open the shown URL.
- [ ] Keys in sliders, selects and other controls stay native. Canvas/game input
  works after clicking the game. Ctrl/Alt/Win chords are not game input.
- [ ] While Flexing streams, edit its prepared passage or the Chaos sample;
  incoming frames must not steal focus. Closing Labs keeps them closed until
  explicitly reopened or a different game transitions into Flexing.

## Automated regression coverage

Run the full portable gate (not `--quick`):

```sh
bash tests/run_all_tests.sh --jobs=4
python3 scripts/check_version.py
python3 scripts/check_input_isolation.py
bash scripts/gen_sha256sums.sh --check
```

The shell gate includes Telex/VNI golden vectors, differential/correctness and
option matrices, pipeline stress/soak, ASan/UBSan hotfix tests, feature tests,
Win32 window procedures executed against a recording shim, and Node web tests.
The optional canvas pixel render is skipped if `@napi-rs/canvas` is unavailable.
The UI shim is not Windows and cannot verify real focus delivery, TSF, UIPI,
keyboard drivers, display scaling or third-party editors.

The source-contract gate rejects game/Lab singleton dependencies in the global
producer (only the explicit bounded LiveEffects adapter is allowed) and missing tab ownership for optional settings controls. It fails on
the pre-fix beta1 sources; it is a structural guard, not an OS input simulation.

Linux CMake now also builds/registers the Arcade renderer/server/window tests;
these targets were missing or incompletely linked before beta2. GitHub Actions
runs the full portable suite plus the existing Windows x64 tests and builds for
x64, ARM64 and ARM64EC. Only the x64 artifact is requested for manual testing.
The optional WinUI 3 frontend is **not** built by this workflow.

## Reporting a failure

Record: exact binary version/PR commit, Windows/app version, Telex/VNI and options,
output mode, keyboard layout, active optional features, raw keys, expected text,
actual text, and whether the same sequence fails on stable v1.2.2. Prefer a short
screen recording using disposable text (no passwords or private documents).

## Follow-up regression evidence

- `test_live_effects`: all four live glyph modes × four intensities, Telex/VNI
  per-stroke reference comparisons, Backspace/restore, a 25,000-event mixed
  campaign, surrogate preservation and concurrent atomic config publication.
- `test_arcade_window`: real window procedures against the recording shim,
  including 4 passage games × 4 DPIs × 90 evolving frames (1,440 frames), asserting
  clipped cell bounds and consistent font/grid scale. The recorder's handle-tag
  collision was fixed so it actually tracks selected font metrics correctly.
- `web_render_test`: explicit cell placement/width, including supplementary
  Unicode, plus refreshed engine-generated fixtures in `tests/data/web_frames.json`.
- Web Canvas2D screenshots in `docs/bench/beta2-followup/` are rasterized by the
  actual `web/arcade.js` from C++ frames. They are NOT native Windows screenshots.
