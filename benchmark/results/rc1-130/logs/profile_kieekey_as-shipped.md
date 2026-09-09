# sampling profile — profile_kieekey_as-shipped.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 2,583 samples at 1000 Hz over 10,683,149,843 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 74.7 ns/key measured
* unresolved sample count: 1 (0.0 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 487 | 18.9 % | `ok::text::TextEngine::checkSpelling(bool)` |
| 313 | 12.1 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 305 | 11.8 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 223 | 8.6 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 214 | 8.3 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 188 | 7.3 % | `ok::text::TextEngine::checkGrammar(int)` |
| 154 | 6.0 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 147 | 5.7 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 119 | 4.6 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 77 | 3.0 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 66 | 2.6 % | `bench::KieeKeyDriver::invoke()` |
| 53 | 2.1 % | `ok::text::TextEngine::saveWord()` |
| 51 | 2.0 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 31 | 1.2 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 30 | 1.2 % | `main` |
| 30 | 1.2 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 23 | 0.9 % | `ok::text::TextEngine::handleOldMark()` |
| 21 | 0.8 % | `ok::text::TextEngine::checkRestoreIfWrongSpelling(ok::text::EngineCode)` |
| 14 | 0.5 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 13 | 0.5 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 8 | 0.3 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 7 | 0.3 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 7 | 0.3 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 1 | 0.0 % | `??` |
| 1 | 0.0 % | `_init` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 228 | 8.8 % | ok::text::TextEngine::findAndCalculateVowel(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1463 |
| 65 | 2.5 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 24 | 0.9 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1410 |
| 23 | 0.9 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1044 |
| 23 | 0.9 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 22 | 0.9 % | matchLeadingConsonant @ /home/user/KieeKey/src/core/TextEngine.cpp:1038 |
| 22 | 0.9 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:623 |
| 21 | 0.8 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2370 |
| 19 | 0.7 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 18 | 0.7 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 18 | 0.7 % | matchLeadingConsonant @ /home/user/KieeKey/src/core/TextEngine.cpp:1035 |
| 18 | 0.7 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2320 |
| 17 | 0.7 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1393 |
| 17 | 0.7 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 17 | 0.7 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 16 | 0.6 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:146 |
| 16 | 0.6 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2435 |
| 15 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:246 |
| 15 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:219 (discriminator 4) |
| 14 | 0.5 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1025 |
| 14 | 0.5 % | matchLeadingConsonant @ /home/user/KieeKey/src/core/TextEngine.cpp:1044 |
| 13 | 0.5 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1291 |
| 13 | 0.5 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1309 |
| 13 | 0.5 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2325 |
| 12 | 0.5 % | ok::text::TextEngine::insertKey(char32_t, bool, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:804 |
| 12 | 0.5 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:686 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
