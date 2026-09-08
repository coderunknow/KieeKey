# sampling profile — profile_unikey-4.x_as-shipped.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 2,167 samples at 1000 Hz over 8,674,195,758 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 60.6 ns/key measured
* unresolved sample count: 37 (1.7 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 438 | 20.2 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 299 | 13.8 % | `UkEngine::macroMatch(UkKeyEvent&)` |
| 157 | 7.2 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 129 | 6.0 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 110 | 5.1 % | `isValidVC(VowelSeq, ConSeq)` |
| 101 | 4.7 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 96 | 4.4 % | `UkEngine::processTone(UkKeyEvent&)` |
| 70 | 3.2 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 61 | 2.8 % | `UkEngine::lastWordIsNonVn()` |
| 60 | 2.8 % | `CMacroTable::lookup(unsigned int*)` |
| 57 | 2.6 % | `bench::UniKeyDriver::invoke()` |
| 55 | 2.5 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 51 | 2.4 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 48 | 2.2 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 48 | 2.2 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 44 | 2.0 % | `UkEngine::prepareBuffer()` |
| 38 | 1.8 % | `UkEngine::restoreKeyStrokes(int&, unsigned char*, int&, UkOutputType&)` |
| 37 | 1.7 % | `??` |
| 37 | 1.7 % | `UkEngine::processHook(UkKeyEvent&)` |
| 34 | 1.6 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 32 | 1.5 % | `main` |
| 32 | 1.5 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 28 | 1.3 % | `UkEngine::getSeqSteps(int, int)` |
| 20 | 0.9 % | `StringBOStream::putB(unsigned char)` |
| 20 | 0.9 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 16 | 0.7 % | `CVnCharsetLib::getVnCharset(int)` |
| 11 | 0.5 % | `UkInputProcessor::getCharType(unsigned int)` |
| 8 | 0.4 % | `_init` |
| 6 | 0.3 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 6 | 0.3 % | `UkEngine::processDd(UkKeyEvent&)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 357 | 16.5 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 99 | 4.6 % | UkEngine::macroMatch(UkKeyEvent&) @ ??:? |
| 41 | 1.9 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 39 | 1.8 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 26 | 1.2 % | CMacroTable::lookup(unsigned int*) @ ??:? |
| 22 | 1.0 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 19 | 0.9 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:425 |
| 19 | 0.9 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 15 | 0.7 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 15 | 0.7 % | UkEngine::processAppend(UkKeyEvent&) @ ??:? |
| 14 | 0.6 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 14 | 0.6 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 14 | 0.6 % | UkEngine::prepareBuffer() @ ??:? |
| 12 | 0.6 % | bench::UniKeyDriver::vkFor(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:485 |
| 12 | 0.6 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 12 | 0.6 % | UkEngine::getSeqSteps(int, int) @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
