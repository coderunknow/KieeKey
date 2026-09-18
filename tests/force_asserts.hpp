//============================================================================
// KieeKey - A modified version based on OpenKey
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
// File: tests/force_asserts.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// Force-included by every TEST target (never by product or bench targets):
//   MSVC   : /FI<path>/tests/force_asserts.hpp
//   GCC/Clang: -include<path>/tests/force_asserts.hpp
//
// WHY THIS EXISTS
// ---------------
// Several harnesses (the whole v1.3.0 suite — arcade, chaos, AI rival,
// progression, analytics, online ghost, soak_arcade — plus outputitem)
// verify through plain assert(). Under a Release configuration the
// NDEBUG that comes with it compiles every one of those asserts to
// ((void)0): the binaries then print "[PASS]" unconditionally and the
// GCC build grows -Wunused-but-set-variable noise for the values only
// the dead asserts ever read. A test that verifies nothing in the very
// configuration CI ships is worse than no test — it looks like one.
//
// Force-including this header BEFORE the translation unit's own includes
// #undefs NDEBUG, so every later <cassert>/<assert.h> inclusion defines
// the real, live assertion macro. This is deterministic on both MSVC and
// GCC/Clang and does not depend on -DNDEBUG//UNDEBUG flag ordering in the
// generated build system. Release binaries (ok_core, ok_tsf, the app)
// and the benchmark executables keep their normal NDEBUG semantics:
// shipped code must stay assert-free and benchmarks must not measure
// assertion overhead.
//----------------------------------------------------------------------------
#undef NDEBUG
#include <cassert>
