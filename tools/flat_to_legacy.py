#!/usr/bin/env python3
"""tools/flat_to_legacy.py — reconstruct the legacy std::map/vector literals
from the generated FlatTables.hpp, writing VietnameseTables.source.hpp.

Input: src/core/FlatTables.hpp (the compiled, validated tables).
Output: src/core/VietnameseTables.source.hpp (generator data source).

Run:  python3 tools/flat_to_legacy.py   (from repo root)

v1.2.2 RC4 (B-1) STATUS: this reverse-bridge was written for the v1.1.2-era
FlatTables.hpp layout (per-key FlatMap entries + FlatVec inners). The
v1.2.1 RC2 table re-layout replaced the five code tables with the FlatCodeTable
form (parallel _Off/_Len/_Data arrays indexed by (char-'A')*3 + flag), which
this tool CANNOT parse — it now fails LOUDLY (explicit format error) instead
of crashing with a KeyError mid-file. The documented regeneration path is the
FORWARD direction (VietnameseTables.source.hpp -> tools/gen_flat_tables.py),
which is proven byte-identical against the committed FlatTables.hpp. Rebuild
this bridge if the reverse direction is ever needed again.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FLAT = ROOT / "src" / "core" / "FlatTables.hpp"
OUT = ROOT / "src" / "core" / "VietnameseTables.source.hpp"

text = FLAT.read_text()

# v1.2.2 RC4 (B-1): format guard. The v1.2.1 RC2 re-layout introduced the
# FlatCodeTable form; this tool predates it. Fail with an actionable message
# instead of a KeyError (the old behavior) — and never write a partial output.
if re.search(r"constexpr\s+FlatCodeTable\s+kCodeTable", text):
    sys.stderr.write(
        "flat_to_legacy.py: UNSUPPORTED INPUT FORMAT.\n"
        "FlatTables.hpp uses the v1.2.1 RC2 FlatCodeTable layout (parallel\n"
        "_Off/_Len/_Data arrays), which this v1.1.2-era reverse bridge cannot\n"
        "parse. The documented regeneration path is the FORWARD direction:\n"
        "    python3 tools/gen_flat_tables.py\n"
        "(input: src/core/VietnameseTables.source.hpp — proven byte-identical\n"
        "against the committed FlatTables.hpp in the v1.2.2 RC4 campaign).\n"
        "Rebuild this bridge if the reverse direction is ever needed.\n")
    sys.exit(3)

def arr(name):
    """parse `constexpr T name[] = { ... };` -> list of stripped tokens"""
    m = re.search(r"\b" + re.escape(name) + r"\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise KeyError(name)
    return [t.strip() for t in m.group(1).split(",") if t.strip()]

def flat_map_entries(name):
    """parse a FlatMap<...> init: { { key, { &Inner[off], cnt } }, ... }
    returns list of (key_expr, first, count)"""
    m = re.search(r"\b" + re.escape(name) + r"\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise KeyError(name)
    body = m.group(1)
    entries = []
    for em in re.finditer(r"\{\s*(0x[0-9A-Fa-f]+|0x[0-9A-Fa-f]+\|[A-Za-z0-9_|]+|'[^']*')\s*,\s*\{\s*&[A-Za-z0-9_]+\[(\d+)\],\s*(\d+)\s*\}\s*\}", body):
        key = em.group(1)
        first, cnt = int(em.group(2)), int(em.group(3))
        entries.append((key, first, cnt))
    return entries

def inner_vecs(inner_name):
    """parse `constexpr FlatVec<T> Inner[] = { {&Data[off], len}, ... };`"""
    m = re.search(r"\b" + re.escape(inner_name) + r"\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise KeyError(inner_name)
    out = []
    for vm in re.finditer(r"\{\s*&[A-Za-z0-9_]+\[(\d+)\],\s*(\d+)\s*\}", m.group(1)):
        out.append((int(vm.group(1)), int(vm.group(2))))
    return out

def seq_lists(data_name, inner_name, entries):
    """reconstruct per-key rows: list of (key, [ [seq] ])"""
    data = arr(data_name)
    inner = inner_vecs(inner_name)
    rows = []
    for key, first, cnt in entries:
        seqs = []
        for i in range(first, first + cnt):
            off, ln = inner[i]
            seqs.append(data[off:off + ln])
        rows.append((key, seqs))
    return rows

def code_rows(table_name):
    offs = arr(table_name + "_Off")
    lens = arr(table_name + "_Len")
    data = arr(table_name + "_Data")
    rows = []
    for idx, (off, ln) in enumerate(zip(offs, lens)):
        if off == "0xFFFF":
            continue
        char = 0x41 + idx // 3
        flag = ["", "|kToneMask", "|kToneWMask"][idx % 3]
        key = f"0x{char:02X}{flag}"
        rows.append((key, data[int(off):int(off) + int(ln)]))
    return rows

def fmt_map_u16_u16(name, rows):
    lines = [f"inline const std::map<std::uint16_t, std::vector<std::uint16_t>> {name} = {{"]
    for key, row in rows:
        lines.append(f"    {{ {key}, {{{', '.join(row)}}} }},")
    lines.append("};")
    return "\n".join(lines)

def fmt_map_u16_vv(name, rows):
    lines = [f"inline const std::map<std::uint16_t, std::vector<std::vector<std::uint16_t>>> {name} = {{"]
    for key, seqs in rows:
        inner = ", ".join("{" + ", ".join(s) + "}" for s in seqs)
        lines.append(f"    {{ {key}, {{ {inner} }} }},")
    lines.append("};")
    return "\n".join(lines)

def fmt_vec_vv(name, rows):
    inner = ", ".join("{" + ", ".join(s) + "}" for s in rows)
    return f"inline const std::vector<std::vector<std::uint16_t>> {name} = {{ {inner} }};"

out = []
# v1.2.2 RC4 (B-1): GPL-3 header + version-free banner (match the committed
# file; the old "KieeKey v1.1.2 —" banner failed the version gate).
out.append("//----------------------------------------------------------------------------")
out.append("// KieeKey - A modified version based on OpenKey")
out.append("//")
out.append("// Original work:")
out.append("//   OpenKey - Vietnamese input method engine")
out.append("//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey")
out.append("//   Licensed under the GNU General Public License version 3.")
out.append("//")
out.append("// Modified work:")
out.append("//   KieeKey - refactored and completed logic")
out.append("//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow")
out.append("//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>")
out.append("//")
out.append("// This program is free software: you can redistribute it and/or modify")
out.append("// it under the terms of the GNU General Public License as published by")
out.append("// the Free Software Foundation, either version 3 of the License, or")
out.append("// (at your option) any later version.")
out.append("//")
out.append("// This program is distributed in the hope that it will be useful,")
out.append("// but WITHOUT ANY WARRANTY; without even the implied warranty of")
out.append("// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the")
out.append("// GNU General Public License for more details.")
out.append("//")
out.append("// You should have received a copy of the GNU General Public License")
out.append("// along with this program. If not, see <https://www.gnu.org/licenses/>.")
out.append("//")
out.append("// File: src/core/VietnameseTables.source.hpp")
out.append("// SPDX-License-Identifier: GPL-3.0-or-later")
out.append("//============================================================================")
out.append("//----------------------------------------------------------------------------")
out.append("// KieeKey — VietnameseTables.source.hpp (GENERATOR SOURCE ONLY)")
out.append("//")
out.append("// Legacy std::map/vector literals reconstructed from the validated data in")
out.append("// FlatTables.hpp (see tools/flat_to_legacy.py). This file is NOT included by")
out.append("// any translation unit; it exists solely as the input format expected by")
out.append("// tools/gen_flat_tables.py so the flat tables can be regenerated/audited.")
out.append("//----------------------------------------------------------------------------")
out.append("#pragma once")
out.append("")
out.append("#include <cstdint>")
out.append("#include <map>")
out.append("#include <vector>")
out.append("")
out.append("namespace ok::text {")
out.append("")
out.append("inline constexpr std::uint16_t kEndConsonantMask   = 0x4000u;")
out.append("inline constexpr std::uint16_t kConsonantAllowMask = 0x8000u;")
out.append("inline constexpr std::uint32_t kToneMask  = 0x20000u;")
out.append("inline constexpr std::uint32_t kToneWMask = 0x40000u;")
out.append("inline constexpr std::uint32_t kCapsMask  = 0x10000u;")
out.append("")

# code tables
for t in ["kCodeTableUnicode", "kCodeTableTcvn3", "kCodeTableVniWindows",
          "kCodeTableUnicodeCompound", "kCodeTableCp1258"]:
    rows = code_rows(t)
    # order like the original: plain letters first? keep map order (sorted by key)
    rows.sort(key=lambda kv: (int(kv[0].split("|")[0], 16), kv[0]))
    out.append(fmt_map_u16_u16(t, rows))
    out.append("")

# vowel maps
for t in ["kVowel", "kVowelForMark"]:
    entries = flat_map_entries(t)
    rows = seq_lists(t + "_Inner_Data", t + "_Inner", entries)
    out.append(fmt_map_u16_vv(t, rows))
    out.append("")
# kVowelCombine (uint32 elements)
entries = flat_map_entries("kVowelCombine")
rows = seq_lists("kVowelCombine_Inner_Data", "kVowelCombine_Inner", entries)
lines = ["inline const std::map<std::uint16_t, std::vector<std::vector<std::uint32_t>>> kVowelCombine = {"]
for key, seqs in rows:
    inner = ", ".join("{" + ", ".join(s) + "}" for s in seqs)
    lines.append(f"    {{ {key}, {{ {inner} }} }},")
lines.append("};")
out.append("\n".join(lines))
out.append("")

# plain tables
for t in ["kConsonantD", "kConsonantTable", "kEndConsonantTable"]:
    inner = inner_vecs(t + "_Inner")
    data = arr(t + "_Inner_Data")
    rows = [data[off:off + ln] for off, ln in inner]
    out.append(fmt_vec_vv(t, rows))
    out.append("")

# kStandaloneWBad
data = arr("kStandaloneWBad_Data")
out.append(f"inline const std::vector<std::uint16_t> kStandaloneWBad = {{ {', '.join(data)} }};")
out.append("")

# kDoubleWAllowed
inner = inner_vecs("kDoubleWAllowed_Inner")
data = arr("kDoubleWAllowed_Inner_Data")
rows = [data[off:off + ln] for off, ln in inner]
out.append(fmt_vec_vv("kDoubleWAllowed", rows))
out.append("")

# quick maps
for t in ["kQuickStartConsonant", "kQuickEndConsonant", "kQuickTelex"]:
    entries = flat_map_entries(t)
    rows = []
    for key, first, cnt in entries:
        # value = single FlatVec (2 elements each)
        inner = inner_vecs(t + "_Inner")
        off, ln = inner[first]
        data = arr(t + "_Data")
        rows.append((key, data[off:off + ln]))
    out.append(fmt_map_u16_u16(t, rows))
    out.append("")

out.append("} // namespace ok::text")
out.append("")
OUT.write_text("\n".join(out))
print(f"wrote {OUT} ({len(out)} lines)")
