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
// File: src/core/Progression.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
#include "Progression.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace ok::progression {

namespace {

uint32_t computeChecksum(std::string_view s) noexcept {
    // 32-bit FNV-1a hash
    uint32_t hash = 2166136261u;
    for (char c : s) {
        hash ^= static_cast<uint8_t>(c);
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

} // namespace

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

uint32_t ProgressionEngine::calculateLevel(uint64_t xp) noexcept {
    if (xp == 0) { return 1; }
    // Binary search for exact monotonic level
    uint32_t low = 1;
    uint32_t high = 100000;
    uint32_t ans = 1;
    while (low <= high) {
        uint32_t mid = low + (high - low) / 2;
        if (xpRequiredForLevel(mid) <= xp) {
            ans = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return ans;
}

uint64_t ProgressionEngine::xpRequiredForLevel(uint32_t level) noexcept {
    if (level <= 1) { return 0; }
    double base = static_cast<double>(level - 1);
    return static_cast<uint64_t>(std::floor(100.0 * std::pow(base, 1.5)));
}

uint64_t ProgressionEngine::xpRemainingToNextLevel(uint64_t currentXp) noexcept {
    uint32_t curLevel = calculateLevel(currentXp);
    uint64_t nextReq = xpRequiredForLevel(curLevel + 1);
    return (nextReq > currentXp) ? (nextReq - currentXp) : 0;
}

void ProgressionEngine::checkAchievementsLocked() {
    auto unlock = [this](AchievementId id) {
        uint64_t mask = 1ULL << static_cast<uint16_t>(id);
        if (!(m_stats.achievementsUnlocked & mask)) {
            m_stats.achievementsUnlocked |= mask;
            auto info = getAchievementInfo(id);
            m_stats.totalXp += info.xpReward;
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

void ProgressionEngine::recordTypingSession(
    uint64_t chars,
    uint64_t words,
    uint64_t keystrokes,
    uint64_t durationSeconds,
    double wpm,
    double accuracy) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.totalCharacters += chars;
    m_stats.totalWords += words;
    m_stats.totalKeystrokes += keystrokes;
    m_stats.totalSessions += 1;
    m_stats.typingTimeSeconds += durationSeconds;

    if (wpm > m_stats.bestWpm) { m_stats.bestWpm = wpm; }
    if (accuracy > m_stats.bestAccuracy) { m_stats.bestAccuracy = accuracy; }

    uint64_t earnedXp = chars;
    if (accuracy >= 95.0) { earnedXp += (chars / 2); }
    m_stats.totalXp += earnedXp;

    checkAchievementsLocked();
    m_stats.currentLevel = calculateLevel(m_stats.totalXp);
}

void ProgressionEngine::addXp(uint64_t xp) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.totalXp += xp;
    checkAchievementsLocked();
    m_stats.currentLevel = calculateLevel(m_stats.totalXp);
}

void ProgressionEngine::recordSnakeScore(int64_t score) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (score > m_stats.snakeHighScore) { m_stats.snakeHighScore = score; }
    if (score > 0) { m_stats.totalXp += static_cast<uint64_t>(score / 5); }
    checkAchievementsLocked();
    m_stats.currentLevel = calculateLevel(m_stats.totalXp);
}

void ProgressionEngine::recordTetrisScore(int64_t score) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (score > m_stats.tetrisHighScore) { m_stats.tetrisHighScore = score; }
    if (score > 0) { m_stats.totalXp += static_cast<uint64_t>(score / 10); }
    checkAchievementsLocked();
    m_stats.currentLevel = calculateLevel(m_stats.totalXp);
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
    m_stats.currentLevel = calculateLevel(m_stats.totalXp);
}

void ProgressionEngine::recordTypingRace(double wpm) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (wpm > m_stats.typingRaceBestWpm) { m_stats.typingRaceBestWpm = wpm; }
    m_stats.totalXp += static_cast<uint64_t>(wpm * 2.0);
    checkAchievementsLocked();
    m_stats.currentLevel = calculateLevel(m_stats.totalXp);
}

void ProgressionEngine::recordWasdRaceScore(int64_t score) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (score > m_stats.wasdRaceHighScore) { m_stats.wasdRaceHighScore = score; }
    if (score > 0) { m_stats.totalXp += static_cast<uint64_t>(score / 10); }
    checkAchievementsLocked();
    m_stats.currentLevel = calculateLevel(m_stats.totalXp);
}

void ProgressionEngine::recordRhythmScore(int64_t score, uint32_t combo) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (score > m_stats.rhythmHighScore) { m_stats.rhythmHighScore = score; }
    if (combo >= 50) {
        uint64_t mask = 1ULL << static_cast<uint16_t>(AchievementId::RhythmStreak50);
        if (!(m_stats.achievementsUnlocked & mask)) {
            m_stats.achievementsUnlocked |= mask;
            m_stats.totalXp += getAchievementInfo(AchievementId::RhythmStreak50).xpReward;
        }
    }
    if (score > 0) { m_stats.totalXp += static_cast<uint64_t>(score / 20); }
    checkAchievementsLocked();
    m_stats.currentLevel = calculateLevel(m_stats.totalXp);
}

void ProgressionEngine::recordNoMistakeCombo(uint64_t combo) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (combo > m_stats.noMistakeMaxCombo) { m_stats.noMistakeMaxCombo = combo; }
    checkAchievementsLocked();
    m_stats.currentLevel = calculateLevel(m_stats.totalXp);
}

bool ProgressionEngine::isAchievementUnlocked(AchievementId id) const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t mask = 1ULL << static_cast<uint16_t>(id);
    return (m_stats.achievementsUnlocked & mask) != 0;
}

AchievementInfo ProgressionEngine::getAchievementInfo(AchievementId id) noexcept {
    auto idx = static_cast<size_t>(id);
    if (idx < static_cast<size_t>(AchievementId::Count)) {
        return kAchievementTable[idx];
    }
    return {AchievementId::FirstKey, "Unknown", "Unknown", 0};
}

std::vector<AchievementInfo> ProgressionEngine::getUnlockedAchievements() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AchievementInfo> res;
    for (size_t i = 0; i < static_cast<size_t>(AchievementId::Count); ++i) {
        if (m_stats.achievementsUnlocked & (1ULL << i)) {
            res.push_back(kAchievementTable[i]);
        }
    }
    return res;
}

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
    ss << "achievementsUnlocked=" << m_stats.achievementsUnlocked << "\n";

    std::string payload = ss.str();
    uint32_t csum = computeChecksum(payload);
    std::ostringstream finalOut;
    finalOut << payload;
    finalOut << "checksum=" << csum << "\n";
    return finalOut.str();
}

bool ProgressionEngine::deserialize(std::string_view data) {
    if (data.empty()) { return false; }
    if (data.rfind("KIEEKEY_PROGRESSION_V1", 0) != 0) {
        return false;
    }

    size_t cpos = data.rfind("checksum=");
    if (cpos == std::string_view::npos) {
        return false;
    }

    std::string_view payload = data.substr(0, cpos);
    std::string_view csumStr = data.substr(cpos + 9);
    uint32_t expectedCsum = 0;
    try {
        expectedCsum = static_cast<uint32_t>(std::stoul(std::string(csumStr)));
    } catch (...) {
        return false;
    }

    if (computeChecksum(payload) != expectedCsum) {
        return false;
    }

    ProgressionStats s{};
    std::istringstream ss{std::string(payload)};
    std::string line;
    std::getline(ss, line); // Header

    while (std::getline(ss, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) { continue; }
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        try {
            if (key == "totalCharacters") s.totalCharacters = std::stoull(val);
            else if (key == "totalWords") s.totalWords = std::stoull(val);
            else if (key == "totalKeystrokes") s.totalKeystrokes = std::stoull(val);
            else if (key == "totalSessions") s.totalSessions = std::stoull(val);
            else if (key == "typingTimeSeconds") s.typingTimeSeconds = std::stoull(val);
            else if (key == "bestWpm") s.bestWpm = std::stod(val);
            else if (key == "bestAccuracy") s.bestAccuracy = std::stod(val);
            else if (key == "currentStreakDays") s.currentStreakDays = static_cast<uint32_t>(std::stoul(val));
            else if (key == "maxStreakDays") s.maxStreakDays = static_cast<uint32_t>(std::stoul(val));
            else if (key == "totalXp") s.totalXp = std::stoull(val);
            else if (key == "currentLevel") s.currentLevel = static_cast<uint32_t>(std::stoul(val));
            else if (key == "snakeHighScore") s.snakeHighScore = std::stoll(val);
            else if (key == "tetrisHighScore") s.tetrisHighScore = std::stoll(val);
            else if (key == "fishCaughtCount") s.fishCaughtCount = std::stoull(val);
            else if (key == "legendaryFishCount") s.legendaryFishCount = std::stoull(val);
            else if (key == "typingRaceBestWpm") s.typingRaceBestWpm = std::stod(val);
            else if (key == "wasdRaceHighScore") s.wasdRaceHighScore = std::stoll(val);
            else if (key == "rhythmHighScore") s.rhythmHighScore = std::stoll(val);
            else if (key == "noMistakeMaxCombo") s.noMistakeMaxCombo = std::stoull(val);
            else if (key == "achievementsUnlocked") s.achievementsUnlocked = std::stoull(val);
        } catch (...) {
            // Ignore parse errors on individual fields
        }
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats = s;
    m_stats.currentLevel = calculateLevel(m_stats.totalXp);
    return true;
}

void ProgressionEngine::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.reset();
}

bool ProgressionEngine::saveToFile(std::string_view path) const {
    std::string text = serialize();
    std::ofstream ofs(std::string(path), std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) { return false; }
    ofs.write(text.data(), static_cast<std::streamsize>(text.size()));
    return ofs.good();
}

bool ProgressionEngine::loadFromFile(std::string_view path) {
    std::ifstream ifs(std::string(path), std::ios::in);
    if (!ifs.is_open()) { return false; }
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return deserialize(content);
}

} // namespace ok::progression
