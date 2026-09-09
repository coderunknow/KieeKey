# sampling profile — profile_kieekey_matched-minimal.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,680 samples at 1000 Hz over 6,796,012,774 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 47.5 ns/key measured
* unresolved sample count: 5 (0.3 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 270 | 16.1 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 260 | 15.5 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 181 | 10.8 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 167 | 9.9 % | `ok::text::TextEngine::checkGrammar(int)` |
| 157 | 9.3 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 124 | 7.4 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 84 | 5.0 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 63 | 3.8 % | `bench::KieeKeyDriver::invoke()` |
| 59 | 3.5 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 59 | 3.5 % | `ok::text::TextEngine::saveWord()` |
| 46 | 2.7 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 41 | 2.4 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 36 | 2.1 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 29 | 1.7 % | `main` |
| 26 | 1.5 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 17 | 1.0 % | `ok::text::TextEngine::handleOldMark()` |
| 16 | 1.0 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 14 | 0.8 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 12 | 0.7 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 6 | 0.4 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 5 | 0.3 % | `??` |
| 5 | 0.3 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 2 | 0.1 % | `ok::text::TextEngine::insertD(char32_t, bool)` |
| 1 | 0.1 % | `_init` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 54 | 3.2 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 34 | 2.0 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 25 | 1.5 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:675 |
| 24 | 1.4 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2320 |
| 22 | 1.3 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 22 | 1.3 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:653 |
| 22 | 1.3 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 20 | 1.2 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1393 |
| 19 | 1.1 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 16 | 1.0 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 16 | 1.0 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1410 |
| 16 | 1.0 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:623 |
| 15 | 0.9 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:200 |
| 14 | 0.8 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:155 |
| 14 | 0.8 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1407 (discriminator 2) |
| 11 | 0.7 % | ok::text::TextEngine::findAndCalculateVowel(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1469 |
| 11 | 0.7 % | ok::text::TextEngine::saveWord() @ /home/user/KieeKey/src/core/TextEngine.cpp:856 |
| 11 | 0.7 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2348 |
| 11 | 0.7 % | ok::text::FlatMap<unsigned short, ok::text::FlatVec<ok::text::FlatVec<unsigned short> >, 4ul>::find(unsigned short) const @ /home/user/KieeKey/src/core/FlatTables.hpp:88 |
| 11 | 0.7 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:215 |
| 10 | 0.6 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2325 |
| 10 | 0.6 % | ok::text::TextEngine::getCharacterCode(unsigned int const&) const @ /home/user/KieeKey/src/core/TextEngine.cpp:2577 |
| 10 | 0.6 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1394 |
| 10 | 0.6 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1407 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
