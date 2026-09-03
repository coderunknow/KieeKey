//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.1.3 - refactored and completed logic
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
// File: src/core/CpuPause.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey v1.1.3 — CpuPause.hpp
// Portable spin-wait CPU hint for the lock-free hot paths.
//
// Why this header exists: the spin loops historically called _mm_pause()
// with an unconditional `#include <immintrin.h>` / `<emmintrin.h>`. Those
// headers are x86-family only — on an MSVC ARM64 build they hard-error with
//     fatal error C1189: #error: This header is specific to X86, X64,
//                          ARM64, and ARM64EC targets
// (observed on CI, MSVC 14.4x, -A ARM64), which killed the whole ok_core
// target. The right primitive per target is:
//
//   x86 / x64        : PAUSE   (_mm_pause)      — SSE2 pause hint
//   ARM64EC          : PAUSE   (_mm_pause)      — x86 intrinsics are
//                                                 first-class on ARM64EC
//   ARM64 / ARM32    : YIELD   (__yield)        — arm architecture hint
//   other targets    : nothing cheap and portable — fall through
//
// The mapping preserves the intent of the original PAUSE (bounded spin,
// lower power, no memory-order cost) on every architecture CI ships.
//----------------------------------------------------------------------------
#pragma once

#if defined(_MSC_VER)
    #if defined(_M_IX86) || defined(_M_X64) || defined(_M_ARM64EC)
        #include <immintrin.h>      // _mm_pause (x86/x64; legal on ARM64EC)
    #else
        #include <intrin.h>         // __yield (ARM64 / ARM32)
    #endif
#elif defined(__i386__) || defined(__x86_64__)
    #include <x86intrin.h>          // _mm_pause (GCC/Clang, incl. MinGW)
#endif

namespace ok::cpu {

// Spin-wait hint: never blocks, never allocates, no memory-order effects.
inline void pause() noexcept {
#if defined(_MSC_VER)
    #if defined(_M_IX86) || defined(_M_X64) || defined(_M_ARM64EC)
        _mm_pause();
    #elif defined(_M_ARM64) || defined(_M_ARM)
        __yield();
    #endif
#elif defined(__i386__) || defined(__x86_64__)
    _mm_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __builtin_arm_yield();
#endif
}

} // namespace ok::cpu
