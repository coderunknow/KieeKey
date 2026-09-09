# sampling profile — profile_kieekey_matched-minimal.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,650 samples at 1000 Hz over 6,634,699,905 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 46.4 ns/key measured
* unresolved sample count: 6 (0.4 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 273 | 16.5 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 189 | 11.5 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 180 | 10.9 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 166 | 10.1 % | `ok::text::TextEngine::checkGrammar(int)` |
| 127 | 7.7 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 118 | 7.2 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 87 | 5.3 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 80 | 4.8 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 62 | 3.8 % | `bench::KieeKeyDriver::invoke()` |
| 61 | 3.7 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 49 | 3.0 % | `ok::text::TextEngine::saveWord()` |
| 44 | 2.7 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 42 | 2.5 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 36 | 2.2 % | `main` |
| 32 | 1.9 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 23 | 1.4 % | `ok::text::TextEngine::handleOldMark()` |
| 21 | 1.3 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 19 | 1.2 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 15 | 0.9 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 12 | 0.7 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 6 | 0.4 % | `??` |
| 5 | 0.3 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 1 | 0.1 % | `_init` |
| 1 | 0.1 % | `ok::text::TextEngine::insertD(char32_t, bool)` |
| 1 | 0.1 % | `ok::text::TextEngine::checkForStandaloneChar(char32_t, bool, char32_t)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 58 | 3.5 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 28 | 1.7 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1450 |
| 27 | 1.6 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 27 | 1.6 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2359 |
| 26 | 1.6 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 26 | 1.6 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2360 |
| 23 | 1.4 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1450 (discriminator 1) |
| 20 | 1.2 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:676 |
| 18 | 1.1 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 17 | 1.0 % | ok::text::TextEngine::getCharacterCode(unsigned int const&) const @ /home/user/KieeKey/src/core/TextEngine.cpp:2581 |
| 17 | 1.0 % | ok::text::TextEngine::getCharacterCode(unsigned int const&) const @ /home/user/KieeKey/src/core/TextEngine.cpp:2617 |
| 16 | 1.0 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2475 |
| 13 | 0.8 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2410 |
| 13 | 0.8 % | ok::text::TextEngine::finalizeResult() @ /home/user/KieeKey/src/core/TextEngine.cpp:2914 |
| 13 | 0.8 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:219 (discriminator 4) |
| 12 | 0.7 % | ok::text::TextEngine::isVowelChar(char32_t) @ /home/user/KieeKey/src/core/TextEngine.hpp:489 |
| 12 | 0.7 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2472 |
| 12 | 0.7 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:623 |
| 12 | 0.7 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:291 |
| 11 | 0.7 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 10 | 0.6 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2352 |
| 10 | 0.6 % | ok::text::TextEngine::composeCached(unsigned long) @ /home/user/KieeKey/src/core/TextEngine.hpp:621 |
| 10 | 0.6 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1447 (discriminator 2) |
| 10 | 0.6 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1449 (discriminator 1) |
| 10 | 0.6 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
