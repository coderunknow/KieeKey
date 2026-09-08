# sampling profile — profile_kieekey_as-shipped.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 2,028 samples at 1000 Hz over 8,273,835,627 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 57.8 ns/key measured
* unresolved sample count: 7 (0.3 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 394 | 19.4 % | `ok::text::TextEngine::checkSpelling(bool)` |
| 258 | 12.7 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 235 | 11.6 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 180 | 8.9 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 153 | 7.5 % | `ok::text::TextEngine::checkGrammar(int)` |
| 128 | 6.3 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 119 | 5.9 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 73 | 3.6 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 68 | 3.4 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 61 | 3.0 % | `bench::KieeKeyDriver::invoke()` |
| 50 | 2.5 % | `main` |
| 48 | 2.4 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 46 | 2.3 % | `ok::text::TextEngine::saveWord()` |
| 34 | 1.7 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 33 | 1.6 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 30 | 1.5 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 21 | 1.0 % | `ok::text::TextEngine::checkRestoreIfWrongSpelling(ok::text::EngineCode)` |
| 21 | 1.0 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 18 | 0.9 % | `ok::text::TextEngine::handleOldMark()` |
| 17 | 0.8 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 11 | 0.5 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 11 | 0.5 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 11 | 0.5 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 7 | 0.3 % | `??` |
| 1 | 0.0 % | `ok::text::TextEngine::insertD(char32_t, bool)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 61 | 3.0 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 39 | 1.9 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 32 | 1.6 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 26 | 1.3 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1449 |
| 25 | 1.2 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2358 |
| 21 | 1.0 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1449 (discriminator 1) |
| 20 | 1.0 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2359 |
| 20 | 1.0 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2474 |
| 18 | 0.9 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 17 | 0.8 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 17 | 0.8 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2409 |
| 15 | 0.7 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:653 |
| 14 | 0.7 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 14 | 0.7 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:200 |
| 13 | 0.6 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1044 |
| 12 | 0.6 % | ok::text::FlatMap<unsigned short, ok::text::FlatVec<ok::text::FlatVec<unsigned short> >, 4ul>::find(unsigned short) const @ /home/user/KieeKey/src/core/FlatTables.hpp:89 |
| 12 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:219 (discriminator 4) |
| 11 | 0.5 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1241 (discriminator 1) |
| 11 | 0.5 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:623 |
| 11 | 0.5 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:731 |
| 10 | 0.5 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2530 |
| 9 | 0.4 % | ok::text::TextEngine::isVowelChar(char32_t) @ /home/user/KieeKey/src/core/TextEngine.hpp:460 |
| 9 | 0.4 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1432 |
| 9 | 0.4 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1448 (discriminator 1) |

_Ranking only. Decisions are made on `--mode=tput`, never here._
