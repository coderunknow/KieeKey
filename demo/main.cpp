//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.1 RC2 - refactored and completed logic
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
// File: demo/main.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.2.1 RC2 — demo main.cpp (Windows console)
//
// A runnable demonstration of the KieeKey engine. Type Vietnamese Telex/VNI
// in the console and watch the engine compose words live — WITHOUT clipboard
// or synthetic backspaces: each keystroke prints the engine's decision
// (EngineCode, backspace count, replacement UTF-16), which is exactly what
// the TSF composer consumes in the full app.
//
// Keys:
//   F2       switch Telex <-> VNI
//   F3       toggle engine-decision diagnostics
//   Enter    commit the current word (like a word break)
//   Esc      clear the line
//   Ctrl+C   exit
//
// Build (MSVC):   cl /std:c++latest /EHsc /I src\core src\core\TextEngine.cpp demo\main.cpp /Fe:openkey-demo.exe
// Build (MinGW):  x86_64-w64-mingw32-g++ -std=c++23 -O2 -I src/core src/core/TextEngine.cpp demo/main.cpp -o openkey-demo.exe
//----------------------------------------------------------------------------
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <conio.h>
#include <cstdio>
#include <cwchar>
#include <string>

#include "TextEngine.hpp"

using namespace ok::text;

namespace {

void draw(const std::wstring& text, InputMethod method, bool diag) {
    HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info{};
    ::GetConsoleScreenBufferInfo(h, &info);
    ::FillConsoleOutputCharacterW(h, L' ', info.dwSize.X, {0, 0}, nullptr);
    ::SetConsoleCursorPosition(h, {0, 0});

    std::wprintf(L"  KieeKey v1.2.1 RC2 - engine demo      [%s]   (F2: %s | F3: diag | Esc: clear | Ctrl+C: quit)\n",
                 method == InputMethod::Telex ? L"TELEX"
                 : method == InputMethod::Vni ? L"VNI" : L"SIMPLE TELEX",
                 method == InputMethod::Telex ? L"switch to VNI" : L"switch to Telex");
    std::wprintf(L"  --------------------------------------------------------------------------------\n");
    std::wprintf(L"  > %ls\n", text.c_str());
    if (diag) {
        std::wprintf(L"\n  (engine decisions shown per key below)\n");
    }
    ::SetConsoleCursorPosition(h, {0, 3});
}

void printDecision(const TextInput& in, const EngineResult& r, const std::wstring& rep) {
    wchar_t chDesc[32] = L"";
    if (in.kind == InputKind::Char) {
        std::swprintf(chDesc, 32, L" '%lc'", static_cast<wchar_t>(in.ch));
    } else if (in.kind == InputKind::Space) {
        std::wcscpy(chDesc, L" <space>");
    } else if (in.kind == InputKind::Backspace) {
        std::wcscpy(chDesc, L" <backspace>");
    } else {
        std::wcscpy(chDesc, L" <break>");
    }
    std::wprintf(L"    %-20ls  code=%-10d bs=%-3d ncc=%-3d rep=\"%ls\"\n",
                 chDesc, static_cast<int>(r.code),
                 r.backspaceCount, r.newCharCount, rep.c_str());
}

} // namespace

int wmain() {
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleTitleW(L"KieeKey v1.2.1 RC2 - engine demo");

    EngineOptions opts;
    opts.inputMethod = InputMethod::Telex;
    opts.codeTable = CodeTable::Unicode;
    TextEngine engine(opts);

    std::wstring text;
    bool diag = true;

    draw(text, opts.inputMethod, diag);
    std::wprintf(L"  (type Telex, e.g. \"as\" -> %lc, \"uow\" -> %lc%lc, \"dd\" -> %lc)\n",
                 L'\u00e1', L'\u01b0', L'\u01a1', L'\u0111');
    std::wprintf(L"  Try: as af ar ax aj   aw   aa   ow   uw   uow   dd   aas   eef   ooj\n");

    for (;;) {
        const int key = _getwch();   // 0x00/0xE0 prefix = function key
        int scancode = 0;
        int actual = key;
        if (key == 0x00 || key == 0xE0) {
            scancode = _getwch();
            actual = (key << 8) | scancode;
        }

        switch (actual) {
            case 0xE03C:  // F2
                opts.inputMethod = (opts.inputMethod == InputMethod::Telex)
                                       ? InputMethod::Vni : InputMethod::Telex;
                engine.setOptions(opts);
                engine.startNewSession();
                draw(text, opts.inputMethod, diag);
                continue;
            case 0xE03D:  // F3
                diag = !diag;
                draw(text, opts.inputMethod, diag);
                continue;
            default: break;
        }

        TextInput in;
        if (actual == L'\r') {
            in.kind = InputKind::WordBreak;   // commit word (Enter)
            in.vkCode = 0x0D;
        } else if (actual == L'\b') {
            in.kind = InputKind::Backspace;
        } else if (actual == L' ') {
            in.kind = InputKind::Space;
        } else if (actual == 0x1B) {   // Esc -> clear line
            text.clear();
            engine.startNewSession();
            draw(text, opts.inputMethod, diag);
            continue;
        } else if (actual == 3) {      // Ctrl+C
            break;
        } else {
            in.kind = InputKind::Char;
            in.ch = static_cast<char32_t>(static_cast<unsigned char>(actual));
            in.isCaps = (in.ch >= U'A' && in.ch <= U'Z');
        }

        const EngineResult& r = engine.process(in);
        if (r.consumed()) {
            const std::wstring rep = engine.replacementUtf16(r);
            const std::size_t bs = std::min<std::size_t>(r.backspaceCount, text.size());
            text.erase(text.size() - bs, bs);
            text += rep;
            if (diag) { printDecision(in, r, rep); }
        } else if (in.kind == InputKind::Char) {
            text += static_cast<wchar_t>(in.ch);
            if (diag) {
                EngineResult rr; rr.code = EngineCode::DoNothing;
                printDecision(in, rr, L"");
            }
        } else if (in.kind == InputKind::Backspace) {
            if (!text.empty()) { text.pop_back(); }
            if (diag) { printDecision(in, r, L""); }
        }

        draw(text, opts.inputMethod, diag);
    }
    return 0;
}
