#!/usr/bin/env python3
import concurrent.futures
import re
import subprocess
import sys

botA = sys.argv[1]
botB = sys.argv[2]
total_seeds = int(sys.argv[3]) if len(sys.argv) > 3 else 24
workers = int(sys.argv[4]) if len(sys.argv) > 4 else 8
base_seed = int(sys.argv[5]) if len(sys.argv) > 5 else 1

shards = []
per = total_seeds // workers
rem = total_seeds % workers
cur = base_seed
for w in range(workers):
    cnt = per + (1 if w < rem else 0)
    if cnt > 0:
        shards.append((cur, cnt))
        cur += cnt

def run_shard(arg):
    start_s, cnt = arg
    p = subprocess.run(["./sim_arena", botA, botB, str(cnt), "-1", str(start_s)],
                       capture_output=True, text=True, check=True)
    return p.stdout

wA = d = lA = 0
maxMsA = maxMsB = 0.0
with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as ex:
    for out in ex.map(run_shard, shards):
        m = re.search(r"Bot A .*?: (\d+)W / (\d+)D / (\d+)L .*? maxTurnMs=([0-9.]+)", out)
        if m:
            wA += int(m.group(1))
            d += int(m.group(2))
            lA += int(m.group(3))
            maxMsA = max(maxMsA, float(m.group(4)))
        m2 = re.search(r"Bot B .*? maxTurnMs=([0-9.]+)", out)
        if m2:
            maxMsB = max(maxMsB, float(m2.group(1)))

tot = wA + d + lA
pct = 100.0 * (wA + 0.5 * d) / max(1, tot)
print(f"BENCHMARK ({tot} games): {botA} vs {botB} => {wA}W / {d}D / {lA}L ({pct:.1f}%) | maxMs=[{maxMsA:.2f}, {maxMsB:.2f}]")
