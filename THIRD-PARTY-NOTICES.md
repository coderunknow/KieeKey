# Third-Party Notices

This document lists all third-party components contained in or derived from
the KieeKey source distribution, together with their licenses and copyright
notices, as required by the GNU General Public License v3.0 (Section 4/5)
and by good open-source compliance practice.

---

## 1. OpenKey — the upstream project KieeKey is based on

| | |
|---|---|
| **Component** | OpenKey (Vietnamese input method engine) |
| **Copyright** | Copyright (C) 2019 Tuyen Mai |
| **Upstream** | https://github.com/tuyenvm/OpenKey |
| **License** | GNU General Public License v3.0 (GPL-3.0-only) |
| **Usage in KieeKey** | Base project. KieeKey is a modified version of OpenKey: the 2.0.5 engine was ported to modern C++ and refactored. |

Because OpenKey is licensed under GPL-3.0, the whole derivative work KieeKey
is licensed under the **GNU General Public License version 3 or (at your
option) any later version**. The original copyright of the upstream author is
retained in the source file headers, in the README and in this notice.

An **unmodified reference snapshot** of upstream OpenKey 2.0.5 sources is
vendored verbatim for differential testing at:

```
tests/reference/openkey-2.0.5/        (with its own LICENSE file, GPL-3.0)
```

Those files are **not modified** by KieeKey and keep their original copyright
headers.

## 2. UniKey — vendored reference engine for differential testing

| | |
|---|---|
| **Component** | UniKey Vietnamese Input Method (ukengine) |
| **Copyright** | Copyright (C) 2000-2005 Pham Kim Long |
| **Upstream** | http://unikey.org |
| **License** | GNU Lesser General Public License (LGPL) version 2 **or any later version** ("either version 2 of the License, or (at your option) any later version" — as stated in the original file headers) |
| **Usage in KieeKey** | Vendored verbatim under `tests/reference/unikey/` and used **only** by the differential-test harness (`tests/uk_driver.cpp`, `tests/engine205.*`) to cross-check KieeKey's engine output against another established Vietnamese IME. Not part of the shipped runtime engine. |

**GPLv3 compatibility note.** The original UniKey headers grant the license
"version 2 of the License, or (at your option) any later version". That
"or any later version" clause permits recipients to redistribute the covered
works under LGPL version 3, which the FSF defines as GPLv3 plus additional
permissions; consequently this vendored material is compatible with, and is
distributed to you under, the GPLv3 terms of this project as a whole. The
original copyright and license statements in those files are **preserved
verbatim** and must not be removed by further redistribution.

## 3. KieeKey modifications

All modifications, refactoring and new code contributed on top of the
upstream project — everything outside `tests/reference/` — are:

```
KieeKey v1.1.2 - refactored and completed logic
Copyright (C) 2026 coderunknow - https://github.com/coderunknow
SPDX-License-Identifier: GPL-3.0-or-later
```

KieeKey is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. KieeKey is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for details.

## 4. Build-time dependencies (not vendored)

* **Windows App SDK / WinUI 3** (optional settings UI front-end) — installed
  at build time via the official Microsoft installer/ NuGet; governed by
  Microsoft's own license terms.
* **CMake ≥ 3.28, MSVC or MinGW-w64 toolchain** — standard build tooling.

These are external tooling/runtime dependencies and are not distributed
inside this source archive.
