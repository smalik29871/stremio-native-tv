#!/usr/bin/env python3
"""The release notes of one version, from CHANGELOG.md, grouped by the device each note is for.

  python3 tools/release_notes.py 0.10.100 [--mobile] [CHANGELOG.md]

A note is a "- " item; a [phone], [tv] or [desktop] tag opens it ([phone,tv] names several, no tag is for
everyone: the same tags the app reads, src/whatsnew_notes.h). A changelog section with no tags prints as it
stands. --mobile: a mobile release, so only what is for everyone and for phones."""
import re
import sys

GROUPS = [
    ("all", "For everyone"),
    ("phone", "Android phones and tablets"),
    ("tv", "TVs (LG webOS, Samsung, Android TV, Fire TV)"),
    ("desktop", "Windows and computers"),
]
TAGS = {"phone": "phone", "mobile": "phone", "tv": "tv", "desktop": "desktop", "all": "all"}


def section(text, version):
    """The lines under "## version", up to the next "## "."""
    out, on = [], False
    for line in text.splitlines():
        if line == "## " + version:
            on = True
            continue
        if line.startswith("## ") and on:
            break
        if on:
            out.append(line)
    while out and not out[-1].strip():
        out.pop()
    while out and not out[0].strip():
        out.pop(0)
    return out


def tags_of(first):
    """("phone,tv" tags, the line without them) for "- [phone,tv] **A.** b"; no tag: ({"all"}, unchanged)."""
    m = re.match(r"^(- )\[([a-z, ]{1,40})\] ?(.*)$", first)
    if not m:
        return {"all"}, first
    names = [t.strip() for t in m.group(2).split(",")]
    if not names or any(t not in TAGS for t in names):
        return {"all"}, first
    return {TAGS[t] for t in names}, m.group(1) + m.group(3)


def notes(lines):
    """[(tags, [lines])] for each "- " item (and any text before the first one)."""
    items = []
    for line in lines:
        if line.startswith("- ") or not items:
            tags, line = tags_of(line)
            items.append((tags, [line]))
        elif line.strip():
            items[-1][1].append(line)
    return items


def render(version, text, mobile=False):
    lines = section(text, version)
    items = notes(lines)
    if not any(tags != {"all"} for tags, _ in items):   # nothing tagged: as it stands
        return "\n".join(lines) + ("\n" if lines else "")
    out = []
    for key, title in GROUPS:
        if mobile and key not in ("all", "phone"):
            continue
        mine = [body for tags, body in items if key in tags]
        if not mine:
            continue
        if out:
            out.append("")
        out.append("### " + title)
        out.append("")
        for body in mine:
            out.extend(body)
    return "\n".join(out) + "\n"


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    path = args[1] if len(args) > 1 else "CHANGELOG.md"
    with open(path, encoding="utf-8") as f:
        sys.stdout.write(render(args[0], f.read(), "--mobile" in argv))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
