# sampling profile — profile_kieekey_as-shipped.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 2,038 samples at 1000 Hz over 8,320,696,457 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 58.1 ns/key measured
* unresolved sample count: 3 (0.1 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 387 | 19.0 % | `ok::text::TextEngine::checkSpelling(bool)` |
| 243 | 11.9 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 242 | 11.9 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 185 | 9.1 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 180 | 8.8 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 146 | 7.2 % | `ok::text::TextEngine::checkGrammar(int)` |
| 104 | 5.1 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 92 | 4.5 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 90 | 4.4 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 63 | 3.1 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 54 | 2.6 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 52 | 2.6 % | `bench::KieeKeyDriver::invoke()` |
| 50 | 2.5 % | `ok::text::TextEngine::saveWord()` |
| 35 | 1.7 % | `main` |
| 27 | 1.3 % | `ok::text::TextEngine::handleOldMark()` |
| 18 | 0.9 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 17 | 0.8 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 13 | 0.6 % | `ok::text::TextEngine::checkRestoreIfWrongSpelling(ok::text::EngineCode)` |
| 13 | 0.6 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 7 | 0.3 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 7 | 0.3 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 5 | 0.2 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 4 | 0.2 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 3 | 0.1 % | `??` |
| 1 | 0.0 % | `ok::text::TextEngine::insertD(char32_t, bool)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 185 | 9.1 % | ok::text::TextEngine::findAndCalculateVowel(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1463 |
| 46 | 2.3 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 33 | 1.6 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1044 |
| 23 | 1.1 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 21 | 1.0 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 21 | 1.0 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:145 |
| 18 | 0.9 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2320 |
| 18 | 0.9 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:200 |
| 17 | 0.8 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:653 |
| 16 | 0.8 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1236 |
| 16 | 0.8 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1410 |
| 15 | 0.7 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:623 |
| 14 | 0.7 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 14 | 0.7 % | matchLeadingConsonant @ /home/user/KieeKey/src/core/TextEngine.cpp:1038 |
| 13 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:246 |
| 12 | 0.6 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2491 |
| 11 | 0.5 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2370 |
| 11 | 0.5 % | ok::text::FlatMap<unsigned short, ok::text::FlatVec<ok::text::FlatVec<unsigned short> >, 4ul>::find(unsigned short) const @ /home/user/KieeKey/src/core/FlatTables.hpp:88 |
| 11 | 0.5 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:659 |
| 10 | 0.5 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1303 (discriminator 4) |
| 9 | 0.4 % | matchLeadingConsonant @ /home/user/KieeKey/src/core/TextEngine.cpp:1035 |
| 9 | 0.4 % | ok::text::FlatVec<ok::text::FlatVec<unsigned short> >::operator[](unsigned long) const @ /home/user/KieeKey/src/core/FlatTables.hpp:59 |
| 9 | 0.4 % | matchLeadingConsonant @ /home/user/KieeKey/src/core/TextEngine.cpp:1044 |
| 9 | 0.4 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1277 |
| 9 | 0.4 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2325 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
