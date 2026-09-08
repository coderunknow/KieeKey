# sampling profile — profile_unikey-4.x_as-shipped.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 1,960 samples at 1000 Hz over 7,844,517,565 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 54.8 ns/key measured
* unresolved sample count: 29 (1.5 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 405 | 20.7 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 280 | 14.3 % | `UkEngine::macroMatch(UkKeyEvent&)` |
| 151 | 7.7 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 127 | 6.5 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 85 | 4.3 % | `isValidVC(VowelSeq, ConSeq)` |
| 84 | 4.3 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 71 | 3.6 % | `UkEngine::processTone(UkKeyEvent&)` |
| 65 | 3.3 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 61 | 3.1 % | `CMacroTable::lookup(unsigned int*)` |
| 55 | 2.8 % | `bench::UniKeyDriver::invoke()` |
| 47 | 2.4 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 46 | 2.3 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 45 | 2.3 % | `UkEngine::prepareBuffer()` |
| 44 | 2.2 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 42 | 2.1 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 40 | 2.0 % | `UkEngine::lastWordIsNonVn()` |
| 40 | 2.0 % | `UkEngine::restoreKeyStrokes(int&, unsigned char*, int&, UkOutputType&)` |
| 37 | 1.9 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 33 | 1.7 % | `UkEngine::processHook(UkKeyEvent&)` |
| 30 | 1.5 % | `StringBOStream::putB(unsigned char)` |
| 29 | 1.5 % | `??` |
| 25 | 1.3 % | `UkEngine::getSeqSteps(int, int)` |
| 22 | 1.1 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 18 | 0.9 % | `main` |
| 17 | 0.9 % | `CVnCharsetLib::getVnCharset(int)` |
| 17 | 0.9 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 16 | 0.8 % | `UkInputProcessor::getCharType(unsigned int)` |
| 8 | 0.4 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 8 | 0.4 % | `UkEngine::processDd(UkKeyEvent&)` |
| 4 | 0.2 % | `StringBOStream::StringBOStream(unsigned char*, int)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 314 | 16.0 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 86 | 4.4 % | UkEngine::macroMatch(UkKeyEvent&) @ ??:? |
| 44 | 2.2 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 36 | 1.8 % | UkEngine::prepareBuffer() @ ??:? |
| 26 | 1.3 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 19 | 1.0 % | CMacroTable::lookup(unsigned int*) @ ??:? |
| 15 | 0.8 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 15 | 0.8 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:425 |
| 15 | 0.8 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 15 | 0.8 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 14 | 0.7 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 14 | 0.7 % | StringBOStream::putB(unsigned char) @ ??:? |
| 14 | 0.7 % | UkEngine::processAppend(UkKeyEvent&) @ ??:? |
| 12 | 0.6 % | bench::UniKeyDriver::vkFor(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:485 |
| 12 | 0.6 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 12 | 0.6 % | UkEngine::appendConsonnant(UkKeyEvent&) @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
