//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.1 Stable - refactored and completed logic
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
// File: src/core/Notifications.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// Intelligent notification framework (v1.2.1 RC2) + the QuickTelex
// unwanted-correction detector.
//
// DESIGN RULES (all enforced here, tested in tests/test_notifications.cpp):
//   * The INPUT PATH never blocks and never allocates: producers call
//     `Detector::observe*()` (a handful of integer ops on a fixed ring) and
//     `NotificationCenter::raise()` (an atomic slot write). Presentation
//     (balloon / toast / dialog) happens on the UI thread, which POLLS
//     `takePending()` on its existing slow timer.
//   * Every notification has a stable id, a confidence threshold, a
//     per-id cooldown, dedup of identical pending notifications, a global
//     rate limit (no spam), user suppression ("Don't show again") that is
//     PERSISTED through a caller-supplied store, and a session mute.
//   * Safe when notifications are unavailable: if the UI never polls, the
//     slot is simply overwritten by newer, higher-confidence items; nothing
//     accumulates, nothing leaks, input is unaffected.
//
// The QuickTelex detector answers: "is the user repeatedly UNDOING a
// quick-Telex expansion?" — general heuristic over ALL kQuickTelex pairs
// (cc→ch, gg→gi, kk→kh, nn→ng, pp→ph, qq→qu, tt→th, uu→ư), not only pp→ph.
// The signal is a quick-Telex expansion (engine emitted WillProcess with the
// 1-backspace/2-char pattern on a double consonant) followed within a short
// window by a Backspace that removes it and the SAME double letter being
// retyped/kept raw, or an immediate Restore of the word. Confidence rises
// with the number of distinct undo events within the observation window and
// decays with clean expansions.
//----------------------------------------------------------------------------
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <utility>

namespace ok::notify {

enum class Id : std::uint8_t {
    None = 0,
    QuickTelexUnwanted,      // "Telex nhanh đang sửa nhầm từ của bạn?"
    TsfSlowDowngrade,        // "Ứng dụng này chậm với TSF — đã chuyển SendInput"
    BarrierTimeouts,         // "Máy đang quá tải; một số phím bị trễ"
    HookReinstalled,         // "Hook bàn phím đã tự phục hồi"
    kCount
};

enum class Action : std::uint8_t { None = 0, TurnOff, Keep, DontShowAgain, Dismissed };

struct Notification {
    Id            id         = Id::None;
    std::uint8_t  confidence = 0;      // 0..100
    std::uint32_t evidence   = 0;      // detector-specific count (for the text)
    std::uint64_t raisedAtMs = 0;
    [[nodiscard]] bool valid() const noexcept { return id != Id::None; }
};

struct Policy {
    std::uint8_t  minConfidence = 70;     // below → never raised
    std::uint32_t cooldownMs    = 10 * 60 * 1000;  // per-id, after presentation
    std::uint32_t maxPerHour    = 3;      // per-id rate limit
    bool          allowRepeat   = true;   // false → once per session unless reset
};

[[nodiscard]] constexpr Policy policyFor(Id id) noexcept {
    switch (id) {
        case Id::QuickTelexUnwanted: return Policy{75, 30 * 60 * 1000, 2, true};
        case Id::TsfSlowDowngrade:   return Policy{60, 60 * 60 * 1000, 1, true};
        case Id::BarrierTimeouts:    return Policy{80, 60 * 60 * 1000, 1, false};
        case Id::HookReinstalled:    return Policy{50, 10 * 60 * 1000, 3, true};
        default:                     return Policy{};
    }
}

// Persistence adapter — the app maps this onto the registry (HKCU), tests
// onto an in-memory map. Only the suppression bit is persisted; cooldowns
// and rate counters are per session by design (a fresh session may re-ask).
struct Store {
    std::function<bool(Id)>       isSuppressed;   // may be empty → false
    std::function<void(Id, bool)> setSuppressed;  // may be empty → no-op
};

//----------------------------------------------------------------------------
// NotificationCenter — the policy engine. Thread model:
//   * raise()          — any thread (producer / consumer): lock-free, O(1).
//   * takePending()    — UI thread, polled.
//   * resolve()        — UI thread, after the user picked an action.
// Global rate limit: at most one PRESENTED notification per kGlobalMinGapMs.
//----------------------------------------------------------------------------
class NotificationCenter final {
public:
    static constexpr std::uint32_t kGlobalMinGapMs = 60 * 1000;

    explicit NotificationCenter(Store store = {}) noexcept : store_(std::move(store)) {}
    void setStore(Store store) noexcept { store_ = std::move(store); }

    void setSessionMuted(bool m) noexcept { muted_.store(m, std::memory_order_relaxed); }
    [[nodiscard]] bool sessionMuted() const noexcept { return muted_.load(std::memory_order_relaxed); }

    // Producer side. Returns true if the item was queued (not yet shown).
    // Never blocks: a single pending slot; a higher-confidence or different
    // item replaces a stale pending one, an identical one is deduplicated.
    bool raise(Id id, std::uint8_t confidence, std::uint32_t evidence, std::uint64_t nowMs) noexcept {
        if (id == Id::None || id >= Id::kCount) { return false; }
        if (muted_.load(std::memory_order_relaxed)) { return false; }
        const Policy p = policyFor(id);
        if (confidence < p.minConfidence) { return false; }
        auto& st = state_[static_cast<std::size_t>(id)];
        if (st.suppressed.load(std::memory_order_relaxed)) { return false; }
        if (!p.allowRepeat && st.shownCount.load(std::memory_order_relaxed) > 0) { return false; }
        const std::uint64_t lastShown = st.lastShownMs.load(std::memory_order_relaxed);
        if (lastShown != 0 && nowMs - lastShown < p.cooldownMs) { return false; }
        // per-id hourly rate limit
        const std::uint64_t hourStart = st.hourStartMs.load(std::memory_order_relaxed);
        if (hourStart != 0 && nowMs - hourStart < 3'600'000ull &&
            st.hourCount.load(std::memory_order_relaxed) >= p.maxPerHour) { return false; }
        // global spacing between presented notifications
        const std::uint64_t lastGlobal = lastPresentedMs_.load(std::memory_order_relaxed);
        if (lastGlobal != 0 && nowMs - lastGlobal < kGlobalMinGapMs) { return false; }

        // dedup / replace policy on the single pending slot
        const std::uint64_t packed = pending_.load(std::memory_order_acquire);
        if (packed != 0) {
            const Notification cur = unpack(packed);
            if (cur.id == id && cur.confidence >= confidence) { return false; }   // dedup
            if (cur.id != id && cur.confidence > confidence)  { return false; }   // keep stronger
        }
        pending_.store(pack(Notification{id, confidence, evidence, nowMs}), std::memory_order_release);
        ++raisedTotal_;
        return true;
    }

    // UI side. Returns the pending item (and marks it presented) or an
    // invalid Notification when there is nothing to show.
    [[nodiscard]] Notification takePending(std::uint64_t nowMs) noexcept {
        const std::uint64_t packed = pending_.exchange(0, std::memory_order_acq_rel);
        if (packed == 0) { return Notification{}; }
        Notification n = unpack(packed);
        auto& st = state_[static_cast<std::size_t>(n.id)];
        // Re-check suppression (could have been set between raise and take).
        if (st.suppressed.load(std::memory_order_relaxed) || muted_.load(std::memory_order_relaxed)) {
            return Notification{};
        }
        // Stale pending item (UI polled late): still valid — the evidence
        // does not expire — but the cooldown clock starts at presentation.
        st.lastShownMs.store(nowMs, std::memory_order_relaxed);
        st.shownCount.fetch_add(1, std::memory_order_relaxed);
        const std::uint64_t hourStart = st.hourStartMs.load(std::memory_order_relaxed);
        if (hourStart == 0 || nowMs - hourStart >= 3'600'000ull) {
            st.hourStartMs.store(nowMs, std::memory_order_relaxed);
            st.hourCount.store(1, std::memory_order_relaxed);
        } else {
            st.hourCount.fetch_add(1, std::memory_order_relaxed);
        }
        lastPresentedMs_.store(nowMs, std::memory_order_relaxed);
        ++presentedTotal_;
        return n;
    }

    // UI side: the user's choice. TurnOff/Keep are reported to the caller
    // (returned unchanged) — the app applies the setting; DontShowAgain is
    // persisted here.
    Action resolve(Id id, Action a) noexcept {
        if (id == Id::None || id >= Id::kCount) { return Action::None; }
        auto& st = state_[static_cast<std::size_t>(id)];
        if (a == Action::DontShowAgain) {
            st.suppressed.store(true, std::memory_order_relaxed);
            if (store_.setSuppressed) { store_.setSuppressed(id, true); }
        }
        return a;
    }

    // Load persisted suppression bits (app startup).
    void loadSuppressions() noexcept {
        if (!store_.isSuppressed) { return; }
        for (std::size_t i = 1; i < static_cast<std::size_t>(Id::kCount); ++i) {
            state_[i].suppressed.store(store_.isSuppressed(static_cast<Id>(i)), std::memory_order_relaxed);
        }
    }
    void resetSuppression(Id id) noexcept {
        if (id == Id::None || id >= Id::kCount) { return; }
        state_[static_cast<std::size_t>(id)].suppressed.store(false, std::memory_order_relaxed);
        if (store_.setSuppressed) { store_.setSuppressed(id, false); }
    }
    [[nodiscard]] bool isSuppressed(Id id) const noexcept {
        if (id == Id::None || id >= Id::kCount) { return false; }
        return state_[static_cast<std::size_t>(id)].suppressed.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool hasPending() const noexcept { return pending_.load(std::memory_order_acquire) != 0; }
    [[nodiscard]] std::uint64_t raisedTotal() const noexcept { return raisedTotal_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t presentedTotal() const noexcept { return presentedTotal_.load(std::memory_order_relaxed); }

private:
    struct PerId {
        std::atomic<bool>          suppressed{false};
        std::atomic<std::uint64_t> lastShownMs{0};
        std::atomic<std::uint64_t> hourStartMs{0};
        std::atomic<std::uint32_t> hourCount{0};
        std::atomic<std::uint32_t> shownCount{0};
    };
    // 64-bit packing: id:8 | confidence:8 | evidence:16 | raisedAt low 32 bits.
    static std::uint64_t pack(const Notification& n) noexcept {
        const std::uint64_t ev = n.evidence > 0xFFFFu ? 0xFFFFu : n.evidence;
        return (static_cast<std::uint64_t>(n.id) << 56) |
               (static_cast<std::uint64_t>(n.confidence) << 48) |
               (ev << 32) | (n.raisedAtMs & 0xFFFFFFFFull);
    }
    static Notification unpack(std::uint64_t v) noexcept {
        Notification n;
        n.id         = static_cast<Id>((v >> 56) & 0xFF);
        n.confidence = static_cast<std::uint8_t>((v >> 48) & 0xFF);
        n.evidence   = static_cast<std::uint32_t>((v >> 32) & 0xFFFF);
        n.raisedAtMs = v & 0xFFFFFFFFull;
        return n;
    }

    Store store_;
    std::array<PerId, static_cast<std::size_t>(Id::kCount)> state_{};
    std::atomic<std::uint64_t> pending_{0};
    std::atomic<std::uint64_t> lastPresentedMs_{0};
    std::atomic<bool> muted_{false};
    std::atomic<std::uint64_t> raisedTotal_{0};
    std::atomic<std::uint64_t> presentedTotal_{0};
};

//----------------------------------------------------------------------------
// QuickTelexDetector — producer-thread affine (called under the engine
// lock right after each engine decision; no allocation, ~10 integer ops).
//
// Observation model (all calls are producer-thread affine, O(1), no alloc):
//   observeExpansion(letter, nowMs) — the engine just expanded a quick-Telex
//       double (cc→ch …). Arms the detector; depth (raw chars typed after the
//       expansion) resets to 0. If a previous expansion of the SAME letter is
//       still armed with the expansion already deleted, this re-typing of the
//       double is a confirmed undo (the "pp → ph → ⌫⌫ → pp → ph…" loop).
//   observeRawChar(nowMs)            — a pass-through char after the
//       expansion: depth++. If the expansion had been deleted (depth < 0)
//       the user is replacing it with something else → confirmed undo.
//   observeBackspace(nowMs)          — depth--; once depth < 0 the backspace
//       reached INTO the expansion itself (not a later typo) → candidate.
//   observeRestore(nowMs)            — engine Restore while armed → confirmed.
//   observeWordCommitted(nowMs)      — the expansion survived a word break →
//       clean acceptance (decays the score); disarms.
// Candidates expire after kUndoWindowMs. Score: +kUndoWeight per confirmed
// undo, −kAcceptWeight per acceptance, clamped to [0, 100]; confidence =
// score. maybeRaise() asks the center to show the notification when
// confidence ≥ policy threshold AND ≥ kMinUndos undos in the last kWindowMs.
//----------------------------------------------------------------------------
class QuickTelexDetector final {
public:
    static constexpr std::uint32_t kUndoWindowMs  = 4000;   // expansion → undo
    static constexpr std::uint32_t kWindowMs      = 10 * 60 * 1000;
    static constexpr std::uint32_t kMinUndos      = 3;
    static constexpr std::uint8_t  kUndoWeight    = 30;
    static constexpr std::uint8_t  kAcceptWeight  = 10;

    void observeExpansion(char32_t letter, std::uint64_t nowMs) noexcept {
        if (armedLetter_ == letter && deletedIntoExpansion_ && inWindow(nowMs)) {
            confirmUndo(letter, nowMs);      // re-typed the same double after undoing
        }
        armedLetter_ = letter;
        armedAtMs_   = nowMs;
        depth_       = 0;
        deletedIntoExpansion_ = false;
        ++expansions_;
    }
    void observeRawChar(std::uint64_t nowMs) noexcept {
        if (!armedLetter_) { return; }
        if (!inWindow(nowMs)) { disarm(); return; }
        if (deletedIntoExpansion_) { confirmUndo(armedLetter_, nowMs); return; }
        ++depth_;
    }
    void observeBackspace(std::uint64_t nowMs) noexcept {
        if (!armedLetter_) { return; }
        if (!inWindow(nowMs)) { disarm(); return; }
        if (--depth_ < 0) { deletedIntoExpansion_ = true; }
    }
    void observeRestore(std::uint64_t nowMs) noexcept {
        if (armedLetter_ && inWindow(nowMs)) { confirmUndo(armedLetter_, nowMs); }
    }
    void observeWordCommitted(std::uint64_t /*nowMs*/) noexcept {
        if (armedLetter_ && !deletedIntoExpansion_) {
            score_ = score_ > kAcceptWeight ? static_cast<std::uint8_t>(score_ - kAcceptWeight) : 0;
            ++accepted_;
        }
        disarm();
    }

    // Evaluate + raise. Returns true when a notification was queued.
    bool maybeRaise(NotificationCenter& center, std::uint64_t nowMs) noexcept {
        const std::uint32_t recent = undosInWindow(nowMs);
        if (recent < kMinUndos) { return false; }
        return center.raise(Id::QuickTelexUnwanted, confidence(), recent, nowMs);
    }

    [[nodiscard]] std::uint8_t confidence() const noexcept { return score_; }
    [[nodiscard]] std::uint32_t undosInWindow(std::uint64_t nowMs) const noexcept {
        std::uint32_t n = 0;
        for (const auto& u : undoRing_) {
            if (u.atMs != 0 && nowMs - u.atMs <= kWindowMs) { ++n; }
        }
        return n;
    }
    [[nodiscard]] std::uint32_t distinctLettersInWindow(std::uint64_t nowMs) const noexcept {
        std::uint32_t mask = 0;
        for (const auto& u : undoRing_) {
            if (u.atMs != 0 && nowMs - u.atMs <= kWindowMs && u.letter >= U'A' && u.letter <= U'Z') {
                mask |= 1u << (u.letter - U'A');
            }
        }
        std::uint32_t c = 0;
        while (mask) { c += mask & 1u; mask >>= 1; }
        return c;
    }
    [[nodiscard]] std::uint64_t expansions() const noexcept { return expansions_; }
    [[nodiscard]] std::uint64_t accepted()   const noexcept { return accepted_; }
    [[nodiscard]] std::uint64_t undos()      const noexcept { return undos_; }
    void reset() noexcept { *this = QuickTelexDetector{}; }

private:
    struct Undo { char32_t letter = 0; std::uint64_t atMs = 0; };
    [[nodiscard]] bool inWindow(std::uint64_t nowMs) const noexcept {
        return nowMs - armedAtMs_ <= kUndoWindowMs;
    }
    void confirmUndo(char32_t letter, std::uint64_t nowMs) noexcept {
        undoRing_[undoIdx_++ & (kRing - 1)] = Undo{letter, nowMs};
        score_ = static_cast<std::uint8_t>(std::min<int>(100, score_ + kUndoWeight));
        ++undos_;
        disarm();
    }
    void disarm() noexcept {
        armedLetter_ = 0; armedAtMs_ = 0; depth_ = 0; deletedIntoExpansion_ = false;
    }

    static constexpr std::size_t kRing = 16;
    std::array<Undo, kRing> undoRing_{};
    std::uint32_t undoIdx_ = 0;
    char32_t      armedLetter_ = 0;
    std::uint64_t armedAtMs_   = 0;
    int           depth_ = 0;
    bool          deletedIntoExpansion_ = false;
    std::uint8_t  score_ = 0;
    std::uint64_t expansions_ = 0, accepted_ = 0, undos_ = 0;
};

} // namespace ok::notify
