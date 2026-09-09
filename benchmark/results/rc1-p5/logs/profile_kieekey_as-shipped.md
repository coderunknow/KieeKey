# sampling profile — profile_kieekey_as-shipped.jsonl

* engine column: `kieekey` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 2,274 samples at 1000 Hz over 9,341,755,255 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 65.3 ns/key measured
* unresolved sample count: 0 (0.0 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 452 | 19.9 % | `ok::text::TextEngine::checkSpelling(bool)` |
| 275 | 12.1 % | `ok::text::TextEngine::mainKeyBranch(char32_t, bool)` |
| 260 | 11.4 % | `ok::text::TextEngine::handleMainKey(char32_t, bool)` |
| 203 | 8.9 % | `ok::text::TextEngine::process(ok::text::TextInput const&)` |
| 178 | 7.8 % | `ok::text::TextEngine::findAndCalculateVowel(bool) [clone .constprop.0]` |
| 178 | 7.8 % | `ok::text::TextEngine::checkGrammar(int)` |
| 133 | 5.8 % | `ok::text::TextEngine::insertMark(unsigned int, bool)` |
| 110 | 4.8 % | `ok::text::TextEngine::findAndCalculateVowel(bool)` |
| 105 | 4.6 % | `ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t)` |
| 71 | 3.1 % | `bench::KieeKeyDriver::invoke()` |
| 62 | 2.7 % | `ok::text::TextEngine::insertKey(char32_t, bool, bool)` |
| 52 | 2.3 % | `bench::KieeKeyDriver::prepare(corpus::Event const&)` |
| 43 | 1.9 % | `ok::text::TextEngine::saveWord()` |
| 28 | 1.2 % | `ok::text::TextEngine::spaceBranch(char32_t, bool)` |
| 27 | 1.2 % | `main` |
| 26 | 1.1 % | `ok::text::TextEngine::canHasEndConsonant()` |
| 14 | 0.6 % | `ok::text::TextEngine::checkRestoreIfWrongSpelling(ok::text::EngineCode)` |
| 13 | 0.6 % | `ok::text::TextEngine::handleOldMark()` |
| 11 | 0.5 % | `ok::text::TextEngine::restoreLastTypingState()` |
| 9 | 0.4 % | `ok::text::TextEngine::getCharacterCode(unsigned int const&) const` |
| 9 | 0.4 % | `ok::text::TextEngine::insertAOE(char32_t, bool)` |
| 8 | 0.4 % | `ok::text::TextEngine::insertW(char32_t, bool)` |
| 6 | 0.3 % | `ok::text::TextEngine::backspaceBranch(bool)` |
| 1 | 0.0 % | `_init` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 189 | 8.3 % | ok::text::TextEngine::findAndCalculateVowel(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1463 |
| 66 | 2.9 % | bench::KieeKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:160 |
| 39 | 1.7 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:183 |
| 34 | 1.5 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1410 |
| 30 | 1.3 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1044 |
| 28 | 1.2 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:611 |
| 25 | 1.1 % | bench::KieeKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:144 |
| 23 | 1.0 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:660 |
| 20 | 0.9 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2320 |
| 18 | 0.8 % | ok::text::TextEngine::mainKeyBranch(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:623 |
| 15 | 0.7 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 15 | 0.7 % | matchLeadingConsonant @ /home/user/KieeKey/src/core/TextEngine.cpp:1038 |
| 15 | 0.7 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1393 |
| 14 | 0.6 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2435 |
| 13 | 0.6 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1236 |
| 13 | 0.6 % | ok::text::TextEngine::checkGrammar(int) @ /home/user/KieeKey/src/core/TextEngine.cpp:1407 (discriminator 2) |
| 13 | 0.6 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:246 |
| 11 | 0.5 % | matchLeadingConsonant @ /home/user/KieeKey/src/core/TextEngine.cpp:1035 |
| 11 | 0.5 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1277 |
| 11 | 0.5 % | ok::text::TextEngine::isVowelChar(char32_t) @ /home/user/KieeKey/src/core/TextEngine.hpp:452 |
| 11 | 0.5 % | ok::text::TextEngine::checkSpelling(bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:1303 (discriminator 4) |
| 11 | 0.5 % | ok::text::TextEngine::checkCorrectVowel(ok::text::FlatVec<ok::text::FlatVec<unsigned short> > const&, int&, int&, char32_t) @ /home/user/KieeKey/src/core/TextEngine.cpp:2325 |
| 11 | 0.5 % | ok::text::TextEngine::handleMainKey(char32_t, bool) @ /home/user/KieeKey/src/core/TextEngine.cpp:2370 |
| 11 | 0.5 % | ok::text::TextEngine::process(ok::text::TextInput const&) @ /home/user/KieeKey/src/core/TextEngine.cpp:271 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
