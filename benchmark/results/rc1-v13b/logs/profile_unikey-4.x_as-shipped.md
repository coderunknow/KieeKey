# sampling profile — profile_unikey-4.x_as-shipped.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 1,837 samples at 1000 Hz over 7,351,503,314 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 51.4 ns/key measured
* unresolved sample count: 28 (1.5 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 351 | 19.1 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 258 | 14.0 % | `UkEngine::macroMatch(UkKeyEvent&)` |
| 146 | 7.9 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 120 | 6.5 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 90 | 4.9 % | `isValidVC(VowelSeq, ConSeq)` |
| 68 | 3.7 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 67 | 3.6 % | `UkEngine::processTone(UkKeyEvent&)` |
| 54 | 2.9 % | `bench::UniKeyDriver::invoke()` |
| 51 | 2.8 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 50 | 2.7 % | `CMacroTable::lookup(unsigned int*)` |
| 50 | 2.7 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 49 | 2.7 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 48 | 2.6 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 48 | 2.6 % | `UkEngine::lastWordIsNonVn()` |
| 46 | 2.5 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 44 | 2.4 % | `UkEngine::restoreKeyStrokes(int&, unsigned char*, int&, UkOutputType&)` |
| 39 | 2.1 % | `UkEngine::processHook(UkKeyEvent&)` |
| 34 | 1.9 % | `UkEngine::prepareBuffer()` |
| 30 | 1.6 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 28 | 1.5 % | `??` |
| 26 | 1.4 % | `StringBOStream::putB(unsigned char)` |
| 21 | 1.1 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 20 | 1.1 % | `UkEngine::getSeqSteps(int, int)` |
| 19 | 1.0 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 16 | 0.9 % | `main` |
| 16 | 0.9 % | `UkInputProcessor::getCharType(unsigned int)` |
| 15 | 0.8 % | `CVnCharsetLib::getVnCharset(int)` |
| 9 | 0.5 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 7 | 0.4 % | `UkEngine::processTelexW(UkKeyEvent&)` |
| 6 | 0.3 % | `_init` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 254 | 13.8 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 116 | 6.3 % | UkEngine::macroMatch(UkKeyEvent&) @ ??:? |
| 42 | 2.3 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 40 | 2.2 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 28 | 1.5 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 17 | 0.9 % | CMacroTable::lookup(unsigned int*) @ ??:? |
| 15 | 0.8 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 15 | 0.8 % | UkEngine::lastWordIsNonVn() @ ??:? |
| 13 | 0.7 % | bench::UniKeyDriver::vkFor(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:485 |
| 12 | 0.7 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 12 | 0.7 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:423 |
| 12 | 0.7 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 11 | 0.6 % | CVnCharsetLib::getVnCharset(int) @ ??:? |
| 11 | 0.6 % | UkEngine::prepareBuffer() @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
