//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: src/core/VnComposer.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
// v1.3.0-beta3 (bug #2): in-window Vietnamese composition for the typing games.
//
// WHY THIS EXISTS
//   The Arcade typing games showed ASCII-only prompts ("KieeKey la bo go tieng
//   Viet ...") with no diacritics, and matched the player's keystrokes one
//   character at a time. A Vietnamese player could not type a real passage:
//   the games run inside KieeKey's OWN window, and the keyboard hook deliberately
//   BYPASSES the app's own windows (otherwise every keystroke would be composed
//   twice). So the IME never composed inside the game.
//
//   VnComposer gives each game its own composition engine — the SAME
//   ok::text::TextEngine the IME uses — so the player types Telex/VNI exactly as
//   configured and the game window composes it into precomposed Vietnamese, then
//   matches that against the (diacritic-bearing) target passage.
//
// PORTABILITY
//   Header-only and free of Win32: TextEngine is portable, so VnComposer is
//   exercised by native Linux tests (tests/test_vn_composer.cpp) — the composition
//   is proven, not asserted.
//
// CONSUMER MODEL
//   The apply() loop is byte-for-byte the model validated by tests/real_passages.cpp
//   (EngOraPair::feed/space): on a consumed result, erase backspaceCount chars from
//   the visible buffer and append replacementUtf16(); on Restore, re-append the
//   literal key; on a non-consumed result, append the literal key. That test types
//   real Vietnamese passages and asserts the accumulated buffer equals the intended
//   text, which is exactly the invariant VnComposer relies on.
#ifndef KIEEKEY_CORE_VNCOMPOSER_HPP
#define KIEEKEY_CORE_VNCOMPOSER_HPP

#include "TextEngine.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ok::arcade {

// Composes Telex/VNI keystrokes into precomposed Vietnamese using the IME engine.
// One instance per game session; cheap to reset.
class VnComposer {
public:
    explicit VnComposer(ok::text::InputMethod method = ok::text::InputMethod::Telex) noexcept {
        setMethod(method);
    }

    // Reconfigure the input method (Telex / VNI / SimpleTelex). Games never expand
    // macros and keep spell-check + restore so a wrong mark is corrected like the IME.
    void setMethod(ok::text::InputMethod method) noexcept {
        ok::text::EngineOptions o = m_engine.options();
        o.inputMethod = method;
        o.useMacro = false;
        o.useMacroInEnglishMode = false;
        o.checkSpelling = true;
        o.restoreIfWrongSpelling = true;
        // VNI spells tones with DIGITS (la8 → là), so digits must compose; Telex
        // spells tones with letters, so digits stay literal (a typed "2" is a "2").
        o.digitsAreLiteral = (method != ok::text::InputMethod::Vni);
        m_engine.setOptions(o);
        m_engine.resetForNewContext();
        m_composed.clear();
    }

    ok::text::InputMethod method() const noexcept { return m_engine.options().inputMethod; }

    // Clear the composed text and the engine's word state (new passage / restart).
    void reset() noexcept {
        m_composed.clear();
        m_engine.resetForNewContext();
    }

    // Feed one produced character. `caps` mirrors shift/caps-lock; when the caller
    // only has the already-cased character, pass caps = isUpperAscii(c) and the
    // LOWERCASED c (see feedProduced()).
    void feedChar(char32_t c, bool caps) noexcept {
        ok::text::TextInput in;
        in.kind = ok::text::InputKind::Char;
        in.ch = c;
        in.isCaps = caps;
        apply(m_engine.process(in), c, caps);
    }

    // Feed the space that ends a word (commits the composed syllable).
    void feedSpace() noexcept {
        ok::text::TextInput in;
        in.kind = ok::text::InputKind::Space;
        in.ch = U' ';
        const ok::text::EngineResult& r = m_engine.process(in);
        if (r.consumed()) {
            eraseAndAppend(r);
        } else {
            m_composed.push_back(U' ');
        }
    }

    // Feed a backspace. v1.3.0-beta4 — COHERENT per-character rewind.
    //
    // The beta3 model mirrored the IME contract (engine rewinds ONE RAW KEY,
    // composer pops ONE COMPOSED code point) and that split is the root cause
    // of the reported "sai rồi backspace thì không gõ tiếp được": after
    // "booj"→"bộ" one backspace leaves the engine holding raw "boo" while the
    // visible buffer shows "b" — from that point on every correctly typed
    // keystroke composes against a state the player cannot see, progress
    // freezes, and no amount of correct typing recovers the missing tone.
    // (Reproduced deterministically: the "wrong key + matched backspaces ->
    // correct continuation" fuzz failed 500/500 seeded runs before this fix.)
    //
    // The fix: the composed buffer is the single source of truth. A backspace
    // pops ONE composed code point (exactly what a text editor shows) and the
    // engine is returned to a fresh word state, so it can never keep a raw-key
    // history that disagrees with the visible text. Composition after the
    // reset is position-independent — the next keystrokes start a fresh word
    // whose replacement is appended to the buffer — which is what makes
    // mid-syllable repair work: "gõ" -> Backspace -> "g" -> type "ox" ->
    // "gõ" again. (A bare tone key right after the pop composes as a fresh
    // word, never as the popped syllable's tone — the player sees it on
    // screen and backspaces again; the state can never wedge.)
    void feedBackspace() noexcept {
        if (!m_composed.empty()) {
            m_composed.pop_back();   // one composed code point, editor-style
        }
        m_engine.resetForNewContext();
    }

    // Convenience for the games: derive caps from an already-cased ASCII character
    // and feed the lowercased base (the engine composes case from isCaps). Non-ASCII
    // produced characters (e.g. a precomposed syllable from an OS-level layout) are
    // appended verbatim — they are already final text.
    void feedProduced(char32_t c) noexcept {
        if (c == U' ') { feedSpace(); return; }
        if (c >= U'a' && c <= U'z') { feedChar(c, false); return; }
        if (c >= U'A' && c <= U'Z') { feedChar(c - U'A' + U'a', true); return; }
        if (c < 0x80) { feedChar(c, false); return; }   // digits/punctuation: literal
        m_composed.push_back(c);                          // already-composed Unicode
    }

    // The full composed text so far (precomposed Vietnamese, BMP code points).
    [[nodiscard]] const std::u32string& text() const noexcept { return m_composed; }
    [[nodiscard]] std::size_t length() const noexcept { return m_composed.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_composed.empty(); }

    // Longest common prefix of the composed text and `target` — the game's progress
    // (how many target code points the player has correctly composed).
    [[nodiscard]] std::size_t matchLength(const std::u32string& target) const noexcept {
        const std::size_t n = std::min(m_composed.size(), target.size());
        std::size_t i = 0;
        while (i < n && m_composed[i] == target[i]) { ++i; }
        return i;
    }

    // True once the composed text equals (or extends) the whole target.
    [[nodiscard]] bool completed(const std::u32string& target) const noexcept {
        return matchLength(target) >= target.size() && !target.empty();
    }

private:
    void apply(const ok::text::EngineResult& r, char32_t c, bool caps) noexcept {
        if (r.consumed()) {
            eraseAndAppend(r);
            if (r.code == ok::text::EngineCode::Restore ||
                r.code == ok::text::EngineCode::RestoreAndStartNewSession) {
                m_composed.push_back(visibleChar(c, caps));
            }
        } else {
            m_composed.push_back(visibleChar(c, caps));
        }
    }

    void eraseAndAppend(const ok::text::EngineResult& r) noexcept {
        const std::size_t b = std::min<std::size_t>(r.backspaceCount, m_composed.size());
        m_composed.erase(m_composed.size() - b, b);
        std::wstring rep;
        m_engine.replacementUtf16(r, rep);
        // Vietnamese precomposed syllables and combining marks are all BMP, so each
        // wchar_t is one code point on every platform. Decode a surrogate pair anyway
        // so a non-BMP glyph (emoji in a passage) can never corrupt the buffer on
        // Windows, where wchar_t is 16-bit.
        for (std::size_t i = 0; i < rep.size(); ++i) {
            const auto w = static_cast<char32_t>(rep[i]);
            if (sizeof(wchar_t) == 2 && w >= 0xD800u && w <= 0xDBFFu && (i + 1) < rep.size()) {
                const auto w2 = static_cast<char32_t>(rep[i + 1]);
                if (w2 >= 0xDC00u && w2 <= 0xDFFFu) {
                    m_composed.push_back(0x10000u + ((w - 0xD800u) << 10) + (w2 - 0xDC00u));
                    ++i;
                    continue;
                }
            }
            m_composed.push_back(w);
        }
    }

    static char32_t visibleChar(char32_t c, bool caps) noexcept {
        return (caps && c >= U'a' && c <= U'z') ? (c - U'a' + U'A') : c;
    }

    ok::text::TextEngine m_engine;
    std::u32string       m_composed;
};

} // namespace ok::arcade

#endif // KIEEKEY_CORE_VNCOMPOSER_HPP
