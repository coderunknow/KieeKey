# sampling profile — profile_unikey-4.x_as-shipped.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `as-shipped`
* 1,840 samples at 1000 Hz over 7,363,170,884 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 51.5 ns/key measured
* unresolved sample count: 28 (1.5 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 384 | 20.9 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 274 | 14.9 % | `UkEngine::macroMatch(UkKeyEvent&)` |
| 139 | 7.6 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 96 | 5.2 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 90 | 4.9 % | `isValidVC(VowelSeq, ConSeq)` |
| 78 | 4.2 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 71 | 3.9 % | `UkEngine::processTone(UkKeyEvent&)` |
| 64 | 3.5 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 49 | 2.7 % | `UkEngine::lastWordIsNonVn()` |
| 48 | 2.6 % | `bench::UniKeyDriver::invoke()` |
| 45 | 2.4 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 44 | 2.4 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 41 | 2.2 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 41 | 2.2 % | `UkEngine::restoreKeyStrokes(int&, unsigned char*, int&, UkOutputType&)` |
| 40 | 2.2 % | `UkEngine::prepareBuffer()` |
| 38 | 2.1 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 35 | 1.9 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 32 | 1.7 % | `CMacroTable::lookup(unsigned int*)` |
| 28 | 1.5 % | `??` |
| 27 | 1.5 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 27 | 1.5 % | `UkEngine::processHook(UkKeyEvent&)` |
| 26 | 1.4 % | `StringBOStream::putB(unsigned char)` |
| 25 | 1.4 % | `UkEngine::getSeqSteps(int, int)` |
| 21 | 1.1 % | `main` |
| 16 | 0.9 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 13 | 0.7 % | `CVnCharsetLib::getVnCharset(int)` |
| 13 | 0.7 % | `UkInputProcessor::getCharType(unsigned int)` |
| 9 | 0.5 % | `UkEngine::processDd(UkKeyEvent&)` |
| 8 | 0.4 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 6 | 0.3 % | `UkEngine::processTelexW(UkKeyEvent&)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 317 | 17.2 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 125 | 6.8 % | UkEngine::macroMatch(UkKeyEvent&) @ ??:? |
| 40 | 2.2 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 38 | 2.1 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 24 | 1.3 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 16 | 0.9 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 15 | 0.8 % | UkEngine::prepareBuffer() @ ??:? |
| 15 | 0.8 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 14 | 0.8 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 12 | 0.7 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 12 | 0.7 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 10 | 0.5 % | StringBOStream::putB(unsigned char) @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
