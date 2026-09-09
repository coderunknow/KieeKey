# sampling profile — profile_kieekey_matched-minimal.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,654 samples at 1000 Hz over 6,673,198,575 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 46.6 ns/key measured
* unresolved sample count: 1 (0.1 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 263 | 15.9 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 263 | 15.9 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 197 | 11.9 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 143 | 8.6 % | `ok::text::TextEngine::checkGrammar(int)` |
| 142 | 8.6 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 135 | 8.2 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 81 | 4.9 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 67 | 4.1 % | `bench::KieeKeyDriver::invoke()` |
| 61 | 3.7 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 60 | 3.6 % | `ok::text::TextEngine::saveWord()` |
| 47 | 2.8 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 42 | 2.5 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 39 | 2.4 % | `main` |
| 34 | 2.1 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 21 | 1.3 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 18 | 1.1 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 15 | 0.9 % | `ok::text::TextEngine::handleOldMark()` |
| 9 | 0.5 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 6 | 0.4 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 5 | 0.3 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 3 | 0.2 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 1 | 0.1 % | `??` |
| 1 | 0.1 % | `_init` |
| 1 | 0.1 % | `ok::text::TextEngine::insertD(char32_t, bool)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 66 | 4.0 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 34 | 2.1 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:198 |
| 31 | 1.9 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2360 |
| 31 | 1.9 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 29 | 1.8 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:202 |
| 24 | 1.5 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 24 | 1.5 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1450 |
| 24 | 1.5 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 21 | 1.3 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:676 |
| 19 | 1.1 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 19 | 1.1 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 18 | 1.1 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2475 |
| 14 | 0.8 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:200 |
| 13 | 0.8 % | ok::text::TextEngine::finalizeResult() @ /home/user/KieeKey/src/core/TextEngine.cpp:2914 |
| 12 | 0.7 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:146 |
| 12 | 0.7 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:246 |
| 10 | 0.6 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:155 |
| 10 | 0.6 % | ok::text::TextEngine::getCharacterCode(unsigned int const&) const @ /home/user/KieeKey/src/core/TextEngine.cpp:2617 |
| 10 | 0.6 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2359 |
| 10 | 0.6 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2410 |
| 10 | 0.6 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2388 |
| 10 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:324 |
| 9 | 0.5 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1450 (discriminator 1) |
| 9 | 0.5 % | void std::__new_allocator<unsigned int>::construct<unsigned int, unsigned int const&>(unsigned int*, unsigned int const&) @ /usr/include/c++/12/bits/new_allocator.h:175 |
| 9 | 0.5 % | ok::text::TextEngine::saveWord() @ /home/user/KieeKey/src/core/TextEngine.cpp:854 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
