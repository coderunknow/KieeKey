# sampling profile — profile_unikey-4.x_matched-minimal.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,572 samples at 1000 Hz over 6,291,449,355 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 44.0 ns/key measured
* unresolved sample count: 28 (1.8 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 423 | 26.9 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 145 | 9.2 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 143 | 9.1 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 96 | 6.1 % | `UkEngine::processTone(UkKeyEvent&)` |
| 95 | 6.0 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 60 | 3.8 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 57 | 3.6 % | `isValidVC(VowelSeq, ConSeq)` |
| 54 | 3.4 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 50 | 3.2 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 49 | 3.1 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 47 | 3.0 % | `bench::UniKeyDriver::invoke()` |
| 41 | 2.6 % | `UkEngine::processHook(UkKeyEvent&)` |
| 34 | 2.2 % | `UkEngine::prepareBuffer()` |
| 33 | 2.1 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 32 | 2.0 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 28 | 1.8 % | `??` |
| 25 | 1.6 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 23 | 1.5 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 22 | 1.4 % | `StringBOStream::putB(unsigned char)` |
| 18 | 1.1 % | `UkEngine::getSeqSteps(int, int)` |
| 17 | 1.1 % | `UkInputProcessor::getCharType(unsigned int)` |
| 15 | 1.0 % | `UkEngine::processNoSpellCheck(UkKeyEvent&)` |
| 13 | 0.8 % | `CVnCharsetLib::getVnCharset(int)` |
| 11 | 0.7 % | `UkEngine::processTelexW(UkKeyEvent&)` |
| 9 | 0.6 % | `_init` |
| 9 | 0.6 % | `main` |
| 8 | 0.5 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 7 | 0.4 % | `StringBOStream::StringBOStream(unsigned char*, int)` |
| 5 | 0.3 % | `UkEngine::processDd(UkKeyEvent&)` |
| 3 | 0.2 % | `UkEngine::processHookWithUO(UkKeyEvent&)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 350 | 22.3 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 44 | 2.8 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 26 | 1.7 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 23 | 1.5 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 22 | 1.4 % | UkEngine::processAppend(UkKeyEvent&) @ ??:? |
| 21 | 1.3 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 19 | 1.2 % | bench::UniKeyDriver::vkFor(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:485 |
| 18 | 1.1 % | StringBOStream::putB(unsigned char) @ ??:? |
| 16 | 1.0 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 15 | 1.0 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 15 | 1.0 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 12 | 0.8 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:423 |
| 10 | 0.6 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:425 |
| 9 | 0.6 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 9 | 0.6 % | UkEngine::prepareBuffer() @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
