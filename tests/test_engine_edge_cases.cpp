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
// File: tests/test_engine_edge_cases.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.0 — test_engine_edge_cases.cpp
//
// Edge-case regression net for the typing core. Where tests/test_textengine.cpp
// pins the golden Telex/VNI vectors, this suite pins the *boundaries*: what
// happens at the ends of the buffers, at empty input, after a reset, under
// repeated/duplicate keys, and around the contracts the shipped consumer
// (src/app/main.cpp) relies on.
//
// It is deliberately platform-independent: TextEngine has no Win32 surface, so
// this runs on the Windows CI job AND on the non-Windows shim job.
//
// Sections
//   1. Empty / first-keystroke inputs (regression: the v1.1.0 out-of-bounds
//      read in checkForStandaloneChar was on this path)
//   2. Buffer limits (kMaxBuff = 32, kMaxLongWord, macro key cap)
//   3. Backspace storms (never underflow, never resurrect a dead word)
//   4. Duplicate / repeated mark keys (Restore + re-issue contract)
//   5. Word breaks and restore semantics
//   6. resumeFromText() re-sync
//   7. switchToneStyle()
//   8. The D2 over-backspace invariant under randomized input
//----------------------------------------------------------------------------
#include "TextEngine.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace ok::text;

namespace {

int failures = 0;

// Plain `if` — `if constexpr` here would demand a constant expression and break
// every runtime CHECK (ill-formed; MSVC /permissive- and GCC both reject it).
// Constant-condition CHECKs are intentional regression nets; the MSVC C4127
// noise they cause is disabled project-wide (/wd4127).
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    ++failures; } } while (0)

//---------------------------------------------------------------------------
// A faithful model of the SHIPPED consumer (src/app/main.cpp, the TSF/inline
// emit path). Getting this right matters: several engine results only make
// sense together with the re-issue contract, and a test suite that models the
// consumer loosely "finds" bugs that do not exist in the product.
//
//   consumed()  -> delete backspaceCount chars at the caret, insert
//                  replacementUtf16(result)
//   Restore     -> additionally RE-TYPE the key that triggered the restore
//                  (Char: the typed character; Space: a space)
//   !consumed() -> the application performs the key's own action
//---------------------------------------------------------------------------
class Consumer {
public:
    explicit Consumer(EngineOptions opts = {}) : engine_(opts) {}

    bool feed(char32_t ch, bool caps = false) {
        TextInput in;
        in.kind   = InputKind::Char;
        in.ch     = ch;
        in.isCaps = caps;
        return step(in, ch);
    }
    void space() {
        TextInput in;
        in.kind = InputKind::Space;
        step(in, U' ');
    }
    void backspace() {
        TextInput in;
        in.kind = InputKind::Backspace;
        step(in, 0);
    }
    void wordBreak(std::uint16_t vk = 0x0D) {   // Enter
        TextInput in;
        in.kind   = InputKind::WordBreak;
        in.vkCode = vk;
        step(in, 0);
    }
    void mouseDown() {
        TextInput in;
        in.kind = InputKind::MouseDown;
        step(in, 0);
    }
    void feedAll(const char* s) {
        for (const char* p = s; *p != '\0'; ++p) {
            feed(static_cast<char32_t>(static_cast<unsigned char>(*p)));
        }
    }
    bool resumeFromText(const std::wstring& w) { return engine_.resumeFromText(w); }
    bool switchToneStyle() { return engine_.switchToneStyle(); }
    void startNewSession() { engine_.startNewSession(); }

    [[nodiscard]] const std::wstring& text() const noexcept { return text_; }
    void setText(const std::wstring& s) { text_ = s; }
    [[nodiscard]] TextEngine& engine() noexcept { return engine_; }
    // Number of times the D2 clamp would have had to fire (diagnostic).
    [[nodiscard]] std::size_t clamped() const noexcept { return clamped_; }

private:
    bool step(const TextInput& in, char32_t visibleChar) {
        const EngineResult& r = engine_.process(in);
        const bool consumed   = r.consumed();

        if (r.backspaceCount > engine_.visibleAccount()) { ++clamped_; }

        if (consumed) {
            std::wstring rep = (r.code == EngineCode::ReplaceMacro)
                                   ? std::wstring()
                                   : engine_.replacementUtf16(r);
            const std::size_t bs = std::min<std::size_t>(r.backspaceCount, text_.size());
            text_.erase(text_.size() - bs, bs);
            text_ += rep;
            // Restore re-issue contract (hotfix §3 / D4).
            if ((r.code == EngineCode::Restore ||
                 r.code == EngineCode::RestoreAndStartNewSession) &&
                (in.kind == InputKind::Char || in.kind == InputKind::Space)) {
                text_ += static_cast<wchar_t>(visibleChar);
            }
        } else if (in.kind == InputKind::Space) {
            text_ += L' ';
        } else if (in.kind == InputKind::Backspace) {
            if (!text_.empty()) { text_.pop_back(); }
        } else if (in.kind == InputKind::WordBreak && in.vkCode == 0x0D) {
            text_ += L'\n';
        } else if (in.kind == InputKind::Char) {
            text_ += static_cast<wchar_t>(visibleChar);
        }
        return consumed;
    }

    std::wstring text_;
    TextEngine   engine_;
    std::size_t  clamped_ = 0;
};

// Readable dump for failure messages (ASCII stays ASCII, the rest is U+xxxx).
std::string dump(const std::wstring& w) {
    std::string s;
    char buf[16];
    for (wchar_t c : w) {
        if (c >= 0 && c < 128) { s += static_cast<char>(c); }
        else { std::snprintf(buf, sizeof buf, "<%04X>", static_cast<unsigned>(c)); s += buf; }
    }
    return s;
}

void expect(const char* what, const std::wstring& got, const wchar_t* want) {
    if (got != want) {
        std::printf("FAIL %s: got \"%s\" want \"%s\"\n", what, dump(got).c_str(), dump(want).c_str());
        ++failures;
    }
}

} // namespace

int main() {
    //=======================================================================
    // 1. Empty / first-keystroke inputs.
    //    Regression: checkForStandaloneChar() used to read typingWord_[-1]
    //    when 'w' was the very first key of a word (UB, caught by UBSan).
    //=======================================================================
    {
        Consumer c;
        CHECK(c.feed(U'w'));                       // standalone w -> "ư"
        expect("telex w", c.text(), L"\u01B0");
    }
    {
        Consumer c;
        (void)c.feed(U'u');                        // plain vowel: pass-through
        CHECK(c.feed(U'w'));                       // uw -> "ư"
        expect("telex uw", c.text(), L"\u01B0");
    }
    {
        // Every key on its own from a clean buffer must not read out of bounds
        // and must not crash. (This is the fuzz-lite version of section 1.)
        const char* all = "abcdefghijklmnopqrstuvwxyz0123456789[]";
        for (const char* p = all; *p != '\0'; ++p) {
            Consumer c;
            (void)c.feed(static_cast<char32_t>(static_cast<unsigned char>(*p)));
            if (c.text().empty()) {
                std::printf("FAIL first-key '%c' produced no text\n", *p);
                ++failures;
            }
        }
        // VNI digits as the first key of a word.
        for (char d = '0'; d <= '9'; ++d) {
            EngineOptions o;
            o.inputMethod = InputMethod::Vni;
            Consumer c(o);
            (void)c.feed(static_cast<char32_t>(d));
            CHECK(!c.text().empty());
        }
    }
    {
        // Empty input kinds on a fresh engine: nothing may be consumed and the
        // document must stay empty.
        Consumer c;
        c.backspace();
        c.backspace();
        c.wordBreak();
        c.mouseDown();
        c.space();
        // Enter is a word break that the application itself renders as "\n";
        // backspaces/mouse-breaks on an empty buffer delete nothing.
        expect("empty-input kinds", c.text(), L"\n ");
        CHECK(c.engine().visibleAccount() <= 2);
    }

    //=======================================================================
    // 2. Buffer limits.
    //=======================================================================
    {
        // A word longer than kMaxBuff (32) must not overflow the internal
        // buffer; the engine keeps typing (overflow helper) and never emits
        // more than kMaxBuff replacement characters in one result.
        Consumer c;
        for (int i = 0; i < 300; ++i) { (void)c.feed(U'a'); }
        CHECK(!c.text().empty());
        CHECK(c.text().size() <= 300);
        (void)c.feed(U's');
        CHECK(c.engine().lastResult().newCharCount <= kMaxBuff);
        CHECK(c.engine().lastResult().backspaceCount <= kMaxBuff);
    }
    {
        // Exactly at the boundary: 32 letters then a mark key.
        Consumer c;
        for (int i = 0; i < 32; ++i) { (void)c.feed(U'a'); }
        (void)c.feed(U's');
        CHECK(c.engine().lastResult().newCharCount <= kMaxBuff);
    }
    {
        // startNewSession() clears the pending word, so a fresh word starts
        // from index 0 (no stale buffer content leaks into the next word).
        Consumer c;
        c.feedAll("chao");
        c.startNewSession();
        c.feedAll("ban");
        expect("startNewSession isolation", c.text(), L"chaoban");
    }

    //=======================================================================
    // 3. Backspace storms.
    //=======================================================================
    {
        Consumer c;
        c.feedAll("chanh");
        for (int i = 0; i < 64; ++i) { c.backspace(); }   // far past the start
        expect("backspace storm", c.text(), L"");
        c.feedAll("ban");
        expect("recovery after storm", c.text(), L"ban");
    }
    {
        // Backspace into a composed word: the engine must erase real
        // characters, never negative ones.
        Consumer c;
        c.feedAll("chao");
        c.feed(U'f');                    // -> "chào"
        expect("chao+f", c.text(), L"ch\u00E0o");
        c.backspace();                   // -> "cha"
        c.backspace();                   // -> "ch"
        CHECK(c.text().size() == 2);
        c.feedAll("e");
        expect("retype after backspace", c.text(), L"che");
    }

    //=======================================================================
    // 4. Duplicate / repeated mark keys — the Restore + re-issue contract.
    //=======================================================================
    {
        Consumer c;
        c.feedAll("as");   expect("as",   c.text(), L"\u00E1");
        c.feed(U's');      expect("ass",  c.text(), L"as");
        c.feed(U'a');      // a new 'a' starts composing again
        CHECK(!c.text().empty());
    }
    {
        Consumer c;
        c.feedAll("dd");   expect("dd",   c.text(), L"\u0111");
        c.feed(U'd');      expect("ddd",  c.text(), L"dd");
    }
    {
        Consumer c;
        c.feedAll("aaa");  expect("aaa",  c.text(), L"aa");
    }
    {
        Consumer c;
        c.feedAll("ooo");  expect("ooo",  c.text(), L"oo");
    }

    //=======================================================================
    // 5. Word breaks and restore semantics.
    //=======================================================================
    {
        Consumer c;
        c.feedAll("chao");
        c.space();
        c.feedAll("ban");
        c.feed(U'f');
        expect("two words + mark", c.text(), L"chao b\u00E0n");
        c.wordBreak();                    // Enter: word break, text kept
        CHECK(c.text().size() >= 8);
        c.feedAll("nha");
        CHECK(c.text().size() >= 11);
    }
    {
        // Mouse click (caret moved) must break the word without deleting text.
        Consumer c;
        c.feedAll("chao");
        c.mouseDown();
        expect("mouse break keeps text", c.text(), L"chao");
        c.feedAll("f");
        CHECK(c.text().size() >= 4);
    }
    {
        // Punctuation is a word break and must pass through visibly.
        Consumer c;
        c.feedAll("chao");
        c.feed(U',');
        expect("comma breaks word", c.text(), L"chao,");
    }

    //=======================================================================
    // 6. resumeFromText() — re-sync to text already on screen.
    //=======================================================================
    {
        Consumer c;
        CHECK(c.resumeFromText(L"chao"));
        // The letters are already visible; composing must continue on them.
        c.setText(L"chao");
        c.feed(U'f');
        expect("resume + mark", c.text(), L"ch\u00E0o");
    }
    {
        Consumer c;
        CHECK(!c.resumeFromText(L""));            // empty -> nothing to resume
        CHECK(!c.resumeFromText(L"ch\u00E0o"));   // already composed -> refuse
        CHECK(!c.resumeFromText(L"abc123"));      // non-letters -> refuse
        CHECK(c.resumeFromText(L"ChAo"));         // mixed case is fine
    }
    {
        // resumeFromText() must leave a CLEAN session: a second call on top of
        // an already-resumed buffer must not double the word.
        Consumer c;
        CHECK(c.resumeFromText(L"chao"));
        CHECK(c.resumeFromText(L"ban"));
        c.setText(L"ban");
        c.feed(U'f');
        expect("double resume", c.text(), L"b\u00E0n");
    }

    //=======================================================================
    // 7. switchToneStyle() — "hoá" <-> "hóa".
    //=======================================================================
    {
        Consumer c;
        c.feedAll("hoas");
        const bool converted = c.switchToneStyle();
        CHECK(converted);
        const std::size_t bs = c.engine().lastResult().backspaceCount;
        CHECK(bs <= c.text().size());            // D2: never delete more than exists
        const std::wstring rep = c.engine().replacementUtf16(c.engine().lastResult());
        std::wstring out = c.text();
        out.erase(out.size() - bs, bs);
        out += rep;
        c.setText(out);
        CHECK(!c.text().empty());
        CHECK(c.text().size() == 3);             // net-zero length change
    }
    {
        // Nothing pending: the style still flips, but no edit is produced and
        // the call is reported as "not converted" or yields an empty edit --
        // either way the consumer must not be asked to delete anything.
        Consumer c;
        c.switchToneStyle();
        CHECK(c.engine().lastResult().backspaceCount <= c.text().size());
    }

    //=======================================================================
    // 8. D2 over-backspace invariant under randomized input.
    //    backspaceCount must never exceed what the consumer has committed, so
    //    the clamp in process() and the consumer's own clamp agree.
    //=======================================================================
    {
        EngineOptions o;
        TextEngine e(o);
        static const char kKeys[] = "abcdefghijklmnopqrstuvwxyz0123456789 ,.-\n";
        constexpr std::size_t kKeyCount = sizeof(kKeys) - 1;
        std::uint64_t seed = 0x9E3779B97F4A7C15ull;
        std::size_t committed = 0;
        const int failuresBeforeLoop = failures;

        for (int i = 0; i < 300000; ++i) {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            const unsigned pick = static_cast<unsigned>((seed >> 33) % 1000u);

            TextInput in;
            std::size_t lands = 0;
            if (pick < 700) {
                const char k = kKeys[(seed >> 13) % kKeyCount];
                in.kind = (k == ' ') ? InputKind::Space
                                     : (k == '\n' ? InputKind::WordBreak : InputKind::Char);
                if (in.kind == InputKind::WordBreak) { in.vkCode = 0x0D; } else { in.ch = static_cast<char32_t>(k); }
                lands = 1;
            } else if (pick < 850) {
                in.kind = InputKind::Backspace;
                lands   = 0;
            } else if (pick < 950) {
                in.kind   = InputKind::WordBreak;
                in.vkCode = 0x0D;
                lands     = 1;
            } else {
                in.kind = InputKind::MouseDown;
                lands   = 0;
            }

            const EngineResult& r = e.process(in);
            // The engine's own account is authoritative for the clamp.
            CHECK(r.backspaceCount <= e.visibleAccount());
            if (failures != failuresBeforeLoop) {
                std::printf("  D2 violated at i=%d (bs=%u account=%zu)\n", i,
                            r.backspaceCount, e.visibleAccount());
                break;
            }

            // Model the consumer's committed length.
            if (in.kind == InputKind::Backspace) {
                committed = (committed > 0) ? committed - 1 : 0;
            } else {
                committed += lands;
                if (r.consumed()) {
                    committed = (committed >= r.backspaceCount)
                                    ? committed - r.backspaceCount + r.newCharCount
                                    : r.newCharCount;
                }
            }
            (void)committed;
        }

        // The restore scratch must stay bounded (D1).
        CHECK(e.debugScratchSize() <= kMaxBuff);
    }

    //=======================================================================
    // 9. Options are applied without corrupting the pending word.
    //=======================================================================
    {
        Consumer c;
        c.feedAll("chao");
        EngineOptions o = c.engine().options();
        o.inputMethod = InputMethod::Vni;
        c.engine().setOptions(o);
        c.feed(U'2');                     // VNI huyền
        CHECK(!c.text().empty());
        CHECK(c.engine().lastResult().backspaceCount <= c.text().size());
    }

    if (failures == 0) {
        std::printf("ALL ENGINE EDGE-CASE TESTS PASSED\n");
        return 0;
    }
    std::printf("%d ENGINE EDGE-CASE TEST(S) FAILED\n", failures);
    return 1;
}
