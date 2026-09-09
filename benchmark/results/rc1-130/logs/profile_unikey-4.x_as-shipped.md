# sampling profile — profile_unikey-4.x_as-shipped.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 2,203 samples at 1000 Hz over 8,817,372,254 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 61.6 ns/key measured
* unresolved sample count: 35 (1.6 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 439 | 19.9 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 355 | 16.1 % | `UkEngine::macroMatch(UkKeyEvent&)` |
| 143 | 6.5 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 139 | 6.3 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 105 | 4.8 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 103 | 4.7 % | `isValidVC(VowelSeq, ConSeq)` |
| 91 | 4.1 % | `UkEngine::processTone(UkKeyEvent&)` |
| 79 | 3.6 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 61 | 2.8 % | `bench::UniKeyDriver::invoke()` |
| 61 | 2.8 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 57 | 2.6 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 56 | 2.5 % | `UkEngine::lastWordIsNonVn()` |
| 54 | 2.5 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 48 | 2.2 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 45 | 2.0 % | `UkEngine::restoreKeyStrokes(int&, unsigned char*, int&, UkOutputType&)` |
| 43 | 2.0 % | `CMacroTable::lookup(unsigned int*)` |
| 35 | 1.6 % | `??` |
| 35 | 1.6 % | `UkEngine::prepareBuffer()` |
| 34 | 1.5 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 30 | 1.4 % | `StringBOStream::putB(unsigned char)` |
| 28 | 1.3 % | `UkEngine::processHook(UkKeyEvent&)` |
| 24 | 1.1 % | `main` |
| 24 | 1.1 % | `UkEngine::getSeqSteps(int, int)` |
| 20 | 0.9 % | `UkInputProcessor::getCharType(unsigned int)` |
| 17 | 0.8 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 16 | 0.7 % | `UkEngine::processTelexW(UkKeyEvent&)` |
| 15 | 0.7 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 11 | 0.5 % | `CVnCharsetLib::getVnCharset(int)` |
| 9 | 0.4 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 9 | 0.4 % | `UkEngine::processDd(UkKeyEvent&)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 329 | 14.9 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 208 | 9.4 % | UkEngine::macroMatch(UkKeyEvent&) @ ??:? |
| 49 | 2.2 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 23 | 1.0 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 21 | 1.0 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 19 | 0.9 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 19 | 0.9 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 17 | 0.8 % | UkEngine::prepareBuffer() @ ??:? |
| 15 | 0.7 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 14 | 0.6 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 14 | 0.6 % | bench::UniKeyDriver::vkFor(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:485 |
| 14 | 0.6 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:423 |
| 13 | 0.6 % | UkEngine::lastWordIsNonVn() @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
