# sampling profile — profile_kieekey_matched-minimal.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 2,049 samples at 1000 Hz over 8,345,221,301 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 58.3 ns/key measured
* unresolved sample count: 11 (0.5 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 360 | 17.6 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 288 | 14.1 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 219 | 10.7 % | `ok::text::TextEngine::checkGrammar(int)` |
| 215 | 10.5 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 193 | 9.4 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 149 | 7.3 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 102 | 5.0 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 84 | 4.1 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 77 | 3.8 % | `bench::KieeKeyDriver::invoke()` |
| 70 | 3.4 % | `ok::text::TextEngine::saveWord()` |
| 59 | 2.9 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 42 | 2.0 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 35 | 1.7 % | `main` |
| 31 | 1.5 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 26 | 1.3 % | `ok::text::TextEngine::handleOldMark()` |
| 24 | 1.2 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 21 | 1.0 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 16 | 0.8 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 13 | 0.6 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 11 | 0.5 % | `??` |
| 9 | 0.4 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 4 | 0.2 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 1 | 0.0 % | `ok::text::TextEngine::insertD(char32_t, bool)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 66 | 3.2 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 47 | 2.3 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:675 |
| 37 | 1.8 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 30 | 1.5 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 30 | 1.5 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1410 |
| 27 | 1.3 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 27 | 1.3 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 25 | 1.2 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:200 |
| 24 | 1.2 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2435 |
| 23 | 1.1 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2370 |
| 19 | 0.9 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1407 (discriminator 2) |
| 18 | 0.9 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2319 |
| 18 | 0.9 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2320 |
| 17 | 0.8 % | ok::text::TextEngine::getCharacterCode(unsigned int const&) const @ /home/user/KieeKey/src/core/TextEngine.cpp:2577 |
| 15 | 0.7 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1428 |
| 15 | 0.7 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2432 |
| 15 | 0.7 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 14 | 0.7 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1425 |
| 14 | 0.7 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2348 |
| 14 | 0.7 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2491 |
| 14 | 0.7 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:653 |
| 12 | 0.6 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1394 |
| 12 | 0.6 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1393 |
| 12 | 0.6 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:659 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
