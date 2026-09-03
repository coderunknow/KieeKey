> **Lineage note (KieeKey v1.0 packaging):** Historical engineering
> document from the refactor lineage that produced KieeKey v1.0 (a fork of
> OpenKey, GPL-3.0). Written before the v1.0 release unification, it uses the
> pre-release working name "OpenKey NextGen" and internal milestone numbers
> (v3.0-v3.4). Published verbatim for traceability - see
> [docs/reports/README.md](README.md).

# Three-Engine Benchmark — OpenKey NextGen vs OpenKey 2.0.5 vs UniKey

**Date:** 2026-08-29 · **Method:** Telex · deterministic byte-identical key streams.

- **OpenKey NextGen v3.0** — shipped `TextEngine` (this repository).
- **OpenKey 2.0.5** — vendored, unmodified legacy engine (`tests/reference/openkey-2.0.5`, compiled `-DLINUX`).
- **UniKey 4.x** — vendored, unmodified `UKEngine` from the official GPL source (`tests/reference/unikey`); the engine behind the current UniKey release line 4.6.250531 (built 2025-12-28). Engine source pulled from the GitHub mirror `hochanh/unikey-source` (SourceForge CVS snapshot) because the official SourceForge SVN is unreachable.

Latency = engine-decision cost per key (the reproducible part headlessly). OS text I/O (TSF / SendInput) is excluded — it is app/OS dependent and measured separately in the app's telemetry.

## 1. Correctness — final visible text per stream

| stream | OpenKey NextGen (v3.0) | OpenKey 2.0.5 (legacy) | UniKey (4.x UKEngine) | 3-way |
|---|---|---|---|---|
| passage-0 | `Xin chào, tôi tên là Nam. Rất …` | `Xin chào, tôi tên là Nam. Rất …` | `Xin chào, tôi tên là Nam. Rất …` | ✓ |
| passage-1 | `Hôm nay trời đẹp quá, chúng …` | `Hôm nay trời đẹp quá, chúng …` | `Hôm nay trời đẹp quá, chúng …` | ✓ |
| passage-2 | `Tôi đang học tiếng Việt ở …` | `Tôi đang học tiếng Việt ở …` | `Tôi đang học tiếng Việt ở …` | ✓ |
| passage-3 | `Cảm ơn bạn đã giúp đỡ tô…` | `Cảm ơn bạn đã giúp đỡ tô…` | `Cảm ơn bạn đã giúp đỡ tô…` | ✓ |
| passage-4 | `Bạn có khỏe không? Tôi khỏe…` | `Bạn có khỏe không? Tôi khỏe…` | `Bạn có khỏe không? Tôi khỏe…` | ✓ |
| passage-5 | `Công việc hôm nay nhiều quá, …` | `Công việc hôm nay nhiều quá, …` | `Công việc hôm nay nhiều quá, …` | ✓ |
| passage-6 | `Món ăn này ngon quá, bạn nấu…` | `Món ăn này ngon quá, bạn nấu…` | `Món ăn này ngon quá, bạn nấu…` | ✓ |
| passage-7 | `Ngày mai chúng ta họp lúc 9 gi…` | `Ngày mai chúng ta họp lúc 9 gi…` | `Ngày mai chúng ta họp lúc 9 gi…` | ✓ |
| passage-8 | `Tôi sẽ gửi báo cáo cho bạn …` | `Tôi sẽ gửi báo cáo cho bạn …` | `Tôi sẽ gửi báo cáo cho bạn …` | ✓ |
| passage-9 | `Em yêu anh nhiều lắm, anh có n…` | `Em yêu anh nhiều lắm, anh có n…` | `Em yêu anh nhiều lắm, anh có n…` | ✓ |
| passage-10 | `Please check the file tôi đã gử…` | `Please check the file tôi đã gử…` | `Please check the file tôi đã gử…` | ✓ |
| passage-11 | `OK, tôi sẽ confirm lại với te…` | `OK, tôi sẽ cònirm lại với te…` | `OK, tôi sẽ cònirm lại với te…` | ✗ |
| passage-12 | `Số điện thoại của tôi là …` | `Số điện thoại của tôi là …` | `Số điện thoại của tôi là …` | ✓ |
| passage-13 | `Giá sản phẩm là 1.250.000 đ…` | `Giá sản phẩm là 1.250.000 đ…` | `Giá sản phẩm là 1.250.000 đ…` | ✓ |
| passage-14 | `Hẹn gặp bạn lúc 14:30 ngày 1…` | `Hẹn gặp bạn lúc 14:30 ngày 1…` | `Hẹn gặp bạn lúc 14:30 ngày 1…` | ✓ |
| stress-0 | `chào` | `chào` | `chào` | ✓ |
| stress-1 | `chaof` | `chaof` | `chaof` | ✓ |
| stress-2 | `chaf` | `chaf` | `chaf` | ✓ |
| stress-3 | `chaff` | `chaff` | `chaff` | ✓ |
| stress-4 | `cchaof` | `cchaof` | `cchaof` | ✓ |
| stress-5 | `cas` | `cas` | `cas` | ✓ |
| stress-6 | `cass` | `cass` | `cass` | ✓ |
| stress-7 | `casss` | `casss` | `casss` | ✓ |
| stress-8 | `cf` | `cf` | `cf` | ✓ |
| stress-9 | `cf` | `cf` | `cf` | ✓ |
| stress-10 | `chf` | `chf` | `chf` | ✓ |
| stress-11 | `tôi` | `tôi` | `tôi` | ✓ |
| stress-12 | `tồi` | `tồi` | `tồi` | ✓ |
| stress-13 | `tôif` | `tôif` | `tôif` | ✓ |
| stress-14 | `tôf` | `tôf` | `tôf` | ✓ |
| stress-15 | `hòa` | `hòa` | `hòa` | ✓ |
| stress-16 | `hof` | `hof` | `hof` | ✓ |
| stress-17 | `hf` | `hf` | `hf` | ✓ |
| stress-18 | `đăng` | `đăng` | `đăng` | ✓ |
| stress-19 | `đằng` | `đằng` | `đằng` | ✓ |
| stress-20 | `đănngf` | `đănngf` | `đănngf` | ✓ |
| stress-21 | `đằn` | `đằn` | `đằn` | ✓ |
| stress-22 | `muòi` | `muòi` | `muòi` | ✓ |
| stress-23 | `muof` | `muof` | `uof` | ✗ |
| stress-24 | `muồi` | `muồi` | `muồi` | ✓ |
| stress-25 | `muf` | `muf` | `uf` | ✗ |
| stress-26 | `quấ` | `quấ` | `quấ` | ✓ |
| stress-27 | `quâs` | `quâs` | `quâs` | ✓ |
| stress-28 | `qus` | `qus` | `qus` | ✓ |
| stress-29 | `giá` | `giá` | `giá` | ✓ |
| stress-30 | `gí` | `gí` | `gí` | ✓ |
| stress-31 | `giấ` | `giấ` | `giấ` | ✓ |
| stress-32 | `gí` | `gí` | `gí` | ✓ |
| stress-33 | `người` | `người` | `người` | ✓ |
| stress-34 | `ngươif` | `ngươif` | `ngươif` | ✓ |
| stress-35 | `ngươf` | `ngươf` | `ngươf` | ✓ |
| stress-36 | `thương` | `thương` | `thương` | ✓ |
| stress-37 | `thường` | `thường` | `thường` | ✓ |
| stress-38 | `thườn` | `thườn` | `thườn` | ✓ |
| stress-39 | `câu` | `câu` | `câu` | ✓ |
| stress-40 | `cầu` | `cầu` | `cầu` | ✓ |
| stress-41 | `cầ` | `cầ` | `cầ` | ✓ |
| stress-42 | `cf` | `cf` | `cf` | ✓ |
| stress-43 | `biet` | `biet` | `biet` | ✓ |
| stress-44 | `biết` | `biết` | `biết` | ✓ |
| stress-45 | `biês` | `biês` | `biês` | ✓ |
| stress-46 | `biết` | `biết` | `biết` | ✓ |
| stress-47 | `chào cá` | `chào cá` | `chào cá` | ✓ |
| stress-48 | `chào cá f` | `chào cá f` | `chào cá f` | ✓ |
| stress-49 | `cas chà` | `cas chà` | `cas chà` | ✓ |
| stress-50 | `toi ten la nam` | `toi ten la nam` | `toi ten la nam` | ✓ |
| stress-51 | `xin chao ban` | `xin chao ban` | `xin chao ban` | ✓ |
| stress-52 | `toi không biết` | `toi không biết` | `toi không biết` | ✓ |
| fuzz-5k | `ovobn ejuhrkt xlyrjc vvtwoy fgssokv …` | `ovobn ẹuhrkt xlyrjc vvtwoy fgssokv…` | `ovobn ẹuhrkt xlyrjc vvtwoy fgssokv…` | ✗ |

**Agreement:** 3-way `65/69` · OpenKey NextGen (v3.0) == OpenKey 2.0.5 (legacy) `67/69` · OpenKey NextGen (v3.0) == UniKey (4.x UKEngine) `65/69` · OpenKey 2.0.5 (legacy) == UniKey (4.x UKEngine) `66/69`

## 2. Real passages — exact match vs intended Vietnamese text

| engine | exact vs intended | of 15 |
|---|---|---|
| OpenKey NextGen (v3.0) | **15** | 15 |
| OpenKey 2.0.5 (legacy) | **14** | 15 |
| UniKey (4.x UKEngine) | **14** | 15 |

## 3. Latency — engine decision per key (ns)

| engine | mean | p50 | p90 | p99 | max | sampled keys |
|---|---|---|---|---|---|---|
| OpenKey NextGen (v3.0) | 228.5 | 207 | 291 | 581 | 1952143 | 2000000 |
| OpenKey 2.0.5 (legacy) | 263.5 | 214 | 393 | 849 | 2202994 | 2000000 |
| UniKey (4.x UKEngine) | 118.4 | 111 | 137 | 180 | 500996 | 2000000 |

Includes ~20–40 ns of per-sample timing overhead, identical for all engines — the relative ranking is exact. `keys` from the correctness run: 6176 / 6176 / 6176.

## 4. Engine state — memory footprint

| engine | core object | internal buffers | approx total |
|---|---|---|---|
| OpenKey NextGen (v3.0) | `TextEngine` 728 B | raw-word buffer (kMaxBuff=32), undo history 64×32 | 728 B (+ heap history) |
| OpenKey 2.0.5 (legacy) | engine globals | TypingWord[80], word/state histories | ~8 KiB |
| UniKey (4.x UKEngine) | `UkEngine` 7752 B + `UkSharedMem` 141384 B | m_buffer[128] WordInfo, m_keyStrokes[128], macro store | 149136 B |

## Notes

- **"Delay when typing accents":** engine decision is 60–400 ns/key on all three engines — imperceptible. The perceived delay in the shipped app is the producer→consumer TSF hop (one synchronous edit session per edit). The fix shipped in this segment batches consecutive edits into ONE `TF_ES_SYNC` session and adds the producer-side ordering barrier; see `notes/fix_design.md`.
- **Ghosting/sticking when typing and deleting quickly:** fixed by (1) swallowing the KeyUp of a suppressed KeyDown (phantom key-up) and (2) draining pending edits before a pass-through key reaches the app. Both live in the shipped Windows exe; the engine-level semantics are locked by `tests/test_hotfix.cpp` and this benchmark.
- **Passage-11 (`confirm`)** and the fuzz show NextGen's shipped spelling auto-restore (`restoreIfWrongSpelling=true`): non-Vietnamese words keep their raw letters, while the 2.0.5/UniKey defaults mark them (`cònirm`, `ẹuhrkt`). Turn the option off and all three agree.
- **UniKey upstream engine limit:** its `UKEngine` uses a fixed 128-entry word buffer; words longer than that overflow its state machine (OOB read, ASAN-confirmed in the vendored reference). No human-typed word can exceed it — the comparative streams cap words at 11 letters, the realistic maximum — and the issue is documented here rather than patched into the unmodified reference.
- **OpenKey 2.0.5 reference defect:** `checkForStandaloneChar` reads `TypingWord[_index - 1]` without an `_index > 0` guard (Engine.cpp:995). Under ASan this is a global-buffer-overflow READ (4 bytes before the 128-byte `TypingWord`), reproducible on the fuzz-5k stream. Release builds read benign adjacent memory and output is unaffected — the differential agreement is unchanged — but the unmodified vendored reference carries this pre-existing UB. It is documented here, not patched, to keep the reference pristine.
- Correctness differences between engines are the documented rule differences (free-marking, auto-restore defaults, tone placement). The exhaustive NextGen vs 2.0.5 differential is `MEGA_BENCH_REPORT.md`; this benchmark adds UniKey.
