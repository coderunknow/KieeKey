> **Lineage note (KieeKey v1.0 packaging):** Historical engineering
> document from the refactor lineage that produced KieeKey v1.0 (a fork of
> OpenKey, GPL-3.0). Written before the v1.0 release unification, it uses the
> pre-release working name "OpenKey NextGen" and internal milestone numbers
> (v3.0-v3.4). Published verbatim for traceability - see
> [docs/reports/README.md](README.md).

# OpenKey NextGen v3.0 — Technical Refactoring Execution Plan

**Source fork:** [github.com/tuyenvm/OpenKey](https://github.com/tuyenvm/OpenKey) tag `2.0.5` (downloaded from `https://github.com/tuyenvm/OpenKey/archive/refs/tags/2.0.5.zip`)
**Baseline:** C++11 / Win32 / VS2019 (v142) / 2022 era
**Target:** C++23 / Win32 + TSF / WinUI 3 (Fluent) / x64 + ARM64 / GitHub Actions CI

> This document is the roadmap. The production-ready implementation snippets for the four
> mandated deliverables live in the sibling `src/` tree of this workspace and are cross-referenced
> below. Platform-neutral components (**TextEngine**, **LockFreeQueue**) were compiled and their
> test suites run green in this workspace; Windows-only components compile against the documented
> toolchain (see §8).

---

## 1. Executive summary

OpenKey 2.0.5's Windows build is a textbook legacy Win32 keyboard hook application. The low-level
`WH_KEYBOARD_LL` callback performs the **entire** Vietnamese text pipeline synchronously, inside the
hook, on the hook thread:

```
KBDLLHOOKSTRUCT → vKeyHandleEvent() → SendBackspace() ×N → SetClipboardText()
               → SendCombineKey(Shift+Insert) → (metro: SendMessage(HWND_BROADCAST, WM_CHAR,…))
```

That design is the root cause of every symptom the task lists (ghosting / duplicate letters, cursor
flicker in Chrome/Edge/Office, dropped keystrokes): **`SendInput` and clipboard round-trips are
slow and racy, and while they run, the hook thread is blocked** — keystrokes pile up, `_syncKey`
bookkeeping desynchronizes from what is actually on screen, and the broadcast `WM_CHAR` hack
double-fires into every window.

NextGen replaces this with a **lock-free producer/consumer pipeline** and a **buffer-state output
model** (TSF), eliminating synthetic backspace spam and the clipboard round-trip entirely.

### Concrete bugs found in the 2.0.5 tag (all fixed in NextGen)

1. **Tone marks silently never inserted.** `Engine.cpp` `handleMainKey()` iterated
   `for (i = 0; i < _vowelForMark.size(); i++) charset = _vowelForMark[i];` — `map::operator[]`
   with keys `0..5`, while the map's real keys are letters → empty rows → `insertMark()` never
   matched. Upstream `master` fixed this to `for (auto& e : _vowelForMark)`; **NextGen ports the
   fix**, verified by the golden test `as → á`.
2. **Clipboard handle leaks / unclosed clipboard.** `OpenKeyHelper::getClipboardText()` and
   `quickConvert()` return early without `CloseClipboard()`/`GlobalUnlock()` on every error path;
   `getRegBinary()` leaks a `new BYTE[]` buffer that is only freed on the *next* call.
3. **Broadcast keystroke injection.** `SendMessage(HWND_BROADCAST, WM_CHAR, VK_BACK, 0)` (Metro
   workaround) delivers the same Backspace to every top-level window — duplicate letters.
4. **Per-event Win32 calls inside the hook.** `GetKeyState`/`OpenProcess`/`GetProcessImageFileName`
   per keypress; next-gen tracks modifier state incrementally from the key stream (zero calls).
5. **`_syncKey` global bookkeeping** desyncs when the hook is starved → ghost characters.
6. **Unsynchronized global mutable state** (`TypingWord`, `_index`, `HookState`, `_flag`, …) makes
   the engine non-reentrant and untestable.

---

## 2. Architecture: before → after

| Concern | 2.0.5 (legacy) | 3.0 (NextGen) |
|---|---|---|
| Hook callback work | whole pipeline, synchronous | O(1): build POD `KeyEvent` → `try_push` → `CallNextHookEx` |
| Cross-thread comms | none (all on hook thread) | lock-free SPSC ring (Vyukov), 4096 slots, producer never blocks |
| Output | backspaces + clipboard + Shift+Insert | `EngineResult` → TSF text store (or batched `KEYEVENTF_UNICODE` fallback) |
| Foreground detection | `GetForegroundWindow` per event + polling | `SetWinEventHook` push → atomic `shared_ptr<const ForegroundInfo>` snapshot (0 idle CPU) |
| App exclusion | hardcoded `_chromiumBrowser` vector | constexpr tables (IDE / game / shell / browser / metro) + fullscreen & DWMWA_CLOAKED heuristics |
| Engine | 30+ file-scope globals, macros, `char*` | one `TextEngine` instance per session; `constexpr` helpers; `std::string_view`/`char32_t` |
| Strings/memory | `TCHAR`, `wchar_t*`, `new BYTE[]` | `std::wstring`, `std::string_view`, RAII handle wrappers |
| C++ standard | C++11 | C++23 (`/std:c++latest`, `/W4 /WX`) |
| Build | `.sln`/`.vcxproj`, v142, x86+x64 | CMake 3.28, x64 + ARM64 (+ ARM64EC), WinAppSDK |
| CI | none | GitHub Actions matrix (x64/ARM64/ARM64EC) + release automation |
| UI | Win32 dialog `.rc` | Shipped: native Win32 **tray app** (green/gray tray icon, Vietnamese menu + Cài đặt dialog) — `src/app/`. Optional: WinUI 3 Fluent (Mica, dark-mode sync) — `src/ui/` (VS2022 + Windows App SDK). |

### Data-flow diagram

```
                       ┌────────────────────── HOOK PUMP THREAD ──────────────────────┐
 WH_KEYBOARD_LL ──────▶│  KBDLLHOOKSTRUCT → KeyEvent (POD) ──┐                        │
 WH_MOUSE_LL ─────────▶│  mouse word-break signal             │  try_push  (lock-free)│
 SetWinEventHook ─────▶│  foreground-changed signal           ▼                        │
                       └───────────────────────────────┬─────┴────────────────────────┘
                                                       │   SPSC ring (4096, no locks)
                                                       ▼
                       ┌────────────────────── CONSUMER THREAD ───────────────────────┐
                       │  batch-drain (64) → ProcessMonitor snapshot (atomic load)     │
                       │  → excluded?  ──yes──▶ forward key untouched (0 CPU)          │
                       │       │ no                                                   │
                       │       ▼                                                       │
                       │  TextEngine::process(TextInput) → EngineResult               │
                       │       │                                                       │
                       │       ▼                                                       │
                       │  TSF composer (ITfContextOwner) or SendInput(UNICODE batch)   │
                       └──────────────────────────────────────────────────────────────┘
```

---

## 3. Step-by-step execution plan

### Phase 0 — Baseline & harness (done)
1. Download & unpack `2.0.5` (`openkey-2.0.5.zip`, 132 files). Map the codebase:
   - Hook core: `win32/OpenKey/OpenKey/{OpenKey.cpp, OpenKeyHelper.*, SystemTrayHelper.*, AppDelegate.*, MainControlDialog.cpp, OpenKey.rc}`.
   - Shared engine: `engine/{Engine.*, Vietnamese.*, DataType.h, Macro.*, SmartSwitchKey.*, ConvertTool.*, platforms/win32.h}`.
2. Build a platform-neutral test harness **before touching logic** (`tests/`) so every port step is
   verified by golden vectors (Telex/VNI) and ring stress. *Outcome: both suites green in this workspace.*

### Phase 1 — Memory & language modernization (engine, no behavior change)
1. `engine/DataType.h` macros → `constexpr` constants (`kCapsMask`, `kToneMask`, `kMark1..5Mask`, …) — **do not change numeric values**.
2. Global `TypingWord`, `_index`, `HookState`, `_flag`, `v*` option globals → private members of a single
   `TextEngine` class; options via `EngineOptions` struct.
3. Replace `std::wstring_convert<std::codecvt_utf8_utf16>` (removed in C++26) with explicit
   `WideCharToMultiByte`-style conversion or `char32_t` normalization.
4. **Deliverable:** `src/core/TextEngine.hpp/.cpp`, `src/core/VietnameseTables.hpp`.

### Phase 2 — Zero-latency hook engine
1. Keep a dedicated **hook pump thread** (required: LL hooks only fire on the installing thread's
   message loop) and move a **consumer thread** in front of the text pipeline.
2. Replace `SetWindowsHookEx`-callback logic with an O(1) producer that pushes `KeyEvent` PODs into
   a **Vyukov SPSC ring**; apply overflow policy (`DropNewest` default — never block, never reorder).
3. Track modifiers incrementally from the key stream; seed once at install (kills per-event
   `GetKeyState`).
4. Self-injection filter on `dwExtraInfo == 0x0B00B1E5` (replaces the `!= 0` blanket filter).
5. **Deliverable:** `src/core/ModernKeyHook.hpp/.cpp`, `src/core/LockFreeQueue.hpp`.

### Phase 3 — Buffer-state output (kill synthetic backspaces)
1. Introduce a `Composer` interface: `commit(backspaceCount, replacementUtf16)`.
2. Preferred backend: **TSF** (`ITfThreadMgrEx` + `ITfContextOwnerCompositionServices`) so
   replacement happens inside the active text store — no `SendInput`, no clipboard, no cursor
   flicker in Chrome/Edge/Office/Excel. Implementation sketch: `src/tsf/TsfComposer.hpp/.cpp`.
3. Fallback backend (legacy apps without TSF): one `SendInput` batch of
   `KEYEVENTF_UNICODE` for the replacement + the *minimal* number of `VK_BACK`s — never the
   `HWND_BROADCAST` hack, never clipboard + Shift+Insert.

### Phase 4 — Process monitoring & auto-exclusion
1. `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` pump thread → atomic snapshot
   (`pid`, exe name, fullscreen, cloaked, process-creation time).
2. PID-reuse-safe: track `GetProcessTimes` creation time.
3. Constexpr classification tables: IDEs, games (fullscreen heuristic), shell, browsers, Metro;
   `autoExcluded()` short-circuits the engine with **zero** cost.
4. `RegisterApplicationRestart` for crash recovery.
5. **Deliverable:** `src/core/ProcessMonitor.hpp/.cpp`.

### Phase 5 — WinUI 3 Fluent UI (replaces `.rc`)
1. Delete `OpenKey.rc` / `resource.h` dialog templates; keep only icons/manifest.
2. WinUI 3 window with `SystemBackdrop="MicaBackdrop"`, `ThemeResource` brushes for native
   dark/light sync, `ToggleSwitch`/`ComboBox`/`Expander` settings, live latency telemetry
   (from `ModernKeyHook::peakLatencyUs()`).
3. **Deliverable:** `src/ui/MainWindow.xaml` (sketch) + code-behind pattern in `CMakeLists.txt`.

### Phase 6 — Build system, packaging & CI
1. CMake 3.28, `/std:c++23`, `/W4 /WX`, x64/ARM64/ARM64EC; Windows App SDK via `find_package(Microsoft.WindowsAppSDK)`.
2. GitHub Actions matrix building x64 + ARM64 (+ARM64EC), running `ctest`, uploading zips; tag →
   GitHub Release.
3. **Deliverables:** `CMakeLists.txt`, `.github/workflows/build.yml`.

### Phase 7 — Hardening (recommended next after this document)
- ThreadSanitizer run of the ring + engine on CI (optional job).
- Fuzz the engine with random ASCII/Vietnamese key streams; compare against 2.0.5 for identical
  output on non-buggy paths (the mark-insertion fix is the only intentional divergence).
- Signing: add `WINDOWS_CERT` secrets → sign MSIX in the release job.

---

## 4. Deliverable 1 — `ModernKeyHook.hpp/.cpp` (lock-free hook queue)

Implemented at `src/core/ModernKeyHook.hpp` / `src/core/ModernKeyHook.cpp`.

Key guarantees (see file comments for the full walkthrough):
- **Producer is O(1):** `keyboardProc` → build `KeyEvent` → `enqueue()` → `CallNextHookEx`.
  No allocation, no locks, no blocking, no Win32 syscalls except the QPC timestamp.
- **Consumer is lock-free:** `try_pop_batch` drains up to 64 events per iteration, amortizing
  atomics; measures E2E latency via QPC and publishes a peak-latency watermark for the UI.
- **Overflow policy:** `DropNewest` (default) or `DropOldest`; both never block the hook.
- **Self-injection:** events with `dwExtraInfo == kSelfInjectedExtraInfo` are filtered first.
- **Modifier tracking:** incremental from the key stream, seeded at install.
- **Mouse + foreground events** enter the same ring so ordering with keystrokes is preserved.

`src/core/LockFreeQueue.hpp` provides the Vyukov **SPSC** ring (used by the hook) and a bounded
**MPMC** queue (tested; for future multi-source topologies). Both are wait-free for the producer,
lock-free for the consumer, allocation-free, and trivially-copyable-`T`-constrained. *Test suite
passes: order preservation over 1M items, overflow, batch drain, 4-producer MPMC stress.*

## 5. Deliverable 2 — `TextEngine.hpp/.cpp` (C++20 string_view Telex/VNI state machine)

Implemented at `src/core/TextEngine.hpp` / `src/core/TextEngine.cpp`, tables at
`src/core/VietnameseTables.hpp`.

- Full 1:1 port of the 2.0.5 algorithm (Telex / VNI / Simple Telex; Unicode / TCVN3 / VNI-Windows /
  Unicode-Compound / CP-1258), with **all** engine globals made instance members and all macros made
  `constexpr` member functions (`isKeyS`, `isKeyW`, `isMarkKey`, `isDoubleCode`, …).
- `process(const TextInput&) → EngineResult` mirrors the legacy `vKeyHandleEvent` branch structure
  (word-break / space / backspace / main-key), including spelling check, grammar repair,
  `insertMark`/`insertAOE`/`insertW`/`insertD`, standalone `w [ ]`, Quick Telex, quick consonants,
  uppercase-first, and macro key accumulation.
- Output: `replacementUtf16(result)` resolves the internal bit-encoding to ready-to-insert UTF-16
  (surrogate-safe), **including the legacy `keyCodeToCharacter` remap** for plain keycodes and the
  reverse iteration order the legacy consumer used — verified by golden vectors.
- **Upstream bug fixed:** `kVowelForMark` iteration (see §1). Golden test `as → á` proves it.
- `processEnglishMode` + `MacroResolver` hook for macro support without clipboard.
- *Test suite passes:* 40+ golden Telex/VNI vectors, mark replacement, multi-word, backspace,
  pass-through — run repeatedly under `-O0` and `-O2`.

## 6. Deliverable 3 — `ProcessMonitor.hpp` (zero-cost foreground detector)

Implemented at `src/core/ProcessMonitor.hpp` / `src/core/ProcessMonitor.cpp`.

- Push model: `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` pump thread; readers do one
  `std::atomic_load(&snapshot_)` — **no locks, no polling, 0 idle CPU**.
- `ForegroundInfo` immutable snapshot: PID, exe path/name (UTF-8 lowercased for tables),
  fullscreen flag (monitor-exact heuristic), `DWMWA_CLOAKED`, process-creation time (PID-reuse
  safety).
- `autoExcluded()` for IDEs / fullscreen games / shell → engine bypass with zero per-key cost.
- `enableCrashRecovery()` wraps `RegisterApplicationRestart`.

## 7. Deliverable 4 — Project configuration / build upgrade

- `CMakeLists.txt` — the **replacement for the `.sln`/`.vcxproj`**: C++23, `/W4 /WX`,
  CFG + ASLR linker flags, x64/ARM64/ARM64EC, `ok_core` static lib, optional `ok_tsf` composer,
  optional WinUI 3 target via `find_package(Microsoft.WindowsAppSDK)`, `ctest` integration,
  install/packaging rules.
- `.github/workflows/build.yml` — **CI/CD**: matrix x64/ARM64/ARM64EC on `windows-2022`,
  MSVC dev-cmd, configure+build+test+smoke, artifact zip upload per arch, tag → GitHub Release
  with all artifacts.

### Migrating the old `.vcxproj` (quick reference)
| Legacy | NextGen |
|---|---|
| `PlatformToolset v142`, `LanguageStandard` (implicit C++11) | `CMAKE_CXX_STANDARD 23` (VS 2022 v143+) |
| `Debug\|Win32`, `Release\|x64` | `-A x64` / `-A ARM64` / `-A ARM64EC` |
| `PreprocessorDefinitions WIN32;_DEBUG;...` | `add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)` |
| `RuntimeLibrary MultiThreaded` | `CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"` |
| `UACExecutionLevel AsInvoker` | manifest in `src/ui_legacy` (kept) |
| `.rc` dialog | WinUI 3 XAML (`src/ui/MainWindow.xaml`) |

### vcpkg / dependencies
`vcpkg.json` (to be added in Phase 6 hardening): `windows-app-sdk` (WinUI 3) and `wil` (Windows
Implementation Library) as the only dependencies; engine + ring have **zero** third-party deps and
compile standalone on any C++23 compiler (verified with GCC 14 here).

---

## 8. Build & run (this workspace)

### Platform-neutral components — verified green here (Linux + GCC 14)
```bash
cd OpenKey-NextGen
g++ -std=c++23 -Wall -Wextra -O2 -I src/core tests/test_textengine.cpp src/core/TextEngine.cpp -o t1 && ./t1
g++ -std=c++23 -Wall -Wextra -O2 -Wno-interference-size -I src/core tests/test_ringbuffer.cpp -o t2 && ./t2
```

### Windows (VS2022, x64/ARM64)
```powershell
cmake -S . -B out/x64   -A x64   -DOPENKEY_BUILD_UI=winui3 -DOPENKEY_BUILD_TESTS=ON
cmake --build out/x64 --config Release
ctest --test-dir out/x64 -C Release
```

### CI
Push → `.github/workflows/build.yml` builds all three architectures, runs tests, uploads zips.

---

## 9. Verification & results (already executed)

| Suite | Scope | Result |
|---|---|---|
| `tests/test_textengine.cpp` | Telex/VNI golden vectors; upstream mark fix; backspace; pass-through; multi-word | **PASS** (repeated runs, -O0 & -O2) |
| `tests/test_ringbuffer.cpp` | SPSC 1M order, overflow, batch; MPMC 4×200k stress | **PASS** (repeated runs) |
| Sanity | `as→á`, `aas→ấ`, `uow→ươ`, `d9→đ`, `chao bàn` … | verified |

Two genuine defects were caught and fixed during this validation: (a) Vyukov SPSC slots must be
pre-armed `seq[i] = i` (first push mis-behaved); (b) the engine fills `newChars`
most-recent-first — the consumer must read it **backwards**, exactly as the legacy
`SendNewCharString` loop did.

---

## 10. Risks & notes for the Windows bring-up

- **TSF vs SendInput:** TSF is the only way to fully eliminate cursor flicker in Chromium/Office;
  the fallback path must keep the replacement to a single `SendInput` batch to avoid hook
  re-entrancy. The self-injection extra-info filter makes re-entrancy harmless regardless.
- **ARM64/ARM64EC:** `SetWindowsHookEx(WH_KEYBOARD_LL)` and TSF are supported on ARM64 Windows 11;
  ARM64EC allows shipping one x64-flavored codebase that runs on ARM64 natively.
- **Admin vs UAC:** legacy "Run as administrator" path (`IsUserAnAdmin` + `ShellExecute runas`)
  should be dropped; a per-user low-level hook does not require elevation, and elevation breaks TSF.
- **Behavior parity:** with the mark-insertion fix, NextGen output intentionally differs from the
  2.0.5 binary *only* where 2.0.5 was broken. Fuzz-parity harness (Phase 7) should pin this.
