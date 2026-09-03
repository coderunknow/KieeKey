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
// File: imebench_kit/harness/bench_perf.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// imebench_kit/harness/bench_perf.cpp — 2,000,000 keys per workload,
// per-key steady_clock, -O2 identical flags. Workloads: vn-compose,
// passthrough, mixed, delete. Emits results/perf.json.
//----------------------------------------------------------------------------
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "../../src/core/TextEngine.hpp"

using Clock = std::chrono::steady_clock;

namespace {

struct Stats {
    std::vector<std::uint32_t> ns;
    void add(std::uint32_t v) { ns.push_back(v); }
    void percentile(double p, std::uint32_t& out) const {
        if (ns.empty()) { out = 0; return; }
        const std::size_t idx = std::min(ns.size() - 1,
            static_cast<std::size_t>(p * static_cast<double>(ns.size())));
        out = ns[idx];
    }
};

// A deterministic vn-compose stream: tone-bearing words with corrections.
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

} // namespace

int main(int argc, char** argv) {
    long keysPer = 2000000;
    const char* outPath = "imebench_kit/results/perf.json";
    for (int i = 1; i < argc; ++i) {
        if (!std::strncmp(argv[i], "--keys=", 7)) keysPer = std::atol(argv[i] + 7);
        else if (!std::strncmp(argv[i], "--out=", 6)) outPath = argv[i] + 6;
    }

    const char* wlNames[] = {"vn-compose", "passthrough", "mixed", "delete"};
    const char* wlTexts[] = {Workloads::compose(), Workloads::passthrough(),
                             Workloads::mixed(), Workloads::del()};

    std::ofstream f(outPath);
    f << "{\n  \"protocol\": \"2M keys/workload, per-key steady_clock ns, -O2\",\n";
    f << "  \"engines\": [\n";

    // ---- KieeKey TextEngine ----
    {
        ok::text::EngineOptions o;   // as-shipped
        ok::text::TextEngine eng(o);
        std::wstring scratch;
        f << "    {\n      \"engine\": \"kieekey\",\n      \"workloads\": [\n";
        for (int w = 0; w < 4; ++w) {
            Stats st;
            const std::string s = wlTexts[w];
            std::size_t idx = 0;
            eng.startNewSession();
            Stats stEnc;
            while (st.ns.size() < static_cast<std::size_t>(keysPer)) {
                const char c = s[idx % s.size()];
                ++idx;
                ok::text::TextInput in;
                if (c == ' ') { in.kind = ok::text::InputKind::Space; }
                else if (c == '\b') { in.kind = ok::text::InputKind::Backspace; }
                else { in.kind = ok::text::InputKind::Char; in.ch = static_cast<char32_t>(c); }
                const Clock::time_point t0 = Clock::now();
                const auto& r = eng.process(in);
                const Clock::time_point t1 = Clock::now();
                std::wstring rep;
                eng.replacementUtf16(r, scratch);           // consumer encode cost
                const Clock::time_point t2 = Clock::now();
                st.add(static_cast<std::uint32_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
                stEnc.add(static_cast<std::uint32_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t0).count()));
                (void)rep;
            }
            auto emit = [&](const char* tier, const Stats& st2, bool last) {
                std::uint32_t p50, p90, p99, p999, mx;
                st2.percentile(0.50, p50); st2.percentile(0.90, p90);
                st2.percentile(0.99, p99); st2.percentile(0.999, p999);
                mx = st2.ns.back();
                f << "        {\"name\": \"" << wlNames[w] << "\", \"tier\": \"" << tier
                  << "\", \"keys\": " << st2.ns.size()
                  << ", \"p50\": " << p50 << ", \"p90\": " << p90 << ", \"p99\": " << p99
                  << ", \"p999\": " << p999 << ", \"max\": " << mx << "}"
                  << (last ? "" : ",") << "\n";
            };
            emit("T1-decision", st, false);
            emit("T1+encode", stEnc, w >= 3);   // last entry of the last workload
        }
        f << "      ]\n    }\n";
    }
    f << "  ]\n}\n";
    std::fprintf(stderr, "[perf] wrote %s\n", outPath);
    return 0;
}
