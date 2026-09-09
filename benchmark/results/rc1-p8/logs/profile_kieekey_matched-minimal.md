# sampling profile — profile_kieekey_matched-minimal.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,682 samples at 1000 Hz over 6,760,512,795 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 47.2 ns/key measured
* unresolved sample count: 1 (0.1 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 299 | 17.8 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 199 | 11.8 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 190 | 11.3 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 189 | 11.2 % | `ok::text::TextEngine::checkGrammar(int)` |
| 157 | 9.3 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 135 | 8.0 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 90 | 5.4 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 62 | 3.7 % | `bench::KieeKeyDriver::invoke()` |
| 61 | 3.6 % | `ok::text::TextEngine::saveWord()` |
| 58 | 3.4 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 46 | 2.7 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 45 | 2.7 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 33 | 2.0 % | `main` |
| 33 | 2.0 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 19 | 1.1 % | `ok::text::TextEngine::handleOldMark()` |
| 16 | 1.0 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 14 | 0.8 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 12 | 0.7 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 8 | 0.5 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 6 | 0.4 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 6 | 0.4 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 3 | 0.2 % | `ok::text::TextEngine::insertD(char32_t, bool)` |
| 1 | 0.1 % | `??` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 52 | 3.1 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 45 | 2.7 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 29 | 1.7 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 23 | 1.4 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 20 | 1.2 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 20 | 1.2 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2320 |
| 19 | 1.1 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1410 |
| 19 | 1.1 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2435 |
| 17 | 1.0 % | ok::text::TextEngine::getCharacterCode(unsigned int const&) const @ /home/user/KieeKey/src/core/TextEngine.cpp:2577 |
| 17 | 1.0 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:653 |
| 15 | 0.9 % | ok::text::TextEngine::chr(unsigned long) const @ /home/user/KieeKey/src/core/TextEngine.hpp:476 |
| 15 | 0.9 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:219 (discriminator 4) |
| 14 | 0.8 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:200 |
| 13 | 0.8 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1407 (discriminator 2) |
| 13 | 0.8 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1410 (discriminator 1) |
| 13 | 0.8 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 12 | 0.7 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1425 |
| 12 | 0.7 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2370 |
| 12 | 0.7 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:246 |
| 11 | 0.7 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:623 |
| 10 | 0.6 % | ok::text::TextEngine::findAndCalculateVowel(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1469 |
| 10 | 0.6 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2312 |
| 10 | 0.6 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2319 |
| 10 | 0.6 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1407 |
| 10 | 0.6 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:675 |
| 10 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:324 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
