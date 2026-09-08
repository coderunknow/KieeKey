#!/usr/bin/env bash
# Materialise a campaign's *release trail* under docs/bench/rc1-130/<name>/.
#
# Deliberately not the raw artifacts: a campaign's raw/ directory is 6-13 MB of
# JSON Lines and belongs in benchmark/results/ (which is what a re-analysis
# reads). What the documentation tree needs is the layer a reviewer follows
# first — the tables, the summary the report was generated from, the gate log
# that made the numbers admissible, the host facts, and the manifest that says
# which tree was measured.
#
# Usage: benchmark/scripts/rc1_trail.sh rc1-130 rc1-cc1 rc1-ca4
set -u
cd "$(dirname "$0")/../.." || exit 1
for name in "$@"; do
    src="benchmark/results/$name"
    dst="docs/bench/rc1-130/$name"
    if [[ ! -d "$src" ]]; then
        echo "[trail] $name: no such campaign directory ($src) — skipped" >&2
        continue
    fi
    mkdir -p "$dst"
    copied=0
    for f in tables.md summary.json environment.txt manifest_at_build.json \
             logs/gates.txt logs/attrib-guard.log logs/build.log; do
        if [[ -s "$src/$f" ]]; then
            # -s, not -f: an empty log means the step did not run, and a trail
            # must not quietly replace a complete artifact with a zero-byte one
            install -m 0644 "$src/$f" "$dst/$(basename "$f")" && copied=$((copied + 1))
        elif [[ -f "$src/$f" ]]; then
            echo "[trail] $name: $f is empty — not copied" >&2
        fi
    done
    if [[ ! -s "$dst/tables.md" ]]; then
        echo "[trail] $name: tables.md empty or missing — trail not trusted" >&2
        exit 1
    fi
    echo "[trail] $name: $copied artifact(s) -> $dst"
done
