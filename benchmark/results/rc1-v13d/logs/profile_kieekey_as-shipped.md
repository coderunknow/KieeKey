# sampling profile — profile_kieekey_as-shipped.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 2,161 samples at 1000 Hz over 8,817,536,301 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 61.6 ns/key measured
* unresolved sample count: 4 (0.2 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 416 | 19.3 % | `ok::text::TextEngine::checkSpelling(bool)` |
| 279 | 12.9 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 226 | 10.5 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 200 | 9.3 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 155 | 7.2 % | `ok::text::TextEngine::checkGrammar(int)` |
| 135 | 6.2 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 116 | 5.4 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 114 | 5.3 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 72 | 3.3 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 59 | 2.7 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 58 | 2.7 % | `bench::KieeKeyDriver::invoke()` |
| 57 | 2.6 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 56 | 2.6 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 49 | 2.3 % | `ok::text::TextEngine::saveWord()` |
| 32 | 1.5 % | `main` |
| 27 | 1.2 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 23 | 1.1 % | `ok::text::TextEngine::checkRestoreIfWrongSpelling(ok::text::EngineCode)` |
| 21 | 1.0 % | `ok::text::TextEngine::handleOldMark()` |
| 20 | 0.9 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 17 | 0.8 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 12 | 0.6 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 7 | 0.3 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 4 | 0.2 % | `??` |
| 3 | 0.1 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 1 | 0.0 % | `_init` |
| 1 | 0.0 % | `ok::text::TextEngine::removeMark()` |
| 1 | 0.0 % | `ok::text::TextEngine::insertD(char32_t, bool)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 48 | 2.2 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 30 | 1.4 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 24 | 1.1 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 24 | 1.1 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1450 |
| 23 | 1.1 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1242 (discriminator 1) |
| 21 | 1.0 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2410 |
| 20 | 0.9 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2360 |
| 17 | 0.8 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 15 | 0.7 % | ok::text::TextEngine::getCharacterCode(unsigned int const&) const @ /home/user/KieeKey/src/core/TextEngine.cpp:2581 |
| 14 | 0.6 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1240 |
| 14 | 0.6 % | ok::text::TextEngine::composeCached(unsigned long) @ /home/user/KieeKey/src/core/TextEngine.hpp:621 |
| 14 | 0.6 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 14 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:200 |
| 13 | 0.6 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2475 |
| 13 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:215 |
| 13 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:219 (discriminator 4) |
| 12 | 0.6 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1450 (discriminator 1) |
| 12 | 0.6 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 12 | 0.6 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:623 |
| 12 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:246 |
| 11 | 0.5 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1039 |
| 11 | 0.5 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2365 |
| 10 | 0.5 % | matchLeadingConsonant @ /home/user/KieeKey/src/core/TextEngine.cpp:1045 |
| 10 | 0.5 % | ok::text::FlatCodeTable::find(unsigned int) const @ /home/user/KieeKey/src/core/FlatTables.hpp:122 |
| 10 | 0.5 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2472 |
| 10 | 0.5 % | ok::text::FlatMap<unsigned short, ok::text::FlatVec<ok::text::FlatVec<unsigned short> >, 4ul>::find(unsigned short) const @ /home/user/KieeKey/src/core/FlatTables.hpp:90 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
