# imebench_kit — 3-way benchmark kit (reconstructed)

The original kit folder referenced by the master prompt was not part of the uploaded archive.
This kit was rebuilt from the repository's own assets and follows master-prompt §7 exactly.
> Note: `results/` shown in historical reports refers to the pre-release lineage runs
> (files `*_nextgen_*.json`, not shipped); fresh runs emit `*_kieekey_*.json`.

## Layout

```
imebench_kit/
├── harness/
│   ├── keygen.hpp        # word -> Telex/VNI keystroke generators (deterministic)
│   ├── adapters.hpp      # KieeKey (v3.1 consumer contract), ok205, UniKey adapters
│   ├── bench_correct.cpp # corpus round-trip -> results/correct_*.json
│   ├── bench_perf.cpp    # 2M keys/workload, per-key steady_clock -> results/perf.json
│   └── bench_mem.cpp     # M1 allocations, M2 live slope, RSS -> results/mem.json
├── scripts/
│   └── analyze_correct.py  # strict + orthography-normalized scorer, failure lists
└── results/              # (created on run) JSON outputs — regenerate locally;
                          #   the frozen lineage evidence is not shipped in the source release
```

## Protocol invariants (§7 — do not "fix", these encode real host behavior)

1. ukengine `charsetId = CONV_CHARSET_XUTF8 (12)`.
2. ukengine `processBackspace`: `backs` already includes the physical Backspace deletion —
   forward the raw BS only when `backs == 0`.
3. ok205 letters use lowercase keycode + caps flag; printable symbols go through
   `processSymbol` (shift+key path); VNI tone/vowel digits are plain keys (NOT symbols).
4. KieeKey input is the normalized `TextInput`; the consumer applies the v3.1 contract:
   Char-Restore re-issue (hotfix §3), Space-Restore re-issue (D4), in-result macro expansion
   (D3), engine-side `backspaceCount` clamp (D2).

## Configs

- **as-shipped**: KieeKey `restore=ON, useDictionaryRestore=ON` (lexicon-gated restore);
  UniKey `autoNonVnRestore=ON, spellCheckEnabled=1, strictSpellCheck=0`; 2.0.5
  `vRestoreIfWrongSpelling=0` (its shipped default).
- **matched**: restore=OFF ×3, dictionary OFF — KieeKey must stay byte-equal to 2.0.5 (G5).

## Build & run (from the repo root)

```bash
# correctness (add -DBENCH_WITH_205 / -DBENCH_WITH_UNIKEY and the reference objects
# as in BENCHMARK_DELTA.md's runs; the objects build per tests/run_three_bench.sh)
g++ -std=c++17 -O2 -w -I src/core -I tests -I tests/reference/openkey-2.0.5/engine \
    -I imebench_kit/harness -DBENCH_WITH_205 imebench_kit/harness/bench_correct.cpp \
    src/core/TextEngine.cpp tests/engine205.cpp tests/reference/openkey-2.0.5/engine/*.cpp \
    -o /tmp/bench_correct
/tmp/bench_correct                       # writes imebench_kit/results/correct_*.json
python3 imebench_kit/scripts/analyze_correct.py

# perf / mem
g++ -std=c++17 -O2 -w -I src/core -I imebench_kit/harness imebench_kit/harness/bench_perf.cpp \
    src/core/TextEngine.cpp -o /tmp/bench_perf && /tmp/bench_perf --keys=2000000
g++ -std=c++17 -O2 -w -I src/core -I imebench_kit/harness imebench_kit/harness/bench_mem.cpp \
    src/core/TextEngine.cpp -o /tmp/bench_mem && /tmp/bench_mem
```

## Lexicon provenance (P1 dictionary-gated restore)

The lexicon is the word list produced by splitting the corpus vocabulary into individual
words, committed through this pipeline. It is a **veto only** (a composed non-word reverts to
the raw keystrokes) — it never injects text, and `useDictionaryRestore=false` (the matched
config and the v3.0 default) makes the engine byte-identical to the pre-feature engine.
