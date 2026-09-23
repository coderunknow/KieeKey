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
// File: src/core/Sha256.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — Sha256.hpp
// SHA-256 (FIPS 180-4), header-only, no dependencies.
//
// WHY THIS EXISTS (v1.3.0-beta8, RS-06)
//   Three test rounds in a row were spent on reports that described a build the
//   reporter was not running: an old process was still in the tray, so the
//   screenshot and the numbers came from the previous revision while the new
//   binary was being tested in a different window. "Which build produced this
//   report?" has to be answerable from the report itself, and the only value
//   that answers it unambiguously is the digest of the running executable.
//
//   The app therefore hashes ITS OWN file once (lazily, never on the input
//   path) and prints the result in the diagnostics report and in the window
//   title. The digest is compared against the published SHA-256 of the build,
//   byte for byte.
//
//   Implementation notes: plain FIPS 180-4, constant-time-free (this is an
//   integrity check for a file the user already trusts, not a MAC), no
//   allocation, no exceptions, usable on the hook thread if it ever needs to be
//   (it does not). tests/test_sha256.cpp pins it against the NIST vectors and a
//   streaming/one-shot equivalence sweep.
//----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace ok::crypto {

class Sha256 {
public:
    static constexpr std::size_t kDigestBytes = 32;
    using Digest = std::array<std::uint8_t, kDigestBytes>;

    Sha256() noexcept { reset(); }

    void reset() noexcept {
        h_[0] = 0x6a09e667U;
        h_[1] = 0xbb67ae85U;
        h_[2] = 0x3c6ef372U;
        h_[3] = 0xa54ff53aU;
        h_[4] = 0x510e527fU;
        h_[5] = 0x9b05688cU;
        h_[6] = 0x1f83d9abU;
        h_[7] = 0x5be0cd19U;
        buffered_ = 0;
        total_ = 0;
    }

    void update(const void* data, std::size_t len) noexcept {
        if (data == nullptr || len == 0) { return; }
        const auto* p = static_cast<const std::uint8_t*>(data);
        total_ += static_cast<std::uint64_t>(len);
        if (buffered_ > 0) {
            const std::size_t take = (len < (64 - buffered_)) ? len : (64 - buffered_);
            std::memcpy(block_ + buffered_, p, take);
            buffered_ += take;
            p += take;
            len -= take;
            if (buffered_ == 64) {
                compress(block_);
                buffered_ = 0;
            }
        }
        while (len >= 64) {
            compress(p);
            p += 64;
            len -= 64;
        }
        if (len > 0) {
            std::memcpy(block_, p, len);
            buffered_ = len;
        }
    }

    [[nodiscard]] Digest digest() const noexcept {
        Sha256 copy = *this;
        return copy.finalize();
    }

    [[nodiscard]] static Digest hash(const void* data, std::size_t len) noexcept {
        Sha256 s;
        s.update(data, len);
        return s.digest();
    }

private:
    [[nodiscard]] Digest finalize() noexcept {
        const std::uint64_t bits = total_ * 8ULL;
        std::uint8_t pad[72] = {};
        pad[0] = 0x80U;
        const std::size_t padLen =
            (buffered_ < 56) ? (56 - buffered_) : (120 - buffered_);
        update(pad, padLen);
        std::uint8_t lenBytes[8] = {};
        for (int i = 0; i < 8; ++i) {
            lenBytes[7 - i] = static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFU);
        }
        update(lenBytes, 8);
        Digest out{};
        for (int i = 0; i < 8; ++i) {
            out[static_cast<std::size_t>(i) * 4 + 0] = static_cast<std::uint8_t>(h_[i] >> 24);
            out[static_cast<std::size_t>(i) * 4 + 1] = static_cast<std::uint8_t>(h_[i] >> 16);
            out[static_cast<std::size_t>(i) * 4 + 2] = static_cast<std::uint8_t>(h_[i] >> 8);
            out[static_cast<std::size_t>(i) * 4 + 3] = static_cast<std::uint8_t>(h_[i]);
        }
        return out;
    }

    [[nodiscard]] static std::uint32_t rotr(std::uint32_t v, unsigned n) noexcept {
        return (v >> n) | (v << (32U - n));
    }

    void compress(const std::uint8_t* chunk) noexcept {
        static constexpr std::uint32_t k[64] = {
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
            0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
            0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
            0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
            0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
            0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
            0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
            0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
            0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

        std::uint32_t w[64] = {};
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(chunk[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(chunk[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(chunk[i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(chunk[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        std::uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
        h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
    }

    std::uint32_t h_[8] = {};
    std::uint8_t  block_[64] = {};
    std::size_t   buffered_ = 0;
    std::uint64_t total_ = 0;
};

// Lower-case hex, 64 characters: the same spelling Sha256sums.txt uses.
[[nodiscard]] inline std::string toHexLower(const Sha256::Digest& d) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(d.size() * 2);
    for (std::size_t i = 0; i < d.size(); ++i) {
        out[i * 2] = kHex[(d[i] >> 4) & 0x0FU];
        out[i * 2 + 1] = kHex[d[i] & 0x0FU];
    }
    return out;
}

}  // namespace ok::crypto
