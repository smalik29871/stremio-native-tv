"""Writes one part of tests/smoke.sh, so the scripted runs can be split across machines.

    python3 -I tests/shard.py tests/smoke.sh K N > part.sh   (K of N, from 1)

The runs are cut into N stretches of about the same length (a run's frame count stands in
for its time), each kept in order. A cut goes only before a run whose previous line closes a
check, never inside an if/for block or a heredoc, so a run keeps its setup and its checks,
and never before a run that picks up an earlier run's profile (HOME="$OUT/home_...").
Each part has the script's header and closing screenshot step, and the variables set in the
stretches before it (the lines that only assign one).

Two settings from the environment, for working on one thing (CI uses neither):
  SMOKE_ONLY=regex    only the stretches with a run whose name matches (each with its setup,
                      its checks and the runs it continues from), split K of N as before
  SMOKE_KEEP_GOING=1  a failed check is counted and the part goes on to the next run; it fails
                      at the end, listing each run whose checks failed"""
import os
import re
import sys

ASSIGN = re.compile(r"^[A-Z][A-Z0-9_]*=")
REUSE = re.compile(r'HOME="\$OUT/home_')   # continues the run before it: same part
SETUP = re.compile(r"^(mkdir|echo|cp|sed|rm|for |touch|cat|printf|\(|\s|[A-Z][A-Z0-9_]*=)")   # "(" and indented: a helper started in the background


def main():
    path, k, n = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    lines = open(path, encoding="utf-8").read().split("\n")
    first = next(i for i, l in enumerate(lines) if l.startswith("run "))
    last = max(i for i, l in enumerate(lines) if l.startswith('python3 -I - "$OUT" <<'))   # the screenshots
    head, body, tail = lines[:first], lines[first:last], lines[last:]
    root = os.path.dirname(os.path.dirname(os.path.abspath(path))).replace("\\", "/")
    head = [re.sub(r'^ROOT=.*', lambda _: 'ROOT="%s"' % root, l) for l in head]

    cuts, weight, inside, depth, heredoc, prev = [], [], [], 0, None, ""
    for i, l in enumerate(body):
        w = 0
        inside.append(bool(heredoc))
        if heredoc:
            if l.strip() == heredoc:
                heredoc = None
        else:
            m = re.search(r"<<'?(\w+)'?", l)
            if m:
                heredoc = m.group(1)
            if re.match(r"^(if|for|while) ", l) and not re.search(r";\s*(fi|done)\s*$", l):
                depth += 1
            elif re.match(r"^(fi|done)\b", l) or re.match(r"^else\b.*;\s*fi\s*$", l):
                depth -= 1
            if l.lstrip().startswith("run "):
                f = l.split()[2] if len(l.split()) > 2 else "120"
                w = 60 + (int(f) if f.isdigit() else 120)   # start-up plus its frames
                if l.startswith("run ") and depth == 0 and i and not SETUP.match(prev) and not REUSE.search(l) and not prev.startswith("run "):   # a run whose check has not come yet stays with it
                    cuts.append(i)
        weight.append(w)
        if l.strip() and not l.lstrip().startswith("#"):
            prev = l
    # stretches that can stand alone (a cut starts each), those wanted, then this part's share of them
    starts = [0] + cuts
    units = [(a, b) for a, b in zip(starts, starts[1:] + [len(body)]) if a < b]
    only = os.environ.get("SMOKE_ONLY")
    if only:
        names = re.compile(only)
        units = [(a, b) for a, b in units if any(names.search(m.group(1)) for l in body[a:b] for m in [re.match(r"^\s*\(?\s*run (\S+) ", l)] if m)]
    total = sum(sum(weight[a:b]) for a, b in units)
    parts, acc, want = [[] for _ in range(n)], 0, 0
    for a, b in units:
        if want < n - 1 and parts[want] and acc >= total * (want + 1) / n:
            want += 1
        parts[want].append((a, b))
        acc += sum(weight[a:b])
    mine = parts[k - 1]
    keep = set(i for a, b in mine for i in range(a, b))
    first = mine[0][0] if mine else len(body)
    carried = [l for i, l in enumerate(body) if ASSIGN.match(l) and not inside[i] and i not in keep and (i < first or only)]
    lines = [l for a, b in mine for l in body[a:b]]
    if os.environ.get("SMOKE_KEEP_GOING"):   # a failed check counts, and the next run goes on
        head = [l.replace("; shift 3", '; shift 3; LAST_RUN="$name"').replace("exit 1; }", "failed_check; }") if l.startswith(("run() {", "  local name=", "    xvfb-run")) else l for l in head]
        head += ['SMOKE_FAILED=""',
                 'failed_check() { SMOKE_FAILED="$SMOKE_FAILED ${LAST_RUN:-?}"; echo "FAILED: ${LAST_RUN:-?} (going on)"; }']
        lines = [l.replace("exit 1; }", "failed_check; }") for l in lines]
        tail = tail + ['[ -z "$SMOKE_FAILED" ] || { echo "checks failed after:$SMOKE_FAILED"; exit 1; }']
    out = head + ["# part %d of %d%s: variables from the runs before it" % (k, n, " (runs matching %s)" % only if only else "")] + carried + lines + tail
    sys.stdout.write("\n".join(out))


main()
