//----------------------------------------------------------------------------
// UniKey UKEngine — Linux build shim (compilation only; no engine code changes)
//----------------------------------------------------------------------------
// The vendored UniKey source targets MSVC + Win32. These three lines make it
// compile cleanly on GCC/Linux without touching the engine:
//   * `_tempnam` (MSVC CRT, used by convert.cpp) -> POSIX `tempnam`
//   * <cstring>/<cstdlib>/<cstdio> are included before the engine sources,
//     which rely on transitive includes that MSVC provides but GCC does not.
//----------------------------------------------------------------------------
#ifndef UK_FIX_H
#define UK_FIX_H

#include <cstring>
#include <cstdlib>
#include <cstdio>

#ifndef _tempnam
#define _tempnam tempnam
#endif

#endif // UK_FIX_H
