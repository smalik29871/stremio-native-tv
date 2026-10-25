"""Every log a part of tests/smoke.sh reads must be written by a run in that same part, at
every split CI may use (4 machines, each 1-4 lanes): a part cut between a run and its check
fails on a machine that never ran it.   python3 -I tests/check_shards.py tests/smoke.sh"""
import re
import subprocess
import sys

script = sys.argv[1]
shard = script.replace("smoke.sh", "shard.py")
bad = []
for n in (4, 8, 12, 16):
    for k in range(1, n + 1):
        part = subprocess.run([sys.executable, "-I", shard, script, str(k), str(n)], capture_output=True, text=True, check=True).stdout
        made = set(re.findall(r"^\s*\(?\s*run (\S+) ", part, re.M)) | set(re.findall(r"idle_rate (\S+) ", part)) | set(re.findall(r'>\s*"\$OUT/(\w+)\.log"', part)) | {"datastore_put", "fake_track"}
        loops = set(re.findall(r"run (\w+)\$\w+ ", part))   # run name$f inside a for loop
        for pid in set(re.findall(r"^wait \$(\w+)", part, re.M)):   # a helper started in the background for a run
            if not re.search(r"\b%s=\$!" % pid, part):
                bad.append(f"{k}/{n}: wait ${pid}")
        for log in set(re.findall(r"\$OUT/([A-Za-z0-9_]+)\.log", part)):
            if log not in made and not any(log.startswith(p) for p in loops):
                bad.append(f"{k}/{n}: {log}.log")
if bad:
    print("parts that read a log another part writes:", ", ".join(bad))
    sys.exit(1)
print("every split keeps each run with its checks")
