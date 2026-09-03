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
// File: tests/test_textengine.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.1 — test_textengine.cpp
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
        const EngineResult& r = engine.process(in);
        // v1.1.0: model the consumer contract for word-break results too —
        // a wrong-spelling Restore deletes backspaceCount chars and re-types
        // the raw keys (previously the sim ignored word-break edits).
        if (r.consumed() && r.code != EngineCode::ReplaceMacro) {
            const std::size_t bs = std::min<std::size_t>(r.backspaceCount, text.size());
            text.erase(text.size() - bs, bs);
            text += engine.replacementUtf16(r);
        }
    }
    void space() {
        TextInput in;
        in.kind = InputKind::Space;
        const EngineResult& r = engine.process(in);
        if (r.code == EngineCode::ReplaceMacro) {
            // v1.1.0 D3 consumer contract: the expansion replaces the raw
            // keys and the space is CONSUMED (the expansion provides the
            // separator). Modeled exactly like the shipped hook.
            const std::size_t bs = std::min<std::size_t>(r.backspaceCount, text.size());
            text.erase(text.size() - bs, bs);
            std::wstring exp;
            engine.macroExpansionUtf16(r, exp);
            text += exp;
            return;
        }
        text += L' ';
    }
    // v1.1.0: consumer-side context re-sync (same call the app makes after a
    // click / caret edit — the visible raw word is replayed into the engine).
    bool resync(const wchar_t* visibleWord) {
        return engine.resumeFromText(std::wstring(visibleWord));
    }
};

void feedAll(AppSim& sim, const char* s) {
    for (const char* p = s; *p; ++p) {
        sim.feed(static_cast<char32_t>(static_cast<unsigned char>(*p)));
    }
}

// Plain `if` — `if constexpr` here would demand a constant expression and
// break every runtime CHECK (ill-formed; MSVC /permissive- and GCC both
// reject it). Constant-condition CHECKs are intentional regression nets;
// the MSVC C4127 noise they cause is disabled project-wide (/wd4127).
#define CHECK(cond) do { if (!(cond)) { \
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
        // %hs (not %s): in a wide printf MSVC reads %s as wchar_t*, so a
        // narrow `const char*` argument fired C4477 under /W4 /WX on CI.
        // %hs explicitly means narrow char* on MSVC, MinGW and glibc alike.
        std::wprintf(L"  telex \"%hs\": got \"%ls\" want \"%ls\"\n",
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
        // %hs (not %s) — see telex_case above for the C4477 rationale.
        std::wprintf(L"  vni   \"%hs\": got \"%ls\" want \"%ls\"\n",
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

    //=====================================================================
    // v1.1.0 regression tests
    //=====================================================================

    // ---- R1: OOB read at word start (checkForStandaloneChar) ----
    // The reverse-to-ư path indexed typingWord_[-1] (SIZE_MAX) when the
    // standalone 'w' arrived at an empty buffer. Behavior must be identical
    // (ư), just now bounds-safe.
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Telex;
        AppSim sim(opts);
        sim.feed(U'w');                       // standalone w at word start
        CHECK(sim.text == L"\u01B0");         // ư
        sim.space();
        sim.feed(U'a');
        sim.space();
        sim.feed(U'w');                       // standalone w after a word
        CHECK(sim.text == L"\u01B0 a \u01B0");
    }

    // ---- R2: context resync must NOT store transforms (raw replay) ----
    // A visible raw word ("as" typed with the IME off) replayed through the
    // FULL state machine left sắc masks in the buffer; the next word break
    // then "restored" composed text that was never displayed.
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Telex;
        AppSim sim(opts);
        sim.text = L"as";                     // raw text already on screen
        CHECK(sim.resync(L"as"));             // the app replays the visible word
        CHECK(sim.text == L"as");             // resync never changes the screen
        sim.feed(U'd');                       // keep typing onto the raw word
        CHECK(sim.text == L"asd");
        sim.wordBreak();                      // wrong-spelling restore
        CHECK(sim.text == L"asd");            // raw keys re-emitted — unchanged
        // And a transformed visible word is NOT replayable (opaque text):
        EngineOptions opts2;
        opts2.inputMethod = InputMethod::Telex;
        AppSim sim2(opts2);
        CHECK(!sim2.resync(L"ch\u00E0o"));
        CHECK(sim2.text == L"");
    }

    // ---- R3: macro expansion + backspace visibility model (D3) ----
    // The expansion (not the raw keys) is on screen; backspace must walk the
    // expansion down and then resume cleanly — no ghost raw keys.
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Telex;
        opts.useMacro = true;
        AppSim sim(opts);
        sim.engine.setMacroResolver(
            [](const std::vector<std::uint32_t>& key,
               std::vector<std::uint32_t>& data) {
                // match ONLY the abbreviation "cn" (any case mix) → "chào"
                if (key.size() == 2 &&
                    (key[0] & 0xFFFF) == U'C' && (key[1] & 0xFFFF) == U'N') {
                    data.assign({U'c', U'h', U'\u00E0', U'o'});
                    return true;
                }
                return false;
            });
        feedAll(sim, "cn");
        sim.space();                          // D3: space consumed by expansion
        CHECK(sim.text == L"ch\u00E0o");
        sim.backspace();                      // delete 'o' from the expansion
        CHECK(sim.text == L"ch\u00E0");
        sim.backspace();
        sim.backspace();
        sim.backspace();                      // expansion fully deleted
        CHECK(sim.text == L"");
        sim.feed(U'x');                       // composition continues cleanly
        CHECK(sim.text == L"x");
        // non-matching abbreviation passes through like a normal word
        sim.space();
        feedAll(sim, "tt");
        sim.space();
        CHECK(sim.text == L"x tt ");
        // after a macro, ONE more backspace deletes real text (no phantom
        // restore of the raw keys "cn")
        sim.backspace();
        CHECK(sim.text == L"x tt");
    }

    // ---- R4: VNI digit 6 with no vowel passes through (stale vowelEnd_) ----
    // The vowel scan previously kept the PREVIOUS word's index; digit 6 then
    // composed onto a phantom vowel.
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Vni;
        AppSim sim(opts);
        feedAll(sim, "a6");                   // normal: a6 → â
        CHECK(sim.text == L"\u00E2");
        sim.space();
        feedAll(sim, "ng6");                  // no O/A/E in buffer: 6 is raw
        CHECK(sim.text == L"\u00E2 ng6");
        sim.space();
        feedAll(sim, "oa6");                  // vowel found in current word
        CHECK(sim.text == L"\u00E2 ng6 o\u00E2");
    }

    if (failures == 0) {
        std::printf("ALL TEXTENGINE TESTS PASSED\n");
        return 0;
    }
    std::printf("%d TEXTENGINE TEST(S) FAILED\n", failures);
    return 1;
}
