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
// File: tests/soak_engine.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.0 — tests/soak_engine.cpp
// LONG-RUNNING SOAK: randomized-but-reproducible mixed workload (chars,
// spaces, backspaces, word breaks, mode switches, macro attempts, idle
// periods) run for many iterations while sampling live heap allocations and
// (on Linux) RSS. The requirement is a FLAT steady-state: allocations and
// RSS must not grow linearly with iterations.
//
//   g++ -std=c++23 -O2 -I src/core tests/soak_engine.cpp src/core/TextEngine.cpp -o soak
//   ./soak [iterations] [checkpointEvery]
//
// Exit 0 = memory slope is flat.
//----------------------------------------------------------------------------
#define OK_COUNT_ALLOCS 1

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "TextEngine.hpp"

using namespace ok::text;

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
#define HAVE_COUNTER 1
#endif

namespace {

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
    std::uint64_t next() noexcept {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 0x2545F4914F6CDD1DULL;
    }
    std::uint32_t below(std::uint32_t n) noexcept { return static_cast<std::uint32_t>(next() % n); }
};

std::size_t rssBytes() {
#if defined(__linux__)
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) { return 0; }
    char line[256];
    std::size_t vmrss = 0;
    while (std::fgets(line, sizeof line, f)) {
        if (std::sscanf(line, "VmRSS: %zu kB", &vmrss) == 1) { break; }
    }
    std::fclose(f);
    return vmrss * 1024;
#else
    return 0;
#endif
}

struct Sample {
    std::size_t iter;
    std::size_t liveAllocs;    // allocated - freed (0 = fully reclaimed)
    std::size_t allocBytes;    // cumulative allocated bytes
    std::size_t rss;
};

} // namespace

int main(int argc, char** argv) {
    const std::size_t iters = argc > 1 ? std::strtoull(argv[1], nullptr, 0) : 4000;
    const std::size_t every = argc > 2 ? std::strtoull(argv[2], nullptr, 0) : 200;
    std::printf("KieeKey — soak test (%zu iterations, sample every %zu)\n",
                iters, every);

    EngineOptions o; o.inputMethod = InputMethod::Telex; o.codeTable = CodeTable::Unicode;
    o.useMacro = true;
    TextEngine e(o);
    std::wstring scratch; scratch.reserve(256);

    Rng rng(0x50A0A5EEDULL);
    static const char32_t letters[] = {
        U'a',U'b',U'c',U'd',U'e',U'f',U'g',U'h',U'i',U'j',U'k',U'l',U'm',U'n',
        U'o',U'p',U'q',U'r',U's',U't',U'u',U'v',U'w',U'x',U'y',U'z',
    };
    std::vector<Sample> samples;
    std::size_t liveNow = 0, bytesNow = 0;

    for (std::size_t it = 0; it < iters; ++it) {
        // One "session" of mixed activity: 50-300 keys.
        const int keys = 50 + static_cast<int>(rng.below(250));
        for (int k = 0; k < keys; ++k) {
            TextInput in;
            const std::uint32_t kind = rng.below(10);
            switch (kind) {
                case 0: case 1: case 2: case 3: case 4: case 5:
                    in.kind = InputKind::Char;
                    in.ch = letters[rng.below(static_cast<std::uint32_t>(std::size(letters)))];
                    break;
                case 6: in.kind = InputKind::Space; break;
                case 7: in.kind = InputKind::Backspace; break;
                case 8: in.kind = InputKind::WordBreak; in.vkCode = 0x0D; break;   // Enter
                case 9: in.kind = InputKind::MouseDown; break;
            }
            in.isCaps = rng.below(8) == 0;
            in.otherCtrl = rng.below(25) == 0;
            const EngineResult& r = e.process(in);
            if (r.consumed()) { e.replacementUtf16(r, scratch); }
        }
        // occasional mode switch (Telex <-> VNI) and session reset
        if (rng.below(30) == 0) {
            o.inputMethod = (o.inputMethod == InputMethod::Telex)
                                ? InputMethod::Vni : InputMethod::Telex;
            e.setOptions(o);
        }
        if (rng.below(40) == 0) { e.startNewSession(); }
        // idle period (engine untouched)
        if (rng.below(50) == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

#if defined(HAVE_COUNTER)
        liveNow = g_allocs.load(std::memory_order_relaxed) -
                  g_frees.load(std::memory_order_relaxed);
        bytesNow = g_bytes.load(std::memory_order_relaxed);
#endif
        if ((it + 1) % every == 0 || it + 1 == iters) {
            samples.push_back({it + 1, liveNow, bytesNow, rssBytes()});
        }
    }

    std::printf("\n  iter    liveAllocs   allocBytes       RSS\n");
    for (const auto& s : samples) {
        std::printf("  %6zu   %10zu   %12zu   %8.2f MB\n",
                    s.iter, s.liveAllocs, s.allocBytes,
                    static_cast<double>(s.rss) / (1024 * 1024));
    }

    // ---- verdict: the SECOND half must show flat growth ----
    // live allocations may rise once during warmup (pre-reserved buffers)
    // then must stay CONSTANT; allocBytes (cumulative) may grow via churn but
    // a flat liveAllocs proves every allocation is freed (no leak).
    bool ok = true;
    if (samples.size() >= 4) {
        const auto& first = samples[samples.size() / 2];       // mid-point
        const auto& last  = samples.back();
        const std::size_t liveDelta = last.liveAllocs - first.liveAllocs;
        std::printf("\n  live allocations: mid=%zu final=%zu delta=%zu\n",
                    first.liveAllocs, last.liveAllocs, liveDelta);
        // Allow zero growth. Any growth after warmup = leak.
        if (liveDelta != 0) {
            ok = false;
            std::printf("  [FAIL] live allocations grew after warmup (possible leak)\n");
        } else {
            std::printf("  [ ok ] live allocations FLAT after warmup (no leak)\n");
        }
        // RSS must not grow by more than a small absolute amount (allocator
        // slack). 4 MiB tolerance for allocator fragmentation.
        if (last.rss > first.rss && last.rss - first.rss > 4 * 1024 * 1024) {
            ok = false;
            std::printf("  [FAIL] RSS grew by >4 MiB after warmup\n");
        } else {
            std::printf("  [ ok ] RSS flat after warmup\n");
        }
    }

    std::printf("\n%s\n", ok ? "SOAK TEST PASSED (flat steady-state)" : "SOAK TEST FAILED");
    return ok ? 0 : 1;
}
