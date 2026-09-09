# sampling profile — profile_unikey-4.x_matched-minimal.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,325 samples at 1000 Hz over 5,303,719,819 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 37.1 ns/key measured
* unresolved sample count: 24 (1.8 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 376 | 28.4 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 102 | 7.7 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 101 | 7.6 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 74 | 5.6 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 71 | 5.4 % | `UkEngine::processTone(UkKeyEvent&)` |
| 57 | 4.3 % | `bench::UniKeyDriver::invoke()` |
| 47 | 3.5 % | `isValidVC(VowelSeq, ConSeq)` |
| 47 | 3.5 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 45 | 3.4 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 44 | 3.3 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 43 | 3.2 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 42 | 3.2 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 34 | 2.6 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 31 | 2.3 % | `UkEngine::processHook(UkKeyEvent&)` |
| 24 | 1.8 % | `??` |
| 24 | 1.8 % | `main` |
| 24 | 1.8 % | `UkEngine::getSeqSteps(int, int)` |
| 24 | 1.8 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 22 | 1.7 % | `StringBOStream::putB(unsigned char)` |
| 21 | 1.6 % | `UkEngine::prepareBuffer()` |
| 13 | 1.0 % | `CVnCharsetLib::getVnCharset(int)` |
| 13 | 1.0 % | `UkInputProcessor::getCharType(unsigned int)` |
| 13 | 1.0 % | `UkEngine::processNoSpellCheck(UkKeyEvent&)` |
| 9 | 0.7 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 6 | 0.5 % | `_init` |
| 5 | 0.4 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 5 | 0.4 % | `UkEngine::processTelexW(UkKeyEvent&)` |
| 3 | 0.2 % | `StringBOStream::StringBOStream(unsigned char*, int)` |
| 3 | 0.2 % | `UkEngine::processDd(UkKeyEvent&)` |
| 1 | 0.1 % | `UkEngine::processHookWithUO(UkKeyEvent&)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 310 | 23.4 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 40 | 3.0 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 33 | 2.5 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 31 | 2.3 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 16 | 1.2 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:423 |
| 16 | 1.2 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 13 | 1.0 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 13 | 1.0 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:425 |
| 13 | 1.0 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 13 | 1.0 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 13 | 1.0 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 10 | 0.8 % | UkEngine::getSeqSteps(int, int) @ ??:? |
| 8 | 0.6 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 8 | 0.6 % | UkEngine::processAppend(UkKeyEvent&) @ ??:? |
| 8 | 0.6 % | UkEngine::processRoof(UkKeyEvent&) @ ??:? |
| 7 | 0.5 % | UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&) @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
