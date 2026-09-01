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
    //   isNonModifierKeyDown — any other key went DOWN (cancels a chord)
    //   keyDown         — the event itself is a key-down (modifier up →
    //                     keyDown == false)
    // Returns Action::Toggle exactly once per clean bare Ctrl+Shift chord.
    //
    // Resolution model (v1.1.0): a chord is evaluated at a *cycle boundary* --
    // the transition from "at least one of Ctrl/Shift is held" to "neither is
    // held". Releasing Ctrl first and Shift a second later therefore resolves
    // once, when the last modifier comes up, instead of at the first release.
    [[nodiscard]] Action onKey(bool isCtrlModifier, bool isShiftModifier,
                               bool isNonModifierKeyDown, bool keyDown) noexcept {
        const bool prevAnyHeld = ctrlHeld_ || shiftHeld_;
        if (isCtrlModifier)  { ctrlHeld_  = keyDown; }
        if (isShiftModifier) { shiftHeld_ = keyDown; }

        const bool anyHeld  = ctrlHeld_ || shiftHeld_;
        const bool bothHeld = ctrlHeld_ && shiftHeld_;

        // v1.1.0: a non-modifier key pressed while EITHER modifier is held
        // marks the cycle dirty, not just while BOTH are held. The old rule
        // let this sequence toggle the IME by accident:
        //     Ctrl down -> X down -> X up -> Shift down -> release both
        // i.e. "Ctrl+X, then Shift, then let go" is a leftover Ctrl combo, not
        // a deliberate Ctrl+Shift. Because the flag is sticky until the cycle
        // ends, a shortcut that starts on Ctrl and finishes with Shift held
        // (very common: Ctrl+Shift+arrow text selection) can never slip
        // through as a bare chord either.
        if (isNonModifierKeyDown && anyHeld) {
            dirty_ = true;
        }
        if (bothHeld && !dirty_) {
            cleanSeen_ = true;          // a genuine Ctrl+Shift was held together
        }

        // Cycle boundary — the only moment a chord can resolve.
        if (prevAnyHeld && !anyHeld) {
            const bool toggle = cleanSeen_ && !dirty_;
            cleanSeen_ = false;
            dirty_     = false;
            return toggle ? Action::Toggle : Action::None;
        }
        return Action::None;
    }

    // v1.1.0: drop all chord state (modifier tracking included).
    //
    // Edge case this fixes: the low-level hook only sees key-up events that
    // are actually delivered to it. Release Ctrl or Shift while another window
    // has focus (Alt+Tab away, a UAC/consent prompt steals focus, an RDP
    // session grabs the keyboard, the self-healing watchdog reinstalls the
    // hook) and the matching key-up never reaches us -- ctrlHeld_/shiftHeld_
    // stay stuck ON. The next bare Shift or Ctrl press+release then completes
    // a "chord" that was never started and silently switches the IME off in
    // the middle of a sentence.
    //
    // Call it whenever the keyboard context is known to have changed:
    // foreground-window changes and hook re-installs.
    void reset() noexcept {
        ctrlHeld_   = false;
        shiftHeld_  = false;
        cleanSeen_  = false;
        dirty_      = false;
    }

    // Diagnostics / test hook.
    [[nodiscard]] bool ctrlHeld()  const noexcept { return ctrlHeld_; }
    [[nodiscard]] bool shiftHeld() const noexcept { return shiftHeld_; }

private:
    bool ctrlHeld_  = false;   // Ctrl is currently down (as seen by the hook)
    bool shiftHeld_ = false;   // Shift is currently down
    bool cleanSeen_ = false;   // Ctrl+Shift were held together this cycle
    bool dirty_     = false;   // another key was pressed inside this cycle
};

} // namespace ok::hotkey
