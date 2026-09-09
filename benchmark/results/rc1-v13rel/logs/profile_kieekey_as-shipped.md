# sampling profile — profile_kieekey_as-shipped.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 2,129 samples at 1000 Hz over 8,730,363,450 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 61.0 ns/key measured
* unresolved sample count: 1 (0.0 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 446 | 20.9 % | `ok::text::TextEngine::checkSpelling(bool)` |
| 284 | 13.3 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 268 | 12.6 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 176 | 8.3 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 145 | 6.8 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 126 | 5.9 % | `ok::text::TextEngine::checkGrammar(int)` |
| 111 | 5.2 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 79 | 3.7 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 76 | 3.6 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 61 | 2.9 % | `bench::KieeKeyDriver::invoke()` |
| 47 | 2.2 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 46 | 2.2 % | `ok::text::TextEngine::saveWord()` |
| 41 | 1.9 % | `main` |
| 41 | 1.9 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 33 | 1.6 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 29 | 1.4 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 26 | 1.2 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 20 | 0.9 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 19 | 0.9 % | `ok::text::TextEngine::handleOldMark()` |
| 19 | 0.9 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 18 | 0.8 % | `ok::text::TextEngine::checkRestoreIfWrongSpelling(ok::text::EngineCode)` |
| 9 | 0.4 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 6 | 0.3 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 2 | 0.1 % | `_init` |
| 1 | 0.0 % | `??` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 50 | 2.3 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 30 | 1.4 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 26 | 1.2 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 26 | 1.2 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:676 |
| 23 | 1.1 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1450 |
| 21 | 1.0 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2360 |
| 17 | 0.8 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1240 |
| 16 | 0.8 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 16 | 0.8 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 16 | 0.8 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:653 |
| 15 | 0.7 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2472 |
| 14 | 0.7 % | ok::text::TextEngine::getCharacterCode(unsigned int const&) const @ /home/user/KieeKey/src/core/TextEngine.cpp:2617 |
| 14 | 0.7 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2475 |
| 14 | 0.7 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 14 | 0.7 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:200 |
| 13 | 0.6 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1045 |
| 12 | 0.6 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:155 |
| 12 | 0.6 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:146 |
| 12 | 0.6 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1039 |
| 12 | 0.6 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2352 |
| 12 | 0.6 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:623 |
| 11 | 0.5 % | ok::text::FlatVec<ok::text::FlatVec<unsigned short> >::operator[](unsigned long) const @ /home/user/KieeKey/src/core/FlatTables.hpp:59 |
| 11 | 0.5 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2388 |
| 11 | 0.5 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2531 |
| 10 | 0.5 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1242 (discriminator 1) |
| 10 | 0.5 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2359 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
