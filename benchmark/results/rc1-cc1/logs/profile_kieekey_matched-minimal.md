# sampling profile — profile_kieekey_matched-minimal.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,791 samples at 1000 Hz over 7,247,779,369 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 50.7 ns/key measured
* unresolved sample count: 5 (0.3 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 314 | 17.5 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 244 | 13.6 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 228 | 12.7 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 158 | 8.8 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 155 | 8.7 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 116 | 6.5 % | `ok::text::TextEngine::checkGrammar(int)` |
| 80 | 4.5 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 75 | 4.2 % | `bench::KieeKeyDriver::invoke()` |
| 68 | 3.8 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 58 | 3.2 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 55 | 3.1 % | `ok::text::TextEngine::saveWord()` |
| 52 | 2.9 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 31 | 1.7 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 29 | 1.6 % | `main` |
| 25 | 1.4 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 25 | 1.4 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 22 | 1.2 % | `ok::text::TextEngine::handleOldMark()` |
| 15 | 0.8 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 13 | 0.7 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 11 | 0.6 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 10 | 0.6 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 5 | 0.3 % | `??` |
| 2 | 0.1 % | `ok::text::TextEngine::insertD(char32_t, bool)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 61 | 3.4 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 57 | 3.2 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 47 | 2.6 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2326 |
| 37 | 2.1 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 29 | 1.6 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2377 |
| 25 | 1.4 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 24 | 1.3 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2327 |
| 24 | 1.3 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2355 |
| 22 | 1.2 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 19 | 1.1 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:200 |
| 17 | 0.9 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2442 |
| 16 | 0.9 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1414 (discriminator 1) |
| 16 | 0.9 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:659 |
| 14 | 0.8 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2328 |
| 14 | 0.8 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:623 |
| 13 | 0.7 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:219 (discriminator 4) |
| 12 | 0.7 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 12 | 0.7 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:215 |
| 11 | 0.6 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:675 |
| 10 | 0.6 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2319 |
| 10 | 0.6 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2434 |
| 10 | 0.6 % | ok::text::FlatVec<ok::text::FlatVec<unsigned short> >::operator[](unsigned long) const @ /home/user/KieeKey/src/core/FlatTables.hpp:59 |
| 10 | 0.6 % | ok::text::FlatMap<unsigned short, ok::text::FlatVec<ok::text::FlatVec<unsigned short> >, 4ul>::find(unsigned short) const @ /home/user/KieeKey/src/core/FlatTables.hpp:89 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
