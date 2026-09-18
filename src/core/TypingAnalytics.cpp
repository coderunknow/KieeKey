//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: src/core/TypingAnalytics.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "TypingAnalytics.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace ok::analytics {

namespace {

constexpr double kPauseThresholdMs = 500.0;

double percentile(const double* sorted, std::size_t count, int percent) {
    if (count == 0) {
        return 0.0;
    }
    std::size_t index = static_cast<std::size_t>(count) * static_cast<std::size_t>(percent) / 100u;
    if (index >= count) {
        index = count - 1;
    }
    return sorted[index];
}

} // namespace

TypingAnalyticsEngine& TypingAnalyticsEngine::instance() noexcept {
    static TypingAnalyticsEngine s_instance;
    return s_instance;
}

TypingAnalyticsEngine::TypingAnalyticsEngine() = default;

void TypingAnalyticsEngine::reset() noexcept {
    m_ring.reset();
    m_sessionStartIndex.store(0, std::memory_order_release);
}

void TypingAnalyticsEngine::beginSession() noexcept {
    m_sessionStartIndex.store(m_ring.head(), std::memory_order_release);
}

void TypingAnalyticsEngine::observeKey(char32_t ch, std::uint64_t timestampUs, bool isBackspace,
                                       bool isVietnameseMark, bool isToneKey) noexcept {
    m_ring.push(timestampUs,
                packKeyPayload(static_cast<std::uint32_t>(ch), isBackspace, isVietnameseMark,
                               isToneKey));
}

AnalyticsSnapshot TypingAnalyticsEngine::computeSnapshot() const {
    const std::uint64_t total = m_ring.head();
    const std::uint64_t count = std::min<std::uint64_t>(total, kRingCapacity);
    return computeRange(total - count, total);
}

AnalyticsSnapshot TypingAnalyticsEngine::computeSessionSnapshot() const {
    const std::uint64_t total = m_ring.head();
    std::uint64_t first = m_sessionStartIndex.load(std::memory_order_acquire);
    const std::uint64_t oldest = (total > kRingCapacity) ? (total - kRingCapacity) : 0;
    first = std::max(first, oldest);
    if (first > total) {
        first = total;
    }
    return computeRange(first, total);
}

AnalyticsSnapshot TypingAnalyticsEngine::computeRange(std::uint64_t firstIndex,
                                                      std::uint64_t total) const {
    std::lock_guard<std::mutex> lock(m_analysisMutex);
    AnalyticsSnapshot snap{};
    if (total <= firstIndex) {
        return snap;
    }

    // Copy the ring window into the scratch buffers (single pass, no alloc).
    std::size_t count = 0;
    const std::size_t maxCount = static_cast<std::size_t>(
        std::min<std::uint64_t>(total - firstIndex, kRingCapacity));
    for (std::uint64_t index = firstIndex; index < total && count < maxCount; ++index) {
        std::uint64_t stamp = 0;
        std::uint64_t payload = 0;
        if (!m_ring.read(index, stamp, payload)) {
            snap.sampleSkipped = true;
            continue;
        }
        m_stampScratch[count] = stamp;
        m_payloadScratch[count] = payload;
        ++count;
    }
    if (count == 0) {
        return snap;
    }

    snap.totalKeystrokes = count;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t ch = 0;
        bool isBackspace = false;
        bool isMark = false;
        bool isTone = false;
        unpackKeyPayload(m_payloadScratch[i], ch, isBackspace, isMark, isTone);
        (void)ch;
        (void)isMark;
        if (isBackspace) {
            ++snap.totalBackspaces;
        } else {
            ++snap.totalChars;
        }
    }

    if (count >= 2 && m_stampScratch[count - 1] > m_stampScratch[0]) {
        snap.sessionDurationMs = (m_stampScratch[count - 1] - m_stampScratch[0]) / 1000;
    }

    if (snap.sessionDurationMs > 0) {
        const double minutes = static_cast<double>(snap.sessionDurationMs) / 60000.0;
        snap.rawWpm = (static_cast<double>(snap.totalKeystrokes) / 5.0) / minutes;
        const double netChars = (snap.totalChars > snap.totalBackspaces)
                                    ? static_cast<double>(snap.totalChars - snap.totalBackspaces)
                                    : 0.0;
        snap.netWpm = (netChars / 5.0) / minutes;
    }

    if (snap.totalKeystrokes > 0) {
        snap.accuracyPercent =
            (static_cast<double>(snap.totalChars) / static_cast<double>(snap.totalKeystrokes)) * 100.0;
        snap.accuracyPercent = std::min(100.0, snap.accuracyPercent);
    }

    std::size_t ikiCount = 0;
    double toneDelaySum = 0.0;
    std::uint32_t toneCount = 0;
    double recoverySum = 0.0;
    std::uint32_t recoveryCount = 0;
    double pauseSum = 0.0;
    std::uint32_t pauseCount = 0;
    std::uint32_t currentBurst = 0;
    std::uint32_t burstCount = 0;
    double burstSum = 0.0;

    std::size_t halfIndex = count / 2;
    double firstHalfSum = 0.0;
    std::size_t firstHalfCount = 0;
    double secondHalfSum = 0.0;
    std::size_t secondHalfCount = 0;

    for (std::size_t i = 1; i < count; ++i) {
        if (m_stampScratch[i] < m_stampScratch[i - 1]) {
            continue;   // out-of-order sample
        }
        const double diffMs =
            static_cast<double>(m_stampScratch[i] - m_stampScratch[i - 1]) / 1000.0;
        if (ikiCount < kRingCapacity) {
            m_ikiScratch[ikiCount++] = diffMs;
        }
        if (i < halfIndex) {
            firstHalfSum += diffMs;
            ++firstHalfCount;
        } else {
            secondHalfSum += diffMs;
            ++secondHalfCount;
        }

        if (diffMs > kPauseThresholdMs) {
            ++pauseCount;
            pauseSum += diffMs;
            if (currentBurst > 0) {
                burstSum += currentBurst;
                ++burstCount;
                currentBurst = 0;
            }
        } else {
            ++currentBurst;
        }

        std::uint32_t ch = 0;
        bool isBackspace = false;
        bool isMark = false;
        bool isTone = false;
        unpackKeyPayload(m_payloadScratch[i], ch, isBackspace, isMark, isTone);
        (void)ch;
        (void)isMark;
        if (isTone) {
            ++toneCount;
            toneDelaySum += diffMs;
        }
        if (isBackspace) {
            ++recoveryCount;
            recoverySum += diffMs;
        }
    }
    if (currentBurst > 0) {
        burstSum += currentBurst;
        ++burstCount;
    }

    if (ikiCount > 0) {
        double sum = 0.0;
        for (std::size_t i = 0; i < ikiCount; ++i) {
            sum += m_ikiScratch[i];
        }
        snap.meanIkiMs = sum / static_cast<double>(ikiCount);
        double variance = 0.0;
        for (std::size_t i = 0; i < ikiCount; ++i) {
            const double d = m_ikiScratch[i] - snap.meanIkiMs;
            variance += d * d;
        }
        snap.stddevIkiMs = std::sqrt(variance / static_cast<double>(ikiCount));
        std::sort(m_ikiScratch.begin(), m_ikiScratch.begin() + static_cast<std::ptrdiff_t>(ikiCount));
        snap.p50IkiMs = percentile(m_ikiScratch.data(), ikiCount, 50);
        snap.p90IkiMs = percentile(m_ikiScratch.data(), ikiCount, 90);
        snap.p99IkiMs = percentile(m_ikiScratch.data(), ikiCount, 99);
    }

    snap.burstCount = burstCount;
    snap.avgBurstLength = (burstCount > 0) ? burstSum / static_cast<double>(burstCount) : 0.0;
    snap.pauseCount = pauseCount;
    snap.avgPauseMs = (pauseCount > 0) ? pauseSum / static_cast<double>(pauseCount) : 0.0;
    snap.toneKeyCount = toneCount;
    snap.avgToneDelayMs = (toneCount > 0) ? toneDelaySum / static_cast<double>(toneCount) : 0.0;
    snap.avgRecoveryLatencyMs =
        (recoveryCount > 0) ? recoverySum / static_cast<double>(recoveryCount) : 0.0;

    // Fatigue: slower second half means the mean interval grew (positive slope)
    // and the implied WPM dropped (the snapshot reports the WPM delta, negative
    // when the typist slowed down).
    if (firstHalfCount >= 5 && secondHalfCount >= 5) {
        const double firstMean = firstHalfSum / static_cast<double>(firstHalfCount);
        const double secondMean = secondHalfSum / static_cast<double>(secondHalfCount);
        const double firstWpm = (firstMean > 0.001) ? 60000.0 / (5.0 * firstMean) : 0.0;
        const double secondWpm = (secondMean > 0.001) ? 60000.0 / (5.0 * secondMean) : 0.0;
        snap.fatigueSlope = secondWpm - firstWpm;
    }

    return snap;
}

std::vector<CoachRecommendation> TypingAnalyticsEngine::generateCoachingAdvice() const {
    const AnalyticsSnapshot snap = computeSnapshot();
    std::vector<CoachRecommendation> recs;

    if (snap.totalKeystrokes < kMinSamplesForCoach) {
        return recs;   // not enough samples: never invent advice
    }

    if (snap.avgBurstLength > 15.0 && snap.accuracyPercent < 94.0) {
        std::ostringstream fact;
        fact << "Độ dài chuỗi gõ nhanh trung bình " << static_cast<int>(snap.avgBurstLength)
             << " ký tự, nhưng độ chính xác chỉ đạt " << static_cast<int>(snap.accuracyPercent)
             << "%.";
        recs.push_back({CoachItemCategory::Burst, fact.str(),
                        "Nên gõ đều nhịp (rhythm pacing) thay vì tăng tốc đột ngột trong các chuỗi dài."});
    }

    if (snap.rawWpm > 70.0 && snap.accuracyPercent < 92.0) {
        const double backspaceRatio =
            static_cast<double>(snap.totalBackspaces) / static_cast<double>(snap.totalKeystrokes);
        std::ostringstream fact;
        fact << "Tốc độ thô đạt " << static_cast<int>(snap.rawWpm)
             << " WPM nhưng tỷ lệ sửa lỗi (backspace) chiếm "
             << static_cast<int>(backspaceRatio * 100.0) << "%.";
        const int target = static_cast<int>(snap.rawWpm * 0.85);
        std::ostringstream advice;
        advice << "Hãy duy trì dải tốc độ " << (target - 5) << "–" << target
               << " WPM để đạt độ chính xác >98%, giúp WPM thực tế cao hơn.";
        recs.push_back({CoachItemCategory::Accuracy, fact.str(), advice.str()});
    }

    if (snap.toneKeyCount > 10 && snap.avgToneDelayMs > 250.0) {
        std::ostringstream fact;
        fact << "Thời gian phản hồi khi gõ phím dấu tiếng Việt trung bình là "
             << static_cast<int>(snap.avgToneDelayMs) << " ms (chậm hơn nhịp gõ thông thường).";
        recs.push_back({CoachItemCategory::Telex, fact.str(),
                        "Luyện tập các cặp nguyên âm + dấu Telex (as, af, ar, ax, aj) để phản xạ gõ dấu trở thành phản xạ cơ bắp."});
    }

    if (snap.fatigueSlope < -12.0) {
        std::ostringstream fact;
        fact << "Tốc độ gõ giảm " << static_cast<int>(-snap.fatigueSlope)
             << " WPM giữa nửa đầu và nửa sau phiên gõ.";
        recs.push_back({CoachItemCategory::Fatigue, fact.str(),
                        "Có dấu hiệu mỏi cổ tay hoặc mất tập trung. Hãy thả lỏng tay và nghỉ ngơi 1–2 phút."});
    }

    if (snap.stddevIkiMs > 0.0 && snap.meanIkiMs > 0.0 &&
        (snap.stddevIkiMs / snap.meanIkiMs) > 0.85 && snap.pauseCount > 3) {
        std::ostringstream fact;
        fact << "Nhịp gõ không ổn định: độ lệch chuẩn " << static_cast<int>(snap.stddevIkiMs)
             << " ms trên nhịp trung bình " << static_cast<int>(snap.meanIkiMs) << " ms.";
        recs.push_back({CoachItemCategory::Pacing, fact.str(),
                        "Giữ nhịp đều thay vì gõ từng cụm rồi dừng: đọc trước 3–4 từ và gõ liên tục."});
    }

    if (recs.empty()) {
        std::ostringstream fact;
        fact << "Độ chính xác hiện tại đạt " << static_cast<int>(snap.accuracyPercent)
             << "% với WPM đạt " << static_cast<int>(snap.netWpm) << ".";
        recs.push_back({CoachItemCategory::Pacing, fact.str(),
                        "Nhịp gõ rất ổn định và chính xác. Tiếp tục duy trì phong độ!"});
    }

    return recs;
}

} // namespace ok::analytics
