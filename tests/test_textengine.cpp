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
// File: tests/test_textengine.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.0 — test_textengine.cpp
// Golden Telex / VNI vectors. Simulates what the composer thread does:
//   1. feed a key;
//   2. if the engine consumed it → erase backspaceCount chars from the tail
//      of the app text and insert replacementUtf16(result).
// Also validates the upstream fix: "as" MUST become "á" (2.0.5 tag never
// inserted tone marks due to the _vowelForMark[i] bug).
//----------------------------------------------------------------------------
#include "TextEngine.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using namespace ok::text;

namespace {

struct AppSim {
    std::wstring text;
    TextEngine   engine;

    explicit AppSim(EngineOptions opts = {}) : engine(opts) {}

    // Returns true if the engine consumed the key (consumer must NOT forward it).
    bool feed(char32_t ch, bool caps = false) {
        TextInput in;
        in.kind = InputKind::Char;
        in.ch = ch;
        in.isCaps = caps;
        const EngineResult& r = engine.process(in);
        if (r.consumed()) {
            const std::wstring rep = engine.replacementUtf16(r);
            const std::size_t bs = std::min<std::size_t>(r.backspaceCount, text.size());
            text.erase(text.size() - bs, bs);
            text += rep;
            return true;
        }
        // Pass-through: the OS/app inserts the original character.
        text += static_cast<wchar_t>(ch);
        return false;
    }
    void backspace() {
        TextInput in;
        in.kind = InputKind::Backspace;
        static_cast<void>(engine.process(in));
        if (!text.empty()) { text.pop_back(); }
    }
    void wordBreak() {
        TextInput in;
        in.kind = InputKind::WordBreak;
        in.vkCode = 0x20;   // space-like break
        static_cast<void>(engine.process(in));
    }
    void space() {
        TextInput in;
        in.kind = InputKind::Space;
        static_cast<void>(engine.process(in));
        text += L' ';
    }
};

void feedAll(AppSim& sim, const char* s) {
    for (const char* p = s; *p; ++p) {
        sim.feed(static_cast<char32_t>(static_cast<unsigned char>(*p)));
    }
}

#define CHECK(cond) do { if constexpr (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    ++failures; } } while (0)

int failures = 0;

void telex_case(const char* keys, const wchar_t* expected) {
    EngineOptions opts;
    opts.inputMethod = InputMethod::Telex;
    opts.codeTable = CodeTable::Unicode;
    AppSim sim(opts);
    feedAll(sim, keys);
    CHECK(sim.text == expected);
    if (sim.text != expected) {
        std::wprintf(L"  telex \"%s\": got \"%ls\" want \"%ls\"\n",
                     keys, sim.text.c_str(), expected);
    }
}

void vni_case(const char* keys, const wchar_t* expected) {
    EngineOptions opts;
    opts.inputMethod = InputMethod::Vni;
    opts.codeTable = CodeTable::Unicode;
    AppSim sim(opts);
    feedAll(sim, keys);
    CHECK(sim.text == expected);
    if (sim.text != expected) {
        std::wprintf(L"  vni   \"%s\": got \"%ls\" want \"%ls\"\n",
                     keys, sim.text.c_str(), expected);
    }
}

} // namespace

int main() {
    // ---- Telex: tone marks (exercises the upstream mark-insertion fix) ----
    telex_case("as",   L"á");
    telex_case("af",   L"à");
    telex_case("ar",   L"ả");
    telex_case("ax",   L"ã");
    telex_case("aj",   L"ạ");

    // ---- Telex: replacing a mark ----
    telex_case("asf",  L"à");     // sắc → huyền replaces
    telex_case("asj",  L"ạ");     // sắc → nặng replaces
    telex_case("ajs",  L"á");     // nặng → sắc replaces

    // ---- Telex: hat vowels + marks ----
    telex_case("aa",   L"â");
    telex_case("aas",  L"ấ");
    telex_case("aaf",  L"ầ");
    telex_case("aaj",  L"ậ");
    telex_case("ee",   L"ê");
    telex_case("eef",  L"ề");
    telex_case("oo",   L"ô");
    telex_case("ooj",  L"ộ");

    // ---- Telex: w-tone vowels ----
    telex_case("aw",   L"ă");
    telex_case("aws",  L"ắ");
    telex_case("ow",   L"ơ");
    telex_case("uw",   L"ư");
    telex_case("uow",  L"ươ");
    telex_case("w",    L"ư");     // standalone w → ư

    // ---- Telex: đ ----
    telex_case("dd",   L"đ");

    // ---- Telex: multiple words ----
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Telex;
        AppSim sim(opts);
        feedAll(sim, "chao");
        sim.space();
        feedAll(sim, "ban");
        CHECK(sim.text == L"chao ban");

        // And with a tone: "ban" + 'f' (huyền) → "bàn"
        feedAll(sim, "f");
        CHECK(sim.text == L"chao bàn");
    }

    // ---- VNI ----
    vni_case("a1",   L"á");
    vni_case("a2",   L"à");
    vni_case("a3",   L"ả");
    vni_case("a4",   L"ã");
    vni_case("a5",   L"ạ");
    vni_case("a6",   L"â");
    vni_case("a61",  L"ấ");
    vni_case("o6",   L"ô");
    vni_case("e6",   L"ê");
    vni_case("o7",   L"ơ");
    vni_case("u7",   L"ư");
    vni_case("u7o7", L"ươ");      // "u7" -> ư, then "o7" -> ơ → "ươ"
    vni_case("a8",   L"ă");
    vni_case("a81",  L"ắ");
    vni_case("d9",   L"đ");

    // ---- Backspace semantics ----
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Telex;
        AppSim sim(opts);
        feedAll(sim, "as");        // -> "á"
        CHECK(sim.text == L"á");
        sim.backspace();           // -> ""
        CHECK(sim.text.empty());
        feedAll(sim, "b");         // fresh word
        CHECK(sim.text == L"b");
    }

    // ---- Modern orthography (chính tả mới: oà/uý vs òa/úy) ----
    // Pins CURRENT behavior as a regression net. NOTE: rule 3.1 of
    // handleModernMark contains a dead condition carried faithfully from
    // BOTH OpenKey 2.0.5 and upstream master:
    //     (VSI+3 < _index && CHR(VSI+2)==KEY_C && CHR(VSI+2)==KEY_H)  // C==H impossible
    // (same for N/H and N/G). It never fires, so the CH/NH/NG ending clause
    // is inert — identical to the reference implementation. These goldens
    // document what the engine actually produces today.
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Telex;
        opts.useModernOrthography = true;
        AppSim sim(opts);
        feedAll(sim, "khuech"); feedAll(sim, "s");
        CHECK(sim.text == L"khuéch");
        sim.space(); feedAll(sim, "khiech"); feedAll(sim, "s");
        CHECK(sim.text == L"khuéch khiéch");
        sim.space(); feedAll(sim, "nguyen"); feedAll(sim, "h"); feedAll(sim, "s");
        CHECK(sim.text == L"khuéch khiéch nguyénh");
        sim.space(); feedAll(sim, "nguyech"); feedAll(sim, "s");
        CHECK(sim.text == L"khuéch khiéch nguyénh nguyéch");
        // same words, old orthography — the mark placement must differ
        EngineOptions optsOld;
        optsOld.inputMethod = InputMethod::Telex;
        optsOld.useModernOrthography = false;
        AppSim simOld(optsOld);
        feedAll(simOld, "khuech"); feedAll(simOld, "s");
        CHECK(simOld.text == L"khúech");
        simOld.space(); feedAll(simOld, "khiech"); feedAll(simOld, "s");
        CHECK(simOld.text == L"khúech khíech");
    }

    // ---- Pass-through when not a Vietnamese sequence ----
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Telex;
        AppSim sim(opts);
        // 'x' at word start is a mark key but matches no vowel → pass-through.
        bool consumed = sim.feed(U'x');
        CHECK(!consumed);
        CHECK(sim.text == L"x");
        feedAll(sim, "in");
        CHECK(sim.text == L"xin");
    }

    if (failures == 0) {
        std::printf("ALL TEXTENGINE TESTS PASSED\n");
        return 0;
    }
    std::printf("%d TEXTENGINE TEST(S) FAILED\n", failures);
    return 1;
}
