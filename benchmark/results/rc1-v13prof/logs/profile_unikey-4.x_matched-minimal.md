# sampling profile — profile_unikey-4.x_matched-minimal.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,429 samples at 1000 Hz over 5,720,560,517 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 40.0 ns/key measured
* unresolved sample count: 25 (1.7 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 372 | 26.0 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 136 | 9.5 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 121 | 8.5 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 107 | 7.5 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 72 | 5.0 % | `UkEngine::processTone(UkKeyEvent&)` |
| 63 | 4.4 % | `bench::UniKeyDriver::invoke()` |
| 60 | 4.2 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 49 | 3.4 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 49 | 3.4 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 46 | 3.2 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 44 | 3.1 % | `UkEngine::prepareBuffer()` |
| 43 | 3.0 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 38 | 2.7 % | `isValidVC(VowelSeq, ConSeq)` |
| 26 | 1.8 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 25 | 1.7 % | `??` |
| 24 | 1.7 % | `UkEngine::processNoSpellCheck(UkKeyEvent&)` |
| 20 | 1.4 % | `main` |
| 20 | 1.4 % | `CVnCharsetLib::getVnCharset(int)` |
| 20 | 1.4 % | `UkEngine::processHook(UkKeyEvent&)` |
| 19 | 1.3 % | `StringBOStream::putB(unsigned char)` |
| 16 | 1.1 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 14 | 1.0 % | `UkInputProcessor::getCharType(unsigned int)` |
| 14 | 1.0 % | `UkEngine::getSeqSteps(int, int)` |
| 8 | 0.6 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 7 | 0.5 % | `UkEngine::processTelexW(UkKeyEvent&)` |
| 6 | 0.4 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 5 | 0.3 % | `StringBOStream::StringBOStream(unsigned char*, int)` |
| 4 | 0.3 % | `UkEngine::processDd(UkKeyEvent&)` |
| 1 | 0.1 % | `_init` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 303 | 21.2 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 48 | 3.4 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 43 | 3.0 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 32 | 2.2 % | UkEngine::prepareBuffer() @ ??:? |
| 23 | 1.6 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:425 |
| 20 | 1.4 % | UkEngine::processAppend(UkKeyEvent&) @ ??:? |
| 18 | 1.3 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 16 | 1.1 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 14 | 1.0 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 13 | 0.9 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 12 | 0.8 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:423 |
| 10 | 0.7 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 9 | 0.6 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 9 | 0.6 % | UkEngine::appendVowel(UkKeyEvent&) @ ??:? |
| 8 | 0.6 % | ?? @ ??:0 |
| 8 | 0.6 % | StringBOStream::putB(unsigned char) @ ??:? |
| 8 | 0.6 % | CVnCharsetLib::getVnCharset(int) @ ??:? |

_Ranking only. Decisions are made on `--mode=tput`, never here._
