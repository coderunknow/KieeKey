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
// File: tests/vi_oracle.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.0 — tests/vi_oracle.hpp
// CLEAN-ROOM REFERENCE ORACLE for the Vietnamese composition algorithm.
//
// This is an INDEPENDENT implementation of the published Telex / VNI /
// Simple-Telex algorithm. It intentionally does NOT include TextEngine.hpp
// and does NOT call TextEngine: it models the algorithm from the published
// rule tables (VietnameseTables.hpp, consumed here strictly as static data)
// plus the documented algorithm semantics (vowel transforms, tone placement,
// w-insertion, backspace, reset/finalization, mode switches, macros).
//
// The engine and the oracle are two separate implementations of the same
// spec; differential testing between them surfaces defects in either.
//
// Internal encoding mirrors the engine's bit-encoding (the mask constants
// below are interface data from VietnameseTables.hpp):
//   bits 0-15   : key code (uppercase letter / digit / bracket)
//   kCapsMask   : shifted / caps
//   kToneMask   : hat (â ê ô)
//   kToneWMask  : ư / ơ (w tone)
//   kMark1..5   : sắc huyền hỏi ngã nặng
//   kStandaloneMask : standalone ư/ơ produced by w / [ / ] alone
//----------------------------------------------------------------------------
#pragma once

#include "VietnameseTables.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace orel {

// Published rule tables from VietnameseTables.hpp, consumed strictly as data.
using ok::text::kProcessingChar;
using ok::text::kVowel;
using ok::text::kVowelCombine;
using ok::text::kConsonantD;
using ok::text::kVowelForMark;
using ok::text::kConsonantTable;
using ok::text::kEndConsonantTable;
using ok::text::kStandaloneWBad;
using ok::text::kDoubleWAllowed;
using ok::text::kQuickStartConsonant;
using ok::text::kQuickEndConsonant;
using ok::text::kQuickTelex;
using ok::text::kEndConsonantMask;
using ok::text::kConsonantAllowMask;
using ok::text::codeTableFor;
using ok::text::kUnicodeCompoundMarks;
using ok::text::FlatVec;

inline constexpr std::uint32_t kCapsMask        = 0x10000u;
inline constexpr std::uint32_t kToneMask        = 0x20000u;
inline constexpr std::uint32_t kToneWMask       = 0x40000u;
inline constexpr std::uint32_t kMark1Mask       = 0x80000u;
inline constexpr std::uint32_t kMark2Mask       = 0x100000u;
inline constexpr std::uint32_t kMark3Mask       = 0x200000u;
inline constexpr std::uint32_t kMark4Mask       = 0x400000u;
inline constexpr std::uint32_t kMark5Mask       = 0x800000u;
inline constexpr std::uint32_t kMarkMask        = 0xF80000u;
inline constexpr std::uint32_t kCharMask        = 0xFFFFu;
inline constexpr std::uint32_t kStandaloneMask  = 0x1000000u;
inline constexpr std::uint32_t kCharCodeMask    = 0x2000000u;
inline constexpr std::uint32_t kPureCharMask    = 0x80000000u;
inline constexpr std::uint32_t kMaxBuff         = 32;
inline constexpr std::uint32_t kMaxMacroKey     = 255;
inline constexpr std::uint32_t kMaxLongWord     = 2048;

enum class Method : std::uint8_t { Telex = 0, Vni = 1, SimpleTelex = 2 };
enum class CodeTable : std::uint8_t {
    Unicode = 0, Tcvn3 = 1, VniWindows = 2, UnicodeCompound = 3, Cp1258 = 4,
};

struct Options {
    Method      method   = Method::Telex;
    CodeTable   table    = CodeTable::Unicode;
    bool checkSpelling        = true;
    bool modernOrthography    = false;
    bool quickTelex           = false;
    bool restoreIfWrongSpelling = true;
    bool freeMark             = false;
    bool allowConsonantZfwj   = false;
    bool quickStartConsonant  = false;
    bool quickEndConsonant    = false;
    bool upperCaseFirstChar   = false;
    bool useMacro             = true;
    bool useMacroInEnglishMode = false;
    // P1 (v3.1): dictionary-assisted foreign-word protection (mirror of the
    // engine's EngineOptions::useDictionaryRestore — see TextEngine.hpp).
    bool useDictionaryRestore = false;
};

enum class Kind : std::uint8_t {
    Char, Space, Backspace, WordBreak, MouseDown,
};

struct Event {
    Kind     kind  = Kind::Char;
    char32_t ch    = 0;
    std::uint16_t vk = 0;
    bool     caps  = false;
    bool     ctrl  = false;
};

enum class Code : std::uint8_t {
    DoNothing = 0, WillProcess, BreakWord, Restore, ReplaceMacro,
    RestoreAndStartNewSession,
};

struct Result {
    Code       code = Code::DoNothing;
    std::uint32_t backspaceCount = 0;
    std::uint32_t newCharCount   = 0;
    std::wstring replacement;      // ready-to-insert UTF-16, oldest-first
    std::vector<std::uint32_t> macroKey{};
    // ReplaceMacro payload (D3 contract — mirrors EngineResult::macroExpansion):
    // resolved macro expansion as final Unicode code points; empty unless
    // code == ReplaceMacro.
    std::vector<std::uint32_t> macroExpansion{};

    [[nodiscard]] bool consumed() const noexcept { return code != Code::DoNothing; }
};

// Consumer-installed macro table lookup: key-sequence (encoded keys, caps
// optional) -> expansion data. Mirror of the engine's MacroResolver.
using MacroResolver = std::function<bool(const std::vector<std::uint32_t>& key,
                                        std::vector<std::uint32_t>& data)>;

//----------------------------------------------------------------------------
// Oracle — one instance per session; state mirrors the engine's.
//----------------------------------------------------------------------------
class Oracle {
public:
    explicit Oracle(Options opts = {}) : opts_(opts) {
        useSpellingBefore_ = opts_.checkSpelling;
        startNewSession();
    }

    const Result& process(const Event& in) {
        res_ = Result{};
        const bool caps = in.caps;
        switch (in.kind) {
            case Kind::Char: {
                const char32_t raw = in.ch;
                if (raw == 0) { return res_; }
                const bool chCaps = caps || (raw >= U'A' && raw <= U'Z');
                const char32_t c = toUpperAscii(raw);
                if ((isNumberKey(c) && chCaps) || in.ctrl ||
                    isWordBreakChar(c) || (index_ == 0 && isNumberKey(c))) {
                    wordBreakBranch(c, chCaps, in, isMacroBreakChar(c));
                } else {
                    mainKeyBranch(c, chCaps);
                }
                break;
            }
            case Kind::Space:     spaceBranch(in.caps); break;
            case Kind::Backspace: backspaceBranch();    break;
            case Kind::WordBreak: wordBreakBranch(0, caps, in, in.vk == 0x0D); break;
            case Kind::MouseDown: wordBreakBranch(0, caps, in, false); break;
        }

        // D2 — over-backspace policy + consumer-visible accounting
        // (exact mirror of TextEngine::process; see the engine for the
        // full contract rationale).
        if (res_.backspaceCount > visibleAccount_) {
            res_.backspaceCount = visibleAccount_;
        }
        switch (in.kind) {
            case Kind::Backspace:
                if (visibleAccount_ > 0) { --visibleAccount_; }
                break;
            default:
                break;
        }
        if (res_.code == Code::ReplaceMacro) {
            const std::size_t expLen = res_.macroExpansion.size();
            visibleAccount_ = visibleAccount_ > res_.backspaceCount
                                  ? visibleAccount_ - res_.backspaceCount + expLen
                                  : expLen;
        } else {
            switch (in.kind) {
                case Kind::Char:
                    if (res_.code == Code::DoNothing) {
                        ++visibleAccount_;
                    } else {
                        visibleAccount_ += res_.newCharCount;
                        if (res_.code == Code::Restore ||
                            res_.code == Code::RestoreAndStartNewSession) {
                            ++visibleAccount_;
                        }
                        visibleAccount_ -= res_.backspaceCount;
                    }
                    break;
                case Kind::Space:
                    if (res_.code == Code::DoNothing) {
                        ++visibleAccount_;
                    } else {
                        visibleAccount_ += res_.newCharCount + 1;
                        visibleAccount_ -= res_.backspaceCount;
                    }
                    break;
                case Kind::WordBreak:
                    if (in.vk == 0x0D) { ++visibleAccount_; }
                    break;
                default:
                    break;
            }
        }

        finalizeResult();
        return res_;
    }

    // English-mode macro hook (mirror of the engine's processEnglishMode).
    const Result& processEnglishMode(const Event& in) {
        res_ = Result{};
        if (in.kind == Kind::MouseDown || (in.ctrl && !in.caps)) {
            macroKey_.clear();
            willTempOffEngine_ = false;
        } else if (in.kind == Kind::Space) {
            if (!hasHandledMacro_ && findMacro(macroKey_, macroData_)) {
                res_.code = Code::ReplaceMacro;
                res_.backspaceCount = static_cast<std::uint32_t>(macroKey_.size());
                setMacroExpansionFromResolver();
            }
            macroKey_.clear();
            willTempOffEngine_ = false;
        } else if (in.kind == Kind::Backspace) {
            if (!macroKey_.empty()) {
                macroKey_.pop_back();
            } else {
                willTempOffEngine_ = false;
            }
        } else {
            const bool breakKey = (in.kind == Kind::WordBreak) ||
                                  (in.kind == Kind::Char && isWordBreakChar(toUpperAscii(in.ch)));
            if (breakKey) {
                macroKey_.clear();
                willTempOffEngine_ = false;
            } else if (in.kind == Kind::Char) {
                if (!willTempOffEngine_) {
                    pushMacroKey(toUpperAscii(in.ch) | (in.caps ? kCapsMask : 0));
                }
            }
        }
        return res_;
    }

    void setOptions(Options o) noexcept { opts_ = o; useSpellingBefore_ = opts_.checkSpelling; }
    void setMacroResolver(MacroResolver r) noexcept { macroResolver_ = std::move(r); }
    // P1 mirror of the engine's DictionaryResolver.
    using DictionaryResolver = std::function<bool(const std::vector<std::uint32_t>& composed)>;
    void setDictionaryResolver(DictionaryResolver r) noexcept { dictResolver_ = std::move(r); }
    void tempOffSpellChecking() noexcept {
        if (useSpellingBefore_) { opts_.checkSpelling = !opts_.checkSpelling; }
    }
    void tempOffEngine(bool off) noexcept { willTempOffEngine_ = off; }
    void startNewSession() noexcept {
        index_ = 0;
        stateIndex_ = 0;
        tempDisableKey_ = false;
        hasHandledMacro_ = false;
        hasHandleQuickConsonant_ = false;
        midWordToggle_ = false;   // v3.3.1 mirror
        longWordHelper_.clear();
    }
    [[nodiscard]] const Options& options() const noexcept { return opts_; }
    [[nodiscard]] bool overflowDetected() const noexcept { return overflowDetected_; }
    // Size of the parallel history buffer mirror. The engine is at risk of
    // its known pushTypingState stack overflow as soon as this is >= kMaxBuff
    // (stale data left by restoreLastTypingState) and the word grows past
    // kMaxBuff before the next saveWord. The harness abandons the sequence at
    // that point so the engine's UB never executes.
    [[nodiscard]] std::size_t staleHistorySize() const noexcept {
        return typingStatesData_.size();
    }

private:
    // ---- encoding helpers ----
    static constexpr char32_t toUpperAscii(char32_t c) noexcept {
        return (c >= U'a' && c <= U'z') ? static_cast<char32_t>(c - 32) : c;
    }
    static constexpr bool isNumberKey(char32_t c) noexcept { return c >= U'0' && c <= U'9'; }
    static constexpr bool isWordBreakChar(char32_t c) noexcept {
        // Mirrors TextEngine::isWordBreakChar (engine fix: shifted-symbol chars
        // are word breaks, matching the 2.0.5 hook which delivers the raw key
        // with the shift bit). Must stay in lockstep with the engine.
        return c == U',' || c == U'.' || c == U'/' || c == U';' || c == U'\'' ||
               c == U'\\' || c == U'-' || c == U'=' || c == U'`' ||
               c == U'!' || c == U'@' || c == U'#' || c == U'$' || c == U'%' ||
               c == U'^' || c == U'&' || c == U'*' || c == U'(' || c == U')' ||
               c == U'_' || c == U'+' || c == U'{' || c == U'}' || c == U'|' ||
               c == U':' || c == U'"' || c == U'<' || c == U'>' || c == U'?' ||
               c == U'~';
    }
    static constexpr bool isMacroBreakChar(char32_t c) noexcept {
        return c == U',' || c == U'.' || c == U'/' || c == U';' || c == U'\'' ||
               c == U'\\' || c == U'-' || c == U'=';
    }
    static constexpr bool isCharKeyCodeChar(char32_t c) noexcept {
        return c == U'`' || (c >= U'0' && c <= U'9') || c == U'-' || c == U'=' ||
               c == U'[' || c == U']' || c == U'\\' || c == U';' || c == U'\'' ||
               c == U',' || c == U'.' || c == U'/';
    }
    static constexpr bool isWordBreakVk(std::uint16_t vk) noexcept {
        switch (vk) {
            case 0x1B: case 0x09: case 0x0D:
            case 0x25: case 0x26: case 0x27: case 0x28:
            case 0x24: case 0x23: case 0x2D: case 0x2E:
            case 0x21: case 0x22:
            case 0x2C: case 0x2A: case 0x29: case 0x2F:
            case 0x2B: case 0x90: case 0x91:
                return true;
            default: return false;
        }
    }
    bool isSpecialKey(char32_t c) const noexcept {
        const auto m = opts_.method;
        if (m == Method::Telex || m == Method::SimpleTelex) {
            return c == U'W' || c == U'E' || c == U'R' || c == U'O' || c == U'[' ||
                   c == U']' || c == U'A' || c == U'S' || c == U'D' || c == U'F' ||
                   c == U'J' || c == U'Z' || c == U'X' || c == U'W';
        }
        return isNumberKey(c);
    }
    constexpr char32_t PCH(std::size_t col) const noexcept {
        return kProcessingChar[static_cast<std::size_t>(opts_.method)][col];
    }
    bool isKeyZ(char32_t c)  const noexcept { return PCH(10) == c; }
    bool isKeyD(char32_t c)  const noexcept { return PCH(9)  == c; }
    bool isKeyW(char32_t c)  const noexcept {
        const auto m = opts_.method;
        return (m == Method::Telex || m == Method::SimpleTelex)
                   ? PCH(8) == c
                   : (m == Method::Vni && (PCH(8) == c || PCH(7) == c));
    }
    bool isKeyDouble(char32_t c) const noexcept {
        const auto m = opts_.method;
        return (m == Method::Telex || m == Method::SimpleTelex)
                   ? (PCH(5) == c || PCH(6) == c || PCH(7) == c)
                   : (m == Method::Vni && PCH(6) == c);
    }
    bool isKeyS(char32_t c) const noexcept { return PCH(0) == c; }
    bool isKeyF(char32_t c) const noexcept { return PCH(1) == c; }
    bool isKeyR(char32_t c) const noexcept { return PCH(2) == c; }
    bool isKeyX(char32_t c) const noexcept { return PCH(3) == c; }
    bool isKeyJ(char32_t c) const noexcept { return PCH(4) == c; }
    static constexpr bool isVowelChar(char32_t c) noexcept {
        return c == U'A' || c == U'E' || c == U'U' || c == U'Y' || c == U'I' || c == U'O';
    }
    static constexpr bool isConsonantChar(char32_t c) noexcept { return !isVowelChar(c); }
    bool isMarkKey(char32_t c) const noexcept {
        const auto m = opts_.method;
        if (m == Method::Telex || m == Method::SimpleTelex) {
            return c == U'S' || c == U'F' || c == U'R' || c == U'J' || c == U'X';
        }
        return c == U'1' || c == U'2' || c == U'3' || c == U'5' || c == U'4';
    }
    static constexpr bool isBracketKey(char32_t c) noexcept {
        return c == U'[' || c == U']';
    }

    std::uint16_t chr(std::size_t i) const noexcept {
        return static_cast<std::uint16_t>(typingWord_[i] & kCharMask);
    }
    // Bounds-safe reads for the handful of places the legacy engine reads
    // near the buffer edge (e.g. checkForStandaloneChar at index_==0). The
    // legacy engine reads adjacent memory there; deterministically treating
    // out-of-range reads as 0 reproduces the observable behavior (no match).
    std::uint16_t chrSafe(std::size_t i) const noexcept {
        return i < kMaxBuff ? static_cast<std::uint16_t>(typingWord_[i] & kCharMask) : 0;
    }
    std::uint32_t wordSafe(std::size_t i) const noexcept {
        return i < kMaxBuff ? typingWord_[i] : 0;
    }
    bool isConsonantAt(std::size_t i) const noexcept {
        return isConsonantChar(static_cast<char32_t>(chr(i)));
    }
    bool isQuickTelexKey(char32_t c) const noexcept {
        return index_ > 0 && (c == U'C' || c == U'G' || c == U'K' || c == U'N' ||
                              c == U'Q' || c == U'P' || c == U'T') &&
               chr(index_ - 1) == c;
    }
    bool isWordBreakAny(const Event& in) const noexcept {
        switch (in.kind) {
            case Kind::MouseDown: return true;
            case Kind::Char:      return isWordBreakChar(toUpperAscii(in.ch));
            case Kind::WordBreak: return isWordBreakVk(in.vk);
            default:              return false;
        }
    }

    // ---- word-buffer primitives ----
    void insertKey(char32_t c, bool caps, bool check = true) {
        if (index_ >= kMaxBuff) {
            if (longWordHelper_.size() < kMaxLongWord) {
                longWordHelper_.push_back(typingWord_[0]);
            }
            for (std::uint32_t i = 0; i < kMaxBuff - 1; ++i) {
                typingWord_[i] = typingWord_[i + 1];
            }
            typingWord_[kMaxBuff - 1] = c | (caps ? kCapsMask : 0);
        } else {
            typingWord_[index_++] = c | (caps ? kCapsMask : 0);
        }
        if (opts_.checkSpelling && check) {
            checkSpelling();
        }
        if (c == U'D' && index_ >= 2 && isConsonantAt(index_ - 2)) {
            tempDisableKey_ = false;
        }
    }
    void insertState(char32_t c, bool caps) {
        if (stateIndex_ >= kMaxBuff) {
            for (std::uint32_t i = 0; i < kMaxBuff - 1; ++i) {
                keyStates_[i] = keyStates_[i + 1];
            }
            keyStates_[kMaxBuff - 1] = c | (caps ? kCapsMask : 0);
        } else {
            keyStates_[stateIndex_++] = c | (caps ? kCapsMask : 0);
        }
    }
    void pushMacroKey(std::uint32_t v) {
        if (macroKey_.size() < kMaxMacroKey) {
            macroKey_.push_back(v);
        }
    }
    void saveWord() {
        if (res_.code != Code::ReplaceMacro) {
            if (index_ > 0) {
                if (!longWordHelper_.empty()) {
                    std::size_t i = 0;
                    for (; i < longWordHelper_.size(); ++i) {
                        typingStatesData_.push_back(longWordHelper_[i]);
                        if (typingStatesData_.size() >= kMaxBuff) {
                            pushTypingState();
                        }
                    }
                    if (!typingStatesData_.empty()) { pushTypingState(); }
                    longWordHelper_.clear();
                }
                typingStatesData_.clear();
                for (std::uint32_t i = 0; i < index_; ++i) {
                    typingStatesData_.push_back(typingWord_[i]);
                }
                if (!typingStatesData_.empty()) { pushTypingState(); }
            }
        } else {
            typingStatesData_.clear();
            for (std::size_t i = 0; i < macroKey_.size(); ++i) {
                typingStatesData_.push_back(macroKey_[i]);
                if (typingStatesData_.size() >= kMaxBuff) {
                    pushTypingState();
                }
            }
            if (!typingStatesData_.empty()) { pushTypingState(); }
        }
    }
    void saveWord(char32_t keyCode, int count) {
        typingStatesData_.clear();
        for (int i = 0; i < count; ++i) {
            typingStatesData_.push_back(keyCode);
            if (typingStatesData_.size() >= kMaxBuff) {
                pushTypingState();
            }
        }
        if (!typingStatesData_.empty()) { pushTypingState(); }
    }
    void saveSpecialChar() {
        typingStatesData_.clear();
        for (std::uint32_t ch : specialChar_) {
            typingStatesData_.push_back(ch);
            if (typingStatesData_.size() >= kMaxBuff) {
                pushTypingState();
            }
        }
        if (!typingStatesData_.empty()) { pushTypingState(); }
        specialChar_.clear();
    }
    void pushTypingState() {
        // KNOWN ENGINE DEFECT (found by this benchmark): the engine's
        // typingStatesData_ can hold >kMaxBuff entries when restoreLastTypingState
        // leaves the buffer populated and a later saveWord() reuses it; its
        // fixed array then overflows (stack buffer overflow). The oracle
        // mirrors the state exactly but CLAMPS its copy and flags the
        // condition so the harness can record the defect and continue.
        if (typingStatesData_.size() > kMaxBuff) {
            overflowDetected_ = true;
        }
        std::array<std::uint32_t, kMaxBuff> entry{};
        const std::size_t n = std::min<std::size_t>(typingStatesData_.size(), kMaxBuff);
        for (std::size_t i = 0; i < n; ++i) {
            entry[i] = typingStatesData_[i];
        }
        typingStates_.push_back(entry);
        typingStatesLen_.push_back(static_cast<std::uint8_t>(n));
        typingStatesData_.clear();
        constexpr std::size_t kMaxHistory = 64;
        if (typingStatesLen_.size() > kMaxHistory) {
            const std::size_t over = typingStatesLen_.size() - kMaxHistory;
            typingStatesLen_.erase(typingStatesLen_.begin(), typingStatesLen_.begin() + over);
            typingStates_.erase(typingStates_.begin(), typingStates_.begin() + over);
        }
    }
    void restoreLastTypingState() {
        if (!typingStates_.empty()) {
            typingStatesData_.assign(typingStates_.back().begin(),
                                     typingStates_.back().begin() + typingStatesLen_.back());
            typingStates_.pop_back();
            typingStatesLen_.pop_back();
            if (!typingStatesData_.empty()) {
                const char32_t first =
                    static_cast<char32_t>(typingStatesData_[0] & kCharMask);
                if (first == U' ') {
                    spaceCount_ = static_cast<int>(typingStatesData_.size());
                    index_ = 0;
                } else if (isCharKeyCodeChar(first)) {
                    index_ = 0;
                    specialChar_ = typingStatesData_;
                    checkSpelling();
                } else {
                    for (std::size_t i = 0; i < typingStatesData_.size(); ++i) {
                        typingWord_[i] = typingStatesData_[i];
                    }
                    index_ = static_cast<std::uint32_t>(typingStatesData_.size());
                }
            }
        }
        // D1 mirror: the scratch is consumed — clear it (engine
        // restoreLastTypingState does the same; the stale data used to be
        // the suite-7 stack-overflow trigger).
        typingStatesData_.clear();
    }

    // ---- spelling / vowel machinery ----
    void checkSpelling(bool forceCheckVowel = false) {
        spellingOK_ = false;
        spellingVowelOK_ = true;
        spellingEndIndex_ = index_;

        if (index_ > 0 && chr(index_ - 1) == U']') {
            spellingEndIndex_ = index_ - 1;
        }

        if (spellingEndIndex_ > 0) {
            std::size_t j = 0;
            if (isConsonantAt(0)) {
                std::size_t i = 0;
                for (; i < kConsonantTable.size(); ++i) {
                    spellingFlag_ = false;
                    if (spellingEndIndex_ < kConsonantTable[i].size()) {
                        spellingFlag_ = true;
                    }
                    j = 0;
                    for (; j < kConsonantTable[i].size(); ++j) {
                        if (spellingEndIndex_ > j &&
                            (kConsonantTable[i][j] & ~(opts_.quickStartConsonant ? kEndConsonantMask : 0)) != chr(j) &&
                            (kConsonantTable[i][j] & ~(opts_.allowConsonantZfwj ? kConsonantAllowMask : 0)) != chr(j)) {
                            spellingFlag_ = true;
                            break;
                        }
                    }
                    if (spellingFlag_) { continue; }
                    break;
                }
            }

            if (j == spellingEndIndex_) {   // for "d" case
                spellingOK_ = true;
            }

            std::size_t k = j;
            vowelStart_ = k;
            if (chr(vowelStart_) == U'U' && k > 0 && k < spellingEndIndex_ - 1 &&
                chr(vowelStart_ - 1) == U'Q') {
                k = k + 1;
                j = k;
                vowelStart_ = k;
            } else if (index_ >= 2 && chr(0) == U'G' && chr(1) == U'I' &&
                       isConsonantAt(2)) {
                vowelStart_ = k = j = 1;   // fix "gìn"
            }
            std::size_t l = 0;
            for (; l < 3; ++l) {
                if (k < spellingEndIndex_ && !isConsonantAt(k)) {
                    ++k;
                    vowelEnd_ = k;
                }
            }
            if (k > j) {   // has vowel
                spellingVowelOK_ = false;
                if (k - j > 1 && forceCheckVowel) {
                    const auto it = kVowelCombine.find(chr(j));
                    if (it != kVowelCombine.end()) {
                        const auto& vowelSet = it->second;
                        for (std::size_t v = 0; v < vowelSet.size(); ++v) {
                            spellingFlag_ = false;
                            std::size_t ii = 1;
                            for (; ii < vowelSet[v].size(); ++ii) {
                                if (j + ii - 1 < spellingEndIndex_ &&
                                    vowelSet[v][ii] !=
                                        (chr(j + ii - 1) |
                                         (typingWord_[j + ii - 1] & kToneWMask) |
                                         (typingWord_[j + ii - 1] & kToneMask))) {
                                    spellingFlag_ = true;
                                    break;
                                }
                            }
                            if (spellingFlag_ || (k < spellingEndIndex_ && !vowelSet[v][0]) ||
                                (j + ii - 1 < spellingEndIndex_ && !isConsonantAt(j + ii - 1))) {
                                continue;
                            }
                            spellingVowelOK_ = true;
                            break;
                        }
                    }
                } else if (!isConsonantAt(j)) {
                    spellingVowelOK_ = true;
                }

                for (std::size_t ii = 0; ii < kEndConsonantTable.size(); ++ii) {
                    spellingFlag_ = false;
                    j = 0;
                    for (; j < kEndConsonantTable[ii].size(); ++j) {
                        if (spellingEndIndex_ > k + j &&
                            (kEndConsonantTable[ii][j] & ~(opts_.quickEndConsonant ? kEndConsonantMask : 0)) != chr(k + j)) {
                            spellingFlag_ = true;
                            break;
                        }
                    }
                    if (spellingFlag_) { continue; }
                    if (k + j >= spellingEndIndex_) {
                        spellingOK_ = true;
                        break;
                    }
                }

                // limit: end consonant "ch", "t" cannot combine with ~, `, ?
                if (spellingOK_) {
                    if (index_ >= 3 && chr(index_ - 1) == U'H' && chr(index_ - 2) == U'C' &&
                        !((typingWord_[index_ - 3] & kMark1Mask) ||
                          (typingWord_[index_ - 3] & kMark5Mask) ||
                          !(typingWord_[index_ - 3] & kMarkMask))) {
                        spellingOK_ = false;
                    } else if (index_ >= 2 && chr(index_ - 1) == U'T' &&
                               !((typingWord_[index_ - 2] & kMark1Mask) ||
                                 (typingWord_[index_ - 2] & kMark5Mask) ||
                                 !(typingWord_[index_ - 2] & kMarkMask))) {
                        spellingOK_ = false;
                    }
                }
            }
        } else {
            spellingOK_ = true;
        }
        tempDisableKey_ = !(spellingOK_ && spellingVowelOK_);
    }

    void checkGrammar(int deltaBackspace) {
        if (index_ <= 1 || index_ >= kMaxBuff) {
            return;
        }
        findAndCalculateVowel(true);
        if (vowelCount_ == 0) { return; }
        isCheckedGrammar_ = false;

        std::size_t l = vowelStart_;

        // "thuơn"/"ưoi"/"ưom"/"ưoc" double-ư repair
        if (index_ >= 3) {
            for (std::size_t i = index_ - 1; i != std::size_t(-1); --i) {
                if (chr(i) == U'N' || chr(i) == U'C' || chr(i) == U'I' ||
                    chr(i) == U'M' || chr(i) == U'P' || chr(i) == U'T') {
                    if (i >= 2 && chr(i - 1) == U'O' && chr(i - 2) == U'U') {
                        if ((typingWord_[i - 1] & kToneWMask) ^ (typingWord_[i - 2] & kToneWMask)) {
                            typingWord_[i - 2] |= kToneWMask;
                            typingWord_[i - 1] |= kToneWMask;
                            isCheckedGrammar_ = true;
                            break;
                        }
                    }
                }
            }
        }

        // move mark onto the correct vowel
        if (index_ >= 2) {
            for (std::size_t i = l; i <= vowelEnd_; ++i) {
                if (typingWord_[i] & kMarkMask) {
                    const std::uint32_t mark = typingWord_[i] & kMarkMask;
                    typingWord_[i] &= ~kMarkMask;
                    insertMark(mark, false);
                    if (i != vowelWillSetMark_) {
                        isCheckedGrammar_ = true;
                    }
                    break;
                }
            }
        }

        // re-arrange data to send back
        if (isCheckedGrammar_) {
            if (res_.code == Code::DoNothing) {
                res_.code = Code::WillProcess;
            }
            res_.backspaceCount = 0;
            std::uint32_t idx = 0;
            std::array<std::uint32_t, kMaxBuff> tail{};
            for (std::uint32_t i = index_ - 1; i != std::uint32_t(-1); --i) {
                if (i < l) { break; }
                ++res_.backspaceCount;
                tail[idx++] = static_cast<char32_t>(getCharacterCode(typingWord_[i]));
            }
            res_.newCharCount = res_.backspaceCount;
            res_.backspaceCount += static_cast<std::uint32_t>(deltaBackspace);
            buildReplacementFromTail(tail, res_.newCharCount);
        }
    }

    void buildReplacementFromTail(const std::array<std::uint32_t, kMaxBuff>& tail,
                                  std::uint32_t n) {
        // tail is most-recent-first; replacement is oldest-first.
        res_.replacement.clear();
        for (std::uint32_t i = n; i-- > 0;) {
            appendFinalChar(res_.replacement, tail[i]);
        }
    }

    void findAndCalculateVowel(bool forGrammar = false) {
        vowelCount_ = 0;
        vowelStart_ = vowelEnd_ = 0;
        for (std::uint32_t iii = index_ - 1; iii != std::uint32_t(-1); --iii) {
            if (isConsonantAt(iii)) {
                if (vowelCount_ > 0) { break; }
            } else {
                if (vowelCount_ == 0) { vowelEnd_ = iii; }
                if (!forGrammar) {
                    if ((iii >= 1 && (chr(iii) == U'I' && chr(iii - 1) == U'G')) ||
                        (iii >= 1 && (chr(iii) == U'U' && chr(iii - 1) == U'Q'))) {
                        break;
                    }
                }
                vowelStart_ = iii;
                ++vowelCount_;
            }
        }
        if (vowelStart_ >= 1 && chr(vowelStart_) == U'U' && chr(vowelStart_ - 1) == U'Q') {
            ++vowelStart_;
            --vowelCount_;
        }
    }

    bool canHasEndConsonant() {
        const auto it = kVowelCombine.find(chr(vowelStart_));
        if (it == kVowelCombine.end()) { return false; }
        const auto& vo = it->second;
        for (std::size_t ii = 0; ii < vo.size(); ++ii) {
            std::size_t kk = vowelStart_;
            std::size_t iii = 1;
            for (; iii < vo[ii].size(); ++iii) {
                if (kk > vowelEnd_ ||
                    (chr(kk) | (typingWord_[kk] & kToneMask) | (typingWord_[kk] & kToneWMask)) != vo[ii][iii]) {
                    break;
                }
                ++kk;
            }
            if (iii >= vo[ii].size()) {
                return vo[ii][0] == 1;
            }
        }
        return false;
    }

    // ---- mark removal / insertion ----
    void removeMark() {
        findAndCalculateVowel(true);
        isChanged_ = false;
        if (index_ > 0) {
            for (std::uint32_t i = vowelStart_; i <= vowelEnd_; ++i) {
                if (typingWord_[i] & kMarkMask) {
                    typingWord_[i] &= ~kMarkMask;
                    isChanged_ = true;
                }
            }
        }
        if (isChanged_) {
            res_.code = Code::WillProcess;
            res_.backspaceCount = 0;
            std::uint32_t idx = 0;
            std::array<std::uint32_t, kMaxBuff> tail{};
            for (std::uint32_t i = index_ - 1; i != std::uint32_t(-1); --i) {
                if (i < vowelStart_) { break; }
                ++res_.backspaceCount;
                tail[idx++] = static_cast<char32_t>(getCharacterCode(typingWord_[i]));
            }
            res_.newCharCount = res_.backspaceCount;
            buildReplacementFromTail(tail, res_.newCharCount);
        } else {
            res_.code = Code::DoNothing;
        }
    }

    void handleOldMark() {
        if (vowelCount_ == 0 && chr(vowelEnd_) == U'I') {
            vowelWillSetMark_ = vowelEnd_;
        } else {
            vowelWillSetMark_ = vowelStart_;
        }
        res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelWillSetMark_);

        if (vowelCount_ == 3 ||
            (vowelEnd_ + 1 < index_ && isConsonantAt(vowelEnd_ + 1) && canHasEndConsonant())) {
            vowelWillSetMark_ = vowelStart_ + 1;
            res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelWillSetMark_);
        }

        for (std::uint32_t ii = vowelStart_; ii <= vowelEnd_; ++ii) {
            if ((chr(ii) == U'E' && typingWord_[ii] & kToneMask) ||
                (chr(ii) == U'O' && typingWord_[ii] & kToneWMask)) {
                vowelWillSetMark_ = ii;
                res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelWillSetMark_);
                break;
            }
        }
        res_.newCharCount = res_.backspaceCount;
    }

    void handleModernMark() {
        vowelWillSetMark_ = vowelEnd_;
        res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelEnd_);

        // rule 2
        if (vowelCount_ == 3 &&
            ((chr(vowelStart_) == U'O' && chr(vowelStart_ + 1) == U'A' && chr(vowelStart_ + 2) == U'I') ||
             (chr(vowelStart_) == U'U' && chr(vowelStart_ + 1) == U'Y' && chr(vowelStart_ + 2) == U'U') ||
             (chr(vowelStart_) == U'O' && chr(vowelStart_ + 1) == U'E' && chr(vowelStart_ + 2) == U'O') ||
             (chr(vowelStart_) == U'U' && chr(vowelStart_ + 1) == U'Y' && chr(vowelStart_ + 2) == U'A'))) {
            vowelWillSetMark_ = vowelStart_ + 1;
            res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelWillSetMark_);
        } else if ((chr(vowelStart_) == U'O' && chr(vowelStart_ + 1) == U'I') ||
                   (chr(vowelStart_) == U'A' && chr(vowelStart_ + 1) == U'I') ||
                   (chr(vowelStart_) == U'U' && chr(vowelStart_ + 1) == U'I')) {
            vowelWillSetMark_ = vowelStart_;
            res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelWillSetMark_);
        } else if (vowelEnd_ >= 1 && chr(vowelEnd_ - 1) == U'A' && chr(vowelEnd_) == U'Y') {
            vowelWillSetMark_ = vowelEnd_ - 1;
            res_.backspaceCount = static_cast<std::uint32_t>((index_ - vowelEnd_) + 1);
        } else if (chr(vowelStart_) == U'U' && chr(vowelStart_ + 1) == U'O') {
            vowelWillSetMark_ = vowelStart_ + 1;
            res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelWillSetMark_);
        } else if (chr(vowelStart_ + 1) == U'O' || chr(vowelStart_ + 1) == U'U') {
            vowelWillSetMark_ = vowelEnd_ - 1;
            res_.backspaceCount = static_cast<std::uint32_t>((index_ - vowelEnd_) + 1);
        } else if (chr(vowelStart_) == U'O' || chr(vowelStart_) == U'U') {
            vowelWillSetMark_ = vowelEnd_;
            res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelEnd_);
        }

        // rule 3.1
        if ((chr(vowelStart_) == U'I' && (typingWord_[vowelStart_ + 1] & (U'E' | kToneMask))) ||
            (chr(vowelStart_) == U'Y' && (typingWord_[vowelStart_ + 1] & (U'E' | kToneMask))) ||
            (chr(vowelStart_) == U'U' && typingWord_[vowelStart_ + 1] == (U'O' | kToneMask)) ||
            (typingWord_[vowelStart_] == (U'U' | kToneWMask) &&
             typingWord_[vowelStart_ + 1] == (U'O' | kToneWMask))) {
            if (vowelStart_ + 2 < index_) {
                const char32_t c2 = chr(vowelStart_ + 2);
                if (c2 == U'P' || c2 == U'T' || c2 == U'M' || c2 == U'N' ||
                    c2 == U'O' || c2 == U'U' || c2 == U'I' || c2 == U'C' ||
                    (vowelStart_ + 3 < index_ && chr(vowelStart_ + 2) == U'C' && chr(vowelStart_ + 2) == U'H') ||
                    (vowelStart_ + 3 < index_ && chr(vowelStart_ + 2) == U'N' && chr(vowelStart_ + 2) == U'H') ||
                    (vowelStart_ + 3 < index_ && chr(vowelStart_ + 2) == U'N' && chr(vowelStart_ + 2) == U'G')) {
                    vowelWillSetMark_ = vowelStart_ + 1;
                    res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelWillSetMark_);
                } else {
                    vowelWillSetMark_ = vowelStart_;
                    res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelWillSetMark_);
                }
            } else {
                vowelWillSetMark_ = vowelStart_;
                res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelWillSetMark_);
            }
        }
        // rule 3.2
        else if ((chr(vowelStart_) == U'I' && chr(vowelStart_) == U'A') ||
                 (chr(vowelStart_) == U'Y' && chr(vowelStart_) == U'A') ||
                 (chr(vowelStart_) == U'U' && chr(vowelStart_) == U'A') ||
                 (chr(vowelStart_) == U'U' && typingWord_[vowelStart_ + 1] == (U'U' | kToneWMask))) {
            vowelWillSetMark_ = vowelStart_;
            res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelWillSetMark_);
        }

        // rule 4
        if (vowelCount_ == 2) {
            if (((chr(vowelStart_) == U'I') && (chr(vowelStart_ + 1) == U'A')) ||
                ((chr(vowelStart_) == U'I') && (chr(vowelStart_ + 1) == U'U')) ||
                ((chr(vowelStart_) == U'I') && (chr(vowelStart_ + 1) == U'O'))) {
                if (vowelStart_ == 0 || chr(vowelStart_ - 1) != U'G') {
                    vowelWillSetMark_ = vowelStart_;
                    res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelWillSetMark_);
                } else {
                    vowelWillSetMark_ = vowelStart_ + 1;
                    res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelWillSetMark_);
                }
            } else if ((chr(vowelStart_) == U'U') && (chr(vowelStart_ + 1) == U'A')) {
                if (vowelStart_ == 0 || chr(vowelStart_ - 1) != U'Q') {
                    if (vowelEnd_ + 1 >= index_ || !canHasEndConsonant()) {
                        vowelWillSetMark_ = vowelStart_;
                        res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelWillSetMark_);
                    }
                } else {
                    vowelWillSetMark_ = vowelStart_ + 1;
                    res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelWillSetMark_);
                }
            } else if ((chr(vowelStart_) == U'O') && (chr(vowelStart_ + 1) == U'O')) {   // "thoong"
                vowelWillSetMark_ = vowelEnd_;
                res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelWillSetMark_);
            }
        }
    }

    void insertMark(std::uint32_t markMask, bool canModify = true) {
        vowelCount_ = 0;

        if (canModify) { res_.code = Code::WillProcess; }
        res_.backspaceCount = res_.newCharCount = 0;

        findAndCalculateVowel();
        vowelWillSetMark_ = 0;

        if (vowelCount_ == 1) {
            vowelWillSetMark_ = vowelEnd_;
            res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelEnd_);
        } else {
            if (!opts_.modernOrthography) {
                handleOldMark();
            } else {
                handleModernMark();
            }
            if (typingWord_[vowelEnd_] & kToneMask || typingWord_[vowelEnd_] & kToneWMask) {
                vowelWillSetMark_ = vowelEnd_;
            }
        }

        std::uint32_t kk = index_ - 1 - vowelStart_;
        std::array<std::uint32_t, kMaxBuff> tail{};
        if (typingWord_[vowelWillSetMark_] & markMask) {
            // duplicate same mark -> restore
            typingWord_[vowelWillSetMark_] &= ~kMarkMask;
            if (canModify) { res_.code = Code::Restore; }
            for (std::uint32_t ii = vowelStart_; ii < index_; ++ii) {
                typingWord_[ii] &= ~kMarkMask;
                tail[kk--] = static_cast<char32_t>(getCharacterCode(typingWord_[ii]));
            }
            tempDisableKey_ = true;
        } else {
            typingWord_[vowelWillSetMark_] &= ~kMarkMask;
            typingWord_[vowelWillSetMark_] |= markMask;
            for (std::uint32_t ii = vowelStart_; ii < index_; ++ii) {
                if (ii != vowelWillSetMark_) {
                    typingWord_[ii] &= ~kMarkMask;
                }
                tail[kk--] = static_cast<char32_t>(getCharacterCode(typingWord_[ii]));
            }
            res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelStart_);
        }
        res_.newCharCount = res_.backspaceCount;
        buildReplacementFromTail(tail, res_.newCharCount);
    }

    // ---- D / ^(AOE) / W handlers ----
    void insertD(char32_t /*c*/, bool /*caps*/) {
        res_.code = Code::WillProcess;
        res_.backspaceCount = 0;
        std::array<std::uint32_t, kMaxBuff> tail{};
        std::uint32_t n = 0;
        for (std::uint32_t ii = index_ - 1; ii != std::uint32_t(-1); --ii) {
            ++res_.backspaceCount;
            if (chr(ii) == U'D') {
                if (typingWord_[ii] & kToneMask) {
                    res_.code = Code::Restore;
                    typingWord_[ii] &= ~kToneMask;
                    tail[n++] = static_cast<char32_t>(typingWord_[ii]);
                    tempDisableKey_ = true;
                    break;
                }
                typingWord_[ii] |= kToneMask;
                tail[n++] = static_cast<char32_t>(getCharacterCode(typingWord_[ii]));
                break;
            }
            tail[n++] = static_cast<char32_t>(getCharacterCode(typingWord_[ii]));
        }
        res_.newCharCount = res_.backspaceCount;
        buildReplacementFromTail(tail, n);
    }

    void insertAOE(char32_t data, bool /*caps*/) {
        findAndCalculateVowel();

        for (std::uint32_t ii = vowelStart_; ii <= vowelEnd_; ++ii) {
            typingWord_[ii] &= ~kToneWMask;
        }

        res_.code = Code::WillProcess;
        res_.backspaceCount = 0;
        std::array<std::uint32_t, kMaxBuff> tail{};
        std::uint32_t n = 0;

        for (std::uint32_t ii = index_ - 1; ii != std::uint32_t(-1); --ii) {
            ++res_.backspaceCount;
            if (chr(ii) == data) {   // reverse unicode char
                // v3.3.1 mirror: non-adjacent toggle crossed a final
                // consonant ("mono"→"môn" family) — lexicon arbitration flag.
                if (ii != index_ - 1) { midWordToggle_ = true; }
                if (typingWord_[ii] & kToneMask) {
                    res_.code = Code::Restore;
                    typingWord_[ii] &= ~kToneMask;
                    // D2 mirror: emit the still-decoded character (marks may
                    // remain) — raw entries resolve to 0 and vanish.
                    tail[n++] = static_cast<char32_t>(getCharacterCode(typingWord_[ii]));
                    if (data != U'O') {   // case "thoòng" stays enabled
                        tempDisableKey_ = true;
                    }
                    break;
                }
                typingWord_[ii] |= kToneMask;
                if (!isKeyD(data)) {
                    typingWord_[ii] &= ~kToneWMask;
                }
                tail[n++] = static_cast<char32_t>(getCharacterCode(typingWord_[ii]));
                break;
            }
            tail[n++] = static_cast<char32_t>(getCharacterCode(typingWord_[ii]));
        }
        res_.newCharCount = res_.backspaceCount;
        buildReplacementFromTail(tail, n);
    }

    void insertW(char32_t /*data*/, bool /*caps*/) {
        isRestoredW_ = false;
        findAndCalculateVowel();

        for (std::uint32_t ii = vowelStart_; ii <= vowelEnd_; ++ii) {
            typingWord_[ii] &= ~kToneMask;
        }

        if (vowelCount_ > 1) {
            res_.backspaceCount = static_cast<std::uint32_t>(index_ - vowelStart_);
            res_.newCharCount = res_.backspaceCount;

            if (((typingWord_[vowelStart_] & kToneWMask) && (typingWord_[vowelStart_ + 1] & kToneWMask)) ||
                ((typingWord_[vowelStart_] & kToneWMask) && chr(vowelStart_ + 1) == U'I') ||
                ((typingWord_[vowelStart_] & kToneWMask) && chr(vowelStart_ + 1) == U'A')) {
                res_.code = Code::Restore;
                std::array<std::uint32_t, kMaxBuff> tail{};
                std::uint32_t n = 0;
                for (std::uint32_t ii = index_ - 1; ii != std::uint32_t(-1); --ii) {
                    if (ii < vowelStart_) { break; }
                    typingWord_[ii] &= ~kToneWMask;
                    tail[n++] = static_cast<char32_t>(getCharacterCode(typingWord_[ii]) & ~kStandaloneMask);
                }
                isRestoredW_ = true;
                tempDisableKey_ = true;
                buildReplacementFromTail(tail, n);
            } else {
                res_.code = Code::WillProcess;

                if ((chr(vowelStart_) == U'U' && chr(vowelStart_ + 1) == U'O')) {
                    if (vowelStart_ >= 2 && typingWord_[vowelStart_ - 2] == U'T' &&
                        typingWord_[vowelStart_ - 1] == U'H') {
                        typingWord_[vowelStart_ + 1] |= kToneWMask;
                        if (vowelStart_ + 2 < index_ && chr(vowelStart_ + 2) == U'N') {
                            typingWord_[vowelStart_] |= kToneWMask;
                        }
                    } else if (vowelStart_ >= 1 && typingWord_[vowelStart_ - 1] == U'Q') {
                        typingWord_[vowelStart_ + 1] |= kToneWMask;
                    } else {
                        typingWord_[vowelStart_] |= kToneWMask;
                        typingWord_[vowelStart_ + 1] |= kToneWMask;
                    }
                } else if ((chr(vowelStart_) == U'U' && chr(vowelStart_ + 1) == U'A') ||
                           (chr(vowelStart_) == U'U' && chr(vowelStart_ + 1) == U'I') ||
                           (chr(vowelStart_) == U'U' && chr(vowelStart_ + 1) == U'U') ||
                           (chr(vowelStart_) == U'O' && chr(vowelStart_ + 1) == U'I')) {
                    typingWord_[vowelStart_] |= kToneWMask;
                } else if ((chr(vowelStart_) == U'I' && chr(vowelStart_ + 1) == U'O') ||
                           (chr(vowelStart_) == U'O' && chr(vowelStart_ + 1) == U'A')) {
                    typingWord_[vowelStart_ + 1] |= kToneWMask;
                } else {
                    tempDisableKey_ = true;
                    isChanged_ = false;
                    res_.code = Code::DoNothing;
                }

                std::array<std::uint32_t, kMaxBuff> tail{};
                std::uint32_t n = 0;
                for (std::uint32_t ii = index_ - 1; ii != std::uint32_t(-1); --ii) {
                    if (ii < vowelStart_) { break; }
                    tail[n++] = static_cast<char32_t>(getCharacterCode(typingWord_[ii]));
                }
                buildReplacementFromTail(tail, n);
            }
            return;
        }

        res_.code = Code::WillProcess;
        res_.backspaceCount = 0;
        std::array<std::uint32_t, kMaxBuff> tail{};
        std::uint32_t n = 0;

        for (std::uint32_t ii = index_ - 1; ii != std::uint32_t(-1); --ii) {
            if (ii < vowelStart_) { break; }
            ++res_.backspaceCount;
            switch (chr(ii)) {
                case U'A':
                case U'U':
                case U'O':
                    if (typingWord_[ii] & kToneWMask) {
                        if (typingWord_[ii] & kStandaloneMask) {
                            res_.code = Code::WillProcess;
                            if (chr(ii) == U'U') {
                                typingWord_[ii] = U'W' | ((typingWord_[ii] & kCapsMask) ? kCapsMask : 0);
                            } else if (chr(ii) == U'O') {
                                res_.code = Code::Restore;
                                typingWord_[ii] = U'O' | ((typingWord_[ii] & kCapsMask) ? kCapsMask : 0);
                                isRestoredW_ = true;
                            }
                            tail[n++] = static_cast<char32_t>(typingWord_[ii]);
                        } else {
                            res_.code = Code::Restore;
                            typingWord_[ii] &= ~kToneWMask;
                            // D2 mirror: emit the still-decoded character.
                            tail[n++] = static_cast<char32_t>(getCharacterCode(typingWord_[ii]));
                            isRestoredW_ = true;
                        }
                        tempDisableKey_ = true;
                    } else {
                        typingWord_[ii] |= kToneWMask;
                        typingWord_[ii] &= ~kToneMask;
                        tail[n++] = static_cast<char32_t>(getCharacterCode(typingWord_[ii]));
                    }
                    break;
                default:
                    tail[n++] = static_cast<char32_t>(getCharacterCode(typingWord_[ii]));
                    break;
            }
        }
        res_.newCharCount = res_.backspaceCount;
        buildReplacementFromTail(tail, n);
    }

    // ---- standalone [ ] and w ----
    void reverseLastStandaloneChar(char32_t keyCode, bool caps) {
        res_.code = Code::WillProcess;
        res_.backspaceCount = 0;
        res_.newCharCount = 1;
        typingWord_[index_ - 1] =
            (keyCode | kToneWMask | kStandaloneMask | (caps ? kCapsMask : 0));
        res_.replacement = finalChar(static_cast<char32_t>(getCharacterCode(typingWord_[index_ - 1])));
    }

    void checkForStandaloneChar(char32_t c, bool caps, char32_t keyWillReverse) {
        if (chrSafe(index_ - 1) == keyWillReverse && wordSafe(index_ - 1) & kToneWMask) {
            res_.code = Code::WillProcess;
            res_.backspaceCount = 1;
            res_.newCharCount = 1;
            typingWord_[index_ - 1] = c | (caps ? kCapsMask : 0);
            res_.replacement = finalChar(static_cast<char32_t>(getCharacterCode(typingWord_[index_ - 1])));
            return;
        }

        // "w" after "u" when reversing to "o": e.g. "uw" -> "ưo"
        if (index_ > 0 && chr(index_ - 1) == U'U' && keyWillReverse == U'O') {
            insertKey(keyWillReverse, caps);
            reverseLastStandaloneChar(keyWillReverse, caps);
            return;
        }

        if (index_ == 0) {
            insertKey(c, caps, false);
            reverseLastStandaloneChar(keyWillReverse, caps);
            return;
        }
        if (index_ == 1) {
            for (std::uint16_t bad : kStandaloneWBad) {
                if (chr(0) == bad) {
                    insertKey(c, caps);
                    return;
                }
            }
            insertKey(c, caps, false);
            reverseLastStandaloneChar(keyWillReverse, caps);
            return;
        }
        if (index_ == 2) {
            for (const auto& pair : kDoubleWAllowed) {
                if (chr(0) == pair[0] && chr(1) == pair[1]) {
                    insertKey(c, caps, false);
                    reverseLastStandaloneChar(keyWillReverse, caps);
                    return;
                }
            }
            insertKey(c, caps);
            return;
        }
        insertKey(c, caps);
    }

    // ---- quick telex / quick consonants / upper-case-first ----
    void handleQuickTelex(char32_t c, bool caps) {
        res_.code = Code::WillProcess;
        res_.backspaceCount = 1;
        res_.newCharCount = 2;
        const auto it = kQuickTelex.find(c);
        if (it == kQuickTelex.end()) {
            res_.code = Code::DoNothing;
            res_.backspaceCount = 0;
            res_.newCharCount = 0;
            insertKey(c, caps);
            return;
        }
        insertKey(it->second[1], caps, false);
        std::array<std::uint32_t, kMaxBuff> tail{};
        tail[0] = static_cast<char32_t>(it->second[0] | (caps ? kCapsMask : 0));
        tail[1] = static_cast<char32_t>(it->second[1] | (caps ? kCapsMask : 0));
        // newChars order: [1]=first letter, [0]=second (most-recent-first)
        res_.replacement.clear();
        res_.replacement += finalChar(static_cast<char32_t>(it->second[0] | (caps ? kCapsMask : 0)));
        res_.replacement += finalChar(static_cast<char32_t>(it->second[1] | (caps ? kCapsMask : 0)));
        static_cast<void>(tail);
    }

    bool checkQuickConsonant() {
        if (index_ <= 1) { return false; }
        int l = 0;
        if (index_ > 0) {
            if (opts_.quickStartConsonant) {
                const auto it = kQuickStartConsonant.find(chr(0));
                if (it != kQuickStartConsonant.end()) {
                    res_.code = Code::Restore;
                    res_.backspaceCount = static_cast<std::uint32_t>(index_);
                    res_.newCharCount = static_cast<std::uint32_t>(index_ + 1);
                    if (index_ < kMaxBuff - 1) { ++index_; }
                    for (std::uint32_t i = index_ - 1; i >= 2; --i) {
                        typingWord_[i] = typingWord_[i - 1];
                    }
                    typingWord_[1] = it->second[1] |
                                     (((typingWord_[0] & kCapsMask) && (typingWord_[2] & kCapsMask)) ? kCapsMask : 0);
                    typingWord_[0] = it->second[0] | (typingWord_[0] & kCapsMask);
                    l = 1;
                }
            }
            if (opts_.quickEndConsonant && index_ >= 2 &&
                !isConsonantAt(index_ - 2)) {
                const auto it = kQuickEndConsonant.find(chr(index_ - 1));
                if (it != kQuickEndConsonant.end()) {
                    res_.code = Code::Restore;
                    if (l == 1) {
                        ++res_.newCharCount;
                    } else {
                        res_.backspaceCount = 1;
                        res_.newCharCount = 2;
                    }
                    if (index_ < kMaxBuff - 1) { ++index_; }
                    typingWord_[index_ - 1] =
                        it->second[1] | (typingWord_[index_ - 2] & kCapsMask);
                    typingWord_[index_ - 2] =
                        it->second[0] | (typingWord_[index_ - 2] & kCapsMask);
                    l = 1;
                }
            }
            if (l == 1) {
                hasHandleQuickConsonant_ = true;
                std::array<std::uint32_t, kMaxBuff> tail{};
                std::uint32_t n = 0;
                for (std::uint32_t i = index_ - 1; i != std::uint32_t(-1); --i) {
                    tail[n++] = static_cast<char32_t>(getCharacterCode(typingWord_[i]));
                }
                buildReplacementFromTail(tail, n);
                return true;
            }
        }
        return false;
    }

    void upperCaseFirstCharacter() {
        if (!(typingWord_[0] & kCapsMask)) {
            res_.code = Code::WillProcess;
            res_.backspaceCount = 0;
            res_.newCharCount = 1;
            typingWord_[0] |= kCapsMask;
            res_.replacement = finalChar(static_cast<char32_t>(getCharacterCode(typingWord_[0])));
            if (opts_.useMacro && !macroKey_.empty()) {
                macroKey_[0] |= kCapsMask;
            }
        }
    }

    bool checkRestoreIfWrongSpelling(Code handleCode) {
        for (std::uint32_t ii = 0; ii < index_; ++ii) {
            if (!isConsonantAt(ii) &&
                (typingWord_[ii] & kMarkMask || typingWord_[ii] & kToneMask ||
                 typingWord_[ii] & kToneWMask)) {
                res_.code = handleCode;
                res_.backspaceCount = static_cast<std::uint32_t>(index_);
                res_.newCharCount = static_cast<std::uint32_t>(stateIndex_);
                std::array<std::uint32_t, kMaxBuff> tail{};
                std::uint32_t n = 0;
                for (std::uint32_t i = stateIndex_; i-- > 0;) {
                    typingWord_[i] = keyStates_[i];
                    tail[n++] = static_cast<char32_t>(typingWord_[i]);
                }
                index_ = stateIndex_;
                buildReplacementFromTail(tail, n);
                return true;
            }
        }
        return false;
    }

    // P1 mirror of TextEngine::checkRestoreIfNotInDictionary (+ v3.3.1
    // refinements: W-hook split alternative and mid-word toggle arbitration).
    bool checkRestoreIfNotInDictionary(Code handleCode) {
        if (!dictResolver_ || index_ == 0) { return false; }
        bool sameAsRaw = (index_ == stateIndex_);
        for (std::uint32_t i = 0; sameAsRaw && i < index_; ++i) {
            sameAsRaw = (typingWord_[i] == keyStates_[i]);
        }
        if (sameAsRaw) { return false; }
        dictScratch_.clear();
        for (std::uint32_t i = 0; i < index_; ++i) {
            const std::uint32_t v = static_cast<std::uint32_t>(
                resolveChar(getCharacterCode(typingWord_[i])));
            if (v == 0) { return false; }
            dictScratch_.push_back(v);
        }
        if (dictResolver_(dictScratch_)) {
            // v3.3.1 mirror: mid-word toggle consumed a key and BOTH forms
            // are lexicon words → the raw keystrokes win ("mono" vs "môn").
            if (midWordToggle_ && rawKeysToCodePoints(dictScratch_) &&
                dictResolver_(dictScratch_)) {
                midWordToggle_ = false;
                res_.code = handleCode;
                res_.backspaceCount = index_;
                res_.newCharCount = stateIndex_;
                std::array<std::uint32_t, kMaxBuff> tail{};
                std::uint32_t n = 0;
                for (std::uint32_t i = stateIndex_; i-- > 0;) {
                    typingWord_[i] = keyStates_[i];
                    tail[n++] = static_cast<char32_t>(typingWord_[i]);
                }
                index_ = stateIndex_;
                buildReplacementFromTail(tail, n);
                return true;
            }
            return false;
        }
        // v3.3.1 mirror: W-hook split alternative ("hươ" → "huơ").
        {
            std::array<std::uint32_t, kMaxBuff> cand{};
            std::uint32_t candLen = 0;
            if (lexiconApprovedAlternative(cand, candLen)) {
                midWordToggle_ = false;
                res_.code = handleCode;
                res_.backspaceCount = index_;
                res_.newCharCount = candLen;
                std::array<std::uint32_t, kMaxBuff> tail{};
                std::uint32_t n = 0;
                for (std::uint32_t i = 0; i < candLen; ++i) {
                    typingWord_[i] = cand[i];
                    tail[n++] = static_cast<char32_t>(getCharacterCode(typingWord_[i]));
                }
                buildReplacementFromTail(tail, n);
                return true;
            }
        }
        midWordToggle_ = false;
        res_.code = handleCode;
        res_.backspaceCount = index_;
        res_.newCharCount = stateIndex_;
        std::array<std::uint32_t, kMaxBuff> tail{};
        std::uint32_t n = 0;
        for (std::uint32_t i = stateIndex_; i-- > 0;) {
            typingWord_[i] = keyStates_[i];
            tail[n++] = static_cast<char32_t>(typingWord_[i]);
        }
        index_ = stateIndex_;
        buildReplacementFromTail(tail, n);
        return true;
    }

    // v3.3.1 mirror of TextEngine::rawKeysToCodePoints.
    bool rawKeysToCodePoints(std::vector<std::uint32_t>& out) const {
        out.clear();
        for (std::uint32_t i = 0; i < stateIndex_; ++i) {
            const std::uint32_t v = static_cast<std::uint32_t>(
                resolveChar(getCharacterCode(keyStates_[i])));
            if (v == 0) { return false; }
            out.push_back(v);
        }
        return true;
    }

    // v3.3.1 mirror of TextEngine::lexiconApprovedAlternative (uo→ươ split).
    bool lexiconApprovedAlternative(std::array<std::uint32_t, kMaxBuff>& out,
                                    std::uint32_t& outLen) {
        outLen = 0;
        if (!dictResolver_ || index_ < 2) { return false; }
        for (std::uint32_t i = 0; i + 1 < index_; ++i) {
            const std::uint32_t u = typingWord_[i];
            const std::uint32_t o = typingWord_[i + 1];
            constexpr std::uint32_t kOnlyW = kCharMask | kToneWMask;
            if ((u & ~kOnlyW) != 0 || (o & ~kOnlyW) != 0) { continue; }
            if ((u & kCharMask) != U'U' || !(u & kToneWMask)) { continue; }
            if ((o & kCharMask) != U'O' || !(o & kToneWMask)) { continue; }
            bool allPlain = true;
            for (std::uint32_t j = 0; j < index_; ++j) {
                if (j == i || j == i + 1) { continue; }
                if (typingWord_[j] & ~(kCharMask | kCapsMask)) { allPlain = false; break; }
            }
            if (!allPlain) { continue; }
            std::array<std::uint32_t, kMaxBuff> cand = typingWord_;
            cand[i] = u & ~kToneWMask;
            dictScratch_.clear();
            bool resolvable = true;
            for (std::uint32_t j = 0; j < index_; ++j) {
                const std::uint32_t v = static_cast<std::uint32_t>(
                    resolveChar(getCharacterCode(cand[j])));
                if (v == 0) { resolvable = false; break; }
                dictScratch_.push_back(v);
            }
            if (!resolvable) { continue; }
            if (dictResolver_(dictScratch_)) {
                out = cand;
                outLen = index_;
                return true;
            }
        }
        return false;
    }

    // ---- checkCorrectVowel + main dispatcher ----
    void checkCorrectVowel(
        const FlatVec<FlatVec<std::uint16_t>>& charset, int& i, int& k, char32_t markKey) {
        // ignore "qu" case
        if (index_ >= 2 && chr(index_ - 1) == U'U' && chr(index_ - 2) == U'Q') {
            isCorect_ = false;
            return;
        }
        k = static_cast<int>(index_ - 1);
        int j = static_cast<int>(charset[static_cast<std::size_t>(i)].size()) - 1;
        for (; j >= 0; --j) {
            if ((charset[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] &
                 ~(opts_.quickEndConsonant ? kEndConsonantMask : 0)) != chr(static_cast<std::size_t>(k))) {
                isCorect_ = false;
                return;
            }
            --k;
            if (k < 0) { break; }
        }

        // limit mark for end consonant "C"/"T"
        if (isCorect_ && charset[static_cast<std::size_t>(i)].size() > 1 &&
            (isKeyF(markKey) || isKeyX(markKey) || isKeyR(markKey))) {
            if (charset[static_cast<std::size_t>(i)][1] == U'C' ||
                charset[static_cast<std::size_t>(i)][1] == U'T') {
                isCorect_ = false;
            } else if (charset[static_cast<std::size_t>(i)].size() > 2 &&
                       charset[static_cast<std::size_t>(i)][2] == U'T') {
                isCorect_ = false;
            }
        }

        if (isCorect_ && k >= 0 && chr(static_cast<std::size_t>(k)) == chr(static_cast<std::size_t>(k + 1))) {
            isCorect_ = false;
        }
    }

    void handleMainKey(char32_t c, bool caps) {
        // ---- Z key removes mark ----
        if (isKeyZ(c)) {
            removeMark();
            if (!isChanged_) {
                insertKey(c, caps);
            }
            return;
        }

        if (c == U'[') {
            checkForStandaloneChar(c, caps, U'O');
            return;
        }
        if (c == U']') {
            checkForStandaloneChar(c, caps, U'U');
            return;
        }

        // ---- D key (đ) ----
        if (isKeyD(c)) {
            isCorect_ = false;
            isChanged_ = false;
            int k = static_cast<int>(index_);
            int i = 0;
            for (; i < static_cast<int>(kConsonantD.size()); ++i) {
                if (index_ < kConsonantD[static_cast<std::size_t>(i)].size()) { continue; }
                isCorect_ = true;
                checkCorrectVowel(kConsonantD, i, k, c);
                if (!isCorect_ && index_ >= 2 && chr(index_ - 1) == U'D' &&
                    isConsonantAt(index_ - 2)) {
                    isCorect_ = true;
                }
                if (isCorect_) {
                    isChanged_ = true;
                    insertD(c, caps);
                    break;
                }
            }
            if (!isChanged_) {
                insertKey(c, caps);
            }
            return;
        }

        // ---- mark keys (S F R X J / 1-5) ----
        if (isMarkKey(c)) {
            for (const auto& vowelEntry : kVowelForMark) {
                const auto& charset = vowelEntry.second;
                isCorect_ = false;
                isChanged_ = false;
                int k = static_cast<int>(index_);
                int l = 0;
                for (; l < static_cast<int>(charset.size()); ++l) {
                    if (index_ < charset[static_cast<std::size_t>(l)].size()) { continue; }
                    isCorect_ = true;
                    checkCorrectVowel(charset, l, k, c);
                    if (isCorect_) {
                        isChanged_ = true;
                        if (isKeyS(c))       { insertMark(kMark1Mask); }
                        else if (isKeyF(c))  { insertMark(kMark2Mask); }
                        else if (isKeyR(c))  { insertMark(kMark3Mask); }
                        else if (isKeyX(c))  { insertMark(kMark4Mask); }
                        else if (isKeyJ(c))  { insertMark(kMark5Mask); }
                        break;
                    }
                }
                if (isCorect_) { break; }
            }
            if (!isChanged_) {
                insertKey(c, caps);
            }
            return;
        }

        // ---- vowel keys ----
        if (opts_.method == Method::Vni) {
            for (std::uint32_t i = index_ - 1; i != std::uint32_t(-1); --i) {
                if (chr(i) == U'O' || chr(i) == U'A' || chr(i) == U'E') {
                    vowelEnd_ = i;
                    break;
                }
            }
        }

        const char32_t keyForAEO =
            (opts_.method != Method::Vni)
                ? c
                : ((c == U'7' || c == U'8') ? U'W'
                                            : (c == U'6' ? chr(vowelEnd_) : c));

        const auto vowelIt = kVowel.find(keyForAEO);
        static const FlatVec<FlatVec<std::uint16_t>> kEmpty{};
        const auto& charset = (vowelIt != kVowel.end()) ? vowelIt->second : kEmpty;

        isCorect_ = false;
        isChanged_ = false;
        int k = static_cast<int>(index_);
        int i = 0;
        for (; i < static_cast<int>(charset.size()); ++i) {
            if (index_ < charset[static_cast<std::size_t>(i)].size()) { continue; }
            isCorect_ = true;
            checkCorrectVowel(charset, i, k, c);
            if (isCorect_) {
                isChanged_ = true;
                if (isKeyDouble(c)) {
                    insertAOE(keyForAEO, caps);
                } else if (isKeyW(c)) {
                    if (opts_.method == Method::Vni) {
                        for (std::uint32_t j = index_ - 1; j != std::uint32_t(-1); --j) {
                            if (chr(j) == U'O' || chr(j) == U'U' || chr(j) == U'A' ||
                                chr(j) == U'E') {
                                vowelEnd_ = j;
                                break;
                            }
                        }
                        if ((c == U'7' && chr(vowelEnd_) == U'A' &&
                             (vowelEnd_ >= 1 ? chr(vowelEnd_ - 1) != U'U' : true)) ||
                            (c == U'8' && (chr(vowelEnd_) == U'O' || chr(vowelEnd_) == U'U'))) {
                            break;
                        }
                    }
                    insertW(keyForAEO, caps);
                }
                break;
            }
        }

        if (!isChanged_) {
            if (c == U'W' && opts_.method != Method::SimpleTelex) {
                checkForStandaloneChar(c, caps, U'U');
            } else {
                insertKey(c, caps);
            }
        }
    }

    // ---- mainKeyBranch ----
    void mainKeyBranch(char32_t c, bool caps) {
        if (willTempOffEngine_) {
            res_.code = Code::DoNothing;
            return;
        }
        if (spaceCount_ > 0) {
            res_.backspaceCount = 0;
            res_.newCharCount = 0;
            startNewSession();
            saveWord(U' ', spaceCount_);
            spaceCount_ = 0;
        } else if (!specialChar_.empty()) {
            saveSpecialChar();
        }

        insertState(c, caps);

        if (!isSpecialKey(c) || tempDisableKey_) {
            if (opts_.quickTelex && isQuickTelexKey(c)) {
                handleQuickTelex(c, caps);
                return;
            }
            res_.code = Code::DoNothing;
            res_.backspaceCount = 0;
            res_.newCharCount = 0;
            insertKey(c, caps);
        } else {
            res_.code = Code::DoNothing;
            handleMainKey(c, caps);
        }

        if (!opts_.freeMark && !isKeyD(c)) {
            if (res_.code == Code::DoNothing) {
                checkGrammar(-1);
            } else {
                checkGrammar(0);
            }
        }

        if (res_.code == Code::Restore) {
            insertKey(c, caps);
            if (stateIndex_ > 0) { --stateIndex_; }
        }

        // ---- macro key bookkeeping ----
        if (opts_.useMacro) {
            if (res_.code == Code::DoNothing) {
                pushMacroKey(c | (caps ? kCapsMask : 0));
            } else if (res_.code == Code::WillProcess || res_.code == Code::Restore) {
                const std::uint32_t bpc = res_.backspaceCount;
                for (std::uint32_t i = 0; i < bpc && !macroKey_.empty(); ++i) {
                    macroKey_.pop_back();
                }
                const std::uint32_t from = index_ - bpc;
                for (std::uint32_t i = from; i < res_.newCharCount + from; ++i) {
                    pushMacroKey(typingWord_[i]);
                }
            }
        }

        if (opts_.upperCaseFirstChar) {
            if (index_ == 1 && upperCaseStatus_ == 2) {
                upperCaseFirstCharacter();
            }
            upperCaseStatus_ = 0;
        }

        // ---- case [ ] : standalone-key word boundaries ----
        // The engine tests result_.newChars[0] (the MOST-RECENT char of any
        // replacement) for being a bracket; for plain pass-through keys
        // newChars[0] is 0. Reproduce from the buffer tail.
        bool newestIsBracket = false;
        if (res_.newCharCount > 0 && index_ > 0) {
            newestIsBracket = isBracketKey(
                static_cast<char32_t>(getCharacterCode(typingWord_[index_ - 1]) & kCharMask));
        }
        if (isBracketKey(c) && (newestIsBracket || opts_.method == Method::SimpleTelex)) {
            if (index_ - (res_.code == Code::WillProcess ? res_.backspaceCount : 0) > 0) {
                --index_;
                saveWord();
            }
            index_ = 0;
            tempDisableKey_ = false;
            stateIndex_ = 0;
            specialChar_.push_back(c | (caps ? kCapsMask : 0));
        }
    }

    // ---- branches ----
    void wordBreakBranch(char32_t c, bool caps, const Event& in, bool macroBreak) {
        res_.code = Code::DoNothing;
        res_.backspaceCount = 0;
        res_.newCharCount = 0;

        // ---- macro feature ----
        if (opts_.useMacro && macroBreak && !hasHandledMacro_ &&
            findMacro(macroKey_, macroData_)) {
            res_.code = Code::ReplaceMacro;
            res_.backspaceCount = static_cast<std::uint32_t>(macroKey_.size());
            setMacroExpansionFromResolver();
            hasHandledMacro_ = true;
        } else if ((opts_.quickStartConsonant || opts_.quickEndConsonant) &&
                   !tempDisableKey_ && macroBreak) {
            checkQuickConsonant();
        } else if (opts_.restoreIfWrongSpelling && isWordBreakAny(in)) {
            if (!tempDisableKey_ && opts_.checkSpelling) {
                checkSpelling(true);
            }
            if (tempDisableKey_) {
                if (opts_.useDictionaryRestore && dictResolver_) {
                    // P1: lexicon supersedes the structural restore.
                    if (!checkRestoreIfNotInDictionary(Code::RestoreAndStartNewSession)) {
                        res_.code = Code::DoNothing;
                    }
                } else if (!checkRestoreIfWrongSpelling(Code::RestoreAndStartNewSession)) {
                    res_.code = Code::DoNothing;
                }
            } else if (opts_.useDictionaryRestore && dictResolver_) {
                if (!checkRestoreIfNotInDictionary(Code::RestoreAndStartNewSession)) {
                    res_.code = Code::DoNothing;
                }
            }
        }

        const bool isCharKeyCode = (in.kind == Kind::Char) && isCharKeyCodeChar(c);
        if (!isCharKeyCode) {
            specialChar_.clear();
            typingStates_.clear();
            typingStatesLen_.clear();
        } else {
            if (spaceCount_ > 0) {
                saveWord(U' ', spaceCount_);
                spaceCount_ = 0;
            } else {
                saveWord();
            }
            specialChar_.push_back(c | (caps ? kCapsMask : 0));
        }

        if (res_.code == Code::DoNothing) {
            startNewSession();
            opts_.checkSpelling = useSpellingBefore_;
            willTempOffEngine_ = false;
        } else if (res_.code == Code::ReplaceMacro || hasHandleQuickConsonant_) {
            index_ = 0;
        }

        if (opts_.useMacro) {
            if (isCharKeyCode) {
                pushMacroKey(c | (caps ? kCapsMask : 0));
            } else {
                macroKey_.clear();
            }
        }

        if (opts_.upperCaseFirstChar) {
            if (c == U'.') {
                upperCaseStatus_ = 1;
            } else if (in.vk == 0x0D) {
                upperCaseStatus_ = 2;
            } else {
                upperCaseStatus_ = 0;
            }
        }
    }

    void spaceBranch(bool /*caps*/) {
        if (!tempDisableKey_ && opts_.checkSpelling) {
            checkSpelling(true);
        }
        if (opts_.useMacro && !hasHandledMacro_ && findMacro(macroKey_, macroData_)) {
            res_.code = Code::ReplaceMacro;
            res_.backspaceCount = static_cast<std::uint32_t>(macroKey_.size());
            setMacroExpansionFromResolver();
            ++spaceCount_;
            hasHandledMacro_ = true;
        } else if ((opts_.quickStartConsonant || opts_.quickEndConsonant) &&
                   !tempDisableKey_ && checkQuickConsonant()) {
            ++spaceCount_;
        } else if (opts_.restoreIfWrongSpelling && tempDisableKey_ && !hasHandledMacro_) {
            if (opts_.useDictionaryRestore && dictResolver_) {
                if (!checkRestoreIfNotInDictionary(Code::Restore)) {
                    res_.code = Code::DoNothing;
                }
            } else if (!checkRestoreIfWrongSpelling(Code::Restore)) {
                res_.code = Code::DoNothing;
            }
            ++spaceCount_;
        } else if (opts_.useDictionaryRestore && dictResolver_ && !hasHandledMacro_) {
            if (!checkRestoreIfNotInDictionary(Code::Restore)) {
                res_.code = Code::DoNothing;
            }
            ++spaceCount_;
        } else {
            res_.code = Code::DoNothing;
            ++spaceCount_;
        }
        if (opts_.useMacro) {
            macroKey_.clear();
        }
        if (opts_.upperCaseFirstChar && upperCaseStatus_ == 1) {
            upperCaseStatus_ = 2;
        }
        if (spaceCount_ == 1) {
            if (!specialChar_.empty()) {
                saveSpecialChar();
            } else {
                saveWord();
            }
        }
        opts_.checkSpelling = useSpellingBefore_;
        willTempOffEngine_ = false;
    }

    void backspaceBranch() {
        res_.code = Code::DoNothing;
        if (!specialChar_.empty()) {
            specialChar_.pop_back();
            if (specialChar_.empty()) {
                restoreLastTypingState();
            }
        } else if (spaceCount_ > 0) {
            --spaceCount_;
            if (spaceCount_ == 0) {
                restoreLastTypingState();
            }
        } else {
            if (stateIndex_ > 0) { --stateIndex_; }
            if (index_ > 0) {
                --index_;
                if (!longWordHelper_.empty()) {
                    for (std::uint32_t i = kMaxBuff - 1; i > 0; --i) {
                        typingWord_[i] = typingWord_[i - 1];
                    }
                    typingWord_[0] = longWordHelper_.back();
                    longWordHelper_.pop_back();
                    ++index_;
                }
                if (opts_.checkSpelling) {
                    checkSpelling();
                }
            }
            if (opts_.useMacro && !macroKey_.empty()) {
                macroKey_.pop_back();
            }
            res_.backspaceCount = 0;
            res_.newCharCount = 0;
            if (index_ == 0) {
                startNewSession();
                specialChar_.clear();
                restoreLastTypingState();
            } else {
                checkGrammar(1);
            }
        }
    }

    // ---- output resolution ----
    std::uint32_t getCharacterCode(const std::uint32_t& data) const {
        const int capsElem = (data & kCapsMask) ? 0 : 1;
        const std::uint32_t key = data & kCharMask;
        const auto& table = codeTableFor(static_cast<int>(opts_.table));

        if (data & kMarkMask) {   // has tone mark
            int markElem = -2;
            switch (data & kMarkMask) {
                case kMark1Mask: markElem = 0; break;
                case kMark2Mask: markElem = 2; break;
                case kMark3Mask: markElem = 4; break;
                case kMark4Mask: markElem = 6; break;
                case kMark5Mask: markElem = 8; break;
                default: break;
            }
            markElem += capsElem;

            if (key == U'A' || key == U'O' || key == U'U' || key == U'E') {
                if ((data & kToneMask) == 0 && (data & kToneWMask) == 0) {
                    markElem += 4;
                }
            }

            std::uint32_t rowKey = key;
            if (data & kToneMask) {
                rowKey |= kToneMask;
            } else if (data & kToneWMask) {
                rowKey |= kToneWMask;
            }
            const auto it = table.find(rowKey);
            if (it == table.end()) { return data; }
            const auto& row = it->second;
            if (markElem < 0 || static_cast<std::size_t>(markElem) >= row.size()) {
                return data;
            }
            return row[static_cast<std::size_t>(markElem)] | kCharCodeMask;
        }

        const auto it = table.find(key);
        if (it == table.end()) { return data; }
        const auto& row = it->second;
        if (data & kToneMask) {
            return row[static_cast<std::size_t>(capsElem)] | kCharCodeMask;
        }
        if (data & kToneWMask) {
            return row[static_cast<std::size_t>(capsElem + 2)] | kCharCodeMask;
        }
        return data;
    }

    char32_t resolveChar(std::uint32_t coded) const {
        if (coded & kPureCharMask) { return static_cast<char32_t>(coded & kCharMask); }
        if (coded & kCharCodeMask) { return static_cast<char32_t>(coded & kCharMask); }
        if (coded & kStandaloneMask) {
            // D2 root fix — mirrors TextEngine::resolveChar: a standalone
            // ư/ơ entry keeps resolving to its visible vowel even when a
            // later transform stripped its kToneWMask (it used to resolve
            // to 0 and silently vanish from the emitted replacement).
            const char32_t base = static_cast<char32_t>(coded & kCharMask);
            const bool caps = (coded & kCapsMask) != 0;
            if (base == U'U') { return static_cast<char32_t>(caps ? 0x1AF : 0x1B0); }
            if (base == U'O') { return static_cast<char32_t>(caps ? 0x1A0 : 0x1A1); }
            return base;
        }
        // Mirror of the engine's SendKeyCode-style fallback: raw entries
        // resolve to their key + caps (internal payload bits never blank
        // the character).
        return keyCodeToCharacter((coded & kCharMask) | (coded & kCapsMask));
    }

    char32_t keyCodeToCharacter(std::uint32_t data) const {
        constexpr std::array<std::array<char32_t, 256>, 2> kTable = [] {
            std::array<std::array<char32_t, 256>, 2> t{};
            for (std::size_t i = 0; i < 256; ++i) { t[0][i] = 0; t[1][i] = 0; }
            for (char32_t c = U'a'; c <= U'z'; ++c) {
                const std::uint32_t vk = static_cast<std::uint32_t>(c - 32);
                t[0][vk] = c;
                t[1][vk] = static_cast<char32_t>(c - 32);
            }
            constexpr char32_t digits[10]  = {U'1',U'2',U'3',U'4',U'5',U'6',U'7',U'8',U'9',U'0'};
            constexpr char32_t shiftedD[10] = {U'!',U'@',U'#',U'$',U'%',U'^',U'&',U'*',U'(',U')'};
            for (int i = 0; i < 10; ++i) {
                const std::uint32_t vk = static_cast<std::uint32_t>(U'0' + ((i + 1) % 10));
                t[0][vk] = digits[i];
                t[1][vk] = shiftedD[i];
            }
            constexpr std::array<std::tuple<char32_t, char32_t, std::uint32_t>, 11> punct = {{
                {U'`', U'~', 0xDC}, {U'-', U'_', 0xBD}, {U'=', U'+', 0xBB},
                {U'[', U'{', 0xDB}, {U']', U'}', 0xDD}, {U'\\', U'|', 0xDE},
                {U';', U':', 0xBA}, {U'\'', U'"', 0xC0}, {U',', U'<', 0xBC},
                {U'.', U'>', 0xBE}, {U'/', U'?', 0xBF},
            }};
            for (const auto& [lo, hi, vk] : punct) {
                t[0][vk] = lo;
                t[1][vk] = hi;
            }
            t[0][0x20] = U' ';
            // D2 mirror: ASCII identity for the standalone-bracket keys
            // ('[' / ']' stored raw in the buffer — see TextEngine.cpp).
            t[0][0x5B] = U'['; t[1][0x5B] = U'[';
            t[0][0x5D] = U']'; t[1][0x5D] = U']';
            return t;
        }();
        if ((data & 0xFFFF0000u) != 0 && (data & 0xFFFF0000u) != kCapsMask) {
            return 0;
        }
        return kTable[(data >> 16) & 1u][data & 0xFFu];
    }

    static void appendUtf16(std::wstring& out, char32_t cp) {
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<wchar_t>(cp));
        } else if (cp <= 0x10FFFF) {
            const char32_t t = cp - 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (t >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (t & 0x3FF)));
        }
    }

    void appendFinalChar(std::wstring& out, std::uint32_t v) {
        v = resolveChar(v);
        if (v & kCharCodeMask) { v &= kCharMask; }
        if (v == 0) { return; }
        if (opts_.table == CodeTable::UnicodeCompound && (v & 0xE000u)) {
            const std::uint32_t markIdx = (v >> 13) & 0x7;
            const char32_t base = static_cast<char32_t>(v & 0x1FFFu);
            appendUtf16(out, base);
            if (markIdx > 0 && markIdx <= kUnicodeCompoundMarks.size()) {
                appendUtf16(out, static_cast<char32_t>(kUnicodeCompoundMarks[markIdx - 1]));
            }
        } else {
            appendUtf16(out, static_cast<char32_t>(v));
        }
    }

    std::wstring finalChar(std::uint32_t v) {
        std::wstring w;
        appendFinalChar(w, v);
        return w;
    }

    void finalizeResult() {
        if (res_.newCharCount > kMaxBuff) {
            res_.newCharCount = kMaxBuff;
        }
    }

    bool findMacro(const std::vector<std::uint32_t>& key,
                   std::vector<std::uint32_t>& data) {
        if (!macroResolver_) { return false; }
        return macroResolver_(key, data);
    }

    // D3 mirror of TextEngine::setMacroExpansionFromResolver.
    void setMacroExpansionFromResolver() {
        res_.macroExpansion.clear();
        res_.macroExpansion.reserve(macroData_.size());
        for (std::uint32_t v : macroData_) {
            if (v & (kCharCodeMask | kPureCharMask)) {
                v &= kCharMask;
            } else if (v & kStandaloneMask) {
                v = static_cast<std::uint32_t>(resolveChar(v));
            } else if (v & kCapsMask) {
                const char32_t mapped = keyCodeToCharacter(v);
                if (mapped != 0) { v = static_cast<std::uint32_t>(mapped); }
            }
            res_.macroExpansion.push_back(v);
        }
    }

    // ---- state ----
    Options opts_;
    bool useSpellingBefore_ = true;
    bool willTempOffEngine_ = false;
    std::uint32_t index_ = 0;
    std::uint32_t stateIndex_ = 0;
    std::array<std::uint32_t, kMaxBuff> typingWord_{};
    std::array<std::uint32_t, kMaxBuff> keyStates_{};
    std::vector<std::uint32_t> specialChar_;
    std::vector<std::uint32_t> longWordHelper_;
    std::vector<std::array<std::uint32_t, kMaxBuff>> typingStates_;
    std::vector<std::uint8_t> typingStatesLen_;
    std::vector<std::uint32_t> typingStatesData_;
    std::vector<std::uint32_t> macroKey_;
    std::vector<std::uint32_t> macroData_;
    MacroResolver macroResolver_;
    DictionaryResolver dictResolver_;
    std::vector<std::uint32_t> dictScratch_;
    // D2 mirror of the engine's consumer-visible accounting (lower-bound
    // committed-length model used by the backspaceCount clamp).
    std::size_t visibleAccount_ = 0;
    int spaceCount_ = 0;
    bool tempDisableKey_ = false;
    bool hasHandledMacro_ = false;
    bool hasHandleQuickConsonant_ = false;
    bool isChanged_ = false;
    bool isCorect_ = false;
    bool isRestoredW_ = false;
    bool isCheckedGrammar_ = false;
    bool midWordToggle_ = false;   // v3.3.1 mirror (engine: insertAOE flag)
    bool spellingOK_ = false;
    bool spellingVowelOK_ = true;
    bool spellingFlag_ = false;
    std::uint32_t spellingEndIndex_ = 0;
    std::uint32_t vowelStart_ = 0;
    std::uint32_t vowelEnd_ = 0;
    std::uint32_t vowelCount_ = 0;
    std::uint32_t vowelWillSetMark_ = 0;
    std::uint32_t upperCaseStatus_ = 0;
    Result res_;
    // Set when the oracle's mirror of the engine state reaches the known
    // engine overflow condition (typingStatesData_ > kMaxBuff at
    // pushTypingState). The harness checks this after every event.
    bool overflowDetected_ = false;
};

} // namespace orel
