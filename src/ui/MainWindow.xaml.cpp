//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.1.1 - refactored and completed logic
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
// File: src/ui/MainWindow.xaml.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.1 — MainWindow.xaml.cpp
// WinUI 3 settings surface. Owns the SAME production pipeline as the console
// app (src/app/main.cpp): ModernKeyHook → TextEngine → TsfComposer, with
// ProcessMonitor auto-exclusion. Toggling "Bật gõ tiếng Việt" starts/stops
// the hook; the telemetry panel is fed by a DispatcherTimer reading the
// hook's zero-cost atomic counters.
//
// Requires: Windows App SDK 1.5+ (see build/CMakeLists.txt target kieekey_winui3).
//----------------------------------------------------------------------------
#include "pch.h"
#include "MainWindow.xaml.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <chrono>

namespace winrt {
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Foundation;
}

namespace KieeKey {

namespace {
// Same registry key as v2.0.5 (shared with the console app).
inline ok::win32::RegistryKey settingsKey() {
    return ok::win32::RegistryKey::openAppKey(true);
}
} // namespace

MainWindow::MainWindow() {
    InitializeComponent();

    Title(L"KieeKey — Bộ gõ Tiếng Việt");

    // ---- load persisted settings into the controls ----
    if (auto key = settingsKey(); key) {
        const DWORD method = key.getDword(L"InputMethod", 0);
        if (method <= 2) { InputMethodBox().SelectedIndex(static_cast<int>(method)); }
        const DWORD table = key.getDword(L"CodeTable", 0);
        if (table <= 4) { CodeTableBox().SelectedIndex(static_cast<int>(table)); }
        ModernOrthography().IsChecked(key.getDword(L"ModernOrthography", 0) != 0);
        QuickTelex().IsChecked(key.getDword(L"QuickTelex", 0) != 0);
        RestoreIfWrong().IsChecked(key.getDword(L"RestoreIfWrong", 1) != 0);
        UpperCaseFirst().IsChecked(key.getDword(L"UpperCaseFirst", 0) != 0);
        ExcludeIde().IsChecked(key.getDword(L"ExcludeIde", 1) != 0);
        ExcludeFullscreen().IsChecked(key.getDword(L"ExcludeFullscreen", 1) != 0);
        ExcludeShell().IsChecked(key.getDword(L"ExcludeShell", 0) != 0);
        SmartSwitch().IsChecked(key.getDword(L"SmartSwitch", 1) != 0);
        UseMacro().IsChecked(key.getDword(L"UseMacro", 1) != 0);
    }

    // ---- telemetry timer (1 Hz; reads atomic counters, zero allocations) ----
    m_timer = DispatcherTimer();
    m_timer.Interval(std::chrono::milliseconds(1000));
    m_timer.Tick({ this, &MainWindow::OnTelemetryTick });
    m_timer.Start();

    // The engine pipeline is attached lazily on first toggle (the consumer
    // thread of ModernKeyHook owns TextEngine/TsfComposer — COM apartment
    // affinity handled in the same thread).
    m_engine = std::make_shared<ok::text::TextEngine>();
    m_hook   = std::make_shared<ok::hook::ModernKeyHook>();
    m_monitor = std::make_shared<ok::monitor::ProcessMonitor>();
    m_composer = std::make_shared<ok::tsf::TsfComposer>();
}

MainWindow::~MainWindow() {
    stopEngine();
}

//---------------------------------------------------------------------------
// Engine start/stop
//---------------------------------------------------------------------------
void MainWindow::startEngine() {
    if (m_running) { return; }

    (void)m_monitor->start();
    (void)m_composer->attach();     // CoInitializeEx + ActivateEx on UI thread

    // Capture the pipeline pieces (shared_ptr copies keep them alive across
    // the hook's threads). Raw `this` is safe: ~MainWindow calls stopEngine(),
    // whose ModernKeyHook::stop() joins the consumer thread before returning.
    const auto engine   = m_engine;
    const auto monitor  = m_monitor;
    const auto composer = m_composer;

    m_running = m_hook->start([this, engine, monitor, composer](const ok::hook::KeyEvent& ev) {
        // Runs on the hook's consumer thread — must never block.
        if (ev.source == ok::hook::EventSource::ForegroundChanged) {
            monitor->refreshNow();
            composer->onForegroundChanged();
            if (auto s = monitor->snapshot(); s && s->autoExcluded()) {
                engine->startNewSession();
            }
            return;
        }
        if (ev.source == ok::hook::EventSource::Mouse) {
            ok::text::TextInput in;
            in.kind = ok::text::InputKind::MouseDown;
            commit(engine, composer, in, engine->process(in));
            // Caret jumped (click into text): re-sync the engine to the
            // visible word so retyping composes onto it ("chugsn" -> select/
            // delete "gsn" -> click -> "sng" must become "chúng", not "chusng").
            resyncFromContext(engine, composer);
            return;
        }
        if (ev.action == ok::hook::KeyAction::KeyUp ||
            ev.action == ok::hook::KeyAction::SysKeyUp) return;
        if (ev.injected || ev.vkCode == 0) return;
        if (isModifierVk(ev.vkCode)) return;
        if (monitor->currentAppAutoExcluded()) { return; }

        const bool shift  = ev.modifiers.shift;
        const bool capsOn = ev.modifiers.caps;
        const bool ctrl   = ev.modifiers.ctrl;
        const bool alt    = ev.modifiers.alt;
        const bool isCaps = shift != capsOn;

        // Caret/text edited outside the engine (nav & edit keys): queue a
        // re-sync so the next keystroke composes onto the visible word.
        if (isCaretEditVk(ev.vkCode, ctrl)) {
            resyncFromContext(engine, composer);
        }

        ok::text::TextInput in;
        in.isCaps    = isCaps;
        in.otherCtrl = ctrl || alt;

        if (ev.vkCode == VK_SPACE) {
            in.kind = ok::text::InputKind::Space;
        } else if (ev.vkCode == VK_BACK) {
            in.kind = ok::text::InputKind::Backspace;
        } else if (isWordBreakVk(ev.vkCode)) {
            in.kind   = ok::text::InputKind::WordBreak;
            in.vkCode = static_cast<std::uint16_t>(ev.vkCode);
        } else {
            const char32_t ch = produceChar(ev.vkCode, shift, capsOn);
            if (ch == 0) { return; }
            in.kind = ok::text::InputKind::Char;
            in.ch   = ch;
        }
        commit(engine, composer, in, engine->process(in));
    });

    if (m_running) {
        StatusText().Text(L"Đang chạy — bộ gõ đã kích hoạt");
    } else {
        StatusText().Text(L"Không thể cài hook (chạy với quyền cao hơn?)");
    }
}

void MainWindow::stopEngine() noexcept {
    if (!m_running) { return; }
    m_running = false;
    if (m_hook)   { m_hook->stop(); }
    if (m_monitor) { m_monitor->stop(); }
    if (m_composer) { m_composer->detach(); }
}

//---------------------------------------------------------------------------
// Pipeline helpers (mirrors src/app/main.cpp; kept in one place here)
//---------------------------------------------------------------------------
// True for keys that move the caret or edit text WITHOUT the engine being
// fed the change (arrows, Delete, Home/End/PgUp/PgDn, Ctrl+Backspace,
// Ctrl+arrows). These desync the engine's raw word buffer from the visible
// text, so a context re-sync is performed before the next keystroke.
bool MainWindow::isCaretEditVk(std::uint32_t vk, bool ctrl) noexcept {
    switch (vk) {
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
        case VK_DELETE: case VK_HOME: case VK_END:
        case VK_PRIOR: case VK_NEXT: case VK_INSERT:
            return true;
        case VK_BACK:
            return ctrl;                    // Ctrl+Backspace deletes a word
        default:
            return false;
    }
}

// Re-sync the engine to the visible word before the caret (after the user
// clicked into text or edited it with nav/delete keys). Read the raw word
// via TSF and quietly replay it into the engine so the next keystroke
// composes onto it. Runs on the consumer thread (TSF-affine).
void MainWindow::resyncFromContext(const std::shared_ptr<ok::text::TextEngine>& engine,
                                   const std::shared_ptr<ok::tsf::TsfComposer>& composer) noexcept {
    std::wstring word;
    if (composer->textBeforeCaret(word)) {
        static_cast<void>(engine->resumeFromText(word));
    }
}

void MainWindow::commit(const std::shared_ptr<ok::text::TextEngine>& engine,
                        const std::shared_ptr<ok::tsf::TsfComposer>& composer,
                        const ok::text::TextInput& in,
                        const ok::text::EngineResult& r) noexcept {
    if (!r.consumed()) { return; }
    if (r.code == ok::text::EngineCode::ReplaceMacro) { return; }
    const std::wstring rep = engine->replacementUtf16(r);
    const std::size_t  bs  = r.backspaceCount;
    if (bs == 0 && rep.empty()) { return; }
    if (!composer->commit(bs, rep)) {
        // Last-resort fallback (no synthetic backspaces by default).
        sendBackspaces(bs);
        sendUnicodeText(rep);
    }
}

char32_t MainWindow::produceChar(std::uint32_t vk, bool shift, bool capsLock) noexcept {
    if (vk >= 'A' && vk <= 'Z') {
        const bool upper = shift != capsLock;
        return static_cast<char32_t>(upper ? vk : (vk + ('a' - 'A')));
    }
    if (vk >= '0' && vk <= '9') {
        static constexpr char32_t kShifted[] = {')','!','@','#','$','%','^','&','*','('};
        return shift ? kShifted[vk - '0'] : static_cast<char32_t>(vk);
    }
    switch (vk) {
        case VK_OEM_1:   return shift ? U':' : U';';
        case VK_OEM_PLUS:   return shift ? U'+' : U'=';
        case VK_OEM_COMMA:  return shift ? U'<' : U',';
        case VK_OEM_MINUS:  return shift ? U'_' : U'-';
        case VK_OEM_PERIOD: return shift ? U'>' : U'.';
        case VK_OEM_2:      return shift ? U'?' : U'/';
        case VK_OEM_3:      return shift ? U'~' : U'`';
        case VK_OEM_4:      return shift ? U'{' : U'[';
        case VK_OEM_5:      return shift ? U'|' : U'\\';
        case VK_OEM_6:      return shift ? U'}' : U']';
        case VK_OEM_7:      return shift ? U'"' : U'\'';
        case VK_OEM_102:    return shift ? U'|' : U'\\';
        default:            return 0;
    }
}

bool MainWindow::isWordBreakVk(std::uint32_t vk) noexcept {
    switch (vk) {
        case 0x1B: case 0x09: case 0x0D:
        case 0x25: case 0x26: case 0x27: case 0x28:
        case 0x24: case 0x23: case 0x2D: case 0x2E:
        case 0x21: case 0x22:
        case 0x2C: case 0x2A: case 0x29: case 0x2F:
        case 0x2B: case 0x90: case 0x91:
            return true;
        default: return false;
    }
}

bool MainWindow::isModifierVk(std::uint32_t vk) noexcept {
    switch (vk) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN:
        case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
        case VK_PACKET:
            return true;
        default: return false;
    }
}

void MainWindow::sendBackspaces(std::size_t n) noexcept {
    std::vector<INPUT> inputs;
    inputs.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        INPUT ki{};
        ki.type = INPUT_KEYBOARD;
        ki.ki.wVk = VK_BACK;
        ki.ki.dwExtraInfo = ok::hook::kSelfInjectedExtraInfo;
        inputs.push_back(ki);
        ki.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(ki);
    }
    if (!inputs.empty()) {
        ::SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }
}

void MainWindow::sendUnicodeText(const std::wstring& text) noexcept {
    for (wchar_t ch : text) {
        INPUT key{};
        key.type = INPUT_KEYBOARD;
        key.ki.wVk = 0;
        key.ki.wScan = static_cast<WORD>(ch);
        key.ki.dwFlags = KEYEVENTF_UNICODE;
        key.ki.dwExtraInfo = ok::hook::kSelfInjectedExtraInfo;
        INPUT up = key;
        up.ki.dwFlags |= KEYEVENTF_KEYUP;
        INPUT inputs[] = { key, up };
        ::SendInput(2, inputs, sizeof(INPUT));
    }
}

//---------------------------------------------------------------------------
// UI event handlers
//---------------------------------------------------------------------------
void MainWindow::OnEngineToggled(IInspectable const&, RoutedEventArgs const&) {
    if (EngineToggle().IsOn()) { startEngine(); } else { stopEngine(); }
}

void MainWindow::OnInputMethodChanged(IInspectable const&, SelectionChangedEventArgs const&) {
    const int idx = InputMethodBox().SelectedIndex();
    if (idx < 0) { return; }
    if (auto e = m_engine) {
        auto opts = e->options();
        opts.inputMethod = static_cast<ok::text::InputMethod>(idx);
        e->setOptions(opts);
        e->startNewSession();
    }
    if (auto key = settingsKey(); key) {
        key.setDword(L"InputMethod", static_cast<DWORD>(idx));
    }
}

void MainWindow::OnCodeTableChanged(IInspectable const&, SelectionChangedEventArgs const&) {
    const int idx = CodeTableBox().SelectedIndex();
    if (idx < 0) { return; }
    if (auto e = m_engine) {
        auto opts = e->options();
        opts.codeTable = static_cast<ok::text::CodeTable>(idx);
        e->setOptions(opts);
    }
    if (auto key = settingsKey(); key) {
        key.setDword(L"CodeTable", static_cast<DWORD>(idx));
    }
}

void MainWindow::OnOptionChanged(IInspectable const&, RoutedEventArgs const&) {
    if (!m_engine) { return; }
    auto opts = m_engine->options();
    opts.useModernOrthography = ModernOrthography().IsChecked().value_or(false);
    opts.quickTelex           = QuickTelex().IsChecked().value_or(false);
    opts.restoreIfWrongSpelling = RestoreIfWrong().IsChecked().value_or(true);
    opts.upperCaseFirstChar   = UpperCaseFirst().IsChecked().value_or(false);
    opts.useMacro             = UseMacro().IsChecked().value_or(true);
    m_engine->setOptions(opts);
    m_engine->startNewSession();

    if (auto key = settingsKey(); key) {
        key.setDword(L"ModernOrthography", opts.useModernOrthography ? 1 : 0);
        key.setDword(L"QuickTelex",        opts.quickTelex ? 1 : 0);
        key.setDword(L"RestoreIfWrong",    opts.restoreIfWrongSpelling ? 1 : 0);
        key.setDword(L"UpperCaseFirst",    opts.upperCaseFirstChar ? 1 : 0);
        key.setDword(L"UseMacro",          opts.useMacro ? 1 : 0);
    }
    // Exclusion flags are consumed by ProcessMonitor at refresh time; poke it.
    m_monitor->refreshNow();
}

void MainWindow::OnHardwareLevelToggled(IInspectable const&, RoutedEventArgs const&) {
    // Hardware (WH_KEYBOARD_LL) is the only mode; the legacy
    // WH_KEYBOARD journal hook was removed. Keep the toggle for UI parity.
    if (!HardwareLevel().IsOn()) {
        HardwareLevel().IsOn(true);   // cannot disable in this build
        StatusText().Text(L"KieeKey chỉ hoạt động ở chế độ hook cấp thấp (WH_KEYBOARD_LL)");
    }
}

void MainWindow::OnTelemetryTick(winrt::Microsoft::UI::Xaml::DispatcherTimer const&,
                                 winrt::Windows::Foundation::IInspectable const&) {
    if (!m_hook) { return; }
    LatencyValue().Text(winrt::to_hstring(m_hook->peakLatencyUs()));
    PushedValue().Text(winrt::to_hstring(m_hook->pushed()));
    DroppedValue().Text(winrt::to_hstring(m_hook->dropped()));

    if (auto s = m_monitor->snapshot(); s) {
        std::wstring exe = s->exePath.substr(s->exePath.find_last_of(L'\\') + 1);
        StatusText().Text(winrt::hstring(exe) + (s->autoExcluded() ? L" (đã loại trừ)" : L""));
    }
}

void MainWindow::OnManageMacros(IInspectable const&, RoutedEventArgs const&) {
    StatusText().Text(L"Gõ tắt (macro): bảng macro sẽ được tích hợp ở bản sau");
}

void MainWindow::OnAdvancedSettings(IInspectable const&, RoutedEventArgs const&) {
    StatusText().Text(L"Cài đặt nâng cao: xem TECHNICAL_REFACTORING_PLAN.md");
}

void MainWindow::OnAbout(IInspectable const&, RoutedEventArgs const&) {
    StatusText().Text(L"KieeKey v1.1.1 — engine C++23, hook lock-free, TSF, WinUI 3");
}

} // namespace KieeKey
