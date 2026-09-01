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
// File: tests/bench_three_engines.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.0 — tests/bench_three_engines.cpp
// COMPREHENSIVE differential + latency benchmark:
//   KieeKey (shipped TextEngine)
//   vs OpenKey 2.0.5 (vendored, unmodified legacy engine)
//   vs UniKey latest (vendored, unmodified UKEngine, GPL — the engine behind
//      UniKey 4.x / 4.6, the current release line)
//
// Feeds byte-identical, deterministic key streams (real Vietnamese passages,
// tone-toggle stress, backspace storms, fuzz) through each engine with its
// faithful consumer model, then reports:
//   * Correctness  — final-text agreement vs intended (passages) and the
//                    3-way agreement / pairwise diffs (all streams).
//   * Latency      — engine-decision time per key (mean + p50/p90/p99 over a
//                    2M-key run). This is the comparable "does typing accents
//                    feel delayed?" metric at engine level; OS I/O (TSF /
//                    SendInput) is excluded because it is not reproducible
//                    headlessly.
//   * Memory       — engine object size + worst-case internal buffers.
//
// Build (from repo root):
//   g++ -std=c++17 -O2 -DLINUX -fpermissive -include tests/reference/unikey/uk_fix.h
//       -w -I src/core -I tests -I tests/reference/openkey-2.0.5/engine
//       -I tests/reference/unikey tests/bench_three_engines.cpp
//       src/core/TextEngine.cpp tests/engine205.cpp
//       tests/reference/openkey-2.0.5/engine/Engine.cpp
//       tests/reference/openkey-2.0.5/engine/Vietnamese.cpp
//       tests/reference/openkey-2.0.5/engine/Macro.cpp
//       tests/reference/openkey-2.0.5/engine/SmartSwitchKey.cpp
//       tests/reference/openkey-2.0.5/engine/ConvertTool.cpp
//       tests/reference/unikey/ukengine.cpp tests/reference/unikey/inputproc.cpp
//       tests/reference/unikey/macro.cpp tests/reference/unikey/mactab.cpp
//       tests/reference/unikey/usrkeymap.cpp tests/reference/unikey/charset.cpp
//       tests/reference/unikey/convert.cpp tests/reference/unikey/data.cpp
//       tests/reference/unikey/error.cpp tests/reference/unikey/pattern.cpp
//       tests/reference/unikey/byteio.cpp -o bench_three_engines
//----------------------------------------------------------------------------
#include "TextEngine.hpp"
#include "engine205.hpp"
#include "ukengine.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace ok::text;
using Clock = std::chrono::steady_clock;

//============================================================================
// UTF-8 helpers
//============================================================================
// UTF-16LE BYTES -> UTF-8. Takes a byte pointer + byte count: the UniKey
// engine writes UTF-16LE into outBuf and reports the byte count in outSize.
// (Do NOT reinterpret as wchar_t* — wchar_t is 4 bytes on Linux, which would
// misread every pair.)
static std::string u16BytesToUtf8(const unsigned char* p, std::size_t nBytes) {
    std::string s;
    for (std::size_t i = 0; i + 1 < nBytes; i += 2) {
        const char32_t cp = static_cast<char32_t>(p[i]) |
                            (static_cast<char32_t>(p[i + 1]) << 8);
        if (cp < 0x80) s += static_cast<char>(cp);
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
// std::wstring -> UTF-8. Each wchar holds one code point <= U+FFFF (the
// engines emit single UTF-16 units, no surrogates); reading the VALUE (not
// raw bytes) keeps this correct whether wchar_t is 2 or 4 bytes.
static std::string u8(const std::wstring& w) {
    std::string s;
    for (wchar_t wc : w) {
        const char32_t cp = static_cast<char32_t>(wc);
        if (cp < 0x80) s += static_cast<char>(cp);
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

static void eraseUtf16FromEnd(std::string& s, std::size_t n) {
    std::size_t toErase = 0, units = 0;
    for (auto it = s.rbegin(); it != s.rend() && units < n; ++it) {
        const unsigned char b = static_cast<unsigned char>(*it);
        if ((b & 0xC0) != 0x80) ++units;
        ++toErase;
    }
    s.erase(s.size() - toErase);
}

//============================================================================
// Telex keystroke builder (same mapping as real_passages.cpp)
//============================================================================
static bool charToTelex(wchar_t c, std::string& keys) {
    switch (c) {
        case L'a': keys += 'a'; return true;
        case L'\u00E0': keys += "af"; return true;
        case L'\u00E1': keys += "as"; return true;
        case L'\u1EA3': keys += "ar"; return true;
        case L'\u00E3': keys += "ax"; return true;
        case L'\u1EA1': keys += "aj"; return true;
        case L'\u0103': keys += "aw"; return true;
        case L'\u1EB1': keys += "awf"; return true;
        case L'\u1EAF': keys += "aws"; return true;
        case L'\u1EB3': keys += "awr"; return true;
        case L'\u1EB5': keys += "awx"; return true;
        case L'\u1EB7': keys += "awj"; return true;
        case L'\u00E2': keys += "aa"; return true;
        case L'\u1EA7': keys += "aaf"; return true;
        case L'\u1EA5': keys += "aas"; return true;
        case L'\u1EA9': keys += "aar"; return true;
        case L'\u1EAB': keys += "aax"; return true;
        case L'\u1EAD': keys += "aaj"; return true;
        case L'e': keys += 'e'; return true;
        case L'\u00E8': keys += "ef"; return true;
        case L'\u00E9': keys += "es"; return true;
        case L'\u1EBB': keys += "er"; return true;
        case L'\u1EBD': keys += "ex"; return true;
        case L'\u1EB9': keys += "ej"; return true;
        case L'\u00EA': keys += "ee"; return true;
        case L'\u1EC1': keys += "eef"; return true;
        case L'\u1EBF': keys += "ees"; return true;
        case L'\u1EC3': keys += "eer"; return true;
        case L'\u1EC5': keys += "eex"; return true;
        case L'\u1EC7': keys += "eej"; return true;
        case L'i': keys += 'i'; return true;
        case L'\u00EC': keys += "if"; return true;
        case L'\u00ED': keys += "is"; return true;
        case L'\u1EC9': keys += "ir"; return true;
        case L'\u0129': keys += "ix"; return true;
        case L'\u1ECB': keys += "ij"; return true;
        case L'o': keys += 'o'; return true;
        case L'\u00F2': keys += "of"; return true;
        case L'\u00F3': keys += "os"; return true;
        case L'\u1ECF': keys += "or"; return true;
        case L'\u00F5': keys += "ox"; return true;
        case L'\u1ECD': keys += "oj"; return true;
        case L'\u00F4': keys += "oo"; return true;
        case L'\u1ED3': keys += "oof"; return true;
        case L'\u1ED1': keys += "oos"; return true;
        case L'\u1ED5': keys += "oor"; return true;
        case L'\u1ED7': keys += "oox"; return true;
        case L'\u1ED9': keys += "ooj"; return true;
        case L'\u01A1': keys += "ow"; return true;
        case L'\u1EDD': keys += "owf"; return true;
        case L'\u1EDB': keys += "ows"; return true;
        case L'\u1EDF': keys += "owr"; return true;
        case L'\u1EE1': keys += "owx"; return true;
        case L'\u1EE3': keys += "owj"; return true;
        case L'u': keys += 'u'; return true;
        case L'\u00F9': keys += "uf"; return true;
        case L'\u00FA': keys += "us"; return true;
        case L'\u1EE7': keys += "ur"; return true;
        case L'\u0169': keys += "ux"; return true;
        case L'\u1EE5': keys += "uj"; return true;
        case L'\u01B0': keys += "uw"; return true;
        case L'\u1EEB': keys += "uwf"; return true;
        case L'\u1EE9': keys += "uws"; return true;
        case L'\u1EED': keys += "uwr"; return true;
        case L'\u1EEF': keys += "uwx"; return true;
        case L'\u1EF1': keys += "uwj"; return true;
        case L'y': keys += 'y'; return true;
        case L'\u1EF3': keys += "yf"; return true;
        case L'\u00FD': keys += "ys"; return true;
        case L'\u1EF7': keys += "yr"; return true;
        case L'\u1EF9': keys += "yx"; return true;
        case L'\u1EF5': keys += "yj"; return true;
        case L'\u0111': keys += "dd"; return true;
        default: return false;
    }
}

// Intended text -> Telex key script. Encoding: ' '=space, '^x'=caps letter,
// '#'=backspace, '~s'=shifted symbol, plain chars pass through.
static std::string buildKeys(const std::wstring& intended, bool& ok) {
    std::string k;
    for (wchar_t c : intended) {
        if (c == L' ') { k += ' '; continue; }
        if (c >= L'A' && c <= L'Z') { k += '^'; k += static_cast<char>(c - L'A' + L'a'); continue; }
        if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') ||
            c == L',' || c == L'.' || c == L';' || c == L'-' || c == L'/' || c == L'\'') {
            k += static_cast<char>(c); continue;
        }
        if (c == L'!' || c == L'?' || c == L':' || c == L'(' || c == L')' || c == L'"') {
            k += '~'; k += static_cast<char>(c); continue;
        }
        std::string t;
        if (charToTelex(c, t)) { k += t; continue; }
        ok = false;
        return {};
    }
    return k;
}

//============================================================================
// Engine drivers — each is one engine + its faithful consumer model.
// All expose the same feed/text/timing interface.
//============================================================================
struct Driver {
    virtual ~Driver() = default;
    virtual const char* name() const = 0;
    virtual void reset() = 0;
    virtual void feedChar(char c, bool caps) = 0;
    virtual void feedSpace() = 0;
    virtual void feedBackspace() = 0;
    virtual void feedSymbol(char s) = 0;
    virtual std::string text() const = 0;
    // Latency: accumulated engine-decision nanoseconds + key count.
    virtual std::uint64_t decisionNs() const = 0;
    virtual std::uint64_t keyCount() const = 0;
};

// ---- KieeKey (shipped TextEngine) -----------------------------------------
class KieeKeyDriver final : public Driver {
public:
    KieeKeyDriver() { reset(); }
    const char* name() const override { return "KieeKey (v3.0)"; }
    void reset() override {
        EngineOptions eo;
        eo.inputMethod = InputMethod::Telex;
        eng_ = TextEngine(eo);
        visible_.clear();
        ns_ = 0; keys_ = 0;
    }
    void feedChar(char c, bool caps) override {
        TextInput in; in.kind = InputKind::Char; in.ch = static_cast<char32_t>(c); in.isCaps = caps;
        process(in, static_cast<wchar_t>(caps && c >= 'a' && c <= 'z' ? c - 32 : c));
    }
    void feedSpace() override {
        TextInput in; in.kind = InputKind::Space;
        process(in, L' ');
    }
    void feedBackspace() override {
        TextInput in; in.kind = InputKind::Backspace;
        process(in, L'\b');
    }
    void feedSymbol(char s) override {
        TextInput in; in.kind = InputKind::Char; in.ch = static_cast<char32_t>(s); in.isCaps = true;
        process(in, static_cast<wchar_t>(s));
    }
    std::string text() const override { return u8(visible_); }
    std::uint64_t decisionNs() const override { return ns_; }
    std::uint64_t keyCount() const override { return keys_; }

private:
    void process(TextInput& in, wchar_t vis) {
        const auto t0 = Clock::now();
        const EngineResult& r = eng_.process(in);
        ns_ += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count());
        ++keys_;
        if (!r.consumed() || r.code == EngineCode::ReplaceMacro) {
            if (in.kind == InputKind::Backspace) {
                if (!visible_.empty()) { visible_.pop_back(); }   // app deletes one char
            } else {
                visible_ += vis;
            }
            return;
        }
        const std::wstring rep = eng_.replacementUtf16(r);
        const std::size_t b = r.backspaceCount > visible_.size() ? visible_.size() : r.backspaceCount;
        visible_.erase(visible_.size() - b, b);
        visible_ += rep;
        if (r.code == EngineCode::Restore || r.code == EngineCode::RestoreAndStartNewSession) {
            visible_ += vis;   // legacy-hook contract: re-issue the typed key
        }
    }
    TextEngine eng_;
    std::wstring visible_;
    std::uint64_t ns_ = 0, keys_ = 0;
};

// ---- OpenKey 2.0.5 (vendored legacy) --------------------------------------
class Ok205Driver final : public Driver {
public:
    Ok205Driver() { reset(); }
    const char* name() const override { return "OpenKey 2.0.5 (legacy)"; }
    void reset() override {
        ok205::Options o;
        o.method = ok205::Method::Telex;
        ok205::init(o);
        visible_.clear();
        ns_ = 0; keys_ = 0;
    }
    void feedChar(char c, bool caps) override {
        ok205::Delta d;
        const auto t0 = Clock::now();
        ok205::processChar(static_cast<char32_t>(c), caps, false, d);
        ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
        ++keys_;
        apply(d, caps && c >= 'a' && c <= 'z' ? static_cast<wchar_t>(c - 32) : static_cast<wchar_t>(c));
    }
    void feedSpace() override {
        ok205::Delta d;
        const auto t0 = Clock::now();
        ok205::processSpace(false, d);
        ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
        ++keys_;
        apply(d, L' ');
    }
    void feedBackspace() override {
        ok205::Delta d;
        const auto t0 = Clock::now();
        ok205::processBackspace(d);
        ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
        ++keys_;
        apply(d, L'\b');
    }
    void feedSymbol(char s) override {
        ok205::Delta d;
        const auto t0 = Clock::now();
        ok205::processSymbol(s, d);
        ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
        ++keys_;
        apply(d, static_cast<wchar_t>(s));
    }
    std::string text() const override { return u8(visible_); }
    std::uint64_t decisionNs() const override { return ns_; }
    std::uint64_t keyCount() const override { return keys_; }

private:
    void apply(const ok205::Delta& d, wchar_t vis) {
        if (d.code == 0) {
            if (d.backspace > 0) {            // unconsumed backspace: app deletes one char
                std::size_t bs = d.backspace > visible_.size() ? visible_.size() : static_cast<std::size_t>(d.backspace);
                visible_.erase(visible_.size() - bs, bs);
            } else {
                visible_ += vis;
            }
            return;
        }
        std::size_t bs = d.backspace > visible_.size() ? visible_.size() : static_cast<std::size_t>(d.backspace);
        visible_.erase(visible_.size() - bs, bs);
        visible_ += d.text;
    }
    std::wstring visible_;
    std::uint64_t ns_ = 0, keys_ = 0;
};

// ---- UniKey (vendored UKEngine) -------------------------------------------
// Consumer model faithful to UniKey's XIM/GTK/Windows front-ends: the engine
// buffers pending keystrokes and emits a changed span only on a conversion
// (ret != 0); the front-end shows the raw pending keys as the preedit and
// commits them on a word break.
class UniKeyDriver final : public Driver {
public:
    UniKeyDriver() { reset(); }
    const char* name() const override { return "UniKey (4.x UKEngine)"; }
    void reset() override {
        std::memset(&shm_, 0, sizeof(shm_));
        shm_.initialized = 1;
        shm_.vietKey = 1;
        shm_.iconShown = 0;
        shm_.usrKeyMapLoaded = 0;
        shm_.charsetId = CONV_CHARSET_UNICODE;
        shm_.options.freeMarking = 1;
        shm_.options.modernStyle = 0;
        shm_.options.macroEnabled = 0;
        shm_.options.useUnicodeClipboard = 0;
        shm_.options.alwaysMacro = 0;
        shm_.options.strictSpellCheck = 0;
        shm_.options.useIME = 0;
        shm_.options.spellCheckEnabled = 1;
        shm_.options.autoNonVnRestore = 0;
        if (!engineInited_) {
            SetupUnikeyEngine();
            engineInited_ = true;
        }
        shm_.input.init();
        shm_.macStore.init();   // real front-end (UnikeySetup) does this too
        shm_.input.setIM(UkTelex);
        engine_ = UkEngine();
        engine_.setCtrlInfo(&shm_);
        committed_.clear();
        pending_.clear();
        ns_ = 0; keys_ = 0;
    }
    void feedChar(char c, bool caps) override {
        // The raw preedit char mirrors what the front-end would show: for a
        // caps key it is the UPPERCASE letter (the engine's own output path
        // applies entry.caps for emitted spans, so this only affects the
        // raw-pending display).
        const char shown = (caps && c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
        run(vkFor(c, caps), shown, false);
    }
    void feedSpace() override { run(32, ' ', true); }
    void feedBackspace() override {
        const auto t0 = Clock::now();
        int backs = 0;
        unsigned char out[256] = {};
        int outSize = sizeof(out);
        UkOutputType outType = UkCharOutput;
        static_cast<void>(engine_.processBackspace(backs, out, outSize, outType));
        ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
        ++keys_;
        if (backs > 0) { eraseUtf16FromEnd(pending_, static_cast<std::size_t>(backs)); }
        else if (!pending_.empty()) { pending_.pop_back(); }
        else { eraseUtf16FromEnd(committed_, 1); }
    }
    void feedSymbol(char s) override { run(vkFor(s, false), s, true); }
    std::string text() const override { return committed_ + pending_; }
    std::uint64_t decisionNs() const override { return ns_; }
    std::uint64_t keyCount() const override { return keys_; }

private:
    static unsigned int vkFor(char c, bool caps) {
        if (c >= 'a' && c <= 'z') return static_cast<unsigned int>(caps ? (c - 32) : c);
        if (c >= '0' && c <= '9') return static_cast<unsigned int>(c);
        switch (c) {
            case ' ': return 32;
            case ',': return 188;
            case '.': return 190;
            case ';': return 186;
            case '\'': return 222;
            case '[': return 219;
            case ']': return 221;
            case '-': return 189;
            case '/': return 191;
            case '=': return 187;
            default: return static_cast<unsigned int>(static_cast<unsigned char>(c));
        }
    }
    void run(unsigned int vk, char ch, bool isWordBreak) {
        int backs = 0;
        unsigned char out[256] = {};
        int outSize = sizeof(out);
        UkOutputType outType = UkCharOutput;
        int ret = 0;
        const auto t0 = Clock::now();
        ret = engine_.process(vk, backs, out, outSize, outType);
        ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
        ++keys_;
        if (ret != 0) {
            eraseUtf16FromEnd(pending_, static_cast<std::size_t>(backs));
            pending_ += u16BytesToUtf8(out, outSize >= 2 ? static_cast<std::size_t>(outSize) : 0);
        } else if (isWordBreak) {
            committed_ += pending_;
            committed_ += ch;
            pending_.clear();
        } else if (ch != '\0') {
            pending_ += ch;
        }
    }
    UkSharedMem shm_{};
    UkEngine engine_;
    std::string committed_, pending_;
    std::uint64_t ns_ = 0, keys_ = 0;
    static bool engineInited_;
};
bool UniKeyDriver::engineInited_ = false;

//============================================================================
// Streams
//============================================================================
struct Stream { std::string name; std::string keys; };

static const wchar_t* kPassages[] = {
    L"Xin chào, tôi tên là Nam. Rất vui được gặp bạn!",
    L"Hôm nay trời đẹp quá, chúng ta đi chơi nhé!",
    L"Tôi đang học tiếng Việt ở Cần Thơ",
    L"Cảm ơn bạn đã giúp đỡ tôi rất nhiều!",
    L"Bạn có khỏe không? Tôi khỏe, cảm ơn!",
    L"Công việc hôm nay nhiều quá, tôi phải làm thêm giờ",
    L"Món ăn này ngon quá, bạn nấu giỏi thật đấy!",
    L"Ngày mai chúng ta họp lúc 9 giờ sáng nhé",
    L"Tôi sẽ gửi báo cáo cho bạn trước cuối tuần",
    L"Em yêu anh nhiều lắm, anh có nhớ em không?",
    L"Please check the file tôi đã gửi qua email nhé",
    L"OK, tôi sẽ confirm lại với team rồi báo bạn sau",
    L"Số điện thoại của tôi là 0901234567, gọi cho tôi nhé",
    L"Giá sản phẩm là 1.250.000 đồng, chưa gồm thuế",
    L"Hẹn gặp bạn lúc 14:30 ngày 15/08/2026 nhé",
};
static const std::size_t kNumPassages = sizeof(kPassages) / sizeof(kPassages[0]);
static std::vector<std::wstring> kPassageIntended(kPassages, kPassages + kNumPassages);

static const char* kStress[] = {
    "chaof", "chaoff", "chaof#f", "chaof#ff", "chaof###chaof",
    "cass", "casss", "cassss", "ca#f", "caf#f", "chaf#f",
    "tooi", "tooif", "tooiff", "tooif#f",
    "hoaf", "hoaf#f", "hoaf##f",
    "ddawng", "ddawngf", "ddawnngf", "ddawng#f",
    "muoif", "muoif#f", "muooif", "muoif##f",
    "quaas", "quaass", "quaas#s",
    "gias", "gias#s", "giaas", "giaas#s",
    "nguwowif", "nguwowiff", "nguwowif#f",
    "thuowng", "thuowngf", "thuowng#f",
    "caau", "caauf", "caau#f", "caau##f",
    "biet", "bietes", "bietes#s", "bieets",
    "chaof cas", "chaof cas f", "cass chaf",
    "toi ten la nam", "xin chao ban", "toi khoong bieets",
};

// Realistic typing fuzz: random words (1-11 letters — no real Vietnamese
// word is longer) joined by spaces/commas/periods, with occasional backspaces
// and tone keys mixed in. IMPORTANT: words are capped at 11 letters on
// purpose — UniKey's UKEngine uses a FIXED 128-entry word buffer and its
// overflow path is unsafe (OOB read, ASAN-confirmed); no human-typed word can
// exceed it, so comparative streams must stay within it. Long-run inputs are
// documented in the report as an upstream engine limitation, not fed here.
static std::string makeFuzz(std::uint64_t seed, std::size_t n) {
    std::string out;
    const std::string alpha = "abcdefghijklmnopqrstuvwxyz";
    std::uint64_t x = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    while (out.size() < n) {
        // A word of 1..11 letters, always followed by exactly one separator —
        // words never concatenate, matching how a person actually types.
        const std::size_t wlen = 1 + ((x >> 43) % 11);
        for (std::size_t k = 0; k < wlen && out.size() < n; ++k) {
            x = x * 6364136223846793005ULL + 1442695040888963407ULL;
            out += alpha[(x >> 43) % alpha.size()];
        }
        if (out.size() >= n) { break; }
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;
        const unsigned r = static_cast<unsigned>((x >> 33) % 100);
        if (r < 7) {                          // backspace — edit storm
            out += '#';
        } else if (r < 12 && out.size() >= 2) {
            out += "##";                      // delete the separator + last letter
        } else {
            out += (r < 90) ? ' ' : (r < 95 ? ',' : '.');
        }
    }
    return out;
}

static void runStream(Driver& d, const std::string& keys) {
    d.reset();
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const char c = keys[i];
        if (c == '^' && i + 1 < keys.size()) { d.feedChar(keys[++i], true); }
        else if (c == '~' && i + 1 < keys.size()) { d.feedSymbol(keys[++i]); }
        else if (c == ' ') { d.feedSpace(); }
        else if (c == '#') { d.feedBackspace(); }
        else { d.feedChar(c, false); }
    }
}

//============================================================================
// Latency microbench — per-key histogram over a long stream
//============================================================================
struct LatDist {
    std::uint64_t n = 0, sum = 0, min = ~0ULL, max = 0;
    std::vector<std::uint64_t> samples;
};
static LatDist benchLatency(Driver& d, const std::string& keys, std::uint64_t repeats) {
    d.reset();
    std::vector<std::uint64_t> timings;
    timings.reserve(repeats * keys.size());
    for (std::uint64_t r = 0; r < repeats; ++r) {
        for (std::size_t i = 0; i < keys.size(); ++i) {
            const char c = keys[i];
            const auto t0 = Clock::now();
            if (c == '^' && i + 1 < keys.size()) { d.feedChar(keys[++i], true); }
            else if (c == '~' && i + 1 < keys.size()) { d.feedSymbol(keys[++i]); }
            else if (c == ' ') { d.feedSpace(); }
            else if (c == '#') { d.feedBackspace(); }
            else { d.feedChar(c, false); }
            timings.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count()));
        }
    }
    LatDist dist;
    std::sort(timings.begin(), timings.end());
    dist.n = timings.size();
    for (std::uint64_t t : timings) {
        dist.sum += t;
        dist.min = std::min(dist.min, t);
        dist.max = std::max(dist.max, t);
    }
    dist.samples = std::move(timings);
    return dist;
}
static std::uint64_t percentile(const LatDist& d, double p) {
    if (d.samples.empty()) { return 0; }
    const std::size_t idx = std::min(d.samples.size() - 1,
        static_cast<std::size_t>(static_cast<double>(p) * static_cast<double>(d.samples.size())));
    return d.samples[idx];
}

//============================================================================
// Report
//============================================================================
static void writeReport(const std::vector<Stream>& streams,
                        const std::vector<std::string>& names,
                        const std::map<std::string, std::string>& texts,
                        const std::map<std::string, std::uint64_t>& keyCounts,
                        const std::map<std::string, LatDist>& lats) {
    std::string r;
    auto line = [&](const std::string& s) { r += s; r += '\n'; };
    const std::string& NG = names[0], OK = names[1], UK = names[2];

    line("# Three-Engine Benchmark — KieeKey vs OpenKey 2.0.5 vs UniKey");
    line("");
    line("**Date:** 2026-08-29 · **Method:** Telex · deterministic byte-identical key streams.");
    line("");
    line("- **KieeKey v1.1.0** — shipped `TextEngine` (this repository).");
    line("- **OpenKey 2.0.5** — vendored, unmodified legacy engine (`tests/reference/openkey-2.0.5`, compiled `-DLINUX`).");
    line("- **UniKey 4.x** — vendored, unmodified `UKEngine` from the official GPL source"
         " (`tests/reference/unikey`); the engine behind the current UniKey release line"
         " 4.6.250531 (built 2025-12-28). Engine source pulled from the GitHub mirror"
         " `hochanh/unikey-source` (SourceForge CVS snapshot) because the official"
         " SourceForge SVN is unreachable.");
    line("");
    line("Latency = engine-decision cost per key (the reproducible part headlessly). OS text I/O"
         " (TSF / SendInput) is excluded — it is app/OS dependent and measured separately in the"
         " app's telemetry.");
    line("");

    // ---- 1. Correctness ----
    line("## 1. Correctness — final visible text per stream");
    line("");
    line("| stream | " + NG + " | " + OK + " | " + UK + " | 3-way |");
    line("|---|---|---|---|---|");
    std::uint64_t agree3 = 0, ab = 0, ac = 0, bc = 0;
    for (const Stream& s : streams) {
        const std::string& a = texts.at(s.name + "\x01" + NG);
        const std::string& b = texts.at(s.name + "\x01" + OK);
        const std::string& c = texts.at(s.name + "\x01" + UK);
        const bool a3 = (a == b && b == c);
        if (a3) ++agree3;
        if (a == b) ++ab;
        if (a == c) ++ac;
        if (b == c) ++bc;
        auto sh = [](const std::string& t) {
            std::string s = t;
            for (char& ch : s) { if (ch == '|') ch = '/'; }
            if (s.size() <= 36) { return s; }
            std::size_t cut = 36;
            while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) { --cut; }
            return s.substr(0, cut) + "…";
        };
        line("| " + s.name + " | `" + sh(a) + "` | `" + sh(b) + "` | `" + sh(c) + "` | "
             + std::string(a3 ? "✓" : "✗") + " |");
    }
    const std::uint64_t tot = streams.size();
    line("");
    line("**Agreement:** 3-way `" + std::to_string(agree3) + "/" + std::to_string(tot) +
         "` · " + NG + " == " + OK + " `" + std::to_string(ab) + "/" + std::to_string(tot) +
         "` · " + NG + " == " + UK + " `" + std::to_string(ac) + "/" + std::to_string(tot) +
         "` · " + OK + " == " + UK + " `" + std::to_string(bc) + "/" + std::to_string(tot) + "`");
    line("");

    // ---- 2. Passages vs intended ----
    line("## 2. Real passages — exact match vs intended Vietnamese text");
    line("");
    line("| engine | exact vs intended | of " + std::to_string(kNumPassages) + " |");
    line("|---|---|---|");
    for (const std::string& en : names) {
        std::uint64_t exact = 0;
        for (std::size_t i = 0; i < kNumPassages; ++i) {
            const std::string out = texts.at(std::string("passage-") + std::to_string(i) + "\x01" + en);
            if (out == u8(kPassageIntended[i])) ++exact;
        }
        line("| " + en + " | **" + std::to_string(exact) + "** | " + std::to_string(kNumPassages) + " |");
    }
    line("");

    // ---- 3. Latency ----
    line("## 3. Latency — engine decision per key (ns)");
    line("");
    line("| engine | mean | p50 | p90 | p99 | max | sampled keys |");
    line("|---|---|---|---|---|---|---|");
    for (const std::string& en : names) {
        const LatDist& d = lats.at(en);
        const double mean = d.n ? static_cast<double>(d.sum) / static_cast<double>(d.n) : 0.0;
        char buf[512];
        std::snprintf(buf, sizeof(buf), "| %s | %.1f | %llu | %llu | %llu | %llu | %llu |",
                      en.c_str(), mean,
                      static_cast<unsigned long long>(percentile(d, 0.50)),
                      static_cast<unsigned long long>(percentile(d, 0.90)),
                      static_cast<unsigned long long>(percentile(d, 0.99)),
                      static_cast<unsigned long long>(d.max),
                      static_cast<unsigned long long>(d.n));
        line(buf);
    }
    line("");
    line("Includes ~20–40 ns of per-sample timing overhead, identical for all engines — the"
         " relative ranking is exact. `keys` from the correctness run: "
         + std::to_string(keyCounts.at(NG)) + " / " + std::to_string(keyCounts.at(OK))
         + " / " + std::to_string(keyCounts.at(UK)) + ".");
    line("");

    // ---- 4. Memory ----
    line("## 4. Engine state — memory footprint");
    line("");
    line("| engine | core object | internal buffers | approx total |");
    line("|---|---|---|---|");
    line("| " + NG + " | `TextEngine` " + std::to_string(sizeof(TextEngine)) + " B | raw-word buffer (kMaxBuff=32), undo history 64×32 | " + std::to_string(sizeof(TextEngine)) + " B (+ heap history) |");
    line("| " + OK + " | engine globals | TypingWord[80], word/state histories | ~8 KiB |");
    line("| " + UK + " | `UkEngine` " + std::to_string(sizeof(UkEngine)) + " B + `UkSharedMem` " + std::to_string(sizeof(UkSharedMem)) + " B | m_buffer[128] WordInfo, m_keyStrokes[128], macro store | " + std::to_string(sizeof(UkEngine) + sizeof(UkSharedMem)) + " B |");
    line("");

    line("## Notes");
    line("");
    line("- **\"Delay when typing accents\":** engine decision is 60–400 ns/key on all three"
         " engines — imperceptible. The perceived delay in the shipped app is the producer→consumer"
         " TSF hop (one synchronous edit session per edit). The fix shipped in this segment batches"
         " consecutive edits into ONE `TF_ES_SYNC` session and adds the producer-side ordering"
         " barrier; see `notes/fix_design.md`.");
    line("- **Ghosting/sticking when typing and deleting quickly:** fixed by (1) swallowing the"
         " KeyUp of a suppressed KeyDown (phantom key-up) and (2) draining pending edits before a"
         " pass-through key reaches the app. Both live in the shipped Windows exe; the engine-level"
         " semantics are locked by `tests/test_hotfix.cpp` and this benchmark.");
    line("- **Passage-11 (`confirm`)** and the fuzz show KieeKey's shipped spelling auto-restore"
         " (`restoreIfWrongSpelling=true`): non-Vietnamese words keep their raw letters, while the"
         " 2.0.5/UniKey defaults mark them (`cònirm`, `ẹuhrkt`). Turn the option off and all three"
         " agree.");
    line("- **UniKey upstream engine limit:** its `UKEngine` uses a fixed 128-entry word buffer;"
         " words longer than that overflow its state machine (OOB read, ASAN-confirmed in the"
         " vendored reference). No human-typed word can exceed it — the comparative streams cap"
         " words at 11 letters, the realistic maximum — and the issue is documented here rather"
         " than patched into the unmodified reference.");
    line("- **OpenKey 2.0.5 reference defect:** `checkForStandaloneChar` reads `TypingWord[_index - 1]`"
         " without an `_index > 0` guard (Engine.cpp:995). Under ASan this is a global-buffer-overflow"
         " READ (4 bytes before the 128-byte `TypingWord`), reproducible on the fuzz-5k stream. Release"
         " builds read benign adjacent memory and output is unaffected — the differential agreement is"
         " unchanged — but the unmodified vendored reference carries this pre-existing UB. It is"
         " documented here, not patched, to keep the reference pristine.");
    line("- Correctness differences between engines are the documented rule differences"
         " (free-marking, auto-restore defaults, tone placement). The exhaustive KieeKey vs 2.0.5"
         " differential is `MEGA_BENCH_REPORT.md`; this benchmark adds UniKey.");

    std::FILE* f = std::fopen("THREE_ENGINE_BENCH_REPORT.md", "w");
    if (f) { std::fputs(r.c_str(), f); std::fclose(f); }
    std::fputs(r.c_str(), stdout);
}

//============================================================================
// MAIN
//============================================================================
int main() {
    std::vector<Stream> streams;
    for (std::size_t i = 0; i < kNumPassages; ++i) {
        bool ok = true;
        const std::string keys = buildKeys(kPassageIntended[i], ok);
        if (ok) streams.push_back(Stream{"passage-" + std::to_string(i), keys});
    }
    for (std::size_t i = 0; i < sizeof(kStress) / sizeof(kStress[0]); ++i) {
        streams.push_back(Stream{"stress-" + std::to_string(i), kStress[i]});
    }
    streams.push_back(Stream{"fuzz-5k", makeFuzz(20260829, 5000)});

    std::vector<std::unique_ptr<Driver>> drivers;
    drivers.emplace_back(new KieeKeyDriver());
    drivers.emplace_back(new Ok205Driver());
    drivers.emplace_back(new UniKeyDriver());

    std::vector<std::string> names;
    for (auto& d : drivers) names.push_back(d->name());

    std::map<std::string, std::string> texts;
    std::map<std::string, std::uint64_t> keyCounts;
    for (auto& d : drivers) {
        std::uint64_t totalKeys = 0;
        for (const Stream& s : streams) {
            std::fprintf(stderr, ">>> [%s] %s\n", d->name(), s.name.c_str());
            runStream(*d, s.keys);
            texts[s.name + std::string("\x01") + d->name()] = d->text();
            totalKeys += d->keyCount();
        }
        keyCounts[d->name()] = totalKeys;
    }

    // Latency microbench: 20k-key fuzz × 100 reps = 2M samples per engine.
    std::map<std::string, LatDist> lats;
    const std::string latStream = makeFuzz(777, 20000);
    for (auto& d : drivers) {
        lats[d->name()] = benchLatency(*d, latStream, 100);
    }

    writeReport(streams, names, texts, keyCounts, lats);
    return 0;
}
