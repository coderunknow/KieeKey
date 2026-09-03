//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.1.0 - refactored and completed logic
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
// File: src/core/Profiler.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.0 — src/core/Profiler.hpp
// QPC stage instrumentation for the E2E typing pipeline (Phase 1).
//
// STAGES (per keystroke, nanoseconds from t0 — hook capture):
//   t0  hook capture            (KeyEvent::timestampQpc — already exists)
//   t1  producer decision done  (TextEngine::process + replacementUtf16 + the
//                                suppress/reissue decision)
//   t2  output pushed           (OutputItem in the SPSC ring + pendingEdits
//                                incremented)  — pass-through keys: the
//                                ordering barrier EXIT (delivery delay)
//   t3  consumer dequeued       (OutputItem popped / key drained)
//   t4  batch flush start       (TSF edit session request / SendInput batch)
//   t5  commit returned         (TF_ES_SYNC session returned / SendInput issued)
//
// COST MODEL (the "zero cost when off" contract):
//   * KIEEKEY_PROFILE == 0 (default): every profiling block is wrapped in
//     `if constexpr (ok::prof::kProfileEnabled)` — the compiler eliminates
//     the whole block; the shipped binary contains NO profiling code, not
//     even a branch.
//   * KIEEKEY_PROFILE == 1 (instrumented build, opt-in at configure time):
//     a relaxed atomic load per stamp (~1 ns) gates the runtime switch (env
//     KIEEKEY_PROFILE=1), so an instrumented binary still runs at production
//     speed when profiling is off. Profiling builds are for diagnosis only.
//
// SINKS:
//   Profiling touches two threads (hook/producer, consumer). Each owns a
//   plain (non-atomic) fixed-capacity overwrite-oldest record ring — no
//   locks, no shared cache lines, O(1) push. Records are joined by `seq`
//   (assigned by the producer) at dump/report time. Pass-through keys carry
//   only t1/t2 (+barrierNs) — there is no consumer work for them.
//
// Real-Windows dumps: ok::prof::dumpCsv writes the merged per-stage table
// (post-process recipe in VERIFICATION_PROTOCOL.md). The in-process benches
// compute percentiles directly from the sinks.
//----------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef KIEEKEY_PROFILE
#define KIEEKEY_PROFILE 0
#endif

namespace ok::prof {

inline constexpr bool kProfileEnabled = (KIEEKEY_PROFILE != 0);

//---------------------------------------------------------------------------
// Stage timestamps are NANOS FROM t0 (saturating to kAbsent).
//---------------------------------------------------------------------------
inline constexpr std::uint32_t kAbsent = 0xFFFFFFFFu;

struct StageRecord {
    std::uint64_t seq        = 0;   // producer-assigned, joins hook+consumer
    std::uint32_t vk         = 0;   // debug: virtual key
    std::uint32_t backspace  = 0;   // debug: edit width
    std::uint32_t textLen    = 0;   // debug: replacement length (UTF-16 units)
    std::uint32_t pop        = 0;   // debug: population/workload id (bench)
    std::uint32_t flags      = 0;   // bit i (0..4) => stageNs[i] valid; kIsEdit
    std::uint32_t barrierNs  = 0;   // pass-through: time spent in the ordering
                                    // barrier (the S1 stall), ns
    std::uint32_t stageNs[5] = {kAbsent, kAbsent, kAbsent, kAbsent, kAbsent};
};

inline constexpr std::uint32_t kIsEdit = 0x01;   // suppress/edit key (consumer work)

//---------------------------------------------------------------------------
// Fixed-capacity overwrite-oldest ring (thread-confined — no atomics).
//---------------------------------------------------------------------------
template <std::size_t Cap>
class RecordRing {
public:
    void push(const StageRecord& r) noexcept {
        buf_[head_ % Cap] = r;
        ++head_;
    }
    StageRecord& at(std::uint64_t idx) noexcept { return buf_[idx % Cap]; }
    const StageRecord& at(std::uint64_t idx) const noexcept { return buf_[idx % Cap]; }
    template <typename Fn>
    void forEach(Fn fn) const {
        const std::uint64_t n = head_;
        const std::uint64_t start = (n > Cap) ? (n - Cap) : 0;
        for (std::uint64_t i = start; i < n; ++i) { fn(buf_[i % Cap]); }
    }
    void clear() noexcept { head_ = 0; }
    [[nodiscard]] std::uint64_t count() const noexcept { return head_; }

private:
    StageRecord buf_[Cap]{};
    std::uint64_t head_ = 0;
};

#if KIEEKEY_PROFILE

//---------------------------------------------------------------------------
// Runtime gate (instrumented builds): env KIEEKEY_PROFILE=1 enables stamps.
//---------------------------------------------------------------------------
inline std::atomic<bool> g_gateInit{false};
inline std::atomic<bool> g_enabled{false};
inline bool enabled() noexcept {
    if (!g_gateInit.load(std::memory_order_relaxed)) {
        const char* e = std::getenv("KIEEKEY_PROFILE");
        g_enabled.store(e != nullptr && e[0] == '1' && e[1] == '\0',
                        std::memory_order_relaxed);
        g_gateInit.store(true, std::memory_order_relaxed);
    }
    return g_enabled.load(std::memory_order_relaxed);
}

//---------------------------------------------------------------------------
// QPC-backed ns clock (real Windows — the same source as the hook's t0) /
// steady_clock (shim & POSIX hosts).
//---------------------------------------------------------------------------
#if defined(_WIN32) && !defined(OK_WRAP_NO_WIN32)
inline std::int64_t qpcNsPerTick() noexcept {
    LARGE_INTEGER f{};
    ::QueryPerformanceFrequency(&f);
    return static_cast<std::int64_t>(1000000000LL / f.QuadPart);
}
inline std::uint64_t qpcNow() noexcept {
    LARGE_INTEGER c{};
    ::QueryPerformanceCounter(&c);
    return static_cast<std::uint64_t>(c.QuadPart);
}
inline std::uint32_t nsSince(std::uint64_t t0) noexcept {
    static const std::int64_t nsPerTick = qpcNsPerTick();
    const std::uint64_t d = (qpcNow() - t0) * static_cast<std::uint64_t>(nsPerTick);
    return (d > 0xFFFFFFF0ull) ? kAbsent : static_cast<std::uint32_t>(d);
}
#else
#include <chrono>
inline std::uint64_t qpcNow() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}
inline std::uint32_t nsSince(std::uint64_t t0) noexcept {
    const std::uint64_t d = qpcNow() - t0;
    return (d > 0xFFFFFFF0ull) ? kAbsent : static_cast<std::uint32_t>(d);
}
#endif

// ns between two already-captured stamps (t1 was taken at/before t2).
inline std::uint32_t nsBetween(std::uint64_t t1, std::uint64_t t2) noexcept {
    const std::uint64_t d = t2 - t1;
    return (d > 0xFFFFFFF0ull) ? kAbsent : static_cast<std::uint32_t>(d);
}

using HookSink     = RecordRing<16384>;
using ConsumerSink = RecordRing<16384>;

//---------------------------------------------------------------------------
// CSV dump (real-Windows recipe).
//---------------------------------------------------------------------------
inline bool dumpCsv(const char* path, const HookSink& h, const ConsumerSink& c) {
    std::FILE* f = std::fopen(path, "w");
    if (!f) { return false; }
    std::fprintf(f, "seq,vk,pop,is_edit,backspace,textLen,barrierNs,"
                    "t1_ns,t2_ns,t3_ns,t4_ns,t5_ns\n");
    auto emit = [f](const StageRecord& r) {
        std::fprintf(f, "%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
            static_cast<unsigned long long>(r.seq), r.vk, r.pop,
            (r.flags & kIsEdit) ? 1u : 0u, r.backspace, r.textLen, r.barrierNs,
            (r.flags & (1u << 0)) ? r.stageNs[0] : kAbsent,
            (r.flags & (1u << 1)) ? r.stageNs[1] : kAbsent,
            (r.flags & (1u << 2)) ? r.stageNs[2] : kAbsent,
            (r.flags & (1u << 3)) ? r.stageNs[3] : kAbsent,
            (r.flags & (1u << 4)) ? r.stageNs[4] : kAbsent);
    };
    c.forEach([&emit](const StageRecord& r) { emit(r); });
    h.forEach([&emit](const StageRecord& r) {
        if (!(r.flags & kIsEdit)) { emit(r); }   // pass-through: hook-side only
    });
    std::fclose(f);
    return true;
}

#endif // KIEEKEY_PROFILE

} // namespace ok::prof
