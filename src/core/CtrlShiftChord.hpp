//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.0.2 - refactored and completed logic
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
    [[nodiscard]] Action onKey(bool isCtrlModifier, bool isShiftModifier,
                               bool isNonModifierKeyDown, bool keyDown) noexcept {
        if (isCtrlModifier)  { ctrlHeld_  = keyDown; }
        if (isShiftModifier) { shiftHeld_ = keyDown; }

        if (isNonModifierKeyDown && ctrlHeld_ && shiftHeld_) {
            state_ = State::Dirty;          // a real Ctrl+Shift shortcut
        }

        const bool bothHeld = ctrlHeld_ && shiftHeld_;
        if (bothHeld && state_ == State::None) {
            state_ = State::Clean;          // chord started
        }
        if (!bothHeld) {
            if (state_ == State::Clean) {   // bare chord finished -> toggle
                state_ = State::None;
                return Action::Toggle;
            }
            state_ = State::None;
        }
        return Action::None;
    }

private:
    enum class State { None, Clean, Dirty };
    bool  ctrlHeld_  = false;
    bool  shiftHeld_ = false;
    State state_     = State::None;
};

} // namespace ok::hotkey
