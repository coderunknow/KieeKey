# sampling profile — profile_unikey-4.x_matched-minimal.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,311 samples at 1000 Hz over 5,249,221,757 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 36.7 ns/key measured
* unresolved sample count: 33 (2.5 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 356 | 27.2 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 118 | 9.0 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 104 | 7.9 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 78 | 5.9 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 71 | 5.4 % | `UkEngine::processTone(UkKeyEvent&)` |
| 53 | 4.0 % | `bench::UniKeyDriver::invoke()` |
| 44 | 3.4 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 40 | 3.1 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 39 | 3.0 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 38 | 2.9 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 37 | 2.8 % | `isValidVC(VowelSeq, ConSeq)` |
| 37 | 2.8 % | `UkEngine::prepareBuffer()` |
| 36 | 2.7 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 33 | 2.5 % | `??` |
| 26 | 2.0 % | `main` |
| 25 | 1.9 % | `UkEngine::processHook(UkKeyEvent&)` |
| 22 | 1.7 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 22 | 1.7 % | `CVnCharsetLib::getVnCharset(int)` |
| 22 | 1.7 % | `UkEngine::getSeqSteps(int, int)` |
| 22 | 1.7 % | `UkEngine::processNoSpellCheck(UkKeyEvent&)` |
| 20 | 1.5 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 15 | 1.1 % | `UkInputProcessor::getCharType(unsigned int)` |
| 14 | 1.1 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 10 | 0.8 % | `StringBOStream::putB(unsigned char)` |
| 9 | 0.7 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 5 | 0.4 % | `_init` |
| 5 | 0.4 % | `UkEngine::processDd(UkKeyEvent&)` |
| 5 | 0.4 % | `UkEngine::processTelexW(UkKeyEvent&)` |
| 4 | 0.3 % | `StringBOStream::StringBOStream(unsigned char*, int)` |
| 1 | 0.1 % | `UkEngine::processHookWithUO(UkKeyEvent&)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 269 | 20.5 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 35 | 2.7 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 26 | 2.0 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 25 | 1.9 % | UkEngine::prepareBuffer() @ ??:? |
| 19 | 1.4 % | CVnCharsetLib::getVnCharset(int) @ ??:? |
| 15 | 1.1 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:423 |
| 14 | 1.1 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 12 | 0.9 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 12 | 0.9 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 11 | 0.8 % | UkEngine::processRoof(UkKeyEvent&) @ ??:? |
| 10 | 0.8 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 10 | 0.8 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 10 | 0.8 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 9 | 0.7 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:425 |
| 9 | 0.7 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 9 | 0.7 % | UkEngine::writeOutput(unsigned char*, int&) @ ??:? |
| 8 | 0.6 % | modeProfile @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 8 | 0.6 % | UkEngine::getSeqSteps(int, int) @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
