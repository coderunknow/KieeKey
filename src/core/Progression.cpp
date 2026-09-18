//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: src/core/Progression.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "Progression.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace ok::progression {

namespace {

std::uint32_t computeChecksum(std::string_view s) noexcept {
    // 32-bit FNV-1a hash
    std::uint32_t hash = 2166136261u;
    for (char c : s) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

const AchievementInfo kAchievementTable[] = {
    {AchievementId::FirstKey, "Khởi đầu", "Gõ ký tự đầu tiên cùng KieeKey", 50},
    {AchievementId::FirstWord, "Từ ngữ đầu tiên", "Hoàn thành một từ tiếng Việt", 100},
    {AchievementId::Speedster50Wpm, "Tốc độ 50 WPM", "Đạt tốc độ gõ 50 WPM", 250},
    {AchievementId::Speedster80Wpm, "Tốc độ 80 WPM", "Đạt tốc độ gõ 80 WPM", 500},
    {AchievementId::Century100Wpm, "Thần tốc 100 WPM", "Vượt mốc 100 WPM", 1000},
    {AchievementId::TenThousandKeys, "Bàn tay dẻo dai", "Gõ tích lũy 10,000 phím", 500},
    {AchievementId::HundredThousandKeys, "Chiến binh bàn phím", "Gõ tích lũy 100,000 phím", 2000},
    {AchievementId::MillionKeys, "Bậc thầy triệu phím", "Gõ tích lũy 1,000,000 phím", 10000},
    {AchievementId::SnakeNovice, "Rắn săn mồi tập sự", "Đạt 500 điểm trong game Snake", 200},
    {AchievementId::SnakeMaster, "Vua rắn", "Đạt 2,000 điểm trong game Snake", 1000},
    {AchievementId::TetrisLover, "Tín đồ xếp gạch", "Đạt 5,000 điểm trong game Tetris", 300},
    {AchievementId::TetrisGrandMaster, "Đại kiện tướng Tetris", "Đạt 25,000 điểm trong game Tetris", 1500},
    {AchievementId::FirstFish, "Mẻ cá đầu tay", "Câu thành công con cá đầu tiên", 150},
    {AchievementId::LegendaryAngler, "Ngư phủ huyền thoại", "Bắt được cá Legendary", 2500},
    {AchievementId::RhythmStreak50, "Nhịp điệu cuồng nhiệt", "Đạt 50 combo Perfect trong Rhythm Typing", 800},
    {AchievementId::NoMistakeCenturion, "Bất khả xâm phạm", "Đạt 100 combo không mắc lỗi nào", 1200},
};

constexpr std::uint64_t kMaxXp = 1'000'000'000'000ULL;   // 1e12: level ≈ 46415

} // namespace

constexpr std::uint32_t kAchievementCount = static_cast<std::uint32_t>(AchievementId::Count);

void ProgressionStats::reset() noexcept {
    totalCharacters = 0;
    totalWords = 0;
    totalKeystrokes = 0;
    totalSessions = 0;
    typingTimeSeconds = 0;
    bestWpm = 0.0;
    bestAccuracy = 0.0;
    currentStreakDays = 0;
    maxStreakDays = 0;
    totalXp = 0;
    currentLevel = 1;
    snakeHighScore = 0;
    tetrisHighScore = 0;
    fishCaughtCount = 0;
    legendaryFishCount = 0;
    typingRaceBestWpm = 0.0;
    wasdRaceHighScore = 0;
    rhythmHighScore = 0;
    noMistakeMaxCombo = 0;
    lastActiveDayIndex = 0;
    achievementsUnlocked = 0;
}

ProgressionEngine& ProgressionEngine::instance() noexcept {
    static ProgressionEngine s_instance;
    return s_instance;
}

ProgressionEngine::ProgressionEngine() {
    m_stats.reset();
}

ProgressionStats ProgressionEngine::getStats() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

//===========================================================================
// Level curve (unchanged formulas — they are part of the published spec)
//===========================================================================
std::uint32_t ProgressionEngine::calculateLevel(std::uint64_t xp) noexcept {
    if (xp == 0) {
        return 1;
    }
    if (xp > kMaxXp) {
        xp = kMaxXp;
    }
    std::uint32_t low = 1;
    std::uint32_t high = 100000;
    std::uint32_t ans = 1;
    while (low <= high) {
        const std::uint32_t mid = low + (high - low) / 2;
        if (xpRequiredForLevel(mid) <= xp) {
            ans = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return ans;
}

std::uint64_t ProgressionEngine::xpRequiredForLevel(std::uint32_t level) noexcept {
    if (level <= 1) {
        return 0;
    }
    const double base = static_cast<double>(level - 1);
    const double required = std::floor(100.0 * std::pow(base, 1.5));
    if (required >= static_cast<double>(kMaxXp)) {
        return kMaxXp;
    }
    return static_cast<std::uint64_t>(required);
}

std::uint64_t ProgressionEngine::xpRemainingToNextLevel(std::uint64_t currentXp) noexcept {
    const std::uint32_t curLevel = calculateLevel(currentXp);
    const std::uint64_t nextReq = xpRequiredForLevel(curLevel + 1);
    return (nextReq > currentXp) ? (nextReq - currentXp) : 0;
}

std::uint64_t ProgressionEngine::xpIntoCurrentLevel(std::uint64_t currentXp) noexcept {
    const std::uint32_t curLevel = calculateLevel(currentXp);
    const std::uint64_t base = xpRequiredForLevel(curLevel);
    return (currentXp > base) ? (currentXp - base) : 0;
}

std::uint64_t ProgressionEngine::xpSpanOfCurrentLevel(std::uint64_t currentXp) noexcept {
    const std::uint32_t curLevel = calculateLevel(currentXp);
    const std::uint64_t base = xpRequiredForLevel(curLevel);
    const std::uint64_t next = xpRequiredForLevel(curLevel + 1);
    return (next > base) ? (next - base) : 0;
}

//===========================================================================
// Achievements & level bookkeeping
//===========================================================================
void ProgressionEngine::checkAchievementsLocked() {
    auto unlock = [this](AchievementId id) {
        const std::uint64_t mask = 1ULL << static_cast<std::uint16_t>(id);
        if ((m_stats.achievementsUnlocked & mask) == 0) {
            m_stats.achievementsUnlocked |= mask;
            m_stats.totalXp += getAchievementInfo(id).xpReward;
        }
    };

    if (m_stats.totalKeystrokes >= 1) { unlock(AchievementId::FirstKey); }
    if (m_stats.totalWords >= 1) { unlock(AchievementId::FirstWord); }
    if (m_stats.bestWpm >= 50.0) { unlock(AchievementId::Speedster50Wpm); }
    if (m_stats.bestWpm >= 80.0) { unlock(AchievementId::Speedster80Wpm); }
    if (m_stats.bestWpm >= 100.0) { unlock(AchievementId::Century100Wpm); }
    if (m_stats.totalKeystrokes >= 10000) { unlock(AchievementId::TenThousandKeys); }
    if (m_stats.totalKeystrokes >= 100000) { unlock(AchievementId::HundredThousandKeys); }
    if (m_stats.totalKeystrokes >= 1000000) { unlock(AchievementId::MillionKeys); }
    if (m_stats.snakeHighScore >= 500) { unlock(AchievementId::SnakeNovice); }
    if (m_stats.snakeHighScore >= 2000) { unlock(AchievementId::SnakeMaster); }
    if (m_stats.tetrisHighScore >= 5000) { unlock(AchievementId::TetrisLover); }
    if (m_stats.tetrisHighScore >= 25000) { unlock(AchievementId::TetrisGrandMaster); }
    if (m_stats.fishCaughtCount >= 1) { unlock(AchievementId::FirstFish); }
    if (m_stats.legendaryFishCount >= 1) { unlock(AchievementId::LegendaryAngler); }
    if (m_stats.noMistakeMaxCombo >= 100) { unlock(AchievementId::NoMistakeCenturion); }
}

void ProgressionEngine::applyLevelLocked() {
    const std::uint32_t level = calculateLevel(m_stats.totalXp);
    if (level > m_stats.currentLevel) {
        m_stats.currentLevel = level;
        m_pendingLevelUp.store(level, std::memory_order_release);
    } else {
        m_stats.currentLevel = level;
    }
}

//===========================================================================
// Hot path (hook thread)
//===========================================================================
void ProgressionEngine::recordKeystroke(bool isBackspace, std::uint32_t chars,
                                        std::uint32_t words) noexcept {
    if (isBackspace) {
        m_pendingBackspaces.fetch_add(1, std::memory_order_relaxed);
        m_pendingKeystrokes.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    m_pendingChars.fetch_add(chars, std::memory_order_relaxed);
    if (words > 0) {
        m_pendingWords.fetch_add(words, std::memory_order_relaxed);
    }
    m_pendingKeystrokes.fetch_add(1, std::memory_order_relaxed);
}

void ProgressionEngine::recordActiveTimeMs(std::uint64_t ms) noexcept {
    m_pendingTimeMs.fetch_add(ms, std::memory_order_relaxed);
}

std::uint64_t ProgressionEngine::pendingKeystrokes() const noexcept {
    return m_pendingKeystrokes.load(std::memory_order_relaxed);
}

void ProgressionEngine::flushStats() noexcept {
    const std::uint64_t chars = m_pendingChars.exchange(0, std::memory_order_acq_rel);
    const std::uint64_t words = m_pendingWords.exchange(0, std::memory_order_acq_rel);
    const std::uint64_t keys = m_pendingKeystrokes.exchange(0, std::memory_order_acq_rel);
    const std::uint64_t backspaces = m_pendingBackspaces.exchange(0, std::memory_order_acq_rel);
    const std::uint64_t timeMs = m_pendingTimeMs.exchange(0, std::memory_order_acq_rel);

    if (chars == 0 && words == 0 && keys == 0 && timeMs == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.totalCharacters += chars;
    m_stats.totalWords += words;
    m_stats.totalKeystrokes += keys;
    // Accumulate whole seconds, carrying the sub-second remainder forward so
    // many small flushes add up to exactly the time that was actually typed.
    const std::uint64_t carriedMs = timeMs + m_timeCarryMs;
    m_stats.typingTimeSeconds += carriedMs / 1000;
    m_timeCarryMs = carriedMs % 1000;

    // XP: 1 per character, +50 % when the accumulated accuracy is >= 95 %.
    const std::uint64_t typedKeys = chars + backspaces;
    const bool highAccuracy = (typedKeys > 0) && (chars * 100 >= typedKeys * 95);
    std::uint64_t earnedXp = chars;
    if (highAccuracy) {
        earnedXp += chars / 2;
    }
    m_stats.totalXp = (m_stats.totalXp + earnedXp > kMaxXp) ? kMaxXp : m_stats.totalXp + earnedXp;

    checkAchievementsLocked();
    applyLevelLocked();
}

//===========================================================================
// Session accounting (UI thread)
//===========================================================================
void ProgressionEngine::recordTypingSession(std::uint64_t chars, std::uint64_t words,
                                            std::uint64_t keystrokes, std::uint64_t durationSeconds,
                                            double wpm, double accuracy) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.totalCharacters += chars;
    m_stats.totalWords += words;
    m_stats.totalKeystrokes += keystrokes;
    m_stats.totalSessions += 1;
    m_stats.typingTimeSeconds += durationSeconds;

    if (wpm > m_stats.bestWpm) { m_stats.bestWpm = wpm; }
    if (accuracy > m_stats.bestAccuracy) { m_stats.bestAccuracy = accuracy; }

    std::uint64_t earnedXp = chars;
    if (accuracy >= 95.0) { earnedXp += (chars / 2); }
    m_stats.totalXp = (m_stats.totalXp + earnedXp > kMaxXp) ? kMaxXp : m_stats.totalXp + earnedXp;

    checkAchievementsLocked();
    applyLevelLocked();
}

void ProgressionEngine::endSession(double wpm, double accuracy) noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.totalSessions += 1;
    if (wpm > m_stats.bestWpm) { m_stats.bestWpm = wpm; }
    if (accuracy > m_stats.bestAccuracy) { m_stats.bestAccuracy = accuracy; }
    checkAchievementsLocked();
    applyLevelLocked();
}

void ProgressionEngine::addXp(std::uint64_t xp) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.totalXp = (m_stats.totalXp + xp > kMaxXp) ? kMaxXp : m_stats.totalXp + xp;
    checkAchievementsLocked();
    applyLevelLocked();
}

//===========================================================================
// Minigame results
//===========================================================================
void ProgressionEngine::recordSnakeScore(std::int64_t score) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (score > m_stats.snakeHighScore) { m_stats.snakeHighScore = score; }
    if (score > 0) { m_stats.totalXp += static_cast<std::uint64_t>(score / 5); }
    checkAchievementsLocked();
    applyLevelLocked();
}

void ProgressionEngine::recordTetrisScore(std::int64_t score) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (score > m_stats.tetrisHighScore) { m_stats.tetrisHighScore = score; }
    if (score > 0) { m_stats.totalXp += static_cast<std::uint64_t>(score / 10); }
    checkAchievementsLocked();
    applyLevelLocked();
}

void ProgressionEngine::recordFishCaught(bool isLegendary) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.fishCaughtCount += 1;
    if (isLegendary) {
        m_stats.legendaryFishCount += 1;
        m_stats.totalXp += 500;
    } else {
        m_stats.totalXp += 50;
    }
    checkAchievementsLocked();
    applyLevelLocked();
}

void ProgressionEngine::recordTypingRace(double wpm) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (wpm > m_stats.typingRaceBestWpm) { m_stats.typingRaceBestWpm = wpm; }
    m_stats.totalXp += static_cast<std::uint64_t>(std::max(0.0, wpm) * 2.0);
    checkAchievementsLocked();
    applyLevelLocked();
}

void ProgressionEngine::recordWasdRaceScore(std::int64_t score) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (score > m_stats.wasdRaceHighScore) { m_stats.wasdRaceHighScore = score; }
    if (score > 0) { m_stats.totalXp += static_cast<std::uint64_t>(score / 10); }
    checkAchievementsLocked();
    applyLevelLocked();
}

void ProgressionEngine::recordRhythmScore(std::int64_t score, std::uint32_t combo) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (score > m_stats.rhythmHighScore) { m_stats.rhythmHighScore = score; }
    if (combo >= 50) {
        const std::uint64_t mask = 1ULL << static_cast<std::uint16_t>(AchievementId::RhythmStreak50);
        if ((m_stats.achievementsUnlocked & mask) == 0) {
            m_stats.achievementsUnlocked |= mask;
            m_stats.totalXp += getAchievementInfo(AchievementId::RhythmStreak50).xpReward;
        }
    }
    if (score > 0) { m_stats.totalXp += static_cast<std::uint64_t>(score / 20); }
    checkAchievementsLocked();
    applyLevelLocked();
}

void ProgressionEngine::recordNoMistakeCombo(std::uint64_t combo) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (combo > m_stats.noMistakeMaxCombo) { m_stats.noMistakeMaxCombo = combo; }
    checkAchievementsLocked();
    applyLevelLocked();
}

void ProgressionEngine::recordArcadeRun(int gameTypeId, std::int64_t score,
                                        std::uint32_t maxCombo, double wpm) {
    switch (gameTypeId) {
        case 1: recordSnakeScore(score); break;
        case 2: recordTetrisScore(score); break;
        case 3:
            // Fishing reports "score", not a catch count; XP only.
            if (score > 0) { addXp(static_cast<std::uint64_t>(score / 20)); }
            break;
        case 4: recordTypingRace(wpm); break;
        case 5: recordWasdRaceScore(score); break;
        case 6: recordRhythmScore(score, maxCombo); break;
        case 7: recordNoMistakeCombo(maxCombo); break;
        case 8: break;   // Flexing Mode is explicitly excluded from progression
        default: break;
    }
}

bool ProgressionEngine::isAchievementUnlocked(AchievementId id) const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::uint64_t mask = 1ULL << static_cast<std::uint16_t>(id);
    return (m_stats.achievementsUnlocked & mask) != 0;
}

AchievementInfo ProgressionEngine::getAchievementInfo(AchievementId id) noexcept {
    const auto idx = static_cast<std::size_t>(id);
    if (idx < kAchievementCount) {
        return kAchievementTable[idx];
    }
    return {AchievementId::FirstKey, "Unknown", "Unknown", 0};
}

std::vector<AchievementInfo> ProgressionEngine::getUnlockedAchievements() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AchievementInfo> res;
    for (std::size_t i = 0; i < kAchievementCount; ++i) {
        if (m_stats.achievementsUnlocked & (1ULL << i)) {
            res.push_back(kAchievementTable[i]);
        }
    }
    return res;
}

std::vector<AchievementInfo> ProgressionEngine::getAllAchievements() {
    std::vector<AchievementInfo> res;
    res.reserve(kAchievementCount);
    for (std::size_t i = 0; i < kAchievementCount; ++i) {
        res.push_back(kAchievementTable[i]);
    }
    return res;
}

//===========================================================================
// Day streaks
//===========================================================================
std::uint32_t ProgressionEngine::dayIndexFromUnixSeconds(std::uint64_t unixSeconds) noexcept {
    return static_cast<std::uint32_t>(unixSeconds / 86400ULL);
}

void ProgressionEngine::touchDailyStreak(std::uint32_t dayIndex) noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (dayIndex == 0) {
        return;
    }
    if (m_stats.lastActiveDayIndex == dayIndex) {
        return;
    }
    if (m_stats.lastActiveDayIndex != 0 && dayIndex == m_stats.lastActiveDayIndex + 1) {
        ++m_stats.currentStreakDays;
    } else {
        m_stats.currentStreakDays = 1;
    }
    m_stats.lastActiveDayIndex = dayIndex;
    m_stats.maxStreakDays = std::max(m_stats.maxStreakDays, m_stats.currentStreakDays);
}

bool ProgressionEngine::consumeLevelUp(std::uint32_t& newLevel) noexcept {
    const std::uint32_t level = m_pendingLevelUp.exchange(0, std::memory_order_acq_rel);
    if (level == 0) {
        return false;
    }
    newLevel = level;
    return true;
}

//===========================================================================
// Serialization
//===========================================================================
std::string ProgressionEngine::serialize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream ss;
    ss << "KIEEKEY_PROGRESSION_V1\n";
    ss << "totalCharacters=" << m_stats.totalCharacters << "\n";
    ss << "totalWords=" << m_stats.totalWords << "\n";
    ss << "totalKeystrokes=" << m_stats.totalKeystrokes << "\n";
    ss << "totalSessions=" << m_stats.totalSessions << "\n";
    ss << "typingTimeSeconds=" << m_stats.typingTimeSeconds << "\n";
    ss << "bestWpm=" << m_stats.bestWpm << "\n";
    ss << "bestAccuracy=" << m_stats.bestAccuracy << "\n";
    ss << "currentStreakDays=" << m_stats.currentStreakDays << "\n";
    ss << "maxStreakDays=" << m_stats.maxStreakDays << "\n";
    ss << "totalXp=" << m_stats.totalXp << "\n";
    ss << "currentLevel=" << m_stats.currentLevel << "\n";
    ss << "snakeHighScore=" << m_stats.snakeHighScore << "\n";
    ss << "tetrisHighScore=" << m_stats.tetrisHighScore << "\n";
    ss << "fishCaughtCount=" << m_stats.fishCaughtCount << "\n";
    ss << "legendaryFishCount=" << m_stats.legendaryFishCount << "\n";
    ss << "typingRaceBestWpm=" << m_stats.typingRaceBestWpm << "\n";
    ss << "wasdRaceHighScore=" << m_stats.wasdRaceHighScore << "\n";
    ss << "rhythmHighScore=" << m_stats.rhythmHighScore << "\n";
    ss << "noMistakeMaxCombo=" << m_stats.noMistakeMaxCombo << "\n";
    ss << "lastActiveDayIndex=" << m_stats.lastActiveDayIndex << "\n";
    ss << "achievementsUnlocked=" << m_stats.achievementsUnlocked << "\n";

    const std::string payload = ss.str();
    std::ostringstream out;
    out << payload;
    out << "checksum=" << computeChecksum(payload) << "\n";
    return out.str();
}

bool ProgressionEngine::applyField(ProgressionStats& stats, const std::string& key,
                                   const std::string& value) {
    try {
        if (key == "totalCharacters") stats.totalCharacters = std::stoull(value);
        else if (key == "totalWords") stats.totalWords = std::stoull(value);
        else if (key == "totalKeystrokes") stats.totalKeystrokes = std::stoull(value);
        else if (key == "totalSessions") stats.totalSessions = std::stoull(value);
        else if (key == "typingTimeSeconds") stats.typingTimeSeconds = std::stoull(value);
        else if (key == "bestWpm") stats.bestWpm = std::stod(value);
        else if (key == "bestAccuracy") stats.bestAccuracy = std::stod(value);
        else if (key == "currentStreakDays") stats.currentStreakDays = static_cast<std::uint32_t>(std::stoul(value));
        else if (key == "maxStreakDays") stats.maxStreakDays = static_cast<std::uint32_t>(std::stoul(value));
        else if (key == "totalXp") stats.totalXp = std::stoull(value);
        else if (key == "currentLevel") stats.currentLevel = static_cast<std::uint32_t>(std::stoul(value));
        else if (key == "snakeHighScore") stats.snakeHighScore = std::stoll(value);
        else if (key == "tetrisHighScore") stats.tetrisHighScore = std::stoll(value);
        else if (key == "fishCaughtCount") stats.fishCaughtCount = std::stoull(value);
        else if (key == "legendaryFishCount") stats.legendaryFishCount = std::stoull(value);
        else if (key == "typingRaceBestWpm") stats.typingRaceBestWpm = std::stod(value);
        else if (key == "wasdRaceHighScore") stats.wasdRaceHighScore = std::stoll(value);
        else if (key == "rhythmHighScore") stats.rhythmHighScore = std::stoll(value);
        else if (key == "noMistakeMaxCombo") stats.noMistakeMaxCombo = std::stoull(value);
        else if (key == "lastActiveDayIndex") stats.lastActiveDayIndex = static_cast<std::uint32_t>(std::stoul(value));
        else if (key == "achievementsUnlocked") stats.achievementsUnlocked = std::stoull(value);
        else return false;
    } catch (...) {
        return false;   // unparsable single field: ignore it, keep the rest
    }
    return true;
}

bool ProgressionEngine::parsePayload(std::string_view payload, ProgressionStats& out) {
    std::istringstream ss{std::string(payload)};
    std::string line;
    if (!std::getline(ss, line)) {
        return false;
    }
    if (line.rfind("KIEEKEY_PROGRESSION_V1", 0) != 0) {
        return false;
    }
    std::size_t parsedFields = 0;
    while (std::getline(ss, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        if (applyField(out, line.substr(0, eq), line.substr(eq + 1))) {
            ++parsedFields;
        }
    }
    return parsedFields > 0;
}

bool ProgressionEngine::deserialize(std::string_view data) {
    if (data.empty() || data.rfind("KIEEKEY_PROGRESSION_V1", 0) != 0) {
        return false;
    }
    const std::size_t cpos = data.rfind("checksum=");
    if (cpos == std::string_view::npos) {
        return false;
    }
    const std::string_view payload = data.substr(0, cpos);
    const std::string_view csumStr = data.substr(cpos + 9);
    std::uint32_t expected = 0;
    try {
        expected = static_cast<std::uint32_t>(std::stoul(std::string(csumStr)));
    } catch (...) {
        return false;
    }
    if (computeChecksum(payload) != expected) {
        return false;
    }

    ProgressionStats parsed{};
    if (!parsePayload(payload, parsed)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats = parsed;
    m_stats.currentLevel = calculateLevel(m_stats.totalXp);
    return true;
}

bool ProgressionEngine::salvageDeserialize(std::string_view data) {
    if (data.empty() || data.rfind("KIEEKEY_PROGRESSION_V1", 0) != 0) {
        return false;
    }
    std::string_view payload = data;
    const std::size_t cpos = data.rfind("checksum=");
    if (cpos != std::string_view::npos) {
        payload = data.substr(0, cpos);
    }
    ProgressionStats parsed{};
    if (!parsePayload(payload, parsed)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats = parsed;
    m_stats.currentLevel = calculateLevel(m_stats.totalXp);
    return true;
}

bool ProgressionEngine::saveToFile(std::string_view path) const {
    const std::string text = serialize();
    std::ofstream ofs(std::string(path), std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
        return false;
    }
    ofs.write(text.data(), static_cast<std::streamsize>(text.size()));
    return ofs.good();
}

bool ProgressionEngine::loadFromFile(std::string_view path, LoadOutcome* outcome) {
    std::ifstream ifs(std::string(path), std::ios::in);
    if (!ifs.is_open()) {
        if (outcome) { *outcome = LoadOutcome::FileMissing; }
        return false;
    }
    const std::string content((std::istreambuf_iterator<char>(ifs)),
                              std::istreambuf_iterator<char>());
    if (deserialize(content)) {
        if (outcome) { *outcome = LoadOutcome::Ok; }
        return true;
    }
    if (salvageDeserialize(content)) {
        if (outcome) { *outcome = LoadOutcome::Recovered; }
        return true;
    }
    if (outcome) { *outcome = LoadOutcome::Corrupt; }
    return false;
}

void ProgressionEngine::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.reset();
    m_pendingChars.store(0, std::memory_order_relaxed);
    m_pendingWords.store(0, std::memory_order_relaxed);
    m_pendingKeystrokes.store(0, std::memory_order_relaxed);
    m_pendingBackspaces.store(0, std::memory_order_relaxed);
    m_pendingTimeMs.store(0, std::memory_order_relaxed);
    m_pendingLevelUp.store(0, std::memory_order_relaxed);
    m_timeCarryMs = 0;
}

} // namespace ok::progression
