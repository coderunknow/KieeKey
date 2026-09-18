//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
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

namespace {

constexpr double kMinIkiMs = 25.0;
constexpr double kMaxIkiMs = 900.0;
constexpr double kMinStddevMs = 4.0;
constexpr double kMaxStddevMs = 400.0;
constexpr double kMinBurst = 1.2;
constexpr double kMaxBurst = 60.0;
constexpr double kMinPauseMs = 120.0;
constexpr double kMaxPauseMs = 2500.0;
constexpr double kMinRecoveryMs = 40.0;
constexpr double kMaxRecoveryMs = 2000.0;
constexpr double kMinToneMs = 40.0;
constexpr double kMaxToneMs = 1500.0;
constexpr double kMaxSlopeMsPer100 = 250.0;
// Evidence weight ceiling: a batch is blended with the existing model in
// proportion to the number of samples behind each, but the historical weight is
// capped so a user whose rhythm changes is followed within a few thousand keys
// instead of being averaged against an ever-growing past.
constexpr double kEvidenceCap = 1200.0;

double clampDouble(double value, double lo, double hi) noexcept {
    if (std::isnan(value)) {
        return lo;
    }
    return std::clamp(value, lo, hi);
}

void sanitizeProfile(AiProfile& p) noexcept {
    p.meanIkiMs = clampDouble(p.meanIkiMs, kMinIkiMs, kMaxIkiMs);
    p.stddevIkiMs = clampDouble(p.stddevIkiMs, kMinStddevMs, kMaxStddevMs);
    p.avgBurstLength = clampDouble(p.avgBurstLength, kMinBurst, kMaxBurst);
    p.pauseThresholdMs = clampDouble(p.pauseThresholdMs, kMinPauseMs, kMaxPauseMs);
    p.errorRate = clampDouble(p.errorRate, 0.0, 0.35);
    p.backspaceRecoveryMs = clampDouble(p.backspaceRecoveryMs, kMinRecoveryMs, kMaxRecoveryMs);
    p.toneDelayMs = clampDouble(p.toneDelayMs, kMinToneMs, kMaxToneMs);
    p.speedCurveSlope = clampDouble(p.speedCurveSlope, -kMaxSlopeMsPer100, kMaxSlopeMsPer100);
}

double meanOf(const std::vector<double>& values) noexcept {
    if (values.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (double v : values) {
        sum += v;
    }
    return sum / static_cast<double>(values.size());
}

} // namespace

//===========================================================================
// AiRacer
//===========================================================================
void AiRacer::reset(std::u32string_view passage, const AiProfile& profile,
                    const AiRaceConfig& config) {
    m_passageLength = passage.size();
    m_charIndex = 0;
    m_elapsedSec = 0.0;
    m_finishTimeSec = 0.0;
    m_typos = 0;
    m_finished = false;
    m_scheduleMs.clear();

    if (m_passageLength == 0) {
        m_finished = true;
        return;
    }

    AiProfile sane = profile;
    sanitizeProfile(sane);

    const std::uint32_t seed = (config.seed != 0) ? config.seed
                                                 : static_cast<std::uint32_t>(sane.sampleCount) ^ 0x51ED2701u;
    std::mt19937 rng(seed);
    std::normal_distribution<double> ikiDist(sane.meanIkiMs, std::max(kMinStddevMs, sane.stddevIkiMs));
    std::uniform_real_distribution<double> uniform(0.0, 1.0);

    const double speed = config.effectiveSpeedMultiplier();
    const double typoRate = config.allowTypos ? clampDouble(sane.errorRate * config.typoFactor, 0.0, 0.4)
                                              : 0.0;
    m_scheduleMs.reserve(m_passageLength + 8);
    double cumulativeMs = 0.0;

    for (std::size_t i = 0; i < m_passageLength; ++i) {
        double delayMs = ikiDist(rng);
        // Fatigue: the rival slows down the longer the run gets, exactly like
        // the measured speed curve of the player.
        delayMs += sane.speedCurveSlope * (static_cast<double>(i) / 100.0);
        delayMs = std::clamp(delayMs, kMinIkiMs, kMaxIkiMs);

        if (passage[i] == U' ' || passage[i] == U'\n') {
            delayMs += sane.pauseThresholdMs * (0.8 + 0.4 * uniform(rng));
        }
        delayMs /= speed;   // aggression: >1 finishes sooner

        if (uniform(rng) < typoRate) {
            // A natural typo costs the recovery time before the correct key.
            ++m_typos;
            delayMs += sane.backspaceRecoveryMs * (0.9 + 0.2 * uniform(rng)) / speed;
        }

        cumulativeMs += delayMs;
        m_scheduleMs.push_back(static_cast<std::uint32_t>(cumulativeMs));
    }
    m_finishTimeSec = cumulativeMs / 1000.0;
}

void AiRacer::update(double dt) {
    if (m_finished) {
        return;
    }
    if (dt > 0.0) {
        m_elapsedSec += (dt > 0.05) ? 0.05 : dt;   // same frame clamp as the games
    }
    const double elapsedMs = m_elapsedSec * 1000.0;
    // Binary search: the schedule is monotonically increasing.
    const auto it = std::upper_bound(m_scheduleMs.begin(), m_scheduleMs.end(),
                                     static_cast<std::uint32_t>(elapsedMs));
    m_charIndex = static_cast<std::size_t>(it - m_scheduleMs.begin());
    if (m_charIndex >= m_passageLength) {
        m_charIndex = m_passageLength;
        m_finished = true;
    }
}

double AiRacer::getProgress() const noexcept {
    if (m_passageLength == 0) {
        return 1.0;
    }
    return static_cast<double>(m_charIndex) / static_cast<double>(m_passageLength);
}

double AiRacer::getWpm() const noexcept {
    if (m_elapsedSec <= 0.0005) {
        return 0.0;
    }
    return (static_cast<double>(m_charIndex) / 5.0) / (m_elapsedSec / 60.0);
}

//===========================================================================
// AiRivalEngine
//===========================================================================
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
    std::lock_guard<std::mutex> lock(m_profileMutex);
    m_profile.reset();
    m_ghost.clear();
    m_ring.reset();
    m_lastObservedStamp.store(0, std::memory_order_relaxed);
    m_observedCount.store(0, std::memory_order_relaxed);
    m_trainedIndex.store(0, std::memory_order_relaxed);
}

void AiRivalEngine::observeKeystroke(char32_t ch, std::uint64_t timestampUs, bool isBackspace,
                                     bool isToneKey) noexcept {
    if (!m_optIn.load(std::memory_order_relaxed)) {
        return;
    }
    // Pure atomics: no lock, no allocation, no branch on shared state.
    m_ring.push(timestampUs, packKeyPayload(static_cast<std::uint32_t>(ch), isBackspace, false,
                                            isToneKey));
    m_lastObservedStamp.store(timestampUs, std::memory_order_relaxed);
    m_observedCount.fetch_add(1, std::memory_order_relaxed);
}

void AiRivalEngine::trainBatch() {
    // Only the samples observed since the previous batch are fitted, so each
    // keystroke is learned exactly once no matter how often the UI refreshes.
    const std::uint64_t total = m_ring.head();
    std::uint64_t first = m_trainedIndex.load(std::memory_order_relaxed);
    const std::uint64_t oldest = (total > SeqRing::kCapacity) ? (total - SeqRing::kCapacity) : 0;
    if (first < oldest) {
        first = oldest;   // the beginning of the window was silently dropped
    }
    if (first >= total) {
        return;           // nothing new to learn
    }
    m_trainedIndex.store(total, std::memory_order_release);

    std::array<std::uint64_t, SeqRing::kCapacity> stamps{};
    std::array<std::uint64_t, SeqRing::kCapacity> payloads{};
    std::size_t count = 0;
    for (std::uint64_t index = first; index < total && count < SeqRing::kCapacity; ++index) {
        std::uint64_t stamp = 0;
        std::uint64_t payload = 0;
        if (m_ring.read(index, stamp, payload)) {
            stamps[count] = stamp;
            payloads[count] = payload;
            ++count;
        }
    }
    if (count < 5) {
        return;
    }

    std::vector<double> ikis;
    std::vector<double> toneDelays;
    std::vector<double> recoveryDelays;
    std::vector<std::uint32_t> bursts;
    std::vector<double> firstHalfIki;
    std::vector<double> secondHalfIki;
    ikis.reserve(count);
    toneDelays.reserve(count / 4 + 1);
    recoveryDelays.reserve(count / 4 + 1);

    std::uint32_t backspaces = 0;
    std::uint32_t currentBurst = 0;
    std::uint64_t previousStamp = 0;
    std::size_t pauseCount = 0;
    double pauseSum = 0.0;

    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t ch = 0;
        bool isBackspace = false;
        bool isMark = false;
        bool isTone = false;
        unpackKeyPayload(payloads[i], ch, isBackspace, isMark, isTone);
        (void)ch;
        (void)isMark;

        if (isBackspace) {
            ++backspaces;
        }

        if (i == 0) {
            previousStamp = stamps[i];
            continue;
        }
        const std::uint64_t stamp = stamps[i];
        if (stamp < previousStamp) {
            continue;   // out-of-order sample (clock adjustment): skip
        }
        const double diffMs = static_cast<double>(stamp - previousStamp) / 1000.0;
        previousStamp = stamp;

        if (diffMs > 10.0 && diffMs < 5000.0) {
            ikis.push_back(diffMs);
            (i < count / 2 ? firstHalfIki : secondHalfIki).push_back(diffMs);
        }
        if (diffMs > 500.0) {
            // Pause: measured, and fed back into the learned pause threshold.
            ++pauseCount;
            pauseSum += diffMs;
            if (currentBurst > 0) {
                bursts.push_back(currentBurst);
                currentBurst = 0;
            }
        } else {
            ++currentBurst;
        }
        if (isTone) {
            toneDelays.push_back(diffMs);
        }
        if (isBackspace) {
            recoveryDelays.push_back(diffMs);   // latency to notice + correct
        }
    }
    if (currentBurst > 0) {
        bursts.push_back(currentBurst);
    }

    std::lock_guard<std::mutex> lock(m_profileMutex);

    if (ikis.empty()) {
        return;   // no usable interval in this batch: nothing to learn from
    }

    // Blend weight = share of the evidence this batch contributes. The first
    // real batch therefore lands *on* the measured rhythm instead of 30 % of
    // the way towards it from the factory default.
    const double oldEvidence = std::min<double>(static_cast<double>(m_profile.sampleCount),
                                                kEvidenceCap);
    const double newEvidence = static_cast<double>(ikis.size());
    const double w = (oldEvidence + newEvidence > 0.0)
                         ? (newEvidence / (oldEvidence + newEvidence))
                         : 1.0;
    const double alpha = w;

    const double mean = meanOf(ikis);
    double variance = 0.0;
    for (double d : ikis) {
        variance += (d - mean) * (d - mean);
    }
    const double stddev = std::sqrt(variance / static_cast<double>(ikis.size()));
    m_profile.meanIkiMs = (1.0 - alpha) * m_profile.meanIkiMs + alpha * mean;
    m_profile.stddevIkiMs = (1.0 - alpha) * m_profile.stddevIkiMs + alpha * stddev;
    m_profile.sampleCount += ikis.size();

    // NEW: speed curve (fatigue) — previously never learned.
    if (firstHalfIki.size() >= 3 && secondHalfIki.size() >= 3) {
        const double slopePerChar = (meanOf(secondHalfIki) - meanOf(firstHalfIki)) /
                                    std::max(1.0, static_cast<double>(count) / 2.0);
        m_profile.speedCurveSlope = (1.0 - alpha) * m_profile.speedCurveSlope +
                                    alpha * (slopePerChar * 100.0);
    }

    // NEW: pause threshold follows the observed pauses instead of staying at the
    // factory default for the lifetime of the process.
    if (pauseCount > 0) {
        const double observedPause = pauseSum / static_cast<double>(pauseCount);
        m_profile.pauseThresholdMs = (1.0 - alpha) * m_profile.pauseThresholdMs +
                                     alpha * observedPause;
    }
    if (!bursts.empty()) {
        double sum = 0.0;
        for (std::uint32_t b : bursts) {
            sum += b;
        }
        m_profile.avgBurstLength =
            (1.0 - alpha) * m_profile.avgBurstLength +
            alpha * (sum / static_cast<double>(bursts.size()));
    }
    if (!toneDelays.empty()) {
        m_profile.toneDelayMs = (1.0 - alpha) * m_profile.toneDelayMs + alpha * meanOf(toneDelays);
    }
    if (!recoveryDelays.empty()) {
        m_profile.backspaceRecoveryMs = (1.0 - alpha) * m_profile.backspaceRecoveryMs +
                                        alpha * meanOf(recoveryDelays);
    }
    if (count > 0) {
        const double observedErrorRate = static_cast<double>(backspaces) / static_cast<double>(count);
        m_profile.errorRate = std::clamp((1.0 - alpha) * m_profile.errorRate +
                                             alpha * observedErrorRate,
                                         0.0, 0.35);
    }

    sanitizeProfile(m_profile);
}

AiProfile AiRivalEngine::getProfile() const {
    std::lock_guard<std::mutex> lock(m_profileMutex);
    return m_profile;
}

std::vector<SimulatedKeystroke> AiRivalEngine::simulateTypingRun(std::u32string_view targetText,
                                                                 std::uint32_t seed) const {
    AiProfile profile;
    {
        std::lock_guard<std::mutex> lock(m_profileMutex);
        profile = m_profile;
    }
    sanitizeProfile(profile);

    const std::uint32_t effectiveSeed = (seed != 0) ? seed : 0xACE1337u;
    std::mt19937 rng(effectiveSeed);
    std::normal_distribution<double> ikiDist(profile.meanIkiMs,
                                             std::max(kMinStddevMs, profile.stddevIkiMs));
    std::uniform_real_distribution<double> uniform(0.0, 1.0);

    std::vector<SimulatedKeystroke> run;
    run.reserve(targetText.size() * 2 + 4);

    for (std::size_t i = 0; i < targetText.size(); ++i) {
        const char32_t targetCh = targetText[i];
        double delay = ikiDist(rng);
        delay += profile.speedCurveSlope * (static_cast<double>(i) / 100.0);
        delay = std::clamp(delay, kMinIkiMs, kMaxIkiMs);

        if (targetCh == U' ' || targetCh == U'\n') {
            delay += profile.pauseThresholdMs * (0.8 + 0.4 * uniform(rng));
        }

        if (uniform(rng) < profile.errorRate && targetCh > 32) {
            // A plausible wrong key: the character next to it on a QWERTY row
            // (the old code used `targetCh + 1`, which produced "{", "|" and
            // other characters no human ever mistypes by accident).
            char32_t typoCh = targetCh;
            if (targetCh >= U'a' && targetCh <= U'z') {
                typoCh = (targetCh == U'a') ? U's'
                        : (targetCh == U'z') ? U'x'
                                             : static_cast<char32_t>(targetCh - 1);
            } else if (targetCh >= U'A' && targetCh <= U'Z') {
                typoCh = static_cast<char32_t>(targetCh + 32);
            }
            run.push_back({typoCh, static_cast<std::uint32_t>(std::clamp(delay, 20.0, 900.0)),
                           true, false});

            const std::uint32_t recovery = static_cast<std::uint32_t>(std::clamp(
                profile.backspaceRecoveryMs * (0.9 + 0.2 * uniform(rng)), 20.0, 1500.0));
            run.push_back({U'\b', recovery, false, true});

            delay = ikiDist(rng);
            delay = std::clamp(delay, kMinIkiMs, kMaxIkiMs);
        }

        run.push_back({targetCh, static_cast<std::uint32_t>(std::clamp(delay, 20.0, 900.0)),
                       false, false});
    }
    return run;
}

void AiRivalEngine::recordGhostRun(const std::vector<std::uint32_t>& charTimeOffsetsMs) {
    std::lock_guard<std::mutex> lock(m_profileMutex);
    m_ghost = charTimeOffsetsMs;
}

std::vector<std::uint32_t> AiRivalEngine::getYesterdayGhost() const {
    std::lock_guard<std::mutex> lock(m_profileMutex);
    return m_ghost;
}

std::string AiRivalEngine::serializeProfile() const {
    std::lock_guard<std::mutex> lock(m_profileMutex);
    std::ostringstream ss;
    ss << "KIEEKEY_AI_PROFILE_V2\n";
    ss << "sampleCount=" << m_profile.sampleCount << "\n";
    ss << "meanIkiMs=" << m_profile.meanIkiMs << "\n";
    ss << "stddevIkiMs=" << m_profile.stddevIkiMs << "\n";
    ss << "avgBurstLength=" << m_profile.avgBurstLength << "\n";
    ss << "pauseThresholdMs=" << m_profile.pauseThresholdMs << "\n";
    ss << "errorRate=" << m_profile.errorRate << "\n";
    ss << "backspaceRecoveryMs=" << m_profile.backspaceRecoveryMs << "\n";
    ss << "toneDelayMs=" << m_profile.toneDelayMs << "\n";
    ss << "speedCurveSlope=" << m_profile.speedCurveSlope << "\n";
    ss << "ghostCount=" << m_ghost.size() << "\n";
    ss << "ghost=";
    for (std::size_t i = 0; i < m_ghost.size(); ++i) {
        if (i > 0) {
            ss << ',';
        }
        ss << m_ghost[i];
    }
    ss << "\n";
    return ss.str();
}

bool AiRivalEngine::deserializeProfile(std::string_view data) {
    if (data.rfind("KIEEKEY_AI_PROFILE_V1", 0) != 0 &&
        data.rfind("KIEEKEY_AI_PROFILE_V2", 0) != 0) {
        return false;
    }

    AiProfile profile{};
    std::vector<std::uint32_t> ghost;
    bool sawAnyField = false;

    std::istringstream ss{std::string(data)};
    std::string line;
    std::getline(ss, line);   // header
    while (std::getline(ss, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        try {
            if (key == "sampleCount") { profile.sampleCount = std::stoull(value); sawAnyField = true; }
            else if (key == "meanIkiMs") { profile.meanIkiMs = std::stod(value); sawAnyField = true; }
            else if (key == "stddevIkiMs") { profile.stddevIkiMs = std::stod(value); sawAnyField = true; }
            else if (key == "avgBurstLength") { profile.avgBurstLength = std::stod(value); sawAnyField = true; }
            else if (key == "pauseThresholdMs") { profile.pauseThresholdMs = std::stod(value); sawAnyField = true; }
            else if (key == "errorRate") { profile.errorRate = std::stod(value); sawAnyField = true; }
            else if (key == "backspaceRecoveryMs") { profile.backspaceRecoveryMs = std::stod(value); sawAnyField = true; }
            else if (key == "toneDelayMs") { profile.toneDelayMs = std::stod(value); sawAnyField = true; }
            else if (key == "speedCurveSlope") { profile.speedCurveSlope = std::stod(value); sawAnyField = true; }
            else if (key == "ghost") {
                std::istringstream values(value);
                std::string item;
                while (std::getline(values, item, ',')) {
                    if (item.empty() || ghost.size() >= 4096) {
                        continue;
                    }
                    ghost.push_back(static_cast<std::uint32_t>(std::stoul(item)));
                }
            }
        } catch (...) {
            // A single unparsable field must not discard the whole profile.
        }
    }

    if (!sawAnyField) {
        return false;
    }

    sanitizeProfile(profile);

    std::lock_guard<std::mutex> lock(m_profileMutex);
    m_profile = profile;
    m_ghost = std::move(ghost);
    return true;
}

AiRacer AiRivalEngine::makeRacer(std::u32string_view passage, const AiRaceConfig& config) const {
    AiRacer racer;
    AiProfile profile;
    {
        std::lock_guard<std::mutex> lock(m_profileMutex);
        profile = m_profile;
    }
    racer.reset(passage, profile, config);
    return racer;
}

} // namespace ok::ai
