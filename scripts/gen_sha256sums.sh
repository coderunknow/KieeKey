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
# PLATFORM COMPATIBILITY
# ----------------------
# This script is designed to produce byte-identical output on both Linux
# and Windows (Git Bash). It uses:
# - git show HEAD:file to hash Git-normalized content (not disk files)
# - Explicit Unix line endings (\n) in output
# - Forward slashes for all paths
# - LC_ALL=C for deterministic sorting
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
    # Generate manifest with platform-independent hashing.
    # Uses git show HEAD:file to hash the Git-normalized content (as stored in repo),
    # not the file as checked out on disk. This ensures:
    # - Consistent hashes across Linux/Windows regardless of autocrlf/eol settings
    # - Hashes match the canonical repository content
    #
    # Path normalization: converts backslashes to forward slashes for Windows compatibility.
    # Line endings: printf with \n ensures Unix line endings even on Windows.
    # Sort order: LC_ALL=C for deterministic byte-order sorting.
    
    git ls-files -z \
        | tr '\0' '\n' \
        | grep -v "^${MANIFEST}$" \
        | LC_ALL=C sort \
        | while IFS= read -r f; do
              # Skip if file doesn't exist (shouldn't happen with git ls-files, but be safe)
              [ -f "$f" ] || continue
              
              # Hash the Git-normalized content (as stored in repository)
              # This is platform-independent: always hashes the canonical version
              if ! hash=$(git show "HEAD:$f" 2>/dev/null | sha256sum | cut -d' ' -f1); then
                  echo "[sha256sums] WARNING: failed to hash $f" >&2
                  continue
              fi
              
              # Normalize path separators to forward slashes (Windows git ls-files may use backslashes)
              normalized_path=$(printf '%s' "$f" | tr '\\' '/')
              
              # Output with explicit Unix line ending (\n)
              printf '%s  %s/%s\n' "$hash" "$PREFIX" "$normalized_path"
          done
}

if [ "${1:-}" = "--check" ]; then
    # Normalize both sides: strip \r (Windows line endings) and ensure consistent comparison
    # This handles cases where:
    # - SHA256SUMS.txt was checked out with \r\n on Windows
    # - Generated output somehow has \r\n
    # - Any line ending conversion artifacts
    
    existing=$(tr -d '\r' < "$MANIFEST" 2>/dev/null || echo "")
    generated=$(generate | tr -d '\r')
    
    if [ "$existing" = "$generated" ]; then
        echo "[sha256sums] OK — $MANIFEST matches the tracked tree."
        exit 0
    fi
    
    # Show diff for debugging
    echo "[sha256sums] FAIL: $MANIFEST is stale. Run scripts/gen_sha256sums.sh" >&2
    diff -u <(echo "$existing") <(echo "$generated") | head -40 >&2
    exit 1
fi

# Generate and write manifest
generate > "$MANIFEST.tmp"
mv "$MANIFEST.tmp" "$MANIFEST"
echo "[sha256sums] wrote $MANIFEST ($(wc -l < "$MANIFEST") entries, prefix '$PREFIX/')."
