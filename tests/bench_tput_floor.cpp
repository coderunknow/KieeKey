//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.2 — throughput-floor driver (RC2)
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
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
// File: tests/bench_tput_floor.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// v1.2.2 RC2 — the throughput FLOOR driver, versioned in-repo.
//
// RC1's floor number (median of 3 x 20 M-key runs, 43.07 / 42.73 /
// 48.15 ns/key) came from an ad-hoc script that was never committed, so it
// could not be re-measured or re-claimed later. This driver is the standing
// definition of the floor: default shipping options (Telex, Unicode table,
// grammar ON, dict OFF), a deterministic keystream, mode 1 = process() +
// replacementUtf16() into a reused scratch (the exact consumer hot path,
// including the RC2 VIQR-precedence branch), and a sink digest that proves
// every measured run did the same work. The engine is only allowed to get
// FASTER from here (or stay inside run-to-run noise); a regression beyond
// the accepted band must be justified in the release report, same policy as
// RC1's.
//
// NOTE on continuity: the keystream generator below is NOT byte-identical
// to the RC1 ad-hoc driver, so the RC2 sink/numbers start a fresh series;
// ns/key remains comparable because the workload SHAPE (same alphabet mix,
// same space/back distribution, same engine config) is unchanged. The RC1
// evidence stays valid in docs/bench and the RC1 changelog entry.
//----------------------------------------------------------------------------
#include "TextEngine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace ok::text;

namespace {

constexpr std::uint64_t kSeed0 = 0x9E3779B97F4A7C15ull;   // splitmix step
inline std::uint64_t nextRand(std::uint64_t& s) {
    s += kSeed0;
    std::uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Alphabet mix mirrors natural Vietnamese Telex typing: heavy vowels and
// the tone/transformation keys (a s d f j w x z r q), a slice of consonants,
// ~6% spaces (word breaks commit words + flush undo history), ~2%
// backspaces (the delete path), a sprinkle of digits (ignored while
// digitsAreLiteral is on — also pinning that gate stays cheap).
const char32_t kWeights[] = U"aaaaaaaassssddddddddffffffjjjjwwwwxxxxzzzzrrrrqqqq"
                            "vvvvbbbbnnnnmmmmgggghhhhkkkllllooooouuuuuuuuiiiiii"
                            "eeeepppptttt0123456789";

std::vector<TextInput> makeStream(std::size_t keys) {
    std::vector<TextInput> v;
    v.reserve(keys);
    std::uint64_t s = 0x20260905ull ^ (keys * 131u);   // deterministic
    const std::size_t alen = sizeof(kWeights) / sizeof(kWeights[0]) - 1;
    for (std::size_t i = 0; i < keys; ++i) {
        const std::uint64_t r = nextRand(s) % 10000u;
        TextInput in{};
        if (r < 600)          in.kind = InputKind::Space;
        else if (r < 800)     in.kind = InputKind::Backspace;
        else {
            in.kind = InputKind::Char;
            in.ch = (r >= 9700) ? char32_t(U'0' + nextRand(s) % 10)
                                : kWeights[nextRand(s) % alen];
            in.isCaps = ((nextRand(s) & 0x3F) == 0);   // ~1.5% shift events
        }
        v.push_back(in);
    }
    return v;
}

struct RunResult { double nsPerKey; std::uint64_t sink; };

RunResult runOnce(const std::vector<TextInput>& stream, bool render) {
    TextEngine eng;                    // default shipping options
    std::wstring scratch;              // reused exactly like the consumers
    std::uint64_t sink = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (const TextInput& in : stream) {
        const EngineResult& r = eng.process(in);
        std::uint64_t acc = (static_cast<std::uint64_t>(r.code) << 24) ^
                            (static_cast<std::uint64_t>(r.backspaceCount) << 16) ^
                            (static_cast<std::uint64_t>(r.newCharCount) << 8) ^
                            (r.consumed() ? 1 : 0);
        if (render) {
            scratch.clear();
            eng.replacementUtf16(r, scratch);
            for (wchar_t wc : scratch)
                acc = acc * 1315423911ull + static_cast<std::uint32_t>(wc);
            acc += scratch.size() * 0x9E3779B1ull;
        }
        sink = sink * 1099511628211ull + acc;
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return {ms * 1'000'000.0 / static_cast<double>(stream.size()), sink};
}

} // namespace

int main(int argc, char** argv) {
    std::size_t keys = 20'000'000;
    int iters = 3;
    int mode = 1;                       // 1 = decisions + render (the floor)
    for (int i = 1; i < argc; ++i) {
        if (!std::strncmp(argv[i], "--keys=", 7))
            keys = std::strtoull(argv[i] + 7, nullptr, 10);
        else if (!std::strncmp(argv[i], "--iters=", 8))
            iters = std::atoi(argv[i] + 8);
        else if (!std::strncmp(argv[i], "--mode=", 7))
            mode = std::atoi(argv[i] + 7);
        else { std::printf("unknown arg %s\n", argv[i]); return 2; }
    }
    if (mode != 0 && mode != 1) { std::printf("mode must be 0 or 1\n"); return 2; }
    const std::vector<TextInput> stream = makeStream(keys);
    double ns[32];
    std::uint64_t sink = 0;
    const int it = std::min(iters, 32);
    for (int i = 0; i < it; ++i) {
        const RunResult r = runOnce(stream, mode == 1);
        ns[i] = r.nsPerKey;
        if (i == 0) sink = r.sink;
        else if (r.sink != sink) { std::printf("SINK DRIFT run %d — nondeterminism!\n", i); return 3; }
    }
    std::sort(ns, ns + it);
    std::printf("keys=%zu mode=%d total=%.1f ms  %.2f ns/key (median of %d)  sink=%llu\n",
                keys, mode, ns[it / 2] * keys / 1e6, ns[it / 2], it,
                (unsigned long long)sink);
    for (int i = 0; i < it; ++i)
        std::printf("  run%d: %.2f ns/key\n", i, ns[i]);
    return 0;
}
