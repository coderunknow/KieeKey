# sampling profile — profile_unikey-4.x_matched-minimal.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,343 samples at 1000 Hz over 5,373,291,317 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 37.6 ns/key measured
* unresolved sample count: 22 (1.6 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 361 | 26.9 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 125 | 9.3 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 109 | 8.1 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 83 | 6.2 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 76 | 5.7 % | `UkEngine::processTone(UkKeyEvent&)` |
| 59 | 4.4 % | `bench::UniKeyDriver::invoke()` |
| 48 | 3.6 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 45 | 3.4 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 42 | 3.1 % | `isValidVC(VowelSeq, ConSeq)` |
| 35 | 2.6 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 35 | 2.6 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 33 | 2.5 % | `UkEngine::processHook(UkKeyEvent&)` |
| 32 | 2.4 % | `StringBOStream::putB(unsigned char)` |
| 30 | 2.2 % | `main` |
| 30 | 2.2 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 30 | 2.2 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 29 | 2.2 % | `UkEngine::prepareBuffer()` |
| 22 | 1.6 % | `??` |
| 22 | 1.6 % | `UkEngine::getSeqSteps(int, int)` |
| 20 | 1.5 % | `UkInputProcessor::getCharType(unsigned int)` |
| 15 | 1.1 % | `UkEngine::processNoSpellCheck(UkKeyEvent&)` |
| 13 | 1.0 % | `CVnCharsetLib::getVnCharset(int)` |
| 12 | 0.9 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 12 | 0.9 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 9 | 0.7 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 5 | 0.4 % | `UkEngine::processDd(UkKeyEvent&)` |
| 3 | 0.2 % | `_init` |
| 3 | 0.2 % | `StringBOStream::StringBOStream(unsigned char*, int)` |
| 2 | 0.1 % | `UkEngine::processHookWithUO(UkKeyEvent&)` |
| 2 | 0.1 % | `UkEngine::processTelexW(UkKeyEvent&)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 288 | 21.4 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 42 | 3.1 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 24 | 1.8 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 22 | 1.6 % | UkEngine::prepareBuffer() @ ??:? |
| 22 | 1.6 % | StringBOStream::putB(unsigned char) @ ??:? |
| 19 | 1.4 % | UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&) @ ??:? |
| 17 | 1.3 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 16 | 1.2 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 15 | 1.1 % | bench::UniKeyDriver::vkFor(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:485 |
| 14 | 1.0 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 13 | 1.0 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:425 |
| 12 | 0.9 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 12 | 0.9 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 10 | 0.7 % | UkEngine::processAppend(UkKeyEvent&) @ ??:? |
| 9 | 0.7 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 8 | 0.6 % | modeProfile @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 8 | 0.6 % | UkEngine::getSeqSteps(int, int) @ ??:? |
| 8 | 0.6 % | UkEngine::processRoof(UkKeyEvent&) @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
