# sampling profile — profile_unikey-4.x_as-shipped.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 1,738 samples at 1000 Hz over 6,955,597,315 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 48.6 ns/key measured
* unresolved sample count: 43 (2.5 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 333 | 19.2 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 268 | 15.4 % | `UkEngine::macroMatch(UkKeyEvent&)` |
| 115 | 6.6 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 111 | 6.4 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 90 | 5.2 % | `isValidVC(VowelSeq, ConSeq)` |
| 68 | 3.9 % | `UkEngine::processTone(UkKeyEvent&)` |
| 66 | 3.8 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 53 | 3.0 % | `bench::UniKeyDriver::invoke()` |
| 53 | 3.0 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 49 | 2.8 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 48 | 2.8 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 44 | 2.5 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 43 | 2.5 % | `??` |
| 41 | 2.4 % | `UkEngine::lastWordIsNonVn()` |
| 39 | 2.2 % | `UkEngine::restoreKeyStrokes(int&, unsigned char*, int&, UkOutputType&)` |
| 36 | 2.1 % | `CMacroTable::lookup(unsigned int*)` |
| 35 | 2.0 % | `UkEngine::prepareBuffer()` |
| 32 | 1.8 % | `StringBOStream::putB(unsigned char)` |
| 32 | 1.8 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 27 | 1.6 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 26 | 1.5 % | `main` |
| 22 | 1.3 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 20 | 1.2 % | `UkEngine::processHook(UkKeyEvent&)` |
| 17 | 1.0 % | `UkEngine::getSeqSteps(int, int)` |
| 14 | 0.8 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 12 | 0.7 % | `UkInputProcessor::getCharType(unsigned int)` |
| 9 | 0.5 % | `CVnCharsetLib::getVnCharset(int)` |
| 7 | 0.4 % | `_init` |
| 6 | 0.3 % | `UkEngine::processMapChar(UkKeyEvent&)` |
| 6 | 0.3 % | `UkEngine::processDd(UkKeyEvent&)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 234 | 13.5 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 130 | 7.5 % | UkEngine::macroMatch(UkKeyEvent&) @ ??:? |
| 39 | 2.2 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 19 | 1.1 % | UkEngine::prepareBuffer() @ ??:? |
| 18 | 1.0 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 14 | 0.8 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:423 |
| 14 | 0.8 % | ?? @ ??:0 |
| 13 | 0.7 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:425 |
| 12 | 0.7 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 12 | 0.7 % | CMacroTable::lookup(unsigned int*) @ ??:? |
| 11 | 0.6 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 11 | 0.6 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 11 | 0.6 % | UkEngine::processAppend(UkKeyEvent&) @ ??:? |
| 11 | 0.6 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 10 | 0.6 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 10 | 0.6 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 10 | 0.6 % | UkEngine::appendConsonnant(UkKeyEvent&) @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
