//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.1 Stable - refactored and completed logic
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
// File: tests/diff_engine_ab.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// tests/diff_engine_ab.cpp — LIVE engine vs FROZEN v1.2.1 RC1 engine,
// lockstep differential over randomized keystreams × the option matrix.
//
// WHY: RC2 optimizes TextEngine's hot path (spelling gate, undo-history
// ring, per-key result reset). The existing suites (golden vectors, oracle
// gate, key correctness, state transitions) pin the CONTRACT; this harness
// pins the exact RC1 BEHAVIOR for every option combination, including the
// obscure ones no golden vector covers (quickStartConsonant + freeMark +
// SimpleTelex + VIQR + macro + dictionary restore ...). Any divergence in
// decision code, backspace count, replacement text, macro expansion, or the
// visible-account counter is a failure with a reproducible seed + case id.
//
// Build (see tests/run_all_tests.sh):
//   g++ -std=c++2b -O2 -Isrc/core -Itests -c tests/diff_engine_ab.cpp
//   g++ -std=c++2b -O2 -Dok=ok_rc1 -Itests/reference/kieekey-1.2.1-rc1 \
//       -c tests/reference/kieekey-1.2.1-rc1/TextEngine.cpp -o rc1.o
//   link with the live TextEngine object.
//
// Usage: diff_engine_ab [--seed=N] [--cases=N] [--keys=N]
//----------------------------------------------------------------------------
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

// Frozen RC1 engine under a renamed namespace (ok_rc1::text).
#define ok ok_rc1
#include "reference/kieekey-1.2.1-rc1/TextEngine.hpp"
#undef ok
// Live engine.
#include "TextEngine.hpp"

namespace {

struct Step {
    int kind;          // 0 char, 1 space, 2 backspace, 3 wordbreak, 4 mouse
    char32_t ch;
    std::uint16_t vk;
    bool caps;
    bool ctrl;
};

// A realistic + adversarial alphabet: Telex letters (heavy on vowels and
// mark keys), VNI digits, punctuation, brackets, uppercase, a few
// non-ASCII code points and a NUL-ish edge.
const char32_t kAlpha[] = {
    U'a',U'a',U'a',U'e',U'e',U'o',U'o',U'o',U'u',U'u',U'i',U'y',
    U'w',U'w',U's',U'f',U'r',U'x',U'j',U'z',U'd',U'd',
    U'n',U'g',U'h',U'c',U't',U'm',U'p',U'q',U'k',U'l',U'b',U'v',
    U'1',U'2',U'3',U'4',U'5',U'6',U'7',U'8',U'9',U'0',
    U'[',U']',U'.',U',',U'-',U'\'',U'/',U';',U'=',U'\\',U'`',U'!',U'?',
    U'A',U'O',U'W',U'S',U'D',U'N',U'G',U'P',U'T',U'C',U'K',U'Q',
    U'é',U'ü',U'ß',U'ñ',U'\u0300',U'\U0001F600',
};

// Realistic Telex/VNI syllables (what a Vietnamese user actually types),
// including the correction-heavy families: double-tone toggles ("cass"),
// W-hook splits ("huow"), quick-Telex pairs ("pp", "cc"), long words,
// English words with doubled consonants, macros ("xl", "vn").
const char* const kWords[] = {
    "chungs", "nguowif", "ddaays", "vieejt", "hoaf", "hoas", "quas", "ddepj",
    "trowif", "nhieeuf", "raast", "camr", "own", "gawpj", "laij", "nhas",
    "cass", "caas", "huow", "huowu", "thuowng", "dduwowcj", "khoong", "bieets",
    "ppongf", "ccaof", "tthe", "kkhi", "nngay", "qquas", "ggif",
    "happy", "apple", "letter", "running", "success", "account", "pp", "cc",
    "xl", "vn", "ko", "toi", "la", "nguoi", "mono", "moon", "bata", "ddawngten",
    "xuoongs", "cuoocj", "tuyeejt", "khuya", "khuyaas", "queen", "quay", "qua",
    "gin", "gif", "gioowf", "giuwx", "yeeu", "yeeus", "uyeen", "uwowis",
    "Vieejt", "Nam", "HAf", "NOOIJ", "Chaof", "AAn", "OOng", "UWs",
    "a", "o", "aa", "ow", "aw", "dd", "w", "[", "]", "[[", "]]", "a[", "o]",
    "1234", "a1", "a6", "a8", "u7", "d9", "o0", "chung1", "viet65",
    "thuowngr", "ddieeuf", "nghieeng", "chuyeenr", "khuyeechs", "nguyeenx",
    "abcdefghijklmnopqrstuvwxyzabcdefghij",   // > kMaxBuff
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
};

std::vector<Step> genStream(std::mt19937& rng, std::size_t n) {
    std::vector<Step> s;
    s.reserve(n + 64);
    std::uniform_int_distribution<int> kindD(0, 99);
    std::uniform_int_distribution<std::size_t> alphaD(0, std::size(kAlpha) - 1);
    std::uniform_int_distribution<std::size_t> wordD(0, std::size(kWords) - 1);
    std::uniform_int_distribution<int> vkD(0, 7);
    const std::uint16_t vks[] = {0x0D, 0x09, 0x1B, 0x25, 0x27, 0x24, 0x23, 0x2E};
    while (s.size() < n) {
        Step st{};
        const int k = kindD(rng);
        if (k < 45) {
            // whole realistic word, sometimes with a caps first letter
            const char* w = kWords[wordD(rng)];
            const bool capFirst = kindD(rng) < 10;
            for (std::size_t j = 0; w[j]; ++j) {
                Step ws{};
                ws.kind = 0;
                ws.ch = static_cast<char32_t>(static_cast<unsigned char>(w[j]));
                ws.caps = (w[j] >= 'A' && w[j] <= 'Z') || (capFirst && j == 0);
                if (ws.caps && ws.ch >= U'a' && ws.ch <= U'z') { ws.ch = ws.ch - 32; }
                s.push_back(ws);
                // mid-word backspace correction (retype the letter)
                if (kindD(rng) < 4) { Step bs{}; bs.kind = 2; s.push_back(bs); s.push_back(ws); }
            }
            Step sp{}; sp.kind = (kindD(rng) < 85) ? 1 : 3; sp.vk = 0x0D; s.push_back(sp);
            continue;
        }
        if (k < 70)      { st.kind = 0; st.ch = kAlpha[alphaD(rng)]; }
        else if (k < 84) { st.kind = 1; }
        else if (k < 94) { st.kind = 2; }
        else if (k < 98) { st.kind = 3; st.vk = vks[vkD(rng)]; }
        else             { st.kind = 4; }
        st.caps = (kindD(rng) < 6);
        st.ctrl = (kindD(rng) < 2);
        // occasional repeated-key burst (pp, tt, aaa, backspace storms)
        if (kindD(rng) < 8 && !s.empty()) { st = s.back(); }
        s.push_back(st);
    }
    return s;
}

template <class TI, class IK>
TI toInput(const Step& st) {
    TI in{};
    switch (st.kind) {
        case 0: in.kind = IK::Char; in.ch = st.ch; break;
        case 1: in.kind = IK::Space; break;
        case 2: in.kind = IK::Backspace; break;
        case 3: in.kind = IK::WordBreak; in.vkCode = st.vk; break;
        default: in.kind = IK::MouseDown; break;
    }
    in.isCaps = st.caps;
    in.otherCtrl = st.ctrl;
    return in;
}

struct OptCase {
    int inputMethod, codeTable, outputEncoding;
    bool checkSpelling, modern, quickTelex, restore, freeMark, zfwj,
         quickStart, quickEnd, upper, macro, macroPunct, dict, digitsLit;
};

OptCase randOpts(std::mt19937& rng) {
    std::uniform_int_distribution<int> b(0, 1);
    OptCase c{};
    c.inputMethod   = std::uniform_int_distribution<int>(0, 2)(rng);
    c.codeTable     = std::uniform_int_distribution<int>(0, 4)(rng);
    c.outputEncoding= b(rng);
    c.checkSpelling = b(rng) || b(rng);   // biased ON (the shipping default)
    c.modern = b(rng); c.quickTelex = b(rng); c.restore = b(rng) || b(rng);
    c.freeMark = b(rng) && b(rng); c.zfwj = b(rng) && b(rng);
    c.quickStart = b(rng) && b(rng); c.quickEnd = b(rng) && b(rng);
    c.upper = b(rng) && b(rng); c.macro = b(rng); c.macroPunct = b(rng);
    c.dict = b(rng) && b(rng); c.digitsLit = b(rng);
    return c;
}

template <class EO>
EO makeOpts(const OptCase& c) {
    EO o{};
    o.inputMethod   = static_cast<decltype(o.inputMethod)>(c.inputMethod);
    o.codeTable     = static_cast<decltype(o.codeTable)>(c.codeTable);
    o.outputEncoding= static_cast<decltype(o.outputEncoding)>(c.outputEncoding);
    o.checkSpelling = c.checkSpelling;
    o.useModernOrthography = c.modern;
    o.quickTelex = c.quickTelex;
    o.restoreIfWrongSpelling = c.restore;
    o.freeMark = c.freeMark;
    o.allowConsonantZfwj = c.zfwj;
    o.quickStartConsonant = c.quickStart;
    o.quickEndConsonant = c.quickEnd;
    o.upperCaseFirstChar = c.upper;
    o.useMacro = c.macro;
    o.macroExpandsOnPunctuation = c.macroPunct;
    o.useDictionaryRestore = c.dict;
    o.digitsAreLiteral = c.digitsLit;
    return o;
}

// Shared tiny macro table + toy lexicon so both engines see the same
// resolver answers.
bool macroLookup(const std::vector<std::uint32_t>& key, std::vector<std::uint32_t>& out) {
    auto eq = [&](const char* s) {
        std::size_t n = std::strlen(s);
        if (key.size() != n) return false;
        for (std::size_t i = 0; i < n; ++i) {
            if ((key[i] & 0xFFFFu) != static_cast<std::uint32_t>(std::toupper(s[i]))) return false;
        }
        return true;
    };
    out.clear();
    if (eq("xl"))  { for (char32_t c : U"xin lỗi")   out.push_back(c); out.pop_back(); return true; }
    if (eq("vn"))  { for (char32_t c : U"Việt Nam")  out.push_back(c); out.pop_back(); return true; }
    if (eq("ko"))  { for (char32_t c : U"không")     out.push_back(c); out.pop_back(); return true; }
    return false;
}
bool dictLookup(const std::vector<std::uint32_t>& composed) {
    static const std::u32string words[] = {U"chào", U"việt", U"huơ", U"hươu", U"môn", U"mono", U"đăng"};
    std::u32string w;
    for (auto v : composed) w.push_back(static_cast<char32_t>(v));
    for (const auto& d : words) if (d == w) return true;
    return false;
}

std::string hex(const std::wstring& s) {
    std::string o;
    char b[16];
    for (wchar_t c : s) { std::snprintf(b, sizeof b, "%04X ", (unsigned)c); o += b; }
    return o;
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t seed = 20260904;
    std::size_t cases = 400, keys = 3000;
    for (int i = 1; i < argc; ++i) {
        if (!std::strncmp(argv[i], "--seed=", 7))  seed  = std::strtoull(argv[i] + 7, nullptr, 10);
        if (!std::strncmp(argv[i], "--cases=", 8)) cases = std::strtoull(argv[i] + 8, nullptr, 10);
        if (!std::strncmp(argv[i], "--keys=", 7))  keys  = std::strtoull(argv[i] + 7, nullptr, 10);
    }
    std::printf("[ab] live engine vs frozen v1.2.1 RC1 — seed=%llu cases=%zu keys/case=%zu\n",
                (unsigned long long)seed, cases, keys);

    std::mt19937 rng(static_cast<std::uint32_t>(seed));
    std::uint64_t events = 0, mismatches = 0, consumed = 0, macros = 0;
    std::uint64_t optionSwitches = 0;

    for (std::size_t c = 0; c < cases && mismatches < 20; ++c) {
        OptCase oc = randOpts(rng);
        ok_rc1::text::TextEngine a(makeOpts<ok_rc1::text::EngineOptions>(oc));
        ok::text::TextEngine     b(makeOpts<ok::text::EngineOptions>(oc));
        a.setMacroResolver(macroLookup);      b.setMacroResolver(macroLookup);
        a.setDictionaryResolver(dictLookup);  b.setDictionaryResolver(dictLookup);

        auto stream = genStream(rng, keys);
        std::wstring ra, rb, ma, mb;
        for (std::size_t i = 0; i < stream.size(); ++i) {
            // Runtime option change mid-stream (settings toggled while
            // typing) every ~500 keys, identically on both sides.
            if (i && (i % 500) == 0) {
                oc = randOpts(rng);
                a.setOptions(makeOpts<ok_rc1::text::EngineOptions>(oc));
                b.setOptions(makeOpts<ok::text::EngineOptions>(oc));
                ++optionSwitches;
            }
            if (i && (i % 733) == 0) {   // tone-style chord mid-word
                const bool sa = a.switchToneStyle(), sb = b.switchToneStyle();
                if (sa != sb) { ++mismatches; std::printf("[ab] MISMATCH switchToneStyle case=%zu i=%zu\n", c, i); }
            }
            if (i && (i % 911) == 0) {   // resync replay (click into word)
                const std::wstring w = (i % 2) ? L"chu" : L"Nguo";
                const bool sa = a.resumeFromText(w), sb = b.resumeFromText(w);
                if (sa != sb) { ++mismatches; std::printf("[ab] MISMATCH resumeFromText case=%zu i=%zu\n", c, i); }
            }
            const Step& st = stream[i];
            const auto& xa = a.process(toInput<ok_rc1::text::TextInput, ok_rc1::text::InputKind>(st));
            const auto& xb = b.process(toInput<ok::text::TextInput, ok::text::InputKind>(st));
            ++events;
            a.replacementUtf16(xa, ra);   b.replacementUtf16(xb, rb);
            a.macroExpansionUtf16(xa, ma); b.macroExpansionUtf16(xb, mb);
            if (xb.consumed()) ++consumed;
            if (xb.code == ok::text::EngineCode::ReplaceMacro) ++macros;
            const bool same =
                static_cast<int>(xa.code) == static_cast<int>(xb.code) &&
                xa.backspaceCount == xb.backspaceCount &&
                xa.newCharCount == xb.newCharCount &&
                xa.extCode == xb.extCode &&
                ra == rb && ma == mb &&
                a.visibleAccount() == b.visibleAccount() &&
                a.debugScratchSize() == b.debugScratchSize();
            if (!same) {
                ++mismatches;
                std::printf("[ab] MISMATCH case=%zu i=%zu kind=%d ch=U+%04X caps=%d ctrl=%d\n"
                            "     rc1: code=%d bs=%u n=%u ext=%u rep=[%s] macro=[%s] acct=%zu\n"
                            "     new: code=%d bs=%u n=%u ext=%u rep=[%s] macro=[%s] acct=%zu\n",
                            c, i, st.kind, (unsigned)st.ch, st.caps, st.ctrl,
                            (int)xa.code, xa.backspaceCount, xa.newCharCount, xa.extCode,
                            hex(ra).c_str(), hex(ma).c_str(), a.visibleAccount(),
                            (int)xb.code, xb.backspaceCount, xb.newCharCount, xb.extCode,
                            hex(rb).c_str(), hex(mb).c_str(), b.visibleAccount());
                break;
            }
        }
    }
    std::printf("[ab] events=%llu consumed=%llu macroExpansions=%llu optionSwitches=%llu mismatches=%llu\n",
                (unsigned long long)events, (unsigned long long)consumed,
                (unsigned long long)macros, (unsigned long long)optionSwitches,
                (unsigned long long)mismatches);
    if (mismatches) { std::printf("[ab] VERDICT: FAIL\n"); return 1; }
    std::printf("[ab] VERDICT: PASS (live engine is decision-identical to v1.2.1 RC1)\n");
    return 0;
}
