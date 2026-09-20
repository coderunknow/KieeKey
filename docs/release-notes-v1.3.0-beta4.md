# KieeKey v1.3.0-beta4

Six reported defects fixed at the root, plus one latent crash found by testing (file build **1.3.0.5**). Every fix was reproduced first (failing probe or seeded fuzz), fixed at its root cause, then pinned by a portable native regression test that runs on Linux.

## Fixed
- **Backspace no longer wedges a Vietnamese typing game** — `VnComposer::feedBackspace()` now pops exactly one *composed* code point and returns the engine to a fresh word state; a 500-run seeded recovery fuzz failed 100 % before the fix, **2 500/2 500** runs complete after.
- **WasdRace ignored Backspace** — both wire shapes (`vk=0x08` and `ch='\b'`) now rewind, in VN and EN mode.
- **Every typing arcade speaks Vietnamese** — Fishing and No-Mistake gained `setPassageLanguage()` with full-diacritic prompts; Rhythm's lane keys move to the arrow cluster in VN mode; live/full config application carries the language to all five games.
- **Latent concurrency race in the Arcade manager closed** (pre-existing on main; the concurrency probe segfaulted ~15 % of runs on unmodified beta3) — game calls serialized by `m_gameMtx` with a documented lock order; IME hot path still takes no lock; 0/300 crashes after the fix, ThreadSanitizer clean.
- **The settings dialog cannot clip text anymore** — authored rectangles re-fit and the beta3 layout solver (`DialogLayout.hpp`) is now wired into `WM_CREATE` (measured with `DrawTextW(DT_CALCRECT)` on the real font/monitor at any DPI).
- **Live external effects visibly transform Vietnamese** — the glyph tables had no mappings for precomposed Vietnamese vowels; all accented vowels now flip by tone (sắc↔huyền, hỏi↔ngã, nặng→hỏi), 1:1 with the input so erase accounting stays exact, NFC ordering handled. 116 glyph pairs pinned by `test_chaos`; intensity selector gains 75 %.

## Added
- **The diagnostics module is actually used** — Chẩn đoán tab control panel (Off/Basic/Full level persisted to `%APPDATA%\KieeKey\diag-level.txt`, five-check quick self-check, report export); hook/engine/consumer counters gated to one relaxed atomic load + branch when Off.
- `tests/test_arcade_recovery.cpp` — 43 checks (desync repro, 300-seed recovery fuzz, WasdRace backspace wire shapes, Fishing VN, No-Mistake word-strict judging, Rhythm arrows, two-thread manager hammer).

## Benchmarks — no regression (paired, interleaved, vs v1.3.0-beta3)
- Correctness gate: 2 059 419 differential events, **PASS on both sides**.
- Core IME decision p50 (2 M keys × 3 interleaved runs): vn-compose 63=63 ns, mixed 63=63, delete 57=57, passthrough 53→54 ns.
- Feature isolation, 15 alternating 300 k-key rounds: every median within ±1.3 % of base, inside the ±0.7 % noise floor of the byte-identical control leg; sink digests identical on all five configurations.
- Glyph flips ON: Vietnamese transforms at ~+1.8 ns/char vs beta3's pass-through (opt-in; default OFF).
- Full evidence: [`docs/bench/beta4/BENCHMARK_REPORT.md`](https://github.com/coderunknow/KieeKey/blob/v1.3.0-beta4/docs/bench/beta4/BENCHMARK_REPORT.md).

## Release verification
- `dist/KieeKeyApp.exe` rebuilt from this tree (Zig cross-build, PE **1.3.0.5**); the full-TU compile caught three Windows-only compile errors in the new diagnostics code before tagging.
- MSVC `/W4 /WX` clean on x64; `ok_core` now lists `Diagnostics.cpp` (LNK2019 fix).
- `tests/run_all_tests.sh --jobs=2`: ALL NATIVE TESTS PASSED (41 run targets). Input-isolation, version-consistency, SHA256SUMS, layout-audit `--strict`, controls-audit: all OK.

## Downloads
Use the CI-built archives below (**KieeKey-x64.zip**, ARM64, ARM64EC) — or the quick-test `dist/KieeKeyApp.exe` from the repository at this tag.

**Full Changelog**: https://github.com/coderunknow/KieeKey/compare/v1.3.0-beta3...v1.3.0-beta4
