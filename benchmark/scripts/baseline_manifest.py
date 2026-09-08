#!/usr/bin/env python3
"""Freeze what a campaign measured: sources, flags, data, host facts.

Writes docs/bench/rc1-130/baseline_manifest.json (+ a human-readable .md next to
it). The manifest exists so that a report can never quietly describe a different
build than the one it measured:

  * engine_sources_sha256          — src/core right now (what this campaign built)
  * baseline_engine_sources_sha256 — the pristine v1.2.2 copy that builds
                                     libkkbase.so, read from
                                     benchmark/reference/kieekey-1.2.2. Regenerating
                                     the manifest can therefore NEVER redefine the
                                     baseline, which is the mistake this file's
                                     first version made.
  * toolchain                      — read back OUT OF benchmark/scripts/build.sh,
                                     so the manifest cannot claim -O3 while the
                                     build uses -O2.

usage: baseline_manifest.py [--out=docs/bench/rc1-130]
"""
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ENGINE = ["TextEngine.cpp", "TextEngine.hpp", "VietnameseTables.hpp", "FlatTables.hpp",
          "ConsonantWalks.hpp"]


def sha256(rel):
    with open(os.path.join(ROOT, rel), "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def hash_tree(rel):
    h = hashlib.sha256()
    base = os.path.join(ROOT, rel)
    for dirpath, dirnames, files in os.walk(base):
        dirnames.sort()
        for fn in sorted(files):
            p = os.path.join(dirpath, fn)
            h.update(os.path.relpath(p, base).encode())
            with open(p, "rb") as f:
                h.update(f.read())
    return {"tree_sha256": h.hexdigest(), "files": sum(1 for _ in _walk(base))}


def _walk(base):
    for dirpath, dirnames, files in os.walk(base):
        dirnames.sort()
        for fn in sorted(files):
            yield os.path.join(dirpath, fn)


def git(*args):
    try:
        return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True,
                              check=True).stdout.strip()
    except Exception:
        return "?"


def read_version():
    try:
        with open(os.path.join(ROOT, "VERSION"), encoding="utf-8") as f:
            return f.read().strip()
    except OSError:
        return "?"


def env_facts():
    env = {}
    try:
        env["compiler"] = subprocess.run(["g++", "--version"], capture_output=True, text=True,
                                         check=True).stdout.splitlines()[0]
    except Exception:
        env["compiler"] = "NOT AVAILABLE"
    buildsh = ""
    try:
        with open(os.path.join(ROOT, "benchmark/scripts/build.sh"), encoding="utf-8") as f:
            buildsh = f.read()
    except OSError:
        pass

    def default(var):
        m = re.search(r'^' + var + r'="\$\{[A-Z_]+:-(.*?)\}"', buildsh, re.M)
        return m.group(1) if m else "?"

    env["std"] = default("STD")
    env["opt"] = default("OPT")
    env["defines_bench"] = default("DEF")
    env["note_flags"] = ("these are what build.sh uses for EVERY engine TU; the product's "
                         "CMake Release configuration is -O3 -DNDEBUG, and the manifest is "
                         "generated from the build script rather than typed, so the two "
                         "cannot drift apart silently")
    env["lto"] = "OFF for every engine (LTO across a dlopen boundary would be an asymmetry)"
    env["pgo"] = "NOT USED (a PGO build trained on the benchmark corpus would be benchmark overfitting by construction)"
    env["warn_flags_bench"] = "-w (bench builds are warning-silent by design; the engine itself is compiled with the repo's own CMake warning set for product builds)"
    env["kernel"] = platform.platform()
    env["machine"] = platform.machine()
    env["python"] = platform.python_version()
    try:
        env["cpu_model"] = re.sub(r"\s+", " ", open("/proc/cpuinfo", encoding="utf-8").read()
                                  .split("model name")[1].split("\n")[0].strip(": ").strip())
    except Exception:
        env["cpu_model"] = "NOT AVAILABLE"
    try:
        env["cores_online"] = os.cpu_count()
        env["affinity"] = len(os.sched_getaffinity(0))
    except Exception:
        pass
    for name, path_ in (("governor", "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"),
                        ("min_freq_khz", "/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq"),
                        ("max_freq_khz", "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"),
                        ("thp", "/sys/kernel/mm/transparent_hugepage/enabled"),
                        ("mitigations", "/sys/devices/system/cpu/vulnerabilities/meltdown"),
                        ("nohz_", "/proc/cmdline")):
        try:
            env[name] = open(path_, encoding="utf-8").read().strip()
        except OSError:
            env[name] = "NOT AVAILABLE (not exposed in this container)"
    try:
        env["loadavg_at_manifest"] = open("/proc/loadavg", encoding="utf-8").read().strip()
    except OSError:
        pass
    env["host_note"] = ("containerised host; perf_event_open is blocked by seccomp and "
                        "valgrind is not installed, so hardware counters and callgrind-style "
                        "attribution are recorded as NOT AVAILABLE rather than substituted")
    return env


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="docs/bench/rc1-130")
    a = ap.parse_args()
    os.chdir(ROOT)
    out_dir = a.out
    os.makedirs(out_dir, exist_ok=True)

    sources = {}
    for name in ENGINE + sorted(
            f for f in os.listdir(os.path.join(ROOT, "src/core")) if f.endswith((".hpp", ".cpp"))):
        p = f"src/core/{name}"
        if os.path.isfile(os.path.join(ROOT, p)) and p not in sources:
            sources[p] = sha256(p)
    baseline = {}
    ref_dir = "benchmark/reference/kieekey-1.2.2"
    if os.path.isdir(os.path.join(ROOT, ref_dir)):
        for fn in sorted(os.listdir(os.path.join(ROOT, ref_dir))):
            if fn in ("UPSTREAM-SHA256.txt", "README.md"):
                continue
            rel = f"src/core/{fn}"
            baseline[rel] = sha256(f"{ref_dir}/{fn}")

    dirty = git("status", "--porcelain", "--", "src", "CMakeLists.txt", "VERSION")
    env = env_facts()
    manifest = {
        "schema": "kieekey-bench-baseline/1",
        "product_version": read_version(),
        "git": {"git_head": git("rev-parse", "HEAD"), "git_head_short": git("rev-parse", "--short", "HEAD"),
                "branch": git("rev-parse", "--abbrev-ref", "HEAD"),
                "dirty_src": dirty or "clean"},
        "engine_sources_sha256": sources,
        "baseline_engine_sources_sha256": baseline,
        "manifest_note": ("regenerate freely per campaign; the baseline half always comes from the "
                          "hash-verified pristine reference copy, never from the working tree"),
        "data": {"tests/data/viet74k.txt": sha256("tests/data/viet74k.txt"),
                 "benchmark/harness/viet_table.inc": sha256("benchmark/harness/viet_table.inc")},
        "harness": hash_tree("benchmark/harness"),
        "scripts": hash_tree("benchmark/scripts"),
        "toolchain": {k: env.get(k) for k in ("compiler", "std", "opt", "defines_bench", "lto",
                                              "pgo", "warn_flags_bench")},
        "environment": env,
    }
    body = json.dumps(manifest, indent=1, sort_keys=True) + "\n"
    with open(os.path.join(out_dir, "baseline_manifest.json"), "w", encoding="utf-8") as f:
        f.write(body)
    with open(os.path.join(out_dir, "baseline_environment.txt"), "w", encoding="utf-8") as f:
        for k, v in sorted(env.items()):
            f.write(f"{k}={v}\n")
    print(f"[manifest] head={manifest['git']['git_head_short']} dirty_src={manifest['git']['dirty_src'] or 'clean'!r}")
    print(f"[manifest] {len(sources)} tree / {len(baseline)} baseline engine sources; "
          f"flags={env['std']} {env['opt']} {env['defines_bench']}")
    print(f"[manifest] wrote {out_dir}/baseline_manifest.json (sha256 {hashlib.sha256(body.encode()).hexdigest()[:16]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
