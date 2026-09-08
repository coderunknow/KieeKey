# sampling profile — profile_unikey-4.x_as-shipped.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 1,807 samples at 1000 Hz over 7,229,003,218 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 50.5 ns/key measured
* unresolved sample count: 31 (1.7 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 352 | 19.5 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 248 | 13.7 % | `UkEngine::macroMatch(UkKeyEvent&)` |
| 160 | 8.9 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 108 | 6.0 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 94 | 5.2 % | `isValidVC(VowelSeq, ConSeq)` |
| 73 | 4.0 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 70 | 3.9 % | `UkEngine::processTone(UkKeyEvent&)` |
| 60 | 3.3 % | `bench::UniKeyDriver::invoke()` |
| 57 | 3.2 % | `UkEngine::lastWordIsNonVn()` |
| 54 | 3.0 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 49 | 2.7 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 45 | 2.5 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 42 | 2.3 % | `CMacroTable::lookup(unsigned int*)` |
| 40 | 2.2 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 38 | 2.1 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 35 | 1.9 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 33 | 1.8 % | `UkEngine::restoreKeyStrokes(int&, unsigned char*, int&, UkOutputType&)` |
| 32 | 1.8 % | `UkEngine::processHook(UkKeyEvent&)` |
| 31 | 1.7 % | `??` |
| 29 | 1.6 % | `UkEngine::prepareBuffer()` |
| 26 | 1.4 % | `UkEngine::getSeqSteps(int, int)` |
| 24 | 1.3 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 19 | 1.1 % | `UkInputProcessor::getCharType(unsigned int)` |
| 16 | 0.9 % | `CVnCharsetLib::getVnCharset(int)` |
| 15 | 0.8 % | `main` |
| 15 | 0.8 % | `StringBOStream::putB(unsigned char)` |
| 15 | 0.8 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 5 | 0.3 % | `StringBOStream::StringBOStream(unsigned char*, int)` |
| 5 | 0.3 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 4 | 0.2 % | `UkInputProcessor::keyCodeToSymbol(unsigned int, UkKeyEvent&)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 269 | 14.9 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 104 | 5.8 % | UkEngine::macroMatch(UkKeyEvent&) @ ??:? |
| 39 | 2.2 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 31 | 1.7 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 24 | 1.3 % | CMacroTable::lookup(unsigned int*) @ ??:? |
| 19 | 1.1 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 17 | 0.9 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 17 | 0.9 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 15 | 0.8 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 15 | 0.8 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:425 |
| 14 | 0.8 % | bench::UniKeyDriver::vkFor(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:485 |
| 14 | 0.8 % | UkEngine::lastWordIsNonVn() @ ??:? |
| 12 | 0.7 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 12 | 0.7 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 11 | 0.6 % | UkEngine::prepareBuffer() @ ??:? |
| 10 | 0.6 % | UkEngine::getSeqSteps(int, int) @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
