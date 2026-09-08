# sampling profile — profile_kieekey_matched-minimal.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,600 samples at 1000 Hz over 6,417,674,987 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 44.8 ns/key measured
* unresolved sample count: 0 (0.0 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 288 | 18.0 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 191 | 11.9 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 178 | 11.1 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 154 | 9.6 % | `ok::text::TextEngine::checkGrammar(int)` |
| 130 | 8.1 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 105 | 6.6 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 88 | 5.5 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 82 | 5.1 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 68 | 4.2 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 57 | 3.6 % | `bench::KieeKeyDriver::invoke()` |
| 55 | 3.4 % | `ok::text::TextEngine::saveWord()` |
| 38 | 2.4 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 38 | 2.4 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 28 | 1.8 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 27 | 1.7 % | `main` |
| 17 | 1.1 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 17 | 1.1 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 14 | 0.9 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 13 | 0.8 % | `ok::text::TextEngine::handleOldMark()` |
| 10 | 0.6 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 2 | 0.1 % | `ok::text::TextEngine::backspaceBranch(bool)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 50 | 3.1 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 39 | 2.4 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 29 | 1.8 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1449 |
| 27 | 1.7 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 24 | 1.5 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2358 |
| 23 | 1.4 % | ok::text::FlatCodeTable::find(unsigned int) const @ /home/user/KieeKey/src/core/FlatTables.hpp:122 |
| 22 | 1.4 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2359 |
| 22 | 1.4 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2387 |
| 21 | 1.3 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 20 | 1.2 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2409 |
| 18 | 1.1 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2474 |
| 18 | 1.1 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:215 |
| 17 | 1.1 % | ok::text::TextEngine::getCharacterCode(unsigned int const&) const @ /home/user/KieeKey/src/core/TextEngine.cpp:2580 |
| 15 | 0.9 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 15 | 0.9 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:200 |
| 13 | 0.8 % | ok::text::TextEngine::getCharacterCode(unsigned int const&) const @ /home/user/KieeKey/src/core/TextEngine.cpp:2616 |
| 13 | 0.8 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 13 | 0.8 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:219 (discriminator 4) |
| 12 | 0.8 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:653 |
| 11 | 0.7 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:155 |
| 11 | 0.7 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2351 |
| 11 | 0.7 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2522 |
| 10 | 0.6 % | ok::text::TextEngine::isVowelChar(char32_t) @ /home/user/KieeKey/src/core/TextEngine.hpp:460 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
