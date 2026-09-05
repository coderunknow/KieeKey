# Raw benchmark artifacts — v1.2.1 RC1 baseline vs v1.2.1 RC2

Committed on purpose (exception to the "regenerable evidence is not
shipped" policy) because the RC2 release claims are made *against* these
files. Nothing here was edited after the runs.

| path | what |
|---|---|
| `rc1/suite/`, `rc2/suite/` | `tests/run_bench_suite.sh --side=<rc>`: `env.json` (compiler, flags, OS, CPU, RAM, timer), gate log + rc, `bench_perf` ×3 (json+log), `e2e_bench` ×3 (json+log+rc), `bench_tone_latency` log, `summary.{txt,json}` |
| `rc1/realworld.log`, `rc2/realworld.log` | `tests/bench_real_world_typing --keys=50000 --runs=3` |
| `rc1/profiles_*.{log,json}` | `tests/bench_profiles.cpp` compiled against the **frozen RC1 tree** (`-DBENCH_RC1_ENGINE`), Balanced row = "RC1 / default" |
| `rc2/profiles_*.{log,json}` | same harness, RC2 tree, all profiles; `burst` = unpaced 100 k keys, `paced` = 1000 keys/s with word pauses, 20 k keys |
| `rc2/gprof_flat.txt` | gprof flat profile of the RC2 engine on the 20 M-key throughput driver |
| `rc2/tput_driver.cpp` | that driver (clock-free; `g++ -std=c++20 -O2 -Isrc/core tput_driver.cpp src/core/TextEngine.cpp`) |
| `rc2/compare_tables.md` | auto-generated RC1-vs-RC2 tables with per-row faster/unchanged/slower verdicts |

RC1 side = commit `0884c64` (`v1.2.1-RC1`); RC2 side = commit recorded in
`rc2/suite/env.json`. Same host, same compiler, same flags, same corpora,
same seeds; runs were interleaved-by-side where the script supports it.
Binaries (`suite/bin/`, `realworld`, `bench_profiles*`) are not committed.
