//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.0 - refactored and completed logic
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
// File: src/ui/MainWindow.xaml.h
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.0 — MainWindow.xaml.h
// Hand-written part of the MainWindow partial class (the XAML compiler
// generates MainWindow.g.h — included below — with the x:Name'd controls and
// event-handler stubs).
//----------------------------------------------------------------------------
#pragma once

#include "MainWindow.g.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "ModernKeyHook.hpp"
#include "ProcessMonitor.hpp"
#include "TextEngine.hpp"
#include "TsfComposer.hpp"

namespace KieeKey {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
    ~MainWindow();

private:
    friend class MainWindowT<MainWindow>;   // generated code may call private members

    void startEngine();
    void stopEngine() noexcept;

    void commit(const std::shared_ptr<ok::text::TextEngine>& engine,
                const std::shared_ptr<ok::tsf::TsfComposer>& composer,
                const ok::text::TextInput& in,
                const ok::text::EngineResult& r) noexcept;
    static bool isCaretEditVk(std::uint32_t vk, bool ctrl) noexcept;
    void resyncFromContext(const std::shared_ptr<ok::text::TextEngine>& engine,
                           const std::shared_ptr<ok::tsf::TsfComposer>& composer) noexcept;
    static char32_t produceChar(std::uint32_t vk, bool shift, bool capsLock) noexcept;
    static bool isWordBreakVk(std::uint32_t vk) noexcept;
    static bool isModifierVk(std::uint32_t vk) noexcept;
    static void sendBackspaces(std::size_t n) noexcept;
    static void sendUnicodeText(const std::wstring& text) noexcept;

    // Owned pipeline (the console app in src/app/main.cpp shares this design).
    std::shared_ptr<ok::text::TextEngine>     m_engine;
    std::shared_ptr<ok::hook::ModernKeyHook>  m_hook;
    std::shared_ptr<ok::monitor::ProcessMonitor> m_monitor;
    std::shared_ptr<ok::tsf::TsfComposer>     m_composer;

    // 1 Hz telemetry pump.
    winrt::Microsoft::UI::Xaml::DispatcherTimer m_timer{nullptr};

    bool m_running = false;
};

} // namespace KieeKey
