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
// File: tests/test_sha256.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
// v1.3.0-beta8 (RS-06): the digest the app prints about itself must be the real
// thing. FIPS 180-4 vectors, the two padding boundaries (55/56/63/64 bytes), the
// million-character vector, and the streaming/one-shot equivalence the app's
// chunked file read depends on.
#include "Sha256.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string hexOf(const void* data, std::size_t len) {
    return ok::crypto::toHexLower(ok::crypto::Sha256::hash(data, len));
}

std::string hexOf(const std::string& s) { return hexOf(s.data(), s.size()); }

void testNistVectors() {
    // FIPS 180-4 / NIST examples.
    assert(hexOf("") ==
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(hexOf("abc") ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(hexOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    assert(hexOf("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                 "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu") ==
           "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
    std::cout << "  [PASS] SHA-256: the four FIPS 180-4 vectors\n";
}

void testPaddingBoundaries() {
    // 55 bytes: the length still fits in this block. 56: it needs a second one.
    // 63/64/65: the tail block, the exact block, and one byte past it.
    const std::size_t sizes[] = {1, 54, 55, 56, 57, 63, 64, 65, 119, 120, 128};
    for (const std::size_t n : sizes) {
        const std::vector<char> buf(n, 'a');
        const std::string oneShot = hexOf(buf.data(), buf.size());
        ok::crypto::Sha256 streamed;
        for (std::size_t i = 0; i < n; ++i) {
            streamed.update(buf.data() + i, 1);   // one byte at a time
        }
        assert(ok::crypto::toHexLower(streamed.digest()) == oneShot);
        assert(oneShot.size() == 64);
    }
    // The million-'a' vector, in the 1000-byte chunks the file reader uses.
    ok::crypto::Sha256 million;
    std::vector<char> chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) { million.update(chunk.data(), chunk.size()); }
    assert(ok::crypto::toHexLower(million.digest()) ==
           "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    std::cout << "  [PASS] SHA-256: padding boundaries + chunked == one-shot\n";
}

void testDigestIsAPureFunction() {
    ok::crypto::Sha256 s;
    const std::string a = "KieeKey";
    s.update(a.data(), a.size());
    const auto first = s.digest();
    // digest() must not consume the state (the app hashes a file in chunks and
    // may need to report the digest twice).
    assert(ok::crypto::toHexLower(s.digest()) == ok::crypto::toHexLower(first));
    const auto again = ok::crypto::Sha256::hash(a.data(), a.size());
    assert(first == again);
    // reset() reuses the object for a second file.
    s.reset();
    const std::string b = "KieeKeyApp.exe";
    s.update(b.data(), b.size());
    assert(ok::crypto::toHexLower(s.digest()) == hexOf(b));
    // The null/zero-length guards the file reader relies on.
    assert(hexOf(nullptr, 0) == hexOf(""));
    std::cout << "  [PASS] SHA-256: digest() is const, reset() is reusable\n";
}

void testBinaryDataWithNulBytes() {
    // A PE file is full of NUL bytes: the hasher must be length-driven, never
    // string-driven.
    const char bin[] = {'\0', 'K', '\0', '\0', 'y', '\0', 0x7F, '\0'};
    const std::string data(bin, sizeof(bin));
    assert(data.size() == sizeof(bin));
    assert(hexOf(data).size() == 64);
    assert(hexOf(data) != hexOf(std::string(bin, sizeof(bin) - 1)));
    std::cout << "  [PASS] SHA-256: binary input with embedded NULs\n";
}

}  // namespace

int main() {
    std::cout << "=== SHA-256 tests (v1.3.0-beta8, RS-06) ===\n";
    testNistVectors();
    testPaddingBoundaries();
    testDigestIsAPureFunction();
    testBinaryDataWithNulBytes();
    std::cout << "=== ALL SHA-256 TESTS PASSED ===\n";
    return 0;
}
