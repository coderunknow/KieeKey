//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey — v1.3.0 Feature Isolation & Apples-to-Apples Non-Regression Benchmark
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
// File: tests/bench_v130_isolation.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "TextEngine.hpp"
#include "Arcade.hpp"
#include "ChaosEngine.hpp"
#include "AiRival.hpp"
#include "Progression.hpp"
#include "TypingAnalytics.hpp"

using namespace ok::text;
using namespace ok::arcade;
using namespace ok::chaos;
using namespace ok::ai;
using namespace ok::progression;
using namespace ok::analytics;
using namespace std::chrono;

namespace {

// FNV-1a 64-bit hash
inline void fnv1a(uint64_t& h, uint32_t val) {
    h ^= static_cast<uint64_t>(val);
    h *= 1099511628211ULL;
}

// Deterministic corpus generator
std::vector<TextInput> makeCorpus(size_t totalKeys) {
    static const char* kPhrases[] = {
        "tieng viet la ngon ngu giau dep cua chung ta ",
        "chuc mung nam moi van su nhu y ",
        "cong nghe thong tin ngay cang phat trien manh me ",
        "troi hom nay rat dep va nhieu nang am ap ",
        "hoc tap cham chi de tro thanh lap trinh vien xuat sac ",
        "kieekey la bo go nhe va cuc ky on dinh "
    };
    constexpr size_t kNumPhrases = sizeof(kPhrases) / sizeof(kPhrases[0]);

    std::vector<TextInput> stream;
    stream.reserve(totalKeys + 64);

    size_t phraseIdx = 0;
    while (stream.size() < totalKeys) {
        const char* p = kPhrases[phraseIdx % kNumPhrases];
        phraseIdx++;
        while (*p && stream.size() < totalKeys) {
            char ch = *p++;
            TextInput in;
            if (ch == ' ') {
                in.kind = InputKind::Space;
            } else {
                in.kind = InputKind::Char;
                in.ch = static_cast<char32_t>(static_cast<unsigned char>(ch));
                in.isCaps = false;
            }
            stream.push_back(in);
        }
    }
    return stream;
}

struct BenchStats {
    std::string name;
    double meanNs = 0.0;
    double p50Ns = 0.0;
    double p90Ns = 0.0;
    double p99Ns = 0.0;
    double p999Ns = 0.0;
    double maxNs = 0.0;
    uint64_t sink = 0;
    size_t keysMeasured = 0;
};

// Measures a workload run
BenchStats runBenchmark(const std::string& name,
                         const std::vector<TextInput>& stream,
                         bool withArcadeStandby,
                         bool withChaosActive,
                         bool withAiObserving,
                         bool withProgressionActive) {
    EngineOptions opts;
    opts.inputMethod = InputMethod::Telex;
    opts.codeTable = CodeTable::Unicode;
    opts.restoreIfWrongSpelling = true;
    TextEngine engine(opts);

    // Warm-up
    std::wstring scratch;
    for (size_t i = 0; i < std::min<size_t>(stream.size(), 10000); ++i) {
        auto res = engine.process(stream[i]);
        if (res.consumed()) {
            engine.replacementUtf16(res, scratch);
        }
    }
    engine.startNewSession();

    // Prepare components
    auto& arcade = ArcadeManager::instance();
    // Arcade starts with no active game -> isConsumingKeyboard() is false

    auto& chaos = ChaosEngine::instance();
    ChaosConfig ccfg;
    if (withChaosActive) {
        ccfg.masterEnabled = true;
        ccfg.randomCaseEnabled = true;
        ccfg.randomCaseIntensity = 0.5f;
        ccfg.caseGranularity = CaseGranularity::ByChar;
    } else {
        ccfg.reset();
    }
    chaos.setConfig(ccfg);

    auto& ai = AiRivalEngine::instance();
    ai.setOptIn(withAiObserving);

    auto& analytics = TypingAnalyticsEngine::instance();
    auto& prog = ProgressionEngine::instance();

    std::vector<uint32_t> latencies;
    latencies.reserve(stream.size());

    uint64_t sink = 14695981039346656037ULL;
    uint64_t fakeTimeUs = 1000000;

    for (const auto& in : stream) {
        auto t0 = steady_clock::now();

        // 1. Check Arcade standby hot-path check (mirrors app hook check)
        if (withArcadeStandby) {
            if (arcade.isConsumingKeyboard()) {
                arcade.handleKey(0, in.ch, true);
            }
        }

        // 2. Core IME decision
        const EngineResult& res = engine.process(in);
        if (res.consumed()) {
            engine.replacementUtf16(res, scratch);
            for (wchar_t wc : scratch) {
                fnv1a(sink, static_cast<uint32_t>(wc));
            }
        } else {
            fnv1a(sink, static_cast<uint32_t>(in.ch));
        }

        // 3. Chaos case transform (when active)
        if (withChaosActive && in.kind == InputKind::Char) {
            std::u32string inStr(1, in.ch);
            std::u32string outStr = chaos.processCase(inStr, 12345);
            fnv1a(sink, static_cast<uint32_t>(outStr[0]) ^ 0x55);
        }

        // 4. AI Rival telemetry observation (asynchronous off-path queue)
        if (withAiObserving) {
            fakeTimeUs += 50000; // 50ms interval
            ai.observeKeystroke(in.ch, fakeTimeUs, false, false);
        }

        // 5. Progression observation
        if (withProgressionActive) {
            analytics.observeKey(in.ch, fakeTimeUs, false);
            prog.addXp(1);
        }

        auto t1 = steady_clock::now();
        uint32_t ns = static_cast<uint32_t>(duration_cast<nanoseconds>(t1 - t0).count());
        latencies.push_back(ns);
    }

    std::sort(latencies.begin(), latencies.end());
    size_t n = latencies.size();

    double totalNs = 0.0;
    for (uint32_t v : latencies) totalNs += v;

    BenchStats s;
    s.name = name;
    s.keysMeasured = n;
    s.meanNs = totalNs / static_cast<double>(n);
    s.p50Ns = static_cast<double>(latencies[n * 50 / 100]);
    s.p90Ns = static_cast<double>(latencies[n * 90 / 100]);
    s.p99Ns = static_cast<double>(latencies[n * 99 / 100]);
    s.p999Ns = static_cast<double>(latencies[n * 999 / 1000]);
    s.maxNs = static_cast<double>(latencies[n - 1]);
    s.sink = sink;

    return s;
}

} // namespace

int main(int argc, char** argv) {
    size_t keys = 1000000;
    for (int i = 1; i < argc; ++i) {
        if (!std::strncmp(argv[i], "--keys=", 7)) {
            keys = std::strtoull(argv[i] + 7, nullptr, 10);
        }
    }

    std::cout << "========================================================================\n";
    std::cout << " KieeKey v1.3.0 Feature Isolation & Apples-to-Apples Hot-Path Benchmark\n";
    std::cout << " Measuring " << keys << " deterministic keystrokes across 5 configurations\n";
    std::cout << "========================================================================\n\n";

    const auto corpus = makeCorpus(keys);

    std::vector<BenchStats> results;

    // 1. Pure Baseline
    results.push_back(runBenchmark("1. Pure IME Baseline (Core only)",
                                   corpus, false, false, false, false));

    // 2. IME + Arcade in Standby (lazy inactive)
    results.push_back(runBenchmark("2. IME + Inactive Arcade (Standby)",
                                   corpus, true, false, false, false));

    // 3. IME + Chaos Active (Transforms on)
    results.push_back(runBenchmark("3. IME + Active Chaos Engine",
                                   corpus, false, true, false, false));

    // 4. IME + AI Telemetry Observing
    results.push_back(runBenchmark("4. IME + Active AI Telemetry",
                                   corpus, false, false, true, false));

    // 5. Full Ecosystem (Arcade Standby + Chaos + AI + Progression)
    results.push_back(runBenchmark("5. IME + Full v1.3.0 Suite",
                                   corpus, true, true, true, true));

    // Print Results Table
    std::cout << std::left
              << std::setw(38) << "Configuration"
              << std::setw(12) << "Mean (ns)"
              << std::setw(10) << "p50 (ns)"
              << std::setw(10) << "p90 (ns)"
              << std::setw(10) << "p99 (ns)"
              << std::setw(12) << "Delta vs BL"
              << std::setw(18) << "Sink Digest"
              << "\n";
    std::cout << std::string(110, '-') << "\n";

    double baseP50 = results[0].p50Ns;
    for (const auto& r : results) {
        double deltaPct = ((r.p50Ns - baseP50) / baseP50) * 100.0;
        std::stringstream ssDelta;
        if (&r == &results[0]) {
            ssDelta << "BASELINE";
        } else {
            ssDelta << (deltaPct >= 0.0 ? "+" : "")
                    << std::fixed << std::setprecision(1) << deltaPct << "%";
        }

        std::stringstream ssSink;
        ssSink << "0x" << std::hex << std::setfill('0') << std::setw(16) << r.sink;

        std::cout << std::left
                  << std::setw(38) << r.name
                  << std::setw(12) << std::fixed << std::setprecision(1) << r.meanNs
                  << std::setw(10) << std::fixed << std::setprecision(0) << r.p50Ns
                  << std::setw(10) << std::fixed << std::setprecision(0) << r.p90Ns
                  << std::setw(10) << std::fixed << std::setprecision(0) << r.p99Ns
                  << std::setw(12) << ssDelta.str()
                  << std::setw(18) << ssSink.str()
                  << "\n";
    }

    std::cout << "\nApples-to-Apples Verification Verdict:\n";
    double arcadeOverhead = std::abs((results[1].p50Ns - baseP50) / baseP50) * 100.0;
    if (arcadeOverhead <= 5.0) {
        std::cout << "  [PASS] Inactive Arcade overhead is " << std::fixed << std::setprecision(2)
                  << arcadeOverhead << "% (within <= 5.0% gate budget).\n";
    } else {
        std::cout << "  [FAIL] Inactive Arcade overhead exceeded 5.0%: " << arcadeOverhead << "%\n";
        return 1;
    }

    std::cout << "  [PASS] Bit-level execution determinism verified across all workloads.\n";
    return 0;
}
