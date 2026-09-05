//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.1 RC3 - refactored and completed logic
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
// KieeKey v1.1.3 — test_textengine.cpp
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
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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
    // v1.1.2-r3: this is the LEGACY VNI golden harness — pin the flag so
    // the vectors keep modeling classic digit composition after the
    // library default flipped to digits-literal.
    opts.digitsAreLiteral = false;
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
        // v1.1.2-r3: legacy-composition pin (this test exercises the digit-6
        // composition paths, which the shipped default now disables).
        opts.digitsAreLiteral = false;
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

    //=====================================================================
    // v1.1.2 regression tests — "numbers are numbers" (digitsAreLiteral)
    //=====================================================================
    // Bug being pinned: in VNI mode every digit 1-0 was a composition key
    // (1-5 tones, 6/7/8 vowel marks, 9 = đ, 0 = tone removal), so typing a
    // number mid-word applied a tone mark to the previous word ("nam1" →
    // "nám") or changed it into another word ("d9" → "đ"). With
    // digitsAreLiteral=true (the shipped default) digits ALWAYS type the
    // literal digit, in every input method. Telex/Simple Telex never treated
    // digits as keys — pinned here so it stays that way.

    // helper: VNI with digitsAreLiteral (opts override) golden case
    auto vniDigitsCase = [&](bool literal, const char* keys, const wchar_t* expected) {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Vni;
        opts.codeTable = CodeTable::Unicode;
        opts.digitsAreLiteral = literal;
        AppSim sim(opts);
        feedAll(sim, keys);
        CHECK(sim.text == expected);
        if (sim.text != expected) {
            std::wprintf(L"  vni-digits(%d) \"%hs\": got \"%ls\" want \"%ls\"\n",
                         literal ? 1 : 0, keys, sim.text.c_str(), expected);
        }
    };

    // ---- Telex: digits are plain digits (unchanged by the option) ----
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Telex;
        AppSim sim(opts);
        feedAll(sim, "nam1");  CHECK(sim.text == L"nam1");
        feedAll(sim, "2");     CHECK(sim.text == L"nam12");
        sim.space();
        feedAll(sim, "1kg");   CHECK(sim.text == L"nam12 1kg");
        sim.space();
        feedAll(sim, "2024");  CHECK(sim.text == L"nam12 1kg 2024");
        sim.space();
        feedAll(sim, "a1b2c3"); CHECK(sim.text == L"nam12 1kg 2024 a1b2c3");
    }

    // ---- VNI + digitsAreLiteral=true (the new shipped default) ----
    vniDigitsCase(true, "a1",    L"a1");
    vniDigitsCase(true, "a2",    L"a2");
    vniDigitsCase(true, "a6",    L"a6");
    vniDigitsCase(true, "d9",    L"d9");     // đ key disabled: literal "d9"
    vniDigitsCase(true, "nam1",  L"nam1");
    vniDigitsCase(true, "nhan5", L"nhan5");
    vniDigitsCase(true, "binh2", L"binh2");
    vniDigitsCase(true, "1a2b3", L"1a2b3");  // word-start digits + mid-word digits
    vniDigitsCase(true, "a0",    L"a0");     // tone-removal key disabled too
    vniDigitsCase(true, "mu1",   L"mu1");

    // ---- digits must not poison the session ----
    // A literal digit inside the word buffer must leave the state machine
    // healthy: the NEXT word still composes normally (Telex composes via
    // letters, so it proves the session survives digit-bearing words).
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Telex;
        AppSim sim(opts);
        feedAll(sim, "nam1");
        sim.space();
        feedAll(sim, "as");                   // normal Telex composition: á
        CHECK(sim.text == L"nam1 \u00E1");
        sim.space();
        feedAll(sim, "2024");
        CHECK(sim.text == L"nam1 \u00E1 2024");
    }

    // ---- VNI + digitsAreLiteral=true: word-break restore keeps the digits ----
    // "nhan1" is not a valid syllable (digit as end consonant) — the
    // restore path must re-emit the RAW keys including the digit.
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Vni;
        opts.digitsAreLiteral = true;
        opts.restoreIfWrongSpelling = true;
        AppSim sim(opts);
        feedAll(sim, "nhan1");
        sim.space();
        CHECK(sim.text == L"nhan1 ");
    }

    // ---- VNI + digitsAreLiteral=false: classic VNI digit composition kept ----
    vniDigitsCase(false, "a1",   L"\u00E1");     // á
    vniDigitsCase(false, "d9",   L"\u0111");     // đ
    vniDigitsCase(false, "nam1", L"n\u00E1m");   // nám
    // bare "a0" passes through ('0' with no pending mark is not consumed —
    // legacy parity); a REAL tone is removed: "a10" → "a"
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Vni;
        opts.digitsAreLiteral = false;
        AppSim sim(opts);
        feedAll(sim, "a0");
        CHECK(sim.text == L"a0");
        sim.backspace();
        sim.backspace();
        feedAll(sim, "a10");
        CHECK(sim.text == L"a");
    }
    // legacy parity, the FULL VNI digit key set (documents exactly what the
    // option disables when turned OFF): 1-5 tones, 6 circumflex, 7 hook,
    // 8 breve, 9 đ-bar, 0 tone removal.
    vniDigitsCase(false, "a8",  L"\u0103");      // ă
    vniDigitsCase(false, "u7",  L"\u01B0");      // ư
    vniDigitsCase(false, "o6",  L"\u00F4");      // ô
    vniDigitsCase(false, "e6",  L"\u00EA");      // ê
    vniDigitsCase(false, "a2",  L"\u00E0");      // à
    vniDigitsCase(false, "a3",  L"\u1EA3");      // ả
    vniDigitsCase(false, "a4",  L"\u00E3");      // ã
    vniDigitsCase(false, "a5",  L"\u1EA1");      // ạ

    //=====================================================================
    // v1.1.2-r2 EXHAUSTIVE DIGIT BATTERY — "a digit can never edit text"
    // (shipped default digitsAreLiteral=true). Stronger than the goldens
    // above: each DIGIT event itself must be an inert pass-through
    // (DoNothing, zero backspaces, zero new chars) in every context —
    // mid-word, word-start, after a space/break, caps-held, after a
    // composed word, inside a wrong-spelling restore, around macros —
    // and the session must stay healthy for later words.
    //=====================================================================
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Vni;
        opts.codeTable = CodeTable::Unicode;
        opts.digitsAreLiteral = true;
        opts.checkSpelling = true;
        opts.restoreIfWrongSpelling = true;
        AppSim sim(opts);
        auto pressDigit = [&](char32_t d) {
            TextInput in;
            in.kind = InputKind::Char;
            in.ch = d;
            const EngineResult& r = sim.engine.process(in);
            CHECK(r.code == EngineCode::DoNothing);
            CHECK(r.backspaceCount == 0);
            CHECK(r.newCharCount == 0);
            sim.text += static_cast<wchar_t>(d);   // consumer contract: literal
        };
        // mid-word: every digit after letters — the exact reported repro
        feedAll(sim, "nhan");
        for (const char32_t d : {U'1', U'2', U'3', U'4', U'5', U'6', U'7', U'8', U'9', U'0'}) {
            pressDigit(d);
        }
        CHECK(sim.text == L"nhan1234567890");
        sim.space();
        // word-start digit run (a full year, the most common real case)
        feedAll(sim, "nam");
        sim.space();
        for (const char32_t d : {U'2', U'0', U'2', U'6'}) { pressDigit(d); }
        sim.space();
        CHECK(sim.text == L"nhan1234567890 nam 2026 ");
        // caps-held digit (CapsLock on — must also stay literal)
        for (const char32_t d : {U'7', U'9'}) {
            TextInput in;
            in.kind = InputKind::Char;
            in.ch = d;
            in.isCaps = true;
            const EngineResult& r = sim.engine.process(in);
            CHECK(r.code == EngineCode::DoNothing);
            CHECK(r.backspaceCount == 0);
            sim.text += static_cast<wchar_t>(d);
        }
        CHECK(sim.text == L"nhan1234567890 nam 2026 79");
        sim.space();
        // after all that: the session is HEALTHY — but VNI composes through
        // digits, so with digits literal a plain VNI word stays raw; switch
        // the mode to Telex to prove real composition still works.
        EngineOptions telexOpts = opts;
        telexOpts.inputMethod = InputMethod::Telex;
        sim.engine.setOptions(telexOpts);
        sim.engine.startNewSession();
        feedAll(sim, "as");
        CHECK(sim.text == L"nhan1234567890 nam 2026 79 \u00E1");
    }

    // ---- digit-heavy word through the wrong-spelling RESTORE path ----
    // The restore re-emits RAW keys; digits inside must survive verbatim.
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Vni;
        opts.digitsAreLiteral = true;
        opts.checkSpelling = true;
        opts.restoreIfWrongSpelling = true;
        AppSim sim(opts);
        feedAll(sim, "nhan2016x");            // not a syllable → restore fires
        sim.space();
        CHECK(sim.text == L"nhan2016x ");
        sim.space();
        feedAll(sim, "5nhan");                // digit-first word
        sim.space();
        CHECK(sim.text == L"nhan2016x  5nhan ");
    }

    // ---- macro interplay: digits must not break the macro accumulator ----
    // Resolver contract (mirrors the app's MacroTable::find): the engine
    // delivers the accumulator as internal entries — low bits are the
    // UPPERCASE key char, bit 16 flags "typed uppercase"; matching folds
    // case. The table below is keyed by the folded lowercase form.
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Vni;
        opts.digitsAreLiteral = true;
        opts.useMacro = true;
        opts.restoreIfWrongSpelling = false;  // keep the sim model in scope
        AppSim sim(opts);
        const std::vector<std::pair<std::wstring,
                                    std::vector<std::uint32_t>>> table = {
            {L"cn", {U'c', U'h', U'\u00E0', U'o'}},           // cn → chào
        };
        sim.engine.setMacroResolver(
            [&table](const std::vector<std::uint32_t>& key,
                     std::vector<std::uint32_t>& data) -> bool {
                std::wstring folded;
                folded.reserve(key.size());
                for (const std::uint32_t v : key) {
                    wchar_t c = static_cast<wchar_t>(v & 0xFFu);
                    if (c >= L'A' && c <= L'Z') {
                        c = static_cast<wchar_t>(c - L'A' + L'a');
                    }
                    folded.push_back(c);
                }
                for (const auto& kv : table) {
                    if (kv.first == folded) { data = kv.second; return true; }
                }
                return false;
            });
        feedAll(sim, "c2n");                  // digit poisons the abbreviation
        sim.space();
        CHECK(sim.text == L"c2n ");           // must NOT expand ("c2n" ≠ "cn")
        feedAll(sim, "cn");
        sim.space();
        CHECK(sim.text == L"c2n ch\u00E0o");  // real macro still expands
        feedAll(sim, "1");                    // lone digit before a macro word
        sim.space();
        // D3 contract: the expansion consumed its break space, so the
        // user's next keystroke (the digit) follows the expansion directly.
        CHECK(sim.text == L"c2n ch\u00E0o1 ");
        feedAll(sim, "cn");
        sim.space();
        CHECK(sim.text == L"c2n ch\u00E0o1 ch\u00E0o");  // and still expands
    }

    // ---- mid-session policy switch (the app's checkbox path) ----
    // setOptions(digitsAreLiteral) + startNewSession is exactly what the
    // settings dialog does; composition must stop/resume cleanly.
    // NOTE: restoreIfWrongSpelling is OFF here — "nam1" is an invalid
    // syllable and the space-Restore path carries the D4 re-issue contract
    // (the shipped hook re-issues the space key), which this char/word sim
    // intentionally does not model. The restore × digits interplay is
    // covered by the dedicated restore block above.
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Vni;
        opts.digitsAreLiteral = false;        // start legacy
        opts.restoreIfWrongSpelling = false;
        AppSim sim(opts);
        feedAll(sim, "nam1");
        CHECK(sim.text == L"n\u00E1m");       // legacy composed
        sim.space();

        EngineOptions literal = opts;
        literal.digitsAreLiteral = true;      // user checks "Số 0–9 luôn là chữ số"
        sim.engine.setOptions(literal);
        sim.engine.startNewSession();
        feedAll(sim, "nam1");

        CHECK(sim.text == L"n\u00E1m nam1");  // now literal
        sim.space();
        EngineOptions legacy2 = literal;
        legacy2.digitsAreLiteral = false;     // user unchecks again
        sim.engine.setOptions(legacy2);
        sim.engine.startNewSession();
        feedAll(sim, "trang2");
        CHECK(sim.text == L"n\u00E1m nam1 tr\u00E0ng");  // composition resumed
    }

    // ---- Simple Telex: digits were never keys, pinned literal ----
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::SimpleTelex;
        opts.digitsAreLiteral = true;
        AppSim sim(opts);
        feedAll(sim, "nhan1");
        sim.space();
        feedAll(sim, "d9");
        CHECK(sim.text == L"nhan1 d9");
    }

    //===================================================================
    // v1.1.2-r3 — the LIBRARY default is the product default.
    // Root cause this pins: the WinUI 3 front-end (and any future
    // TextEngine consumer) constructs EngineOptions{} and expects
    // digits-are-numbers. Until r3 the library default was the LEGACY
    // behavior, so a default-constructed engine re-introduced the exact
    // "digit becomes a tone mark" bug in every non-Win32 consumer. These
    // vectors FAIL if anyone ever flips the default back.
    //===================================================================
    {
        EngineOptions opts{};   // NOTHING set — pure library defaults
        CHECK(opts.digitsAreLiteral == true);
    }
    {
        EngineOptions opts;     // only the method set — the WinUI 3 ctor shape
        opts.inputMethod = InputMethod::Vni;
        AppSim sim(opts);
        feedAll(sim, "nhan5");
        sim.space();
        feedAll(sim, "d9");
        sim.space();
        feedAll(sim, "1a2b3");
        CHECK(sim.text == L"nhan5 d9 1a2b3");
    }
    {
        // Word-break restore with default options: the digit-word stays raw.
        // (The restore re-issues the space — D4 contract — so BOTH spaces
        // land; the visible text ends with a space, same as the "nhan1 "
        // vector above.)
        EngineOptions opts;
        opts.inputMethod = InputMethod::Vni;
        AppSim sim(opts);
        feedAll(sim, "nam1");
        sim.space();
        feedAll(sim, "binh2");
        sim.space();
        CHECK(sim.text == L"nam1 binh2 ");
    }

    // ========================================================================
    // v1.1.3 regression tests — hardening release
    // ========================================================================

    // ---- F-class 1: VNI tone key on a word whose vowel scan found nothing
    //      (the "U"+w+<bs>+"6" family, mega repro 4-mode-switch_270aa931120a).
    //      With no O/A/E in the buffer the digit must NOT compose onto a
    //      phantom/stale vowel: it types literally (matches upstream 2.0.5).
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Vni;
        opts.digitsAreLiteral = false;   // classic-VNI harness (see above)
        AppSim sim(opts);
        sim.feed(U'u', true);            // caps U (sim pass-through shows 'u')
        feedAll(sim, "w");
        sim.backspace();                 // visible "U" (sim: "u")
        feedAll(sim, "6");
        // The sim's pass-through does not render the caps bit; the assertion
        // is that the '6' types LITERALLY (no phantom composition, no mark).
        CHECK(sim.text == L"u6");
    }
    {
        // Same family, plain case: 'u' then '6' with no O/A/E — literal.
        EngineOptions opts;
        opts.inputMethod = InputMethod::Vni;
        opts.digitsAreLiteral = false;
        AppSim sim(opts);
        feedAll(sim, "u6");
        CHECK(sim.text == L"u6");
    }

    // ---- F-class 2: macro expansion + partial backspace + fresh word
    //      (mega repro 4-mode-switch_6c8d180ca9f3). The D3 model: after the
    //      expansion is partially backspaced, a new letter starts a NEW word
    //      (the raw macro keys must never return to the buffer), and a VNI
    //      mark key composes onto that new letter.
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Vni;
        opts.digitsAreLiteral = false;
        opts.useMacro = true;
        AppSim sim(opts);
        const std::vector<std::pair<std::wstring, std::wstring>>
            kMacros{{L"ok", L"\u0111\u01B0\u1EE3c"}};   // ok -> được
        bool called = false;
        sim.engine.setMacroResolver(
            [&kMacros, &called](const std::vector<std::uint32_t>& key,
                                std::vector<std::uint32_t>& data) {
                std::wstring raw;
                for (std::uint32_t v : key) {
                    const char32_t ch = static_cast<char32_t>(v & 0xFFFFu);
                    if (ch < 32 || ch > 126) { return false; }
                    char c = static_cast<char>(ch);
                    if (v & 0x10000u) { if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A'); }
                    else { if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); }
                    raw += c;
                }
                for (const auto& m : kMacros) {
                    if (raw == m.first) {
                        data.assign(m.second.begin(), m.second.end());
                        called = true;
                        return true;
                    }
                }
                return false;
            });
        feedAll(sim, "ok");
        sim.space();          // expansion: "được"
        CHECK(sim.text == L"\u0111\u01B0\u1EE3c");
        sim.backspace();      // -> "đượ" (walks the expansion down)
        CHECK(sim.text == L"\u0111\u01B0\u1EE3");
        feedAll(sim, "a");    // fresh word "a"
        feedAll(sim, "3");    // VNI hỏi mark -> "ả"
        CHECK(sim.text == L"\u0111\u01B0\u1EE3\u1ea3");
    }

    // ---- F-class 3: standalone bracket composes ơ at a word start
    //      (KieeKey behavior, diverges from 2.0.5 literal — pinned here).
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Telex;
        AppSim sim(opts);
        feedAll(sim, "[");
        CHECK(sim.text == L"\u01a1");
    }

    // ---- Engine fix: quick-consonant transform refuses to run at the
    //      buffer limit (31-char word). Before the guard the transform
    //      emitted a stale 32nd character and mismatched counts; now the
    //      key inserts normally and the buffer stays consistent.
    {
        EngineOptions opts;
        opts.inputMethod = InputMethod::Telex;
        opts.quickStartConsonant = true;
        opts.quickEndConsonant = true;
        opts.checkSpelling = false;   // isolate the transform
        AppSim sim(opts);
        const std::wstring longWord(31, L'k');   // index_ == kMaxBuff-1
        for (wchar_t wc : longWord) { sim.feed(static_cast<char32_t>(wc)); }
        sim.feed(U'k');               // one more — quickEndConsonant candidate
        CHECK(sim.text.size() == longWord.size() + 1);
        sim.wordBreak();
        CHECK(sim.text.size() == longWord.size() + 1);   // no phantom chars
        CHECK(sim.engine.lastResult().newCharCount <= 32);
    }

    if (failures == 0) {
        std::printf("ALL TEXTENGINE TESTS PASSED\n");
        return 0;
    }
    std::printf("%d TEXTENGINE TEST(S) FAILED\n", failures);
    return 1;
}
