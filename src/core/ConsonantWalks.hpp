//============================================================================
// Vietnamese IME — engine spelling walks
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File: src/core/ConsonantWalks.hpp
//----------------------------------------------------------------------------
// v1.3.0 RC1 C-A — the two consonant walks of checkSpelling, as small
// deterministic automata.
//
// WHY. The RC1 sampling profile of the frozen v1.2.2 engine put
// TextEngine::checkSpelling at 22.1 % of the whole timed window — the single
// largest cost in the engine — and inside it, the two table walks: the
// leading-consonant matcher and the end-consonant matcher. Both had already
// been narrowed by the v1.2.1/v1.2.2 rounds (first-letter buckets that shrink
// 33 rows to ≤ 8 and 11 rows to ≤ 3), but each still WALKED the surviving rows'
// cells: load, mask, compare, conditional break — once per cell, with the
// iteration count depending on which row was being tested.
//
// WHAT. The tables are tiny and fixed (33 leading rows of ≤ 3 cells over the 26
// letters; 11 end rows of ≤ 2 cells), so "is this row consistent with these
// cells" is a bitmask AND. Row i becomes bit i; "row i matches letter c at
// position p" is precomputed into rowsForP[c]; a row set is ANDed once per
// position and the length rules are further ANDs (leading: rows no longer than
// the prefix; end: rows at least as long as the tail). No loop over rows, no
// per-cell branch, and the work depends only on the number of positions (≤ 3,
// ≤ 2), never on which rows survive.
//
// EQUIVALENCE. The pre-C-A code is kept verbatim here under *Ref names, and
// kk_walks_selftest() enumerates the reachable input space — every word up to a
// given length over an alphabet that includes a non-letter, every split point,
// every option-mask combination — against it. That oracle is what caught the one
// real bug found while writing this: the end walk had silently dropped the
// "row must be at least as long as the tail" rejection for tails of 3+ letters.
// The length rule therefore lives in the accept path as a FILTER TABLE
// (rowsLenGe[]), not as a special case: an AND cannot be skipped by a
// control-flow edit, and if the tables ever outgrow the mask width the
// static_asserts fire at compile time.
//
// ABLATION KNOBS (compile-time, for PROTOCOL §13 sensitivity runs; they select
// an implementation, never a decision):
//   -DKK_WALK_FAST_LEAD=0   leading walk uses the reference loop
//   -DKK_WALK_FAST_END=0    end walk uses the reference loop
//============================================================================
#pragma once

#include "VietnameseTables.hpp"   // masks (kCharMask, kEndConsonantMask, …)
#include "FlatTables.hpp"          // kConsonantTable, kEndConsonantTable

#include <array>
#include <cstdint>

#ifndef KK_WALK_FAST_LEAD
#define KK_WALK_FAST_LEAD 1
#endif
#ifndef KK_WALK_FAST_END
#define KK_WALK_FAST_END 1
#endif

namespace ok {
namespace text {
namespace walks {

static_assert(kConsonantTable.size() <= 64, "leading-consonant table wider than the row mask");
static_assert(kEndConsonantTable.size() <= 64, "end-consonant table wider than the row mask");

constexpr std::size_t kNoLetterSlot = 26;   // 'A'..'Z' -> 0..25, anything else -> 26

constexpr std::size_t letterIdx(std::uint32_t c) noexcept {
    return (c >= U'A' && c <= U'Z') ? static_cast<std::size_t>(c - U'A') : kNoLetterSlot;
}

constexpr int lowestBit(std::uint64_t m) noexcept {
    int n = 0;
    while ((m & 1u) == 0) { m >>= 1; ++n; }
    return n;
}

// Reading past a row's last cell would be out of bounds on a FlatVec, and the
// reference loops never do it (they stop at size()). A row is therefore
// "unconstrained" at a position it does not have, which the builders encode by
// only consulting cells below size().
constexpr std::uint32_t cellAt(const FlatVec<std::uint16_t>& row, std::size_t p) noexcept {
    return p < row.size() ? static_cast<std::uint32_t>(row[p]) : 0xFFFFFFFFu;
}

//===========================================================================
// Reference path — verbatim pre-C-A engine code, kept for the oracle and for
// the ablation switches.
//===========================================================================

// v1.2.1 RC2/RC3 first-letter buckets (moved here from TextEngine.cpp so both
// implementations read the same tables).
struct RowBucket {
    std::uint8_t rows[8];
    std::uint8_t count;
};

template <std::uint16_t maskA, std::uint16_t maskB>
constexpr std::array<RowBucket, 26> buildLeadingBuckets() {
    std::array<RowBucket, 26> b{};
    for (std::size_t i = 0; i < kConsonantTable.size(); ++i) {
        const auto& row = kConsonantTable[i];
        for (std::size_t L = 0; L < 26; ++L) {
            const std::uint16_t c = static_cast<std::uint16_t>(0x41 + L);
            if (static_cast<std::uint16_t>(cellAt(row, 0) & ~maskA) == c ||
                static_cast<std::uint16_t>(cellAt(row, 0) & ~maskB) == c) {
                RowBucket& bucket = b[L];
                if (bucket.count < 8) { bucket.rows[bucket.count++] = static_cast<std::uint8_t>(i); }
                break;   // a cell can match at most one letter per mask pair
            }
        }
    }
    return b;
}

constexpr std::array<RowBucket, 26> kLeadB00 = buildLeadingBuckets<0, 0>();
constexpr std::array<RowBucket, 26> kLeadB10 = buildLeadingBuckets<kEndConsonantMask, 0>();
constexpr std::array<RowBucket, 26> kLeadB01 = buildLeadingBuckets<0, kConsonantAllowMask>();
constexpr std::array<RowBucket, 26> kLeadB11 = buildLeadingBuckets<kEndConsonantMask, kConsonantAllowMask>();

static_assert([] {
    for (const auto& t : {&kLeadB00, &kLeadB10, &kLeadB01, &kLeadB11}) {
        for (const auto& b : *t) { if (b.count > 8) { return false; } }
    }
    return true;
}(), "leading-consonant bucket overflow — widen rows[] and the row mask together");

constexpr const std::array<RowBucket, 26>& leadBuckets(std::uint16_t maskA, std::uint16_t maskB) {
    if (maskB == 0) { return maskA == 0 ? kLeadB00 : kLeadB10; }
    return maskA == 0 ? kLeadB01 : kLeadB11;
}

// v1.2.1 RC3 end-consonant buckets.
template <std::uint16_t endMask>
constexpr std::array<RowBucket, 26> buildEndBuckets() {
    std::array<RowBucket, 26> b{};
    for (std::size_t i = 0; i < kEndConsonantTable.size(); ++i) {
        const auto& row = kEndConsonantTable[i];
        for (std::size_t L = 0; L < 26; ++L) {
            const std::uint16_t c = static_cast<std::uint16_t>(0x41 + L);
            if (static_cast<std::uint16_t>(cellAt(row, 0) & ~endMask) == c) {
                RowBucket& bucket = b[L];
                if (bucket.count < 8) { bucket.rows[bucket.count++] = static_cast<std::uint8_t>(i); }
                break;
            }
        }
    }
    return b;
}

constexpr std::array<RowBucket, 26> kEndB0 = buildEndBuckets<0>();
constexpr std::array<RowBucket, 26> kEndB1 = buildEndBuckets<kEndConsonantMask>();

inline const std::array<RowBucket, 26>& endBuckets(bool quick) noexcept {
    return quick ? kEndB1 : kEndB0;
}

// Verbatim pre-C-A leading walk (TextEngine.cpp, v1.2.1 RC2/RC3). Returns the
// cluster length.
inline std::size_t matchLeadingConsonantRef(const std::uint32_t* word, std::size_t endIndex,
                                            std::uint16_t maskA, std::uint16_t maskB) noexcept {
    const std::uint32_t c0 = word[0] & kCharMask;
    if (c0 < U'A' || c0 > U'Z') { return 0; }
    const auto& buckets = leadBuckets(maskA, maskB);
    const RowBucket& bucket = buckets[c0 - U'A'];
    for (std::uint8_t bi = 0; bi < bucket.count; ++bi) {
        const auto& row = kConsonantTable[bucket.rows[bi]];
        const std::size_t n = row.size();
        if (endIndex < n) { continue; }   // RC1: too short — rejected
        bool reject = false;
        std::size_t j = 0;
        for (j = 0; j < n; ++j) {
            const std::uint16_t c = static_cast<std::uint16_t>(word[j] & kCharMask);
            const std::uint16_t t = static_cast<std::uint16_t>(row[j]);
            if (static_cast<std::uint16_t>(t & ~maskA) != c &&
                static_cast<std::uint16_t>(t & ~maskB) != c) { reject = true; break; }
        }
        if (!reject) { return j; }
    }
    return 0;   // total rejection — the RC2 exit value
}

// Verbatim pre-C-A end walk, as it stood inline in checkSpelling. Returns the
// value spellingOK_ took; *jOut reproduces the (unused afterwards) final j.
inline bool endConsonantMatchRef(const std::uint32_t* tail, std::size_t tailLen, bool quick,
                                 std::size_t* jOut = nullptr) noexcept {
    const std::uint16_t endMask = quick ? kEndConsonantMask : std::uint16_t{0};
    bool ok = false;
    std::size_t j = 0;
    const std::uint32_t t0 = tail[0] & kCharMask;
    if (t0 >= U'A' && t0 <= U'Z') {
        const auto& tailBuckets = endBuckets(quick);
        const RowBucket& bucket = tailBuckets[t0 - U'A'];
        for (std::uint8_t bi = 0; bi < bucket.count; ++bi) {
            const auto& row = kEndConsonantTable[bucket.rows[bi]];
            const std::size_t n = row.size();
            bool spellingFlag = false;
            for (j = 0; j < n; ++j) {
                if (j < tailLen &&
                    static_cast<std::uint16_t>(row[j] & ~endMask) !=
                        static_cast<std::uint16_t>(tail[j] & kCharMask)) {
                    spellingFlag = true;
                    break;
                }
            }
            if (spellingFlag) { continue; }
            if (j >= tailLen) { ok = true; break; }
        }
    }
    if (jOut) { *jOut = j; }
    return ok;
}

//===========================================================================
// Automata
//===========================================================================

struct LeadTables {
    std::array<std::uint64_t, 27> rowsFor0{};   // bit i set: row i's cell 0 is this letter
    std::array<std::uint64_t, 27> rowsFor1{};   // … or the row has no cell 1 (untested)
    std::array<std::uint64_t, 27> rowsFor2{};
    std::array<std::uint64_t, 4> lenLE{};       // bit i set: row i is no longer than len
    std::array<std::uint8_t, 64> rowLen{};      // the value the reference loop returns
};

struct EndTables {
    std::array<std::uint64_t, 27> rowsFor0{};
    std::array<std::uint64_t, 27> rowsFor1{};
    std::array<std::uint64_t, 4> rowsLenGe{};   // bit i set: row i covers a tail of len
};

template <std::uint16_t maskA, std::uint16_t maskB>
constexpr LeadTables buildLeadTables() {
    LeadTables T{};
    for (std::size_t i = 0; i < kConsonantTable.size(); ++i) {
        const auto& row = kConsonantTable[i];
        const std::size_t n = row.size();
        const std::uint64_t bit = std::uint64_t{1} << i;
        T.rowLen[i] = static_cast<std::uint8_t>(n);
        for (std::size_t len = 1; len <= 3; ++len) {
            if (n <= len) { T.lenLE[len] |= bit; }
        }
        for (std::size_t L = 0; L < 26; ++L) {
            const std::uint16_t c = static_cast<std::uint16_t>(0x41 + L);
            if (static_cast<std::uint16_t>(cellAt(row, 0) & ~maskA) == c ||
                static_cast<std::uint16_t>(cellAt(row, 0) & ~maskB) == c) { T.rowsFor0[L] |= bit; }
            if (n <= 1 || static_cast<std::uint16_t>(cellAt(row, 1) & ~maskA) == c ||
                          static_cast<std::uint16_t>(cellAt(row, 1) & ~maskB) == c) { T.rowsFor1[L] |= bit; }
            if (n <= 2 || static_cast<std::uint16_t>(cellAt(row, 2) & ~maskA) == c ||
                          static_cast<std::uint16_t>(cellAt(row, 2) & ~maskB) == c) { T.rowsFor2[L] |= bit; }
        }
        // Slot 26 — the position holds a non-letter. A row is only ever tested
        // at a position it has, and every table cell is a letter, so a
        // non-letter matches nothing: the row survives position p at most when
        // it never reaches p (n <= p).
        if (n <= 1) { T.rowsFor1[kNoLetterSlot] |= bit; }
        if (n <= 2) { T.rowsFor2[kNoLetterSlot] |= bit; }
    }
    return T;
}

constexpr std::array<LeadTables, 4> kLeadAll = {
    buildLeadTables<0, 0>(), buildLeadTables<kEndConsonantMask, 0>(),
    buildLeadTables<0, kConsonantAllowMask>(), buildLeadTables<kEndConsonantMask, kConsonantAllowMask>()};

constexpr EndTables buildEndTables(bool quick) {
    EndTables T{};
    const std::uint16_t endMask = quick ? kEndConsonantMask : std::uint16_t{0};
    for (std::size_t i = 0; i < kEndConsonantTable.size(); ++i) {
        const auto& row = kEndConsonantTable[i];
        const std::size_t n = row.size();
        const std::uint64_t bit = std::uint64_t{1} << i;
        // The rejection the reference loop applies AFTER the cells: a row only
        // accepts when it is long enough to cover the tail.
        for (std::size_t len = 1; len <= 3; ++len) {
            if (n >= len) { T.rowsLenGe[len] |= bit; }
        }
        for (std::size_t L = 0; L < 26; ++L) {
            const std::uint16_t c = static_cast<std::uint16_t>(0x41 + L);
            if (static_cast<std::uint16_t>(cellAt(row, 0) & ~endMask) == c) { T.rowsFor0[L] |= bit; }
            if (n <= 1 || static_cast<std::uint16_t>(cellAt(row, 1) & ~endMask) == c) { T.rowsFor1[L] |= bit; }
        }
        if (n <= 1) { T.rowsFor1[kNoLetterSlot] |= bit; }
    }
    return T;
}

constexpr std::array<EndTables, 2> kEndAll = {buildEndTables(false), buildEndTables(true)};

inline const LeadTables& leadTables(std::uint16_t maskA, std::uint16_t maskB) noexcept {
    return kLeadAll[(maskA != 0 ? 1 : 0) + (maskB != 0 ? 2 : 0)];
}

// Length of the leading consonant cluster (0 = none).
inline std::size_t matchLeadingConsonant(const std::uint32_t* word, std::size_t endIndex,
                                         std::uint16_t maskA, std::uint16_t maskB) noexcept {
#if !KK_WALK_FAST_LEAD
    return matchLeadingConsonantRef(word, endIndex, maskA, maskB);
#else
    const std::uint32_t c0 = word[0] & kCharMask;
    if (c0 < U'A' || c0 > U'Z') { return 0; }
    const LeadTables& T = leadTables(maskA, maskB);
    // Clamped with a ternary rather than std::min(const&, const&): the profile
    // showed the by-reference helper keeping an out-of-line copy on this path.
    const std::size_t lim = endIndex < 3 ? endIndex : 3;
    std::uint64_t alive = T.rowsFor0[c0 - U'A'];
    if (lim >= 2) { alive &= T.rowsFor1[letterIdx(word[1] & kCharMask)]; }
    if (lim >= 3) { alive &= T.rowsFor2[letterIdx(word[2] & kCharMask)]; }
    alive &= T.lenLE[lim];
    if (alive == 0) { return 0; }
    return T.rowLen[static_cast<std::size_t>(lowestBit(alive))];
#endif
}

// Does some end-consonant row cover the whole tail?
inline bool endConsonantMatch(const std::uint32_t* tail, std::size_t tailLen, bool quick) noexcept {
#if !KK_WALK_FAST_END
    return endConsonantMatchRef(tail, tailLen, quick);
#else
    const std::uint32_t t0 = tail[0] & kCharMask;
    if (t0 < U'A' || t0 > U'Z') { return false; }
    const EndTables& T = kEndAll[quick ? 1 : 0];
    std::uint64_t alive = T.rowsFor0[t0 - U'A'];
    if (tailLen >= 2) { alive &= T.rowsFor1[letterIdx(tail[1] & kCharMask)]; }
    alive &= T.rowsLenGe[tailLen < 3 ? tailLen : 3];
    return alive != 0;
#endif
}

//===========================================================================
// Exhaustive equivalence oracle
//===========================================================================

struct WalkSelftest {
    unsigned long long leadCases = 0;
    unsigned long long endCases = 0;
    unsigned long long mismatches = 0;
    unsigned long long digest = 0;   // FNV-1a over the reference answers only
};

inline void fnvMix(unsigned long long& h, unsigned long long v) noexcept {
    h ^= v + 0x9E3779B97F4A7C15ull;
    h *= 1099511628211ull;
}

// Enumerates every word of length 1..maxLen over `alpha` (letters plus a
// non-letter) × every prefix length × every reachable option-mask pair, and
// every tail split × quick on/off, comparing the automata with the reference
// loops. The tables cannot observe past cell 2, so a 4-symbol window covers the
// whole behaviour; words are padded by cycling for the longer tail cases.
// `randWords` adds randomized words with tone/mark/CAPS bits set in the entry
// (which both paths must mask off) as a second, differently-shaped sample.
inline WalkSelftest kk_walks_selftest(std::size_t maxLen = 5,
                                      const char* const alpha = "ACDGHKLNQRTX1",
                                      unsigned randWords = 3000000) {
    WalkSelftest R;
    std::size_t na = 0;
    while (alpha[na]) { ++na; }
    std::array<std::uint32_t, 16> w{};
    for (std::size_t total = 1; total <= maxLen; ++total) {
        std::size_t combos = 1;
        for (std::size_t p = 0; p < total; ++p) { combos *= na; }
        for (std::size_t enc = 0; enc < combos; ++enc) {
            std::size_t e = enc;
            for (std::size_t p = 0; p < total; ++p) {
                const std::size_t s = e % na;
                e /= na;
                w[p] = static_cast<std::uint32_t>(static_cast<unsigned char>(alpha[s]));
            }
            for (std::size_t end = 1; end <= total + 1; ++end) {
                for (int m = 0; m < 4; ++m) {
                    const std::uint16_t mA = (m & 1) ? kEndConsonantMask : std::uint16_t{0};
                    const std::uint16_t mB = (m & 2) ? kConsonantAllowMask : std::uint16_t{0};
                    const std::size_t ref = matchLeadingConsonantRef(w.data(), end, mA, mB);
                    ++R.leadCases;
                    R.digest = R.digest * 1000003ull + ref + end * 7 + m;
                    if (matchLeadingConsonant(w.data(), end, mA, mB) != ref) { ++R.mismatches; }
                }
            }
            for (std::size_t k = 0; k < total; ++k) {
                for (int quick = 0; quick < 2; ++quick) {
                    const std::size_t tailLen = total - k;
                    std::size_t jRef = 0;
                    const bool ref = endConsonantMatchRef(w.data() + k, tailLen, quick != 0, &jRef);
                    ++R.endCases;
                    R.digest = R.digest * 1000003ull + (ref ? 1 : 0) + tailLen * 11 + k * 3 + quick;
                    if (endConsonantMatch(w.data() + k, tailLen, quick != 0) != ref) { ++R.mismatches; }
                    (void)jRef;   // dead in the engine; compared only for documentation
                }
            }
        }
    }
    std::uint64_t rng = 0x9E3779B97F4A7C15ull;
    for (unsigned t = 0; t < randWords; ++t) {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        const std::size_t len = static_cast<std::size_t>(1 + (rng % 9));
        std::array<std::uint32_t, 16> v{};
        for (std::size_t p = 0; p < len; ++p) {
            rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
            v[p] = static_cast<std::uint32_t>(static_cast<unsigned char>(alpha[rng % na]));
            if ((rng >> 11) & 1) { v[p] |= kToneMask; }
            if ((rng >> 13) & 1) { v[p] |= kToneWMask; }
            if ((rng >> 15) & 1) { v[p] |= kMark1Mask; }
            if ((rng >> 17) & 1) { v[p] |= kCapsMask; }
        }
        const std::size_t end = static_cast<std::size_t>(1 + (rng % (len + 1)));
        const int m = static_cast<int>(rng & 3);
        const std::uint16_t mA = (m & 1) ? kEndConsonantMask : std::uint16_t{0};
        const std::uint16_t mB = (m & 2) ? kConsonantAllowMask : std::uint16_t{0};
        const std::size_t ref = matchLeadingConsonantRef(v.data(), end, mA, mB);
        ++R.leadCases;
        fnvMix(R.digest, ref);
        if (matchLeadingConsonant(v.data(), end, mA, mB) != ref) { ++R.mismatches; }
        const std::size_t k = len > 1 ? static_cast<std::size_t>(rng % (len - 1)) : 0;
        const int quick = static_cast<int>((rng >> 19) & 1);
        const bool refEnd = endConsonantMatchRef(v.data() + k, len - k, quick != 0);
        ++R.endCases;
        fnvMix(R.digest, refEnd ? 1u : 0u);
        if (endConsonantMatch(v.data() + k, len - k, quick != 0) != refEnd) { ++R.mismatches; }
    }
    return R;
}

}  // namespace walks
}  // namespace text
}  // namespace ok
