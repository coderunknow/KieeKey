# sampling profile — profile_unikey-4.x_matched-minimal.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,423 samples at 1000 Hz over 5,697,116,576 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 39.8 ns/key measured
* unresolved sample count: 24 (1.7 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 371 | 26.1 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 135 | 9.5 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 126 | 8.9 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 94 | 6.6 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 66 | 4.6 % | `UkEngine::processTone(UkKeyEvent&)` |
| 59 | 4.1 % | `bench::UniKeyDriver::invoke()` |
| 53 | 3.7 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 50 | 3.5 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 48 | 3.4 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 43 | 3.0 % | `isValidVC(VowelSeq, ConSeq)` |
| 41 | 2.9 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 40 | 2.8 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 31 | 2.2 % | `main` |
| 31 | 2.2 % | `UkEngine::processHook(UkKeyEvent&)` |
| 30 | 2.1 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 30 | 2.1 % | `UkEngine::prepareBuffer()` |
| 28 | 2.0 % | `StringBOStream::putB(unsigned char)` |
| 24 | 1.7 % | `??` |
| 19 | 1.3 % | `UkEngine::getSeqSteps(int, int)` |
| 19 | 1.3 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 17 | 1.2 % | `UkInputProcessor::getCharType(unsigned int)` |
| 14 | 1.0 % | `CVnCharsetLib::getVnCharset(int)` |
| 12 | 0.8 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 12 | 0.8 % | `UkEngine::processNoSpellCheck(UkKeyEvent&)` |
| 8 | 0.6 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 6 | 0.4 % | `_init` |
| 6 | 0.4 % | `StringBOStream::StringBOStream(unsigned char*, int)` |
| 5 | 0.4 % | `UkEngine::processTelexW(UkKeyEvent&)` |
| 4 | 0.3 % | `UkEngine::processHookWithUO(UkKeyEvent&)` |
| 1 | 0.1 % | `UkEngine::processDd(UkKeyEvent&)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 315 | 22.1 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 37 | 2.6 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 37 | 2.6 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 24 | 1.7 % | UkEngine::prepareBuffer() @ ??:? |
| 20 | 1.4 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 19 | 1.3 % | UkEngine::processAppend(UkKeyEvent&) @ ??:? |
| 17 | 1.2 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 16 | 1.1 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 15 | 1.1 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 14 | 1.0 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 12 | 0.8 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:425 |
| 12 | 0.8 % | StringBOStream::putB(unsigned char) @ ??:? |
| 11 | 0.8 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 10 | 0.7 % | bench::UniKeyDriver::vkFor(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:485 |
| 9 | 0.6 % | modeProfile @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 9 | 0.6 % | UkEngine::appendVowel(UkKeyEvent&) @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
