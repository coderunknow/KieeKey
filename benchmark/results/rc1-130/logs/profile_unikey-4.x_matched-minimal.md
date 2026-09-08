# sampling profile — profile_unikey-4.x_matched-minimal.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,594 samples at 1000 Hz over 6,377,907,482 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 44.6 ns/key measured
* unresolved sample count: 31 (1.9 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 414 | 26.0 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 153 | 9.6 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 110 | 6.9 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 110 | 6.9 % | `UkEngine::processTone(UkKeyEvent&)` |
| 94 | 5.9 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 61 | 3.8 % | `bench::UniKeyDriver::invoke()` |
| 59 | 3.7 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 56 | 3.5 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 53 | 3.3 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 53 | 3.3 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 46 | 2.9 % | `isValidVC(VowelSeq, ConSeq)` |
| 44 | 2.8 % | `UkEngine::prepareBuffer()` |
| 35 | 2.2 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 35 | 2.2 % | `UkEngine::processHook(UkKeyEvent&)` |
| 31 | 1.9 % | `??` |
| 30 | 1.9 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 29 | 1.8 % | `StringBOStream::putB(unsigned char)` |
| 28 | 1.8 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 27 | 1.7 % | `main` |
| 27 | 1.7 % | `UkEngine::getSeqSteps(int, int)` |
| 23 | 1.4 % | `UkEngine::processNoSpellCheck(UkKeyEvent&)` |
| 20 | 1.3 % | `UkInputProcessor::getCharType(unsigned int)` |
| 19 | 1.2 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 12 | 0.8 % | `CVnCharsetLib::getVnCharset(int)` |
| 9 | 0.6 % | `UkEngine::processTelexW(UkKeyEvent&)` |
| 6 | 0.4 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 4 | 0.3 % | `_init` |
| 4 | 0.3 % | `UkEngine::processDd(UkKeyEvent&)` |
| 2 | 0.1 % | `StringBOStream::StringBOStream(unsigned char*, int)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 320 | 20.1 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 40 | 2.5 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 37 | 2.3 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 33 | 2.1 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 32 | 2.0 % | UkEngine::prepareBuffer() @ ??:? |
| 19 | 1.2 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 17 | 1.1 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:425 |
| 15 | 0.9 % | bench::UniKeyDriver::vkFor(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:485 |
| 14 | 0.9 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 13 | 0.8 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 13 | 0.8 % | UkEngine::writeOutput(unsigned char*, int&) @ ??:? |
| 13 | 0.8 % | UkEngine::processRoof(UkKeyEvent&) @ ??:? |
| 12 | 0.8 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 12 | 0.8 % | UkEngine::appendConsonnant(UkKeyEvent&) @ ??:? |
| 11 | 0.7 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 11 | 0.7 % | UkEngine::processAppend(UkKeyEvent&) @ ??:? |
| 10 | 0.6 % | UkEngine::getSeqSteps(int, int) @ ??:? |
| 9 | 0.6 % | StringBOStream::putB(unsigned char) @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
