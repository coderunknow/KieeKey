# sampling profile — profile_unikey-4.x_as-shipped.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 1,893 samples at 1000 Hz over 7,579,645,881 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 53.0 ns/key measured
* unresolved sample count: 31 (1.6 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 388 | 20.5 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 274 | 14.5 % | `UkEngine::macroMatch(UkKeyEvent&)` |
| 144 | 7.6 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 109 | 5.8 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 95 | 5.0 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 93 | 4.9 % | `isValidVC(VowelSeq, ConSeq)` |
| 73 | 3.9 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 69 | 3.6 % | `UkEngine::processTone(UkKeyEvent&)` |
| 63 | 3.3 % | `bench::UniKeyDriver::invoke()` |
| 50 | 2.6 % | `UkEngine::lastWordIsNonVn()` |
| 48 | 2.5 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 48 | 2.5 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 45 | 2.4 % | `CMacroTable::lookup(unsigned int*)` |
| 40 | 2.1 % | `UkEngine::processHook(UkKeyEvent&)` |
| 39 | 2.1 % | `UkEngine::restoreKeyStrokes(int&, unsigned char*, int&, UkOutputType&)` |
| 37 | 2.0 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 31 | 1.6 % | `??` |
| 27 | 1.4 % | `StringBOStream::putB(unsigned char)` |
| 27 | 1.4 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 25 | 1.3 % | `UkEngine::prepareBuffer()` |
| 24 | 1.3 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 24 | 1.3 % | `UkEngine::getSeqSteps(int, int)` |
| 22 | 1.2 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 21 | 1.1 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 20 | 1.1 % | `main` |
| 14 | 0.7 % | `CVnCharsetLib::getVnCharset(int)` |
| 9 | 0.5 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 9 | 0.5 % | `UkEngine::processTelexW(UkKeyEvent&)` |
| 7 | 0.4 % | `UkInputProcessor::getCharType(unsigned int)` |
| 7 | 0.4 % | `UkEngine::processDd(UkKeyEvent&)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 287 | 15.2 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 123 | 6.5 % | UkEngine::macroMatch(UkKeyEvent&) @ ??:? |
| 41 | 2.2 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 41 | 2.2 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 29 | 1.5 % | CMacroTable::lookup(unsigned int*) @ ??:? |
| 16 | 0.8 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:425 |
| 16 | 0.8 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 15 | 0.8 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 14 | 0.7 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:423 |
| 13 | 0.7 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 12 | 0.6 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 12 | 0.6 % | UkEngine::lastWordIsNonVn() @ ??:? |
| 12 | 0.6 % | UkEngine::processAppend(UkKeyEvent&) @ ??:? |
| 11 | 0.6 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 11 | 0.6 % | UkEngine::processRoof(UkKeyEvent&) @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
