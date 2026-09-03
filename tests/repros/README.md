# Minimized mismatch reproducers

Each `repro_*.txt` is a deterministic, self-describing reproducer written by
`tests/mega_correctness.cpp` when a suite finds an engine-vs-oracle text
divergence (init options, compact op stream, first-diverging position, both
sides' UTF-8/UTF-16 dumps).

Housekeeping: reproducer files larger than 100 KB (the long-session suite
emits multi-megabyte op streams) are trimmed from the repository to keep the
source tree small; the full streams are regenerable by rerunning the mega
suite (`tests/run_mega_bench.sh` without `--fast`). See
`docs/reports/V1.1.2_MEGA_BENCH_REPORT.md` for the current full-budget
results and the categorization of the remaining divergences.
