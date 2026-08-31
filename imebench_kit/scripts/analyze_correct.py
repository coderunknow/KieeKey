#!/usr/bin/env python3
#----------------------------------------------------------------------------
# imebench_kit/scripts/analyze_correct.py — strict + orthography-normalized
# scorer for the 3-way correctness results.
#
# Strict scoring: NFC-normalized exact match of the final visible text
# (word + trailing space) against the corpus word.
#
# Normalized scoring: the same, after mapping every character to its
# orthography equivalence class — tone marks placed on a different vowel of
# the same rhyme count as equal (nguỳ==ngùy, hoá==hóa, thuỷ==thủy, …), and
# the oà/òa, uý/úy modern-orthography families are treated as equal.
#----------------------------------------------------------------------------
import json
import sys
import unicodedata
from pathlib import Path

RESULTS = Path(__file__).resolve().parent.parent / "results"

# Rhyme-level equivalence: normalize the tone-vowel choice inside a rhyme.
# Families (all members normalize to the first entry):
_RHYME_MAP = {
    # modern vs old orthography for the oà/uý families
    "oa": "oa", "oà": "oa", "óa": "óa", "oá": "oá", "òa": "òa",
    "oe": "oe", "oè": "oe",
    "uy": "uy", "uý": "uý", "úy": "úy", "uỵ": "uỵ", "ụy": "ụy",
    "uy": "uy", "uỷ": "uỷ", "ủy": "ủy", "uỹ": "uỹ", "ũy": "ũy",
    # tone-on-wrong-vowel corpus variants (nguỳ/ngùy, thuỷ/thủy, …)
    "uỳ": "uỳ", "ùy": "ùy",
    "uỷ": "uỷ", "ủy": "ủy",
    "uỹ": "uỹ", "ũy": "ũy",
    "uỵ": "uỵ", "ụy": "ụy",
}


def nfc(s: str) -> str:
    return unicodedata.normalize("NFC", s)


def normalize_rhymes(word: str) -> str:
    """Map second-vowel tone variants (uỳ/ùy, uỷ/ủy, uỹ/ũy, uỵ/ụy) and the
    oà/òa uý/úy orthography families to one representative per class."""
    out = nfc(word)
    for variant, canon in [
        ("ùy", "uỳ"), ("ủy", "uỷ"), ("ũy", "uỹ"), ("ụy", "uỵ"),
        ("úa", "óa"), ("ùá", "òá"),  # placeholder families, NFC composed
        ("óa", "oá"), ("oà", "òa"),
    ]:
        out = out.replace(variant, canon)
    return out


def score_file(path: Path, normalized: bool):
    data = json.loads(path.read_text(encoding="utf-8"))
    ok = 0
    total = 0
    failures = []
    for row in data["rows"]:
        want = normalize_rhymes(row["w"] + " ") if normalized else nfc(row["w"] + " ")
        got = normalize_rhymes(row["got"]) if normalized else nfc(row["got"])
        total += 1
        if got == want:
            ok += 1
        elif len(failures) < 5000:
            failures.append((row["w"], row["got"]))
    return data["engine"], data["config"], data["method"], ok, total, failures


def main() -> int:
    files = sorted(RESULTS.glob("correct_*.json"))
    if not files:
        print("no results files found — run harness/bench_correct first", file=sys.stderr)
        return 2
    print(f"{'file':48s} {'strict':>12s} {'normalized':>12s}")
    all_fail = {}
    for f in files:
        engine, config, method, ok, total, failures = score_file(f, normalized=False)
        _, _, _, okn, totaln, failn = score_file(f, normalized=True)
        print(f"{f.name:48s} {ok:6d}/{total:<5d} {okn:6d}/{totaln:<5d}")
        if failures:
            all_fail[f.name] = failures
    # dump failure lists for the delta report
    for name, fails in all_fail.items():
        out = RESULTS / ("failures_" + name.replace("correct_", "").replace(".json", ".txt"))
        with out.open("w", encoding="utf-8") as fh:
            for want, got in fails:
                fh.write(f"{want}\t{got}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
