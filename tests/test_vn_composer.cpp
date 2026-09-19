//============================================================================
// KieeKey - A modified version based on OpenKey
// SPDX-License-Identifier: GPL-3.0-or-later
// File: tests/test_vn_composer.cpp
//============================================================================
// v1.3.0-beta3 (bug #2): prove the in-window Vietnamese composer the typing games
// use. The games run inside KieeKey's own window, which the keyboard hook bypasses,
// so each game composes Telex/VNI itself via VnComposer. These tests feed real
// keystrokes and assert the composed text equals the intended diacritic-bearing
// Vietnamese — the same invariant tests/real_passages.cpp validates for the IME.
//
// Build:
//   g++ -std=c++23 -O2 -Wall -Wextra -Isrc/core tests/test_vn_composer.cpp \
//       src/core/TextEngine.cpp -o /tmp/test_vn_composer -pthread
#include "VnComposer.hpp"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    if (cond) {
        std::cout << "  [ok]   " << what << "\n";
    } else {
        std::cout << "  [FAIL] " << what << "\n";
        ++g_failures;
    }
}

std::string u32To8(const std::u32string& s) {
    std::string out;
    for (char32_t c : s) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// Type a whole ASCII keystroke string (Telex/VNI) into the composer.
void typeAll(ok::arcade::VnComposer& c, const std::string& keys) {
    for (char k : keys) {
        c.feedProduced(static_cast<char32_t>(static_cast<unsigned char>(k)));
    }
}

bool composedIs(ok::arcade::VnComposer& c, const std::string& keys, const std::u32string& want) {
    c.reset();
    typeAll(c, keys);
    const bool eq = (c.text() == want);
    if (!eq) {
        std::cout << "      keys=\"" << keys << "\" composed=\"" << u32To8(c.text())
                  << "\" want=\"" << u32To8(want) << "\"\n";
    }
    return eq;
}

} // namespace

int main() {
    std::cout.sync_with_stdio(false);
    using ok::arcade::VnComposer;
    using ok::text::InputMethod;

    std::cout << "== VnComposer: Telex syllables ==\n";
    {
        VnComposer c(InputMethod::Telex);
        check(composedIs(c, "laf", U"là"), "Telex 'laf' -> 'là' (huyền)");
        check(composedIs(c, "cos", U"có"), "Telex 'cos' -> 'có' (sắc)");
        check(composedIs(c, "maj", U"mạ"), "Telex 'maj' -> 'mạ' (nặng)");
        check(composedIs(c, "vieejt", U"việt"), "Telex 'vieejt' -> 'việt' (ê + nặng)");
        check(composedIs(c, "nuowcs", U"nước"), "Telex 'nuowcs' -> 'nước' (ư + ơ + sắc)");
        check(composedIs(c, "cowm", U"cơm"), "Telex 'cowm' -> 'cơm' (horn ơ)");
    }

    std::cout << "== VnComposer: multi-word passage + spaces ==\n";
    {
        VnComposer c(InputMethod::Telex);
        // "là có" and "việt nam" built from the syllables proven above.
        check(composedIs(c, "laf cos", U"là có"), "Telex 'laf cos' -> 'là có'");
        check(composedIs(c, "vieejt nam", U"việt nam"), "Telex 'vieejt nam' -> 'việt nam'");
    }

    std::cout << "== VnComposer: capitals ==\n";
    {
        VnComposer c(InputMethod::Telex);
        // feedProduced derives caps from an uppercase ASCII letter.
        check(composedIs(c, "Laf", U"Là"), "Telex 'Laf' -> 'Là' (capital)");
        check(composedIs(c, "Vieejt", U"Việt"), "Telex 'Vieejt' -> 'Việt' (capital)");
    }

    std::cout << "== VnComposer: VNI method ==\n";
    {
        VnComposer c(InputMethod::Vni);
        check(c.method() == InputMethod::Vni, "method() reports Vni");
        check(composedIs(c, "la2", U"là"), "VNI 'la2' -> 'là' (2 = huyền)");
        check(composedIs(c, "co1", U"có"), "VNI 'co1' -> 'có' (1 = sắc)");
    }

    std::cout << "== VnComposer: matchLength / completed (game progress) ==\n";
    {
        VnComposer c(InputMethod::Telex);
        const std::u32string target = U"việt nam";
        c.reset();
        check(c.matchLength(target) == 0, "empty composed matches 0 of target");
        typeAll(c, "vieejt");
        check(c.matchLength(target) == 4, "after 'vieejt': matched 'việt' (4 code points)");
        check(!c.completed(target), "not completed before the second word");
        typeAll(c, " nam");
        check(c.matchLength(target) == target.size(), "after ' nam': full target matched");
        check(c.completed(target), "completed() true at full match");
    }

    std::cout << "== VnComposer: backspace is forgiving ==\n";
    {
        VnComposer c(InputMethod::Telex);
        c.reset();
        typeAll(c, "laf");
        check(c.text() == U"là", "composed 'là'");
        c.feedBackspace();
        // After one backspace the composer must not be longer than before.
        check(c.length() <= 2, "backspace never grows the buffer");
        c.reset();
        check(c.empty(), "reset() clears the composed text");
    }

    std::cout << "== VnComposer: a wrong key diverges (game can flag a mistake) ==\n";
    {
        VnComposer c(InputMethod::Telex);
        const std::u32string target = U"là";
        c.reset();
        typeAll(c, "las");   // 's' = sắc -> "lá", not "là"
        check(c.text() == U"lá", "Telex 'las' -> 'lá' (sắc, the wrong tone)");
        check(c.matchLength(target) == 1, "wrong tone matches only the 'l' (LCP=1)");
        check(!c.completed(target), "wrong tone is not completed");
    }

    if (g_failures == 0) {
        std::cout << "\nALL VnComposer TESTS PASSED\n";
        return 0;
    }
    std::cout << "\n" << g_failures << " VnComposer TEST(S) FAILED\n";
    return 1;
}
