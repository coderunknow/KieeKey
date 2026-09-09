# sampling profile — profile_kieekey_matched-minimal.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,177 samples at 1000 Hz over 4,739,363,482 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 33.1 ns/key measured
* unresolved sample count: 5 (0.4 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 291 | 24.7 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 192 | 16.3 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 166 | 14.1 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 117 | 9.9 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 94 | 8.0 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 60 | 5.1 % | `bench::KieeKeyDriver::invoke()` |
| 50 | 4.2 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 48 | 4.1 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 39 | 3.3 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 24 | 2.0 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 21 | 1.8 % | `main` |
| 20 | 1.7 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 14 | 1.2 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 10 | 0.8 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 8 | 0.7 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 8 | 0.7 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 5 | 0.4 % | `??` |
| 5 | 0.4 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 5 | 0.4 % | `ok::text::TextEngine::handleOldMark()` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 52 | 4.4 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 42 | 3.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 28 | 2.4 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 23 | 2.0 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2369 |
| 21 | 1.8 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2540 |
| 21 | 1.8 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 21 | 1.8 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:215 |
| 20 | 1.7 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 17 | 1.4 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:155 |
| 16 | 1.4 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2419 |
| 14 | 1.2 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2397 |
| 12 | 1.0 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 12 | 1.0 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2361 |
| 12 | 1.0 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2368 |
| 11 | 0.9 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2476 |
| 11 | 0.9 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2481 |
| 10 | 0.8 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2484 |
| 10 | 0.8 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:653 |
| 10 | 0.8 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:200 |
| 10 | 0.8 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:246 |
| 9 | 0.8 % | ok::text::TextEngine::isSpecialKey(char32_t) const @ /home/user/KieeKey/src/core/TextEngine.hpp:502 |
| 9 | 0.8 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:670 |
| 9 | 0.8 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:276 |
| 8 | 0.7 % | ok::text::TextEngine::isVowelChar(char32_t) @ /home/user/KieeKey/src/core/TextEngine.hpp:540 |
| 8 | 0.7 % | ok::text::TextEngine::findAndCalculateVowel(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1518 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
