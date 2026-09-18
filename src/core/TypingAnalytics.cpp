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
// File: src/core/TypingAnalytics.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "TypingAnalytics.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace ok::analytics {

TypingAnalyticsEngine& TypingAnalyticsEngine::instance() noexcept {
    static TypingAnalyticsEngine s_instance;
    return s_instance;
}

TypingAnalyticsEngine::TypingAnalyticsEngine() {
    reset();
}

void TypingAnalyticsEngine::reset() noexcept {
    std::lock_guard<std::mutex> lock(m_analysisMutex);
    m_head.store(0, std::memory_order_relaxed);
    for (auto& item : m_ring) {
        item = KeyLogEntry{};
    }
}

void TypingAnalyticsEngine::observeKey(
    char32_t ch,
    uint64_t timestampUs,
    bool isBackspace,
    bool isVietnameseMark,
    bool isToneKey) noexcept {
    uint64_t idx = m_head.fetch_add(1, std::memory_order_acq_rel);
    size_t slot = static_cast<size_t>(idx % kRingCapacity);
    m_ring[slot] = KeyLogEntry{
        .ch = ch,
        .timestampUs = timestampUs,
        .isBackspace = isBackspace,
        .isVietnameseMark = isVietnameseMark,
        .isToneKey = isToneKey,
    };
}

AnalyticsSnapshot TypingAnalyticsEngine::computeSnapshot() {
    std::lock_guard<std::mutex> lock(m_analysisMutex);

    AnalyticsSnapshot snap{};
    uint64_t totalWritten = m_head.load(std::memory_order_acquire);
    if (totalWritten == 0) {
        return snap;
    }

    size_t count = static_cast<size_t>(std::min<uint64_t>(totalWritten, kRingCapacity));
    uint64_t startIdx = (totalWritten > kRingCapacity) ? (totalWritten - kRingCapacity) : 0;

    std::vector<KeyLogEntry> entries;
    entries.reserve(count);
    for (uint64_t i = startIdx; i < totalWritten; ++i) {
        entries.push_back(m_ring[static_cast<size_t>(i % kRingCapacity)]);
    }

    snap.totalKeystrokes = count;
    for (const auto& e : entries) {
        if (e.isBackspace) {
            snap.totalBackspaces++;
        } else {
            snap.totalChars++;
        }
    }

    if (count >= 2) {
        uint64_t startUs = entries.front().timestampUs;
        uint64_t endUs = entries.back().timestampUs;
        if (endUs > startUs) {
            snap.sessionDurationMs = (endUs - startUs) / 1000;
        }
    }

    if (snap.sessionDurationMs > 0) {
        double minutes = static_cast<double>(snap.sessionDurationMs) / 60000.0;
        snap.rawWpm = (static_cast<double>(snap.totalKeystrokes) / 5.0) / minutes;
        double netChars = (snap.totalChars > snap.totalBackspaces) ?
                          static_cast<double>(snap.totalChars - snap.totalBackspaces) : 0.0;
        snap.netWpm = (netChars / 5.0) / minutes;
    }

    if (snap.totalKeystrokes > 0) {
        double correct = static_cast<double>(snap.totalChars);
        snap.accuracyPercent = (correct / static_cast<double>(snap.totalKeystrokes)) * 100.0;
        if (snap.accuracyPercent > 100.0) snap.accuracyPercent = 100.0;
    }

    // Intervals, bursts, pauses, tone delay
    std::vector<double> ikis;
    ikis.reserve(count);

    std::vector<double> toneDelays;
    std::vector<double> recoveryDelays;

    uint32_t currentBurst = 0;
    std::vector<uint32_t> burstLengths;
    std::vector<double> pauseDurations;

    for (size_t i = 1; i < count; ++i) {
        if (entries[i].timestampUs >= entries[i - 1].timestampUs) {
            double diffMs = static_cast<double>(entries[i].timestampUs - entries[i - 1].timestampUs) / 1000.0;
            ikis.push_back(diffMs);

            if (diffMs > 500.0) { // Pause boundary
                snap.pauseCount++;
                pauseDurations.push_back(diffMs);
                if (currentBurst > 0) {
                    burstLengths.push_back(currentBurst);
                    currentBurst = 0;
                }
            } else {
                currentBurst++;
            }

            if (entries[i].isToneKey) {
                snap.toneKeyCount++;
                toneDelays.push_back(diffMs);
            }

            if (entries[i].isBackspace && !entries[i - 1].isBackspace) {
                recoveryDelays.push_back(diffMs);
            }
        }
    }

    if (currentBurst > 0) {
        burstLengths.push_back(currentBurst);
    }

    if (!ikis.empty()) {
        double sum = 0.0;
        for (double v : ikis) sum += v;
        snap.meanIkiMs = sum / static_cast<double>(ikis.size());

        double var = 0.0;
        for (double v : ikis) {
            double d = v - snap.meanIkiMs;
            var += d * d;
        }
        snap.stddevIkiMs = std::sqrt(var / static_cast<double>(ikis.size()));

        std::sort(ikis.begin(), ikis.end());
        snap.p50IkiMs = ikis[ikis.size() * 50 / 100];
        snap.p90IkiMs = ikis[ikis.size() * 90 / 100];
        snap.p99IkiMs = ikis[ikis.size() * 99 / 100];
    }

    if (!burstLengths.empty()) {
        snap.burstCount = static_cast<uint32_t>(burstLengths.size());
        double bsum = 0.0;
        for (auto b : burstLengths) bsum += b;
        snap.avgBurstLength = bsum / static_cast<double>(burstLengths.size());
    }

    if (!pauseDurations.empty()) {
        double psum = 0.0;
        for (double p : pauseDurations) psum += p;
        snap.avgPauseMs = psum / static_cast<double>(pauseDurations.size());
    }

    if (!recoveryDelays.empty()) {
        double rsum = 0.0;
        for (double r : recoveryDelays) rsum += r;
        snap.avgRecoveryLatencyMs = rsum / static_cast<double>(recoveryDelays.size());
    }

    if (!toneDelays.empty()) {
        double tsum = 0.0;
        for (double t : toneDelays) tsum += t;
        snap.avgToneDelayMs = tsum / static_cast<double>(toneDelays.size());
    }

    // Fatigue slope: compare first half WPM vs second half WPM
    if (count >= 40) {
        size_t half = count / 2;
        uint64_t dur1 = (entries[half - 1].timestampUs - entries[0].timestampUs) / 1000;
        uint64_t dur2 = (entries.back().timestampUs - entries[half].timestampUs) / 1000;
        if (dur1 > 0 && dur2 > 0) {
            double wpm1 = (static_cast<double>(half) / 5.0) / (static_cast<double>(dur1) / 60000.0);
            double wpm2 = (static_cast<double>(count - half) / 5.0) / (static_cast<double>(dur2) / 60000.0);
            snap.fatigueSlope = wpm2 - wpm1;
        }
    }

    return snap;
}

std::vector<CoachRecommendation> TypingAnalyticsEngine::generateCoachingAdvice() {
    AnalyticsSnapshot snap = computeSnapshot();
    std::vector<CoachRecommendation> recs;

    if (snap.totalKeystrokes < kMinSamplesForCoach) {
        return recs; // Not enough samples
    }

    // 1. Pacing & Burst analysis
    if (snap.avgBurstLength > 15.0 && snap.accuracyPercent < 94.0) {
        std::ostringstream fact, adv;
        fact << "Độ dài chuỗi gõ nhanh trung bình " << static_cast<int>(snap.avgBurstLength)
             << " ký tự, nhưng độ chính xác chỉ đạt " << static_cast<int>(snap.accuracyPercent) << "%.";
        adv << "Nên gõ đều nhịp (rhythm pacing) thay vì tăng tốc đột ngột trong các chuỗi dài.";
        recs.push_back({CoachItemCategory::Burst, fact.str(), adv.str()});
    }

    // 2. Accuracy vs Speed
    if (snap.rawWpm > 70.0 && snap.accuracyPercent < 92.0) {
        std::ostringstream fact, adv;
        fact << "Tốc độ thô đạt " << static_cast<int>(snap.rawWpm)
             << " WPM nhưng tỷ lệ sửa lỗi (backspace) chiếm "
             << static_cast<int>((static_cast<double>(snap.totalBackspaces) / snap.totalKeystrokes) * 100.0) << "%.";
        int target = static_cast<int>(snap.rawWpm * 0.85);
        adv << "Hãy duy trì dải tốc độ " << target - 5 << "–" << target
            << " WPM để đạt độ chính xác >98%, giúp WPM thực tế cao hơn.";
        recs.push_back({CoachItemCategory::Accuracy, fact.str(), adv.str()});
    }

    // 3. Vietnamese Tone key analysis
    if (snap.toneKeyCount > 10 && snap.avgToneDelayMs > 250.0) {
        std::ostringstream fact, adv;
        fact << "Thời gian phản hồi khi gõ phím dấu tiếng Việt trung bình là "
             << static_cast<int>(snap.avgToneDelayMs) << " ms (chậm hơn nhịp gõ thông thường).";
        adv << "Luyện tập các cặp nguyên âm + dấu Telex (as, af, ar, ax, aj) để phản xạ gõ dấu trở thành phản xạ cơ bắp.";
        recs.push_back({CoachItemCategory::Telex, fact.str(), adv.str()});
    }

    // 4. Fatigue detection
    if (snap.fatigueSlope < -12.0) {
        std::ostringstream fact, adv;
        fact << "Tốc độ gõ giảm " << static_cast<int>(-snap.fatigueSlope)
             << " WPM giữa nửa đầu và nửa sau phiên gõ.";
        adv << "Có dấu hiệu mỏi cổ tay hoặc mất tập trung. Hãy thả lỏng tay và nghỉ ngơi 1–2 phút.";
        recs.push_back({CoachItemCategory::Fatigue, fact.str(), adv.str()});
    }

    // 5. Positive reinforcement / Steady rhythm
    if (recs.empty()) {
        std::ostringstream fact, adv;
        fact << "Độ chính xác hiện tại đạt " << static_cast<int>(snap.accuracyPercent)
             << "% với WPM đạt " << static_cast<int>(snap.netWpm) << ".";
        adv << "Nhịp gõ rất ổn định và chính xác. Tiếp tục duy trì phong độ!";
        recs.push_back({CoachItemCategory::Pacing, fact.str(), adv.str()});
    }

    return recs;
}

} // namespace ok::analytics
