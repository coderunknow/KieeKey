# Revised beta2 renderer snapshots

Generated 2026-09-18 from the C++ engine (`tests/capture_web_frames.py`), then
rasterized by the shipped `web/arcade.js` with `@napi-rs/canvas`.

These demonstrate the **web Canvas2D passage renderer**, not screenshots of a
real Windows session or evidence of external-editor IME behavior. Captured
scores/WPM reflect the automated input driver, not measured human gameplay.

- [Typing Race](typing-race.png) — colored runs share the same cell grid.
- [Fishing](fishing.png) — caret and feedback panel.
- [No-Mistake](no-mistake.png) — bounded passage and reserve meter.

Native geometry is separately covered by the Win32 recording-shim matrix
(4 passage games × 4 DPIs × 90 evolving frames). Manual Windows checks remain
in `docs/BETA2_TESTING.md`.
