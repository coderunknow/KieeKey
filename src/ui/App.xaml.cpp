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
// File: src/ui/App.xaml.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.0 — App.xaml.cpp
// WinUI 3 application entry point. Creates the MainWindow and keeps a
// process-wide TextEngine instance (the actual engine runs on the hook's
// consumer thread inside MainWindow, which owns the ModernKeyHook).
//
// Note: this file requires the Windows App SDK toolchain (VS 2022 +
// Microsoft.WindowsAppSDK nuget). It is not part of the MinGW cross-build;
// see build/CMakeLists.txt target `kieekey_winui3`.
//----------------------------------------------------------------------------
#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#if __has_include("App.g.cpp")
#include "App.g.cpp"
#endif

namespace winrt {
using namespace Microsoft::UI::Xaml;
}

namespace KieeKey {

App::App() {
    // XamlControlsResources is merged by App.xaml; this is the standard hook
    // for WinUI 3 apps that own the lifetime of the Application object.
    InitializeComponent();

#if defined _DEBUG
    // Debug builds do not ship; fail fast on unhandled exceptions.
    UnhandledException([](IInspectable const&, Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& e) {
        if (IsDebuggerPresent()) {
            auto msg = e.Message();
            __debugbreak();
        }
    });
#endif
}

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
    m_window = make<MainWindow>();
    m_window.Activate();
}

} // namespace KieeKey
