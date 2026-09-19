//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey - refactored and completed logic
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
// File: src/core/Diagnostics.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — Diagnostics.hpp (v1.3.0-beta3)
// Runtime, user-selectable diagnostics for the WHOLE input pipeline.
//
// WHY THIS EXISTS
//   Before beta3 the "Chẩn đoán" tab showed six numbers that were either
//   process-lifetime watermarks (a single 400 µs outlier at startup dominated
//   the "peak" forever) or counters that did not mean what their label said
//   ("Sự kiện bàn phím đã xử lý" was fed by mouse clicks and foreground
//   changes too, so it climbed while the user was not typing). A user who
//   reported "gõ bị trễ" had nothing to send back, and a maintainer had
//   nothing to look at. This module is the answer: per-stage latency
//   histograms, per-source event counters, a bounded event trace, an optional
//   rolling file log, an injectable delay for reproducing a slow machine, and
//   one "export report" text dump that carries all of it.
//
// COST MODEL (the contract the hot path relies on)
//   * Level::Off   — every recorder is a single relaxed load + branch. No
//                    histogram update, no event record, no file I/O, no
//                    allocation. This is the shipped default for the tracing
//                    parts; the BASIC counters/histograms below are the only
//                    thing still running, and they are relaxed atomics.
//   * Level::Basic — counters + stage histograms (a few relaxed RMWs per key).
//   * Level::Full  — Basic + the event trace ring + the file log + injected
//                    delay support.
//   Nothing here allocates on the keystroke path; the file log is written by
//   the UI thread draining the trace ring, never by the hook thread.
//
// PORTABILITY
//   Pure standard C++ (no windows.h) so the histograms, the counter set, the
//   trace ring, the log rotation and the report text are unit-tested on every
//   platform (tests/test_diagnostics.cpp) instead of only being reviewable.
//----------------------------------------------------------------------------
#pragma once

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace ok::diag {

//---------------------------------------------------------------------------
// Levels — the user-facing "Tắt / Cơ bản / Đầy đủ" switch.
//---------------------------------------------------------------------------
enum class Level : std::uint8_t { Off = 0, Basic = 1, Full = 2 };

[[nodiscard]] constexpr const char* levelName(Level level) noexcept {
    switch (level) {
        case Level::Off:   return "Off";
        case Level::Basic: return "Basic";
        case Level::Full:  return "Full";
    }
    return "Off";
}

[[nodiscard]] constexpr Level levelFromInt(int value) noexcept {
    if (value <= 0) { return Level::Off; }
    if (value == 1) { return Level::Basic; }
    return Level::Full;
}

//---------------------------------------------------------------------------
// Pipeline stages. Names match the t0..t5 model of Profiler.hpp so a report
// can be read next to an instrumented-build dump without translation.
//---------------------------------------------------------------------------
enum class Stage : std::uint8_t {
    HookToDecision = 0,   // LL callback entry -> producer decision done (t0->t1)
    DecisionToQueue,      // decision -> OutputItem visible in the ring (t1->t2)
    QueueToConsumer,      // ring push -> consumer dequeue (t2->t3)
    ConsumerToCommit,     // dequeue -> TSF session / SendInput returned (t3->t5)
    TotalEdit,            // t0 -> commit, for keys that produced an edit
    TotalPassThrough,     // t0 -> delivery, for keys the app receives as-is
    BarrierWait,          // ordering-barrier stall before a pass-through key
    TsfCommit,            // RequestEditSession(TF_ES_SYNC) duration
    SendInputCall,        // SendInput duration
    EngineDecision,       // TextEngine::process() alone
    LayoutRefresh,        // refreshLayoutCache() (ToUnicode/layout reads)
    ForegroundSwitch,     // EVENT_SYSTEM_FOREGROUND handling (monitor refresh)
    kCount
};

[[nodiscard]] constexpr const char* stageName(Stage stage) noexcept {
    switch (stage) {
        case Stage::HookToDecision:   return "hook->decision";
        case Stage::DecisionToQueue:  return "decision->queue";
        case Stage::QueueToConsumer:  return "queue->consumer";
        case Stage::ConsumerToCommit: return "consumer->commit";
        case Stage::TotalEdit:        return "total (edit key)";
        case Stage::TotalPassThrough: return "total (pass-through)";
        case Stage::BarrierWait:      return "ordering barrier";
        case Stage::TsfCommit:        return "TSF commit";
        case Stage::SendInputCall:    return "SendInput";
        case Stage::EngineDecision:   return "engine decision";
        case Stage::LayoutRefresh:    return "layout refresh";
        case Stage::ForegroundSwitch: return "foreground switch";
        case Stage::kCount: break;
    }
    return "?";
}

//---------------------------------------------------------------------------
// Counters. Every counter has ONE writer class and a label that says exactly
// what it counts — the beta2 "keyboard events" counter counted three
// different event sources, which is the bug this enumeration makes
// impossible: keyboard, mouse and foreground events are separate ids.
//---------------------------------------------------------------------------
enum class Counter : std::uint16_t {
    // --- keyboard (the ONLY ids that may be labelled "sự kiện bàn phím") ---
    KeyDown = 0,
    KeyUp,
    KeySuppressed,          // the app never saw this key (engine consumed it)
    KeyPassThrough,         // the app received this key untouched
    KeySelfInjected,        // our own SendInput, skipped by the hook
    KeyIgnoredInjected,     // third-party injected input, not composed
    KeyIgnoredModifier,
    // --- non-keyboard sources (never counted as keyboard events) ---
    MouseButton,
    MouseWheel,
    ForegroundChanged,
    // --- pipeline ---
    QueuedToConsumer,       // KeyEvent pushed into the SPSC ring
    QueueOverflowDropped,
    ConsumerWakes,
    SetEventSyscalls,
    EngineEdits,            // decisions that produced a replacement
    MacroExpansions,
    LiveEffectRewrites,     // characters visually restyled by live effects
    TsfCommits,
    TsfSlowCommits,
    TsfFailedCommits,
    SendInputCalls,
    SendInputFailedEvents,
    BarrierTimeouts,
    HookReinstalls,
    ProducerExceptions,
    ConsumerExceptions,
    DigitGuardHits,
    ShortcutGuardHits,
    Resyncs,
    // --- own-window policy (the "cannot type Vietnamese in our own dialog"
    //     bug is observable from here) ---
    OwnWindowBypassed,      // keyboard input ignored because our UI had focus
    OwnTextFieldComposed,   // … but a text field had focus, so the IME ran
    // --- diagnostics machinery ---
    TraceRecordsDropped,
    LogLinesWritten,
    LogRotateEvents,
    kCount
};

[[nodiscard]] const char* counterName(Counter id) noexcept;
[[nodiscard]] const char* counterNameVi(Counter id) noexcept;

//---------------------------------------------------------------------------
// Log2 latency histogram, microsecond resolution, fixed buckets.
//
// Bucket b covers [2^(b-1), 2^b) µs; bucket 0 is "under 1 µs" and the last
// bucket is the saturating overflow. 16 buckets therefore span <1 µs … ≥32 ms
// with a resolution that is finer than the human perception threshold at the
// low end (where it matters) and coarser at the high end (where a keystroke
// is already visibly broken). Percentiles are reported at bucket granularity
// and the report SAYS so — no false precision.
//---------------------------------------------------------------------------
class Histogram {
public:
    static constexpr std::size_t kBuckets = 16;

    void recordUs(std::int64_t us) noexcept;
    [[nodiscard]] std::uint64_t bucket(std::size_t index) const noexcept;
    [[nodiscard]] std::uint64_t total() const noexcept;
    [[nodiscard]] std::int64_t maxUs() const noexcept { return maxUs_.load(std::memory_order_relaxed); }
    // Mean over bucket midpoints (documented approximation, ±half a bucket).
    [[nodiscard]] double meanUs() const noexcept;
    // p in [0,1]. Returns the UPPER edge of the bucket containing the
    // percentile; -1 when empty.
    [[nodiscard]] std::int64_t percentileUs(double p) const noexcept;
    void reset() noexcept;
    // "  <1us: 12 | 1-2: 340 | 2-4: 88 | …" — portable, allocation-light.
    [[nodiscard]] std::string toText() const;
    [[nodiscard]] static std::int64_t bucketLowUs(std::size_t index) noexcept;
    [[nodiscard]] static std::int64_t bucketHighUs(std::size_t index) noexcept;

private:
    std::array<std::atomic<std::uint64_t>, kBuckets> buckets_{};
    std::atomic<std::int64_t> maxUs_{0};
};

//---------------------------------------------------------------------------
// Bounded keystroke trace (Level::Full). One writer at a time in practice
// (hook thread for producer-side records, consumer thread for commit-side
// records); the mutex makes the two-writer case well-defined and is only ever
// taken while Full is on, off the shipped hot path.
//---------------------------------------------------------------------------
struct EventRecord {
    std::uint64_t tickMs   = 0;   // wall clock (GetTickCount64 / steady clock)
    std::uint64_t seq      = 0;   // monotonic per-process record id
    std::int64_t  stageUs  = 0;   // measured latency for `stage`
    std::uint32_t vk       = 0;
    std::uint32_t chars    = 0;   // emitted UTF-16 units (edit) or 0
    std::uint32_t backspace= 0;
    std::uint16_t stage    = 0;   // Stage
    std::uint8_t  action   = 0;   // 0 down, 1 up, 2 sysdown, 3 sysup
    std::uint8_t  source   = 0;   // 0 keyboard, 1 mouse, 2 foreground
    std::uint8_t  decision = 0;   // 0 pass, 1 suppress+edit, 2 ignored, 3 inline
    std::uint8_t  flags    = 0;   // bit0 self-injected, bit1 excluded app,
                                  // bit2 live effect, bit3 macro, bit4 own-window
};

class EventTrace {
public:
    explicit EventTrace(std::size_t capacity = 512) noexcept;

    // Returns false when the trace is full and the OLDEST record was
    // overwritten (the caller counts that as TraceRecordsDropped only when
    // the ring is in "drop newest" mode; this ring overwrites oldest, so the
    // return value means "the trace wrapped").
    bool push(const EventRecord& record) noexcept;
    // Most recent `cap` records, oldest first. Returns how many were copied.
    [[nodiscard]] std::size_t snapshot(EventRecord* out, std::size_t cap) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept { return records_.size(); }
    [[nodiscard]] std::uint64_t wrapped() const noexcept;
    // Removes up to `cap` oldest records, copying them out (the file-log
    // drainer). Returns how many were copied.
    std::size_t drain(EventRecord* out, std::size_t cap) noexcept;
    void clear() noexcept;

private:
    mutable std::mutex mutex_;
    std::vector<EventRecord> records_;
    std::size_t head_ = 0;    // next write slot
    std::size_t count_ = 0;   // valid records (<= capacity)
    std::uint64_t seq_ = 0;
    std::uint64_t wrapped_ = 0;
};

//---------------------------------------------------------------------------
// Rolling file log. UTF-8 lines, size-capped with one rotation (a .1 backup),
// flushed per batch by the UI thread — never from the hook thread.
//---------------------------------------------------------------------------
class FileLog {
public:
    FileLog() noexcept = default;
    FileLog(const FileLog&) = delete;
    FileLog& operator=(const FileLog&) = delete;
    ~FileLog();

    // Opens (append) `pathUtf8`; an empty path closes the log. `maxBytes`
    // caps the file before rotation (0 = the 4 MiB default).
    bool open(const std::string& pathUtf8, std::uint64_t maxBytes = 0) noexcept;
    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    // Thread-safe. Returns false when the write failed (the log then closes
    // itself so a full disk cannot be retried per keystroke).
    bool write(const std::string& utf8Line) noexcept;
    bool writef(const char* fmt, ...) noexcept
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;
    [[nodiscard]] std::uint64_t bytesWritten() const noexcept { return bytesWritten_; }
    [[nodiscard]] std::uint64_t rotations() const noexcept { return rotations_; }

private:
    bool writeLocked(const char* data, std::size_t len) noexcept;
    bool rotateLocked() noexcept;

    mutable std::mutex mutex_;
    std::FILE* file_ = nullptr;
    std::string path_;
    std::uint64_t maxBytes_ = 4ull * 1024 * 1024;
    std::uint64_t bytesWritten_ = 0;
    std::uint64_t rotations_ = 0;
};

//---------------------------------------------------------------------------
// Machine/process context the report carries. Filled by the app (the values
// are Windows-only), read by the portable report writer.
//---------------------------------------------------------------------------
struct SystemSnapshot {
    std::string osName;         // e.g. "Windows 11 Pro 23H2 (build 22631)"
    std::string arch;           // "x64" / "ARM64" / "ARM64EC"
    std::string appVersion;     // "1.3.0-beta3 (PE 1.3.0.4)"
    std::string foregroundApp;  // "chrome.exe — đang gõ"
    std::string keyboardLayout; // "00000409 (US)"
    std::string outputMode;     // "Auto (TSF)" / "SendInput"
    std::string inputMethod;    // "Telex"
    std::string codeTable;      // "Unicode"
    std::uint64_t workingSetKb = 0;
    std::uint64_t peakWorkingSetKb = 0;
    std::uint64_t kernelTimeMs = 0;
    std::uint64_t userTimeMs = 0;
    std::uint64_t uptimeMs = 0;
    double cpuPercentSinceStart = 0.0;
    std::uint32_t dpi = 96;
    bool imeEnabled = true;
    bool hookInstalled = false;
    bool fgHookInstalled = false;
    bool liveEffectsEnabled = false;
    bool excludedApp = false;
};

//---------------------------------------------------------------------------
// The process-wide diagnostics service.
//---------------------------------------------------------------------------
class Diagnostics {
public:
    static Diagnostics& instance() noexcept;

    Diagnostics(const Diagnostics&) = delete;
    Diagnostics& operator=(const Diagnostics&) = delete;

    // --- level ------------------------------------------------------------
    [[nodiscard]] Level level() const noexcept {
        return static_cast<Level>(level_.load(std::memory_order_relaxed));
    }
    void setLevel(Level level) noexcept;
    // Cheap gate for the hot path: one relaxed load + compare.
    [[nodiscard]] bool atLeast(Level level) const noexcept {
        return static_cast<std::uint8_t>(level_.load(std::memory_order_relaxed)) >=
               static_cast<std::uint8_t>(level);
    }

    // --- counters ---------------------------------------------------------
    void add(Counter id, std::uint64_t delta = 1) noexcept;
    [[nodiscard]] std::uint64_t get(Counter id) const noexcept;
    // Convenience: the number of KEYBOARD events only (the label the beta2 UI
    // got wrong). KeyDown + KeyUp.
    [[nodiscard]] std::uint64_t keyboardEvents() const noexcept;

    // --- latency ----------------------------------------------------------
    // No-op below `minLevel` (Basic for histograms, Full for the trace).
    void record(Stage stage, std::int64_t microseconds) noexcept;
    [[nodiscard]] const Histogram& histogram(Stage stage) const noexcept;
    [[nodiscard]] Histogram& histogram(Stage stage) noexcept;

    // --- trace ------------------------------------------------------------
    void trace(const EventRecord& record) noexcept;   // Full only
    [[nodiscard]] EventTrace& events() noexcept { return trace_; }

    // --- injected delay (Full only) ---------------------------------------
    // Reproduces "my machine is slow" without needing a slow machine: the
    // hook thread busy-waits `hookUs` after the decision and the consumer
    // waits `consumerUs` before committing. 0 = disabled (the default).
    void setInjectedDelayUs(std::uint32_t hookUs, std::uint32_t consumerUs) noexcept;
    [[nodiscard]] std::uint32_t injectedHookDelayUs() const noexcept {
        return injectedHookUs_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t injectedConsumerDelayUs() const noexcept {
        return injectedConsumerUs_.load(std::memory_order_relaxed);
    }
    // Busy-wait (not Sleep): a 50 µs Sleep is a 15 ms scheduler quantum on a
    // default-timer Windows box, which would measure the timer, not the code.
    void applyHookDelay() const noexcept;
    void applyConsumerDelay() const noexcept;

    // --- file log ---------------------------------------------------------
    [[nodiscard]] FileLog& fileLog() noexcept { return fileLog_; }
    bool setLogFile(const std::string& pathUtf8) noexcept;
    // Writes the trace records that are still in the ring into the file log
    // (UI-thread call, e.g. every 500 ms). Returns the number of lines.
    std::size_t flushTraceToFileLog() noexcept;

    // --- report -----------------------------------------------------------
    void setSystemSnapshot(const SystemSnapshot& snapshot) noexcept;
    [[nodiscard]] SystemSnapshot systemSnapshot() const noexcept;
    // Full text report (UTF-8): header, system, counters grouped by meaning,
    // per-stage histograms with bucket edges, the last N traced events and the
    // health verdict. This is what "Xuất báo cáo" writes.
    [[nodiscard]] std::string report(std::size_t traceLines = 40) const;
    // One-line health verdict used by the UI header ("OK", "…").
    [[nodiscard]] std::string verdict() const;

    void resetAll() noexcept;

    // Monotonic-ish clock for the trace (ms). Portable: steady_clock.
    [[nodiscard]] static std::uint64_t nowMs() noexcept;
    // Portable monotonic microsecond clock (the app passes QPC deltas on
    // Windows; benches and tests use this).
    [[nodiscard]] static std::int64_t nowUs() noexcept;

private:
    Diagnostics() noexcept;
    ~Diagnostics() = default;

    std::atomic<std::uint8_t> level_{static_cast<std::uint8_t>(Level::Basic)};
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(Counter::kCount)> counters_{};
    std::array<Histogram, static_cast<std::size_t>(Stage::kCount)> stages_{};
    std::atomic<std::uint32_t> injectedHookUs_{0};
    std::atomic<std::uint32_t> injectedConsumerUs_{0};

    mutable std::mutex snapshotMutex_;
    SystemSnapshot snapshot_;

    EventTrace trace_;
    FileLog fileLog_;
};

// Format one trace record as a log/report line (shared by the file log and
// the report so the two never disagree about the columns).
[[nodiscard]] std::string formatEventRecord(const EventRecord& record);

} // namespace ok::diag
