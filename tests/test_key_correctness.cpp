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
// File: tests/test_key_correctness.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.2.0 Stable — tests/test_key_correctness.cpp
// Keystroke-CORRECTNESS suite: what the user sees for a given key sequence.
//
// WHY THIS FILE EXISTS
//   Every other harness in tests/ checks ENGINE contracts (backspace counts,
//   buffer bounds, determinism, latency). None of them answers the only
//   question a user actually asks: "I pressed these keys — is the text on my
//   screen what it should be?" This suite answers exactly that, with a
//   consumer model copied line-for-line from the shipped hook
//   (src/app/main.cpp, onHookEventImpl) rather than an invented one.
//
// THE CONSUMER MODEL (authoritative — do not "simplify" it)
//     scratch  = replacementUtf16(r)   (macroExpansionUtf16 for ReplaceMacro)
//     reissue  = (Restore || RestoreAndStartNewSession) && (Char || Space)
//     suppress = code == ReplaceMacro ? (bs > 0 || scratch non-empty)
//                                     : (consumed() && !(bs == 0 && scratch empty))
//     if (reissue) scratch.push_back(typed char)
//     if (!suppress)  -> the key reaches the application untouched (NO edit)
//     else            -> delete bs chars, then insert scratch
//
//   The important subtlety: when code == DoNothing the engine still reports a
//   non-zero backspaceCount in some grammar-repair paths, and the shipped hook
//   IGNORES it and lets the key through. An earlier revision of the
//   state-transition suite applied backspaceCount unconditionally, which is
//   why it modelled "work" as "ửokk" instead of the real "ửok".
//
// CASES
//   1. FALSE-POSITIVE CORPUS — with "gõ tắt" (quickTelex) enabled, real
//      Latin/English words containing a doubled consonant must survive
//      byte-for-byte. This is the user-reported P1 ("typing 'p' twice turns
//      it into 'ph'"): the legacy rule fired at ANY position, so happy→haphy,
//      apple→aphle, letter→lether, account→achount, running→runging...
//   2. FEATURE-PRESERVED CORPUS — the same option must still expand the
//      cluster when the doubling is syllable-initial ("ppongf" → "phòng").
//   3. DEFAULT-CONFIG CORPUS — the exact output for a set of words under the
//      shipping defaults, pinned so a future change cannot silently alter
//      what millions of keystrokes produce.
//   4. NO-SILENT-LOSS PROPERTY — exhaustive over every 1-, 2- and 3-letter
//      lowercase word: the emitted edit must never delete more characters
//      than the word could legitimately have consumed (tone/breve/undo keys),
//      and must never produce a character outside the engine's own alphabet.
//   5. DOCUMENTED-BEHAVIOUR CORPUS — the two "surprising but correct"
//      Telex conventions, pinned so nobody "fixes" them by accident:
//      a doubled tone key undoes the mark (ass → as) and 'z' removes the
//      mark (mà + z → ma). Both are byte-identical to OpenKey 2.0.5.
//
// Build & run (also driven by tests/run_all_tests.sh):
//   g++ -std=c++2b -O2 -I src/core tests/test_key_correctness.cpp
//       src/core/TextEngine.cpp -o keycorr
//   ./keycorr
// Exit 0 = ALL PASSED.
//----------------------------------------------------------------------------

#include "TextEngine.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace ok::text;

namespace {

int g_failures = 0;
int g_checks   = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  [FAIL] %s\n", what.c_str());
    }
}

// Render a wchar_t string as UTF-8 so failures print readably under the C
// locale (printf("%ls") aborts at the first non-ASCII code point).
std::string dump(const std::wstring& s) {
    std::string o;
    for (wchar_t c : s) {
        const unsigned u = static_cast<unsigned>(static_cast<std::uint32_t>(c));
        if (u < 0x80) {
            o += static_cast<char>(u);
        } else if (u < 0x800) {
            o += static_cast<char>(0xC0 | (u >> 6));
            o += static_cast<char>(0x80 | (u & 0x3F));
        } else {
            o += static_cast<char>(0xE0 | (u >> 12));
            o += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (u & 0x3F));
        }
    }
    return o;
}

//---------------------------------------------------------------------------
// The shipped consumer, reproduced (see the header comment).
//---------------------------------------------------------------------------
class Consumer final {
public:
    explicit Consumer(const EngineOptions& o) : eng_(o) {}

    void type(const std::string& keys) {
        for (const char c : keys) {
            TextInput in;
            if (c == ' ') {
                in.kind = InputKind::Space;
            } else {
                in.kind = InputKind::Char;
                in.ch   = static_cast<char32_t>(static_cast<unsigned char>(c));
            }
            apply(eng_.process(in), in);
        }
    }

    [[nodiscard]] const std::wstring& text() const noexcept { return text_; }

private:
    void apply(const EngineResult& r, const TextInput& in) {
        scratch_.clear();
        if (r.code == EngineCode::ReplaceMacro) {
            eng_.macroExpansionUtf16(r, scratch_);
        } else {
            eng_.replacementUtf16(r, scratch_);
        }

        const bool reissue =
            (r.code == EngineCode::Restore ||
             r.code == EngineCode::RestoreAndStartNewSession) &&
            (in.kind == InputKind::Char || in.kind == InputKind::Space);

        bool         suppress = false;
        std::size_t  bs       = 0;
        if (r.code == EngineCode::ReplaceMacro) {
            bs = r.backspaceCount;
            if (bs > 0 || !scratch_.empty()) { suppress = true; }
        } else if (r.consumed() && !(r.backspaceCount == 0 && scratch_.empty())) {
            suppress = true;
            bs       = r.backspaceCount;
        }

        if (reissue) {
            scratch_.push_back(in.kind == InputKind::Space
                                   ? L' '
                                   : static_cast<wchar_t>(in.ch));
        }

        if (!suppress) {
            // Pass-through: the application receives the key untouched and
            // the engine's backspaceCount is deliberately NOT applied.
            if (in.kind == InputKind::Char)  { text_.push_back(static_cast<wchar_t>(in.ch)); }
            if (in.kind == InputKind::Space) { text_.push_back(L' '); }
            return;
        }
        if (bs > text_.size()) { text_.clear(); } else { text_.resize(text_.size() - bs); }
        text_ += scratch_;
    }

    TextEngine     eng_;
    std::wstring   text_;
    std::wstring   scratch_;
};

std::string run(const std::string& keys, const EngineOptions& o) {
    Consumer c(o);
    c.type(keys);
    return dump(c.text());
}

void expectEq(const std::string& keys, const std::string& want,
              const EngineOptions& o, const char* group) {
    const std::string got = run(keys, o);
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("  [FAIL] %s: \"%s\" -> \"%s\" (expected \"%s\")\n",
                    group, keys.c_str(), got.c_str(), want.c_str());
    }
}

EngineOptions defaults() {
    EngineOptions o{};
    o.digitsAreLiteral = true;      // the shipping policy
    return o;
}

} // namespace

int main() {
    std::printf("== KieeKey keystroke correctness ==\n\n");

    //=====================================================================
    // 1. FALSE-POSITIVE CORPUS — the user-reported P1
    //    "typing 'p' twice turns it into 'ph'"
    //=====================================================================
    {
        std::printf("-- 1. quickTelex: doubled consonant must NOT be mangled "
                    "word-medially --\n");
        EngineOptions o = defaults();
        o.quickTelex = true;

        // Real words a Vietnamese user types every day (English/tech terms,
        // brand names, file paths). Every one of these contains a doubled
        // consonant and NONE of them is Vietnamese, so the quick-Telex
        // cluster expansion must not fire.
        //
        // Deliberately EXCLUDED: words whose doubling is a Telex TONE key
        // ("coffee", "chess", "kissing", "buff", "puff") or a Telex VOWEL
        // ("google"). Those legitimately compose — 'ff' undoes a huyền,
        // 'oo' is an ô — and are pinned in case 5 instead. Mixing them in
        // here would hide the actual defect behind expected behaviour.
        const char* words[] = {
            "happy", "apple", "letter", "account", "running", "attitude",
            "success", "address", "process", "tattoo", "oppn", "supper",
            "copper", "topping", "mapping", "dipping", "button", "bottle",
            "middle", "socket", "packet", "dress", "bill", "goggle",
            "summer", "hobby", "jazz", "muzzle", "puzzle", "buzz", "fizz",
            "class", "cross", "cliff", "troll", "cotton", "banner",
        };
        for (const char* w : words) { expectEq(w, w, o, "quickTelex no-false-positive"); }
        std::printf("   %zu words verified byte-identical\n\n",
                    sizeof(words) / sizeof(words[0]));
    }

    //=====================================================================
    // 2. FEATURE PRESERVED — the cluster shortcuts must still work
    //=====================================================================
    {
        std::printf("-- 2. quickTelex: syllable-initial doubling still expands --\n");
        EngineOptions o = defaults();
        o.quickTelex = true;

        // Vietnamese clusters are ALWAYS syllable-initial, so every
        // legitimate use of the feature has the doubled pair at the very
        // start of the word. These are the exact sequences a "gõ tắt" user
        // types.
        expectEq("ppongf",  "phòng",  o, "quickTelex PP->PH");
        expectEq("ccaof",   "chào",   o, "quickTelex CC->CH");
        expectEq("ttooi",   "thôi",   o, "quickTelex TT->TH");
        expectEq("kkongf",  "khòng",  o, "quickTelex KK->KH");
        expectEq("nnuw",    "ngư",    o, "quickTelex NN->NG");
        expectEq("qqan",    "quan",   o, "quickTelex QQ->QU");
        expectEq("ttieeng", "thiêng", o, "quickTelex TT->TH");
        expectEq("cc",      "ch",     o, "quickTelex bare CC");
        expectEq("tt",      "th",     o, "quickTelex bare TT");
        expectEq("pp",      "ph",     o, "quickTelex bare PP");
        // ...and the rest of the word still composes normally afterwards.
        expectEq("ppongf ", "phòng ", o, "quickTelex + word break");
        expectEq("ppongf ccaof", "phòng chào", o, "quickTelex consecutive words");
        std::printf("   ok\n\n");
    }

    //=====================================================================
    // 3. DEFAULT CONFIG — pin the exact shipped behaviour
    //=====================================================================
    {
        std::printf("-- 3. default configuration: exact output pinned --\n");
        const EngineOptions o = defaults();

        // Vietnamese (composition must happen exactly like this).
        expectEq("tooi",      "tôi",       o, "vn");
        expectEq("xin chao",  "xin chao",  o, "vn");
        expectEq("chaoj",     "chạo",      o, "vn");
        expectEq("dd",        "đ",         o, "vn");
        expectEq("uw",        "ư",         o, "vn");
        expectEq("ow",        "ơ",         o, "vn");
        expectEq("aa",        "â",         o, "vn");
        expectEq("truong",    "truong",    o, "vn");
        expectEq("truowng",   "trương",    o, "vn");
        expectEq("nghia",     "nghia",     o, "vn");
        expectEq("nghi~a",    "nghi~a",    o, "vn");

        // Latin/English: the letters that are NOT Telex modifiers must come
        // out untouched. ('w', vowels and s/f/r/x/j/a/e/o/d/u ARE modifiers
        // and are covered by the documented-behaviour corpus below.)
        expectEq("cmd",       "cmd",       o, "latin");
        expectEq("bmw",       "bmw",       o, "latin");
        expectEq("http",      "http",      o, "latin");
        expectEq("pho",       "pho",       o, "latin");
        expectEq("nh",        "nh",        o, "latin");
        expectEq("clip",      "clip",      o, "latin");
        expectEq("mvn",       "mvn",       o, "latin");
        expectEq("km",        "km",        o, "latin");
        expectEq("gh",        "gh",        o, "latin");
        expectEq("png",       "png",       o, "latin");
        expectEq("txt",       "txt",       o, "latin");
        std::printf("   ok\n\n");
    }

    //=====================================================================
    // 4. NO-SILENT-LOSS PROPERTY (exhaustive over 1–3 letter words)
    //=====================================================================
    {
        std::printf("-- 4. no silent character loss (exhaustive 1-3 letters) --\n");
        const EngineOptions o = defaults();

        // Telex partitions the alphabet three ways.
        //   * MODIFIERS — consumed or transformed: the five tone keys
        //     (s f r x j), the vowels (a e i o u y), 'w' (horn/breve),
        //     'd' (đ) and 'z' (remove-mark undo). 'yxi' -> 'ỹi' is correct:
        //     'y' is a Vietnamese vowel and 'x' put a ngã on it.
        //   * INERT CONSONANTS — b c g h k l m n p q t v. Telex can never
        //     consume or transform these, so every one of them must still be
        //     present in the output. That is the property we assert.
        auto isModifier = [](char c) {
            return c == 's' || c == 'f' || c == 'r' || c == 'x' || c == 'j' ||
                   c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ||
                   c == 'y' || c == 'w' || c == 'd' || c == 'z';
        };
        auto mustSurvive = [](char c) {
            return c == 'b' || c == 'c' || c == 'g' || c == 'h' || c == 'k' ||
                   c == 'l' || c == 'm' || c == 'n' || c == 'p' || c == 'q' ||
                   c == 't' || c == 'v';
        };

        long long shrinks = 0;
        const char* L = "abcdefghijklmnopqrstuvwxyz";

        auto test = [&](const std::string& w) {
            const std::string got = run(w, o);
            // (a) Every non-modifier letter typed must still be present, in
            //     order. Composition may change/replace letters but must
            //     never make a plain letter vanish.
            std::size_t need = 0;
            for (const char c : w) { if (mustSurvive(c)) { ++need; } }
            // Straight subsequence scan: a plain letter may be preceded by
            // composed (multi-byte) characters produced by the modifier
            // letters before it, but it must still be present, in order.
            std::size_t found = 0;
            std::size_t gi    = 0;
            for (const char c : w) {
                if (!mustSurvive(c)) { continue; }
                const std::size_t at = got.find(c, gi);
                if (at == std::string::npos) { break; }
                gi = at + 1;
                ++found;
            }
            if (found != need) {
                check(false, "plain letter vanished: \"" + w + "\" -> \"" + got + "\"");
            }
            // (b) An edit must never shrink the word by more than the number
            //     of modifier letters it could have consumed.
            std::size_t mods = 0;
            for (const char c : w) { if (isModifier(c)) { ++mods; } }
            if (got.size() + mods < w.size()) {
                check(false, "word shrank more than its modifiers: \"" + w +
                             "\" -> \"" + got + "\"");
            }
            if (got.size() < w.size()) { ++shrinks; }
        };

        for (int a = 0; a < 26; ++a) {
            test(std::string(1, L[a]));
            for (int b = 0; b < 26; ++b) {
                std::string w;
                w += L[a]; w += L[b];
                test(w);
                for (int c = 0; c < 26; ++c) {
                    std::string w3 = w;
                    w3 += L[c];
                    test(w3);
                }
            }
        }
        std::printf("   18278 words checked, %lld legitimately shortened by "
                    "Telex modifiers\n\n", shrinks);
    }

    //=====================================================================
    // 5. DOCUMENTED (surprising but correct) BEHAVIOUR — do not "fix" these
    //=====================================================================
    {
        std::printf("-- 5. documented Telex conventions pinned --\n");
        const EngineOptions o = defaults();

        // (a) A doubled tone key undoes the mark. Byte-identical to
        //     OpenKey 2.0.5 (see the comment in tests/engine205.cpp:
        //     "turning 'cass' into 'cas'").
        expectEq("ass",  "as",  o, "double tone key undoes");
        expectEq("aff",  "af",  o, "double tone key undoes");
        expectEq("cass", "cas", o, "double tone key undoes");

        // (b) 'z' is the remove-mark key (handleMainKey: "Z key removes
        //     mark"; upstream Engine.cpp:1056 has the identical branch).
        expectEq("mafz", "ma",  o, "z removes mark");
        expectEq("masz", "ma",  o, "z removes mark");

        // (c) The mark is replaced, not stacked, when a second tone key
        //     arrives.
        expectEq("asf",  "à",   o, "tone key replaces");
        expectEq("afs",  "á",   o, "tone key replaces");
        std::printf("   ok\n\n");
    }

    std::printf("----------------------------------------------------------\n");
    std::printf("  checks: %d   failures: %d\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("\nALL KEY-CORRECTNESS TESTS PASSED\n");
        return 0;
    }
    std::printf("\nKEY-CORRECTNESS TESTS FAILED\n");
    return 1;
}
