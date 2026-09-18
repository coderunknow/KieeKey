# KieeKey
## Project Status

**KieeKey is currently under active development!**

Development has resumed after a period of being frozen due to the project's growing bug count and maintenance complexity.

The current Stable release remains available for anyone who wants to use or experiment with it. New development is focused on improving reliability, fixing known issues, and carefully introducing changes without expanding the scope unnecessarily.

New features and major changes may be introduced during development, but they will be tested before being considered for a Stable release.

At this stage, development builds should be considered **pre-release** and may contain bugs or incomplete changes.

The project may still be paused again in the future if development no longer provides enough value to justify the maintenance cost.


[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
![Language](https://img.shields.io/badge/language-C%2B%2B20%2FC%2B%2B23-00599C.svg)
![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%7C%20ARM64-0078D6.svg)
![Build](https://img.shields.io/badge/build-CMake%20%3E%3D%203.28-064FAD.svg)

**KieeKey v1.3.0-beta1** is a modern, low-latency Vietnamese input method
engine (bộ gõ Tiếng Việt) for Windows, with a system-tray application, a TSF
text-store composer and an optional WinUI 3 Fluent settings UI.

> **KieeKey is a modified version based on
> [OpenKey](https://github.com/tuyenvm/OpenKey)** by Tuyen Mai (GPL-3.0).
> The original C++11/Win32 engine of OpenKey 2.0.5 was fully ported to
> modern C++, refactored and hardened. KieeKey inherits the **GNU General
> Public License v3.0** in its entirety and keeps upstream attribution in
> every source file.

![KieeKey preview](src/app/KieeKeyApp-preview.png)

---

## What's new in v1.3.0-beta1 — Arcade Hub, Chaos Lab, AI Rival & Progression

**v1.3.0-beta1** expands KieeKey into a Vietnamese IME that is also fun to
type in: an isolated **Arcade Hub** of eight typing games, a **Chaos Lab** of
visual text transforms that can type into other applications, a **personal AI
typing rival** that learns your rhythm and then races you, **global
progression** (levels, XP, achievements) and a **real-time typing coach**.
Every optional module is off the hot path: when nothing is active the engine
performs the same as before (verified by the feature-isolation gate, see
[Verification](#verification-in-this-release)).

Everything below is one release; the full history of v1.2.x and v1.1.x lives in
[CHANGELOG.md](CHANGELOG.md).

### Graphical front-ends (the games are never text-mode)

| surface | what it is | how to open it |
|---|---|---|
| **Arcade Hub window** | Win32/GDI window (1180×760, double-buffered, 60 FPS) with a game-catalogue sidebar, click-to-play, live score/WPM footer and a level-up toast | `KieeKeyApp.exe --arcade[=slug]`, the tray menu, or the settings dialog buttons |
| **Web player** | HTML5 canvas client driving *the same C++ engine* over HTTP + SSE (`tools/arcade_serve`, `web/`) | `arcade_serve --port 8765 --host 0.0.0.0 --web web` then open `http://localhost:8765/` |
| **Chaos Lab window** | Dedicated test UI: type text, see the exact bytes KieeKey would emit, and (optionally) write them into the application you were working in | `KieeKeyApp.exe --chaos-lab` or the tray menu |
| **Flexing page** | Its own surface inside the lab: prepared passage in, engine text out (WPM / efficiency / cursor), then really typed into the app you came from — one paste or chunk by chunk | Lab window → 🗿 Flexing Mode |
| **Web labs** | The same two surfaces in the browser: `POST /api/preload` + `flex.emitted` for the Flexing stage, `GET|POST /api/chaos` + `POST /api/chaos/preview` for the Chaos lab (the transformation runs in C++, never in JavaScript) | Buttons in the side panel of the web player |
| **Web progression & AI** | Level, XP bar, streak, achievements and minigame records from `ProgressionEngine`, plus the opt-in AI rival with the pace it learned and the finish time it would get on the passage you are racing — a level-up toasts in the page | Side panel of the web player |
| **Settings surfaces** | WinUI 3 pages (Arcade / Chaos / Progression & AI) and the Win32 settings dialog act as launcher + live telemetry, and carry the run configuration: Rhythm/No-Mistake **fail mode** (Hardcore default, or HP bar) and the **BPM** | `KieeKeyApp.exe --settings=N` |

### The eight games

| | game | what it tests | controls |
|---|---|---|---|
| 🐍 | **Snake** | reaction + control under acceleration | WASD / arrows, `P`/`F1` pause, `R`/`F2` restart, `Esc` quit |
| 🧱 | **Tetris** | 7-bag tetrominoes, rotation, line clears, levels | `A`/`D` move, `W` rotate, `S` soft drop, `Space` hard drop |
| 🎣 | **Fishing by typing** | type the shown bait; pull meter, escape timers, rarities from common to legendary | type the prompt |
| 🏎️ | **Typing-speed racing** | race a pacer on a WPM track | type the passage |
| 🛣️ | **Racing + WASD dodging** | multitasking: refuel by typing, dodge in three lanes | `A`/`D` lanes, `W`/`S` speed + typing to refuel |
| 🎵 | **Rhythm typing (FNF-style)** | beat-synchronised notes: too fast dies, too slow dies | `D`/`F`/`J`/`K` on the beat; **hardcore** (one miss = death, default) or **HP bar** mode, selectable in the UI |
| 🎯 | **No-Mistake Mode** | one wrong key is punished (configurable reserve/penalty) | type each character correctly |
| 🗿 | **Flexing Mode** | type anything and pre-prepared text appears at ludicrous speed — and can be injected into the focused app | any key |

### Chaos Lab — chaos you can actually test

* 🌀 **Random UPPER/lower case** — intensity and granularity (per character / per word),
  Vietnamese case tables (including `Ưư`), off by default.
* 🔄 **Rotated / flipped glyphs** — lookalike transforms (rotate, flip, random).
  90°/270° rotations are *render-only*: the document keeps pristine Unicode.
* Both can **type the result into the focused application** through the same
  `InlineEmitter` the IME uses, and the Chaos Lab window is the dedicated UI
  for testing exactly that (type, compare, inject).

### AI rival, progression, analytics, ghost

* 🤖 **Personal AI typing rival** — learns inter-key intervals, pauses, bursts and
  tone-mark delay from your own keystrokes (explicit opt-in, local only,
  one-click purge), then races you with your own rhythm. Learning is
  incremental: each keystroke is fitted exactly once and the model is
  evidence-weighted, so it follows a fast typist and a slow one alike.
* 📈 **Global progression** — XP for characters, words and whole runs (snake,
  tetris, fishing, races, rhythm, no-mistake), a deterministic level curve,
  lifetime statistics, daily streaks, achievements and checksummed
  persistence with corrupt-file recovery.
* 📊 **Analytics & coach** — a fixed-size lock-free ring records your typing;
  the coach separates measured facts (“your mean IKI is 168 ms”) from heuristic
  advice, and only speaks once there is enough evidence.
* 🌐 **Solo-online / ghost** — a pluggable provider interface for replays,
  ghosts and leaderboards, with a fully local provider and zero network traffic.

### Verification in this release

Run everything with `tests/run_all_tests.sh` (native) or `ctest` on Windows:

| suite | covers | result |
|---|---|---|
| `ok_arcade_tests` | all 8 games, manager, live config, determinism, no steady-state allocations | 3774 checks, 0 failures |
| `ok_arcade_render_tests` | display list, JSON wire format, injection safety, viewport letterboxing, payload budget | 6/6 |
| `ok_arcade_server_tests` | HTTP routing, session control, input forwarding, traversal guards, a full simulated race, the Flexing payload, the Chaos lab, progression/AI-rival routes, `POST /api/config` live/applyNow | 10/10 |
| `ok_arcade_window_tests` | **the real Win32 window procedures**: catalogue painting, hover, click-to-play, keyboard, timer, `Esc`/close, Chaos Lab preview + injection, and the Flexing page (prepared text in, engine text typed out) | 5/5 |
| `tests/web_labs_test.js` | the browser lab glue: engine text into the flexing control, chaos preview/replay, "keys stay in the text field" guard | 27 checks |
| `tests/web_progress_test.js` | the progression/AI panel: engine numbers, level-up toast, opt-in switch, learned pace, live race preview, visibility-aware polling | 23 checks |
| `tests/web_render_test.js` | the HTML5 renderer replayed over frames captured from the C++ engine (`tests/data/web_frames.json`): every game's commands, colours, gradients, HUD | 62 checks |
| `demo/arcade_cli --test` | the terminal front-end: all 8 games launch, answer input and produce a display list + wire JSON | 8/8 + chaos |
| `ok_chaos_tests` | case + glyph transforms, render-only rotations, thread safety | 4/4 |
| `ok_ai_tests` | opt-in/privacy, learning, racer determinism, ghost round-trip, hostile payloads, concurrency | 6/6 |
| `ok_progression_tests` | XP, levels, streaks, achievements, persistence/recovery, concurrency | 9/9 |
| `ok_analytics_tests` | rhythm metrics, ring rollover, session window, coach, concurrency | 6/6 |
| `ok_online_ghost_tests` | ghost/replay/leaderboard abstraction | ✓ |

The Windows-only UI code is exercised on any host through
`tests/win32_gdi_stub.cpp` — a recording USER32/GDI32 layer that delivers real
`WM_*` messages to the real window procedures and records every drawing call,
so “the sidebar lists all eight games”, “hovering highlights a row” and
“injection only happens when the checkbox is ticked” are assertions, not
claims. Standardised performance numbers for the arcade pipeline live in
[docs/bench/arcade-130/ARCADE_BENCH_REPORT.md](docs/bench/arcade-130/ARCADE_BENCH_REPORT.md).

**Core IME guarantees are unchanged**: 2,059,419-event gate correctness
against the clean-room oracle (0 mismatches), 0 ns added latency when the
optional modules are idle, and unchanged peak RSS. The v1.3.0 line was also
A/B-measured against the **v1.2.2 stable tag** with byte-identical harnesses —
see [docs/bench/v122_vs_130/](docs/bench/v122_vs_130/README.md): equal or faster
on every end-to-end percentile (p50 −5.2 %, p99 −1.5 %), identical peak RSS
(8.551 MB), the same SendInput batching invariant (108 batched edits per
100 000 keys), and a byte-identical three-engine differential.

## Highlights

* **Correct Vietnamese output** — Telex, VNI and Simple-Telex, with the
  upstream tone-mark fix carried in (golden test: `"as"` → `"á"`).
* **Low latency by design** — asynchronous low-level keyboard hook feeding a
  lock-free Vyukov SPSC ring; the hook callback never blocks and returns in
  O(1). Deep E2E latency audit included ([docs/reports/LATENCY_AUDIT_REPORT.md](docs/reports/LATENCY_AUDIT_REPORT.md)).
* **Modern TSF composer** — commits text through the Text Services Framework
  text store, no synthetic backspaces.
* **Event-driven app awareness** — foreground-process monitor with
  auto-exclusion of games/apps that dislike IMEs.
* **Familiar tray UX** — green/gray tray icon, right-click menu, Vietnamese
  settings dialog, explicit in-app on/off toggle (tray menu + settings
  button; the old Ctrl+Shift global hotkey was removed in v1.1.1).

## What KieeKey changes compared to OpenKey

| Area | OpenKey 2.0.5 (upstream) | KieeKey v1.3.0 |
|---|---|---|
| Language level | C++11 / Win32 | C++20/23 (per-instance state machine, constexpr, RAII) |
| Input pipeline | Synchronous processing in hook callbacks | Async hook thread → lock-free ring → consumer thread |
| Text insertion | Backspace-driven editing | TSF text-store composer (single-edit fast path) |
| Phonetics data | `std::map`/`std::vector` built at runtime | Generated flat tables (`tools/gen_flat_tables.py`) for cache-friendly lookups |
| Resource handling | Manual HANDLE/registry lifecycle | RAII wrappers (`Win32RAII.hpp`) |
| Testing | Manual QA | Golden vectors + stress/soak/fuzz suites + 3-way differential vs a clean-room oracle and vendored engines (~5.97M cases, [docs/reports/MEGA_BENCH_REPORT.md](docs/reports/MEGA_BENCH_REPORT.md)) |
| Latency engineering | — | Instrumented tone-mark path, event-driven ordering barrier, p50–p99.9 percentile benches ([docs/reports/LATENCY_AUDIT_REPORT.md](docs/reports/LATENCY_AUDIT_REPORT.md)) |
| CI/CD | — | GitHub Actions matrix: x64 + ARM64 + ARM64EC, unit tests (ctest) on the native-arch x64 job, on every push |

> KieeKey keeps the engine algorithm faithful to upstream (1:1 phonetics
> tables) — the refactor targets architecture, latency and testability, not
> behavior changes. The full engineering history (15 verbatim lineage
> reports, written under the pre-release working name "OpenKey NextGen")
> is preserved in [docs/reports/](docs/reports/README.md).

## Architecture

```
WH_KEYBOARD_LL / WH_MOUSE_LL          (hook thread, serialized)
        │  try_push — O(1), allocation-free
        ▼
LockFreeQueue (Vyukov SPSC ring)
        │  drain
        ▼
TextEngine  ── Telex / VNI / Simple-Telex state machine (flat tables)
        │  OutputItem (backspace count + UTF-16 payload)
        ▼
TsfComposer ── TSF text-store commits (zero-alloc single-edit fast path)
        │
        ├── ProcessMonitor ── foreground app detection + auto-exclusion
        ├── Win32 tray app (KieeKeyApp.exe) + optional WinUI 3 settings UI
        │
        └── optional side features (off the hot path, same core)
              ArcadeManager ── Frame ── buildRenderList ──┬── Win32 GDI hub window
                                                          └── JSON/SSE ── HTML5 canvas
              ChaosEngine · AiRivalEngine · ProgressionEngine · TypingAnalytics
```

Source layout: `src/core` (engine, queue, RAII, tables **and** the arcade,
chaos, AI, progression, analytics and ghost modules), `src/tsf` (composer),
`src/app` (Win32 tray app, resources, the GDI Arcade Hub window and the Chaos
Lab window), `src/ui` (WinUI 3 Fluent settings), `web` (HTML5 Arcade client),
`tests` (unit/stress/bench harnesses + vendored reference engines),
`tools` (table generators + `arcade_serve`, the web bridge), `demo`
(interactive console demos).

## Building

Requirements: **Windows 10/11**, **Visual Studio 2022** (Desktop C++),
**CMake ≥ 3.28**, Windows App SDK (only for the optional WinUI 3 UI).

```bat
:: configure (x64 Release, WinUI 3 UI + tests)
cmake --preset x64-release

:: build
cmake --build --preset x64-release

:: run the unit tests via CTest
ctest --preset x64-release
```

The native Arcade Hub needs no extra dependency (GDI only). The optional web
player is a second target that builds on Windows **and** on Linux:

```bat
cmake --build --preset x64-release --target arcade_serve
arcade_serve --port 8765 --host 0.0.0.0 --web web
```

Presets available: `x64-debug`, `x64-release`, `arm64-release` (see
`CMakePresets.json`). A MinGW-w64 toolchain file is provided at
`cmake/mingw-w64-x86_64.cmake` for console/engine-only builds. CI builds
both targets on every push via `.github/workflows/build.yml`; tagged commits
additionally produce downloadable release artifacts — prebuilt binaries are
**not** committed to this repository (see `bin/README.txt`). The optional
WinUI 3 front-end is not built in CI (it needs the Windows App SDK NuGet
package restored locally).

### Keeping CI green: the `SHA256SUMS.txt` manifest

CI verifies `SHA256SUMS.txt` against the tree on every push — a commit that
changes any tracked file without regenerating the manifest fails the
`Verify SHA256SUMS manifest` step (this is exactly how the two CI breakages
after README-only edits happened). The manifest hashes the **staged** (index)
content, so regenerating is a single pre-commit step, not a second commit:

```bash
git add -A                        # stage everything you changed
scripts/gen_sha256sums.sh         # regenerate against the staged tree
git add SHA256SUMS.txt
git commit                        # manifest + tree are consistent
```

To make it impossible to forget, enable the bundled pre-commit hook once
per clone — it regenerates and re-stages the manifest automatically
whenever a commit touches tracked files:

```bash
git config core.hooksPath scripts/hooks
```

## Repository layout

```
KieeKey/
├── LICENSE                     GNU GPLv3 (verbatim)
├── README.md                   this file
├── THIRD-PARTY-NOTICES.md      upstream licenses & compliance notes
├── CHANGELOG.md                release history
├── CMakeLists.txt / CMakePresets.json / cmake/
├── .github/workflows/build.yml CI matrix (x64/ARM64)
├── docs/reports/               15 verbatim lineage engineering reports (+ index)
├── docs/bench/                 raw benchmark artifacts per campaign
├── src/                        core engine, arcade/chaos/AI modules, TSF, tray app, WinUI 3
├── web/                        HTML5 Arcade Hub client (canvas + SSE)
├── tests/                      unit / stress / bench + vendored references
├── tools/                      flat-table generators + arcade_serve web bridge
├── imebench_kit/               3-way benchmark harness (results regenerated locally)
├── demo/                       interactive console demos (engine + arcade)
├── scripts/                    engine probes + SHA256SUMS tooling & hooks
└── bin/                        build-output placeholder (see bin/README.txt)
```

## License

KieeKey — Copyright (C) 2026 coderunknow - https://github.com/coderunknow.

This program is **free software**: you can redistribute it and/or modify it
under the terms of the **GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version** — see the [LICENSE](LICENSE) file.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
more details.

KieeKey is a modified version based on OpenKey — Copyright (C) 2019
Tuyen Mai — which is licensed under GPL-3.0; KieeKey inherits that license
in full. Third-party reference sources are vendored verbatim under their
original licenses — see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Acknowledgements

* **[OpenKey](https://github.com/tuyenvm/OpenKey)** by **Tuyen Mai** — the
  upstream project KieeKey is based on. KieeKey would not exist without the
  original engine, its phonetics tables and its years of real-world
  refinement. Xin cảm ơn!
* **[UniKey](https://www.unikey.org)** by **Pham Kim Long** — vendored as a
  reference engine used by the differential-test harness to cross-validate
  correctness (LGPL; original headers preserved).
* The Vietnamese free-software community, whose feedback shaped both
  upstream projects.

---

## Tóm tắt (Tiếng Việt)

**KieeKey v1.3.0-beta1** là bộ gõ Tiếng Việt cho Windows, xây dựng dựa trên
**[OpenKey](https://github.com/tuyenvm/OpenKey)** (GPL-3.0) của tác giả Tuyen
Mai. Engine gốc đã được port sang C++ hiện đại: hook bất đồng bộ với hàng đợi
lock-free, composer TSF (không backspace ảo), bảng âm tiết flat tối ưu cache,
kèm bộ test vi mô, đo hiệu năng và đối chiếu sai khác hàng triệu trường hợp.

Điểm mới của v1.3.0: **Arcade Hub** với 8 mini-game gõ phím (Rắn, Xếp gạch,
Câu cá, Đua tốc độ, Đua + né WASD, Nhịp điệu FNF, Không-lỗi, Flexing) chạy
trong **cửa sổ đồ hoạ thật** (Win32 GDI trên desktop, HTML5 canvas trên web —
cùng một engine C++); **Phòng Chaos** đổi hoa/thường và xoay/lật glyph (có
thể gõ thật ra ứng dụng đang mở, kèm giao diện test riêng); **AI đối thủ** học
nhịp gõ của bạn rồi đua lại; **tiến trình** cấp độ/XP/thành tựu; **thống kê &
huấn luyện viên** theo thời gian thực; và **ghost/online** dạng pluggable chạy
hoàn toàn cục bộ. Mọi tính năng mới đều ở ngoài đường găng: khi không dùng,
độ trễ gõ không đổi.

Chạy toàn bộ kiểm thử: `tests/run_all_tests.sh` (Linux/mac) hoặc `ctest`
(Windows). Bật/tắt bộ gõ nằm hoàn toàn trong ứng dụng (trình đơn khay + Cài
đặt); mở nhanh game bằng `KieeKeyApp.exe --arcade`, mở phòng Chaos bằng
`KieeKeyApp.exe --chaos-lab`.

KieeKey là phần mềm tự do theo **GNU GPLv3** (kế thừa từ OpenKey). Bản quyền
tác giả gốc được giữ trong đầu mỗi file nguồn; mã tham chiếu OpenKey 2.0.5 và
UniKey giữ nguyên trong `tests/reference/` — xem
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
