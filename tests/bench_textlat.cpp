//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.1.1 - refactored and completed logic
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
// File: tests/bench_textlat.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// tests/bench_textlat.cpp — standalone per-key latency driver for TextEngine.
// Feeds a deterministic Telex key stream (same flavor as the three-engine
// harness) and reports mean/p50/p90/p99 per-key decision latency.
//
// Build:
//   g++ -std=c++17 -O2 -I src/core tests/bench_textlat.cpp \
//       src/core/TextEngine.cpp -o bench_textlat
//----------------------------------------------------------------------------
#include "TextEngine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

using namespace ok::text;
using Clock = std::chrono::steady_clock;

// Deterministic pseudo-random Telex stream: 78% a-z, 12% space, 5% accents
// (s f r x j), 3% punctuation, 2% backspace.
static std::string makeStream(std::uint64_t seed, std::size_t n) {
    std::string s;
    s.reserve(n);
    std::uint64_t x = seed;
    auto rnd = [&x]() {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        return x;
    };
    const char* letters = "abcdefghijklmnopqrstuvwxyz";
    const char* accents = "sfrxj";
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t r = rnd() % 100;
        if (r < 78)      s.push_back(letters[rnd() % 26]);
        else if (r < 90) s.push_back(' ');
        else if (r < 95) s.push_back(accents[rnd() % 5]);
        else if (r < 98) s.push_back((rnd() % 2) ? ',' : '.');
        else             s.push_back('\b');
    }
    return s;
}

int main(int argc, char** argv) {
    const std::size_t nKeys = (argc > 1) ? static_cast<std::size_t>(std::atoll(argv[1]))
                                         : 2000000;
    const std::string stream = makeStream(0x9E3779B97F4A7C15ull, nKeys);

    EngineOptions eo;
    eo.inputMethod = InputMethod::Telex;
    TextEngine eng(eo);

    // warmup
    for (char c : stream) {
        TextInput in;
        if (c == ' ') { in.kind = InputKind::Space; }
        else if (c == '\b') { in.kind = InputKind::Backspace; }
        else if (c == ',' || c == '.') { in.kind = InputKind::Char; in.ch = c; }
        else { in.kind = InputKind::Char; in.ch = static_cast<char32_t>(c); }
        (void)eng.process(in);
    }

    std::vector<std::uint64_t> t;
    t.reserve(nKeys);
    for (char c : stream) {
        TextInput in;
        if (c == ' ') { in.kind = InputKind::Space; }
        else if (c == '\b') { in.kind = InputKind::Backspace; }
        else if (c == ',' || c == '.') { in.kind = InputKind::Char; in.ch = c; }
        else { in.kind = InputKind::Char; in.ch = static_cast<char32_t>(c); }
        const auto t0 = Clock::now();
        (void)eng.process(in);
        const auto t1 = Clock::now();
        t.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }

    std::sort(t.begin(), t.end());
    const double mean = static_cast<double>(std::accumulate(t.begin(), t.end(), 0ull)) / t.size();
    auto pct = [&](double p) { return t[static_cast<std::size_t>(p * (t.size() - 1))]; };
    std::printf("TextEngine Telex per-key latency  n=%zu  (ns)\n", nKeys);
    std::printf("  mean %8.1f  p50 %8llu  p90 %8llu  p99 %8llu  max %8llu\n",
                mean,
                (unsigned long long)pct(0.50),
                (unsigned long long)pct(0.90),
                (unsigned long long)pct(0.99),
                (unsigned long long)t.back());
    return 0;
}
