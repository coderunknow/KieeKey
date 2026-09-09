# sampling profile — profile_unikey-4.x_matched-minimal.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,258 samples at 1000 Hz over 5,034,444,258 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 35.2 ns/key measured
* unresolved sample count: 33 (2.6 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 358 | 28.5 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 108 | 8.6 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 99 | 7.9 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 87 | 6.9 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 57 | 4.5 % | `UkEngine::processTone(UkKeyEvent&)` |
| 44 | 3.5 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 42 | 3.3 % | `bench::UniKeyDriver::invoke()` |
| 42 | 3.3 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 41 | 3.3 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 41 | 3.3 % | `UkEngine::processHook(UkKeyEvent&)` |
| 38 | 3.0 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 35 | 2.8 % | `isValidVC(VowelSeq, ConSeq)` |
| 33 | 2.6 % | `??` |
| 32 | 2.5 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 28 | 2.2 % | `main` |
| 27 | 2.1 % | `UkEngine::prepareBuffer()` |
| 24 | 1.9 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 21 | 1.7 % | `StringBOStream::putB(unsigned char)` |
| 21 | 1.7 % | `UkEngine::getSeqSteps(int, int)` |
| 14 | 1.1 % | `UkEngine::processNoSpellCheck(UkKeyEvent&)` |
| 14 | 1.1 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 12 | 1.0 % | `CVnCharsetLib::getVnCharset(int)` |
| 12 | 1.0 % | `UkInputProcessor::getCharType(unsigned int)` |
| 7 | 0.6 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 6 | 0.5 % | `_init` |
| 6 | 0.5 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 5 | 0.4 % | `UkEngine::processDd(UkKeyEvent&)` |
| 2 | 0.2 % | `StringBOStream::StringBOStream(unsigned char*, int)` |
| 1 | 0.1 % | `UkEngine::processMapChar(UkKeyEvent&)` |
| 1 | 0.1 % | `UkEngine::processTelexW(UkKeyEvent&)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 270 | 21.5 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 42 | 3.3 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 31 | 2.5 % | UkEngine::processRoof(UkKeyEvent&) @ ??:? |
| 22 | 1.7 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 17 | 1.4 % | UkEngine::processAppend(UkKeyEvent&) @ ??:? |
| 16 | 1.3 % | UkEngine::prepareBuffer() @ ??:? |
| 14 | 1.1 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:425 |
| 12 | 1.0 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:423 |
| 12 | 1.0 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 11 | 0.9 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 11 | 0.9 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 9 | 0.7 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |
| 9 | 0.7 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 9 | 0.7 % | bench::UniKeyDriver::vkFor(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:485 |
| 9 | 0.7 % | StringBOStream::putB(unsigned char) @ ??:? |
| 9 | 0.7 % | UkEngine::getSeqSteps(int, int) @ ??:? |
| 8 | 0.6 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 8 | 0.6 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 7 | 0.6 % | ?? @ ??:0 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
