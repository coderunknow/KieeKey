# sampling profile — profile_kieekey_matched-minimal.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,578 samples at 1000 Hz over 6,321,133,476 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 44.2 ns/key measured
* unresolved sample count: 6 (0.4 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 283 | 17.9 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 189 | 12.0 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 173 | 11.0 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 140 | 8.9 % | `ok::text::TextEngine::checkGrammar(int)` |
| 137 | 8.7 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 136 | 8.6 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 94 | 6.0 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 64 | 4.1 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 58 | 3.7 % | `bench::KieeKeyDriver::invoke()` |
| 53 | 3.4 % | `ok::text::TextEngine::saveWord()` |
| 46 | 2.9 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 45 | 2.9 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 27 | 1.7 % | `main` |
| 27 | 1.7 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 21 | 1.3 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 20 | 1.3 % | `ok::text::TextEngine::handleOldMark()` |
| 17 | 1.1 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 15 | 1.0 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 10 | 0.6 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 9 | 0.6 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 7 | 0.4 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 6 | 0.4 % | `??` |
| 1 | 0.1 % | `_init` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 43 | 2.7 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 32 | 2.0 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 28 | 1.8 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 26 | 1.6 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1449 |
| 25 | 1.6 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2358 |
| 24 | 1.5 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2359 |
| 21 | 1.3 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2409 |
| 21 | 1.3 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:324 |
| 20 | 1.3 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 18 | 1.1 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 18 | 1.1 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2474 |
| 16 | 1.0 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:653 |
| 15 | 1.0 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 14 | 0.9 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2387 |
| 14 | 0.9 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:659 |
| 14 | 0.9 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:675 |
| 13 | 0.8 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1446 (discriminator 2) |
| 13 | 0.8 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:215 |
| 11 | 0.7 % | ok::text::TextEngine::getCharacterCode(unsigned int const&) const @ /home/user/KieeKey/src/core/TextEngine.cpp:2616 |
| 11 | 0.7 % | ok::text::TextEngine::pushTypingState() @ /home/user/KieeKey/src/core/TextEngine.cpp:907 |
| 10 | 0.6 % | ok::text::TextEngine::findAndCalculateVowel(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1508 |
| 10 | 0.6 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2351 |
| 10 | 0.6 % | ok::text::FlatMap<unsigned short, ok::text::FlatVec<ok::text::FlatVec<unsigned short> >, 4ul>::find(unsigned short) const @ /home/user/KieeKey/src/core/FlatTables.hpp:90 |
| 10 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:219 (discriminator 4) |

_Ranking only. Decisions are made on `--mode=tput`, never here._
