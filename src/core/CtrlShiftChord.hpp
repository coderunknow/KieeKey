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
// File: src/core/CtrlShiftChord.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — CtrlShiftChord.hpp
//
// Pure state machine for the "Ctrl+Shift toggles Vietnamese input" global
// hotkey. It fires ONLY on a deliberate BARE chord: Ctrl+Shift pressed
// together and released WITHOUT any other key in between. Every Ctrl+Shift
// application shortcut (Ctrl+Shift+S, Ctrl+Shift+Tab, Ctrl+Shift+Esc,
// Ctrl+Shift+arrows for text selection, …) cancels the chord, so the IME
// never silently turns itself off while the user is selecting text or using
// normal shortcuts.
//
// v1.1.0 defect analysis (user report: "sometimes I type normally and the
// IME suddenly turns off for no reason"). Three real phantom-toggle paths
// existed in the previous state machine, all reproduced by the extreme
// test harness (scripts/extreme_toggle_tests.cpp):
//
//   P1 — the Dirty marker was ERASED by the same event that set it whenever
//        the second modifier was not yet held (`!bothHeld → state = None`
//        ran at the tail of onKey). Consequences, all verified:
//          * Shift↓ H↓ H↑ Ctrl↓ Ctrl↑ Shift↑ — typing a CAPITAL then
//            tapping Ctrl while Shift is still held → TOGGLED. This is the
//            everyday "typing normally and it turned off" report.
//          * Ctrl↓ S↓ Shift↓ Shift↑ (shortcut key between the modifier
//            presses) → still toggled despite the v1.1.0 audit fix, for
//            the same reason.
//   P2 — third-party INJECTED bare chords (RDP clients forwarding
//        keystrokes, key remappers, automation tools) armed and fired the
//        toggle; the user pressed nothing. The APP now filters injected
//        MODIFIER events before feeding this class (see main.cpp); the
//        class itself stays event-source agnostic. Injected NON-modifier
//        keydowns keep cancelling — a remapped key between the modifiers
//        is still "a key in between".
//   P3 — resync(ctrlDown, shiftDown) ARMED a Clean chord when the OS
//        reported both held (a chord that started BEFORE the resync, i.e.
//        one whose bare-ness this process never observed). Releasing then
//        toggled an unobserved chord. Resync now marks the chord Dirty:
//        only chords pressed AND released entirely after the resync can
//        toggle. A dropped deliberate toggle is invisible; a phantom
//        toggle is a bug — conservative wins.
//
// New model: contamination (any non-modifier keydown while any Ctrl/Shift
// is held) is CUMULATIVE and survives until BOTH modifiers are released —
// exactly the documented "no other key in between" contract, with no
// phase-dependent erasure hole.
//
// Thread affinity: the owning app feeds events in key order from its single
// hook thread; the class holds plain state and is NOT thread-safe.
//----------------------------------------------------------------------------
#pragma once

namespace ok::hotkey {

class CtrlShiftChord final {
public:
    enum class Action { None, Toggle };

    // Feed one keyboard event. Parameters:
    //   isCtrlModifier  — the event's vkCode is VK_LCONTROL/VK_RCONTROL
    //   isShiftModifier — the event's vkCode is VK_LSHIFT/VK_RSHIFT
    //   isNonModifierKeyDown — any other key went DOWN (contaminates a chord)
    //   keyDown         — the event itself is a key-down (modifier up →
    //                     keyDown == false)
    // Returns Action::Toggle exactly once per clean bare Ctrl+Shift chord:
    // both modifiers were pressed together, no other key went down while
    // either was held, and the chord was observed from its first press.
    // The toggle fires at the FIRST modifier release (the historical
    // behavior the project's test_hotfix contract pins down).
    [[nodiscard]] Action onKey(bool isCtrlModifier, bool isShiftModifier,
                               bool isNonModifierKeyDown, bool keyDown) noexcept {
        if (isCtrlModifier)  { ctrlHeld_  = keyDown; }
        if (isShiftModifier) { shiftHeld_ = keyDown; }

        if (isNonModifierKeyDown && (ctrlHeld_ || shiftHeld_)) {
            // A real (half-)chord shortcut. Cumulative: the mark survives
            // until the chord ends, so contaminations during the
            // single-modifier phase (capital typing, abandoned shortcuts)
            // can never be forgotten the way the per-event state reset used
            // to forget them (P1).
            chordDirty_ = true;
        }

        const bool bothHeld = ctrlHeld_ && shiftHeld_;
        const bool anyHeld  = ctrlHeld_ || shiftHeld_;

        if (bothHeld) {
            chordLive_ = true;              // chord armed (Clean or Dirty —
            return Action::None;            // decided at the first release)
        }
        if (chordLive_) {                   // a modifier was released: the
            chordLive_ = false;             // chord ends HERE (first release)
            const bool wasClean = !chordDirty_;
            chordDirty_ = false;
            return wasClean ? Action::Toggle : Action::None;
        }
        if (!anyHeld) {
            chordDirty_ = false;            // idle again — fresh start
        }
        return Action::None;
    }

    // v1.1.0 — hard resync from the OS key state. The chord tracker is fed
    // purely from the event stream, so a missed Shift/Ctrl KeyUp (LL-hook
    // timeout removal, UIPI transition at an elevated window, RDP session
    // switch) left a permanently "held" modifier — the next unrelated
    // Ctrl press+release then fired Toggle and silently switched the IME
    // off mid-work. The app calls this on every foreground change with the
    // GetAsyncKeyState snapshot.
    //
    // v1.1.0 (P3 fix): a chord whose start predates this resync can never
    // be vouched for — the OS snapshot says both modifiers are held, but
    // whether they were pressed BARE is unobservable. Arming it Clean
    // (the old behavior) toggled chords we never saw start; mark it Dirty
    // instead so the pending release cannot fire. Only chords pressed AND
    // released entirely after this point can toggle.
    void resync(bool ctrlDown, bool shiftDown) noexcept {
        ctrlHeld_  = ctrlDown;
        shiftHeld_ = shiftDown;
        if (ctrlDown && shiftDown) {
            chordLive_  = true;
            chordDirty_ = true;             // unobserved start → never toggles
        } else {
            chordLive_  = false;
            chordDirty_ = false;
        }
    }

private:
    bool ctrlHeld_    = false;   // tracked Ctrl hold state
    bool shiftHeld_   = false;   // tracked Shift hold state
    bool chordLive_   = false;   // both modifiers were held together; the
                                 // chord ends when BOTH are released
    bool chordDirty_  = false;   // a non-modifier key went down while any
                                 // Ctrl/Shift was held (cumulative; cleared
                                 // only by a full release / resync)
};

} // namespace ok::hotkey
