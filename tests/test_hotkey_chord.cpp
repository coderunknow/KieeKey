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
// File: tests/test_hotkey_chord.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.0 — test_hotkey_chord.cpp
// Unit tests for the Ctrl+Shift "toggle Vietnamese input" chord state machine
// (src/core/CtrlShiftChord.hpp).
//
// The chord is the IME's only global hotkey, and a false positive is the
// single most disruptive bug this app can have: the user is typing, the IME
// silently turns itself off, and every following word comes out as bare
// ASCII. These tests pin down every sequence that must NOT toggle (real
// Ctrl+Shift application shortcuts, Ctrl combos that run into a Shift
// release, leftover modifier state after a focus change) alongside the ones
// that must.
//
// The header is platform-independent (no Win32 includes), so this suite runs
// on every host — including the non-Windows CI/dev shim build.
//----------------------------------------------------------------------------
#include "CtrlShiftChord.hpp"

#include <cstdio>

namespace {

int failures = 0;

// Plain `if` — `if constexpr` here would demand a constant expression and
// break every runtime CHECK (ill-formed; MSVC /permissive- and GCC both reject
// it). Constant-condition CHECKs are intentional regression nets; the MSVC
// C4127 noise they cause is disabled project-wide (/wd4127).
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    ++failures; } } while (0)

using ok::hotkey::CtrlShiftChord;

//---------------------------------------------------------------------------
// Tiny event DSL so each test case reads like the actual key sequence.
//   ctrl(true/false)  -> Ctrl down / up
//   shift(...)        -> Shift down / up
//   key()             -> any non-modifier key press (down + up folded)
// Returns true when the sequence toggled at least once, and (optionally) the
// total number of toggles.
//---------------------------------------------------------------------------
class Feeder {
public:
    explicit Feeder(CtrlShiftChord& c) noexcept : c_(c) {}

    Feeder& ctrl(bool down) noexcept {
        step(true, false, false, down);
        return *this;
    }
    Feeder& shift(bool down) noexcept {
        step(false, true, false, down);
        return *this;
    }
    // One non-modifier key: down (the press that "dirties" a chord) + up.
    Feeder& key() noexcept {
        step(false, false, true, true);
        step(false, false, false, false);
        return *this;
    }
    Feeder& reset() noexcept { c_.reset(); return *this; }

    [[nodiscard]] int toggles() const noexcept { return toggles_; }

private:
    void step(bool isCtrl, bool isShift, bool nonModDown, bool down) noexcept {
        if (c_.onKey(isCtrl, isShift, nonModDown, down) == CtrlShiftChord::Action::Toggle) {
            ++toggles_;
        }
    }
    CtrlShiftChord& c_;
    int toggles_ = 0;
};

} // namespace

int main() {
    // ---- 1. The canonical bare chord toggles exactly once -----------------
    {
        CtrlShiftChord c;
        Feeder f(c);
        f.ctrl(true).shift(true).shift(false).ctrl(false);
        CHECK(f.toggles() == 1);
    }
    {   // reversed release order
        CtrlShiftChord c;
        Feeder f(c);
        f.ctrl(true).shift(true).ctrl(false).shift(false);
        CHECK(f.toggles() == 1);
    }
    {   // Shift first
        CtrlShiftChord c;
        Feeder f(c);
        f.shift(true).ctrl(true).ctrl(false).shift(false);
        CHECK(f.toggles() == 1);
    }
    {   // releasing one modifier does not resolve the chord; the LAST one does
        CtrlShiftChord c;
        Feeder f(c);
        f.ctrl(true).shift(true);
        CHECK(f.toggles() == 0);
        f.ctrl(false);                  // Ctrl first — Shift is still down
        CHECK(f.toggles() == 0);
        f.shift(false);                 // last modifier up -> resolves
        CHECK(f.toggles() == 1);
        // ...and it resolves exactly once (a second release is a no-op)
        f.shift(false).ctrl(false);
        CHECK(f.toggles() == 1);
    }

    // ---- 2. Real Ctrl+Shift application shortcuts must NOT toggle ---------
    {
        CtrlShiftChord c;
        Feeder f(c);
        f.ctrl(true).shift(true).key().shift(false).ctrl(false);   // Ctrl+Shift+S
        CHECK(f.toggles() == 0);
    }
    {   // Ctrl+Shift+Tab (and every Ctrl+Shift+arrow selection combo)
        CtrlShiftChord c;
        Feeder f(c);
        f.ctrl(true).shift(true).key().key().shift(false).ctrl(false);
        CHECK(f.toggles() == 0);
        // ...and the next deliberate chord still works (state was cleaned up)
        f.ctrl(true).shift(true).shift(false).ctrl(false);
        CHECK(f.toggles() == 1);
    }

    // ---- 3. v1.1.0: a plain Ctrl combo that runs into a Shift release ----
    //      Ctrl+X, then Shift down, then release both == leftover Ctrl chord,
    //      NOT a deliberate Ctrl+Shift. This toggled the IME before v1.1.0.
    {
        CtrlShiftChord c;
        Feeder f(c);
        f.ctrl(true).key().shift(true).shift(false).ctrl(false);
        CHECK(f.toggles() == 0);
    }
    {   // mirrored: Shift+A to select text, then Ctrl, then release both
        CtrlShiftChord c;
        Feeder f(c);
        f.shift(true).key().ctrl(true).ctrl(false).shift(false);
        CHECK(f.toggles() == 0);
    }
    {   // typing (no modifiers) between two chords must not poison them
        CtrlShiftChord c;
        Feeder f(c);
        f.key().key().key();
        CHECK(f.toggles() == 0);
        f.ctrl(true).shift(true).shift(false).ctrl(false);
        CHECK(f.toggles() == 1);
    }

    // ---- 4. v1.1.0: stuck modifier state is recoverable -------------------
    {
        CtrlShiftChord c;
        Feeder f(c);
        f.ctrl(true);                    // Ctrl down ...
        // ... focus is stolen (Alt+Tab / UAC / RDP): the Ctrl-up never arrives.
        CHECK(c.ctrlHeld());
        f.reset();                       // app: foreground changed -> reset
        CHECK(!c.ctrlHeld());
        CHECK(!c.shiftHeld());
        // The stale "Ctrl held" would have made the next bare Shift press+release
        // complete a phantom chord:
        f.shift(true).shift(false);
        CHECK(f.toggles() == 0);
        // A real chord still works after the reset.
        f.ctrl(true).shift(true).shift(false).ctrl(false);
        CHECK(f.toggles() == 1);
    }
    {   // reset() mid-chord: the half-finished chord must not fire later
        CtrlShiftChord c;
        Feeder f(c);
        f.ctrl(true).shift(true);
        f.reset();
        f.ctrl(false).shift(false);
        CHECK(f.toggles() == 0);
    }
    {   // reset() is idempotent
        CtrlShiftChord c;
        Feeder f(c);
        f.reset().reset().reset();
        CHECK(f.toggles() == 0);
        f.ctrl(true).shift(true).shift(false).ctrl(false);
        CHECK(f.toggles() == 1);
    }

    // ---- 5. Chords are repeatable, never sticky ---------------------------
    {
        CtrlShiftChord c;
        Feeder f(c);
        for (int i = 1; i <= 5; ++i) {
            f.ctrl(true).shift(true).shift(false).ctrl(false);
            CHECK(f.toggles() == i);
        }
    }

    // ---- 6. Modifier key-up without a matching key-down -------------------
    //      (hook installed while the user was already holding Ctrl)
    {
        CtrlShiftChord c;
        Feeder f(c);
        f.ctrl(false).shift(false);
        CHECK(f.toggles() == 0);
        f.ctrl(true).shift(true).shift(false).ctrl(false);
        CHECK(f.toggles() == 1);
    }

    if (failures == 0) {
        std::printf("ALL HOTKEY-CHORD TESTS PASSED\n");
        return 0;
    }
    std::printf("%d HOTKEY-CHORD TEST(S) FAILED\n", failures);
    return 1;
}
