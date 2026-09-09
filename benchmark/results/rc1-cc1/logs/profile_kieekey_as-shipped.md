# sampling profile — profile_kieekey_as-shipped.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 2,335 samples at 1000 Hz over 9,734,267,334 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 68.0 ns/key measured
* unresolved sample count: 1 (0.0 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 480 | 20.6 % | `ok::text::TextEngine::checkSpelling(bool)` |
| 288 | 12.3 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 287 | 12.3 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 211 | 9.0 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 193 | 8.3 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 155 | 6.6 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 128 | 5.5 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 112 | 4.8 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 98 | 4.2 % | `ok::text::TextEngine::checkGrammar(int)` |
| 62 | 2.7 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 55 | 2.4 % | `ok::text::TextEngine::saveWord()` |
| 53 | 2.3 % | `bench::KieeKeyDriver::invoke()` |
| 49 | 2.1 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 26 | 1.1 % | `main` |
| 26 | 1.1 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 22 | 0.9 % | `ok::text::TextEngine::checkRestoreIfWrongSpelling(ok::text::EngineCode)` |
| 21 | 0.9 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 16 | 0.7 % | `ok::text::TextEngine::handleOldMark()` |
| 15 | 0.6 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 11 | 0.5 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 9 | 0.4 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 8 | 0.3 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 7 | 0.3 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 2 | 0.1 % | `ok::text::TextEngine::insertD(char32_t, bool)` |
| 1 | 0.0 % | `??` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 228 | 9.8 % | ok::text::TextEngine::findAndCalculateVowel(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1470 |
| 47 | 2.0 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 41 | 1.8 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 27 | 1.2 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2355 |
| 24 | 1.0 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 24 | 1.0 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2377 |
| 24 | 1.0 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 23 | 1.0 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1044 |
| 20 | 0.9 % | matchLeadingConsonant @ /home/user/KieeKey/src/core/TextEngine.cpp:1035 |
| 19 | 0.8 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1025 |
| 17 | 0.7 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:200 |
| 15 | 0.6 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 15 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:219 (discriminator 4) |
| 14 | 0.6 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2498 |
| 14 | 0.6 % | ok::text::TextEngine::PCH(unsigned long) const @ /home/user/KieeKey/src/core/TextEngine.hpp:424 |
| 13 | 0.6 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2326 |
| 13 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:246 |
| 12 | 0.5 % | matchLeadingConsonant @ /home/user/KieeKey/src/core/TextEngine.cpp:1038 |
| 12 | 0.5 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:659 |
| 12 | 0.5 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:686 |
| 11 | 0.5 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:155 |
| 11 | 0.5 % | ok::text::FlatVec<ok::text::FlatVec<unsigned short> >::operator[](unsigned long) const @ /home/user/KieeKey/src/core/FlatTables.hpp:59 |
| 11 | 0.5 % | ok::text::FlatMap<unsigned short, ok::text::FlatVec<ok::text::FlatVec<unsigned short> >, 4ul>::find(unsigned short) const @ /home/user/KieeKey/src/core/FlatTables.hpp:89 |
| 11 | 0.5 % | ok::text::TextEngine::isSpecialKey(char32_t) const @ /home/user/KieeKey/src/core/TextEngine.hpp:414 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
