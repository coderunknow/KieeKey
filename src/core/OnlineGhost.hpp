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
// File: src/core/OnlineGhost.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — OnlineGhost.hpp
// Ghost, Replay & Leaderboard abstraction layer.
//
// PRIVACY & ARCHITECTURE INVARIANT:
//   * NO UNAUTHORIZED NETWORK TRAFFIC: Backend specifications are deferred.
//     All abstractions operate entirely locally via `LocalGhostProvider`.
//   * ZERO RUNTIME OVERHEAD when not in a replay or racing mode.
//----------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ok::online {

struct GhostFrame {
    uint32_t timeOffsetMs = 0;
    uint32_t charIndex = 0;
    float currentWpm = 0.0f;
    float currentAccuracy = 100.0f;
};

struct ReplaySession {
    std::string sessionId;
    std::string modeName;
    std::string playerTag;
    uint64_t timestampUtc = 0;
    int64_t finalScore = 0;
    float finalWpm = 0.0f;
    float finalAccuracy = 100.0f;
    std::vector<GhostFrame> frames;
};

struct ScorePayload {
    std::string gameMode;
    std::string playerTag;
    int64_t score = 0;
    float wpm = 0.0f;
    float accuracy = 0.0f;
    uint64_t timestampUtc = 0;
    std::string payloadSignature;
};

class IGhostProvider {
public:
    virtual bool saveReplay(const ReplaySession& session) = 0;
    virtual std::optional<ReplaySession> loadReplay(std::string_view sessionId) = 0;
    virtual std::optional<ReplaySession> getBestReplay(std::string_view modeName) = 0;
    virtual std::vector<ReplaySession> listReplays(std::string_view modeName) = 0;
    virtual ~IGhostProvider() = default;
};

class IOnlineLeaderboardProvider {
public:
    virtual bool submitScore(const ScorePayload& payload) = 0;
    virtual std::vector<ScorePayload> getLeaderboard(std::string_view modeName, size_t topN) = 0;
    virtual bool isOnline() const = 0;
    virtual ~IOnlineLeaderboardProvider() = default;
};

class LocalGhostProvider final : public IGhostProvider {
public:
    LocalGhostProvider() = default;
    ~LocalGhostProvider() override = default;

    bool saveReplay(const ReplaySession& session) override;
    std::optional<ReplaySession> loadReplay(std::string_view sessionId) override;
    std::optional<ReplaySession> getBestReplay(std::string_view modeName) override;
    std::vector<ReplaySession> listReplays(std::string_view modeName) override;
    void clear();

private:
    mutable std::mutex m_mutex;
    std::vector<ReplaySession> m_replays;
};

class OfflineLeaderboardProvider final : public IOnlineLeaderboardProvider {
public:
    OfflineLeaderboardProvider() = default;
    ~OfflineLeaderboardProvider() override = default;

    bool submitScore(const ScorePayload& payload) override;
    std::vector<ScorePayload> getLeaderboard(std::string_view modeName, size_t topN) override;
    bool isOnline() const override { return false; }
    void clear();

private:
    mutable std::mutex m_mutex;
    std::vector<ScorePayload> m_scores;
};

} // namespace ok::online
