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
// File: tests/test_v331_features.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v3.3.1 — tests/test_v331_features.cpp
// Native unit tests for the two v3.3.1 core features:
//   1. switchToneStyle() — seamless "hoá" <-> "hóa" conversion of the
//      PENDING word directly inside the state buffer (no corruption, no
//      dropped characters, composition continues, style persists).
//   2. Lexicon-gated restoration refinements — "huơ" vs "hươ" (W-hook
//      split candidate) and "mono" vs "môn" (mid-word toggle arbitration).
// Every case drives the engine through the VERIFICATION-CONSUMER model
// (backspaces + replacement + Restore re-issue contracts), then asserts the
// committed screen text byte-exactly.
// Build: g++ -std=c++20 -I src/core tests/test_v331_features.cpp
//            src/core/TextEngine.cpp
//----------------------------------------------------------------------------
#include <cstdio>
#include <string>
#include <vector>

#include "TextEngine.hpp"

using namespace ok::text;

static int g_failed = 0;
// Non-constexpr helper: `if constexpr` is C2131 for runtime conds; a plain
// `if` is C4127 for constant conds under MSVC /W4 /WX.
[[nodiscard]] inline bool ok_check_failed(bool cond) noexcept { return !cond; }
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (ok_check_failed(static_cast<bool>(cond))) {                    \
            ++g_failed;                                                    \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                  \
    } while (0)

static std::string toUtf8(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) {
        const char32_t cp = c;
        if (cp < 0x80) { s += static_cast<char>(cp); }
        else if (cp < 0x800) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return s;
}

//---------------------------------------------------------------------------
// Verification consumer: applies engine results exactly like the shipped
// Win32 producer + harness model (hotfix §3 Char-Restore re-issue, D4
// Space-Restore re-issue, D3 macro expansion).
//---------------------------------------------------------------------------
class Screen {
public:
    void feed(TextEngine& eng, const std::string& keys) {
        for (char k : keys) {
            TextInput in;
            if (k == ' ') { in.kind = InputKind::Space; }
            else { in.kind = InputKind::Char; in.ch = static_cast<char32_t>(k); }
            apply(eng.process(in), in);
        }
    }
    void apply(const EngineResult& r, const TextInput& in) {
        if (r.code == EngineCode::DoNothing) {
            // pass-through: the raw key lands in the document untouched
            if (in.kind == InputKind::Char) {
                text_.push_back(static_cast<wchar_t>(in.ch));
            } else if (in.kind == InputKind::Space) {
                text_.push_back(L' ');
            } else if (in.kind == InputKind::WordBreak && in.vkCode == 0x0D) {
                text_.push_back(L'\n');
            }
            return;
        }
        const bool reissue = (r.code == EngineCode::Restore ||
                              r.code == EngineCode::RestoreAndStartNewSession) &&
                             (in.kind == InputKind::Char || in.kind == InputKind::Space);
        for (std::size_t i = 0; i < r.backspaceCount && !text_.empty(); ++i) { text_.pop_back(); }
        std::wstring rep;
        eng_.replacementUtf16(r, rep);
        if (r.code == EngineCode::ReplaceMacro) {
            rep.clear();
            eng_.macroExpansionUtf16(r, rep);
        } else if (reissue) {
            rep.push_back(in.kind == InputKind::Space ? L' ' : in.ch);
        }
        text_ += rep;
    }
    void clear() { text_.clear(); }
    [[nodiscard]] const std::wstring& text() const noexcept { return text_; }
    TextEngine& engine() noexcept { return eng_; }

private:
    TextEngine eng_;
    std::wstring text_;
};

//===========================================================================
// 1. Tone-style switching
//===========================================================================
static void testToneStyleHoáHóa() {
    std::printf("== switchToneStyle: hoa+s -> hóa -> hoá -> hóa ==\n");
    Screen s;
    auto& eng = s.engine();
    eng.setOptions(EngineOptions{});   // defaults: Telex, old style (òa úy)
    s.feed(eng, "hoas");
    CHECK(toUtf8(s.text()) == "hóa");   // old style: mark on 'o'

    const bool conv1 = eng.switchToneStyle();
    CHECK(conv1);                       // hoá is visibly different — converted
    {
        // consumer applies the pending result: delete 3, retype 3
        s.apply(eng.lastResult(), TextInput{});
        CHECK(toUtf8(s.text()) == "hoá");   // modern style: mark on 'a'
    }
    // seamless continuation: composition still works on the converted buffer
    s.feed(eng, "n");                    // "hoá" + n
    CHECK(toUtf8(s.text()) == "hoán");

    // switch back on a NEW word with a pending mark: NOTE — a group with an
    // end consonant ("toán") is placed identically in BOTH styles (old style
    // places at vowelStart+1 when an end consonant follows), so switching
    // must report "no conversion" and leave the text untouched.
    eng.startNewSession();
    s.clear();
    s.feed(eng, "toans");
    CHECK(toUtf8(s.text()) == "toán");   // style persisted (modern: mark on 'a')
    CHECK(!eng.switchToneStyle());       // both styles agree here — nothing to do
    s.apply(eng.lastResult(), TextInput{});
    CHECK(toUtf8(s.text()) == "toán");
    // the definitive both-direction check on a bare group: NOTE the switch
    // above already flipped the style back to OLD — "hoas" renders "hóa",
    // and switching converts it to modern "hoá".
    eng.startNewSession();
    s.clear();
    s.feed(eng, "hoas");                 // old style again → "hóa"
    CHECK(toUtf8(s.text()) == "hóa");
    const bool conv3 = eng.switchToneStyle();
    CHECK(conv3);
    s.apply(eng.lastResult(), TextInput{});
    CHECK(toUtf8(s.text()) == "hoá");    // converted to modern
}

static void testToneStyleNoCorruption() {
    std::printf("== switchToneStyle: net-zero length (no drops) ==\n");
    Screen s;
    auto& eng = s.engine();
    eng.setOptions(EngineOptions{});
    s.feed(eng, "hoas");
    const std::size_t lenBefore = s.text().size();
    const bool conv = eng.switchToneStyle();
    CHECK(conv);
    s.apply(eng.lastResult(), TextInput{});
    CHECK(s.text().size() == lenBefore);          // same length — nothing dropped
    CHECK(eng.lastResult().backspaceCount == eng.lastResult().newCharCount);
    CHECK(eng.visibleAccount() >= s.text().size()); // D2 account consistent
}

static void testToneStyleEdgeCases() {
    std::printf("== switchToneStyle: edges (empty / no mark / single vowel) ==\n");
    Screen s;
    auto& eng = s.engine();
    eng.setOptions(EngineOptions{});

    eng.startNewSession();                        // empty buffer → flip only
    CHECK(!eng.switchToneStyle());

    s.clear();
    s.feed(eng, "tes");                           // single vowel 'é'
    const std::wstring before = s.text();
    CHECK(toUtf8(before) == "té");
    CHECK(!eng.switchToneStyle());                // both styles identical
    s.apply(eng.lastResult(), TextInput{});
    CHECK(toUtf8(s.text()) == "té");              // nothing changed

    s.clear();
    s.feed(eng, "chung");                         // no mark at all
    CHECK(!eng.switchToneStyle());                // flip only, no edit pending
}

//===========================================================================
// 2. Lexicon-gated restoration refinements
//===========================================================================
static bool miniLexicon(const std::vector<std::uint32_t>& w) {
    static const char* words[] = {
        "môn", "huơ", "khuơ", "huơ tay", "mono", "moong", "mông",
        "hươu", "quê", "quê hương", "sông", "moon",
    };
    const std::string s = toUtf8(std::wstring(w.begin(), w.end()));
    for (const char* x : words) { if (s == x) return true; }
    return false;
}

static Screen makeDictScreen() {
    Screen s;
    EngineOptions o;
    o.restoreIfWrongSpelling = true;
    o.useDictionaryRestore = true;
    s.engine().setOptions(o);
    s.engine().setDictionaryResolver(miniLexicon);
    return s;
}

static void testLexRestHuo() {
    std::printf("== lexicon restore: 'huow' -> 'huơ' (W-hook split) ==\n");
    Screen s = makeDictScreen();
    s.feed(s.engine(), "huow ");
    CHECK(toUtf8(s.text()) == "huơ ");          // NOT "huow " and NOT "hươ "

    s.feed(s.engine(), "tay ");
    CHECK(toUtf8(s.text()) == "huơ tay ");

    s.engine().startNewSession();
    s.clear();
    s.feed(s.engine(), "khuow ");
    CHECK(toUtf8(s.text()) == "khuơ ");         // beats UniKey ("khươ")
}

static void testLexRestMono() {
    std::printf("== lexicon restore: 'mono' stays raw, 'moon' -> 'môn' ==\n");
    Screen s = makeDictScreen();
    s.feed(s.engine(), "mono ");
    CHECK(toUtf8(s.text()) == "mono ");         // mid-word toggle vetoed by lexicon

    s.engine().startNewSession();
    s.clear();
    s.feed(s.engine(), "moon ");
    CHECK(toUtf8(s.text()) == "môn ");          // normal composition intact

    s.engine().startNewSession();
    s.clear();
    s.feed(s.engine(), "moong ");
    CHECK(toUtf8(s.text()) == "mông ");         // adjacent toggle untouched

    s.engine().startNewSession();
    s.clear();
    s.feed(s.engine(), "kimono ");
    CHECK(toUtf8(s.text()) == "kimono ");       // spelling-disabled path untouched

    s.engine().startNewSession();
    s.clear();
    s.feed(s.engine(), "songo ");
    // "song"+o toggles mid-word → "sông": the composed form IS a lexicon
    // word while the raw keys ("songo") are not — the v3.1 lexicon policy
    // keeps the composed word (the mid-word raw-restore needs BOTH forms to
    // be words). This documents the arbitration precedence.
    CHECK(toUtf8(s.text()) == "sông ");
}

static void testLexRestMatchedUnaffected() {
    std::printf("== lexicon features OFF: engine untouched (v3.0 parity) ==\n");
    Screen s;   // defaults: useDictionaryRestore=false
    s.feed(s.engine(), "mono ");
    CHECK(toUtf8(s.text()) == "môn ");          // 2.0.5-compatible behavior
    s.engine().startNewSession();
    s.clear();
    s.feed(s.engine(), "huow ");
    CHECK(toUtf8(s.text()) == "hươ ");          // 2.0.5-compatible behavior
}

int main() {
    testToneStyleHoáHóa();
    testToneStyleNoCorruption();
    testToneStyleEdgeCases();
    testLexRestHuo();
    testLexRestMono();
    testLexRestMatchedUnaffected();
    if (g_failed == 0) {
        std::printf("\nALL v3.3.1 FEATURE TESTS PASSED\n");
        return 0;
    }
    std::printf("\n%d CHECKS FAILED\n", g_failed);
    return 1;
}
