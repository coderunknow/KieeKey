# Frozen KieeKey v1.2.1 RC1 engine (reference copy)

Byte-for-byte copy of `src/core/{TextEngine.cpp,TextEngine.hpp,
VietnameseTables.hpp,FlatTables.hpp}` at commit `0884c64` (the v1.2.1 RC1
release). **Do not edit.**

Purpose: `tests/diff_engine_ab.cpp` compiles this copy with the namespace
renamed (`-Dok=ok_rc1`) and links it next to the live engine, then feeds both
identical randomized keystreams across the full option matrix and compares
every per-event decision (code, backspace count, replacement text, macro
expansion, visible account). It is the regression net for the RC2 engine
optimizations: any semantic drift versus RC1 is a test failure, not a
benchmark footnote.

License: GPL-3.0-or-later (same as the live sources).
