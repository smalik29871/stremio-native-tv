"""Writes one part of tests/smoke.sh, so the scripted runs can be split across machines.

    python3 -I tests/shard.py tests/smoke.sh K N > part.sh   (K of N, from 1)

The runs are cut into N stretches of about the same length (a run's frame count stands in
for its time), each kept in order. A cut goes only before a run whose previous line closes a
check, never inside an if/for block or a heredoc, so a run keeps its setup and its checks,
and never before a run that picks up an earlier run's profile (HOME="$OUT/home_...").
Each part has the script's header and closing screenshot step, and the variables set in the
stretches before it (the lines that only assign one)."""
import os
import re
import sys

ASSIGN = re.compile(r"^[A-Z][A-Z0-9_]*=")
REUSE = re.compile(r'HOME="\$OUT/home_')   # continues the run before it: same part
SETUP = re.compile(r"^(mkdir|echo|cp|sed|rm|for |touch|cat|printf|\(|\s|[A-Z][A-Z0-9_]*=)")   # "(" and indented: a helper started in the background


def main():
    path, k, n = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    lines = open(path).read().split("\n")
    first = next(i for i, l in enumerate(lines) if l.startswith("run "))
    last = max(i for i, l in enumerate(lines) if l.startswith('python3 -I - "$OUT" <<'))   # the screenshots
    head, body, tail = lines[:first], lines[first:last], lines[last:]
    head = [re.sub(r'^ROOT=.*', 'ROOT="%s"' % os.path.dirname(os.path.dirname(os.path.abspath(path))), l) for l in head]

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
    total = sum(weight)
    bounds, acc, want = [0], 0, 1
    for i, w in enumerate(weight):
        if want < n and i in cuts and acc >= total * want / n:
            bounds.append(i)
            want += 1
        acc += w
    bounds += [len(body)] * (n + 1 - len(bounds))
    lo, hi = bounds[k - 1], bounds[k]
    carried = [l for i, l in enumerate(body[:lo]) if ASSIGN.match(l) and not inside[i]]
    out = head + ["# part %d of %d: variables from the runs before it" % (k, n)] + carried + body[lo:hi] + tail
    sys.stdout.write("\n".join(out))


main()
