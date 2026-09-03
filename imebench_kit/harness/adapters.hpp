//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.1.2 - refactored and completed logic
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
// File: imebench_kit/harness/adapters.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// imebench_kit/harness/adapters.hpp — consumer-faithful adapters for the
// three engines (reconstruction of the frozen 3-way benchmark kit; the
// original kit folder was not part of the uploaded archive).
//
// Protocol invariants (master prompt §7 — the adapters encode real host
// behavior, verified against fcitx5-unikey and xim.c):
//   * ukengine charsetId           = CONV_CHARSET_XUTF8 (12)
//   * ukengine processBackspace    : `backs` already includes the physical
//     Backspace deletion — forward the raw BS only when backs == 0
//   * ok205 letters use lowercase keycode + caps flag; printable symbols go
//     through processSymbol (shift+key path)
//   * KieeKey input is the normalized TextInput; the consumer applies the
//     v3.1 contract (Char-Restore re-issue, Space-Restore re-issue, macro
//     expansion in-result)
//----------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>

#ifdef BENCH_WITH_UNIKEY
#include "ukengine.h"      // vendored UniKey (global namespace)
#include "vnconv.h"
#endif

#include "TextEngine.hpp"
#include "keygen.hpp"

namespace bench {

// One applied key event on the app-visible text (UTF-8).
struct Applied {
    bool changed = false;
};

//----------------------------------------------------------------------------
// 1. KieeKey — TextEngine + the verified consumer contract.
//----------------------------------------------------------------------------
class KieeKeyAdapter {
public:
    // dictionary=true enables the v3.1 lexicon-gated restore (P1); the
    // lexicon set is a general Vietnamese word list committed as data.
    KieeKeyAdapter(bool restore, bool dictionary, const std::vector<std::string>* lexicon = nullptr) {
        ok::text::EngineOptions o;
        o.inputMethod = ok::text::InputMethod::Telex;
        o.checkSpelling = true;
        o.restoreIfWrongSpelling = restore;
        o.useMacro = false;
        o.useDictionaryRestore = dictionary && lexicon != nullptr;
        eng_.setOptions(o);
        if (o.useDictionaryRestore) {
            lex_ = lexicon;
            eng_.setDictionaryResolver([this](const std::vector<std::uint32_t>& composed) {
                return lookup(composed);
            });
        }
    }
    void setMethod(int m) {
        ok::text::EngineOptions o = eng_.options();
        o.inputMethod = static_cast<ok::text::InputMethod>(m);
        eng_.setOptions(o);
    }
    void reset() { eng_.startNewSession(); text_.clear(); }
    void key(char32_t c) {
        ok::text::TextInput in;
        in.kind = ok::text::InputKind::Char;
        in.ch = c;
        const auto& r = eng_.process(in);
        apply(r, eng_.replacementUtf16(r), c);
    }
    void space() {
        ok::text::TextInput in; in.kind = ok::text::InputKind::Space;
        const auto& r = eng_.process(in);
        apply(r, eng_.replacementUtf16(r), U' ');
    }
    void backspace() {
        ok::text::TextInput in; in.kind = ok::text::InputKind::Backspace;
        static_cast<void>(eng_.process(in));
        if (!text_.empty()) { text_.pop_back(); }
    }
    const std::string& textUtf8() {
        utf8_.clear();
        for (wchar_t c : text_) {
            const char32_t cp = static_cast<char32_t>(c);
            if (cp < 0x80) { utf8_.push_back(static_cast<char>(cp)); }
            else if (cp < 0x800) {
                utf8_.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                utf8_.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                utf8_.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                utf8_.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                utf8_.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
        return utf8_;
    }

private:
    void apply(const ok::text::EngineResult& r, const std::wstring& rep, char32_t typed) {
        if (r.code == ok::text::EngineCode::ReplaceMacro) {
            std::size_t b = std::min<std::size_t>(r.backspaceCount, text_.size());
            text_.erase(text_.size() - b, b);
            for (std::uint32_t v : r.macroExpansion) { text_.push_back(static_cast<wchar_t>(v)); }
            return;
        }
        if (r.consumed()) {
            std::size_t b = std::min<std::size_t>(r.backspaceCount, text_.size());
            text_.erase(text_.size() - b, b);
            text_ += rep;
            if (r.code == ok::text::EngineCode::Restore ||
                r.code == ok::text::EngineCode::RestoreAndStartNewSession) {
                text_.push_back(static_cast<wchar_t>(typed));
            }
        } else {
            text_.push_back(static_cast<wchar_t>(typed));
        }
    }
    bool lookup(const std::vector<std::uint32_t>& composed) const {
        if (!lex_) { return false; }
        std::string s;
        for (std::uint32_t c : composed) {
            const char32_t cp = c;
            if (cp < 0x80) { s.push_back(static_cast<char>(cp)); }
            else if (cp < 0x800) {
                s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
        return std::binary_search(lex_->begin(), lex_->end(), s);
    }
    ok::text::TextEngine eng_;
    std::wstring text_;
    std::string utf8_;
    const std::vector<std::string>* lex_ = nullptr;
};

//----------------------------------------------------------------------------
// 3. UniKey UKEngine (vendored, unmodified).
//----------------------------------------------------------------------------
#ifdef BENCH_WITH_UNIKEY
class UnikeyAdapter {
public:
    UnikeyAdapter(bool autoRestore, bool freeMarking) {
        std::memset(&shm_, 0, sizeof(shm_));
        shm_.initialized = 1;
        shm_.vietKey = 1;
        shm_.iconShown = 0;
        shm_.usrKeyMapLoaded = 0;
        shm_.charsetId = CONV_CHARSET_XUTF8;      // frozen protocol (12)
        shm_.options.freeMarking = freeMarking ? 1 : 0;
        shm_.options.modernStyle = 0;
        shm_.options.macroEnabled = 0;
        shm_.options.useUnicodeClipboard = 0;
        shm_.options.alwaysMacro = 0;
        shm_.options.strictSpellCheck = 0;
        shm_.options.useIME = 0;
        shm_.options.spellCheckEnabled = 1;
        shm_.options.autoNonVnRestore = autoRestore ? 1 : 0;
        SetupUnikeyEngine();
        shm_.input.init();
        shm_.input.setIM(UkTelex);
        engine_.setCtrlInfo(&shm_);
    }
    void setMethod(int m) { shm_.input.setIM(static_cast<UkInputMethod>(m)); }
    void reset() { engine_.reset(); committed_.clear(); pending_.clear(); }
    void key(char32_t c) { process(c); }
    void space() { process(U' '); }
    void backspace() {
        int backs = 0;
        unsigned char out[512] = {};
        int outSize = sizeof(out);
        UkOutputType ot = UkCharOutput;
        const int ret = engine_.processBackspace(backs, out, outSize, ot);
        if (ret && backs > 0) {
            eraseUnits(static_cast<std::size_t>(backs));
        } else if (!pending_.empty()) {
            pending_.pop_back();
        } else if (!committed_.empty()) {
            committed_.pop_back();   // §7: forward the raw BS only when backs == 0
        }
    }
    const std::string& textUtf8() { utf8_ = committed_ + pending_; return utf8_; }

private:
    static unsigned int vkForChar(char32_t c) {
        if (c >= U'a' && c <= U'z') return static_cast<unsigned int>(U'a') + (c - U'a');
        if (c == U' ') return 32;
        if (c == U',') return 188;
        if (c == U'.') return 190;
        if (c == U';') return 186;
        if (c == U'\'') return 222;
        if (c == U'[') return 219;
        if (c == U']') return 221;
        return static_cast<unsigned int>(c);
    }
    void process(char32_t c) {
        int backs = 0;
        unsigned char out[512] = {};
        int outSize = sizeof(out);
        UkOutputType ot = UkCharOutput;
        const int ret = engine_.process(vkForChar(c), backs, out, outSize, ot);
        const bool isBreak = (c == U' ' || c == U',' || c == U'.' || c == U';' ||
                              c == U'\'' || c == U'[' || c == U']');
        if (ret != 0) {
            if (backs > 0) { eraseUnits(static_cast<std::size_t>(backs)); }
            pending_ += std::string(reinterpret_cast<const char*>(out),
                                    static_cast<std::size_t>(outSize));
        } else if (isBreak) {
            committed_ += pending_;
            committed_.push_back(static_cast<char>(c));
            pending_.clear();
        } else {
            pending_.push_back(static_cast<char>(c));
        }
    }
    void eraseUnits(std::size_t n) {
        // Erase n UTF-16 code units from the pending tail (XUTF8 = UTF-8:
        // one code point per UTF-16 unit for Vietnamese text).
        while (n > 0) {
            std::string& s = !pending_.empty() ? pending_ : committed_;
            if (s.empty()) { break; }
            std::size_t cut = 1;
            while (cut < s.size() && (s[s.size() - cut] & 0xC0) == 0x80) { ++cut; }
            s.erase(s.size() - cut, cut);
            --n;
        }
    }
    UkSharedMem shm_;
    UkEngine engine_;
    std::string committed_, pending_, utf8_;
};
#endif // BENCH_WITH_UNIKEY

} // namespace bench
