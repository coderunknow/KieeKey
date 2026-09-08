# sampling profile — profile_unikey-4.x_matched-minimal.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,350 samples at 1000 Hz over 5,402,812,500 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 37.8 ns/key measured
* unresolved sample count: 30 (2.2 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 358 | 26.5 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 126 | 9.3 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 114 | 8.4 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 95 | 7.0 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 68 | 5.0 % | `UkEngine::processTone(UkKeyEvent&)` |
| 67 | 5.0 % | `bench::UniKeyDriver::invoke()` |
| 45 | 3.3 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 44 | 3.3 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 42 | 3.1 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 41 | 3.0 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 40 | 3.0 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 37 | 2.7 % | `isValidVC(VowelSeq, ConSeq)` |
| 33 | 2.4 % | `UkEngine::prepareBuffer()` |
| 30 | 2.2 % | `??` |
| 29 | 2.1 % | `UkEngine::processHook(UkKeyEvent&)` |
| 27 | 2.0 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 25 | 1.9 % | `StringBOStream::putB(unsigned char)` |
| 23 | 1.7 % | `main` |
| 18 | 1.3 % | `UkEngine::getSeqSteps(int, int)` |
| 16 | 1.2 % | `UkInputProcessor::getCharType(unsigned int)` |
| 15 | 1.1 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 12 | 0.9 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 11 | 0.8 % | `CVnCharsetLib::getVnCharset(int)` |
| 9 | 0.7 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 8 | 0.6 % | `UkEngine::processNoSpellCheck(UkKeyEvent&)` |
| 5 | 0.4 % | `_init` |
| 5 | 0.4 % | `StringBOStream::StringBOStream(unsigned char*, int)` |
| 5 | 0.4 % | `UkEngine::processTelexW(UkKeyEvent&)` |
| 2 | 0.1 % | `UkEngine::processDd(UkKeyEvent&)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 302 | 22.4 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 31 | 2.3 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 30 | 2.2 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 28 | 2.1 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 16 | 1.2 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 15 | 1.1 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:425 |
| 14 | 1.0 % | bench::UniKeyDriver::vkFor(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:485 |
| 14 | 1.0 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 13 | 1.0 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 13 | 1.0 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 12 | 0.9 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 12 | 0.9 % | UkEngine::prepareBuffer() @ ??:? |
| 12 | 0.9 % | UkEngine::processRoof(UkKeyEvent&) @ ??:? |
| 11 | 0.8 % | UkEngine::processAppend(UkKeyEvent&) @ ??:? |
| 9 | 0.7 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:423 |
| 9 | 0.7 % | StringBOStream::putB(unsigned char) @ ??:? |
| 8 | 0.6 % | UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&) @ ??:? |
| 8 | 0.6 % | CVnCharsetLib::getVnCharset(int) @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
