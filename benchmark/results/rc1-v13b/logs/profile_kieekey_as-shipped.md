# sampling profile — profile_kieekey_as-shipped.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 2,019 samples at 1000 Hz over 8,350,557,183 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 58.4 ns/key measured
* unresolved sample count: 7 (0.3 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 418 | 20.7 % | `ok::text::TextEngine::checkSpelling(bool)` |
| 266 | 13.2 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 262 | 13.0 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 149 | 7.4 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 142 | 7.0 % | `ok::text::TextEngine::checkGrammar(int)` |
| 108 | 5.3 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 87 | 4.3 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 78 | 3.9 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 61 | 3.0 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 59 | 2.9 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 57 | 2.8 % | `bench::KieeKeyDriver::invoke()` |
| 56 | 2.8 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 47 | 2.3 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 39 | 1.9 % | `ok::text::TextEngine::saveWord()` |
| 28 | 1.4 % | `main` |
| 28 | 1.4 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 25 | 1.2 % | `ok::text::TextEngine::handleOldMark()` |
| 24 | 1.2 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 24 | 1.2 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 19 | 0.9 % | `ok::text::TextEngine::checkRestoreIfWrongSpelling(ok::text::EngineCode)` |
| 14 | 0.7 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 14 | 0.7 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 7 | 0.3 % | `??` |
| 6 | 0.3 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 1 | 0.0 % | `_init` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 48 | 2.4 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 33 | 1.6 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 32 | 1.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 24 | 1.2 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2409 |
| 22 | 1.1 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 22 | 1.1 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 21 | 1.0 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1449 |
| 15 | 0.7 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 15 | 0.7 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2359 |
| 14 | 0.7 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1239 |
| 14 | 0.7 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2474 |
| 13 | 0.6 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:686 |
| 13 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:200 |
| 11 | 0.5 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:155 |
| 11 | 0.5 % | ok::text::FlatVec<ok::text::FlatVec<unsigned short> >::operator[](unsigned long) const @ /home/user/KieeKey/src/core/FlatTables.hpp:59 |
| 11 | 0.5 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:675 |
| 10 | 0.5 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1038 |
| 10 | 0.5 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2358 |
| 10 | 0.5 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1446 (discriminator 2) |
| 10 | 0.5 % | ok::text::TextEngine::insertKey(char32_t, bool, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:804 |
| 10 | 0.5 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:246 |
| 9 | 0.4 % | ok::text::TextEngine::isVowelChar(char32_t) @ /home/user/KieeKey/src/core/TextEngine.hpp:460 |
| 9 | 0.4 % | matchLeadingConsonant @ /home/user/KieeKey/src/core/TextEngine.cpp:1044 |
| 9 | 0.4 % | ok::text::FlatCodeTable::find(unsigned int) const @ /home/user/KieeKey/src/core/FlatTables.hpp:122 |
| 9 | 0.4 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1449 (discriminator 1) |
| 9 | 0.4 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2471 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
