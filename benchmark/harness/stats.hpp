//============================================================================
// Vietnamese IME cross-engine benchmark — harness
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File: benchmark/harness/stats.hpp
//----------------------------------------------------------------------------
// Timing, statistics and JSON output. Kept deliberately small and dumb: the
// numbers in the report are computed by scripts/summarize.py from these raw
// per-round samples, so nothing is silently aggregated away.
//============================================================================
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string_view>
#include <string>
#include <vector>

namespace stats {

using Clock = std::chrono::steady_clock;

inline uint64_t nowNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
}

// Median cost of the measurement itself: one now()/now() pair plus the
// accumulate. Subtracted from per-key latency, reported in the JSON.
inline uint64_t calibrateClockPair(int reps = 200000) {
    std::vector<uint64_t> v;
    v.reserve(static_cast<std::size_t>(reps));
    volatile uint64_t sink = 0;
    for (int i = 0; i < reps; ++i) {
        const uint64_t a = nowNs();
        const uint64_t b = nowNs();
        sink += b - a;
        v.push_back(b - a);
    }
    std::sort(v.begin(), v.end());
    (void)sink;
    return v[v.size() / 2];
}

inline uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

// Aggregation of one measurement over one stream, in nanoseconds.
struct Agg {
    uint64_t n = 0;
    double sum = 0.0;
    uint64_t mn = 0;
    uint64_t mx = 0;
    std::vector<uint64_t> samples;   // optional, for percentiles

    void add(uint64_t ns) {
        if (n == 0) { mn = ns; mx = ns; }
        else { if (ns < mn) mn = ns; if (ns > mx) mx = ns; }
        ++n;
        sum += static_cast<double>(ns);
        samples.push_back(ns);
    }
    double mean() const { return n ? sum / static_cast<double>(n) : 0.0; }
    void sortSamples() { std::sort(samples.begin(), samples.end()); }
    uint64_t pct(double p) const {
        if (samples.empty()) { return 0; }
        std::size_t i = static_cast<std::size_t>(p * static_cast<double>(samples.size() - 1));
        if (i >= samples.size()) { i = samples.size() - 1; }
        return samples[i];
    }
};

//---------------------------------------------------------------------------
// One-line JSON writer (JSON Lines artifacts: one flat object per line).
// Simple on purpose — every number a report quotes is visible in a line of
// the artifact file, and summarize.py recomputes all statistics from them.
//---------------------------------------------------------------------------
class LineJson {
public:
    explicit LineJson(std::FILE* f) : f_(f) {}
    void addStr(std::string_view k, const std::string& v) { put(k); std::fprintf(f_, "%s", quote(v).c_str()); }
    void addInt(std::string_view k, uint64_t v) { put(k); std::fprintf(f_, "%llu", static_cast<unsigned long long>(v)); }
    void addI64(std::string_view k, int64_t v) { put(k); std::fprintf(f_, "%lld", static_cast<long long>(v)); }
    void addF(std::string_view k, double v, int digits = 3) { put(k); std::fprintf(f_, "%.*f", digits, v); }
    void addB(std::string_view k, bool v) { put(k); std::fprintf(f_, "%s", v ? "true" : "false"); }
    void end() { std::fprintf(f_, "}\n"); }

private:
    void put(std::string_view k) { std::fputs(first_ ? "{" : ",", f_); first_ = false; std::fprintf(f_, "\"%s\":", std::string(k).c_str()); }
    static std::string quote(const std::string& s) {
        std::string o = "\"";
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
            else if (c == '\n') { o += "\\n"; }
            else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
            else { o += static_cast<char>(c); }
        }
        o += "\"";
        return o;
    }
    std::FILE* f_ = nullptr;
    bool first_ = true;
};

// /proc/self/statm resident bytes (0 if unavailable).
inline uint64_t rssBytes() {
    std::ifstream f("/proc/self/statm");
    if (!f) { return 0; }
    long size = 0, resident = 0;
    f >> size >> resident;
    return static_cast<uint64_t>(resident) * 4096ULL;
}

}  // namespace stats
