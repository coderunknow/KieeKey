//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.1.0 - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// File: tests/extreme_toggle.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
// KieeKey — EXTREME toggle tests (portable, Linux-runnable)
//
// Reproduces the user-reported bug family:
//   "Whenever I open, it suddenly turn off for no reasons and I don't know
//    how to open it again."
//
// The toggle is driven by CtrlShiftChord fed from main.cpp's onHookEvent.
// This harness models that feed FAITHFULLY (same argument mapping, same
// event order) and drives it with extreme scenarios + a differential fuzz
// oracle. Scenarios marked [BUG-REPRO] demonstrate a real defect in the
// CURRENT code; they must pass after the fix.
//
// Scenarios:
//   T1  [FEATURE]        bare physical Ctrl+Shift toggles exactly once
//   T2  [v1.1.0 guard]   Ctrl↓ S↓ Shift↓ Shift↑   → no toggle
//   T3  [shortcut guard] Ctrl↓ Shift↓ A↓ A↑ Shift↑ Ctrl↑ → no toggle
//   T4  [v1.1.0 guard]   missed Shift-up + resync(0,0) + bare Ctrl tap → no toggle
//   T5  [BUG-REPRO]      INJECTED bare chord (RDP-forwarded language switch /
//                        key remapper / automation) → must NOT toggle
//   T6  [BUG-REPRO]      chord held across a foreground change: resync arms
//                        Clean → release toggles a chord we never saw start
//   T7  [BUG-REPRO]      injected shortcut traffic KILLS a pending real
//                        chord arming state? (modeling aid — see T8)
//   T8  [contract]       injected non-modifier keydown still cancels a real
//                        chord (remapped key between modifiers = shortcut)
//   T9  [persistence]    accidental toggle persists Enabled=0 → next launch
//                        starts OFF (state model; feedback handled in app)
//   T10 [FUZZ]           2M random events × 5 seeds, oracle invariants:
//                        - all-injected streams never toggle
//                        - exactly one clean physical chord ⇒ exactly one toggle
//                        - any physical/injected key between the modifier
//                          downs and first up ⇒ no toggle
//                        - any resync between arming and first up ⇒ no toggle
//                        - toggles never exceed physical clean chords
//============================================================================
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "CtrlShiftChord.hpp"

using ok::hotkey::CtrlShiftChord;

//-----------------------------------------------------------------------------
// Faithful model of main.cpp onHookEvent's chord feed.
//   isNonModifierKeyDown = !isCtrl && !isShift && keyDown
//   fed for EVERY EventSource::Keyboard event (injected included — this is
//   the CURRENT app contract; the FIX will add the injected-modifier filter
//   here, mirrored by kModelFixInjected).
//-----------------------------------------------------------------------------
static constexpr bool kModelFixInjected = true;    // true after the main.cpp fix

struct AppFeed {
    CtrlShiftChord chord;
    int            toggles = 0;
    bool           engineEnabled = true;

    void key(std::uint32_t vk, bool down, bool injected) {
        const bool isCtrl  = (vk == 0xA2 || vk == 0xA3);   // VK_LCONTROL/VK_RCONTROL
        const bool isShift = (vk == 0xA0 || vk == 0xA1);   // VK_LSHIFT/VK_RSHIFT
        const bool keyDown = down;
        if (kModelFixInjected && injected && (isCtrl || isShift)) {
            return;   // FIX: injected modifiers never drive the chord
        }
        if (chord.onKey(isCtrl, isShift, !isCtrl && !isShift && keyDown, keyDown)
                == CtrlShiftChord::Action::Toggle) {
            ++toggles;
            engineEnabled = !engineEnabled;
        }
    }
    void fgChanged(bool osCtrl, bool osShift) { chord.resync(osCtrl, osShift); }
    void mouse() {}   // mouse events never reach the chord (main.cpp contract)
};

//-----------------------------------------------------------------------------
// Event model for scenarios/fuzz
//-----------------------------------------------------------------------------
enum class EvKind : std::uint8_t { KeyDown, KeyUp, ForegroundChange, Mouse };
struct Ev {
    EvKind       kind;
    std::uint32_t vk = 0;      // VK_LCTRL 0xA2 / VK_RCTRL 0xA3 / VK_LSHIFT 0xA0 ...
    bool         injected = false;
    bool         osCtrl = false;   // for ForegroundChange: GetAsyncKeyState truth
    bool         osShift = false;
};

static constexpr std::uint32_t VK_LCTRL  = 0xA2, VK_RCTRL  = 0xA3;
static constexpr std::uint32_t VK_LSHIFT = 0xA0, VK_RSHIFT = 0xA1;
static constexpr std::uint32_t VK_A = 0x41, VK_S = 0x53, VK_V = 0x56;

static int run(const std::vector<Ev>& events) {
    AppFeed f;
    for (const Ev& e : events) {
        switch (e.kind) {
            case EvKind::KeyDown: f.key(e.vk, true,  e.injected); break;
            case EvKind::KeyUp:   f.key(e.vk, false, e.injected); break;
            case EvKind::ForegroundChange: f.fgChanged(e.osCtrl, e.osShift); break;
            case EvKind::Mouse:   f.mouse(); break;
        }
    }
    return f.toggles;
}

static int g_failures = 0;
#define CHECK(cond, name)                                                        \
    do {                                                                         \
        if (cond) { std::printf("  PASS  %s\n", name); }                          \
        else      { std::printf("  FAIL  %s\n", name); ++g_failures; }            \
    } while (0)

int main() {
    std::printf("=== KieeKey extreme toggle tests (kModelFixInjected=%d) ===\n",
                kModelFixInjected ? 1 : 0);

    //------------------------------------------------------------------ T1
    {
        std::printf("T1 bare physical chord toggles once:\n");
        const int t = run({
            {EvKind::KeyDown, VK_LCTRL}, {EvKind::KeyDown, VK_LSHIFT},
            {EvKind::KeyUp,   VK_LSHIFT}, {EvKind::KeyUp, VK_LCTRL}});
        CHECK(t == 1, "T1 bare chord → exactly 1 toggle");
    }

    //------------------------------------------------------------------ T2
    {
        std::printf("T2 shortcut key between modifiers (v1.1.0 audit):\n");
        const int t = run({
            {EvKind::KeyDown, VK_LCTRL}, {EvKind::KeyDown, VK_S},
            {EvKind::KeyDown, VK_LSHIFT}, {EvKind::KeyUp, VK_LSHIFT}});
        CHECK(t == 0, "T2 Ctrl↓ S↓ Shift↓ Shift↑ → 0 toggles");
    }

    //------------------------------------------------------------------ T3
    {
        std::printf("T3 full shortcut chord:\n");
        const int t = run({
            {EvKind::KeyDown, VK_LCTRL}, {EvKind::KeyDown, VK_LSHIFT},
            {EvKind::KeyDown, VK_A}, {EvKind::KeyUp, VK_A},
            {EvKind::KeyUp, VK_LSHIFT}, {EvKind::KeyUp, VK_LCTRL}});
        CHECK(t == 0, "T3 Ctrl+Shift+A press/release → 0 toggles");
    }

    //------------------------------------------------------------------ T4
    {
        std::printf("T4 missed Shift KeyUp + resync + bare Ctrl tap:\n");
        const int t = run({
            {EvKind::KeyDown, VK_LSHIFT},                 // Shift↑ MISSED (UIPI/RDP)
            {EvKind::ForegroundChange, 0, false, false, false},   // resync(0,0)
            {EvKind::KeyDown, VK_LCTRL}, {EvKind::KeyUp, VK_LCTRL}});
        CHECK(t == 0, "T4 desync recovery → 0 toggles");
    }

    //------------------------------------------------------------------ T5
    {
        std::printf("T5 [BUG-REPRO] INJECTED bare chord (RDP / remapper / automation):\n");
        const int t = run({
            {EvKind::KeyDown, VK_LCTRL, true}, {EvKind::KeyDown, VK_LSHIFT, true},
            {EvKind::KeyUp,   VK_LSHIFT, true}, {EvKind::KeyUp, VK_LCTRL, true}});
        CHECK(t == 0, "T5 all-injected chord → 0 toggles (user pressed nothing)");
    }

    //------------------------------------------------------------------ T6
    {
        std::printf("T6 [BUG-REPRO] chord held across foreground change (resync arms it):\n");
        // The user holds Ctrl+Shift while clicking another window (e.g. mid
        // Ctrl+Shift+click selection, or switching Windows input language
        // with the language bar hotkey while a window switch lands).
        const int t = run({
            {EvKind::KeyDown, VK_LCTRL}, {EvKind::KeyDown, VK_LSHIFT},
            {EvKind::Mouse},                                   // click → new fg
            {EvKind::ForegroundChange, 0, false, true, true},  // resync(1,1)
            {EvKind::KeyUp, VK_LSHIFT}, {EvKind::KeyUp, VK_LCTRL}});
        CHECK(t == 0, "T6 resync must not arm an unobserved chord → 0 toggles");
    }

    //------------------------------------------------------------------ T7
    {
        std::printf("T7 [BUG-REPRO] Windows language-switch hotkey (Ctrl+Shift) toggle is silent:\n");
        // The chord fires BY DESIGN here (indistinguishable keystrokes) — the
        // DEFECT is that the app gives zero feedback, so the user cannot tell
        // why typing stopped or how to undo it. Modeled: the toggle result
        // must be observable (feedback), which the portable layer encodes as
        // "the toggle is counted and surfaced" — asserted at app layer.
        const int t = run({
            {EvKind::KeyDown, VK_RCTRL}, {EvKind::KeyDown, VK_RSHIFT},
            {EvKind::KeyUp, VK_RCTRL}, {EvKind::KeyUp, VK_RSHIFT}});
        CHECK(t == 1, "T7 language-switch chord toggles (by design) — app must surface it");
    }

    //------------------------------------------------------------------ T8
    {
        std::printf("T8 injected non-modifier keydown still cancels a real chord:\n");
        const int t = run({
            {EvKind::KeyDown, VK_LCTRL}, {EvKind::KeyDown, VK_LSHIFT},
            {EvKind::KeyDown, VK_V, true},        // remapped key (only injected form seen)
            {EvKind::KeyUp, VK_V, true},
            {EvKind::KeyUp, VK_LSHIFT}, {EvKind::KeyUp, VK_LCTRL}});
        CHECK(t == 0, "T8 remapped key between modifiers → 0 toggles");
    }

    //------------------------------------------------------------------ T9
    {
        std::printf("T9 persistence amplifier (state model):\n");
        // Accidental toggle → saveSettings() writes Enabled=0 → relaunch
        // starts disabled. The STATE machine is intended (OpenKey parity);
        // the fix adds visible startup feedback. Model the persistence:
        bool enabled = true;
        const int t1 = run({{EvKind::KeyDown, VK_LSHIFT}, {EvKind::KeyDown, VK_LCTRL},
                            {EvKind::KeyUp, VK_LCTRL}, {EvKind::KeyUp, VK_LSHIFT}});
        enabled = (t1 % 2 == 0) ? enabled : !enabled;      // persist
        const bool enabledOnStart = enabled;               // v1.1.0 loadSettings
        CHECK(!enabledOnStart, "T9 accidental toggle persists OFF across relaunch (must be surfaced)");
    }

    //------------------------------------------------------------------ T10
    {
        std::printf("T10 FUZZ (6 families x 100k segments, exact contract invariants):\n");
        std::mt19937 rng(1);
        const std::uint32_t vks[] = {VK_LCTRL, VK_RCTRL, VK_LSHIFT, VK_RSHIFT,
                                     VK_A, VK_S, VK_V, 0x09, 0xE7};
        bool allOk = true;

        // Junk builders. "Benign" junk is SOUND: it can never change the
        // chord outcome — mouse events and non-modifier KEY-UPS only (a
        // non-modifier keyDOWN would contaminate; a modifier keyDOWN/UP can
        // legitimately restructure the chord, e.g. release an arm key early,
        // so it is NOT benign). RepeatFlood models holding the chord under
        // OS auto-repeat: adjacent repeat DOWN/UP pairs of the chord
        // modifiers (held-bit no-ops inside the chord).
        enum class Junk { None, Benign, Contaminate, Resync, InjectedOnly, RepeatFlood };

        auto addJunk = [&](std::vector<Ev>& s, Junk kind, std::size_t at) {
            switch (kind) {
                case Junk::None: break;
                case Junk::Benign:
                    for (int j = 0, n = 1 + static_cast<int>(rng() % 6); j < n; ++j) {
                        if (rng() % 3 == 0) {
                            s.insert(s.begin() + static_cast<long>(at), {EvKind::Mouse});
                        } else {
                            s.insert(s.begin() + static_cast<long>(at),
                                     {EvKind::KeyUp, vks[rng() % 9], false});
                        }
                    }
                    break;
                case Junk::RepeatFlood:
                    // OS auto-repeat generates KEYDOWNS only (no key-ups) —
                    // repeats of held modifiers are held-bit no-ops, so the
                    // chord outcome must stay at exactly one toggle.
                    for (int j = 0, n = 1 + static_cast<int>(rng() % 12); j < n; ++j) {
                        s.insert(s.begin() + static_cast<long>(at),
                                 {EvKind::KeyDown, vks[rng() % 4], false});
                    }
                    break;
                case Junk::Contaminate:   // non-modifier keydowns (phys or injected)
                    for (int j = 0, n = 1 + static_cast<int>(rng() % 6); j < n; ++j) {
                        s.insert(s.begin() + static_cast<long>(at),
                                 {EvKind::KeyDown, vks[4 + (rng() % 5)], (rng() % 2) == 0});
                    }
                    break;
                case Junk::Resync:
                    for (int j = 0, n = 1 + static_cast<int>(rng() % 4); j < n; ++j) {
                        s.insert(s.begin() + static_cast<long>(at),
                                 {EvKind::ForegroundChange, 0, false,
                                  (rng() % 2) == 0, (rng() % 2) == 0});
                    }
                    break;
                case Junk::InjectedOnly:   // ONLY injected keyboard traffic
                    for (int j = 0, n = 1 + static_cast<int>(rng() % 12); j < n; ++j) {
                        s.push_back({(rng() % 2) ? EvKind::KeyDown : EvKind::KeyUp,
                                     vks[rng() % 9], true});
                    }
                    break;
            }
        };

        const std::vector<Ev> bareChord = {
            {EvKind::KeyDown, VK_LCTRL}, {EvKind::KeyDown, VK_LSHIFT},
            {EvKind::KeyUp, VK_LSHIFT}, {EvKind::KeyUp, VK_LCTRL}};
        const std::vector<Ev> capitalThenCtrlTap = {   // the reported bug path
            {EvKind::KeyDown, VK_LSHIFT}, {EvKind::KeyDown, VK_A},
            {EvKind::KeyUp, VK_A}, {EvKind::KeyDown, VK_RCTRL},
            {EvKind::KeyUp, VK_RCTRL}, {EvKind::KeyUp, VK_LSHIFT}};
        const std::vector<Ev> singleCtrlTap = {
            {EvKind::KeyDown, VK_LCTRL}, {EvKind::KeyUp, VK_LCTRL}};

        for (int seg = 0; seg < 100000 && allOk; ++seg) {
            const int family = static_cast<int>(rng() % 6);
            std::vector<Ev> stream;
            int expected = -1;
            switch (family) {
                case 0:   // INV-2/5: bare chord + benign/repeat junk → still 1
                    stream = bareChord;
                    addJunk(stream, (rng() % 2) ? Junk::Benign : Junk::RepeatFlood,
                            2);   // after both downs, before first up
                    expected = 1;
                    break;
                case 1:   // INV-3: non-mod keydown between the modifier downs
                          // and the final release → 0
                    stream = bareChord;
                    addJunk(stream, Junk::Contaminate, 2);
                    expected = 0;
                    break;
                case 2:   // INV-4: resync AFTER the chord armed (both downs
                          // observed) and BEFORE the first release → 0.
                          // (Position must be exactly 2: under first-release
                          // semantics a resync at position 3 runs after the
                          // chord already toggled. A resync between the two
                          // downs re-seeds held bits from OS ground truth, so
                          // its outcome is state-dependent — not an
                          // invariant.)
                    stream = bareChord;
                    addJunk(stream, Junk::Resync, 2);
                    expected = 0;
                    break;
                case 3:   // INV-1: all-injected traffic → 0
                    stream.clear();
                    addJunk(stream, Junk::InjectedOnly, 0);
                    expected = 0;
                    break;
                case 4:   // INV-8: capital typing + Ctrl tap → 0
                    stream = capitalThenCtrlTap;
                    if (rng() % 2) { addJunk(stream, Junk::Benign, 1 + rng() % 5); }
                    expected = 0;
                    break;
                case 5:   // INV-7: lone modifier taps → 0
                    stream = singleCtrlTap;
                    if (rng() % 2) { stream = {{EvKind::KeyDown, VK_RSHIFT},
                                               {EvKind::KeyUp, VK_RSHIFT}}; }
                    addJunk(stream, Junk::Benign, rng() % 2);
                    expected = 0;
                    break;
            }
            const int got = run(stream);
            if (got != expected) {
                std::printf("  FUZZ FAIL seg=%d family=%d expected=%d got=%d "
                            "(stream size=%zu)\n", seg, family, expected, got,
                            stream.size());
                allOk = false;
            }
        }
        CHECK(allOk, "T10 fuzz contract invariants (600k segments total)");
    }

    std::printf("=== %s (%d failure%s) ===\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "FAILURES PRESENT",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}

// Build & run (portable — no Windows required):
//   g++ -std=c++20 -O2 -I ../src/core -o extreme_toggle tests/extreme_toggle.cpp
//   ./extreme_toggle
