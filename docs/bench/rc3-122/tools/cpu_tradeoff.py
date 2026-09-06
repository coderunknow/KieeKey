#!/usr/bin/env python3
"""Measure process CPU% (wall-normalized jiffies) of an e2e_bench run.

Usage: cpu_tradeoff.py <e2e_binary> <spin_us> <model>

Samples /proc/<pid>/task/*/stat every ~50 ms and reports mean/peak of the
instantaneous CPU fraction. Wall time is intentionally NOT recomputed from
t0/t1 (that was the bug in the first version); the sweep table in the RC3
report records wall separately from run logs.

Note the process also watches a 1-CPU host: "peak" values >100% are the
sampler's instantaneous denominator (a fraction of ONE CPU wall per 50 ms
interval), not multi-core parallelism.
"""
import subprocess, sys, os, glob, time

binary, spin, model = sys.argv[1], sys.argv[2], sys.argv[3]
p = subprocess.Popen([binary, "--keys=100000", "--runs=1",
                      f"--spin={spin}", f"--model={model}"],
                     cwd=os.getcwd(),
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
h = os.sysconf("SC_CLK_TCK")

def cpu(pid):
    total = 0
    try:
        for t in glob.glob(f"/proc/{pid}/task/*/stat"):
            with open(t) as f:
                d = f.read().split()
            total += int(d[13]) + int(d[14])
    except Exception:
        pass
    return total

t0 = time.time(); c0 = cpu(p.pid)
samples = []
while True:
    time.sleep(0.05)
    t1 = time.time(); c1 = cpu(p.pid)
    if t1 - t0 > 0.04:
        samples.append((c1 - c0) / h / (t1 - t0))
        t0 = t1; c0 = c1
    if p.poll() is not None:
        break
t1 = time.time(); c1 = cpu(p.pid)
if t1 - t0 > 0.04:
    samples.append((c1 - c0) / h / (t1 - t0))
if samples:
    mean = sum(samples) / len(samples)
    peak = max(samples)
    print(f"spin={spin} model={model} wall=manual mean_cpu={mean*100:.1f}% "
          f"peak_cpu={peak*100:.1f}% n={len(samples)}")
else:
    print(f"spin={spin} model={model} no samples")
