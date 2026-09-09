# sampling profile — profile_kieekey_matched-minimal.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,264 samples at 1000 Hz over 5,066,356,350 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 35.4 ns/key measured
* unresolved sample count: 6 (0.5 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 279 | 22.1 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 190 | 15.0 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 171 | 13.5 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 145 | 11.5 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 96 | 7.6 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 64 | 5.1 % | `ok::text::TextEngine::saveWord()` |
| 63 | 5.0 % | `bench::KieeKeyDriver::invoke()` |
| 44 | 3.5 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 43 | 3.4 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 42 | 3.3 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 22 | 1.7 % | `main` |
| 20 | 1.6 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 17 | 1.3 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 16 | 1.3 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 15 | 1.2 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 13 | 1.0 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 10 | 0.8 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 6 | 0.5 % | `??` |
| 4 | 0.3 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 3 | 0.2 % | `ok::text::TextEngine::handleOldMark()` |
| 1 | 0.1 % | `ok::text::TextEngine::insertD(char32_t, bool)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 56 | 4.4 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 47 | 3.7 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 42 | 3.3 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2360 |
| 33 | 2.6 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2359 |
| 26 | 2.1 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 25 | 2.0 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 22 | 1.7 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:246 |
| 19 | 1.5 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 19 | 1.5 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2388 |
| 16 | 1.3 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2410 |
| 16 | 1.3 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:623 |
| 15 | 1.2 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 15 | 1.2 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2531 |
| 13 | 1.0 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:732 |
| 13 | 1.0 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:276 |
| 11 | 0.9 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:215 |
| 11 | 0.9 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:219 (discriminator 4) |
| 10 | 0.8 % | ok::text::FlatMap<unsigned short, ok::text::FlatVec<ok::text::FlatVec<unsigned short> >, 4ul>::find(unsigned short) const @ /home/user/KieeKey/src/core/FlatTables.hpp:88 |
| 10 | 0.8 % | ok::text::FlatMap<unsigned short, ok::text::FlatVec<ok::text::FlatVec<unsigned short> >, 4ul>::find(unsigned short) const @ /home/user/KieeKey/src/core/FlatTables.hpp:90 |
| 10 | 0.8 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:653 |
| 9 | 0.7 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2352 |
| 9 | 0.7 % | void std::__new_allocator<unsigned int>::construct<unsigned int, unsigned int const&>(unsigned int*, unsigned int const&) @ /usr/include/c++/12/bits/new_allocator.h:175 |
| 9 | 0.7 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2475 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
