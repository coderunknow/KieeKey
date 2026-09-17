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
// File: src/core/AiRival.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "AiRival.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>

namespace ok::ai {

AiRivalEngine& AiRivalEngine::instance() noexcept {
    static AiRivalEngine s_instance;
    return s_instance;
}

AiRivalEngine::AiRivalEngine() {
    m_optIn.store(false, std::memory_order_relaxed);
    m_profile.reset();
}

void AiRivalEngine::setOptIn(bool optIn) noexcept {
    m_optIn.store(optIn, std::memory_order_release);
    if (!optIn) {
        resetProfile();
    }
}

void AiRivalEngine::resetProfile() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_profile.reset();
    m_pendingObs.clear();
    m_yesterdayGhost.clear();
}

void AiRivalEngine::observeKeystroke(
    char32_t ch,
    uint64_t timestampUs,
    bool isBackspace,
    bool isToneKey) noexcept {
    if (!m_optIn.load(std::memory_order_relaxed)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pendingObs.size() < 2048) {
        m_pendingObs.push_back({ch, timestampUs, isBackspace, isToneKey});
    }
}

void AiRivalEngine::trainBatch() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pendingObs.size() < 5) {
        return;
    }

    std::vector<double> ikis;
    std::vector<double> toneDelays;
    std::vector<double> recoveryDelays;
    uint32_t backspaces = 0;
    uint32_t currentBurst = 0;
    std::vector<uint32_t> bursts;

    for (size_t i = 1; i < m_pendingObs.size(); ++i) {
        if (m_pendingObs[i].timestampUs >= m_pendingObs[i - 1].timestampUs) {
            double diff = static_cast<double>(m_pendingObs[i].timestampUs - m_pendingObs[i - 1].timestampUs) / 1000.0;
            if (diff > 10.0 && diff < 2000.0) {
                ikis.push_back(diff);
                if (diff > 350.0) {
                    if (currentBurst > 0) {
                        bursts.push_back(currentBurst);
                        currentBurst = 0;
                    }
                } else {
                    currentBurst++;
                }
            }

            if (m_pendingObs[i].isToneKey) {
                toneDelays.push_back(diff);
            }

            if (m_pendingObs[i].isBackspace) {
                backspaces++;
                recoveryDelays.push_back(diff);
            }
        }
    }

    if (currentBurst > 0) bursts.push_back(currentBurst);

    if (!ikis.empty()) {
        double sum = 0.0;
        for (double d : ikis) sum += d;
        double mean = sum / static_cast<double>(ikis.size());

        double var = 0.0;
        for (double d : ikis) {
            double diff = d - mean;
            var += diff * diff;
        }
        double stddev = std::sqrt(var / static_cast<double>(ikis.size()));

        // Exponential moving average update
        double alpha = 0.3;
        m_profile.meanIkiMs = (1.0 - alpha) * m_profile.meanIkiMs + alpha * mean;
        m_profile.stddevIkiMs = (1.0 - alpha) * m_profile.stddevIkiMs + alpha * stddev;
        m_profile.sampleCount += ikis.size();
    }

    if (!toneDelays.empty()) {
        double sum = 0.0;
        for (double d : toneDelays) sum += d;
        double meanTone = sum / static_cast<double>(toneDelays.size());
        m_profile.toneDelayMs = 0.7 * m_profile.toneDelayMs + 0.3 * meanTone;
    }

    if (!recoveryDelays.empty()) {
        double sum = 0.0;
        for (double d : recoveryDelays) sum += d;
        m_profile.backspaceRecoveryMs = 0.7 * m_profile.backspaceRecoveryMs +
                                       0.3 * (sum / static_cast<double>(recoveryDelays.size()));
    }

    if (!m_pendingObs.empty()) {
        double curErr = static_cast<double>(backspaces) / static_cast<double>(m_pendingObs.size());
        m_profile.errorRate = 0.8 * m_profile.errorRate + 0.2 * curErr;
    }

    if (!bursts.empty()) {
        double bsum = 0.0;
        for (auto b : bursts) bsum += b;
        m_profile.avgBurstLength = 0.7 * m_profile.avgBurstLength +
                                   0.3 * (bsum / static_cast<double>(bursts.size()));
    }

    m_pendingObs.clear();
}

AiProfile AiRivalEngine::getProfile() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_profile;
}

std::vector<SimulatedKeystroke> AiRivalEngine::simulateTypingRun(
    std::u32string_view targetText,
    uint32_t seed) const {
    AiProfile prof = getProfile();
    std::mt19937 rng(seed != 0 ? seed : 0xACE1337);
    std::normal_distribution<double> distIki(prof.meanIkiMs, std::max(5.0, prof.stddevIkiMs));
    std::uniform_real_distribution<double> distUniform(0.0, 1.0);

    std::vector<SimulatedKeystroke> run;
    run.reserve(targetText.size() * 2);

    for (size_t i = 0; i < targetText.size(); ++i) {
        char32_t targetCh = targetText[i];
        double delay = distIki(rng);
        if (delay < 30.0) delay = 30.0;
        if (delay > 600.0) delay = 600.0;

        // Space or word boundary pause
        if (targetCh == U' ' || targetCh == U'\n') {
            delay += prof.pauseThresholdMs * (0.8 + 0.4 * distUniform(rng));
        }

        // Simulate natural typo
        if (distUniform(rng) < prof.errorRate && targetCh > 32) {
            char32_t typoCh = (targetCh == U'a') ? U's' : (targetCh + 1);
            run.push_back({typoCh, static_cast<uint32_t>(delay), true, false});

            // Backspace recovery
            uint32_t recDelay = static_cast<uint32_t>(prof.backspaceRecoveryMs * (0.9 + 0.2 * distUniform(rng)));
            run.push_back({U'\b', recDelay, false, true});

            // Retry correct key
            delay = distIki(rng);
            if (delay < 30.0) delay = 30.0;
        }

        run.push_back({targetCh, static_cast<uint32_t>(delay), false, false});
    }

    return run;
}

void AiRivalEngine::recordGhostRun(const std::vector<uint32_t>& charTimeOffsetsMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_yesterdayGhost = charTimeOffsetsMs;
}

std::vector<uint32_t> AiRivalEngine::getYesterdayGhost() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_yesterdayGhost;
}

std::string AiRivalEngine::serializeProfile() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream ss;
    ss << "KIEEKEY_AI_PROFILE_V1\n";
    ss << "sampleCount=" << m_profile.sampleCount << "\n";
    ss << "meanIkiMs=" << m_profile.meanIkiMs << "\n";
    ss << "stddevIkiMs=" << m_profile.stddevIkiMs << "\n";
    ss << "avgBurstLength=" << m_profile.avgBurstLength << "\n";
    ss << "pauseThresholdMs=" << m_profile.pauseThresholdMs << "\n";
    ss << "errorRate=" << m_profile.errorRate << "\n";
    ss << "backspaceRecoveryMs=" << m_profile.backspaceRecoveryMs << "\n";
    ss << "toneDelayMs=" << m_profile.toneDelayMs << "\n";
    ss << "speedCurveSlope=" << m_profile.speedCurveSlope << "\n";
    return ss.str();
}

bool AiRivalEngine::deserializeProfile(std::string_view data) {
    if (data.rfind("KIEEKEY_AI_PROFILE_V1", 0) != 0) {
        return false;
    }

    AiProfile p{};
    std::istringstream ss{std::string(data)};
    std::string line;
    std::getline(ss, line);

    while (std::getline(ss, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        try {
            if (k == "sampleCount") p.sampleCount = std::stoull(v);
            else if (k == "meanIkiMs") p.meanIkiMs = std::stod(v);
            else if (k == "stddevIkiMs") p.stddevIkiMs = std::stod(v);
            else if (k == "avgBurstLength") p.avgBurstLength = std::stod(v);
            else if (k == "pauseThresholdMs") p.pauseThresholdMs = std::stod(v);
            else if (k == "errorRate") p.errorRate = std::stod(v);
            else if (k == "backspaceRecoveryMs") p.backspaceRecoveryMs = std::stod(v);
            else if (k == "toneDelayMs") p.toneDelayMs = std::stod(v);
            else if (k == "speedCurveSlope") p.speedCurveSlope = std::stod(v);
        } catch (...) {}
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_profile = p;
    return true;
}

} // namespace ok::ai
