//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: src/app/ChaosLabWindow.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — ChaosLabWindow.hpp
// Dedicated test surface for the two "chaos" features the user asked to be
// playable *outside* the app as well:
//
//   * Chaos Case / glyph transforms — type into this window, see the exact
//     bytes KieeKey would emit, and (optionally) write them into whatever
//     application currently has the focus.
//   * Flexing Mode — the same window, with the flexing game driving the text.
//
// The window is deliberately separate from the settings dialog: it is a
// keyboard playground, so the user can compare "what I typed" with "what the
// engine produced" character by character, with the transform knobs live.
//----------------------------------------------------------------------------
#pragma once

#include <string>

namespace ok::app {

class ChaosLabWindow {
public:
    static constexpr int kTimerId = 0xC4A0;
    static constexpr int kTimerIntervalMs = 50;

    static ChaosLabWindow& instance() noexcept;

    // Creates the window (or focuses it) owned by `owner`.
    bool open(void* owner);

    void close();
    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] void* handle() const noexcept;

    // Applies the current chaos configuration to `input` (exposed for tests:
    // the window itself only moves text between controls).
    [[nodiscard]] static std::u32string transformForPreview(std::u32string_view input,
                                                            std::uint32_t seed);

    // Host hook that writes text into the focused application. main.cpp wraps
    // the wrapper's inline emitter here, so the lab never needs to know how the
    // text is delivered (SendInput today, TSF when the target allows it).
    using EmitCallback = std::size_t (*)(const std::wstring& text);
    static void setEmitCallback(EmitCallback callback) noexcept;
    [[nodiscard]] static EmitCallback emitCallback() noexcept;

    // Implementation state. Public (but opaque) because the window procedure
    // and the control helpers live outside the class.
    struct Impl;

private:
    ChaosLabWindow();
    ~ChaosLabWindow() = default;
    ChaosLabWindow(const ChaosLabWindow&) = delete;
    ChaosLabWindow& operator=(const ChaosLabWindow&) = delete;

    Impl* m_impl = nullptr;
};

} // namespace ok::app
