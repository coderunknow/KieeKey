# v1.3.0-beta2 — Windows x64 test handoff

This is a stabilization candidate, not a claim that every IME bug is fixed.
Merge is intentionally left to the maintainer after testing the Windows binary.

## Build identity and installation

- UI version: **1.3.0-beta2**; Windows file/manifest version: **1.3.0.2**.
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

**Intentional beta2 safety change:** Chaos does not transform ordinary IME
replacement deltas. Use Lab preview and its explicit send buttons. Games only
receive input on their focused surface; background native games do not advance.

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

The new source-contract gate rejects game/Chaos dependencies in the global
producer and missing tab ownership for optional settings controls. It fails on
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
