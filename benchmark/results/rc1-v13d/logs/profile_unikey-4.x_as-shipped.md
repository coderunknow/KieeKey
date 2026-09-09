# sampling profile — profile_unikey-4.x_as-shipped.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 1,849 samples at 1000 Hz over 7,400,805,236 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 51.7 ns/key measured
* unresolved sample count: 33 (1.8 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 416 | 22.5 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 250 | 13.5 % | `UkEngine::macroMatch(UkKeyEvent&)` |
| 140 | 7.6 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 126 | 6.8 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 88 | 4.8 % | `isValidVC(VowelSeq, ConSeq)` |
| 80 | 4.3 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 64 | 3.5 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 58 | 3.1 % | `bench::UniKeyDriver::invoke()` |
| 54 | 2.9 % | `UkEngine::processTone(UkKeyEvent&)` |
| 47 | 2.5 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 46 | 2.5 % | `UkEngine::lastWordIsNonVn()` |
| 45 | 2.4 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 44 | 2.4 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 40 | 2.2 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 34 | 1.8 % | `CMacroTable::lookup(unsigned int*)` |
| 33 | 1.8 % | `??` |
| 30 | 1.6 % | `UkEngine::restoreKeyStrokes(int&, unsigned char*, int&, UkOutputType&)` |
| 28 | 1.5 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 27 | 1.5 % | `StringBOStream::putB(unsigned char)` |
| 27 | 1.5 % | `UkEngine::prepareBuffer()` |
| 25 | 1.4 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 25 | 1.4 % | `UkEngine::getSeqSteps(int, int)` |
| 25 | 1.4 % | `UkEngine::processHook(UkKeyEvent&)` |
| 20 | 1.1 % | `CVnCharsetLib::getVnCharset(int)` |
| 18 | 1.0 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 16 | 0.9 % | `main` |
| 16 | 0.9 % | `UkInputProcessor::getCharType(unsigned int)` |
| 7 | 0.4 % | `_init` |
| 7 | 0.4 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 5 | 0.3 % | `UkEngine::processDd(UkKeyEvent&)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 327 | 17.7 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 95 | 5.1 % | UkEngine::macroMatch(UkKeyEvent&) @ ??:? |
| 34 | 1.8 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 30 | 1.6 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 27 | 1.5 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 15 | 0.8 % | bench::UniKeyDriver::vkFor(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:485 |
| 15 | 0.8 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 14 | 0.8 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 14 | 0.8 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 13 | 0.7 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 12 | 0.6 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:425 |
| 12 | 0.6 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 12 | 0.6 % | UkEngine::processAppend(UkKeyEvent&) @ ??:? |
| 11 | 0.6 % | CVnCharsetLib::getVnCharset(int) @ ??:? |
| 11 | 0.6 % | UkEngine::prepareBuffer() @ ??:? |
| 10 | 0.5 % | CMacroTable::lookup(unsigned int*) @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
