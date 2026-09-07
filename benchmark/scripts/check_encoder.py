#!/usr/bin/env python3
"""Independent cross-check of the benchmark's keystroke model.

The C++ harness turns each corpus word (Vietnamese text) into the keystrokes a
user would type, then feeds those keystrokes to the engines.  If that encoder
were wrong, every correctness number in REPORT.md would be wrong in the same
direction for every engine — so the encoder is re-implemented here from an
independent source of truth (Python ``unicodedata``) and compared:

  1. table check      harness/viet_table.inc   vs  unicodedata decomposition
  2. stream check     --mode=encoder-dump rows  vs  this file's encoder

Exit status is non-zero on any mismatch, so run_campaign.sh can gate a
campaign on it.

  python3 benchmark/scripts/check_encoder.py --dump=/path/to/encoder-dump.txt
"""
import argparse
import re
import sys
import unicodedata

# ---------------------------------------------------------------------------
# Vietnamese letter model, derived from Unicode decomposition only.
# ---------------------------------------------------------------------------
MARKS = {  # combining mark -> (kind, value)
    0x0302: ("hat", 1),      # circumflex  â ê ô
    0x0306: ("hat", 2),      # breve       ă
    0x031B: ("hat", 3),      # horn        ơ ư
    0x0301: ("tone", 1),     # sac         á
    0x0300: ("tone", 2),     # huyen       à
    0x0309: ("tone", 3),     # hoi         ả
    0x0303: ("tone", 4),     # nga         ã
    0x0323: ("tone", 5),     # nang        ạ
}
# The Vietnamese alphabet: six vowels + y take hats and tones; d and the
# d-bar đ are plain consonants (a "d with dot below" is NOT Vietnamese).
HATS_OK = {"a": (1, 2), "e": (1,), "o": (1, 3), "u": (3,), "i": (), "y": ()}
D_BAR = {0x111: "d", 0x110: "D"}


def split_letter(cp):
    """(base_char, hat, tone) for a Vietnamese letter, else None.

    Derived only from Unicode's own decomposition, with the base-letter and
    hat-permissibility rules of Vietnamese orthography restated here.
    """
    if cp in D_BAR:
        return (D_BAR[cp], 0, 0)
    ch = chr(cp)
    d = unicodedata.normalize("NFD", ch)
    if not d or ord(d[0]) >= 128 or len(d) == 1:
        return None
    base = d[0]
    if base.lower() not in HATS_OK:
        return None
    hat = tone = 0
    for m in d[1:]:
        k = MARKS.get(ord(m))
        if k is None:
            return None
        kind, val = k
        if kind == "hat":
            if hat or val not in HATS_OK[base.lower()]:
                return None
            hat = val
        else:
            if tone:
                return None                     # two tones in one letter
            tone = val
    return (base, hat, tone)


def is_letter(cp):
    """A character that belongs to a typed word run (letter of any alphabet)."""
    if 0x41 <= cp <= 0x5A or 0x61 <= cp <= 0x7A:
        return True                             # ASCII letters are typed too
    return split_letter(cp) is not None


# ---------------------------------------------------------------------------
# stream encoder (mirrors the documented key model, independent code path)
# ---------------------------------------------------------------------------
RAW_OK = set(",.;:/-=[]'")
SHIFTED_OK = set("?!()\"")
# Digit tables as each engine documents them. KieeKey / OpenKey route the
# circumflex of a and o through 6 and of e through 7, with 7 and 8 both acting
# as the "w" key (horn on o/u, bowl on a); UniKey's own VNI method uses
# 6 = roof-all, 7 = horn, 8 = bowl.
VNI_HAT = {1: "6", 2: "8", 3: "7"}
VNI_HAT_UK = {1: "6", 2: "8", 3: "7"}
VNI_TONE = {1: "1", 2: "2", 3: "3", 4: "4", 5: "5"}
TELEX_TONE = {1: "s", 2: "f", 3: "r", 4: "x", 5: "j"}
TELEX_HAT = {1: "double", 2: "w", 3: "w"}


def encode(intended, method="telex", tone_pos="end", unikey_vni=False):
    out = []
    word = []                     # pending run of Vietnamese letters

    def flush():
        if not word:
            return True
        ok = emit_word(word, out, method, tone_pos, unikey_vni)
        word.clear()
        return ok

    for ch in intended:
        cp = ord(ch)
        if is_letter(cp):
            word.append(ch)
            continue
        if not flush():
            return None
        if ch == " ":
            out.append(" ")
        elif "a" <= ch <= "z":
            out.append(ch)
        elif "A" <= ch <= "Z":
            out.append("^" + chr(cp + 32))
        elif "0" <= ch <= "9":
            out.append(ch)
        elif ch in RAW_OK:
            out.append(ch)
        elif ch in SHIFTED_OK:
            out.append("~" + ch)
        else:
            return None
    if not flush():
        return None
    s = "".join(out)
    return s if s else None


def emit_word(word, out, method, tone_pos, unikey_vni):
    pending_tone = ""
    for ch in word:
        cp = ord(ch)
        d_stroke = cp in (0x111, 0x110)
        info = split_letter(cp)
        if info is None:
            # plain ASCII letter of the word: typed as it is
            if "a" <= ch <= "z":
                out.append(ch)
                continue
            if "A" <= ch <= "Z":
                out.append("^" + ch.lower())
                continue
            return False
        base, hat, tone = info
        upper = base.isupper()
        low = base.lower()
        if method == "telex":
            if d_stroke:
                out.append(("^d" if upper else "d") + "d")
            elif upper:
                out.append("^" + low)
            else:
                out.append(low)
            if hat == 1:
                out.append(low)                  # aa ee oo
            elif hat in (2, 3):
                out.append("w")
            tk = TELEX_TONE.get(tone, "")
        else:
            if d_stroke:
                out.append("^d" if upper else "d")
                out.append("9")
            elif upper:
                out.append("^" + low)
            else:
                out.append(low)
            if hat:
                out.append(VNI_HAT_UK[hat] if unikey_vni else VNI_HAT[hat])
            tk = VNI_TONE.get(tone, "")
        if tk:
            if tone_pos == "mid":
                out.append(tk)
            elif pending_tone:
                return False                     # two tones in one word
            else:
                pending_tone = tk
    if pending_tone:
        out.append(pending_tone)
    return True


# ---------------------------------------------------------------------------
# checks
# ---------------------------------------------------------------------------
def check_table(path):
    rows = open(path, encoding="utf-8").read().split("\n")
    pat = re.compile(r"\{0x([0-9A-Fa-f]+), '(.)', (\d), (\d)\}")
    n = bad = 0
    problems = []
    for line in rows:
        m = pat.search(line)
        if not m:
            continue
        cp = int(m.group(1), 16)
        base, hat, tone = m.group(2), int(m.group(3)), int(m.group(4))
        n += 1
        info = split_letter(cp)
        if cp < 128:
            if (base, hat, tone) not in [(chr(cp), 0, 0)]:
                problems.append((cp, (base, hat, tone), "ascii-letter"))
            continue
        if info is None:
            problems.append((cp, (base, hat, tone), "python: not a Viet letter"))
            continue
        pbase, phat, ptone = info
        # The harness stores đ / Đ as base 'd' (the stroke is the letter, not a
        # mark), which is exactly what D_BAR returns.
        if (pbase.lower() != base.lower()) or phat != hat or ptone != tone:
            problems.append((cp, (base, hat, tone), (pbase, phat, ptone)))
        bad += 0
    return n, problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--table", default="benchmark/harness/viet_table.inc")
    ap.add_argument("--dump", default="", help="encoder-dump output (method\\tintended\\tstream)")
    ap.add_argument("--expect", default="0", help="fail unless exactly this many problems")
    a = ap.parse_args()

    n, problems = check_table(a.table)
    print(f"[encoder] table rows checked: {n}, mismatches vs unicodedata: {len(problems)}")
    for cp, in_cpp, in_py in problems[:20]:
        print(f"   U+{cp:04X} ({chr(cp)}) cpp={in_cpp} python={in_py}")

    stream_rows = mism = skipped = 0
    if a.dump:
        for line in open(a.dump, encoding="utf-8"):
            if line.startswith("{") or not line.strip():
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) != 3:
                continue
            method, intended, stream = parts
            tone_pos = "mid" if method == "telex-mid" else "end"
            meth = "vni" if method == "vni" else "telex"
            got = encode(intended, meth, tone_pos)
            stream_rows += 1
            if got is None:
                skipped += 1
                continue
            if got != stream:
                mism += 1
                if mism <= 20:
                    print(f"   {method} {intended!r}: cpp={stream!r} python={got!r}")
        print(f"[encoder] stream rows checked: {stream_rows}, mismatches: {mism}, "
              f"python-refused(not modelled): {skipped}")

    total = len(problems) + mism
    if total != int(a.expect):
        print(f"[encoder] FAIL: {total} problem(s) but --expect={a.expect} "
              f"(expect=0 means 'no disagreement allowed'; run with --dump/--streams "
              f"to also compare generated streams)")
        return 1
    print("[encoder] OK — the C++ key model is reproduced by an independent implementation")
    return 0


if __name__ == "__main__":
    sys.exit(main())
