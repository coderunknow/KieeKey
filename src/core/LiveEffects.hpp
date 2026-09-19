// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 coderunknow
#pragma once

#include "ChaosEngine.hpp"
#include <atomic>
#include <cstdint>
#include <string>
#include <utility>

namespace ok::effects {

// Separate from Lab configuration: nothing affects external input until the
// user explicitly enables this channel. These modes preserve order and UTF-16
// length. "UpsideDown" rotates individual Unicode lookalikes, not the string.
enum class Glyph : std::uint8_t { None, UpsideDown, Mirror, Random };
struct Config {
    bool enabled = false;
    bool randomCase = true;
    Glyph glyph = Glyph::None;
    unsigned intensity = 50;
};

// UI publishes one atomic word. Cursor/working config belong exclusively to
// the hook thread; no locks, singleton construction or allocation per mapping.
// Original engine state is NEVER modified. Rewrite deltas rewind the visual
// cursor so a later tone correction makes the same case/glyph choice.
class LiveEffects {
public:
    void configure(Config c) noexcept {
        const auto intensity = c.intensity > 100 ? 100u : c.intensity;
        const auto bits = (c.enabled ? 1u : 0u) | (c.randomCase ? 2u : 0u) |
                          (static_cast<unsigned>(c.glyph) << 2) | (intensity << 8);
        published_.store(bits, std::memory_order_release);
    }
    [[nodiscard]] Config config() const noexcept {
        return decode(published_.load(std::memory_order_acquire));
    }
    [[nodiscard]] bool enabled() const noexcept {
        return (published_.load(std::memory_order_acquire) & 1u) != 0;
    }
    void disable() noexcept { published_.fetch_and(~1u, std::memory_order_acq_rel); }

    // Called by producer before processing a key. A change requires an engine
    // session reset too; don't let an old word straddle two output policies.
    bool sync() noexcept {
        const auto bits = published_.load(std::memory_order_acquire);
        if (bits == applied_) { return false; }
        applied_ = bits;
        active_ = decode(bits);
        reset();
        return true;
    }
    void reset() noexcept { position_ = 0; }
    void backspace(std::size_t count = 1) noexcept {
        position_ = count > position_ ? 0 : position_ - count;
    }
    // Text is UTF-16 on Windows. Surrogates and combining marks pass unchanged;
    // BMP mappings remain one code unit, so engine erase counts stay correct.
    bool rewrite(std::size_t erase, std::wstring& text) noexcept {
        backspace(erase);
        bool changed = false;
        for (auto& unit : text) {
            const auto original = static_cast<char32_t>(unit);
            const auto mapped = map(original, position_++);
            unit = static_cast<wchar_t>(mapped);
            changed = changed || mapped != original;
        }
        return changed;
    }
    [[nodiscard]] char32_t map(char32_t ch, std::uint64_t position) const noexcept {
        if (!active_.enabled || ch > 0xFFFF || (ch >= 0xD800 && ch <= 0xDFFF)) { return ch; }
        const auto seed = hash(position);
        if (active_.randomCase && seed % 100u < active_.intensity) {
            ch = chaos::ChaosEngine::toggleCaseVn(ch);
        }
        const auto glyphSeed = hash(position ^ 0x9e3779b9u);
        if (glyphSeed % 100u >= active_.intensity) { return ch; }
        switch (active_.glyph) {
            case Glyph::UpsideDown: return chaos::ChaosEngine::getFlippedVerticalGlyph(ch);
            case Glyph::Mirror: return chaos::ChaosEngine::getFlippedHorizontalGlyph(ch);
            case Glyph::Random:
                return (glyphSeed & 1u) ? chaos::ChaosEngine::getFlippedVerticalGlyph(ch)
                                       : chaos::ChaosEngine::getFlippedHorizontalGlyph(ch);
            case Glyph::None: default: return ch;
        }
    }
private:
    static Config decode(std::uint32_t bits) noexcept {
        return {bool(bits & 1), bool(bits & 2), static_cast<Glyph>((bits >> 2) & 3), (bits >> 8) & 127};
    }
    static std::uint32_t hash(std::uint64_t n) noexcept {
        auto x = static_cast<std::uint32_t>(n ^ (n >> 32)) + 0x85ebca6bu;
        x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu;
        return x ^ (x >> 16);
    }
    std::atomic<std::uint32_t> published_{2u | (50u << 8)};
    std::uint32_t applied_ = 2u | (50u << 8);
    Config active_{};
    std::uint64_t position_ = 0;
};
//---------------------------------------------------------------------------
// v1.3.0-beta3 — the app-level output decision, EXTRACTED.
//
// WHY: beta2 kept this logic inline in onHookEventImpl() (src/app/main.cpp),
// i.e. inside a Windows-only translation unit that no test can execute. The
// user-visible symptom was "bật random case rồi mà gõ bên ngoài vẫn bình
// thường": either the channel was never reached (the settings page that
// switches it on was one of the tab headers the crowded tab control clipped)
// or the decision was wrong — and there was NO WAY to tell which from the
// repository, because the only executable test (tests/test_live_effects.cpp)
// re-implemented the decision instead of calling it.
//
// planOutput() is now the single implementation: the hook calls it, the tests
// call it, and the two cannot drift. It is pure (no Win32, no globals): the
// caller owns the scratch string and the LiveEffects cursor.
//---------------------------------------------------------------------------
enum class KeyKind : std::uint8_t { Char, Space, Backspace, WordBreak, Other };

struct OutputPlan {
    bool suppress = false;           // true => the raw key must NOT reach the app
    std::size_t backspace = 0;       // characters to erase at the caret first
    bool styled = false;             // live effects changed what is emitted
    bool nativePassThrough = false;  // the app receives the raw key untouched
};

// The single implementation of the app-level output decision (v1.3.0-beta3).
//
// `text` is the hook's scratch buffer holding the engine's replacement for this
// key (already resolved, including macro expansions and the restore re-issue).
// It is styled IN PLACE — exactly as the shipped inline path did — so the hook
// reuses one buffer with ZERO per-key allocation. On return `text` holds what
// must be emitted (empty when the raw key passes through natively), and the
// plan carries the suppress/backspace decision.
//
// `typed` is the character the user pressed (only used for KeyKind::Char /
// Space). `active` is `liveEffects.enabled() && codeTable == Unicode` — the
// shipped gate; when it is false this is a pure pass-through of the engine's
// decision (the "feature isolation" contract).
[[nodiscard]] inline OutputPlan planOutput(bool active,
                                           bool engineSuppress,
                                           std::size_t engineBackspace,
                                           std::wstring& text,
                                           KeyKind kind,
                                           char32_t typed,
                                           LiveEffects& fx) {
    OutputPlan plan;
    if (!active) {
        // Effects off: the plan is exactly the engine's decision and `text` is
        // left untouched. Nothing below can run.
        plan.suppress = engineSuppress;
        plan.backspace = engineBackspace;
        plan.nativePassThrough = !plan.suppress;
        return plan;
    }
    if (engineSuppress) {
        // The engine already rewrites the word (tone mark, đ, macro…): style
        // the replacement in place. The cursor rewinds by the erase count so a
        // later correction of the same character makes the same visual choice.
        plan.backspace = engineBackspace;
        plan.styled = fx.rewrite(engineBackspace, text);
        plan.suppress = true;
        return plan;
    }
    if (kind == KeyKind::Char || kind == KeyKind::Space) {
        const char32_t ch = (kind == KeyKind::Space) ? U' ' : typed;
        // One BMP unit only: a supplementary/control event keeps its native
        // delivery instead of being truncated into a surrogate half. The
        // styling cursor is deliberately left UNTOUCHED here (matching the
        // shipped inline path) — the character is delivered raw and the
        // engine's word continues; only a word break starts a fresh context.
        if (ch < 0x20 || ch > 0xFFFF) {
            plan.nativePassThrough = true;
            return plan;
        }
        text.assign(1, static_cast<wchar_t>(ch));
        plan.styled = fx.rewrite(0, text);
        if (plan.styled) {
            plan.suppress = true;      // swallow the raw key, emit the styled one
            plan.backspace = 0;
        } else {
            text.clear();              // unchanged: let the app take the raw key
            plan.nativePassThrough = true;
        }
        return plan;
    }
    if (kind == KeyKind::Backspace) {
        fx.backspace();                // keep the visual cursor in step
        plan.nativePassThrough = true;
        return plan;
    }
    fx.reset();                        // word break / navigation: new context
    plan.nativePassThrough = true;
    return plan;
}

} // namespace ok::effects
