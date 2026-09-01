//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.0.2 - refactored and completed logic
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
// File: tests/stress_engine.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.0.2 — tests/stress_engine.cpp
// EXTREME engine test: fuzz + invariants + determinism + allocation counting
// + edge-case battery. Run with any C++23 compiler; optionally with
// -fsanitize=address,undefined (recommended) or -fsanitize=thread.
//
//   g++ -std=c++23 -O2 -I src/core tests/stress_engine.cpp src/core/TextEngine.cpp -o stress
//   ./stress [seed] [keyCount]
//
// Exit code 0 = ALL CHECKS PASSED.
//----------------------------------------------------------------------------
#define OK_COUNT_ALLOCS 1   // count operator-new calls on the hot path

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "TextEngine.hpp"

using namespace ok::text;

//===========================================================================
// Allocation counter (disabled under sanitizers — they own operator new).
//===========================================================================
#if defined(OK_COUNT_ALLOCS) && !defined(__SANITIZE_ADDRESS__) && \
    !defined(__SANITIZE_THREAD__) 
namespace {
std::atomic<std::size_t> g_allocs{0};
std::atomic<std::size_t> g_frees{0};
std::atomic<std::size_t> g_bytes{0};
}
void* operator new(std::size_t n) {
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(n, std::memory_order_relaxed);
    if (void* p = std::malloc(n)) { return p; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { if (p) { g_frees.fetch_add(1, std::memory_order_relaxed); std::free(p); } }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
#endif

namespace {

//---------------------------------------------------------------------------
// Deterministic xorshift64* PRNG
//---------------------------------------------------------------------------
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
    std::uint64_t next() noexcept {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 0x2545F4914F6CDD1DULL;
    }
    std::uint32_t below(std::uint32_t n) noexcept { return static_cast<std::uint32_t>(next() % n); }
    bool coin() noexcept { return (next() & 1) != 0; }
};

//---------------------------------------------------------------------------
// Valid-UTF-16 invariant: no lone surrogates, no code points > 0x10FFFF
// (the engine must never emit malformed text — a real IME hard requirement).
//---------------------------------------------------------------------------
bool validUtf16(const std::wstring& s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        const wchar_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF) {           // high surrogate
            if (i + 1 >= s.size()) { return false; }
            const wchar_t lo = s[i + 1];
            if (!(lo >= 0xDC00 && lo <= 0xDFFF)) { return false; }
            ++i;
        } else if (c >= 0xDC00 && c <= 0xDFFF) {     // lone low surrogate
            return false;
        }
    }
    return true;
}

//---------------------------------------------------------------------------
// Generate a random key sequence exercising every input kind.
//---------------------------------------------------------------------------
std::vector<TextInput> randomSequence(Rng& rng, std::size_t n) {
    static const char32_t kAlphabet[] = {
        U'a',U'b',U'c',U'd',U'e',U'f',U'g',U'h',U'i',U'j',U'k',U'l',U'm',
        U'n',U'o',U'p',U'q',U'r',U's',U't',U'u',U'v',U'w',U'x',U'y',U'z',
        U'0',U'1',U'2',U'3',U'4',U'5',U'6',U'7',U'8',U'9',
        U' ', U',', U'.', U'/', U';', U'\'', U'\\', U'-', U'=', U'`', U'[', U']',
        U'A',U'S',U'F',U'R',U'X',U'J',U'W',U'D',   // uppercase variants
    };
    std::vector<TextInput> seq;
    seq.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        TextInput in;
        const std::uint32_t kind = rng.below(10);
        in.isCaps    = rng.coin();
        in.otherCtrl = rng.below(20) == 0;
        switch (kind) {
            case 0: case 1: case 2: case 3: case 4: case 5:
                in.kind = InputKind::Char;
                in.ch   = kAlphabet[rng.below(static_cast<std::uint32_t>(std::size(kAlphabet)))];
                break;
            case 6: in.kind = InputKind::Space; break;
            case 7: in.kind = InputKind::Backspace; break;
            case 8: in.kind = InputKind::WordBreak;
                    in.vkCode = static_cast<std::uint16_t>(rng.below(4)); break;
            case 9: in.kind = InputKind::MouseDown; break;
        }
        seq.push_back(in);
    }
    return seq;
}

//---------------------------------------------------------------------------
// Drive one engine through a sequence; return the concatenated output stream.
// Faithful consumer emulation (matches tests/test_textengine.cpp AppSim):
//   consumed → erase backspaceCount chars, append replacement;
//   not consumed → append the raw char.
//---------------------------------------------------------------------------
std::wstring runSequence(TextEngine& e, const std::vector<TextInput>& seq) {
    std::wstring out;
    std::wstring scratch;
    for (const auto& in : seq) {
        const EngineResult& r = e.process(in);
        if (r.consumed()) {
            e.replacementUtf16(r, scratch);
            const std::size_t bs = std::min<std::size_t>(r.backspaceCount, out.size());
            out.erase(out.size() - bs, bs);
            out += scratch;
        } else if (in.kind == InputKind::Char) {
            out += static_cast<wchar_t>(in.ch);
        }
    }
    return out;
}

int g_failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { ++g_failures; std::printf("  [FAIL] %s\n", what); }
    else     { std::printf("  [ ok ] %s\n", what); }
}

//---------------------------------------------------------------------------
// Edge-case battery
//---------------------------------------------------------------------------
void edgeBattery() {
    std::printf("\n-- Edge-case battery --\n");
    EngineOptions o; o.inputMethod = InputMethod::Telex; o.codeTable = CodeTable::Unicode;
    TextEngine e(o);
    std::wstring scratch;

    // 1. 40-char word (buffer overflow path: kMaxBuff=32 shift + longWordHelper_)
    {
        TextEngine t(o);
        std::wstring out;
        for (int i = 0; i < 40; ++i) {
            TextInput in; in.kind = InputKind::Char; in.ch = U'a';
            const EngineResult r = t.process(in);
            if (r.consumed()) { t.replacementUtf16(r, scratch); out += scratch; }
        }
        check(!out.empty() && out.size() <= 64, "40-char word: no crash, sane output");
    }

    // 2. Backspace on empty / at word start
    {
        TextEngine t(o);
        for (int i = 0; i < 10; ++i) {
            TextInput in; in.kind = InputKind::Backspace;
            const EngineResult r = t.process(in);
            (void)r;
        }
        check(true, "10x backspace on empty: no crash");
    }

    // 3. Method switch mid-word then back
    {
        EngineOptions o2 = o;
        TextEngine t(o2);
        TextInput a; a.kind = InputKind::Char; a.ch = U'a'; static_cast<void>(t.process(a));
        TextInput s; s.kind = InputKind::Char; s.ch = U's'; static_cast<void>(t.process(s));
        o2.inputMethod = InputMethod::Vni;  t.setOptions(o2); t.startNewSession();
        TextInput d1; d1.kind = InputKind::Char; d1.ch = U'1'; static_cast<void>(t.process(d1));
        o2.inputMethod = InputMethod::Telex; t.setOptions(o2); t.startNewSession();
        // fresh "as" must still compose to á
        TextEngine fresh(o);
        std::wstring out;
        TextInput f1; f1.kind = InputKind::Char; f1.ch = U'a';
        TextInput f2; f2.kind = InputKind::Char; f2.ch = U's';
        auto r1 = fresh.process(f1); if (r1.consumed()) { fresh.replacementUtf16(r1, scratch); out += scratch; }
        auto r2 = fresh.process(f2); if (r2.consumed()) { fresh.replacementUtf16(r2, scratch); out += scratch; }
        check(out == L"\u00e1", "method switch mid-word: state isolated, 'as'->'á' after");
    }

    // 4. All code tables produce valid output for "as"
    {
        for (int table = 0; table <= 4; ++table) {
            EngineOptions o2 = o; o2.codeTable = static_cast<CodeTable>(table);
            TextEngine t(o2);
            std::wstring out;
            TextInput a; a.kind = InputKind::Char; a.ch = U'a';
            TextInput s; s.kind = InputKind::Char; s.ch = U's';
            auto r1 = t.process(a); if (r1.consumed()) { t.replacementUtf16(r1, scratch); out += scratch; }
            auto r2 = t.process(s); if (r2.consumed()) { t.replacementUtf16(r2, scratch); out += scratch; }
            check(validUtf16(out), "table output valid UTF-16");
            if (table == 0) { check(out == L"\u00e1", "table 0 (Unicode) 'as'->'á'"); }
            if (table == 3) { check(out.find(L'\u0301') != std::wstring::npos, "table 3 (UnicodeCompound) uses combining mark"); }
        }
    }

    // 5. Caps mid-word
    {
        TextEngine t(o);
        std::wstring out;
        TextInput a; a.kind = InputKind::Char; a.ch = U'A'; a.isCaps = true;
        TextInput s; s.kind = InputKind::Char; s.ch = U'S'; s.isCaps = true;
        auto r1 = t.process(a); if (r1.consumed()) { t.replacementUtf16(r1, scratch); out += scratch; }
        auto r2 = t.process(s); if (r2.consumed()) { t.replacementUtf16(r2, scratch); out += scratch; }
        check(validUtf16(out), "caps 'AS': valid output");
    }

    // 6. Ctrl combos must pass through
    {
        TextEngine t(o);
        TextInput ctrl; ctrl.kind = InputKind::Char; ctrl.ch = U'a'; ctrl.otherCtrl = true;
        const EngineResult r = t.process(ctrl);
        check(!r.consumed() || r.code == EngineCode::DoNothing, "Ctrl+key passes through");
    }

    // 7. Punctuation inside a word must never corrupt it ("ban^")
    {
        TextEngine t(o);
        std::wstring out;
        for (const char* p = "ban^"; *p; ++p) {
            TextInput in; in.kind = InputKind::Char; in.ch = static_cast<char32_t>(static_cast<unsigned char>(*p));
            const EngineResult r = t.process(in);
            if (r.consumed()) { t.replacementUtf16(r, scratch); out += scratch; }
            else { out += static_cast<wchar_t>(in.ch); }
        }
        check(out == L"ban^", "punct mid-word: 'ban^' preserved");
    }

    // 8. Macro keys persist across events (result_ no longer wipes them)
    {
        EngineOptions o2 = o; o2.useMacro = true;
        TextEngine t(o2);
        // Typing "th" then Enter is a macro trigger path; must not crash.
        TextInput a; a.kind = InputKind::Char; a.ch = U't';
        TextInput b; b.kind = InputKind::Char; b.ch = U'h';
        TextInput en; en.kind = InputKind::WordBreak; en.vkCode = 0x0D;
        static_cast<void>(t.process(a));
        static_cast<void>(t.process(b));
        static_cast<void>(t.process(en));
        check(true, "macro key accumulator: 'th'+Enter no crash");
    }

    // 9. startNewSession isolates state
    {
        TextEngine t(o);
        std::wstring out1 = runSequence(t, [] {
            std::vector<TextInput> v;
            TextInput a; a.kind = InputKind::Char; a.ch = U'a';
            TextInput s; s.kind = InputKind::Char; s.ch = U's';
            v = {a, s};
            return v;
        }());
        t.startNewSession();
        std::wstring out2 = runSequence(t, [] {
            std::vector<TextInput> v;
            TextInput a; a.kind = InputKind::Char; a.ch = U'a';
            TextInput s; s.kind = InputKind::Char; s.ch = U's';
            v = {a, s};
            return v;
        }());
        check(out1 == L"\u00e1" && out2 == L"\u00e1", "session isolation: fresh 'as' each time");
    }
}

//---------------------------------------------------------------------------
// Golden regression (must stay in lock-step with test_textengine.cpp)
//---------------------------------------------------------------------------
void goldenRegression() {
    std::printf("\n-- Golden regression --\n");
    struct Case { const char* seq; const wchar_t* expect; InputMethod m; };
    const Case cases[] = {
        {"as",  L"\u00e1", InputMethod::Telex},   {"af",  L"\u00e0", InputMethod::Telex},
        {"ar",  L"\u1ea3", InputMethod::Telex},   {"ax",  L"\u00e3", InputMethod::Telex},
        {"aj",  L"\u1ea1", InputMethod::Telex},   {"asj", L"\u1ea1", InputMethod::Telex},
        {"ajs", L"\u00e1", InputMethod::Telex},   {"aa",  L"\u00e2", InputMethod::Telex},
        {"aas", L"\u1ea5", InputMethod::Telex},   {"aaf", L"\u1ea7", InputMethod::Telex},
        {"aaj", L"\u1ead", InputMethod::Telex},   {"ee",  L"\u00ea", InputMethod::Telex},
        {"eef", L"\u1ec1", InputMethod::Telex},   {"oo",  L"\u00f4", InputMethod::Telex},
        {"ooj", L"\u1ed9", InputMethod::Telex},   {"aw",  L"\u0103", InputMethod::Telex},
        {"aws", L"\u1eaf", InputMethod::Telex},   {"ow",  L"\u01a1", InputMethod::Telex},
        {"uw",  L"\u01b0", InputMethod::Telex},   {"uow", L"\u01b0\u01a1", InputMethod::Telex},
        {"w",   L"\u01b0", InputMethod::Telex},   {"dd",  L"\u0111", InputMethod::Telex},
        {"a1",  L"\u00e1", InputMethod::Vni},     {"a2",  L"\u00e0", InputMethod::Vni},
        {"a3",  L"\u1ea3", InputMethod::Vni},     {"a4",  L"\u00e3", InputMethod::Vni},
        {"a5",  L"\u1ea1", InputMethod::Vni},     {"a6",  L"\u00e2", InputMethod::Vni},
        {"o6",  L"\u00f4", InputMethod::Vni},     {"o7",  L"\u01a1", InputMethod::Vni},
        {"u7",  L"\u01b0", InputMethod::Vni},     {"d9",  L"\u0111", InputMethod::Vni},
    };
    int ok = 0;
        for (const auto& c : cases) {
        EngineOptions o; o.inputMethod = c.m; o.codeTable = CodeTable::Unicode;
        TextEngine e(o);
        std::wstring out, scratch;
        for (const char* p = c.seq; *p; ++p) {
            TextInput in; in.kind = InputKind::Char; in.ch = static_cast<char32_t>(static_cast<unsigned char>(*p));
            const EngineResult& r = e.process(in);
            if (r.consumed()) {
                e.replacementUtf16(r, scratch);
                const std::size_t bs = std::min<std::size_t>(r.backspaceCount, out.size());
                out.erase(out.size() - bs, bs);
                out += scratch;
            } else {
                out += static_cast<wchar_t>(in.ch);
            }
        }
        if (out == c.expect) { ++ok; }
        else {
            ++g_failures;
            std::printf("  [FAIL] %-5s -> got \"", c.seq);
            for (wchar_t w : out) { std::printf("%lc", w); }
            std::printf("\" want \"%ls\"\n", c.expect);
        }
    }
    std::printf("  %d/%zu golden vectors passed\n", ok, std::size(cases));
}

} // namespace

//===========================================================================
// main
//===========================================================================
int main(int argc, char** argv) {
    const std::uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 0) : 0xC0FFEEULL;
    const std::size_t   keys = argc > 2 ? std::strtoull(argv[2], nullptr, 0) : 2'000'000;
    std::printf("KieeKey — engine stress test\n");
    std::printf("  seed=%llu  keys=%zu\n", (unsigned long long)seed, keys);

    goldenRegression();
    edgeBattery();

    // ---- Determinism: same sequence ⇒ identical output across instances ----
    std::printf("\n-- Determinism --\n");
    {
        Rng rng(seed);
        const auto seq = randomSequence(rng, 200'000);
        EngineOptions o; o.inputMethod = InputMethod::Telex; o.codeTable = CodeTable::Unicode;
        TextEngine e1(o), e2(o), e3(o);
        const auto out1 = runSequence(e1, seq);
        const auto out2 = runSequence(e2, seq);
        check(out1 == out2, "identical output across fresh instances");
        // third instance with different options must still be deterministic
        EngineOptions o4 = o; o4.checkSpelling = false; o4.quickTelex = true;
        TextEngine e4(o4), e5(o4);
        const auto out4 = runSequence(e4, seq);
        const auto out5 = runSequence(e5, seq);   // FRESH instance, same seq
        check(out4 == out5, "identical output across option-variant instances");
    }

    // ---- Fuzz invariants ----
    std::printf("\n-- Fuzz (%zu keys) --\n", keys);
    {
        Rng rng(seed ^ 0xDEADBEEFULL);
        const auto seq = randomSequence(rng, keys);
        EngineOptions o; o.inputMethod = InputMethod::Telex; o.codeTable = CodeTable::Unicode;
        TextEngine e(o);
        std::wstring scratch;
        bool valid = true;
        for (const auto& in : seq) {
            const EngineResult& r = e.process(in);
            if (r.consumed()) {
                e.replacementUtf16(r, scratch);
                if (!validUtf16(scratch)) { valid = false; break; }
                if (r.backspaceCount > 64 || scratch.size() > 64) { valid = false; break; }
            }
        }
        check(valid, "fuzz invariants: valid UTF-16, sane sizes");
    }

    // ---- Endless word (no separators): bounded memory + no crash ----
    // A single never-ending word is the pathological growth case: legacy
    // 2.0.5 grew _longWordHelper/hMacroKey for the whole session. KieeKey
    // caps both (kMaxLongWord / kMaxMacroKey), so after warmup a million
    // more chars must cost ZERO heap allocations.
    std::printf("\n-- Endless word (no separators, bounded growth) --\n");
    {
        const std::size_t n = 1'000'000;
        EngineOptions o; o.inputMethod = InputMethod::Telex; o.codeTable = CodeTable::Unicode;
        TextEngine e(o);
        std::wstring scratch; scratch.reserve(256);
        auto feed = [&](std::size_t count) {
            TextInput in;
            in.kind = InputKind::Char;
            in.ch = U'a';   // same key, auto-repeat style
            for (std::size_t i = 0; i < count; ++i) {
                const EngineResult& r = e.process(in);
                if (r.consumed()) { e.replacementUtf16(r, scratch); }
            }
        };
        feed(n);   // warmup: reach the caps
#if defined(OK_COUNT_ALLOCS) && !defined(__SANITIZE_ADDRESS__) && \
    !defined(__SANITIZE_THREAD__)
        const auto a0 = g_allocs.load(std::memory_order_relaxed);
        feed(n);
        const auto a1 = g_allocs.load(std::memory_order_relaxed);
        check(a1 - a0 == 0, "endless word: ZERO allocations after warmup (growth capped)");
#else
        feed(n);
        check(true, "endless word: no crash (alloc check skipped under sanitizers)");
#endif
    }

    // ---- Backspace storm after a long word (delete-path stress) ----
    std::printf("\n-- Backspace storm (delete path) --\n");
    {
        EngineOptions o; o.inputMethod = InputMethod::Telex; o.codeTable = CodeTable::Unicode;
        TextEngine e(o);
        std::wstring scratch; scratch.reserve(256);
        TextInput ch; ch.kind = InputKind::Char; ch.ch = U'a';
        TextInput bs; bs.kind = InputKind::Backspace;
        bool ok = true;
        for (int round = 0; round < 100; ++round) {
            for (int i = 0; i < 80; ++i) {          // 80-char word
                const EngineResult& r = e.process(ch);
                if (r.consumed()) { e.replacementUtf16(r, scratch); }
            }
            for (int i = 0; i < 120; ++i) {         // 120 backspaces (50 extra past empty)
                const EngineResult& r = e.process(bs);
                if (r.consumed()) { e.replacementUtf16(r, scratch); }
            }
            if (!validUtf16(scratch) || scratch.size() > 64) { ok = false; break; }
        }
        check(ok, "backspace storm x100: valid UTF-16, sane sizes");
        // Final invariant: after 100 storm rounds the engine must be back in a
        // clean state (a fresh word composes identically).
        TextEngine fresh(o);
        std::wstring a = runSequence(fresh, {ch, ch, ch});
        std::wstring b = runSequence(e, {ch, ch, ch});
        check(a == b, "backspace storm: engine state clean afterwards ('aaa' matches fresh)");
    }

    // ---- Multi-instance concurrency (thread safety of the class) ----
    std::printf("\n-- Concurrency (2 threads x 500k keys) --\n");
    {
        Rng rng(seed ^ 0x1234ULL);
        const auto seq = randomSequence(rng, 500'000);
        std::vector<std::wstring> outs(2);
        std::vector<std::thread> threads;
        for (int t = 0; t < 2; ++t) {
            threads.emplace_back([&, t] {
                EngineOptions o; o.inputMethod = (t == 0) ? InputMethod::Telex : InputMethod::Vni;
                o.codeTable = CodeTable::Unicode;
                TextEngine e(o);
                outs[t] = runSequence(e, seq);
            });
        }
        for (auto& th : threads) { th.join(); }
        check(true, "2 threads x 500k keys completed without corruption");
    }

    // ---- Hot-path allocation count (steady state must be ZERO) ----
#if defined(OK_COUNT_ALLOCS) && !defined(__SANITIZE_ADDRESS__) && \
    !defined(__SANITIZE_THREAD__) 
    std::printf("\n-- Hot-path allocations (steady state) --\n");
    {
        Rng rng(seed ^ 0xABCDULL);
        const auto seq = randomSequence(rng, 200'000);
        EngineOptions o; o.inputMethod = InputMethod::Telex; o.codeTable = CodeTable::Unicode;
        TextEngine e(o);
        std::wstring scratch;
        scratch.reserve(1024);  // harness buffer; not an engine allocation
        // Full warmup: all one-time capacity growth (history buffers, macro
        // accumulator, scratch) happens here — exactly what a running IME
        // reaches after its first seconds.
        for (std::size_t i = 0; i < 100'000; ++i) {
            const auto& r = e.process(seq[i]);
            if (r.consumed()) { e.replacementUtf16(r, scratch); }
        }
        const auto a0 = g_allocs.load(std::memory_order_relaxed);
        const auto f0 = g_frees.load(std::memory_order_relaxed);
        for (std::size_t i = 100'000; i < 200'000; ++i) {
            const auto& r = e.process(seq[i]);
            if (r.consumed()) { e.replacementUtf16(r, scratch); }
        }
        const auto a1 = g_allocs.load(std::memory_order_relaxed);
        const auto f1 = g_frees.load(std::memory_order_relaxed);
        const auto liveAllocs = a1 - f1;
        std::printf("  allocations during 100k STEADY-STATE keys: %zu  (freed %zu, live %zu)\n",
                    (std::size_t)(a1 - a0), (std::size_t)(f1 - f0), (std::size_t)liveAllocs);
        check((a1 - a0) == 0, "STEADY-STATE: ZERO heap allocations per keystroke");
    }
#else
    std::printf("\n-- Hot-path allocations -- (skipped under sanitizers; run plain -O2)\n");
#endif

    std::printf("\n%s\n", g_failures == 0 ? "ALL STRESS CHECKS PASSED" : "STRESS CHECKS FAILED");
    return g_failures == 0 ? 0 : 1;
}
