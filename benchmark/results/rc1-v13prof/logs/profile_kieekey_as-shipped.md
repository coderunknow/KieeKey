# sampling profile — profile_kieekey_as-shipped.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 1,990 samples at 1000 Hz over 8,209,610,568 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 57.4 ns/key measured
* unresolved sample count: 10 (0.5 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 460 | 23.1 % | `ok::text::TextEngine::checkSpelling(bool)` |
| 301 | 15.1 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 238 | 12.0 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 214 | 10.8 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 146 | 7.3 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 108 | 5.4 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 63 | 3.2 % | `bench::KieeKeyDriver::invoke()` |
| 63 | 3.2 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 61 | 3.1 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 57 | 2.9 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 48 | 2.4 % | `ok::text::TextEngine::saveWord()` |
| 40 | 2.0 % | `main` |
| 37 | 1.9 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 31 | 1.6 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 27 | 1.4 % | `ok::text::TextEngine::checkRestoreIfWrongSpelling(ok::text::EngineCode)` |
| 18 | 0.9 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 17 | 0.9 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 15 | 0.8 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 14 | 0.7 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 11 | 0.6 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 10 | 0.5 % | `??` |
| 9 | 0.5 % | `ok::text::TextEngine::handleOldMark()` |
| 1 | 0.1 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 1 | 0.1 % | `ok::text::TextEngine::checkForStandaloneChar(char32_t, bool, char32_t)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 61 | 3.1 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 47 | 2.4 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 35 | 1.8 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2360 |
| 30 | 1.5 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 27 | 1.4 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 26 | 1.3 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 26 | 1.3 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2388 |
| 18 | 0.9 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2475 |
| 18 | 0.9 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 15 | 0.8 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2410 |
| 15 | 0.8 % | ok::text::FlatMap<unsigned short, ok::text::FlatVec<ok::text::FlatVec<unsigned short> >, 4ul>::find(unsigned short) const @ /home/user/KieeKey/src/core/FlatTables.hpp:90 |
| 14 | 0.7 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2359 |
| 14 | 0.7 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:659 |
| 14 | 0.7 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:653 |
| 13 | 0.7 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1045 |
| 12 | 0.6 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1240 |
| 12 | 0.6 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2352 |
| 12 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:200 |
| 11 | 0.6 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2531 |
| 11 | 0.6 % | ok::text::TextEngine::isSpecialKey(char32_t) const @ /home/user/KieeKey/src/core/TextEngine.hpp:471 |
| 11 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:324 |
| 11 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:276 |
| 10 | 0.5 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1370 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
