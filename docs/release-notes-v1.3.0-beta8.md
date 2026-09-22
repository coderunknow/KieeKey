# KieeKey v1.3.0-beta8 — release notes

**Release date:** 2026-09-22
**Version carriers:** `1.3.0-beta8` (PE **1.3.0.9**, manifest 1.3.0.9)
**Baseline:** v1.3.0-beta7 (`e9e5009`, PE 1.3.0.8)
**Full evidence:** `BUG_HUNT_REPORT_beta8_layout_and_features.md`

Beta8 fixes **every item in the beta7 tester report** — the layout complaints
("chữ bị che / không kéo"), the Chẩn đoán tab that could not show its own report,
the Chaos Lab at 125/150 % DPI, and a silent **data-loss defect in a shipped
v1.3.0 feature**. No engine, hook or TSF pipeline behaviour changed: every edit is
UI, UX, presentation, persistence or feature wiring.

---

## 1. What you will notice

### Text can no longer be clipped or stranded — `[VERIFIED]`
* **The scroll fallback works.** The scroll model used two different units and a
  range that was one pixel too large, so a control below a tab's viewport was
  painted but unreachable. One unit now: content below the frame is reachable
  again, and the scrollbar appears exactly when content really overflows.
* **One-line values ellipsize instead of cutting words off.** Version, engine
  status, live gate, every diagnostics result line: the tail is replaced by "…"
  and the full text is available as a tooltip.
* **Rows whose text grows now grow with it.** Arcade status, AI stats and the
  coach advice line are re-measured with the real font (`DrawTextW DT_CALCRECT`)
  when their value changes and their box grows — grow-only, capped, and never at
  the cost of your scroll position.
* **The worst-case notes are pinned.** The long notes that make tab 0 scroll
  (`IDC_STAT_OUT_NOTE` and three siblings) are checked at 100/125/150 % on every
  commit, so a future string edit cannot bring the clipping back.

### Labels really do grow to fit their text now — `[VERIFIED]`
The dialog measures every label with your real font and grows the box when the
text needs more room. That half of the layout model had **never actually run**:
the code asked Windows for each control's class and compared it against
`"STATIC"`/`"BUTTON"`, but Windows answers `"Static"`/`"Button"` — so no label was
ever marked growable and no group box was ever stretched (the authored boxes were
just big enough to hide it, except for two labels that clipped by 6–7 px). Found
by the new UI probe printing the app's own measurement (`needs 102px … in a 96px
box`); fixed with a case-insensitive comparison, plus a new audit rule
(`class_case`) that fails the build if anyone writes that comparison again.

### The bottom button row no longer jumps to the left edge — `[VERIFIED]` (model), `UNVERIFIED (Windows)` (pixels)
Whenever the dialog had to grow to fit its content, the OK / Hủy / Áp dụng
buttons and the big ON/OFF switch were moved with a `SetWindowPos` call that
passed **x = 0** (and `SWP_NOSIZE` does not suppress a move — only
`SWP_NOMOVE` does). All four ended up stacked on the left edge and unusable.
The new Windows UI probe caught it on its first run; the decision now lives in
the tested portable model (`ok::layout::bottomRowMove()`), so the row can only
ever move down, keeping its X and the authored gaps. This was a **beta7 defect,
not a beta8 regression** (present in `git show e9e5009:src/app/main.cpp:4412`).

### Nothing hides under the tab strip any more — `[VERIFIED]` (model), `UNVERIFIED (Windows)` (pixels)
The nine tab labels wrap to a second row when the window is narrow or the font is
larger. The page underneath was solved for a *single* row, so the top of every
tab (a group box's title, sometimes a whole label) was painted **under the tab
labels** — the "chữ bị che" you reported. A page now shifts down so its first
control starts below the tab strip, and only when it has to (already-solved pages
do not move). Found by the new Windows UI probe, fixed in the tested portable
model (`ok::layout::pageTopShiftPx()`).

### Combo boxes no longer hide their neighbours — `[VERIFIED]`
A `CBS_DROPDOWNLIST` is created with the height of its *drop-down list*, so twelve
combos were silently covering up to 175 px of the tab below the 25 px you see.
Every combo is now created **closed** and asks for its list height afterwards
(`CB_SETMINVISIBLE`), which removes that invisible-window class entirely. The
audit fails if a tall combo is ever authored again.

### Phòng Chaos theo đúng DPI / the Chaos Lab follows your monitor — `[VERIFIED]` (source contract), `UNVERIFIED (Windows)` (pixels)
The Lab is a fixed 96-DPI design with no solver, and beta7 rescaled only the
font — at 125/150 % the glyphs grew inside unchanged boxes ("chữ bị đè"). Children
are now re-laid out from the authored table on `WM_DPICHANGED` and the window is
re-fitted to the scaled design (never shrunk below a size you chose).

### Arcade Hub footer — `[VERIFIED]` (geometry), `UNVERIFIED (Windows)` (pixels)
The hint band and the FPS counter band now come from one function, so a long hint
(the Backspace-recovery message) can never run under the counter, and the counter
scales with the DPI instead of using fixed pixels.

### Đọc được báo cáo Chẩn đoán ngay trong app — `[VERIFIED]`
* A read-only, scrollable report pane inside tab **Chẩn đoán**, filled by the
  **same builder the file export uses** — pane bytes == exported bytes, so the
  screen and the file can never disagree.
* **Xem báo cáo** refreshes the pane, **Mở file** opens the last export
  (`ShellExecuteW`), and the buttons report what they did.
* The quick check no longer prints a raw machine token: you get a Vietnamese item
  list (`lõi gõ: lỗi round-trip`, `backspace biên từ: lỗi`, …) with the raw token
  kept in the report file.
* The beta7 truth markers (`emit-chain`, `process-resolution`, `display-metrics`,
  DPI/uptime provenance) are all still there.

### XP/cấp độ/thành tựu và hồ sơ AI **không còn mất khi thoát** — `[VERIFIED]`
> This is the one I would call a "must take": in beta7 the v1.3.0 progression
> feature was effectively per-session.

* Saved to `%APPDATA%\KieeKey\progression.dat` and `aiprofile.dat` on both exit
  paths (`WM_ENDSESSION`, clean destroy) **and every 30 seconds** from the UI tick
  (skipped when nothing changed), so a crash costs at most 30 seconds.
* Loaded at boot, fail-open: a corrupt file is reported once and you start with a
  fresh profile instead of a broken startup.
* The AI profile is written **only while you are opted in** — turning the AI off
  stops it being persisted.

### "Gõ chữ Flexing ra app" now tells you why nothing happened — `UNVERIFIED (Windows)`
Beta7 returned a bare `0` for every outcome, so three very different causes looked
like success. The Lab now shows the exact reason in a status line, and a refused
activation (Windows only lets the foreground window hand focus over) raises a
one-time dialog that explains the fix. Fail-closed behaviour is unchanged: the
emitter never types into a window that is not the one you asked for, and a focus
change mid-injection stops the run instead of typing into the wrong document.

### F9 and the Live-Effects gate — `[VERIFIED]`
F9 (tone-style toggle) no longer steals the key from a running arcade game — the
guard rides on an atomic mirror published by the UI timer, because the hook thread
is architecturally forbidden from calling the arcade singletons
(`scripts/check_input_isolation.py` enforces it). The gate row ("Hiệu ứng: …") is
on the fit path so the longest blocker sentence is always fully readable, and the
"+1,8 ns/ký tự" note is now pinned to the figure in `docs/PERFORMANCE.md`.

### Web lab parity closed — `[VERIFIED]`
The web Flexing lab's **N** input was suspected of being decoration. It is not: it
travels as `nChars` to `FlexingGame::setGranularity(gran, nChars)` — the same
engine call the desktop granularity combo makes. It is kept, and the tests now
assert it is live and that its bounds match the engine clamp.

### New CI capability: a Windows UI probe — `UNVERIFIED (Windows)` (never executed yet)
An opt-in target (`kieekey_ui_probe`, x64 CI only) opens the **real** settings
dialog through the app's own creation path and checks all nine tabs with real font
metrics: no overlap, nothing outside its tab page, every label fits, the scrollbar
is enabled exactly when content overflows, every interactive control's centre
hit-tests back to itself, every tab header fits. It writes `ui_probe.json` plus one
PNG per tab as CI artifacts. It does not replace your eyes at 150 %: it reports the
DPI it really measured, and the 125/150 % arithmetic stays with the local audit and
the manual checklist below.

---

## 2. Evidence summary

| Layer | Result |
|---|---|
| Local gate `tests/run_all_tests.sh --quick --jobs=2` | **ALL GREEN** — 14 checks (incl. 5 audit gates), ~50 native targets, 4 node suites, SHA256SUMS |
| New portable suites | `test_arcade_chrome_layout` (177 checks), `test_diag_report_text` (7), `test_progression_persist` (29), `test_flex_send_outcome` (94 checks), `test_dialog_layout` extended (BS-01 + BS-09) |
| Seed-verified audits | `audit_layout` 9 seeds, `audit_chaos_lab` 4 layers, `audit_feature_persistence` (RED 11 on the pre-fix tree), `audit_live_effects_truth` (RED 4) |
| Cross-compile | every Windows-only edit compiles with `zig c++ -target x86_64-windows-gnu -Wall -Wextra`; a deliberately broken copy fails as expected |
| `check_version.py` | OK — `1.3.0-beta8` (PE 1.3.0.9, manifest 1.3.0.9) |
| Windows CI (x64/ARM64/ARM64EC `/W4 /WX` + ctest + UI probe) | **GREEN** — 3 platforms built, 8/8 ctest, UI probe **0 findings over 184 controls / 1764 checks** at DPI 96 |
| Manual checklist (below) | **you** |

---

## 3. Manual Windows checklist (please tick each item)

Nothing Windows-only is claimed as verified on Linux or CI evidence alone. Ten
minutes with this list closes the release.

### W1–W7 — the beta7 checklist, re-run on beta8
1. **W1** Open Cài đặt on a **100 %** monitor → every tab shows full text, no
   clipped label, no overlap.
2. **W2** Repeat at **125 %**, then **150 %** (Bàn phím, Chẩn đoán, Thông tin,
   Chaos, AI — the five named tabs).
3. **W3** Open and close each combo box; the list drops fully on screen and nothing
   below it becomes unclickable (BS-05).
4. **W4** Type a long word with a wrong accent so the coach line and the engine
   status line get long → both stay fully readable.
5. **W5** Arcade Hub: play a game and press **F9** — the game gets the key, the
   tone style does **not** change; with no game running F9 still toggles (FT-05).
6. **W6** Chaos Lab at **100 %**, then drag it to a 150 % monitor: font **and**
   boxes both scale, nothing overlaps (BS-06).
7. **W7** Tray icon, menu, toggle button, tooltip all still correct.

### M1–M7 — the beta8 additions
1. **M1** At 100/125/150 %, on the five named tabs: no clipped text, and every
   control you can see is clickable. *(CI probe covers the runner's own DPI.)*
2. **M2** Every combo drop-down opens fully, on every tab, at every scale; the
   control directly under it still responds to clicks afterwards.
3. **M3** Chẩn đoán: **Kiểm tra nhanh** → read the Vietnamese item list →
   **Xem báo cáo** (pane fills) → **Xuất báo cáo** → **Mở file** (Notepad opens the
   same bytes; the pane and the file must match).
4. **M4** Flexing: type a few keys, press **Gõ chữ Flexing ra app** with (a) a
   Notepad window focused before opening the Lab — text appears; (b) no external
   app — the status line says so; (c) an elevated app — the one-time dialog
   explains the elevation rule.
5. **M5** Live Effects: untick "Bật khi gõ bên ngoài", then re-open the tab — the
   gate row says exactly which condition is missing; Ctrl+Alt+F12 turns the effects
   off immediately.
6. **M6** Earn some XP (or use the Arcade for a minute), **exit KieeKey, restart**:
   level, XP and achievements are preserved; if the AI opt-in is on, the learned
   profile survives too (delete `%APPDATA%\KieeKey\aiprofile.dat` to re-check the
   opt-in gate).
7. **M7** Move the hub window to a **second monitor with different scaling**: the
   footer hint stays clear of the FPS counter and the counter scales.

**Deliberately NOT covered by this checklist** (accepted, documented limitations):
one-frame cosmetic latency in the live-effects overlay (risk R2); geometric
90°/270° glyph rotation stays render-only (risk R4, the Lab says so on screen).

---

## 4. Version carriers — 1.3.0-beta8 (PE 1.3.0.9)

| Carrier | Value |
|---|---|
| `src/app/KieeKeyApp.rc` | `FILEVERSION/PRODUCTVERSION 1,3,0,9`, `FileVersion/ProductVersion "1.3.0.9"` |
| `src/app/KieeKeyApp.manifest` | `assemblyIdentity version="1.3.0.9"` |
| `src/core/kieekey_core.hpp` | `VERSION_STRING "1.3.0-beta8"` |
| `src/app/main.cpp` | `kAppVersionFull L"1.3.0-beta8"`, `kAppTitle` |
| `scripts/check_version.py` | `CHANNEL "beta8"`, `BUILD_REVISION 9` |
| `README.md` / `CHANGELOG.md` | `1.3.0-beta8` |

`python3 scripts/check_version.py --repo=.` → **OK** — every carrier agrees on
`1.3.0-beta8 (PE 1.3.0.9, manifest 1.3.0.9)`.

---

*SPDX-License-Identifier: GPL-3.0-or-later*

## CI now checks what the screen shows, not just the rectangles

The Windows UI probe (x64 job) used to compare window rectangles. A real desktop at
150 % still showed overlapping text while it was green, so the probe now audits the
**visible** rectangle: window rect ∩ window region ∩ tab page. It also proves that
only one page is on screen at a time, drives the real vertical-scroll path through
its whole range and re-checks the geometry at every step, and repeats the entire
audit at 150 % using the application's own DPI-change path. Everything it finds is
reported per tab with the numbers (`id: x,y wxh`), so a finding can be reproduced
without the screenshot.

## The report now tells us WHERE, not just how it looks

"Chữ bị đè" survives CI because CI's DPI, font and text scale are not the user's. So
the app measures its own settings dialog — every tab, at the DPI in use, with each
control's own font — and appends the result to the diagnostics report (the pane and the
exported file, one builder). `Xuất báo cáo` now ends with a section like:

```
=== Tự kiểm tra bố cục (đo trên cửa sổ đang mở) ===
DPI hiệu dụng: 96 (100 %)
Đã đo: 9 tab, 124 điều khiển (124 đang hiện), 2148 phép so
Kết quả: OK — không mục nào đè, cắt hay nằm ngoài tầm với.
```

or, when something really is wrong, `Kết quả: CÓ LỖI — N mục:` and one line per defect:
`[overlap] đè nhau 24x6px — …`, `[clipped] chữ cần 56px cao nhưng ô chỉ 28px — …`,
`[region] bị cắt bởi vùng vẽ cũ …`, `[cut] rộng hơn vùng tab 60px ở bên phải …`,
`[unreachable] nằm dưới trang kể cả khi đã cuộn hết 193px …`. Send that file instead of
a screenshot: it carries the numbers, the tab and the DPI.

---

## Cuộn chỉ DI CHUYỂN, không đổi kích thước — và hàng chữ mọc thêm thì cả trang cùng dãn

Bốn dòng trong tab Cài đặt mang chữ sống (kết luận chẩn đoán, trạng thái Arcade, số liệu
AI, lời khuyên Coach): chữ dài ra thì hàng phải cao thêm. Bản trước tự nới hàng đó **tại
chỗ**, nên hai chuyện xảy ra cùng lúc: các hàng bên dưới không hề dịch xuống (chữ chạy
xuống dưới chúng), và vị trí cuộn vẫn nhớ chiều cao CŨ — vừa kéo là hàng bị bóp về chiều
cao cũ, dòng bạn đang đọc bị cắt ngang, nửa giây sau mới mọc lại. CI đo được đúng con số:
`id 627: 188px → 168px ở offset 7/7` (tab 6 @150 %), `id 650: 296px → 276px` (tab 3 @150 %).

Nay: bộ giải layout là nơi DUY NHẤT đặt vị trí và kích thước; cuộn chỉ di chuyển
(`SWP_NOSIZE`); hàng cần thêm chỗ thì đặt yêu cầu "giải lại", và mỗi nhịp 500 ms chỉ giải
lại **một lần**: hàng nới đúng theo chữ đo được, mọi thứ bên dưới dịch xuống, cửa sổ tự
nới, dải cuộn được tính lại — **giữ nguyên vị trí cuộn** của bạn (kẹp trong dải mới).
Giới hạn nới cứng (8/4/5/60 px) đã bỏ: thước đo thật quyết định, nên không hàng nào còn
bị cắt chữ.
