# sampling profile — profile_unikey-4.x_matched-minimal.jsonl

* engine column: `unikey-4.x` · build `in-process` · stage `invoke` · configuration `matched-minimal`
* 1,288 samples at 1000 Hz over 5,156,935,219 ns of timed work (ITIMER_PROF granularity here is the kernel tick, ~4 ms)
* keys processed in the window: 143,094,720 → 36.0 ns/key measured
* unresolved sample count: 18 (1.4 %)

## top functions

| samples | % | function |
|---:|---:|---|
| 361 | 28.0 % | `UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&)` |
| 138 | 10.7 % | `UkEngine::appendConsonnant(UkKeyEvent&)` |
| 118 | 9.2 % | `UkEngine::appendVowel(UkKeyEvent&)` |
| 77 | 6.0 % | `UkEngine::processRoof(UkKeyEvent&)` |
| 65 | 5.0 % | `UkEngine::processTone(UkKeyEvent&)` |
| 45 | 3.5 % | `bench::UniKeyDriver::prepare(corpus::Event const&)` |
| 44 | 3.4 % | `UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&)` |
| 40 | 3.1 % | `bench::UniKeyDriver::invoke()` |
| 39 | 3.0 % | `UkEngine::processAppend(UkKeyEvent&)` |
| 36 | 2.8 % | `isValidVC(VowelSeq, ConSeq)` |
| 35 | 2.7 % | `UkEngine::writeOutput(unsigned char*, int&)` |
| 32 | 2.5 % | `UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&)` |
| 29 | 2.3 % | `UkEngine::getTonePosition(VowelSeq, bool)` |
| 28 | 2.2 % | `UkEngine::prepareBuffer()` |
| 27 | 2.1 % | `UkEngine::processHook(UkKeyEvent&)` |
| 24 | 1.9 % | `StringBOStream::putB(unsigned char)` |
| 19 | 1.5 % | `UkEngine::getSeqSteps(int, int)` |
| 18 | 1.4 % | `??` |
| 16 | 1.2 % | `CVnCharsetLib::getVnCharset(int)` |
| 16 | 1.2 % | `isValidCVC(ConSeq, VowelSeq, ConSeq)` |
| 15 | 1.2 % | `main` |
| 15 | 1.2 % | `UkEngine::processWordEnd(UkKeyEvent&)` |
| 11 | 0.9 % | `UkInputProcessor::getCharType(unsigned int)` |
| 11 | 0.9 % | `UkEngine::processNoSpellCheck(UkKeyEvent&)` |
| 10 | 0.8 % | `UkEngine::processBackspace(int&, unsigned char*, int&, UkOutputType&)` |
| 5 | 0.4 % | `_init` |
| 5 | 0.4 % | `UkEngine::processDd(UkKeyEvent&)` |
| 5 | 0.4 % | `UkEngine::processTelexW(UkKeyEvent&)` |
| 4 | 0.3 % | `StringBOStream::StringBOStream(unsigned char*, int)` |

## top 30 source lines (of the hottest 30 pcs)

| samples | % | location |
|---:|---:|---|
| 290 | 22.5 % | UkEngine::process(unsigned int, int&, unsigned char*, int&, UkOutputType&) @ ??:? |
| 42 | 3.3 % | UkInputProcessor::keyCodeToEvent(unsigned int, UkKeyEvent&) @ ??:? |
| 34 | 2.6 % | UkEngine::appendConsonnant(UkKeyEvent&) @ ??:? |
| 18 | 1.4 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:418 |
| 17 | 1.3 % | UkEngine::processTone(UkKeyEvent&) @ ??:? |
| 13 | 1.0 % | bench::UniKeyDriver::prepare(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:423 |
| 11 | 0.9 % | bench::UniKeyDriver::vkFor(corpus::Event const&) @ /home/user/KieeKey/benchmark/harness/engines.hpp:485 |
| 10 | 0.8 % | UkEngine::getTonePosition(VowelSeq, bool) @ ??:? |
| 9 | 0.7 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:427 |
| 9 | 0.7 % | bench::UniKeyDriver::invoke() @ /home/user/KieeKey/benchmark/harness/engines.hpp:429 |
| 9 | 0.7 % | UnicodeUTF8Charset::putChar(ByteOutStream&, unsigned int, int&) @ ??:? |
| 9 | 0.7 % | UkInputProcessor::getCharType(unsigned int) @ ??:? |
| 9 | 0.7 % | UkEngine::prepareBuffer() @ ??:? |
| 8 | 0.6 % | StringBOStream::putB(unsigned char) @ ??:? |
| 8 | 0.6 % | isValidVC(VowelSeq, ConSeq) @ ??:? |
| 8 | 0.6 % | UkEngine::processAppend(UkKeyEvent&) @ ??:? |
| 8 | 0.6 % | UkEngine::processRoof(UkKeyEvent&) @ ??:? |
| 7 | 0.5 % | main @ /home/user/KieeKey/benchmark/harness/rc1.hpp:404 |

_Ranking only. Decisions are made on `--mode=tput`, never here._
