# KieeKey v1.3.0 — Arcade / Chaos / AI stack benchmark

**Campaign:** `arcade-130` · **Date (UTC):** 2026-09-18T08:56:11Z · **Commit:** `3a7dacc`
**Host:** Linux 6.1.158+ x86_64 · Intel(R) Xeon(R) Processor @ 2.60GHz · **Compiler:** `g++ (Debian 12.2.0-14+deb12u1) 12.2.0`
**Parameters:** `--iters=4000`, `--server-iters=2000`

Raw evidence in this directory: [`arcade_bench.txt`](arcade_bench.txt) (measurements),
[`environment.json`](environment.json) (host/compiler freeze).

Reproduce:

```bash
tests/run_arcade_bench.sh --iters=4000 --server-iters=2000
```

## 1. What is measured

`tools/arcade_bench.cpp` drives the *real* arcade stack — the same
`ArcadeManager`, `Frame`, `buildRenderList`, `renderListToJson` and
`ArcadeServer` that the Win32 GDI window and the HTML5 client use — with a
scripted, deterministic input stream (typing the passage for the typing games,
WASD/space for the others) and reports monotonic-clock percentiles per stage:

| stage | what the front-end does with it |
|---|---|
| `update` | one 1/60 s simulation step (`ArcadeManager::update`) |
| `build` | `buildRenderList` — the device-independent display list |
| `json` | `renderListToJson` — the wire payload of the web player |
| `input` | `handleKey` — one keystroke into the running game |

The frame budget is the 16 667 µs of a 60 FPS frame.

## 2. Per-game frame cost (µs)

| game | title | update p50 | update p99 | display list (mean) | JSON (mean) | frame total (mean) | % of 60 FPS budget | cmds | JSON bytes | digest |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| `snake` | Snake | 0.073 | 0.308 | 0.99 | 63.3 | **64.4** | 0.386% | 42 | 1881 | `cdd82d8d313aa88c` |
| `tetris` | Tetris | 0.052 | 0.211 | 1.42 | 28.3 | **29.8** | 0.179% | 18 | 1289 | `be6abe9c15b595f7` |
| `fishing` | Fishing | 0.056 | 0.288 | 1.59 | 46.8 | **48.5** | 0.291% | 32 | 1910 | `ac317530d87618f0` |
| `typing-race` | Typing Race | 0.070 | 0.258 | 1.78 | 46.1 | **47.9** | 0.288% | 30 | 1777 | `b94f13233ee159e5` |
| `wasd-race` | WASD + Typing Racing | 0.081 | 0.306 | 2.08 | 82.6 | **84.8** | 0.509% | 50 | 3081 | `c757fb9552879a3d` |
| `rhythm` | Rhythm Typing | 0.053 | 0.215 | 1.49 | 30.9 | **32.4** | 0.195% | 20 | 1467 | `a4ad3adab7ba4917` |
| `no-mistake` | No-Mistake Mode | 0.052 | 0.187 | 1.59 | 14.8 | **16.5** | 0.099% | 10 | 1074 | `7c820be7274c0f53` |
| `flexing` | Flexing Mode | 0.052 | 0.201 | 2.61 | 18.1 | **20.7** | 0.124% | 12 | 1398 | `bd01fd0b69b49757` |

**Reading the table**

* The whole pipeline of the heaviest game costs **84.8 µs**
  — under **0.51 %** of a 60 FPS frame — and the
  simulation itself never exceeds ~0.2 µs (p99), i.e. the games are not what
  would ever cost a frame.
* JSON serialization dominates the pipeline, and only the web player pays it
  (the native window draws the display list directly). Payloads stay in the
  1–3 KB range, which keeps the SSE stream comfortable on localhost and over a
  LAN.
* The `digest` column is an FNV-1a over the JSON frames of the run: rerunning
  the benchmark on the same commit must reproduce it, which is what makes the
  numbers comparable across machines.

## 3. HTTP bridge (no sockets, `ArcadeServer::handleRequest`)

| request | mean | p99 | notes |
|---|---:|---:|---|
| `GET /api/state` | 50.42 µs | 99.87 µs | full frame JSON, 1889 bytes |
| `POST /api/input` | 0.70 µs | 1.86 µs | one keystroke forwarded to the game |
| `GET /arcade.js` | 37.59 µs | 62.90 µs | static file served from `web/` |

The bridge serves the state endpoint at up to **19834 requests/s on a
single thread**, i.e. roughly two orders of magnitude more than the 60 Hz the
player asks for.

## 4. Steady-state allocations (Tetris, 600 frames, allocation counter armed)

| pipeline | allocations per frame |
|---|---:|
| game logic + display list (native window per frame) | **0.0000** |
| game logic + display list + wire JSON (web player per frame) | 1.0000 (the value-returning `renderListToJson` convenience overload) |
| one keystroke (`handleKey`) | 0.0000 |

`allocations_logic_total = 0` over 600 frames means the frame pipeline
that the native window runs **does not touch the heap at all** once warmed up.
This was not true before this campaign: the display list re-created every
command's text buffer and re-converted the four frame strings on every frame
(~4 allocations/frame, ~240/s). Fixed by reusing the command slots and the
conversion buffer (`RenderCommand::reset`, `RenderList::scratch`,
`utf8FromUtf32(text, out)`), and locked in by
`test_arcade.cpp::testNoSteadyStateAllocations`, which now measures the render
path too.

Total counter over the whole benchmark run: 600 `new` / 600 `delete` (balanced).

## 5. Correctness gates that accompany these numbers

The benchmark measures speed; these suites measure behaviour, and they all run
in the standard suite (`tests/run_all_tests.sh`, or `ctest` on Windows):

| suite | result |
|---|---|
| `test_arcade` | 3742 checks, 0 failures (games, manager, determinism, allocations) |
| `test_arcade_render` | 6/6 (display list, JSON, injection safety, viewport, payload budget) |
| `test_arcade_server` | 6/6 (routing, sessions, input, traversal guards, full race, isolation) |
| `test_arcade_window` | 5/5 (the real Win32 window procedures, executed via the GDI recorder) |
| `test_chaos` / `test_ai_rival` / `test_progression` / `test_analytics` / `test_online_ghost` | 4 / 6 / 9 / 6 / ✓ |
| engine gate | 2 059 419 events vs the clean-room oracle, 0 mismatches |
| feature isolation | 0.0 % overhead with the arcade/chaos modules idle, bit-identical output digests |

## 6. Notes and limits

* Numbers are from a shared cloud VM (Intel(R) Xeon(R) Processor @ 2.60GHz); treat the *ratios* and
  the allocation counts as the portable results, the absolute microseconds as
  host-specific.
* The frame-budget column uses the 60 FPS target, which is what both
  front-ends schedule (`WM_TIMER` at 16 ms in the GDI hub, a 60 Hz tick in the
  web bridge).
* The native window adds GDI painting on top (double-buffered `BitBlt` of a
  cached display list); its cost is dominated by the number of draw calls and
  is reported indirectly by the `cmds` column — 9–50 commands per game.
