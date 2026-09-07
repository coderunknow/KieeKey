#!/usr/bin/env bash
#============================================================================
# KieeKey - A modified version based on OpenKey
#
# Original work:
#   OpenKey - Vietnamese input method engine
#   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
#   Licensed under the GNU General Public License version 3.
#
# Modified work:
#   KieeKey - refactored and completed logic
#   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
#   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.
#
# File: scripts/gen_sha256sums.sh
# SPDX-License-Identifier: GPL-3.0-or-later
#============================================================================
#
# Regenerate SHA256SUMS.txt over every git-tracked file.
#
# WHY THIS EXISTS
# ---------------
# SHA256SUMS.txt was maintained by hand and rotted exactly the way the
# version identifiers did: by v1.2.2 RC3 it recorded stale hashes for 16
# files (CHANGELOG, CMakeLists, TextEngine.*, ModernKeyHook.*, main.cpp,
# kieekey_core.hpp, run_all_tests.sh, …) and omitted ~62 tracked files
# entirely (all of docs/bench/rc2-122/ and rc3-122/). A manifest that
# neither covers the tree nor matches it cannot verify anything — it is
# worse than no manifest, because it looks authoritative.
#
# The file list is `git ls-files`, so the manifest tracks the repository
# by construction. Paths are recorded with the historical "KieeKey/" prefix
# so the file verifies from the PARENT of the checkout:
#
#     sha256sum -c KieeKey/SHA256SUMS.txt
#
# SHA256SUMS.txt cannot hash itself, so it is the one excluded entry.
#
# Usage:
#     scripts/gen_sha256sums.sh            # rewrite SHA256SUMS.txt
#     scripts/gen_sha256sums.sh --check    # verify, do not write (exit 1 on drift)
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MANIFEST="SHA256SUMS.txt"
# Use hardcoded prefix for deterministic output regardless of checkout directory name
PREFIX="KieeKey"

cd "$REPO_ROOT"

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "[sha256sums] FAIL: not a git checkout — the file list comes from git ls-files" >&2
    exit 1
fi

generate() {
    # -z keeps paths with spaces/unicode intact; LC_ALL=C gives a stable order
    # so an unchanged tree always produces a byte-identical manifest.
    git ls-files -z \
        | tr '\0' '\n' \
        | grep -v "^${MANIFEST}$" \
        | LC_ALL=C sort \
        | while IFS= read -r f; do
              [ -f "$f" ] || continue
              printf '%s  %s/%s\n' "$(sha256sum -- "$f" | cut -d' ' -f1)" "$PREFIX" "$f"
          done
}

if [ "${1:-}" = "--check" ]; then
    # Normalize line endings (strip \r) on both sides to handle Windows checkouts
    if diff -u <(tr -d '\r' < "$MANIFEST" 2>/dev/null) <(generate) >/tmp/sha256sums.diff 2>&1; then
        echo "[sha256sums] OK — $MANIFEST matches the tracked tree."
        exit 0
    fi
    echo "[sha256sums] FAIL: $MANIFEST is stale. Run scripts/gen_sha256sums.sh" >&2
    sed -n '1,40p' /tmp/sha256sums.diff >&2
    exit 1
fi

generate > "$MANIFEST.tmp"
mv "$MANIFEST.tmp" "$MANIFEST"
echo "[sha256sums] wrote $MANIFEST ($(wc -l < "$MANIFEST") entries, prefix '$PREFIX/')."
