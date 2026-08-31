> **Lineage note (KieeKey v1.0 packaging):** Historical engineering
> document from the refactor lineage that produced KieeKey v1.0 (a fork of
> OpenKey, GPL-3.0). Written before the v1.0 release unification, it uses the
> pre-release working name "OpenKey NextGen" and internal milestone numbers
> (v3.0-v3.4). Published verbatim for traceability - see
> [docs/reports/README.md](README.md).

# Accent-latency A/B — TSF sessions per keystroke stream

Same shipped `TextEngine`, byte-identical streams; only the consumer grouping differs (ARM A: one session per edit, ARM B: one session per consecutive-edit burst). Modeled cost: 100 us/session + 4 us/delta, identical constants in both arms.

| stream | edits | sessions A | sessions B | A (us) | B (us) |
|---|---|---|---|---|---|
