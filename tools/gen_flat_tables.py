#!/usr/bin/env python3
"""Generate src/core/FlatTables.hpp from the validated table literals in
src/core/VietnameseTables.source.hpp (v1.2.2 RC4: the docstring previously
named VietnameseTables.hpp, which is the SHIPPED engine header — not this
tool's input).

The engine's hot path is dominated by std::map/std::vector lookups (RB-tree
walks with pointer chasing) on tables whose keys are single uppercase letters.
This generator mechanically converts the *exact same data* into constexpr
flat storage:

  * FlatVec<T>            — (ptr,len) view, size()/operator[]/begin/end
  * FlatMap<K,V,N>        — sorted entries, constexpr binary-search find()
  * FlatCodeTable         — code tables indexed by (char, tone-flag) O(1)

Everything is compile-time computed; the engine keeps the same names and a
std::map-compatible interface (find/end/begin/->second/[i]/size()), so the
algorithm source compiles unchanged and semantics are bit-identical.

Run:  python3 tools/gen_flat_tables.py   (from repo root)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src" / "core" / "VietnameseTables.source.hpp"
OUT = ROOT / "src" / "core" / "FlatTables.hpp"

MASKS = {
    "kEndConsonantMask": 0x4000,
    "kConsonantAllowMask": 0x8000,
    "kCapsMask": 0x10000,
    "kToneMask": 0x20000,
    "kToneWMask": 0x40000,
    "kMark1Mask": 0x80000,
    "kMark2Mask": 0x100000,
    "kMark3Mask": 0x200000,
    "kMark4Mask": 0x400000,
    "kMark5Mask": 0x800000,
    "kMarkMask": 0xF80000,
    "kCharMask": 0xFFFF,
    "kStandaloneMask": 0x1000000,
    "kCharCodeMask": 0x2000000,
    "kPureCharMask": 0x80000000,
}

def eval_expr(e: str) -> int:
    e = e.strip()
    # replace mask names with values, evaluate as C-ish bitwise expr
    for name, val in MASKS.items():
        e = e.replace(name, str(val))
    return _eval_c(e)

def _eval_c(s: str) -> int:
    # minimal C bitwise expression evaluator (no arithmetic +/-)
    # tokenize
    tokens = re.findall(r"0x[0-9A-Fa-f]+|\d+|[|&~<>()]", s)
    pos = [0]
    def peek():
        return tokens[pos[0]] if pos[0] < len(tokens) else ""
    def take():
        t = tokens[pos[0]]; pos[0] += 1; return t
    def parse_primary():
        t = peek()
        if t == "(":
            take()
            v = parse_or()
            assert take() == ")"
            return v
        if t == "~":
            take()
            return ~parse_primary() & 0xFFFFFFFF
        v = int(t, 0)
        take()
        return v
    def parse_shift():
        v = parse_primary()
        while peek() in ("<", ">"):
            op = take() + take()
            r = parse_primary()
            v = (v << r) if op == "<<" else (v >> r)
        return v
    def parse_and():
        v = parse_shift()
        while peek() == "&":
            take(); v &= parse_shift()
        return v
    def parse_or():
        v = parse_and()
        while peek() == "|":
            take(); v |= parse_and()
        return v
    return parse_or() & 0xFFFFFFFF


def emit_vowel_combine_pair_lut(fout, entries):
    """Emit the M-6 direct pair verdict table for canHasEndConsonant().

    kVowelCombine rows are [allowEnd, v1, v2, ...].  canHasEndConsonant() only
    asks the table for two-vowel groups on the hot mark-placement path; rows
    longer than [flag,v1,v2] are deliberately left to the legacy fallback.
    """
    dim = 26 * 4  # A-Z times {plain, ^/tone, horn, both}; both is verified empty-compatible.
    table = [0] * (dim * dim)  # 0 missing, 1 present+reject, 2 present+allow
    rows = []
    for _key, seqs in entries:
        for row in seqs:
            if len(row) != 3:
                continue
            vals = [eval_expr(x) for x in row]
            def enc(v):
                base = v & 0xFFFF
                if base < 0x41 or base > 0x5A:
                    raise ValueError(f"bad vowel-combine codepoint 0x{v:x}")
                flags = (1 if (v & MASKS["kToneMask"]) else 0) | (2 if (v & MASKS["kToneWMask"]) else 0)
                return (base - 0x41) * 4 + flags
            idx = enc(vals[1]) * dim + enc(vals[2])
            verdict = 2 if vals[0] else 1
            if table[idx] and table[idx] != verdict:
                raise ValueError(f"conflicting kVowelCombine pair row at {row}")
            if table[idx] == 0:
                rows.append((vals[1], vals[2], verdict))
            table[idx] = verdict

    fout.write("// ---- vowel-combine pair verdict LUT (M-6) ----------------------------\n")
    fout.write("// Generated from kVowelCombine. Values: 0=missing/false, 1=present+false, 2=present+true.\n")
    fout.write("constexpr std::size_t kVowelCombinePairAlphabet = 26;\n")
    fout.write("constexpr std::size_t kVowelCombinePairFlagStates = 4;\n")
    fout.write("constexpr std::size_t kVowelCombinePairDim = kVowelCombinePairAlphabet * kVowelCombinePairFlagStates;\n")
    fout.write("constexpr std::size_t kVowelCombinePairLutSize = kVowelCombinePairDim * kVowelCombinePairDim;\n")
    fout.write(f"constexpr std::size_t kVowelCombinePairLutPopcount = {len(rows)};\n")
    fout.write("[[nodiscard]] constexpr std::size_t vowelCombinePairComponentIndex(std::uint32_t v) noexcept {\n")
    fout.write("    const std::uint32_t base = v & kCharMask;\n")
    fout.write("    if (base < U'A' || base > U'Z') { return kVowelCombinePairDim; }\n")
    fout.write("    const std::uint32_t flags = ((v & kToneMask) ? 1u : 0u) | ((v & kToneWMask) ? 2u : 0u);\n")
    fout.write("    return static_cast<std::size_t>(base - U'A') * kVowelCombinePairFlagStates + flags;\n")
    fout.write("}\n")
    fout.write("[[nodiscard]] constexpr std::size_t vowelCombinePairIndex(std::uint32_t a, std::uint32_t b) noexcept {\n")
    fout.write("    const std::size_t ai = vowelCombinePairComponentIndex(a);\n")
    fout.write("    const std::size_t bi = vowelCombinePairComponentIndex(b);\n")
    fout.write("    return (ai >= kVowelCombinePairDim || bi >= kVowelCombinePairDim) ? kVowelCombinePairLutSize : (ai * kVowelCombinePairDim + bi);\n")
    fout.write("}\n")
    fout.write("constexpr std::array<std::uint8_t, kVowelCombinePairLutSize> kVowelCombinePairLut = {\n    {\n")
    for off in range(0, len(table), 32):
        chunk = table[off:off + 32]
        line = ", ".join(str(v) for v in chunk)
        if off + 32 < len(table):
            line += ","
        fout.write(f"        {line}\n")
    fout.write("    }\n};\n")
    fout.write("[[nodiscard]] constexpr bool vowelCombinePairAllowsEnd(std::uint32_t a, std::uint32_t b) noexcept {\n")
    fout.write("    const std::size_t idx = vowelCombinePairIndex(a, b);\n")
    fout.write("    return idx < kVowelCombinePairLutSize && kVowelCombinePairLut[idx] == 2;\n")
    fout.write("}\n")
    fout.write("[[nodiscard]] constexpr bool verifyVowelCombinePairLut() noexcept {\n")
    fout.write("    std::size_t seen = 0;\n")
    fout.write("    for (std::size_t g = 0; g < kVowelCombine.size(); ++g) {\n")
    fout.write("        const auto& rows = kVowelCombine.entries[g].second;\n")
    fout.write("        for (std::size_t r = 0; r < rows.size(); ++r) {\n")
    fout.write("            const auto& row = rows[r];\n")
    fout.write("            if (row.size() != 3) { continue; }\n")
    fout.write("            const std::size_t idx = vowelCombinePairIndex(row[1], row[2]);\n")
    fout.write("            if (idx >= kVowelCombinePairLutSize) { return false; }\n")
    fout.write("            const std::uint8_t want = row[0] == 1 ? 2u : 1u;\n")
    fout.write("            if (kVowelCombinePairLut[idx] != want) { return false; }\n")
    fout.write("            ++seen;\n")
    fout.write("        }\n")
    fout.write("    }\n")
    fout.write("    std::size_t actual = 0;\n")
    fout.write("    for (std::uint8_t v : kVowelCombinePairLut) { if (v != 0) { ++actual; } }\n")
    fout.write("    return seen == kVowelCombinePairLutPopcount && actual == kVowelCombinePairLutPopcount;\n")
    fout.write("}\n")
    fout.write("static_assert(verifyVowelCombinePairLut(), \"kVowelCombine pair LUT drifted from source verdicts\");\n\n")

# ---------------------------------------------------------------------------
# tokenizer over an initializer list
# ---------------------------------------------------------------------------
class Parser:
    def __init__(self, text: str):
        self.text = text
        self.i = 0
        self.n = len(text)

    def skip_ws(self):
        while self.i < self.n and self.text[self.i] in " \t\r\n":
            self.i += 1

    def peek(self):
        self.skip_ws()
        if self.i >= self.n:
            return None
        return self.text[self.i]

    def parse_value(self):
        """Parse one initializer-list element -> ('list', [children]) | ('atom', str)."""
        self.skip_ws()
        if self.i >= self.n:
            raise ValueError("unexpected EOF")
        if self.text[self.i] == "{":
            self.i += 1
            children = []
            while True:
                self.skip_ws()
                if self.i >= self.n:
                    raise ValueError("unclosed brace")
                if self.text[self.i] == "}":
                    self.i += 1
                    break
                children.append(self.parse_value())
                self.skip_ws()
                if self.i < self.n and self.text[self.i] == ",":
                    self.i += 1
            return ("list", children)
        # atom: one or more tokens connected by bitwise operators, e.g.
        # `0x41 | kToneMask` is a single key expression. Tokens are separated
        # by whitespace and/or operators; we merge them into one atom.
        def read_token():
            while self.i < self.n and self.text[self.i] in " \t\r\n":
                self.i += 1
            start = self.i
            while self.i < self.n and self.text[self.i] not in ",{} \t\r\n|&~()<>":
                self.i += 1
            return self.text[start:self.i]

        atom = read_token()
        # merge: <op><ws><token> while an operator follows the atom
        while True:
            j = self.i
            while j < self.n and self.text[j] in " \t\r\n":
                j += 1
            if j >= self.n or self.text[j] not in "|&~()<>":
                break
            op_start = j
            while j < self.n and self.text[j] in "|&~()<>":
                j += 1
            op = self.text[op_start:j]
            # look for a following token (skip ws)
            k = j
            while k < self.n and self.text[k] in " \t\r\n":
                k += 1
            if k >= self.n or self.text[k] in ",{}":
                break  # dangling operator; leave it for the caller
            # commit the merge: move self.i to end of the next token
            self.i = j
            token2 = read_token()
            atom = atom + op + token2
        return ("atom", atom)

# ---------------------------------------------------------------------------
# extract table bodies
# ---------------------------------------------------------------------------
TABLES = [
    "kCodeTableUnicode", "kCodeTableTcvn3", "kCodeTableVniWindows",
    "kCodeTableUnicodeCompound", "kCodeTableCp1258",
    "kVowel", "kVowelCombine", "kVowelForMark",
    "kConsonantD", "kConsonantTable", "kEndConsonantTable",
    "kStandaloneWBad", "kDoubleWAllowed",
    "kQuickStartConsonant", "kQuickEndConsonant", "kQuickTelex",
]

def find_table_body(text: str, name: str):
    """Return ('map', entries) or ('list', items) for the named table."""
    m = re.search(r"\binline\s+const\s+[^{;]*?\b" + re.escape(name) + r"\s*=\s*(\{)", text)
    if not m:
        raise ValueError(f"table {name} not found")
    start = m.start(1)
    p = Parser(text[start:])
    node = p.parse_value()
    assert node[0] == "list", name
    children = node[1]
    # MAP entry = { KEY_ATOM, { ... } } i.e. a 2-list whose 2nd element is a LIST.
    # Plain rows like {0x54,0x52} have a 2nd element that is an ATOM -> not a map.
    def is_map2(c):
        if c[0] != "list" or len(c[1]) != 2:
            return False
        k, v = c[1]
        return k[0] == "atom" and v[0] == "list"
    if children and all(is_map2(c) for c in children):
        return ("map", node)
    return ("list", node)

def split_map_entries(node):
    # node = ('list', [entry, entry, ...]) where entry = ('list', [KEY, value])
    return node[1]

# ---------------------------------------------------------------------------
# emission helpers
# ---------------------------------------------------------------------------
def emit_flat(out, name, key_type, val_type, entries, data_elems, inner_name):
    """Emit a FlatMap from entries [(key_atom, seqlist)] where each seqlist is
    a list of sequences (lists of atoms). Lays out: inner FlatVec array +
    data array + FlatMap. Returns nothing."""
    seqs = []          # list of list-of-atoms
    key_rows = []      # (key_atom, first_seq_index, count)
    for key_atom, rowlist in entries:
        first = len(seqs)
        for seq in rowlist:
            seqs.append(seq)
        key_rows.append((key_atom, first, len(rowlist)))

    # data array
    flat_data = []
    for seq in seqs:
        flat_data.extend(seq)
    data_expr = ", ".join(flat_data) if flat_data else "0"

    # inner vec array
    inner_lines = []
    off = 0
    for seq in seqs:
        inner_lines.append(f"{{&{inner_name}_Data[{off}], {len(seq)}}}")
        off += len(seq)
    inner_expr = ",\n    ".join(inner_lines)

    out.write(f"// {name}: {len(key_rows)} keys, {len(seqs)} sequences\n")
    out.write(f"constexpr {val_type} {inner_name}_Data[] = {{ {data_expr} }};\n")
    out.write(f"constexpr FlatVec<{val_type}> {inner_name}[] = {{\n    {inner_expr}\n}};\n")
    entries_expr = ",\n    ".join(
        f"{{ {k}, {{ &{inner_name}[{first}], {cnt} }} }}"
        for k, first, cnt in key_rows)
    out.write(f"constexpr FlatMap<{key_type}, FlatVec<FlatVec<{val_type}>>, {len(key_rows)}> {name} = {{\n    {{ {entries_expr} }}\n}};\n\n")

def emit_flat_list(out, name, val_type, rows, data_name):
    """Emit a plain FlatVec<FlatVec<T>> from rows = list of sequences."""
    flat_data = []
    inner_lines = []
    off = 0
    for seq in rows:
        flat_data.extend(seq)
        inner_lines.append(f"{{&{data_name}_Data[{off}], {len(seq)}}}")
        off += len(seq)
    data_expr = ", ".join(flat_data) if flat_data else "0"
    inner_expr = ",\n    ".join(inner_lines)
    out.write(f"constexpr {val_type} {data_name}_Data[] = {{ {data_expr} }};\n")
    out.write(f"constexpr FlatVec<{val_type}> {data_name}[] = {{\n    {inner_expr}\n}};\n")
    out.write(f"constexpr FlatVec<FlatVec<{val_type}>> {name} = {{ &{data_name}[0], {len(rows)} }};\n\n")

def emit_code_table(out, name, entries):
    """Code table: map key -> flat row (single sequence). Rows indexed by
    rowIdx = (char-'A')*3 + flagIdx  (flagIdx: 0 plain, 1 hat, 2 w)."""
    # collect rows per char
    char_rows = {}  # char -> {flagidx: row-atoms}
    for key_atom, row in entries:
        # row = list of atom strings (single row per key)
        key_val = eval_expr(key_atom)
        char = key_val & 0xFF
        flag = (key_val >> 17) & 0x7
        char_rows.setdefault(char, {})[flag] = row
    # build data + offsets
    data = []
    offs = []
    lens = []
    for c in range(0x41, 0x5B):
        for f in range(3):
            row = char_rows.get(c, {}).get(f)
            if row is None:
                offs.append("0xFFFF")
                lens.append("0")
            else:
                offs.append(str(len(data)))
                lens.append(str(len(row)))
                data.extend(row)
    data_expr = ", ".join(data) if data else "0"
    off_expr = ", ".join(offs)
    len_expr = ", ".join(lens)
    out.write(f"// {name}: rows indexed by (char-'A')*3 + flag(0=plain,1=hat,2=w)\n")
    out.write(f"constexpr std::uint16_t {name}_Data[] = {{ {data_expr} }};\n")
    out.write(f"constexpr std::array<std::uint16_t, 78> {name}_Off = {{ {off_expr} }};\n")
    out.write(f"constexpr std::array<std::uint8_t, 78> {name}_Len = {{ {len_expr} }};\n")
    out.write(f"constexpr FlatCodeTable {name} = {{ {name}_Off, {name}_Len, {name}_Data }};\n\n")

# ---------------------------------------------------------------------------
def main():
    text = SRC.read_text()
    out = []
    # v1.2.2 RC4 (B-1): emit the exact GPL-3 header + version-free banner the
    # committed file carries. The previous versioned banner ("KieeKey v1.1.2 —")
    # failed the check_version.py boilerplate gate and, worse, regeneration
    # stripped the license header from this GPL file entirely.
    out.append("//============================================================================")
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
    out.append("// File: src/core/FlatTables.hpp")
    out.append("// SPDX-License-Identifier: GPL-3.0-or-later")
    out.append("//============================================================================")
    out.append("//----------------------------------------------------------------------------")
    out.append("// KieeKey — FlatTables.hpp (GENERATED by tools/gen_flat_tables.py)")
    out.append("//")
    out.append("// Compile-time flattened lookup tables: the same validated phonetics data as")
    out.append("// VietnameseTables.hpp, re-laid-out as constexpr flat storage so the hot path")
    out.append("// does O(1)/binary-search lookups with NO heap, NO std::map RB-tree walks and")
    out.append("// NO pointer chasing. Interface keeps the std::map/vector shape the engine")
    out.append("// already uses (find/end/begin/->second/[i]/size), so TextEngine.cpp compiles")
    out.append("// unchanged against these types. DO NOT EDIT BY HAND — regenerate instead.")
    out.append("//----------------------------------------------------------------------------")
    out.append("#pragma once")
    out.append("")
    out.append("#include <array>")
    out.append("#include <cstddef>")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace ok::text {")
    out.append("")
    out.append("//---------------------------------------------------------------------------")
    out.append("// FlatVec<T> — (ptr,len) read-only view with std::vector-like access.")
    out.append("//---------------------------------------------------------------------------")
    out.append("template <class T>")
    out.append("struct FlatVec {")
    out.append("    const T* p = nullptr;")
    out.append("    std::size_t n = 0;")
    out.append("    constexpr FlatVec() noexcept = default;")
    out.append("    constexpr FlatVec(const T* pp, std::size_t nn) noexcept : p(pp), n(nn) {}")
    out.append("    [[nodiscard]] constexpr std::size_t size() const noexcept { return n; }")
    out.append("    [[nodiscard]] constexpr bool empty() const noexcept { return n == 0; }")
    out.append("    [[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept { return p[i]; }")
    out.append("    [[nodiscard]] constexpr const T* data() const noexcept { return p; }")
    out.append("    [[nodiscard]] constexpr const T* begin() const noexcept { return p; }")
    out.append("    [[nodiscard]] constexpr const T* end() const noexcept { return p + n; }")
    out.append("};")
    out.append("")
    out.append("//---------------------------------------------------------------------------")
    out.append("// FlatMap<K,V,N> — sorted constexpr entries, binary-search find().")
    out.append("//---------------------------------------------------------------------------")
    out.append("template <class K, class V, std::size_t N>")
    out.append("struct FlatMap {")
    out.append("    struct Entry { K key; V second; };")
    out.append("    Entry entries[N]{};")
    out.append("    struct const_iterator {")
    out.append("        const Entry* e = nullptr;")
    out.append("        constexpr const_iterator() noexcept = default;")
    out.append("        constexpr explicit const_iterator(const Entry* p) noexcept : e(p) {}")
    out.append("        constexpr const_iterator& operator++() noexcept { ++e; return *this; }")
    out.append("        [[nodiscard]] constexpr bool operator==(const const_iterator& o) const noexcept { return e == o.e; }")
    out.append("        [[nodiscard]] constexpr bool operator!=(const const_iterator& o) const noexcept { return e != o.e; }")
    out.append("        [[nodiscard]] constexpr const Entry& operator*() const noexcept { return *e; }")
    out.append("        [[nodiscard]] constexpr const Entry* operator->() const noexcept { return e; }")
    out.append("    };")
    out.append("    [[nodiscard]] constexpr std::size_t size() const noexcept { return N; }")
    out.append("    [[nodiscard]] constexpr const_iterator begin() const noexcept { return const_iterator(entries); }")
    out.append("    [[nodiscard]] constexpr const_iterator end() const noexcept { return const_iterator(entries + N); }")
    out.append("    [[nodiscard]] constexpr const_iterator find(K k) const noexcept {")
    out.append("        const Entry* e = entries;")
    out.append("        std::size_t lo = 0, hi = N;")
    out.append("        while (lo < hi) {")
    out.append("            const std::size_t mid = (lo + hi) >> 1;")
    out.append("            if (e[mid].key < k) { lo = mid + 1; } else { hi = mid; }")
    out.append("        }")
    out.append("        if (lo < N && e[lo].key == k) { return const_iterator(e + lo); }")
    out.append("        return end();")
    out.append("    }")
    out.append("};")
    out.append("")
    out.append("//---------------------------------------------------------------------------")
    out.append("// FlatCodeTable — code-table rows indexed O(1) by (char-'A')*3 + flagIdx.")
    out.append("//---------------------------------------------------------------------------")
    out.append("struct FlatCodeTable {")
    out.append("    std::array<std::uint16_t, 78> off;   // 0xFFFF = row absent")
    out.append("    std::array<std::uint8_t, 78> len;")
    out.append("    const std::uint16_t* data;")
    out.append("    struct Entry { std::uint32_t key; FlatVec<std::uint16_t> second; };")
    out.append("    struct const_iterator {")
    out.append("        Entry entry{};")
    out.append("        bool valid = false;")
    out.append("        constexpr const_iterator() noexcept = default;")
    out.append("        constexpr const_iterator(std::uint32_t k, std::size_t row, const FlatCodeTable* t, bool ok) noexcept")
    out.append("            : entry{k, ok ? FlatVec<std::uint16_t>{t->data + t->off[row], t->len[row]} : FlatVec<std::uint16_t>{}},")
    out.append("              valid(ok) {}")
    out.append("        [[nodiscard]] constexpr bool operator==(const const_iterator& o) const noexcept { return valid == o.valid; }")
    out.append("        [[nodiscard]] constexpr bool operator!=(const const_iterator& o) const noexcept { return valid != o.valid; }")
    out.append("        [[nodiscard]] constexpr const Entry& operator*() const noexcept { return entry; }")
    out.append("        [[nodiscard]] constexpr const Entry* operator->() const noexcept { return &entry; }")
    out.append("    };")
    out.append("    [[nodiscard]] constexpr const_iterator end() const noexcept { return const_iterator{}; }")
    out.append("    [[nodiscard]] constexpr const_iterator find(std::uint32_t key) const noexcept {")
    out.append("        const std::uint32_t c = key & 0xFFu;")
    out.append("        if (c < 0x41u || c > 0x5Au) { return end(); }")
    out.append("        const std::size_t idx = static_cast<std::size_t>(c - 0x41u) * 3 + ((key >> 17) & 7u);")
    out.append("        if (off[idx] == 0xFFFFu) { return end(); }")
    out.append("        return const_iterator(key, idx, this, true);")
    out.append("    }")
    out.append("};")
    out.append("")
    out.append("//---------------------------------------------------------------------------")
    out.append("// Generated data (semantics identical to VietnameseTables.hpp)")
    out.append("//---------------------------------------------------------------------------")
    out.append("")

    text_out = "\n".join(out)
    with open(OUT, "w") as f:
        f.write(text_out)

    # Now append table data by parsing the ORIGINAL header
    fout = open(OUT, "a")
    fout.write("// ---- code tables ----------------------------------------------------\n")
    for name in ["kCodeTableUnicode", "kCodeTableTcvn3", "kCodeTableVniWindows",
                 "kCodeTableUnicodeCompound", "kCodeTableCp1258"]:
        kind, node = find_table_body(text, name)
        assert kind == "map", name
        entries = []
        for entry in split_map_entries(node):
            key_atom = entry[1][0][1]
            value = entry[1][1]
            # value = list of atoms (single row)
            row = [a[1] for a in value[1]]
            entries.append((key_atom, row))
        emit_code_table(fout, name, entries)

    fout.write("// ---- vowel structural maps ------------------------------------------\n")
    for name, vtype in [("kVowel", "std::uint16_t"),
                        ("kVowelCombine", "std::uint32_t"),
                        ("kVowelForMark", "std::uint16_t")]:
        kind, node = find_table_body(text, name)
        assert kind == "map", name
        entries = []
        for entry in split_map_entries(node):
            key_atom = entry[1][0][1]
            value = entry[1][1]   # list of rows
            seqs = [[a[1] for a in row[1]] for row in value[1]]
            entries.append((key_atom, seqs))
        # sort by key for binary search (std::map order)
        entries.sort(key=lambda kv: eval_expr(kv[0]))
        emit_flat(fout, name, "std::uint16_t", vtype, entries,
                  f"{name}", f"{name}_Inner")
        if name == "kVowelCombine":
            vowel_combine_entries = entries

    emit_vowel_combine_pair_lut(fout, vowel_combine_entries)

    fout.write("// ---- plain structural tables ----------------------------------------\n")
    for name in ["kConsonantD", "kConsonantTable", "kEndConsonantTable"]:
        kind, node = find_table_body(text, name)
        assert kind == "list", name
        rows = [[a[1] for a in row[1]] for row in node[1]]
        emit_flat_list(fout, name, "std::uint16_t", rows, name + "_Inner")

    kind, node = find_table_body(text, "kStandaloneWBad")
    assert kind == "list", "kStandaloneWBad"
    row = node[1]
    atoms = [r[1] for r in row] if row and row[0][0] == "list" else [a[1] for a in row]
    fout.write(f"constexpr std::uint16_t kStandaloneWBad_Data[] = {{ {', '.join(atoms)} }};\n")
    fout.write("constexpr FlatVec<std::uint16_t> kStandaloneWBad = { &kStandaloneWBad_Data[0], "
               f"{len(atoms)} }};\n\n")

    kind, node = find_table_body(text, "kDoubleWAllowed")
    assert kind == "list", "kDoubleWAllowed"
    rows = [[a[1] for a in row[1]] for row in node[1]]
    emit_flat_list(fout, "kDoubleWAllowed", "std::uint16_t", rows, "kDoubleWAllowed_Inner")

    fout.write("// ---- quick-telex maps ------------------------------------------------\n")
    for name in ["kQuickStartConsonant", "kQuickEndConsonant", "kQuickTelex"]:
        kind, node = find_table_body(text, name)
        assert kind == "map", name
        entries = []
        for entry in split_map_entries(node):
            key_atom = entry[1][0][1]
            value = entry[1][1]  # list of atoms (single row of codes)
            atoms = [r[1] for r in value[1]]
            entries.append((key_atom, atoms))
        entries.sort(key=lambda kv: eval_expr(kv[0]))
        # simple map: value = FlatVec<uint16>
        # build data + inner
        flat = []
        for k, row in entries:
            flat.extend(row)
        data_expr = ", ".join(flat)
        fout.write(f"constexpr std::uint16_t {name}_Data[] = {{ {data_expr} }};\n")
        inner = []
        off = 0
        for k, row in entries:
            inner.append(f"{{&{name}_Data[{off}], {len(row)}}}")
            off += len(row)
        fout.write(f"constexpr FlatVec<std::uint16_t> {name}_Inner[] = {{ {', '.join(inner)} }};\n")
        ent = ",\n    ".join(
            f"{{ {k}, {name}_Inner[{i}] }}" for i, (k, _) in enumerate(entries))
        fout.write(f"constexpr FlatMap<std::uint16_t, FlatVec<std::uint16_t>, {len(entries)}> {name} = {{\n    {{ {ent} }}\n}};\n\n")

    fout.write("// ---- codeTableFor -----------------------------------------------------\n")
    # INTERNAL linkage (static): the tables it refers to are constexpr
    # (internal linkage) — an external-linkage inline function referencing
    # them is an ODR violation (see the comment in the generated header).
    fout.write("// v1.2.1 RC2 — INTERNAL linkage on purpose. The tables above are namespace-\n")
    fout.write("// scope `constexpr` objects (internal linkage); an `inline` (external-\n")
    fout.write("// linkage) function that refers to them is ill-formed NDR (each TU gets a\n")
    fout.write("// different definition — [basic.def.odr]). GCC's linker surfaced it as\n")
    fout.write("// \"referenced in section .data.rel.local … defined in discarded section\"\n")
    fout.write("// under -O1/-fsanitize builds; MSVC happened to fold it. `static` makes every\n")
    fout.write("// TU own its (identical) copy, which is exactly the semantics intended.\n")
    fout.write("[[nodiscard]] static inline const FlatCodeTable& codeTableFor(int codeTable) noexcept {\n")
    fout.write("    static constexpr std::array<const FlatCodeTable*, 5> tables = {\n")
    fout.write("        &kCodeTableUnicode, &kCodeTableTcvn3, &kCodeTableVniWindows,\n")
    fout.write("        &kCodeTableUnicodeCompound, &kCodeTableCp1258,\n")
    fout.write("    };\n")
    fout.write("    const int idx = (codeTable < 0) ? 0 : (codeTable > 4 ? 4 : codeTable);\n")
    fout.write("    return *tables[static_cast<std::size_t>(idx)];\n")
    fout.write("}\n\n")
    fout.write("} // namespace ok::text\n")
    fout.close()
    print(f"wrote {OUT}")

if __name__ == "__main__":
    main()
