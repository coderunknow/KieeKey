//============================================================================
// KieeKey v1.2.1 RC2 — tests/bench_real_world_typing.cpp
// Real-world typing latency + resource cost benchmark.
//
// Unlike e2e_bench.cpp (which measures burst throughput) and bench_tone_latency
// (which measures per-population pipeline latency), this harness models
// HUMAN-LIKE typing patterns:
//
//   * Variable inter-key intervals (30-200+ WPM equivalent)
//   * Bursts followed by pauses (think-then-type)
//   * Backspace corrections
//   * Spaces between words
//   * Punctuation
//   * Application-switching simulation (idle gaps)
//   * Single keystrokes separated by long idle periods
//
// For each workload it reports BOTH:
//   LATENCY:  p50, p90, p95, p99, p99.9, max
//   RESOURCE: CPU-time-per-1000-keys, wall-clock time
//
// Build (native / Linux shim):
//   g++ -std=c++20 -O2 -I src/core -I tests -DOK_WRAP_NO_WIN32
//       tests/bench_real_world_typing.cpp src/core/win32_wrapper.cpp
//       src/core/TextEngine.cpp -o bench_real_world
//
// Usage:
//   ./bench_real_world [--keys=N] [--workload=NAME] [--runs=R]
//
// Workloads:
//   all           (default) run all workloads
//   burst_200wpm  sustained 200 WPM burst
//   normal_60wpm  typical 60 WPM typing
//   slow_30wpm    slow deliberate typing
//   burst_pause   bursts separated by thinking pauses
//   single_keys   one key every ~1 second (worst case for spin decay)
//   mixed         realistic mix of all patterns
//
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "TextEngine.hpp"

using namespace ok::text;

// Timing
using Clock = std::chrono::steady_clock;
using ns = std::chrono::nanoseconds;

static inline std::uint64_t nowNs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<ns>(Clock::now().time_since_epoch()).count());
}

// Percentile helper
static double percentile(std::vector<std::uint64_t>& sorted_data, double p) {
    if (sorted_data.empty()) return 0.0;
    const double idx = p * (sorted_data.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(idx);
    const std::size_t hi = lo + 1;
    if (hi >= sorted_data.size()) return static_cast<double>(sorted_data.back());
    const double frac = idx - lo;
    return sorted_data[lo] * (1.0 - frac) + sorted_data[hi] * frac;
}

// Simple Telex typing sequences (the engine processes these)
struct KeySequence {
    char32_t ch;
    bool isCaps;
    InputKind kind;
    std::uint64_t delayAfterNs;  // delay BEFORE this key (inter-key gap)
};

// Generate a word as a sequence of Telex keystrokes
static void appendWord(std::vector<KeySequence>& seq, const char* word,
                       std::mt19937& rng, std::uint64_t baseGapNs) {
    std::uniform_int_distribution<int> jitter(80, 120);  // ±20% jitter
    for (const char* p = word; *p; ++p) {
        KeySequence k;
        k.ch = static_cast<char32_t>(*p);
        k.isCaps = (*p >= 'A' && *p <= 'Z');
        k.kind = InputKind::Char;
        k.delayAfterNs = baseGapNs * jitter(rng) / 100;
        seq.push_back(k);
    }
    // Space after word
    KeySequence sp;
    sp.ch = U' ';
    sp.isCaps = false;
    sp.kind = InputKind::Space;
    sp.delayAfterNs = baseGapNs * 2;  // word gap is longer
    seq.push_back(sp);
}

// Workload generators
enum class Workload { Burst200, Normal60, Slow30, BurstPause, SingleKeys, Mixed };

static std::vector<KeySequence> generateWorkload(Workload w, std::size_t targetKeys,
                                                  std::mt19937& rng) {
    std::vector<KeySequence> seq;
    seq.reserve(targetKeys);

    // Vietnamese Telex words for realistic typing
    const char* words[] = {
        "xin", "chao", "cac", "ban", "toi", "la", "nguoi", "viet",
        "nam", "hom", "nay", "troi", "dep", "qua", "chung", "ta",
        "di", "choi", "nhe", "cam", "on", "rat", "nhieu", "hen",
        "gap", "lai", "sau", "nha", "ban", "co", "khong", "biet",
        "gi", "da", "roi", "the", "a", "vay", "sao", "ma",
        "duoc", "thoi", "ke", "may", "tinh", "lam", "viec", "hoc"
    };
    const std::size_t nwords = sizeof(words) / sizeof(words[0]);

    std::uint64_t baseGap = 0;
    switch (w) {
        case Workload::Burst200: baseGap = 3'000'000ULL; break;     // ~3ms = 200 WPM
        case Workload::Normal60: baseGap = 10'000'000ULL; break;    // ~10ms = 60 WPM
        case Workload::Slow30:   baseGap = 20'000'000ULL; break;    // ~20ms = 30 WPM
        case Workload::BurstPause: baseGap = 3'000'000ULL; break;   // burst rate
        case Workload::SingleKeys: baseGap = 1'000'000'000ULL; break; // 1 second
        case Workload::Mixed: baseGap = 8'000'000ULL; break;       // ~8ms average
    }

    std::uniform_int_distribution<std::size_t> wordDist(0, nwords - 1);
    std::uniform_int_distribution<int> backspaceChance(0, 99);
    std::uniform_int_distribution<int> punctChance(0, 99);
    const char puncts[] = {',', '.', '!', '?', ';'};

    while (seq.size() < targetKeys) {
        if (w == Workload::SingleKeys) {
            // One key at a time with long idle
            KeySequence k;
            k.ch = static_cast<char32_t>('a' + (seq.size() % 26));
            k.isCaps = false;
            k.kind = InputKind::Char;
            k.delayAfterNs = baseGap;
            seq.push_back(k);
            continue;
        }

        // Type a word
        appendWord(seq, words[wordDist(rng)], rng, baseGap);

        // Occasional backspace (typo correction)
        if (w == Workload::Mixed && backspaceChance(rng) < 5) {
            KeySequence bs;
            bs.ch = 0;
            bs.isCaps = false;
            bs.kind = InputKind::Backspace;
            bs.delayAfterNs = baseGap;
            seq.push_back(bs);
            // Re-type
            KeySequence retype;
            retype.ch = static_cast<char32_t>('a' + (seq.size() % 26));
            retype.isCaps = false;
            retype.kind = InputKind::Char;
            retype.delayAfterNs = baseGap;
            seq.push_back(retype);
        }

        // Occasional punctuation
        if (w == Workload::Mixed && punctChance(rng) < 10) {
            KeySequence pk;
            pk.ch = static_cast<char32_t>(puncts[seq.size() % 5]);
            pk.isCaps = false;
            pk.kind = InputKind::Char;
            pk.delayAfterNs = baseGap * 3;
            seq.push_back(pk);
        }

        // BurstPause: add a 500ms thinking pause every ~10 words
        if (w == Workload::BurstPause && seq.size() % 50 == 0) {
            if (!seq.empty()) {
                seq.back().delayAfterNs = 500'000'000ULL;  // 500ms pause
            }
        }
    }

    return seq;
}

struct BenchResult {
    std::string workloadName;
    std::size_t keysProcessed;
    std::uint64_t wallTimeNs;
    std::uint64_t engineCpuNs;   // total time spent in engine.process()
    std::vector<std::uint64_t> latencies;  // per-key engine decision latency
};

static BenchResult runWorkload(const std::string& name, const std::vector<KeySequence>& seq,
                                bool paced) {
    BenchResult result;
    result.workloadName = name;
    result.keysProcessed = 0;

    TextEngine engine;
    EngineOptions opts;
    opts.inputMethod = InputMethod::Telex;
    opts.digitsAreLiteral = true;
    engine.setOptions(opts);

    result.latencies.reserve(seq.size());
    std::uint64_t totalEngineNs = 0;

    const auto wallStart = Clock::now();

    for (std::size_t i = 0; i < seq.size(); ++i) {
        const auto& k = seq[i];

        // Simulate inter-key delay (paced mode)
        if (paced && k.delayAfterNs > 0 && i > 0) {
            // Busy-wait for accuracy (short delays) or sleep for long ones
            if (k.delayAfterNs > 1'000'000) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(k.delayAfterNs - 500'000));
            }
            const auto target = Clock::now() + std::chrono::nanoseconds(k.delayAfterNs);
            while (Clock::now() < target) {
                // spin
            }
        }

        TextInput in;
        in.kind = k.kind;
        in.ch = k.ch;
        in.isCaps = k.isCaps;

        const auto t0 = nowNs();
        const EngineResult& r = engine.process(in);
        const auto t1 = nowNs();
        const std::uint64_t latency = t1 - t0;

        totalEngineNs += latency;
        result.latencies.push_back(latency);
        ++result.keysProcessed;

        // Consume result (prevent optimization)
        if (r.consumed()) {
            // Simulate replacement (just read the data)
            volatile auto bs = r.backspaceCount;
            volatile auto nc = r.newCharCount;
            (void)bs; (void)nc;
        }
    }

    const auto wallEnd = Clock::now();
    result.wallTimeNs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<ns>(wallEnd - wallStart).count());
    result.engineCpuNs = totalEngineNs;

    return result;
}

static void printResult(const BenchResult& r) {
    auto sorted = r.latencies;
    std::sort(sorted.begin(), sorted.end());

    const double p50 = percentile(sorted, 0.50);
    const double p90 = percentile(sorted, 0.90);
    const double p95 = percentile(sorted, 0.95);
    const double p99 = percentile(sorted, 0.99);
    const double p999 = percentile(sorted, 0.999);
    const double maxLat = sorted.empty() ? 0 : static_cast<double>(sorted.back());
    const double mean = sorted.empty() ? 0 :
        static_cast<double>(std::accumulate(sorted.begin(), sorted.end(), 0ULL)) / sorted.size();

    const double cpuPerKey = r.keysProcessed > 0
        ? static_cast<double>(r.engineCpuNs) / r.keysProcessed : 0;
    const double keysPerSec = r.wallTimeNs > 0
        ? static_cast<double>(r.keysProcessed) * 1e9 / r.wallTimeNs : 0;
    const double wallMs = r.wallTimeNs / 1e6;

    std::printf("\n=== %s ===\n", r.workloadName.c_str());
    std::printf("  Keys processed:   %zu\n", r.keysProcessed);
    std::printf("  Wall time:        %.1f ms (%.0f keys/s)\n", wallMs, keysPerSec);
    std::printf("  Engine decision latency (ns):\n");
    std::printf("    mean=%.0f  p50=%.0f  p90=%.0f  p95=%.0f  p99=%.0f  p99.9=%.0f  max=%.0f\n",
                mean, p50, p90, p95, p99, p999, maxLat);
    std::printf("  CPU cost per key: %.0f ns\n", cpuPerKey);
    std::printf("  CPU per 1000 keys: %.1f us (%.3f ms)\n",
                cpuPerKey * 1000 / 1000.0, cpuPerKey * 1000 / 1e6);
}

int main(int argc, char** argv) {
    std::size_t targetKeys = 50000;
    int runs = 3;
    std::string workloadName = "all";
    bool paced = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--keys=", 7) == 0) {
            targetKeys = std::stoull(argv[i] + 7);
        } else if (std::strncmp(argv[i], "--runs=", 7) == 0) {
            runs = std::stoi(argv[i] + 7);
        } else if (std::strncmp(argv[i], "--workload=", 11) == 0) {
            workloadName = argv[i] + 11;
        } else if (std::strcmp(argv[i], "--paced") == 0) {
            paced = true;
        }
    }

    std::printf("KieeKey v1.2.1 RC2 — Real-World Typing Benchmark\n");
    std::printf("  keys=%zu  runs=%d  paced=%s  workload=%s\n",
                targetKeys, runs, paced ? "yes" : "no", workloadName.c_str());

    struct WorkloadDef {
        Workload type;
        const char* name;
        bool defaultPaced;
    };
    const WorkloadDef allWorkloads[] = {
        {Workload::Burst200, "burst_200wpm", false},
        {Workload::Normal60, "normal_60wpm", false},
        {Workload::Slow30,   "slow_30wpm",   false},
        {Workload::BurstPause, "burst_pause", false},
        {Workload::SingleKeys, "single_keys", false},
        {Workload::Mixed,    "mixed",         false},
    };

    std::mt19937 rng(42);  // deterministic seed

    for (const auto& wd : allWorkloads) {
        if (workloadName != "all" && workloadName != wd.name) continue;

        auto seq = generateWorkload(wd.type, targetKeys, rng);
        // Trim to targetKeys
        if (seq.size() > targetKeys) seq.resize(targetKeys);

        const bool usePaced = paced || wd.defaultPaced;

        // Warm-up run (not counted)
        runWorkload(std::string(wd.name) + "_warmup", seq, usePaced);

        // Measured runs
        std::vector<BenchResult> results;
        for (int r = 0; r < runs; ++r) {
            results.push_back(runWorkload(std::string(wd.name), seq, usePaced));
        }

        // Print median result
        std::sort(results.begin(), results.end(),
                  [](const BenchResult& a, const BenchResult& b) {
                      return a.wallTimeNs < b.wallTimeNs;
                  });
        printResult(results[runs / 2]);
    }

    std::printf("\n[done] Real-world typing benchmark complete.\n");
    return 0;
}
