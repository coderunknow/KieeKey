# sampling profile — profile_unikey-4.x_as-shipped.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 1,878 samples at 1000 Hz over 7,516,423,328 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 52.5 ns/key measured
* unresolved sample count: 32 (1.7 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 380 | 20.2 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 281 | 15.0 % | `UkEngine::macroMatch(UkKeyEvent&)` |
| 130 | 6.9 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 120 | 6.4 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 102 | 5.4 % | `isValidVC(VowelSeq, ConSeq)` |
| 92 | 4.9 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 74 | 3.9 % | `UkEngine::processTone(UkKeyEvent&)` |
| 62 | 3.3 % | `bench::UniKeyDriver::invoke()` |
| 59 | 3.1 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 55 | 2.9 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 52 | 2.8 % | `UkEngine::lastWordIsNonVn()` |
| 48 | 2.6 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 42 | 2.2 % | `UkEngine::prepareBuffer()` |
| 38 | 2.0 % | `CMacroTable::lookup(unsigned int*)` |
| 37 | 2.0 % | `UkEngine::restoreKeyStrokes(int&, unsigned char*, int&, UkOutputType&)` |
| 32 | 1.7 % | `??` |
| 32 | 1.7 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 30 | 1.6 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 29 | 1.5 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 27 | 1.4 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 24 | 1.3 % | `UkEngine::processHook(UkKeyEvent&)` |
| 23 | 1.2 % | `StringBOStream::putB(unsigned char)` |
| 22 | 1.2 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 21 | 1.1 % | `UkEngine::getSeqSteps(int, int)` |
| 18 | 1.0 % | `main` |
| 13 | 0.7 % | `CVnCharsetLib::getVnCharset(int)` |
| 12 | 0.6 % | `UkInputProcessor::getCharType(unsigned int)` |
| 6 | 0.3 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 6 | 0.3 % | `UkEngine::processTelexW(UkKeyEvent&)` |
| 4 | 0.2 % | `UkEngine::processDd(UkKeyEvent&)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 275 | 14.6 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 122 | 6.5 % | UkEngine::macroMatch(UkKeyEvent&) @ ??:? |
| 32 | 1.7 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 30 | 1.6 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 27 | 1.4 % | UkEngine::prepareBuffer() @ ??:? |
| 24 | 1.3 % | UkEngine::processAppend(UkKeyEvent&) @ ??:? |
| 20 | 1.1 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:425 |
| 20 | 1.1 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 15 | 0.8 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 15 | 0.8 % | CMacroTable::lookup(unsigned int*) @ ??:? |
| 14 | 0.7 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 14 | 0.7 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 14 | 0.7 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 12 | 0.6 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 10 | 0.5 % | UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&) @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
