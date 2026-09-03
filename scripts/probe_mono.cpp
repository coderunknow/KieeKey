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
// File: scripts/probe_mono.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
// Probe: trace engine state for problem keystreams ("mono", "huow", "moong")
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "TextEngine.hpp"

using namespace ok::text;

static std::string u32toUtf8(const std::vector<std::uint32_t>& v) {
    std::string s;
    for (auto cp : v) {
        if (cp < 0x80) s += static_cast<char>(cp);
        else if (cp < 0x800) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return s;
}

static void feed(TextEngine& eng, const char* label, const std::string& keys) {
    std::wstring screen;   // committed app text (verification-consumer model)
    std::printf("== %s  keys=\"%s\" ==\n", label, keys.c_str());
    for (char k : keys) {
        TextInput in;
        if (k == ' ') in.kind = InputKind::Space;
        else { in.kind = InputKind::Char; in.ch = static_cast<char32_t>(k); }
        const EngineResult& r = eng.process(in);
        // verification-consumer model: apply backspaces + replacement,
        // re-issue typed char/space on Restore (hotfix §3 + D4)
        const bool reissue = (r.code == EngineCode::Restore ||
                              r.code == EngineCode::RestoreAndStartNewSession) &&
                             (in.kind == InputKind::Char || in.kind == InputKind::Space);
        for (std::size_t i = 0; i < r.backspaceCount && !screen.empty(); ++i) screen.pop_back();
        std::wstring rep;
        eng.replacementUtf16(r, rep);
        if (reissue) rep.push_back(in.kind == InputKind::Space ? L' ' : in.ch);
        if (r.code == EngineCode::ReplaceMacro) { rep.clear(); eng.macroExpansionUtf16(r, rep); }
        screen += rep;
        std::printf("  key '%c' code=%d bs=%u new=%u rep=\"",
                    k, static_cast<int>(r.code), r.backspaceCount, r.newCharCount);
        for (wchar_t wc : rep) {
            if (wc < 0x80) std::printf("%c", static_cast<char>(wc));
            else std::printf("\\u%04x", wc);
        }
        std::printf("\" screen=\"");
        for (wchar_t wc : screen) {
            if (wc < 0x80) std::printf("%c", static_cast<char>(wc));
            else std::printf("\\u%04x", wc);
        }
        std::printf("\"\n");
    }
}

int main() {
    auto dict = [](const std::vector<std::uint32_t>& w) {
        static const char* words[] = {"môn", "huơ", "khuơ", "huơ tay", "mono", "moong", "mông",
                                      "hươu", "quê", "quê hương"};
        const std::string s = u32toUtf8(w);
        for (const char* x : words) { if (s == x) return true; }
        return false;
    };
    EngineOptions o;
    o.restoreIfWrongSpelling = true;
    o.useDictionaryRestore = true;
    TextEngine eng(o);
    eng.setDictionaryResolver(dict);
    feed(eng, "mono", "mono ");
    eng.startNewSession();
    feed(eng, "huow", "huow ");
    eng.startNewSession();
    feed(eng, "moong", "moong ");
    eng.startNewSession();
    feed(eng, "khuow", "khuow ");
    eng.startNewSession();
    feed(eng, "kimono", "kimono ");
    eng.startNewSession();
    feed(eng, "moon", "moon ");
    return 0;
}
// appended: kimono trace
