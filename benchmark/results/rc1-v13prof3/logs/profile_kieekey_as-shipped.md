# sampling profile — profile_kieekey_as-shipped.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 1,731 samples at 1000 Hz over 6,946,203,226 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 48.5 ns/key measured
* unresolved sample count: 6 (0.3 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 359 | 20.7 % | `ok::text::TextEngine::checkSpelling(bool)` |
| 280 | 16.2 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 224 | 12.9 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 222 | 12.8 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 127 | 7.3 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 125 | 7.2 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 78 | 4.5 % | `bench::KieeKeyDriver::invoke()` |
| 52 | 3.0 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 44 | 2.5 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 43 | 2.5 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 38 | 2.2 % | `main` |
| 34 | 2.0 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 33 | 1.9 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 16 | 0.9 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 13 | 0.8 % | `ok::text::TextEngine::handleOldMark()` |
| 11 | 0.6 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 8 | 0.5 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 8 | 0.5 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 6 | 0.3 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 6 | 0.3 % | `??` |
| 3 | 0.2 % | `ok::text::TextEngine::checkRestoreIfWrongSpelling(ok::text::EngineCode)` |
| 1 | 0.1 % | `ok::text::TextEngine::insertD(char32_t, bool)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 64 | 3.7 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 36 | 2.1 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 26 | 1.5 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 21 | 1.2 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2369 |
| 19 | 1.1 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 19 | 1.1 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:653 |
| 18 | 1.0 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1240 |
| 17 | 1.0 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:271 |
| 16 | 0.9 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2419 |
| 16 | 0.9 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2484 |
| 15 | 0.9 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1039 |
| 15 | 0.9 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2368 |
| 15 | 0.9 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:200 |
| 14 | 0.8 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 14 | 0.8 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 14 | 0.8 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:215 |
| 13 | 0.8 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2540 |
| 12 | 0.7 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:155 |
| 12 | 0.7 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1045 |
| 12 | 0.7 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2361 |
| 11 | 0.6 % | matchLeadingConsonant @ /home/user/KieeKey/src/core/TextEngine.cpp:1045 |
| 11 | 0.6 % | ok::text::TextEngine::getCharacterCode(unsigned int const&) const @ /home/user/KieeKey/src/core/TextEngine.cpp:2626 |
| 11 | 0.6 % | ok::text::TextEngine::pushTypingState() @ /home/user/KieeKey/src/core/TextEngine.cpp:907 |
| 10 | 0.6 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1237 (discriminator 1) |
| 10 | 0.6 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1242 (discriminator 1) |
| 10 | 0.6 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2397 |
| 10 | 0.6 % | ok::text::FlatMap<unsigned short, ok::text::FlatVec<ok::text::FlatVec<unsigned short> >, 4ul>::find(unsigned short) const @ /home/user/KieeKey/src/core/FlatTables.hpp:90 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
