//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.0 - refactored and completed logic
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
// File: imebench_kit/harness/bench_perf.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// imebench_kit/harness/bench_perf.cpp — micro (engine decision) latency,
// hardened for the v1.2.0 release evidence.
//
// Methodology (v2, supersedes the v1.1.x per-key steady_clock harness):
//   * per-key steady_clock ns, identical -O3 -DNDEBUG flags on both sides;
//   * warm-up keys (default 200 000) discarded before any measurement;
//   * engine session RESET at the start of every workload (removes the old
//     cross-workload state carry-over);
//   * percentiles computed on the SORTED sample (the v1.1.x file indexed an
//     unsorted vector — p50/p99/…/max were invalid);
//   * min/mean/p50/p90/p99/p99.9/max reported per tier;
//   * T0-control: the identical measurement loop with the engine call
//     replaced by a volatile black hole — the per-sample timer+loop
//     overhead floor, reported separately (never subtracted silently);
//   * R runs (default 3) with a deterministic seeded shuffle of the
//     workload order per run (order-bias guard); every run recorded in the
//     machine-readable JSON (results/perf.json), including the full decile
//     tail so latency distributions are preserved, not just summarized.
//
// Workloads (deterministic, unchanged from v1.1.x):
//   vn-compose / passthrough / mixed / delete (see Workloads below).
//   T1-decision = TextEngine::process only; T1+encode = decision plus the
//   consumer's replacementUtf16 render.
//
// Usage: bench_perf [--keys=N] [--warmup=N] [--runs=R] [--seed=S]
//                   [--out=path] [--label=text]
//----------------------------------------------------------------------------
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "../../src/core/TextEngine.hpp"

using Clock = std::chrono::steady_clock;

namespace {

struct Sample {
    std::vector<std::uint32_t> ns;
    void add(std::uint32_t v) { ns.push_back(v); }
    void finalize() { std::sort(ns.begin(), ns.end()); }
    bool empty() const { return ns.empty(); }
    std::size_t size() const { return ns.size(); }
    double q(double p) const {
        if (ns.empty()) { return 0.0; }
        const std::size_t idx = static_cast<std::size_t>(
            p * static_cast<double>(ns.size() - 1) + 0.5);
        return static_cast<double>(ns[idx]);
    }
    double mean() const {
        if (ns.empty()) { return 0.0; }
        return static_cast<double>(std::accumulate(ns.begin(), ns.end(), 0ull)) /
               static_cast<double>(ns.size());
    }
    double minNs() const { return ns.empty() ? 0.0 : static_cast<double>(ns.front()); }
    double maxNs() const { return ns.empty() ? 0.0 : static_cast<double>(ns.back()); }
    // deciles + 99/99.9/99.99 tail as ns values (full-distribution record)
    std::vector<double> distribution() const {
        std::vector<double> d;
        for (int i = 1; i <= 10; ++i) { d.push_back(q(0.10 * static_cast<double>(i))); }
        d.push_back(q(0.99)); d.push_back(q(0.999)); d.push_back(q(0.9999));
        return d;
    }
};

struct TierResult {
    const char* name;
    Sample s;
    TierResult(const char* n) : name(n) {}
};

struct WlResult {
    std::string name;
    std::vector<TierResult> tiers;
};

// deterministic keystroke streams (identical to the v1.1.x workloads)
struct Workloads {
    static const char* compose() {
        return "xin chao cac ban toi la nguoi viet nam chao ngay moi dep qua";
    }
    static const char* passthrough() {
        return "the quick brown fox jumps over the lazy dog 0123456789";
    }
    static const char* mixed() {
        return "xinchao mixte text va tieng viet 2024 nam thay doi lon";
    }
    static const char* del() {
        return "chaof backss datex t\u00F4i";   // includes tone-toggle corrections
    }
};

volatile std::uint64_t g_sink = 0;   // black hole — defeats DCE of the control

} // namespace

int main(int argc, char** argv) {
    long keysPer = 2000000;
    long warmup = 200000;
    long runs = 3;
    std::uint64_t seed = 0x9E3779B97F4A7C15ull;
    const char* outPath = "imebench_kit/results/perf.json";
    std::string label;
    for (int i = 1; i < argc; ++i) {
        if (!std::strncmp(argv[i], "--keys=", 7)) keysPer = std::atol(argv[i] + 7);
        else if (!std::strncmp(argv[i], "--warmup=", 9)) warmup = std::atol(argv[i] + 9);
        else if (!std::strncmp(argv[i], "--runs=", 7)) runs = std::atol(argv[i] + 7);
        else if (!std::strncmp(argv[i], "--seed=", 7)) seed = std::strtoull(argv[i] + 7, nullptr, 10);
        else if (!std::strncmp(argv[i], "--out=", 6)) outPath = argv[i] + 6;
        else if (!std::strncmp(argv[i], "--label=", 8)) label = argv[i] + 8;
        else { std::fprintf(stderr, "[perf] unknown arg %s\n", argv[i]); return 2; }
    }

    const char* wlNames[] = {"vn-compose", "passthrough", "mixed", "delete"};
    const char* wlTexts[] = {Workloads::compose(), Workloads::passthrough(),
                             Workloads::mixed(), Workloads::del()};
    constexpr int kNW = 4;
    const char* tierNames[] = {"T1-decision", "T1+encode"};

    std::ofstream f(outPath);
    if (!f) { std::fprintf(stderr, "[perf] cannot open %s\n", outPath); return 2; }
    f << "{\n";
    f << "  \"schema\": \"bench_perf.v2\",\n";
    f << "  \"label\": \"" << label << "\",\n";
    f << "  \"method\": \"per-key steady_clock ns; warmup " << warmup
      << " keys; session reset per workload; sorted percentiles; T0-control = "
         "timer+loop overhead floor (reported, not subtracted); R runs with "
         "deterministic seeded workload-order shuffle; full decile+tail recorded\",\n";
    f << "  \"keys_per_workload\": " << keysPer << ",\n";
    f << "  \"warmup_keys\": " << warmup << ",\n";
    f << "  \"runs\": " << runs << ",\n";
    f << "  \"seed\": " << seed << ",\n";
    f << "  \"run_records\": [\n";

    for (long r = 0; r < runs; ++r) {
        // deterministic order shuffle for this run (order-bias guard)
        std::vector<int> order(kNW);
        std::iota(order.begin(), order.end(), 0);
        std::mt19937_64 rng(seed ^ static_cast<std::uint64_t>(r) * 0x100000001B3ull);
        std::shuffle(order.begin(), order.end(), rng);

        // T0-control: identical loop + timer, engine replaced by a black hole.
        // Measures the per-sample timer+loop overhead floor.
        {
            Sample ctl;
            for (long i = 0; i < keysPer; ++i) {
                const auto t0 = Clock::now();
                g_sink = g_sink + static_cast<std::uint64_t>(i) * 3u;
                const auto t1 = Clock::now();
                ctl.add(static_cast<std::uint32_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
            }
            ctl.finalize();
            f << (r == 0 ? "    {\n" : "    ,{\n");
            f << "      \"run\": " << r << ",\n";
            f << "      \"T0_control_ns\": { \"n\": " << ctl.size()
              << ", \"min\": " << ctl.minNs() << ", \"mean\": " << ctl.mean()
              << ", \"p50\": " << ctl.q(0.50) << ", \"p99\": " << ctl.q(0.99)
              << ", \"max\": " << ctl.maxNs() << " },\n";
            f << "      \"workloads\": [\n";
        }

        ok::text::EngineOptions o;   // as-shipped defaults
        ok::text::TextEngine eng(o);
        std::wstring scratch;

        for (int oi = 0; oi < kNW; ++oi) {
            const int w = order[static_cast<std::size_t>(oi)];
            const std::string s = wlTexts[w];
            TierResult tr1("T1-decision"), tr1e("T1+encode");
            eng.startNewSession();
            // warm-up on this workload's own session (discarded)
            std::size_t wi = 0;
            for (long i = 0; i < warmup; ++i) {
                const char c = s[wi % s.size()]; ++wi;
                ok::text::TextInput in;
                if (c == ' ') { in.kind = ok::text::InputKind::Space; }
                else { in.kind = ok::text::InputKind::Char; in.ch = static_cast<char32_t>(c); }
                static_cast<void>(eng.process(in));
            }
            // measured pass
            wi = 0;
            for (long i = 0; i < keysPer; ++i) {
                const char c = s[wi % s.size()]; ++wi;
                ok::text::TextInput in;
                if (c == ' ') { in.kind = ok::text::InputKind::Space; }
                else { in.kind = ok::text::InputKind::Char; in.ch = static_cast<char32_t>(c); }
                const Clock::time_point p0 = Clock::now();
                const auto& res = eng.process(in);
                const Clock::time_point p1 = Clock::now();
                eng.replacementUtf16(res, scratch);
                const Clock::time_point p2 = Clock::now();
                tr1.s.add(static_cast<std::uint32_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(p1 - p0).count()));
                tr1e.s.add(static_cast<std::uint32_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(p2 - p0).count()));
            }
            tr1.s.finalize();
            tr1e.s.finalize();
            const TierResult* tiers[] = {&tr1, &tr1e};
            f << (oi == 0 ? "        {\n" : "        ,{\n");
            f << "          \"name\": \"" << wlNames[w] << "\",\n";
            f << "          \"tiers\": [\n";
            for (int ti = 0; ti < 2; ++ti) {
                const TierResult& tr = *tiers[ti];
                const Sample& smp = tr.s;
                const auto dist = smp.distribution();
                f << (ti == 0 ? "            {\"tier\": \"" : "            ,{\"tier\": \"") << tr.name
                  << "\", \"n\": " << smp.size()
                  << ", \"min\": " << smp.minNs() << ", \"mean\": " << smp.mean()
                  << ", \"p50\": " << smp.q(0.50) << ", \"p90\": " << smp.q(0.90)
                  << ", \"p99\": " << smp.q(0.99) << ", \"p999\": " << smp.q(0.999)
                  << ", \"max\": " << smp.maxNs() << ", \"dist_ns\": [";
                for (std::size_t di = 0; di < dist.size(); ++di) {
                    f << (di == 0 ? "" : ",") << dist[di];
                }
                f << "]}\n";
            }
            f << "          ]\n        }\n";
        }
        f << "      ]\n    }\n";
    }
    f << "  ]\n}\n";
    f.close();
    std::fprintf(stderr, "[perf] wrote %s (runs=%ld keys=%ld warmup=%ld)\n",
                 outPath, runs, keysPer, warmup);
    return 0;
}
