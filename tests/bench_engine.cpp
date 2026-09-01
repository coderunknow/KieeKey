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
// File: tests/bench_engine.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.0.2 — tests/bench_engine.cpp
// TextEngine hot-path benchmark (ns/key). Build & run:
//   g++ -std=c++23 -O2 -I src/core tests/bench_engine.cpp src/core/TextEngine.cpp -o bench && ./bench
//
// Reference numbers (GCC 14, -O2, x86-64): 90–160 ns/key — the engine is
// never the bottleneck; human typing (200 WPM ≈ 17 keys/s ≈ 58 ms/key) is
// six orders of magnitude slower than a single engine decision.
//----------------------------------------------------------------------------
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include "TextEngine.hpp"

using namespace ok::text;
using namespace std::chrono;

namespace {

std::uint64_t bench(const char* corpus, InputMethod m) {
    EngineOptions o;
    o.inputMethod = m;
    o.codeTable = CodeTable::Unicode;
    TextEngine e(o);
    std::wstring scratch;
    volatile std::uint64_t sink = 0;

    constexpr int kIters = 5000;
    const std::size_t keysPerIter = std::char_traits<char>::length(corpus) + 1;

    const auto t0 = steady_clock::now();
    for (int it = 0; it < kIters; ++it) {
        for (const char* p = corpus; *p; ++p) {
            TextInput in;
            in.kind = InputKind::Char;
            in.ch = static_cast<char32_t>(static_cast<unsigned char>(*p));
            in.isCaps = (in.ch >= U'A' && in.ch <= U'Z');
            const EngineResult& r = e.process(in);
            if (r.consumed()) {
                e.replacementUtf16(r, scratch);
                sink += scratch.size();
            }
        }
        TextInput sp;
        sp.kind = InputKind::Space;
        static_cast<void>(e.process(sp));
        e.startNewSession();
    }
    const auto t1 = steady_clock::now();

    const std::size_t keys = static_cast<std::size_t>(kIters) * keysPerIter;
    const double nsPerKey = static_cast<double>(duration_cast<nanoseconds>(t1 - t0).count())
                            / static_cast<double>(keys);
    std::printf("  %-24s %7.0f ns/key  (%.2f µs/key)  sink=%llu\n",
                corpus, nsPerKey, nsPerKey / 1000.0,
                static_cast<unsigned long long>(sink));
    return static_cast<std::uint64_t>(nsPerKey);
}

} // namespace

int main() {
    std::printf("TextEngine hot-path benchmark (O2, %d iterations each):\n\n", 5000);
    bench("xin chao cac ban", InputMethod::Telex);
    bench("ban as aw aa ow uw uow", InputMethod::Telex);
    bench("toi la nguoi viet nam", InputMethod::Telex);
    bench("a1 a2 a3 a4 a5 a6 o6 e6 o7 u7", InputMethod::Vni);
    std::printf("\nHuman typing at 200 WPM = ~17 keys/s = ~58 ms/key.\n");
    std::printf("Engine cost is ~0.0002 ms of that budget.\n");
    return 0;
}
