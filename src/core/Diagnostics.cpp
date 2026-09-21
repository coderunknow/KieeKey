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
// File: src/core/Diagnostics.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "Diagnostics.hpp"

#include "LiveEffects.hpp"   // GateBlocker — the emit-chain gate token names

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace ok::diag {
namespace {

// Index of the log2 bucket a microsecond value falls into.
std::size_t bucketIndexFor(std::int64_t us) noexcept {
    if (us <= 0) { return 0; }
    std::size_t index = 1;
    std::int64_t edge = 2;                 // bucket 1 == [1,2)
    while (index + 1 < Histogram::kBuckets && us >= edge) {
        edge <<= 1;
        ++index;
    }
    return index;
}

std::uint64_t pow2(std::size_t exponent) noexcept {
    return (exponent >= 63) ? (std::uint64_t{1} << 62) : (std::uint64_t{1} << exponent);
}

} // namespace

//---------------------------------------------------------------------------
// Counter labels. Two tables: the machine-facing English id (log/report keys,
// stable across releases) and the Vietnamese label the UI shows.
//---------------------------------------------------------------------------
const char* counterName(Counter id) noexcept {
    switch (id) {
        case Counter::KeyDown:              return "key.down";
        case Counter::KeyUp:                return "key.up";
        case Counter::KeySuppressed:        return "key.suppressed";
        case Counter::KeyPassThrough:       return "key.pass_through";
        case Counter::KeySelfInjected:      return "key.self_injected";
        case Counter::KeyIgnoredInjected:   return "key.ignored_injected";
        case Counter::KeyIgnoredModifier:   return "key.ignored_modifier";
        case Counter::MouseButton:          return "mouse.button";
        case Counter::MouseWheel:           return "mouse.wheel";
        case Counter::ForegroundChanged:    return "foreground.changed";
        case Counter::QueuedToConsumer:     return "queue.pushed";
        case Counter::QueueOverflowDropped: return "queue.dropped";
        case Counter::ConsumerWakes:        return "consumer.wakes";
        case Counter::SetEventSyscalls:     return "consumer.set_event";
        case Counter::EngineEdits:          return "engine.edits";
        case Counter::MacroExpansions:      return "engine.macro_expansions";
        case Counter::LiveEffectRewrites:   return "effects.rewritten_chars";
        case Counter::TsfCommits:           return "tsf.commits";
        case Counter::TsfSlowCommits:       return "tsf.slow_commits";
        case Counter::TsfFailedCommits:     return "tsf.failed_commits";
        case Counter::SendInputCalls:       return "sendinput.calls";
        case Counter::SendInputFailedEvents:return "sendinput.failed_events";
        case Counter::BarrierTimeouts:      return "barrier.timeouts";
        case Counter::HookReinstalls:       return "hook.reinstalls";
        case Counter::ProducerExceptions:   return "fault.producer_exceptions";
        case Counter::ConsumerExceptions:   return "fault.consumer_exceptions";
        case Counter::DigitGuardHits:       return "guard.digit_hits";
        case Counter::ShortcutGuardHits:    return "guard.shortcut_hits";
        case Counter::Resyncs:              return "engine.resyncs";
        case Counter::OwnWindowBypassed:    return "policy.own_window_bypassed";
        case Counter::OwnTextFieldComposed: return "policy.own_textfield_composed";
        case Counter::TraceRecordsDropped:  return "diag.trace_wrapped";
        case Counter::LogLinesWritten:      return "diag.log_lines";
        case Counter::LogRotateEvents:      return "diag.log_rotations";
        case Counter::kCount: break;
    }
    return "?";
}

const char* counterNameVi(Counter id) noexcept {
    switch (id) {
        case Counter::KeyDown:              return "Phím bấm xuống";
        case Counter::KeyUp:                return "Phím nhả ra";
        case Counter::KeySuppressed:        return "Phím bộ gõ nuốt (tự phát chữ)";
        case Counter::KeyPassThrough:       return "Phím đi thẳng vào ứng dụng";
        case Counter::KeySelfInjected:      return "Phím do KieeKey tự bơm (bỏ qua)";
        case Counter::KeyIgnoredInjected:   return "Phím do app khác bơm (bỏ qua)";
        case Counter::KeyIgnoredModifier:   return "Phím modifier (Ctrl/Alt/Shift…)";
        case Counter::MouseButton:          return "Sự kiện nút chuột";
        case Counter::MouseWheel:           return "Sự kiện lăn chuột";
        case Counter::ForegroundChanged:    return "Sự kiện đổi cửa sổ";
        case Counter::QueuedToConsumer:     return "Sự kiện đưa vào hàng đợi";
        case Counter::QueueOverflowDropped: return "Sự kiện bị bỏ (hàng đợi đầy)";
        case Counter::ConsumerWakes:        return "Số lần luồng tiêu thụ thức dậy";
        case Counter::SetEventSyscalls:     return "Số lệnh SetEvent (syscall)";
        case Counter::EngineEdits:          return "Số lần engine sinh chữ thay thế";
        case Counter::MacroExpansions:      return "Số lần mở rộng gõ tắt";
        case Counter::LiveEffectRewrites:   return "Số ký tự bị đổi bởi hiệu ứng ngoài";
        case Counter::TsfCommits:           return "Số lần commit TSF";
        case Counter::TsfSlowCommits:       return "Commit TSF chậm (≥100 ms)";
        case Counter::TsfFailedCommits:     return "Commit TSF thất bại";
        case Counter::SendInputCalls:       return "Số lệnh SendInput";
        case Counter::SendInputFailedEvents:return "Sự kiện SendInput bị từ chối";
        case Counter::BarrierTimeouts:      return "Chờ hàng đợi quá hạn (barrier)";
        case Counter::HookReinstalls:       return "Lần tự phục hồi hook";
        case Counter::ProducerExceptions:   return "Lỗi ngoại lệ phía hook";
        case Counter::ConsumerExceptions:   return "Lỗi ngoại lệ phía tiêu thụ";
        case Counter::DigitGuardHits:       return "Lưới an toàn chữ số đã chặn";
        case Counter::ShortcutGuardHits:    return "Lưới an toàn phím tắt đã chặn";
        case Counter::Resyncs:              return "Số lần đồng bộ lại trạng thái gõ";
        case Counter::OwnWindowBypassed:    return "Phím bỏ qua vì focus là UI KieeKey";
        case Counter::OwnTextFieldComposed: return "Phím vẫn gõ tiếng Việt trong ô chữ KieeKey";
        case Counter::TraceRecordsDropped:  return "Bản ghi trace bị ghi đè";
        case Counter::LogLinesWritten:      return "Dòng đã ghi vào file log";
        case Counter::LogRotateEvents:      return "Số lần xoay vòng file log";
        case Counter::kCount: break;
    }
    return "?";
}

//---------------------------------------------------------------------------
// Histogram
//---------------------------------------------------------------------------
std::int64_t Histogram::bucketLowUs(std::size_t index) noexcept {
    if (index == 0) { return 0; }
    if (index >= kBuckets) { index = kBuckets - 1; }
    return static_cast<std::int64_t>(pow2(index - 1));
}

std::int64_t Histogram::bucketHighUs(std::size_t index) noexcept {
    if (index == 0) { return 1; }
    if (index >= kBuckets - 1) { return bucketLowUs(kBuckets - 1); }   // saturating tail
    return static_cast<std::int64_t>(pow2(index));
}

void Histogram::recordUs(std::int64_t us) noexcept {
    const std::size_t index = bucketIndexFor(us);
    buckets_[index].fetch_add(1, std::memory_order_relaxed);
    std::int64_t max = maxUs_.load(std::memory_order_relaxed);
    while (us > max && !maxUs_.compare_exchange_weak(max, us, std::memory_order_relaxed)) {}
}

std::uint64_t Histogram::bucket(std::size_t index) const noexcept {
    if (index >= kBuckets) { return 0; }
    return buckets_[index].load(std::memory_order_relaxed);
}

std::uint64_t Histogram::total() const noexcept {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < kBuckets; ++i) { sum += bucket(i); }
    return sum;
}

double Histogram::meanUs() const noexcept {
    const std::uint64_t n = total();
    if (n == 0) { return 0.0; }
    double acc = 0.0;
    for (std::size_t i = 0; i < kBuckets; ++i) {
        const std::uint64_t c = bucket(i);
        if (c == 0) { continue; }
        const double low = static_cast<double>(bucketLowUs(i));
        // Midpoint of [low, 2*low); bucket 0 is [0,1) -> 0.5; the saturating
        // tail uses its low edge (a documented UNDER-estimate: an unbounded
        // bucket has no honest midpoint).
        const double mid = (i == 0) ? 0.5 : (low + 0.5 * low);
        acc += mid * static_cast<double>(c);
    }
    return acc / static_cast<double>(n);
}

std::int64_t Histogram::percentileUs(double p) const noexcept {
    const std::uint64_t n = total();
    if (n == 0) { return -1; }
    if (p < 0.0) { p = 0.0; }
    if (p > 1.0) { p = 1.0; }
    const double target = p * static_cast<double>(n);
    std::uint64_t cumulative = 0;
    for (std::size_t i = 0; i < kBuckets; ++i) {
        cumulative += bucket(i);
        if (static_cast<double>(cumulative) >= target) {
            return (i == kBuckets - 1) ? bucketLowUs(i) : bucketHighUs(i);
        }
    }
    return bucketHighUs(kBuckets - 1);
}

void Histogram::reset() noexcept {
    for (auto& b : buckets_) { b.store(0, std::memory_order_relaxed); }
    maxUs_.store(0, std::memory_order_relaxed);
}

std::string Histogram::toText() const {
    std::string out;
    char buffer[64];
    for (std::size_t i = 0; i < kBuckets; ++i) {
        const std::uint64_t c = bucket(i);
        if (c == 0) { continue; }
        int written = 0;
        if (i == 0) {
            written = std::snprintf(buffer, sizeof(buffer), "<1us=%llu",
                                    static_cast<unsigned long long>(c));
        } else if (i == kBuckets - 1) {
            written = std::snprintf(buffer, sizeof(buffer), ">=%lldus=%llu",
                                    static_cast<long long>(bucketLowUs(i)),
                                    static_cast<unsigned long long>(c));
        } else {
            written = std::snprintf(buffer, sizeof(buffer), "[%lld-%lld)us=%llu",
                                    static_cast<long long>(bucketLowUs(i)),
                                    static_cast<long long>(bucketHighUs(i)),
                                    static_cast<unsigned long long>(c));
        }
        if (written <= 0) { continue; }
        if (!out.empty()) { out += ' '; }
        out += buffer;
    }
    return out;
}

//---------------------------------------------------------------------------
// EventTrace
//---------------------------------------------------------------------------
EventTrace::EventTrace(std::size_t capacity) noexcept {
    if (capacity == 0) { capacity = 1; }
    records_.resize(capacity);
}

bool EventTrace::push(const EventRecord& record) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    EventRecord stored = record;
    stored.seq = ++seq_;
    records_[head_] = stored;
    head_ = (head_ + 1) % records_.size();
    if (count_ < records_.size()) {
        ++count_;
        return false;
    }
    ++wrapped_;
    return true;
}

std::size_t EventTrace::snapshot(EventRecord* out, std::size_t cap) const noexcept {
    if (out == nullptr || cap == 0) { return 0; }
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t take = std::min(cap, count_);
    // Oldest of the taken window first: start `take` slots behind the head.
    std::size_t index = (head_ + records_.size() - take) % records_.size();
    for (std::size_t i = 0; i < take; ++i) {
        out[i] = records_[index];
        index = (index + 1) % records_.size();
    }
    return take;
}

std::size_t EventTrace::drain(EventRecord* out, std::size_t cap) noexcept {
    if (out == nullptr || cap == 0) { return 0; }
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t take = std::min(cap, count_);
    // The oldest record sits `count_` slots behind the head.
    std::size_t index = (head_ + records_.size() - count_) % records_.size();
    for (std::size_t i = 0; i < take; ++i) {
        out[i] = records_[index];
        index = (index + 1) % records_.size();
    }
    count_ -= take;
    return take;
}

std::size_t EventTrace::size() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

std::uint64_t EventTrace::wrapped() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return wrapped_;
}

void EventTrace::clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    head_ = 0;
    count_ = 0;
    wrapped_ = 0;
}

//---------------------------------------------------------------------------
// EmitTrace — v1.3.0-beta6 (V4): last-N output deliveries (tester evidence)
//---------------------------------------------------------------------------
EmitTrace::EmitTrace(std::size_t capacity) noexcept {
    if (capacity == 0) { capacity = 1; }
    records_.resize(capacity);
}

bool EmitTrace::push(EmitRecord record) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    record.seq = ++seq_;
    if (record.tickMs == 0) { record.tickMs = Diagnostics::nowMs(); }
    records_[head_] = std::move(record);
    head_ = (head_ + 1) % records_.size();
    if (count_ < records_.size()) {
        ++count_;
        return false;
    }
    return true;
}

std::vector<EmitRecord> EmitTrace::snapshot(std::size_t cap) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t take = std::min(cap, count_);
    std::vector<EmitRecord> out;
    out.reserve(take);
    std::size_t index = (head_ + records_.size() - take) % records_.size();
    for (std::size_t i = 0; i < take; ++i) {
        out.push_back(records_[index]);
        index = (index + 1) % records_.size();
    }
    return out;
}

std::size_t EmitTrace::size() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

void EmitTrace::clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    head_ = 0;
    count_ = 0;
}

namespace {

// The gate verdict token must never drift from the enum order, so it is
// derived from the enumeration itself (LiveEffects.hpp).
const char* gateToken(std::uint8_t gate) noexcept {
    using ok::effects::GateBlocker;
    switch (static_cast<GateBlocker>(gate)) {
        case GateBlocker::None:           return "none";
        case GateBlocker::ImeDisabled:    return "ime-disabled";
        case GateBlocker::AppExcluded:    return "app-excluded";
        case GateBlocker::MasterOff:      return "master-off";
        case GateBlocker::NonUnicodeTable: return "non-unicode-table";
    }
    return "unknown";
}

// Quotes a value for the machine-readable lines; interior quotes are doubled
// (the lines are greppable, not JSON).
void appendQuoted(std::string& out, const std::string& value) {
    out += '"';
    for (const char c : value) {
        out += c;
        if (c == '"') { out += '"'; }
    }
    out += '"';
}

} // namespace

std::string formatEmitRecord(const EmitRecord& record) {
    std::string line = "emit-chain seq=";
    line += std::to_string(record.seq);
    line += " t=";
    line += std::to_string(record.tickMs);
    line += "ms channel=";
    line += emitChannelName(record.channel);
    line += " gate=";
    line += gateToken(record.gate);
    line += " pid=";
    line += std::to_string(record.pid);
    line += " process=";
    appendQuoted(line, record.processName);
    line += " class=";
    appendQuoted(line, record.windowClass);
    line += " chars=";
    line += std::to_string(record.chars);
    line += "\n";
    return line;
}

std::string formatProcessResolution(const ProcessResolution& r) {
    std::string line = "process-resolution pid=";
    line += std::to_string(r.pid);
    line += " name=";
    appendQuoted(line, r.name);
    line += " api=";
    line += resolveApiName(static_cast<ResolveApi>(r.api));
    line += " error=";
    line += std::to_string(r.error);
    line += " elevated=";
    line += r.elevated ? "on" : "off";
    line += "\n";
    return line;
}

std::string formatDisplayMetrics(const DisplayMetrics& m) {
    std::string line = "display-metrics dpi=";
    line += std::to_string(m.dpi);
    line += " systemDpi=";
    line += std::to_string(m.systemDpi);
    line += " font=";
    appendQuoted(line, m.fontFace);
    line += " fontHeightPx=";
    line += std::to_string(m.fontHeightPx);
    line += " dwm=";
    line += m.dwmComposition ? "on" : "off";
    line += " perMonitorAware=";
    line += m.perMonitorAware ? "on" : "off";
    line += " screen=";
    line += std::to_string(m.screenWidth);
    line += "x";
    line += std::to_string(m.screenHeight);
    line += "\n";
    return line;
}

//---------------------------------------------------------------------------
// FileLog
//---------------------------------------------------------------------------
FileLog::~FileLog() { close(); }

bool FileLog::open(const std::string& pathUtf8, std::uint64_t maxBytes) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
    path_ = pathUtf8;
    if (pathUtf8.empty()) { return false; }
    maxBytes_ = (maxBytes == 0) ? (4ull * 1024 * 1024) : maxBytes;
    file_ = std::fopen(pathUtf8.c_str(), "ab");
    if (file_ == nullptr) {
        path_.clear();
        return false;
    }
    std::fseek(file_, 0, SEEK_END);
    const long size = std::ftell(file_);
    bytesWritten_ = (size > 0) ? static_cast<std::uint64_t>(size) : 0;
    return true;
}

void FileLog::close() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
    path_.clear();
}

bool FileLog::isOpen() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return file_ != nullptr;
}

bool FileLog::rotateLocked() noexcept {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
    const std::string backup = path_ + ".1";
    // A failed rename (locked file, read-only directory) must not stop
    // logging: fall back to truncating the current file.
    (void)std::remove(backup.c_str());
    if (std::rename(path_.c_str(), backup.c_str()) != 0) {
        file_ = std::fopen(path_.c_str(), "wb");
    } else {
        file_ = std::fopen(path_.c_str(), "ab");
    }
    ++rotations_;
    bytesWritten_ = 0;
    return file_ != nullptr;
}

bool FileLog::writeLocked(const char* data, std::size_t len) noexcept {
    if (file_ == nullptr || len == 0) { return file_ != nullptr; }
    if (bytesWritten_ + len > maxBytes_) {
        if (!rotateLocked()) { return false; }
    }
    const std::size_t written = std::fwrite(data, 1, len, file_);
    if (written != len) {
        std::fclose(file_);
        file_ = nullptr;
        return false;
    }
    bytesWritten_ += written;
    return true;
}

bool FileLog::write(const std::string& utf8Line) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ == nullptr) { return false; }
    if (!writeLocked(utf8Line.data(), utf8Line.size())) { return false; }
    const char newline = '\n';
    if (!writeLocked(&newline, 1)) { return false; }
    std::fflush(file_);
    return true;
}

bool FileLog::writef(const char* fmt, ...) noexcept {
    char buffer[1024];
    std::va_list args;
    va_start(args, fmt);
    const int written = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (written <= 0) { return false; }
    const std::size_t len = static_cast<std::size_t>(written) < sizeof(buffer)
                                ? static_cast<std::size_t>(written)
                                : sizeof(buffer) - 1;
    return write(std::string(buffer, len));
}

//---------------------------------------------------------------------------
// Diagnostics
//---------------------------------------------------------------------------
Diagnostics& Diagnostics::instance() noexcept {
    static Diagnostics service;
    return service;
}

Diagnostics::Diagnostics() noexcept {
    for (auto& counter : counters_) { counter.store(0, std::memory_order_relaxed); }
}

void Diagnostics::setLevel(Level level) noexcept {
    level_.store(static_cast<std::uint8_t>(level), std::memory_order_relaxed);
}

void Diagnostics::add(Counter id, std::uint64_t delta) noexcept {
    const auto index = static_cast<std::size_t>(id);
    if (index >= counters_.size()) { return; }
    counters_[index].fetch_add(delta, std::memory_order_relaxed);
}

std::uint64_t Diagnostics::get(Counter id) const noexcept {
    const auto index = static_cast<std::size_t>(id);
    if (index >= counters_.size()) { return 0; }
    return counters_[index].load(std::memory_order_relaxed);
}

std::uint64_t Diagnostics::keyboardEvents() const noexcept {
    return get(Counter::KeyDown) + get(Counter::KeyUp);
}

void Diagnostics::record(Stage stage, std::int64_t microseconds) noexcept {
    if (!atLeast(Level::Basic)) { return; }
    const auto index = static_cast<std::size_t>(stage);
    if (index >= stages_.size()) { return; }
    stages_[index].recordUs(microseconds);
}

const Histogram& Diagnostics::histogram(Stage stage) const noexcept {
    static const Histogram kEmpty;
    const auto index = static_cast<std::size_t>(stage);
    if (index >= stages_.size()) { return kEmpty; }
    return stages_[index];
}

Histogram& Diagnostics::histogram(Stage stage) noexcept {
    const auto index = static_cast<std::size_t>(stage);
    if (index >= stages_.size()) {
        static Histogram fallback;
        return fallback;
    }
    return stages_[index];
}

void Diagnostics::trace(const EventRecord& record) noexcept {
    if (!atLeast(Level::Full)) { return; }
    if (trace_.push(record)) { add(Counter::TraceRecordsDropped, 1); }
}

void Diagnostics::setInjectedDelayUs(std::uint32_t hookUs, std::uint32_t consumerUs) noexcept {
    injectedHookUs_.store(hookUs > 100000u ? 100000u : hookUs, std::memory_order_relaxed);
    injectedConsumerUs_.store(consumerUs > 100000u ? 100000u : consumerUs, std::memory_order_relaxed);
}

void Diagnostics::applyHookDelay() const noexcept {
    const std::uint32_t us = injectedHookUs_.load(std::memory_order_relaxed);
    if (us == 0 || !atLeast(Level::Full)) { return; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(us);
    while (std::chrono::steady_clock::now() < deadline) {
        // Busy-wait on purpose: see setInjectedDelayUs.
    }
}

void Diagnostics::applyConsumerDelay() const noexcept {
    const std::uint32_t us = injectedConsumerUs_.load(std::memory_order_relaxed);
    if (us == 0 || !atLeast(Level::Full)) { return; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(us);
    while (std::chrono::steady_clock::now() < deadline) {}
}

bool Diagnostics::setLogFile(const std::string& pathUtf8) noexcept {
    if (pathUtf8.empty()) {
        fileLog_.close();
        return true;
    }
    return fileLog_.open(pathUtf8);
}

std::size_t Diagnostics::flushTraceToFileLog() noexcept {
    if (!fileLog_.isOpen() || !atLeast(Level::Full)) { return 0; }
    EventRecord batch[64];
    std::size_t total = 0;
    for (;;) {
        const std::size_t n = trace_.drain(batch, std::size(batch));
        if (n == 0) { break; }
        for (std::size_t i = 0; i < n; ++i) {
            if (fileLog_.write(formatEventRecord(batch[i]))) {
                add(Counter::LogLinesWritten, 1);
            }
        }
        total += n;
        if (n < std::size(batch)) { break; }
    }
    return total;
}

void Diagnostics::setSystemSnapshot(const SystemSnapshot& snapshot) noexcept {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    snapshot_ = snapshot;
}

SystemSnapshot Diagnostics::systemSnapshot() const noexcept {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_;
}

std::uint64_t Diagnostics::nowMs() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::int64_t Diagnostics::nowUs() noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void Diagnostics::resetAll() noexcept {
    for (auto& counter : counters_) { counter.store(0, std::memory_order_relaxed); }
    for (auto& stage : stages_) { stage.reset(); }
    trace_.clear();
    emits_.clear();
    {
        std::lock_guard<std::mutex> lock(evidenceMutex_);
        resolution_ = ProcessResolution{};
        displayMetrics_ = DisplayMetrics{};
    }
}

//---------------------------------------------------------------------------
// v1.3.0-beta6 (V4): tester-evidence accessors
//---------------------------------------------------------------------------
void Diagnostics::recordEmit(EmitRecord record) noexcept {
    try {
        emits_.push(std::move(record));
    } catch (...) {
        // Evidence collection must never take the IME down with it.
    }
}

void Diagnostics::setProcessResolution(ProcessResolution resolution) noexcept {
    std::lock_guard<std::mutex> lock(evidenceMutex_);
    if (resolution.tickMs == 0) { resolution.tickMs = nowMs(); }
    resolution_ = std::move(resolution);
}

ProcessResolution Diagnostics::processResolution() const noexcept {
    std::lock_guard<std::mutex> lock(evidenceMutex_);
    return resolution_;
}

void Diagnostics::setDisplayMetrics(DisplayMetrics metrics) noexcept {
    std::lock_guard<std::mutex> lock(evidenceMutex_);
    displayMetrics_ = std::move(metrics);
}

DisplayMetrics Diagnostics::displayMetrics() const noexcept {
    std::lock_guard<std::mutex> lock(evidenceMutex_);
    return displayMetrics_;
}

namespace {
void appendCounterLine(std::string& out, Counter id, std::uint64_t value) {
    out += "  ";
    out += counterNameVi(id);
    out += " (";
    out += counterName(id);
    out += "): ";
    out += std::to_string(value);
    out += "\n";
}
} // namespace

std::string Diagnostics::verdict() const {
    std::string text;
    const std::uint64_t producerFaults = get(Counter::ProducerExceptions);
    const std::uint64_t consumerFaults = get(Counter::ConsumerExceptions);
    const std::uint64_t dropped = get(Counter::QueueOverflowDropped);
    const std::uint64_t tsfFailed = get(Counter::TsfFailedCommits);
    const std::uint64_t sendFailed = get(Counter::SendInputFailedEvents);
    const std::uint64_t reinstalls = get(Counter::HookReinstalls);
    const std::uint64_t barriers = get(Counter::BarrierTimeouts);
    const std::int64_t p99 = histogram(Stage::TotalEdit).percentileUs(0.99);

    if (producerFaults != 0 || consumerFaults != 0) {
        text = "LỖI: có ngoại lệ trong pipeline (producer=";
        text += std::to_string(producerFaults);
        text += ", consumer=";
        text += std::to_string(consumerFaults);
        text += ") — phím đã giảm cấp pass-through";
        return text;
    }
    if (dropped != 0) {
        text = "CẢNH BÁO: hàng đợi tràn, mất ";
        text += std::to_string(dropped);
        text += " tín hiệu đánh thức";
        return text;
    }
    if (tsfFailed != 0 || sendFailed != 0) {
        text = "CẢNH BÁO: xuất chữ lỗi (TSF=";
        text += std::to_string(tsfFailed);
        text += ", SendInput=";
        text += std::to_string(sendFailed);
        text += ")";
        return text;
    }
    if (p99 > 8000) {
        text = "CHẬM: p99 tổng độ trễ phím sinh chữ = ";
        text += std::to_string(p99);
        text += " µs";
        return text;
    }
    if (reinstalls != 0) {
        text = "ĐÃ TỰ PHỤC HỒI hook ";
        text += std::to_string(reinstalls);
        text += " lần (Windows gỡ hook do quá hạn)";
        return text;
    }
    if (barriers != 0) {
        text = "OK (có ";
        text += std::to_string(barriers);
        text += " lần chờ barrier quá hạn)";
        return text;
    }
    text = "OK — không phát hiện lỗi pipeline";
    if (p99 >= 0) {
        text += " · p99 ";
        text += std::to_string(p99);
        text += " µs";
    }
    return text;
}

std::string Diagnostics::report(std::size_t traceLines) const {
    std::string out;
    out.reserve(8192);
    const SystemSnapshot sys = systemSnapshot();

    out += "=== KieeKey diagnostics report ===\n";
    out += "app          : "; out += sys.appVersion; out += "\n";
    out += "os           : "; out += sys.osName; out += "\n";
    out += "arch         : "; out += sys.arch; out += "\n";
    out += "dpi          : "; out += std::to_string(sys.dpi); out += "\n";
    out += "uptime       : "; out += std::to_string(sys.uptimeMs / 1000); out += " s\n";
    out += "process time : user "; out += std::to_string(sys.userTimeMs);
    out += " ms, kernel "; out += std::to_string(sys.kernelTimeMs);
    out += " ms, cpu "; {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.3f%%", sys.cpuPercentSinceStart);
        out += buffer;
    }
    out += "\n";
    out += "memory       : WS "; out += std::to_string(sys.workingSetKb);
    out += " kB (peak "; out += std::to_string(sys.peakWorkingSetKb); out += " kB)\n";
    out += "ime          : "; out += sys.imeEnabled ? "ON" : "OFF";
    out += sys.excludedApp ? " (app đang bị loại trừ)" : "";
    out += " · hook "; out += sys.hookInstalled ? "đã cài" : "CHƯA cài";
    out += " · fg-hook "; out += sys.fgHookInstalled ? "đã cài" : "CHƯA cài";
    out += "\n";
    out += "method/table : "; out += sys.inputMethod; out += " / "; out += sys.codeTable; out += "\n";
    out += "output       : "; out += sys.outputMode; out += "\n";
    out += "foreground   : "; out += sys.foregroundApp; out += "\n";
    out += "layout       : "; out += sys.keyboardLayout; out += "\n";
    out += "live effects : "; out += sys.liveEffectsEnabled ? "ON" : "OFF"; out += "\n";
    out += "diag level   : "; out += levelName(level()); out += "\n";
    out += "verdict      : "; out += verdict(); out += "\n\n";

    // v1.3.0-beta6 (V4): machine-readable tester evidence. These blocks are
    // the hard-data answer to the Windows-only residuals: B2 (did output
    // really reach an external window, and was the gate open?), B4 (which
    // Win32 API resolved the foreground process name?) and B1 (the real
    // DPI / font / DWM numbers at the moment of the report).
    out += "-- emit chain (last deliveries to external windows) --\n";
    {
        const std::vector<EmitRecord> deliveries = emits_.snapshot(emits_.capacity());
        if (deliveries.empty()) {
            out += "  (no deliveries recorded yet — type into an external app)\n";
        } else {
            for (const EmitRecord& record : deliveries) {
                out += formatEmitRecord(record);
            }
        }
    }
    out += "-- foreground resolution (B4) --\n";
    {
        const ProcessResolution resolution = processResolution();
        if (resolution.pid == 0 && resolution.name.empty()) {
            out += "  (no foreground resolution recorded yet)\n";
        } else {
            out += formatProcessResolution(resolution);
        }
    }
    out += "-- display (B1) --\n";
    out += formatDisplayMetrics(displayMetrics());
    out += "\n";

    out += "-- keyboard (chỉ phím thật, không tính chuột/cửa sổ) --\n";
    for (const Counter id : {Counter::KeyDown, Counter::KeyUp, Counter::KeySuppressed,
                             Counter::KeyPassThrough, Counter::KeySelfInjected,
                             Counter::KeyIgnoredInjected, Counter::KeyIgnoredModifier}) {
        appendCounterLine(out, id, get(id));
    }
    out += "  TỔNG sự kiện bàn phím: " + std::to_string(keyboardEvents()) + "\n";
    out += "-- non-keyboard sources --\n";
    for (const Counter id : {Counter::MouseButton, Counter::MouseWheel,
                             Counter::ForegroundChanged}) {
        appendCounterLine(out, id, get(id));
    }
    out += "-- pipeline --\n";
    for (const Counter id : {Counter::QueuedToConsumer, Counter::QueueOverflowDropped,
                             Counter::ConsumerWakes, Counter::SetEventSyscalls,
                             Counter::EngineEdits, Counter::MacroExpansions,
                             Counter::LiveEffectRewrites, Counter::Resyncs,
                             Counter::BarrierTimeouts, Counter::HookReinstalls}) {
        appendCounterLine(out, id, get(id));
    }
    out += "-- output --\n";
    for (const Counter id : {Counter::TsfCommits, Counter::TsfSlowCommits,
                             Counter::TsfFailedCommits, Counter::SendInputCalls,
                             Counter::SendInputFailedEvents}) {
        appendCounterLine(out, id, get(id));
    }
    out += "-- guards & faults --\n";
    for (const Counter id : {Counter::DigitGuardHits, Counter::ShortcutGuardHits,
                             Counter::ProducerExceptions, Counter::ConsumerExceptions}) {
        appendCounterLine(out, id, get(id));
    }
    out += "-- own-window policy --\n";
    for (const Counter id : {Counter::OwnWindowBypassed, Counter::OwnTextFieldComposed}) {
        appendCounterLine(out, id, get(id));
    }
    out += "-- diagnostics machinery --\n";
    for (const Counter id : {Counter::TraceRecordsDropped, Counter::LogLinesWritten,
                             Counter::LogRotateEvents}) {
        appendCounterLine(out, id, get(id));
    }
    out += "\n";

    out += "-- latency (µs; bucket = [low,high), percentile = upper bucket edge) --\n";
    for (std::size_t i = 0; i < static_cast<std::size_t>(Stage::kCount); ++i) {
        const Stage stage = static_cast<Stage>(i);
        const Histogram& h = histogram(stage);
        out += "  ";
        out += stageName(stage);
        out += ": n=";
        out += std::to_string(h.total());
        if (h.total() != 0) {
            char buffer[160];
            std::snprintf(buffer, sizeof(buffer),
                          " mean=%.1f p50=%lld p95=%lld p99=%lld max=%lld",
                          h.meanUs(),
                          static_cast<long long>(h.percentileUs(0.50)),
                          static_cast<long long>(h.percentileUs(0.95)),
                          static_cast<long long>(h.percentileUs(0.99)),
                          static_cast<long long>(h.maxUs()));
            out += buffer;
            out += "  | ";
            out += h.toText();
        }
        out += "\n";
    }
    out += "\n";

    if (traceLines != 0 && atLeast(Level::Full)) {
        out += "-- last traced events --\n";
        std::vector<EventRecord> recent(traceLines);
        const std::size_t n = trace_.snapshot(recent.data(), recent.size());
        for (std::size_t i = 0; i < n; ++i) {
            out += "  ";
            out += formatEventRecord(recent[i]);
            out += "\n";
        }
        if (n == 0) { out += "  (trống)\n"; }
        out += "\n";
    }

    out += "-- injected delay (chẩn đoán, Full) --\n";
    out += "  hook: "; out += std::to_string(injectedHookDelayUs()); out += " µs";
    out += ", consumer: "; out += std::to_string(injectedConsumerDelayUs()); out += " µs\n";
    out += "-- file log --\n";
    out += "  path: "; out += fileLog_.path().empty() ? std::string("(tắt)") : fileLog_.path();
    out += "\n  bytes: "; out += std::to_string(fileLog_.bytesWritten());
    out += ", rotations: "; out += std::to_string(fileLog_.rotations()); out += "\n";
    out += "=== end of report ===\n";
    return out;
}

//---------------------------------------------------------------------------
std::string formatEventRecord(const EventRecord& record) {
    static const char* kSource[] = {"kb", "mouse", "fg"};
    static const char* kAction[] = {"down", "up", "sysdown", "sysup"};
    static const char* kDecision[] = {"pass", "edit", "ignored", "inline"};
    char buffer[256];
    const std::size_t source = record.source < 3 ? record.source : 0;
    const std::size_t action = record.action < 4 ? record.action : 0;
    const std::size_t decision = record.decision < 4 ? record.decision : 0;
    std::snprintf(buffer, sizeof(buffer),
                  "#%llu t=%llums %s %s vk=0x%02X %s bs=%u len=%u stage=%s %lldus flags=0x%02X",
                  static_cast<unsigned long long>(record.seq),
                  static_cast<unsigned long long>(record.tickMs),
                  kSource[source], kAction[action], record.vk, kDecision[decision],
                  record.backspace, record.chars,
                  stageName(static_cast<Stage>(record.stage)),
                  static_cast<long long>(record.stageUs),
                  record.flags);
    return std::string(buffer);
}

} // namespace ok::diag
