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
// File: imebench_kit/harness/bench_mem.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// imebench_kit/harness/bench_mem.cpp — M1: allocations per 100k keys
// (global new/delete counter), M2: live-object slope over 100k words,
// RSS samples. Emits results/mem.json.
//----------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <string>

#include "../../src/core/TextEngine.hpp"

namespace {
std::size_t gAllocs = 0;
std::size_t gLive = 0;
}

#if !defined(__SANITIZE_ADDRESS__)
static void* (*gOldMalloc)(std::size_t) = nullptr;
void* operator new(std::size_t n) {
    ++gAllocs; ++gLive;
    void* p = std::malloc(n ? n : 1);
    if (!p) { throw std::bad_alloc(); }
    return p;
}
void* operator new[](std::size_t n) {
    ++gAllocs; ++gLive;
    void* p = std::malloc(n ? n : 1);
    if (!p) { throw std::bad_alloc(); }
    return p;
}
void operator delete(void* p) noexcept {
    if (p) { --gLive; }
    std::free(p);
}
void operator delete[](void* p) noexcept {
    if (p) { --gLive; }
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { operator delete[](p); }
#endif

static long rssKb() {
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) { return -1; }
    long total = 0, resident = 0;
    if (std::fscanf(f, "%ld %ld", &total, &resident) != 2) { resident = -1; }
    std::fclose(f);
    return resident * 4;   // pages -> KiB
}

int main(int argc, char** argv) {
    long wordsPer = 100000;
    const char* outPath = "imebench_kit/results/mem.json";
    for (int i = 1; i < argc; ++i) {
        if (!std::strncmp(argv[i], "--words=", 8)) wordsPer = std::atol(argv[i] + 8);
        else if (!std::strncmp(argv[i], "--out=", 6)) outPath = argv[i] + 6;
    }

    // Warm up (constructor reservations, locale, etc.)
    {
        ok::text::TextEngine warm;
        for (const char* w : {"xin", "chao", "cac", "ban"}) {
            for (const char* p = w; *p; ++p) {
                ok::text::TextInput in; in.kind = ok::text::InputKind::Char; in.ch = *p;
                static_cast<void>(warm.process(in));
            }
            ok::text::TextInput sp; sp.kind = ok::text::InputKind::Space;
            static_cast<void>(warm.process(sp));
        }
    }

    // ---- M1: allocations per 100k keys ----
    const std::size_t allocsBefore = gAllocs;
    ok::text::TextEngine eng;
    const char* stream = "xin chao cac ban toi la nguoi viet nam day la doan van mau co dau";
    long keys = 0;
    while (keys < wordsPer) {   // wordsPer keys for M1
        const char c = stream[keys % (sizeof(stream) - 1)];
        ok::text::TextInput in;
        if (c == ' ') { in.kind = ok::text::InputKind::Space; }
        else { in.kind = ok::text::InputKind::Char; in.ch = c; }
        static_cast<void>(eng.process(in));
        ++keys;
    }
    const std::size_t m1 = gAllocs - allocsBefore;

    // ---- M2: live-object slope over 100k words ----
    const std::size_t liveBefore = gLive;
    long rssBefore = rssKb();
    for (long i = 0; i < wordsPer; ++i) {
        ok::text::TextEngine session;   // one IME session per word (worst case)
        for (const char* p = "tiengviet"; *p; ++p) {
            ok::text::TextInput in; in.kind = ok::text::InputKind::Char; in.ch = *p;
            static_cast<void>(session.process(in));
        }
        ok::text::TextInput sp; sp.kind = ok::text::InputKind::Space;
        static_cast<void>(session.process(sp));
    }
    const std::size_t liveAfter = gLive;
    long rssAfter = rssKb();
    const double slope = static_cast<double>(liveAfter > liveBefore ? liveAfter - liveBefore : 0)
                         / (static_cast<double>(wordsPer) / 10000.0);

    std::ofstream f(outPath);
    f << "{\n  \"engine\": \"kieekey\",\n"
      << "  \"M1_allocs_per_" << wordsPer << "_keys\": " << m1 << ",\n"
      << "  \"M2_live_object_slope_per_10k_words\": " << slope << ",\n"
      << "  \"M2_live_delta\": " << static_cast<long>(liveAfter) - static_cast<long>(liveBefore) << ",\n"
      << "  \"rss_kib_before\": " << rssBefore << ",\n"
      << "  \"rss_kib_after\": " << rssAfter << "\n}\n";
    std::fprintf(stderr, "[mem] M1=%zu allocs/%ld keys, M2 slope=%.2f/10k words, RSS %ld->%ld KiB\n",
                 m1, wordsPer, slope, rssBefore, rssAfter);
    return 0;
}
